#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// When a control means something. A knob that does nothing in the current
// state is greyed out, and these are how a page says what that state is:
// "needs Sync on", "needs Sync off", "needs the trigger set to Timer".
namespace ui
{

struct Condition
{
    enum class Kind { on, off, equals, unlessBoth };

    const char* id    = nullptr;
    Kind kind         = Kind::on;
    int  value        = 0;
    const char* other = nullptr;

    // On: a switch that is on, or an amount above zero.
    static Condition on (const char* parameterId)                { return { parameterId, Kind::on, 0 }; }
    static Condition off (const char* parameterId)               { return { parameterId, Kind::off, 0 }; }
    static Condition equals (const char* parameterId, int index) { return { parameterId, Kind::equals, index }; }

    // Holds unless both are on: "not while its curve is enabled and the Age stage is running".
    static Condition unlessBoth (const char* first, const char* second) { return { first, Kind::unlessBoth, 0, second }; }
};

inline bool holds (const juce::AudioProcessorValueTreeState& apvts, const Condition& condition) noexcept
{
    if (condition.id == nullptr)
        return true;

    const auto* raw = apvts.getRawParameterValue (condition.id);
    if (raw == nullptr)
        return false;

    const auto value = raw->load();
    switch (condition.kind)
    {
        case Condition::Kind::unlessBoth:
        {
            const auto* second = condition.other != nullptr ? apvts.getRawParameterValue (condition.other) : nullptr;
            return ! (value > 1.0e-6f && second != nullptr && second->load() > 1.0e-6f);
        }
        case Condition::Kind::off:    return value <= 1.0e-6f;
        case Condition::Kind::equals: return (int) std::lround (value) == condition.value;
        case Condition::Kind::on:
        default:                      return value > 1.0e-6f;
    }
}

inline bool allHold (const juce::AudioProcessorValueTreeState& apvts, const std::vector<Condition>& conditions) noexcept
{
    for (const auto& condition : conditions)
        if (! holds (apvts, condition))
            return false;
    return true;
}

} // namespace ui
