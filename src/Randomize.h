#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// Randomize (5.10). Walks every parameter in the tree and skips an explicit
// exclusion list, so anything a later phase adds is randomised by default —
// which is what "touches everything in 5.1–5.9" has to mean as the set grows.
namespace randomize
{

// Never randomised: the user's mix and level, a random Freeze is a bug not a
// feature, Panic is momentary, the fallback tempo is a host setting, and the
// amount knob itself is meta. Wake mode joins this list in phase 7.
inline constexpr std::array<const char*, 6> kExcluded {
    "mix", "output", "freeze", "panic", "fallbackBpm", "randomizeAmount"
};

bool isExcluded (const juce::String& parameterId);

// `amount` in 0..1: 1 is a full reroll, 0.2 nudges each parameter a fifth of
// the way toward a random target. Choices and toggles reroll with probability
// `amount`. Must be called from the message thread.
void apply (juce::AudioProcessorValueTreeState& apvts, float amount, juce::Random& rng);

} // namespace randomize
