#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// Lifetime Curves (5.6 B): a small multi-point envelope per destination over
// normalised Age 0→1. Per-grain destinations resolve at grain birth from that
// grain's own age; global destinations resolve once per block from the
// average age of live grains (5.9).
namespace lifetime
{

enum class Destination
{
    lowpass = 0,
    highpass,
    bitDepth,
    sampleRate,
    grainSize,
    pitchOffset,
    reverse,
    panSpread,
    level,
    count
};

inline constexpr int kNumDestinations = (int) Destination::count;
inline constexpr int kMaxPoints       = 8;

inline constexpr std::array<const char*, kNumDestinations> kDestinationNames {
    "LP cutoff", "HP cutoff", "Bit depth", "Sample rate", "Grain size",
    "Pitch offset", "Reverse", "Pan spread", "Level"
};

// Global destinations act on the loop, not on individual grains.
inline constexpr bool isGlobal (Destination d) noexcept
{
    return d == Destination::lowpass || d == Destination::highpass
        || d == Destination::bitDepth || d == Destination::sampleRate;
}

struct Point
{
    float x    = 0.0f; // 0..1, normalised age
    float y    = 0.0f; // 0..1, destination-mapped
    float bend = 0.0f; // -1..1, shapes the segment *after* this point
};

struct Curve
{
    int numPoints = 2;
    std::array<Point, kMaxPoints> points {};

    float evaluate (float x) const noexcept;
    void  sortPoints() noexcept;
};

struct CurveSet
{
    std::array<Curve, kNumDestinations> curves {};
};

// The y value that leaves the destination unchanged; a fresh curve is a flat
// line here.
float neutralY (Destination destination) noexcept;
Curve flatCurve (float y) noexcept;
CurveSet defaultCurveSet() noexcept;

// y (0..1) -> destination value.
float  lowpassHz (float y) noexcept;      // 200 Hz .. 20 kHz, log
float  highpassHz (float y) noexcept;     // 20 Hz .. 4 kHz, log
float  bitDepth (float y) noexcept;       // 4 .. 24
float  sampleRateHz (float y, double sr) noexcept; // 4 kHz .. sr, log
double grainSizeMs (float y) noexcept;    // 2 .. 2000 ms, log
float  pitchSemitones (float y) noexcept; // -24 .. +24, y = 0.5 is 0

// Serialisation into the plugin state so curves save with sessions/presets.
inline const juce::Identifier kCurvesType { "CURVES" };
inline const juce::Identifier kCurveType  { "CURVE" };
inline const juce::Identifier kPointType  { "POINT" };

juce::ValueTree toValueTree (const CurveSet& set);
CurveSet fromValueTree (const juce::ValueTree& tree); // missing/invalid -> defaults

// Randomize (5.10) touches curves too. Amount nudges points toward random
// targets; 1 is a full reroll of shape as well as position.
void randomise (CurveSet& set, float amount, juce::Random& rng);

// Message thread writes, audio thread reads, no locks. A double buffer with an
// atomic index: the reader copies the published slot at block start. The
// writer never writes the slot it just published, and edits arrive at mouse
// rate against a sub-microsecond copy, so a torn read is vanishingly rare and
// harmless — evaluate() clamps everything it touches.
class CurveStore
{
public:
    CurveStore();

    void set (const CurveSet& set);
    void copyTo (CurveSet& out) const noexcept;

private:
    std::array<CurveSet, 2> slots;
    std::atomic<int> published { 0 };
};

} // namespace lifetime
