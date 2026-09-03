#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Shimmer intervals, matching the order of the parameter's choice list.
constexpr std::array<float, 9> kShimmerIntervals { -12.0f, -7.0f, -5.0f, 0.0f,
                                                    5.0f, 7.0f, 12.0f, 19.0f, 24.0f };
} // namespace

SillageAudioProcessor::SillageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
}

void SillageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    wetBuffer.setSize (2, samplesPerBlock);

    mixSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);

    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));

    // Zero latency: the granular loop shifter reads from a delay line rather
    // than looking ahead, so nothing here delays the dry path.
    setLatencySamples (0);
}

void SillageAudioProcessor::releaseResources() {}

bool SillageAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Mono-in upmixes cleanly to stereo-out.
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

double SillageAudioProcessor::getTailLengthSeconds() const
{
    // The feedback buffer holds up to 10 s of audio.
    return kBufferSeconds;
}

void SillageAudioProcessor::updateTransport()
{
    auto bpm = (double) parameterValue (params::id::fallbackBpm);
    auto beatsPerBar = 4.0;

    if (auto* playhead = getPlayHead())
    {
        if (auto position = playhead->getPosition(); position.hasValue())
        {
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            // The bar division is only correct in odd metres if we ask.
            if (auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    beatsPerBar = 4.0 * (double) signature->numerator
                                      / (double) signature->denominator;
        }
    }

    effectiveBpm.store (bpm);
    barBeats.store (beatsPerBar);
}

GrainEngine::Settings SillageAudioProcessor::resolveGrainSettings() const
{
    GrainEngine::Settings settings;

    double timeSeconds = (double) parameterValue (params::id::time) * 0.001;

    if (parameterValue (params::id::timeSync) >= 0.5f)
    {
        const auto index = juce::jlimit (0, (int) params::kDivisions.size() - 1,
                                         (int) parameterValue (params::id::timeDivision));
        const auto& division = params::kDivisions[(size_t) index];

        // kBarDivision is a negative sentinel: a bar's length is only known
        // once the host time signature is in.
        const auto beats = division.beats < 0.0 ? barBeats.load() : division.beats;
        timeSeconds = beats * 60.0 / juce::jmax (1.0, effectiveBpm.load());
    }

    // Never ask for more delay than the buffer holds.
    timeSeconds = juce::jlimit (0.001, kBufferSeconds * 0.98, timeSeconds);

    settings.timeSamples = timeSeconds * currentSampleRate;
    settings.density     = (double) parameterValue (params::id::density);
    settings.spread      = parameterValue (params::id::spread) * 0.01f;
    settings.sizeSamples = (double) parameterValue (params::id::size) * 0.001 * currentSampleRate;
    settings.window      = (WindowShape) juce::jlimit (
        0, (int) WindowShape::numShapes - 1, (int) parameterValue (params::id::window));
    settings.feedback    = parameterValue (params::id::feedback) * 0.01f;

    settings.pitchSemitones = parameterValue (params::id::pitch)
                            + parameterValue (params::id::pitchFine) * 0.01f;
    settings.pitchSpread    = parameterValue (params::id::pitchSpread) * 0.01f;
    settings.quantize       = (scales::Scale) juce::jlimit (
        0, (int) scales::Scale::numScales - 1, (int) parameterValue (params::id::quantize));
    settings.quantizeRoot   = juce::jlimit (0, 11, (int) parameterValue (params::id::quantizeRoot));
    settings.panSpread      = parameterValue (params::id::panSpread) * 0.01f;
    settings.reverseProbability = parameterValue (params::id::reverse) * 0.01f;

    return settings;
}

FeedbackPath::Settings SillageAudioProcessor::resolveFeedbackSettings() const
{
    FeedbackPath::Settings settings;

    settings.highpassHz = parameterValue (params::id::fbHighpass);
    settings.lowpassHz  = parameterValue (params::id::fbLowpass);
    settings.resonance  = parameterValue (params::id::fbResonance) * 0.01f;

    const auto index = juce::jlimit (0, (int) kShimmerIntervals.size() - 1,
                                     (int) parameterValue (params::id::shimmerInterval));
    auto shimmer = kShimmerIntervals[(size_t) index]
                 + parameterValue (params::id::shimmerFine) * 0.01f;

    // Handoff open question 5: when Quantize is on the loop shimmer snaps too,
    // otherwise a +7 against a minor scale stops being musical after two passes.
    const auto scale = (scales::Scale) juce::jlimit (
        0, (int) scales::Scale::numScales - 1, (int) parameterValue (params::id::quantize));
    if (scale != scales::Scale::off)
        shimmer = scales::snapToScale (shimmer, scale,
                                       juce::jlimit (0, 11, (int) parameterValue (params::id::quantizeRoot)));

    settings.shimmerSemitones = shimmer;
    settings.shimmerAmount    = parameterValue (params::id::shimmerAmount) * 0.01f;
    settings.diffuse          = parameterValue (params::id::diffuse) * 0.01f;
    settings.satType          = juce::jlimit (0, 2, (int) parameterValue (params::id::satType));
    settings.drive            = parameterValue (params::id::drive) * 0.01f;

    return settings;
}

void SillageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numOut     = getTotalNumOutputChannels();
    const auto numIn      = getTotalNumInputChannels();

    for (int channel = numIn; channel < numOut; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples); // mono -> stereo upmix

    updateTransport();

    wetBuffer.setSize (numOut, numSamples, false, false, true);

    engine.getFeedbackPath().setSettings (resolveFeedbackSettings());
    engine.process (buffer.getArrayOfReadPointers(),
                    wetBuffer.getArrayOfWritePointers(),
                    numOut, numSamples, resolveGrainSettings());

    mixSmoothed.setTargetValue (parameterValue (params::id::mix) * 0.01f);
    outputGainSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (parameterValue (params::id::output)));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto mix     = mixSmoothed.getNextValue();
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);
        const auto outGain = outputGainSmoothed.getNextValue();

        for (int channel = 0; channel < numOut; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            data[i] = (data[i] * dryGain + wetBuffer.getSample (channel, i) * wetGain) * outGain;
        }
    }
}

juce::AudioProcessorEditor* SillageAudioProcessor::createEditor()
{
    return new SillageAudioProcessorEditor (*this);
}

void SillageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SillageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SillageAudioProcessor();
}
