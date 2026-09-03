#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

#include "FeedbackPath.h"
#include "LifetimeCurves.h"
#include "Modulation.h"
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

// Rewind (5.7) captures the wet tail into a secondary ring. 8 s is the longest
// Rewind Length; the headroom keeps the capture head from overrunning a
// playback that is pitched down.
inline constexpr double kRewindCaptureSeconds = 12.0;
inline constexpr double kRewindMaxSeconds     = 8.0;

enum class WindowShape
{
    hann = 0,
    trapezoid,
    tukey,
    expoDecay,
    numShapes
};

// Rewind (5.7): captures a wet tail, and plays the last Rewind Length of it
// backward on a trigger. Two voices crossfade so a new trigger restarts
// rather than stacks. The engine owns one for the shared tail; the Wake layer
// owns another that covers the isolated instances together.
class RewindPlayer
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    bool isPrepared() const noexcept { return capacity > 0; }
    bool isPlaying() const noexcept;

    // Starts (or restarts, with a crossfade) playback of the last
    // `lengthSamples` of capture at `rate`.
    void begin (double lengthSamples, double rate) noexcept;

    // Records one frame of tail (two channels) and its age.
    void capture (const float* frame, float ageSeconds) noexcept;

    // Adds one frame of playback to `out` (two channels) and accumulates the
    // amplitude-weighted age of what was played.
    void render (float* out, float& ageAccum, float& weightAccum,
                 float level, double rate, int activeChannels) noexcept;

private:
    struct Voice
    {
        bool   active    = false;
        double readPos   = 0.0;
        double remaining = 0.0;
        float  fade      = 0.0f;
        float  fadeStep  = 0.0f;
        bool   fadingOut = false;
    };

    float read (int channel, double position) const noexcept;

    juce::AudioBuffer<float> ring;
    std::vector<float> ageRing;
    int    capacity  = 0;
    int    writePos  = 0;
    int    channels  = 2;
    double sampleRate = 44100.0;
    float  fadeStep  = 0.0f;
    std::array<Voice, 2> voices {};
};

// Input writes continuously into a stereo circular buffer; a scheduler fires
// windowed grains that read back from it and sum to the wet output. The grain
// sum is written back into the buffer through the feedback path, which always
// ends in a brickwall limiter.
//
// Every buffer sample carries its Age (5.6) in a parallel ring, so a grain
// knows how old the audio it plays is, and the feedback path can degrade
// audio by that age.
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

        // Age & Lifetime Curves (5.6). Per-grain destinations resolve at birth
        // from that grain's normalised age.
        double lifetimeSeconds = 2.0;
        const lifetime::CurveSet* curves = nullptr;
        std::array<bool, lifetime::kNumDestinations> curveEnabled {};

        // Rewind (5.7).
        bool   rewindOn            = false;
        double rewindLengthSamples = 0.0;
        float  rewindLevel         = 1.0f;
        double rewindRate          = 1.0;  // 2^(pitch/12)

        // Modulation (5.9): slots whose source is per-grain Age and whose
        // destination is per-grain resolve here, at grain birth, through the
        // destination parameter's own range.
        std::array<mod::PerGrainMod, params::id::kNumModSlots> ageMods {};
        int numAgeMods = 0;
        std::array<const juce::NormalisableRange<float>*, mod::kNumPerGrain> perGrainRanges {};
    };

    // What a detected transient does inside the engine (5.5, 5.7). Duck lives
    // in the processor because it acts on the wet output, not the buffer.
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

        bool   rewind = false; // Rewind trigger mode is Transient
    };

    // `bufferSeconds` sizes the circular buffer; an isolated Wake instance
    // (5.8) uses a shorter one and no Rewind capture of its own.
    void prepare (double sampleRate, int numChannels,
                  double bufferSeconds = kBufferSeconds, bool withRewind = true);
    void reset();

    // The 64-grain cap is global (5.8): the Wake layer shares it out between
    // the engines that are sounding.
    void setGrainCap (int cap) noexcept { grainCap = juce::jlimit (1, kMaxGrains, cap); }

    // Writes input straight into the buffer at age 0, without the feedback
    // path — how a new isolated instance receives the hit that spawned it.
    void primeInput (const float* const* input, int numChannels, int numSamples) noexcept;

    // Renders numSamples of wet output and writes input + the feedback path's
    // output back into the buffer. `input` and `wet` may not alias. `onsets`
    // holds block-relative sample offsets of detected transients, ascending.
    // `inject` (optional, two channels) is added to the feedback path's input
    // along with its per-sample age, which is how a Rewind reaches an
    // isolated instance.
    void process (const float* const* input,
                  float* const* wet,
                  int numChannels,
                  int numSamples,
                  const Settings& settings,
                  const int* onsets,
                  int numOnsets,
                  const TransientResponse& response,
                  const float* const* inject = nullptr,
                  const float* injectAge = nullptr);

    // Starts (or restarts, with a crossfade) a reversed playback of the last
    // Rewind Length of captured tail into the feedback path.
    void triggerRewind() noexcept { rewindRequested = true; }
    bool isRewinding() const noexcept;

    FeedbackPath& getFeedbackPath() noexcept { return feedbackPath; }

    // Live grain count, for the debug menu and the cap test.
    int getActiveGrainCount() const noexcept { return activeGrains; }

    // Amplitude-weighted average age of what the live grains are playing,
    // in seconds, over the last processed block. Global curve destinations
    // and (later) the mod matrix read this.
    float getAverageAgeSeconds() const noexcept { return averageAgeSeconds.load (std::memory_order_relaxed); }

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
        float       ageSeconds = 0.0f; // age of the audio at birth

        // Choke ramps a grain's level toward a target over a short fade.
        float scale           = 1.0f;
        float scaleTarget     = 1.0f;
        float scaleStep       = 0.0f;
        int   scaleSamplesLeft = 0;
    };

    // What the Lifetime Curves resolve to for one grain at birth.
    struct GrainShape
    {
        double lengthSamples      = 0.0;
        float  pitchOffset        = 0.0f;
        float  pitchSpread        = 0.0f;
        float  reverseProbability = 0.0f;
        float  panSpread          = 0.0f;
        float  level              = 1.0f;
    };

    float readInterpolated (int channel, double position) const noexcept;
    float ageAt (double position) const noexcept;
    float ageOfRegion (double startPos, double span) const noexcept;
    GrainShape resolveShape (const Settings& settings, float ageSeconds) noexcept;
    float resolvePitchRate (const Settings& settings, const GrainShape& shape) noexcept;
    void  activateGrain (double startPos, double rate, const Settings& settings,
                         const GrainShape& shape, float gainScale, float ageSeconds);
    void  spawnScheduledGrain (const Settings& settings);
    void  spawnBurstGrain (const Settings& settings);
    void  beginBurst (const TransientResponse& response);
    void  beginChoke (const TransientResponse& response);
    void  advanceChoke() noexcept;
    void  beginRewind (const Settings& settings) noexcept;
    int   claimSlot();

    double sampleRate = 44100.0;
    int    channels   = 2;
    int    capacity   = 0;
    int    writePos   = 0;
    juce::AudioBuffer<float> ring;
    std::vector<float> ageRing; // age in seconds of each sample when written

    std::array<Grain, kGrainSlots> grains {};
    int     activeGrains = 0;
    int     grainCap     = kMaxGrains;
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

    // Rewind capture and playback (absent on an isolated instance).
    RewindPlayer rewind;
    bool   rewindRequested = false;

    std::atomic<float> averageAgeSeconds { 0.0f };

    JUCE_LEAK_DETECTOR (GrainEngine)
};
