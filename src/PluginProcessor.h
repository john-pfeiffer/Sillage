#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

class SillageAudioProcessor : public juce::AudioProcessor
{
public:
    SillageAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sillage"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Host tempo if available this block, else the user-set fallback BPM.
    double getEffectiveBpm() const { return effectiveBpm.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    std::atomic<double> effectiveBpm { 120.0 };

    juce::SmoothedValue<float> mixSmoothed, outputGainSmoothed;
    juce::AudioBuffer<float> wetBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessor)
};
