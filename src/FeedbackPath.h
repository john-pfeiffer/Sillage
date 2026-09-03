#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// Everything written back into the grain buffer passes through this chain, in
// this order (handoff 5.2):
//
//   grain sum -> HP -> LP -> Pitch Shift -> Diffuser -> Saturation -> Limiter
//
// The limiter is last and stays last: it is what lets Feedback run past 100 %
// without the loop blowing up, so no colour stage may ever be placed after it.

// Granular pitch shifter used for the loop shimmer. Two taps read the delay
// line half a window out of phase and crossfade, which is cheap and a little
// gritty — on brand, and per the handoff's recommendation for this build. The
// interface is deliberately narrow so a cleaner algorithm can replace it later.
class PitchShifter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    void setSemitones (float semitones) noexcept;

    // Shifts one frame in place. Fully wet; the caller crossfades by Amount.
    void processFrame (float* samples, int numChannels) noexcept;

private:
    float readLine (int channel, double delaySamples) const noexcept;

    std::array<std::vector<float>, 2> lines;
    int    lineLength    = 0;
    int    writeIndex    = 0;
    double windowSamples = 0.0;
    double phase         = 0.0;
    double phaseIncrement = 0.0;
    double rate          = 1.0;
};

class FeedbackPath
{
public:
    // Resolved once per block.
    struct Settings
    {
        float highpassHz       = 20.0f;
        float lowpassHz        = 20000.0f;
        float resonance        = 0.0f;  // 0..1
        float shimmerSemitones = 0.0f;  // interval + fine, already scale-snapped
        float shimmerAmount    = 0.0f;  // 0..1
        float diffuse          = 0.0f;  // 0..1
        int   satType          = 0;     // 0 soft, 1 hard, 2 fold
        float drive            = 0.0f;  // 0..1
    };

    void prepare (double sampleRate, int numChannels);
    void reset();
    void setSettings (const Settings& settings);

    // Processes one frame in place and leaves it limited, ready for the buffer.
    void processFrame (float* samples, int numChannels) noexcept;

    // Current limiter gain reduction, for the debug menu / tests.
    float getLimiterGain() const noexcept { return limiterGain; }

private:
    struct Allpass
    {
        std::vector<float> line;
        int index = 0;

        void setLength (int length);
        void reset();
        float process (float input, float coefficient) noexcept;
    };

    static constexpr int kAllpassStages = 4;

    juce::dsp::StateVariableTPTFilter<float> highpass, lowpass;
    PitchShifter shimmer;
    std::array<std::array<Allpass, kAllpassStages>, 2> diffusers;

    Settings current;
    float driveGain      = 1.0f;
    float diffuseCoeff   = 0.0f;

    double sampleRate    = 44100.0;
    int    channels      = 2;

    float limiterGain    = 1.0f;
    float limiterRelease = 0.0f;

    JUCE_LEAK_DETECTOR (FeedbackPath)
};
