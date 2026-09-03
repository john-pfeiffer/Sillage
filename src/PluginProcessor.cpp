#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Shimmer intervals, matching the order of the parameter's choice list.
constexpr std::array<float, 9> kShimmerIntervals { -12.0f, -7.0f, -5.0f, 0.0f,
                                                    5.0f, 7.0f, 12.0f, 19.0f, 24.0f };

// Chaos reach per destination at 100 % (5.4).
constexpr float kChaosFeedbackBoost   = 0.30f; // up to +30 % above the set value
constexpr float kChaosDensityRange    = 0.50f; // ±50 %
constexpr float kChaosPitchSemitones  = 12.0f;
constexpr float kChaosShimmerCents    = 50.0f;
} // namespace

SillageAudioProcessor::SillageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    apvts.addParameterListener (params::id::panic, this);
}

SillageAudioProcessor::~SillageAudioProcessor()
{
    apvts.removeParameterListener (params::id::panic, this);
}

void SillageAudioProcessor::parameterChanged (const juce::String& parameterId, float newValue)
{
    // Panic is momentary: a rising edge from the UI pulse or host automation
    // arms a clear that the audio thread performs at the top of its next block.
    if (parameterId == params::id::panic && newValue >= 0.5f)
        panicRequested.store (true);
}

void SillageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    wetBuffer.setSize (2, samplesPerBlock);

    mixSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);

    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, channels);
    onsetDetector.prepare (sampleRate);
    envelopeFollower.prepare (sampleRate);
    chaosModulator.prepare (sampleRate);
    duck.prepare (sampleRate);

    // Zero latency: the granular loop shifter reads from a delay line rather
    // than looking ahead, and the onset detector's reaction time delays the
    // responses, not the dry path.
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

GrainEngine::Settings SillageAudioProcessor::resolveGrainSettings (const ChaosValues& chaos,
                                                                   float envelope) const
{
    GrainEngine::Settings settings;

    const auto bpm       = effectiveBpm.load();
    const auto bar       = barBeats.load();
    const auto chaosAmt  = parameterValue (params::id::chaos) * 0.01f;
    const auto bipolar   = envelope * 2.0f - 1.0f; // -1 quiet .. +1 loud

    double timeSeconds = (double) parameterValue (params::id::time) * 0.001;
    if (parameterValue (params::id::timeSync) >= 0.5f)
        timeSeconds = params::divisionSeconds ((int) parameterValue (params::id::timeDivision), bpm, bar);

    // Never ask for more delay than the buffer holds.
    timeSeconds = juce::jlimit (0.001, kBufferSeconds * 0.98, timeSeconds);

    settings.timeSamples    = timeSeconds * currentSampleRate;
    settings.timeModSamples = (double) (chaosAmt * chaos.position) * settings.timeSamples;

    // Density: Chaos wanders it ±50 %, the input envelope scales it up to an
    // octave either way — positive means hits dense, sustains sparse.
    auto density = (double) parameterValue (params::id::density);
    density *= 1.0 + (double) (chaosAmt * chaos.density * kChaosDensityRange);
    if (const auto envDensity = parameterValue (params::id::envDensity) * 0.01f; std::abs (envDensity) > 1.0e-6f)
        density *= std::pow (2.0, (double) (envDensity * bipolar));
    settings.density = juce::jlimit (0.5, 500.0, density);

    auto spread = parameterValue (params::id::spread) * 0.01f;
    spread += parameterValue (params::id::envSpread) * 0.01f * bipolar * 0.5f;
    settings.spread = juce::jlimit (0.0f, 1.0f, spread);

    settings.sizeSamples = (double) parameterValue (params::id::size) * 0.001 * currentSampleRate;
    settings.window      = (WindowShape) juce::jlimit (
        0, (int) WindowShape::numShapes - 1, (int) parameterValue (params::id::window));

    // Chaos only ever pushes feedback up; at 100 % the loop goes past unity on
    // purpose and the limiter is what keeps that musical.
    settings.feedback = parameterValue (params::id::feedback) * 0.01f
                      + chaosAmt * kChaosFeedbackBoost * (chaos.feedback + 1.0f) * 0.5f;

    settings.pitchSemitones    = parameterValue (params::id::pitch)
                               + parameterValue (params::id::pitchFine) * 0.01f;
    settings.pitchModSemitones = chaosAmt * chaos.pitch * kChaosPitchSemitones;
    settings.pitchSpread       = parameterValue (params::id::pitchSpread) * 0.01f;
    settings.quantize          = (scales::Scale) juce::jlimit (
        0, (int) scales::Scale::numScales - 1, (int) parameterValue (params::id::quantize));
    settings.quantizeRoot      = juce::jlimit (0, 11, (int) parameterValue (params::id::quantizeRoot));
    settings.panSpread         = parameterValue (params::id::panSpread) * 0.01f;
    settings.reverseProbability = parameterValue (params::id::reverse) * 0.01f;

    settings.freeze            = parameterValue (params::id::freeze) >= 0.5f;
    settings.freezeFadeSamples = (double) parameterValue (params::id::freezeFade) * 0.001 * currentSampleRate;

    settings.sync = parameterValue (params::id::sync) >= 0.5f;
    settings.syncIntervalSamples = params::divisionSeconds (
        (int) parameterValue (params::id::grainDivision), bpm, bar) * currentSampleRate;
    settings.swing = parameterValue (params::id::swing) * 0.01f;

    return settings;
}

FeedbackPath::Settings SillageAudioProcessor::resolveFeedbackSettings (const ChaosValues& chaos) const
{
    FeedbackPath::Settings settings;

    settings.highpassHz = parameterValue (params::id::fbHighpass);
    settings.lowpassHz  = parameterValue (params::id::fbLowpass);
    settings.resonance  = parameterValue (params::id::fbResonance) * 0.01f;

    const auto index    = juce::jlimit (0, (int) kShimmerIntervals.size() - 1,
                                        (int) parameterValue (params::id::shimmerInterval));
    const auto chaosAmt = parameterValue (params::id::chaos) * 0.01f;

    auto shimmer = kShimmerIntervals[(size_t) index]
                 + (parameterValue (params::id::shimmerFine)
                    + chaosAmt * chaos.shimmerFine * kChaosShimmerCents) * 0.01f;

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

GrainEngine::TransientResponse SillageAudioProcessor::resolveTransientResponse() const
{
    GrainEngine::TransientResponse response;

    const auto synced = parameterValue (params::id::sync) >= 0.5f;

    response.retrigger  = parameterValue (params::id::retriggerOn) >= 0.5f;
    response.burstCount = (int) parameterValue (params::id::retriggerCount);
    response.burstIntervalSamples = synced
        ? params::divisionSeconds ((int) parameterValue (params::id::retriggerDivision),
                                   effectiveBpm.load(), barBeats.load()) * currentSampleRate
        : (double) parameterValue (params::id::retriggerRate) * 0.001 * currentSampleRate;

    // Look back past the detector's reaction time to where the hit landed,
    // plus the user's pre-roll (handoff open question 4).
    response.burstOffsetSamples = (double) OnsetDetector::kDetectionLagSamples
                                + (double) parameterValue (params::id::retriggerOffset) * 0.001 * currentSampleRate;
    response.burstAmount = parameterValue (params::id::retriggerAmount) * 0.01f;

    response.choke            = parameterValue (params::id::chokeOn) >= 0.5f;
    response.chokeAmount      = parameterValue (params::id::chokeAmount) * 0.01f;
    response.chokeFadeSamples = (double) parameterValue (params::id::chokeFade) * 0.001 * currentSampleRate;
    response.chokeProtectSamples = (double) OnsetDetector::kDetectionLagSamples;

    return response;
}

void SillageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numOut     = getTotalNumOutputChannels();
    const auto numIn      = getTotalNumInputChannels();

    for (int channel = numIn; channel < numOut; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples); // mono -> stereo upmix

    if (panicRequested.exchange (false))
    {
        engine.reset();
        duck.reset();
        onsetDetector.reset();
        envelopeFollower.reset();
    }

    updateTransport();

    // Transient detection and the input envelope run on the dry input, before
    // anything the engine does to it.
    onsetDetector.setSensitivity (parameterValue (params::id::sensitivity) * 0.01f);
    const auto numOnsets = onsetDetector.process (buffer.getArrayOfReadPointers(), numOut, numSamples,
                                                  onsetOffsets.data(), kMaxOnsetsPerBlock);
    const auto envelope  = envelopeFollower.process (buffer.getArrayOfReadPointers(), numOut, numSamples);
    const auto chaos     = chaosModulator.advance (numSamples);

    wetBuffer.setSize (numOut, numSamples, false, false, true);

    engine.getFeedbackPath().setSettings (resolveFeedbackSettings (chaos));
    engine.process (buffer.getArrayOfReadPointers(),
                    wetBuffer.getArrayOfWritePointers(),
                    numOut, numSamples,
                    resolveGrainSettings (chaos, envelope),
                    onsetOffsets.data(), numOnsets,
                    resolveTransientResponse());

    mixSmoothed.setTargetValue (parameterValue (params::id::mix) * 0.01f);
    outputGainSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (parameterValue (params::id::output)));

    const auto ducking   = parameterValue (params::id::duckOn) >= 0.5f;
    const auto duckDepth = parameterValue (params::id::duckDepth) * 0.01f;
    duck.setTimes (parameterValue (params::id::duckAttack), parameterValue (params::id::duckRelease));

    int onsetIndex = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (onsetIndex < numOnsets && onsetOffsets[(size_t) onsetIndex] <= i)
        {
            if (ducking)
                duck.trigger();
            ++onsetIndex;
        }

        // Duck sits on the wet signal ahead of Mix, per the signal-flow diagram.
        const auto duckGain = ducking ? duck.next (duckDepth) : duck.next (0.0f);

        const auto mix     = mixSmoothed.getNextValue();
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi) * duckGain;
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
