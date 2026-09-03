#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

#include "GrainEngine.h"
#include "LifetimeCurves.h"
#include "Modulation.h"
#include "Modulators.h"
#include "OnsetDetector.h"
#include "Parameters.h"
#include "Wake.h"

class SillageAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    SillageAudioProcessor();
    ~SillageAudioProcessor() override;

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

    // Live grain / instance counts, for the debug menu and the tests.
    int getActiveGrainCount() const noexcept { return wake.getActiveGrainCount(); }
    int getActiveInstanceCount() const noexcept { return wake.getActiveInstanceCount(); }

    // Monotonic transient counter for the hit indicator.
    uint64_t getOnsetCount() const noexcept { return onsetDetector.getOnsetCount(); }

    // True once Freeze has fully engaged.
    bool isFrozen() const noexcept { return wake.isFrozen(); }

    // Age (5.6) of what the live grains are playing, in seconds and against
    // Lifetime.
    float getAverageAgeSeconds() const noexcept { return wake.getAverageAgeSeconds(); }
    float getAverageAgeNormalised() const noexcept;

    bool isRewinding() const noexcept { return wake.isRewinding(); }

    // Lifetime Curves live in the state tree (they save with the session) and
    // are published to the audio thread as a snapshot. Message thread only.
    lifetime::CurveSet getCurves() const;
    void setCurves (const lifetime::CurveSet& curves);
    void publishCurves();

    // File presets (EVS standard: host-side state plus file-based save/load).
    // Message thread only.
    static juce::File getPresetDirectory();
    static juce::String getPresetExtension() { return ".sillage"; }
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    void resetToDefaults();
    juce::String getPresetName() const;
    void setPresetName (const juce::String& name);

    juce::AudioProcessorValueTreeState apvts;

private:
    float parameterValue (const char* id) const noexcept
    {
        return apvts.getRawParameterValue (id)->load();
    }

    // A destination's plain value with this block's modulation applied.
    float modulated (mod::Destination destination) const noexcept;

    void parameterChanged (const juce::String& parameterId, float newValue) override;

    void ensureCurvesInState();
    void updateTransport();
    void updateModulation (int numSamples, float envelope, int numOnsets);
    double resolveTimeSeconds() const;
    GrainEngine::Settings resolveGrainSettings (const ChaosValues& chaos, float envelope) const;
    FeedbackPath::Settings resolveFeedbackSettings (const ChaosValues& chaos) const;
    GrainEngine::TransientResponse resolveTransientResponse() const;
    WakeEngine::Settings resolveWakeSettings() const;
    void updateRewindTriggers (int numSamples);

    std::atomic<double> effectiveBpm { 120.0 };
    std::atomic<double> barBeats { 4.0 };
    std::atomic<double> hostTimeSeconds { -1.0 }; // < 0 when the host is not playing
    std::atomic<bool>   panicRequested { false };
    std::atomic<bool>   rewindManualRequested { false };

    WakeEngine       wake;
    OnsetDetector    onsetDetector;
    EnvelopeFollower envelopeFollower;
    ChaosModulator   chaosModulator;
    DuckEnvelope     duck;

    lifetime::CurveStore curveStore;
    lifetime::CurveSet   activeCurves; // per-block copy handed to the engine

    // Modulation (5.9): sources advance once per block; the offsets they
    // produce are read by every resolve* below.
    std::array<mod::Lfo, params::id::kNumLfos> lfos;
    mod::TransientEnvelope transientEnvelope;
    SmoothedRandom   modRandom;
    juce::Random     modRng { 0x5111c0de };
    mod::SourceValues sources;
    std::array<float, mod::kNumDestinations> modOffsets {};
    std::array<juce::RangedAudioParameter*, mod::kNumDestinations> destinationParams {};
    std::array<mod::PerGrainMod, params::id::kNumModSlots> ageMods {};
    int numAgeMods = 0;

    // Rewind trigger state (5.7).
    double rewindTimerLeft       = 0.0;   // samples until the next timed rewind
    bool   rewindThresholdArmed  = false; // re-arms once the tail rises again
    float  wetLevelDb            = -120.0f;

    static constexpr int kMaxOnsetsPerBlock = 64;
    std::array<int, kMaxOnsetsPerBlock> onsetOffsets {};

    // Post-loop wet stage (5.11): HP/LP, then Width, ahead of Mix.
    juce::dsp::StateVariableTPTFilter<float> wetHighpass, wetLowpass;

    juce::SmoothedValue<float> mixSmoothed, outputGainSmoothed, widthSmoothed;
    juce::AudioBuffer<float> wetBuffer;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessor)
};
