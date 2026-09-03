#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

#include "GrainEngine.h"

// Wake (5.8): how the existing tail meets new input.
//
// Shared: one buffer. New input writes into the same ring the tail lives in,
// and Displace is Choke made continuous — every hit pushes the tail down by
// that much.
//
// Isolated: every detected onset spawns its own instance — its own buffer,
// scheduler, feedback path and Age clock — and the instances sum at the
// output. Eight can sound at once; a ninth steals the oldest with a fade.
// The 64-grain cap is shared out between whatever is sounding. Between
// onsets, input keeps feeding the newest instance. Displace here pushes the
// older instances down.
//
// Switching modes re-routes the *input* over a short crossfade; whatever
// tail is already ringing keeps ringing on the side it lives, so a switch
// never clicks or drops the tail.
class WakeEngine
{
public:
    static constexpr int    kMaxInstances          = 8;
    static constexpr int    kInstanceSlots         = kMaxInstances + 1; // one spare for a stolen instance's fade
    static constexpr double kIsolatedBufferSeconds = 6.0;  // Time (4 s) + Size (2 s)
    static constexpr double kModeFadeSeconds       = 0.1;
    static constexpr double kStealFadeSeconds      = 0.05;
    static constexpr double kPrimeSeconds          = 0.25; // input history handed to a new instance

    struct Settings
    {
        bool   isolated              = false;
        float  displace              = 0.0f; // 0..1
        double displaceFadeSamples   = 0.0;
    };

    void prepare (double sampleRate, int numChannels, int maxBlockSize);
    void reset();

    // Applied to every engine that is sounding, at the top of process().
    void setFeedbackSettings (const FeedbackPath::Settings& settings) { feedbackSettings = settings; }

    void process (const float* const* input,
                  float* const* wet,
                  int numChannels,
                  int numSamples,
                  const GrainEngine::Settings& grain,
                  const Settings& wake,
                  const int* onsets,
                  int numOnsets,
                  const GrainEngine::TransientResponse& response);

    void triggerRewind() noexcept { rewindRequested = true; }
    bool isRewinding() const noexcept;

    float getAverageAgeSeconds() const noexcept { return averageAgeSeconds.load (std::memory_order_relaxed); }
    int   getActiveGrainCount() const noexcept;
    int   getActiveInstanceCount() const noexcept { return activeInstances.load (std::memory_order_relaxed); }
    bool  isFrozen() const noexcept;

private:
    struct Instance
    {
        GrainEngine engine;
        bool    active       = false;
        bool    stealing     = false;
        float   gain         = 1.0f;
        float   gainTarget   = 1.0f;
        float   gainStep     = 0.0f;
        int     gainLeft     = 0;
        int64_t birth        = 0;
        int     silentBlocks = 0;
    };

    void spawnInstance (const Settings& wake, const GrainEngine::Settings& grain);
    void rampGain (Instance& instance, float target, double samples) noexcept;
    void shareGrainCap (bool sharedAlive);
    void recordHistory (const float* const* input, int numChannels, int from, int to) noexcept;

    double sampleRate = 44100.0;
    int    channels   = 2;

    GrainEngine shared;
    bool        sharedAlive = true;
    int         sharedSilentBlocks = 0;

    std::array<Instance, kInstanceSlots> instances;
    int     newest          = -1;
    int64_t instanceCounter = 0;

    // Input routing crossfade: 0 = all to the shared buffer, 1 = all isolated.
    float routing     = 0.0f;
    float routingStep = 0.0f;

    // Rewind for the isolated side: captures the summed instances, plays
    // back into the newest one.
    RewindPlayer isolatedRewind;
    bool         rewindRequested = false;

    FeedbackPath::Settings feedbackSettings;

    // Scratch.
    juce::AudioBuffer<float> sharedInput, isolatedInput, silence, scratchWet, isolatedWet, injectBuffer;
    std::vector<float> injectAge;

    // Recent input, so a new instance can be primed with the hit that
    // spawned it (the detector reacts a few ms after it lands).
    juce::AudioBuffer<float> history;
    int historyCapacity = 0;
    int historyPos      = 0;

    std::atomic<float> averageAgeSeconds { 0.0f };
    std::atomic<int>   activeInstances { 0 };

    JUCE_LEAK_DETECTOR (WakeEngine)
};
