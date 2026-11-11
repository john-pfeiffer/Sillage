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

// The first six read like a reverb's algorithms. The second six are the
// other half of the plugin: each one is built on something only Sillage does
// (pitched and reversed grains, a loop that stacks or sinks or rots, bursts
// on the hit, Chaos), so there is a way to hear it without building it.
enum class Type
{
    delay = 0, room, reverb, shimmer, wash, granular,
    shards, cascade, undertow, rot, stutter, swarm,
    count
};

inline constexpr int kNumTypes = (int) Type::count;

inline constexpr std::array<const char*, kNumTypes> kTypeNames {
    "Delay", "Room", "Reverb", "Shimmer", "Wash", "Granular",
    "Shards", "Cascade", "Undertow", "Rot", "Stutter", "Swarm"
};

// The first of the second six: the editor's list has a divider before it.
inline constexpr int kFirstCharacterType = (int) Type::shards;

// Message thread only.
void apply (SillageAudioProcessor& processor, Type type);

} // namespace types
