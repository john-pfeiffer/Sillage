#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "LifetimeCurves.h"

// Randomize (5.10). Walks every parameter in the tree and skips an explicit
// exclusion list, so anything a later phase adds is randomised by default —
// which is what "touches everything in 5.1–5.9" has to mean as the set grows.
//
// A roll is not uniform. Rolling every parameter across its whole range gave
// silence six times in seven: a wet high-pass above its low-pass, a Decay
// shorter than Time, a choke on every hit. Each parameter that can do that has
// a Spec (the part of its range a roll may land in, and for a switch or an
// effect amount how often it is on at all), and constrain() repairs the few
// combinations that no per-parameter range can rule out.
namespace randomize
{

// Never randomised: the user's mix and level, a random Freeze is a bug not a
// feature, Wake mode changes the entire behaviour so the user chooses it,
// the stage switches are for listening not for rolling, Panic and the manual
// Rewind trigger are momentary, the fallback tempo is a host setting, and the
// amount knob itself is meta.
inline constexpr std::array<const char*, 13> kExcluded {
    "mix", "output", "freeze", "wakeMode", "panic", "fallbackBpm", "randomizeAmount", "rewindManual",
    "loopOn", "transientsOn", "ageOn", "chaosOn", "modOn"
};

bool isExcluded (const juce::String& parameterId);

// Where a roll may land, in the parameter's own units. For a knob, `chance`
// is how often it lands in low..high rather than at `rest`; for a switch it is
// how often the switch ends up on; for a choice low..high are the first and
// last index a roll may pick. A parameter with no Spec rolls over its whole
// range, and a switch with none is a coin toss.
struct Spec
{
    const char* id;
    float low    = 0.0f;
    float high   = 1.0f;
    float chance = 1.0f;
    float rest   = 0.0f;
};

const Spec* specFor (const juce::String& parameterId);

// At most this many mod slots are live after a roll.
inline constexpr int kMaxLiveModSlots = 3;

// `amount` in 0..1: 1 is a full reroll, 0.2 nudges each parameter a fifth of
// the way toward a random target. Choices and toggles reroll with probability
// `amount`. Ends by calling constrain(). Must be called from the message thread.
void apply (juce::AudioProcessorValueTreeState& apvts, float amount, juce::Random& rng);

// Repairs what would leave a roll silent or uncontrollable: filters that
// cross, a Decay shorter than twice Time, Feedback that Chaos would push past
// 100 %, a mod slot aimed at Mix, more live mod slots than kMaxLiveModSlots.
void constrain (juce::AudioProcessorValueTreeState& apvts);

// The same for the Lifetime Curves after lifetime::randomise: a Level curve
// may not fall to nothing, the filter curves may not close, and so on.
void constrain (lifetime::CurveSet& curves);

} // namespace randomize
