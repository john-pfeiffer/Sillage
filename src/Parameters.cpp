#include "Parameters.h"

namespace params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using P = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<P> (
        juce::ParameterID { id::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.0f), 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<P> (
        juce::ParameterID { id::output, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Used for synced parameters when the host provides no tempo.
    layout.add (std::make_unique<P> (
        juce::ParameterID { id::fallbackBpm, 1 }, "Fallback BPM",
        juce::NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f));

    return layout;
}

} // namespace params
