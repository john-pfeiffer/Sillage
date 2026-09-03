#pragma once

#include <array>
#include <cmath>

// Pitch quantisation for randomised grain pitch (5.1) and, when it is on, for
// the loop shimmer as well (handoff open question 5: a +7 shimmer against a
// minor scale stops being musical after two passes otherwise).
namespace scales
{

enum class Scale
{
    off = 0,
    chromatic,
    major,
    minor,
    pentatonic,
    octavesAndFifths,
    numScales
};

// Semitone degrees within one octave, per scale.
struct ScaleTable
{
    const char* name;
    std::array<int, 12> degrees;
    int size;
};

inline constexpr std::array<ScaleTable, 6> kScales { {
    { "Off",             { 0 },                                    0 },
    { "Chromatic",       { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    { "Major",           { 0, 2, 4, 5, 7, 9, 11 },                 7 },
    { "Minor",           { 0, 2, 3, 5, 7, 8, 10 },                 7 },
    { "Pentatonic",      { 0, 3, 5, 7, 10 },                       5 },
    { "Octaves+5ths",    { 0, 7 },                                 2 },
} };

inline constexpr std::array<const char*, 12> kRootNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// Snaps a semitone offset to the nearest degree of `scale` relative to `root`.
// Scale::off passes the value through untouched.
inline float snapToScale (float semitones, Scale scale, int root) noexcept
{
    const auto index = (size_t) scale;
    if (scale == Scale::off || index >= kScales.size() || kScales[index].size == 0)
        return semitones;

    const auto& table = kScales[index];

    const float relative = semitones - (float) root;
    const float octave   = std::floor (relative / 12.0f);
    const float within   = relative - octave * 12.0f;

    // The first degree of the next octave is also a candidate, otherwise notes
    // just below it would snap down a long way instead of up a short one.
    float best     = (float) table.degrees[0];
    float bestDist = std::abs (within - best);

    for (int i = 1; i < table.size; ++i)
    {
        const auto candidate = (float) table.degrees[(size_t) i];
        const auto distance  = std::abs (within - candidate);
        if (distance < bestDist)
        {
            bestDist = distance;
            best     = candidate;
        }
    }

    if (std::abs (within - 12.0f) < bestDist)
        best = 12.0f;

    return (float) root + octave * 12.0f + best;
}

} // namespace scales
