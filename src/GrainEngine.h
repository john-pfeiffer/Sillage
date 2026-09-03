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
        float       feedback    = 0.0f;  // 0..1.2 (+ Chaos)

        float          pitchSemitones     = 0.0f; // Pitch + Fine
        float          pitchSpread        = 0.0f; // 0..1
        scales::Scale  quantize           = scales::Scale::off;
        int            quantizeRoot       = 0;
        float          panSpread          = 0.0f; // 0..1
        float          reverseProbability = 0.0f; // 0..1

        // Chaos (5.4), resolved by the processor and applied at grain birth.
        double timeModSamples    = 0.0;  // added to the base offset
        float  pitchModSemitones = 0.0f; // added before Quantize

        // Freeze (5.3).
        bool   freeze            = false;
        double freezeFadeSamples = 0.0;

        // Sync & Swing (5.5). With sync on, grain spacing is the division.
        bool   sync                = false;
        double syncIntervalSamples = 0.0;
        float  swing               = 0.0f; // 0..1
    };

    // What a detected transient does inside the engine (5.5). Duck lives in
    // the processor because it acts on the wet output, not the buffer.
    struct TransientResponse
    {
        bool   retrigger            = false;
        int    burstCount           = 4;
        double burstIntervalSamples = 0.0;
        double burstOffsetSamples   = 0.0; // how far behind the head the hit is
        float  burstAmount          = 1.0f;

        bool   choke               = false;
        float  chokeAmount         = 1.0f;
        double chokeFadeSamples    = 0.0;
        double chokeProtectSamples = 0.0; // the hit itself, kept out of the sweep
    };

    void prepare (double sampleRate, int numChannels);
    void reset();

    // Renders numSamples of wet output and writes input + the feedback path's
    // output back into the buffer. `input` and `wet` may not alias. `onsets`
    // holds block-relative sample offsets of detected transients, ascending.
    void process (const float* const* input,
                  float* const* wet,
                  int numChannels,
                  int numSamples,
                  const Settings& settings,
                  const int* onsets,
                  int numOnsets,
                  const TransientResponse& response);

    FeedbackPath& getFeedbackPath() noexcept { return feedbackPath; }

    // Live grain count, for the debug menu and the cap test.
    int getActiveGrainCount() const noexcept { return activeGrains; }

    // True once Freeze has fully engaged and the write head has stopped.
    bool isFrozen() const noexcept { return writeGain <= 0.0f; }

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

        // Choke ramps a grain's level toward a target over a short fade.
        float scale           = 1.0f;
        float scaleTarget     = 1.0f;
        float scaleStep       = 0.0f;
        int   scaleSamplesLeft = 0;
    };

    float readInterpolated (int channel, double position) const noexcept;
    float resolvePitchRate (const Settings& settings) noexcept;
    void  activateGrain (double startPos, double rate, const Settings& settings, float gainScale);
    void  spawnScheduledGrain (const Settings& settings);
    void  spawnBurstGrain (const Settings& settings);
    void  beginBurst (const TransientResponse& response);
    void  beginChoke (const TransientResponse& response);
    void  advanceChoke() noexcept;
    int   claimSlot();

    double sampleRate = 44100.0;
    int    channels   = 2;
    int    capacity   = 0;
    int    writePos   = 0;
    juce::AudioBuffer<float> ring;

    std::array<Grain, kGrainSlots> grains {};
    int     activeGrains = 0;
    int64_t grainCounter = 0;
    double  samplesUntilNextGrain = 0.0;
    bool    slotParity = false; // swing alternates on this

    juce::Random rng;
    FeedbackPath feedbackPath;
    float stealFadeStep = 0.0f;

    // Feedback is the one grain parameter that cannot resolve at grain birth —
    // it scales a running signal — so it is the one that needs smoothing.
    juce::SmoothedValue<float> feedbackSmoothed;

    // Freeze: crossfades the write in and out; the head stops at 0.
    float writeGain = 1.0f;

    // The last few ms of what the feedback path wrote, so a choke can remove
    // exactly the old tail from the samples around the hit and keep the hit.
    static constexpr int kFeedbackHistorySize = 2048; // power of two
    juce::AudioBuffer<float> feedbackHistory;

    // Retrigger burst in flight (a new hit restarts it).
    struct Burst
    {
        int    remaining = 0;
        double nextIn    = 0.0;
        double interval  = 0.0;
        double hitPos    = 0.0;
        float  amount    = 1.0f;
    } burst;

    // Choke sweeps the buffer by (1 - amount), amortised across the fade so
    // there is no 480k-multiply spike on the hit. It runs backward from just
    // behind the head so the region grains read next is cleared first, and
    // the feedback write is held at the same factor until the fade is over so
    // nothing of the old tail is re-seeded into the loop.
    int   chokeSamplesLeft    = 0;
    int   chokePositionsLeft  = 0;
    int   chokeChunk          = 0;
    int   chokeSweepPos       = 0;
    float chokeGain           = 1.0f;

    JUCE_LEAK_DETECTOR (GrainEngine)
};
