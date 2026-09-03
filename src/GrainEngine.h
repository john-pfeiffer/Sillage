#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

#include "FeedbackPath.h"
#include "Scales.h"

// Hard cap on simultaneously sounding grains. Compile-time by design (EVS DSP
// invariant): per-grain work has to stay cheap enough for 8+ instances.
inline constexpr int kMaxGrains = 64;

// A stolen grain keeps a slot for a couple of milliseconds while it fades out,
// so overload steals a grain instead of clicking. These slots sit outside the
// cap, and a stolen grain stops counting against it immediately — so the grain
// that triggered the steal gets a slot on the same sample.
inline constexpr int kStealSlots = 8;
inline constexpr int kGrainSlots = kMaxGrains + kStealSlots;

// The circular buffer holds 10 s at any sample rate, which is also the widest
// reach Spread has.
inline constexpr double kBufferSeconds = 10.0;

enum class WindowShape
{
    hann = 0,
    trapezoid,
    tukey,
    expoDecay,
    numShapes
};

// Input writes continuously into a stereo circular buffer; a scheduler fires
// windowed grains that read back from it and sum to the wet output. The grain
// sum is written back into the buffer through the feedback path, which always
// ends in a brickwall limiter.
class GrainEngine
{
public:
    // Resolved once per block. Every grain reads these at birth and then keeps
    // its own copy for life — that is what makes Time, Density and Spread
    // automatable without artefacts.
    struct Settings
    {
        double      timeSamples = 0.0;   // base read offset behind the write head
        double      density     = 20.0;  // grains per second
        float       spread      = 0.0f;  // 0..1
        double      sizeSamples = 0.0;   // grain length
        WindowShape window      = WindowShape::hann;
        float       feedback    = 0.0f;  // 0..1.2

        float          pitchSemitones     = 0.0f; // Pitch + Fine
        float          pitchSpread        = 0.0f; // 0..1
        scales::Scale  quantize           = scales::Scale::off;
        int            quantizeRoot       = 0;
        float          panSpread          = 0.0f; // 0..1
        float          reverseProbability = 0.0f; // 0..1
    };

    void prepare (double sampleRate, int numChannels);
    void reset();

    // Renders numSamples of wet output and writes input + the feedback path's
    // output back into the buffer. `input` and `wet` may not alias.
    void process (const float* const* input,
                  float* const* wet,
                  int numChannels,
                  int numSamples,
                  const Settings& settings);

    FeedbackPath& getFeedbackPath() noexcept { return feedbackPath; }

    // Live grain count, for the debug menu and the cap test.
    int getActiveGrainCount() const noexcept { return activeGrains; }

private:
    struct Grain
    {
        bool        active    = false;
        double      readPos   = 0.0;  // fractional index into the circular buffer
        double      rate      = 1.0;  // negative for a reversed grain
        double      pos       = 0.0;  // 0 .. length, in output samples
        double      length    = 0.0;
        double      invLength = 0.0;
        float       gainLeft  = 1.0f;
        float       gainRight = 1.0f;
        WindowShape window    = WindowShape::hann;
        float       fade      = 1.0f; // 1 normally, ramps to 0 once stolen
        float       fadeStep  = 0.0f;
        bool        stolen    = false;
        int64_t     birth     = 0;
    };

    float readInterpolated (int channel, double position) const noexcept;
    void spawnGrain (const Settings& settings);
    int  claimSlot();

    double sampleRate = 44100.0;
    int    channels   = 2;
    int    capacity   = 0;
    int    writePos   = 0;
    juce::AudioBuffer<float> ring;

    std::array<Grain, kGrainSlots> grains {};
    int     activeGrains = 0;
    int64_t grainCounter = 0;
    double  samplesUntilNextGrain = 0.0;

    juce::Random rng;
    FeedbackPath feedbackPath;
    float stealFadeStep = 0.0f;

    // Feedback is the one grain parameter that cannot resolve at grain birth —
    // it scales a running signal — so it is the one that needs smoothing.
    juce::SmoothedValue<float> feedbackSmoothed;

    JUCE_LEAK_DETECTOR (GrainEngine)
};
