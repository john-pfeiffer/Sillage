#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class SillageAudioProcessor;

// Quick-start Types: the algorithm selector a reverb plugin has. Each one is a
// tuned starting point for a character — a full setting, not a partial one,
// because a "Reverb" that left Spread at 0 would not be a reverb. Choosing a
// Type is an action like Randomize, not a parameter: it moves everything
// except what Randomize also leaves alone (Mix, Output, Freeze, Wake mode,
// Panic, Fallback BPM), resets the curves, and names the preset after itself.
// The main-page knobs are then the user's to move.
namespace types
{

enum class Type { delay = 0, reverb, shimmer, wash, granular, count };

inline constexpr int kNumTypes = (int) Type::count;

inline constexpr std::array<const char*, kNumTypes> kTypeNames {
    "Delay", "Reverb", "Shimmer", "Wash", "Granular"
};

// Message thread only.
void apply (SillageAudioProcessor& processor, Type type);

} // namespace types
