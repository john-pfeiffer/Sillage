#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

#include "Parameters.h"

// Modulation (5.9): six visible slots, each Source -> Destination with a
// bipolar Amount and a Curve — deliberately not a full matrix. Sources read
// 0..1; a slot contributes amount * curve(source) in the destination
// parameter's *normalised* range, so ±100 % always means "the whole knob",
// and skewed ranges move perceptually.
//
// Per-grain Age modulating a per-grain destination (Size, Pitch, Pitch
// Spread, Pan Spread, Reverse) resolves per grain at birth inside the engine;
// everything else resolves once per block in the processor.
namespace mod
{

enum class Source
{
    none = 0,
    agePerGrain,   // that grain's own age / Lifetime (average for global destinations)
    ageNormalised, // average age of live grains / Lifetime
    envelope,      // input envelope, relative to recent loudness
    transient,     // one-shot on a hit, exponential decay
    chaos,         // a smoothed-random source of its own
    lfo1,
    lfo2,
    count
};

enum class Destination
{
    none = 0,
    time, density, spread, size, feedback,
    pitch, pitchSpread, panSpread, reverse,
    loopHighpass, loopLowpass, shimmerAmount, shimmerFine, diffuse, drive,
    degradeBits, degradeRate, degradeNoise, degradeTilt, degradeDrift,
    rewindLevel, displace, mix,
    decay,
    count
};

enum class Curve { linear = 0, exponential, logarithmic, count };

enum class LfoShape { sine = 0, triangle, saw, square, sampleHold, count };

inline constexpr int kNumSources      = (int) Source::count;
inline constexpr int kNumDestinations = (int) Destination::count;

inline constexpr std::array<const char*, kNumSources> kSourceNames {
    "Off", "Age (grain)", "Age (avg)", "Envelope", "Transient", "Chaos", "LFO 1", "LFO 2"
};

inline constexpr std::array<const char*, kNumDestinations> kDestinationNames {
    "Off", "Time", "Density", "Spread", "Size", "Feedback",
    "Pitch", "Pitch Spread", "Pan Spread", "Reverse",
    "Loop HP", "Loop LP", "Shimmer Amt", "Shimmer Fine", "Diffuse", "Drive",
    "Bits/pass", "SR/pass", "Noise/pass", "LP tilt/pass", "Drift/pass",
    "Rewind Level", "Displace", "Mix", "Decay"
};

inline constexpr std::array<const char*, (size_t) Curve::count> kCurveNames { "Linear", "Exp", "Log" };

inline constexpr std::array<const char*, (size_t) LfoShape::count> kLfoShapeNames {
    "Sine", "Triangle", "Saw", "Square", "S&H"
};

juce::StringArray sourceNames();
juce::StringArray destinationNames();
juce::StringArray curveNames();
juce::StringArray lfoShapeNames();

// The parameter a destination modulates; nullptr for Off.
const char* destinationParameterId (Destination destination) noexcept;

// 0..1 -> 0..1.
inline float shape (float x, Curve curve) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    switch (curve)
    {
        case Curve::exponential: return x * x;
        case Curve::logarithmic: return std::sqrt (x);
        case Curve::linear:
        case Curve::count:
        default:                 return x;
    }
}

// Per-grain destinations, in the order the engine keeps their ranges.
enum class PerGrain { size = 0, pitch, pitchSpread, panSpread, reverse, count };
inline constexpr int kNumPerGrain = (int) PerGrain::count;

// -1 when a destination is global.
inline constexpr int perGrainIndex (Destination destination) noexcept
{
    if (destination == Destination::size)        return (int) PerGrain::size;
    if (destination == Destination::pitch)       return (int) PerGrain::pitch;
    if (destination == Destination::pitchSpread) return (int) PerGrain::pitchSpread;
    if (destination == Destination::panSpread)   return (int) PerGrain::panSpread;
    if (destination == Destination::reverse)     return (int) PerGrain::reverse;
    return -1;
}

// A per-grain Age modulation the engine applies at grain birth.
struct PerGrainMod
{
    int   perGrain = 0;    // PerGrain index
    float amount   = 0.0f; // -1..1
    Curve curve    = Curve::linear;
};

// Applies a normalised offset to a plain value through its parameter range.
inline float applyOffset (const juce::NormalisableRange<float>& range, float plain, float offset) noexcept
{
    const auto normalised = range.convertTo0to1 (juce::jlimit (range.start, range.end, plain));
    return range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised + offset));
}

// Control-rate LFO. Advances once per block and returns 0..1 at the end of
// it. A synced LFO is re-phased from the transport by the processor each
// block, so it stays locked to the bar while the host is playing.
class Lfo
{
public:
    void prepare (double sampleRate);
    void reset();

    void setPhase (double newPhase) noexcept { phase = newPhase - std::floor (newPhase); }

    float advance (int numSamples, double hz, float phaseOffset, LfoShape shape) noexcept;
    float getValue() const noexcept { return value; }

private:
    double  sampleRate = 44100.0;
    double  phase      = 0.0;
    float   value      = 0.0f;
    float   held       = 0.5f;
    int64_t cycles     = 0;   // whole cycles completed, for sample-and-hold
    int64_t lastCycle  = -1;
    juce::Random rng { 0x1f0a9 };
};

// One-shot with exponential decay: 1 on a hit, then falls by Decay.
class TransientEnvelope
{
public:
    void prepare (double sampleRate);
    void reset();
    void trigger() noexcept { value = 1.0f; }

    float advance (int numSamples, double decaySeconds) noexcept;
    float getValue() const noexcept { return value; }

private:
    double sampleRate = 44100.0;
    float  value      = 0.0f;
};

// Everything a slot can read this block, all 0..1.
struct SourceValues
{
    float ageNormalised = 0.0f;
    float envelope      = 0.0f;
    float transient     = 0.0f;
    float chaos         = 0.0f;
    float lfo1          = 0.0f;
    float lfo2          = 0.0f;

    float get (Source source) const noexcept
    {
        switch (source)
        {
            case Source::agePerGrain:
            case Source::ageNormalised: return ageNormalised;
            case Source::envelope:      return envelope;
            case Source::transient:     return transient;
            case Source::chaos:         return chaos;
            case Source::lfo1:          return lfo1;
            case Source::lfo2:          return lfo2;
            case Source::none:
            case Source::count:
            default:                    return 0.0f;
        }
    }
};

} // namespace mod
