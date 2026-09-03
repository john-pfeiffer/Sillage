#include "PluginProcessor.h"
#include "PluginEditor.h"

SillageAudioProcessor::SillageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
}

void SillageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    wetBuffer.setSize (2, samplesPerBlock);

    mixSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);

    // Zero latency by default; any future lookahead stage must report here.
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
    return 10.0;
}

void SillageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numOut     = getTotalNumOutputChannels();
    const int numIn      = getTotalNumInputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples); // mono -> stereo upmix

    if (auto* playhead = getPlayHead())
    {
        if (auto pos = playhead->getPosition(); pos.hasValue() && pos->getBpm().hasValue())
            effectiveBpm.store (*pos->getBpm());
        else
            effectiveBpm.store ((double) apvts.getRawParameterValue (params::id::fallbackBpm)->load());
    }
    else
    {
        effectiveBpm.store ((double) apvts.getRawParameterValue (params::id::fallbackBpm)->load());
    }

    // Wet path: identity until the grain engine lands.
    wetBuffer.setSize (numOut, numSamples, false, false, true);
    for (int ch = 0; ch < numOut; ++ch)
        wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    mixSmoothed.setTargetValue (apvts.getRawParameterValue (params::id::mix)->load() * 0.01f);
    outputGainSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (params::id::output)->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        const float m       = mixSmoothed.getNextValue();
        const float dryGain = std::cos (m * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (m * juce::MathConstants<float>::halfPi);
        const float outGain = outputGainSmoothed.getNextValue();

        for (int ch = 0; ch < numOut; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = (d[i] * dryGain + wetBuffer.getSample (ch, i) * wetGain) * outGain;
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
