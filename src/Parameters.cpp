#include "Parameters.h"
#include "Scales.h"

namespace params
{
namespace
{
using Float  = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Bool   = juce::AudioParameterBool;

juce::NormalisableRange<float> skewed (float min, float max, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> range (min, max, step);
    range.setSkewForCentre (centre);
    return range;
}

juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (const auto& division : kDivisions)
        names.add (division.name);
    return names;
}

juce::StringArray scaleNames()
{
    juce::StringArray names;
    for (const auto& scale : scales::kScales)
        names.add (scale.name);
    return names;
}

juce::StringArray rootNames()
{
    juce::StringArray names;
    for (const auto* root : scales::kRootNames)
        names.add (root);
    return names;
}

auto label (const char* text)
{
    return juce::AudioParameterFloatAttributes().withLabel (text);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Grain engine (5.1) -------------------------------------------------
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::time, 1 }, "Time",
        skewed (1.0f, 4000.0f, 400.0f), 350.0f, label ("ms")));

    layout.add (std::make_unique<Bool> (
        juce::ParameterID { id::timeSync, 1 }, "Time Sync", false));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::timeDivision, 1 }, "Division",
        divisionNames(), kDefaultDivision));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::density, 1 }, "Density",
        skewed (0.5f, 500.0f, 30.0f), 30.0f, label ("/s")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::spread, 1 }, "Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f), 40.0f, label ("%")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::size, 1 }, "Size",
        skewed (2.0f, 2000.0f, 120.0f), 150.0f, label ("ms")));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::window, 1 }, "Window",
        juce::StringArray { "Hann", "Trapezoid", "Tukey", "Expo-decay" }, 0));

    // Above 100 % is intentional and safe only because of the loop limiter.
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::feedback, 1 }, "Feedback",
        juce::NormalisableRange<float> (0.0f, 120.0f), 45.0f, label ("%")));

    // ---- Per-grain pitch & placement (5.1) ----------------------------------
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::pitch, 1 }, "Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, label ("st")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::pitchFine, 1 }, "Pitch Fine",
        juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, label ("ct")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::pitchSpread, 1 }, "Pitch Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::quantize, 1 }, "Quantize", scaleNames(), 0));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::quantizeRoot, 1 }, "Root", rootNames(), 0));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::panSpread, 1 }, "Pan Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::reverse, 1 }, "Reverse",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    // ---- Feedback path (5.2) ------------------------------------------------
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::fbHighpass, 1 }, "Loop HP",
        skewed (20.0f, 4000.0f, 200.0f), 20.0f, label ("Hz")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::fbLowpass, 1 }, "Loop LP",
        skewed (200.0f, 20000.0f, 2000.0f), 20000.0f, label ("Hz")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::fbResonance, 1 }, "Loop Res",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::shimmerInterval, 1 }, "Shimmer",
        juce::StringArray { "-12", "-7", "-5", "0", "+5", "+7", "+12", "+19", "+24" }, 6));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::shimmerAmount, 1 }, "Shimmer Amt",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::shimmerFine, 1 }, "Shimmer Fine",
        juce::NormalisableRange<float> (-50.0f, 50.0f), 0.0f, label ("ct")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::diffuse, 1 }, "Diffuse",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<Choice> (
        juce::ParameterID { id::satType, 1 }, "Saturation",
        juce::StringArray { "Soft", "Hard", "Fold" }, 0));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::drive, 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    // ---- Output & global (5.11) ---------------------------------------------
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f), 30.0f, label ("%")));

    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::output, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, label ("dB")));

    // Used for synced parameters when the host provides no tempo.
    layout.add (std::make_unique<Float> (
        juce::ParameterID { id::fallbackBpm, 1 }, "Fallback BPM",
        juce::NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f));

    return layout;
}

} // namespace params
