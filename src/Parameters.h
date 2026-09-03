#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// All parameter IDs live here so the processor, editor, and tests agree on
// one spelling. Every user-facing parameter is host-automatable (EVS standard).
namespace params
{
namespace id
{
    inline constexpr auto mix      = "mix";
    inline constexpr auto output   = "output";
    inline constexpr auto fallbackBpm = "fallbackBpm";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace params
