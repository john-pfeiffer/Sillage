#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// Control-rate modulation sources. None of these touch audio directly; the
// processor folds their values into the engine's per-block settings.

// Sample-and-hold random through a one-pole slew. Each hold time is drawn
// log-uniformly from the 0.1–20 Hz range, so every source has its own tempo
// and the combined motion never settles into a pattern.
class SmoothedRandom
{
public:
    void prepare (double sampleRate, juce::Random& rng);
    void reset (juce::Random& rng);

    // Advances by numSamples and returns the value at the end, in -1..1.
    float advance (int numSamples, juce::Random& rng) noexcept;
    float getValue() const noexcept { return current; }

private:
    void drawNext (juce::Random& rng) noexcept;

    double sampleRate    = 44100.0;
    float  target        = 0.0f;
    float  current       = 0.0f;
    double holdRemaining = 0.0;
    double slewSamples   = 1.0;
};

// The Chaos macro (5.4): five independent smoothed-random sources, one per
// destination, so the read position, pitch, feedback, density and shimmer
// detune all wander on their own clocks.
struct ChaosValues
{
    float position    = 0.0f; // -1..1
    float pitch       = 0.0f;
    float feedback    = 0.0f;
    float density     = 0.0f;
    float shimmerFine = 0.0f;
};

class ChaosModulator
{
public:
    void prepare (double sampleRate);
    void reset();
    ChaosValues advance (int numSamples) noexcept;

private:
    std::array<SmoothedRandom, 5> sources;
    juce::Random rng { 0x5111a9e };
};

// Input envelope follower normalised by a slow peak tracker, so it reads 0..1
// relative to recent loudness rather than absolute level. That is what lets
// Env→Density mean "hits dense, sustains sparse" at any input gain.
class EnvelopeFollower
{
public:
    void prepare (double sampleRate);
    void reset();

    // Consumes a block and returns the normalised envelope at its end.
    float process (const float* const* input, int numChannels, int numSamples) noexcept;
    float getValue() const noexcept { return normalised; }

private:
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, slowReleaseCoeff = 0.0f;
    float fast = 0.0f, slow = 0.0f, normalised = 0.0f;
};

// Duck (5.5): a transient pushes the wet signal down by Depth over Attack and
// lets it bloom back over Release. Runs per sample on the wet output.
class DuckEnvelope
{
public:
    void prepare (double sampleRate);
    void reset();
    void setTimes (float attackMs, float releaseMs) noexcept;

    void trigger() noexcept;

    // Gain to apply to this sample, given Depth in 0..1.
    float next (float depth) noexcept;

private:
    double sampleRate  = 44100.0;
    float  attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float  envelope    = 0.0f;
    int    attackLeft  = 0;
    int    attackSamples = 0;
};
