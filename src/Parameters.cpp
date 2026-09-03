#include "Parameters.h"
#include "LifetimeCurves.h"
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

juce::NormalisableRange<float> percent()
{
    return juce::NormalisableRange<float> (0.0f, 100.0f);
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

juce::StringArray longDivisionNames()
{
    juce::StringArray names;
    for (const auto& division : kLongDivisions)
        names.add (division.name);
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

template <typename... Args>
std::unique_ptr<Float> makeFloat (const char* id, const char* name, Args&&... args)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, std::forward<Args> (args)...);
}

std::unique_ptr<Bool> makeBool (const char* id, const juce::String& name, bool defaultValue)
{
    return std::make_unique<Bool> (juce::ParameterID { id, 1 }, name, defaultValue);
}

std::unique_ptr<Choice> makeChoice (const char* id, const char* name,
                                    juce::StringArray choices, int defaultIndex)
{
    return std::make_unique<Choice> (juce::ParameterID { id, 1 }, name, std::move (choices), defaultIndex);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Grain engine (5.1) -------------------------------------------------
    layout.add (makeFloat (id::time, "Time", skewed (1.0f, 4000.0f, 400.0f), 350.0f, label ("ms")));
    layout.add (makeBool (id::timeSync, "Time Sync", false));
    layout.add (makeChoice (id::timeDivision, "Division", divisionNames(), kDefaultDivision));
    layout.add (makeFloat (id::density, "Density", skewed (0.5f, 500.0f, 30.0f), 30.0f, label ("/s")));
    layout.add (makeFloat (id::spread, "Spread", percent(), 40.0f, label ("%")));
    layout.add (makeFloat (id::size, "Size", skewed (2.0f, 2000.0f, 120.0f), 150.0f, label ("ms")));
    layout.add (makeChoice (id::window, "Window",
                            juce::StringArray { "Hann", "Trapezoid", "Tukey", "Expo-decay" }, 0));

    // Above 100 % is intentional and safe only because of the loop limiter.
    layout.add (makeFloat (id::feedback, "Feedback",
                           juce::NormalisableRange<float> (0.0f, 120.0f), 45.0f, label ("%")));

    // ---- Per-grain pitch & placement (5.1) ----------------------------------
    layout.add (makeFloat (id::pitch, "Pitch",
                           juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, label ("st")));
    layout.add (makeFloat (id::pitchFine, "Pitch Fine",
                           juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, label ("ct")));
    layout.add (makeFloat (id::pitchSpread, "Pitch Spread", percent(), 0.0f, label ("%")));
    layout.add (makeChoice (id::quantize, "Quantize", scaleNames(), 0));
    layout.add (makeChoice (id::quantizeRoot, "Root", rootNames(), 0));
    layout.add (makeFloat (id::panSpread, "Pan Spread", percent(), 0.0f, label ("%")));
    layout.add (makeFloat (id::reverse, "Reverse", percent(), 0.0f, label ("%")));

    // ---- Feedback path (5.2) ------------------------------------------------
    layout.add (makeFloat (id::fbHighpass, "Loop HP", skewed (20.0f, 4000.0f, 200.0f), 20.0f, label ("Hz")));
    layout.add (makeFloat (id::fbLowpass, "Loop LP", skewed (200.0f, 20000.0f, 2000.0f), 20000.0f, label ("Hz")));
    layout.add (makeFloat (id::fbResonance, "Loop Res", percent(), 0.0f, label ("%")));
    layout.add (makeChoice (id::shimmerInterval, "Shimmer",
                            juce::StringArray { "-12", "-7", "-5", "0", "+5", "+7", "+12", "+19", "+24" }, 6));
    layout.add (makeFloat (id::shimmerAmount, "Shimmer Amt", percent(), 0.0f, label ("%")));
    layout.add (makeFloat (id::shimmerFine, "Shimmer Fine",
                           juce::NormalisableRange<float> (-50.0f, 50.0f), 0.0f, label ("ct")));
    layout.add (makeFloat (id::diffuse, "Diffuse", percent(), 0.0f, label ("%")));
    layout.add (makeChoice (id::satType, "Saturation", juce::StringArray { "Soft", "Hard", "Fold" }, 0));
    layout.add (makeFloat (id::drive, "Drive", percent(), 0.0f, label ("%")));

    // ---- Freeze (5.3), Chaos (5.4), Randomize (5.10) ------------------------
    layout.add (makeBool (id::freeze, "Freeze", false));
    layout.add (makeFloat (id::freezeFade, "Freeze Fade",
                           juce::NormalisableRange<float> (0.0f, 2000.0f), 200.0f, label ("ms")));
    layout.add (makeFloat (id::chaos, "Chaos", percent(), 0.0f, label ("%")));
    layout.add (makeFloat (id::randomizeAmount, "Randomize Amt", percent(), 100.0f, label ("%")));

    // ---- Transient system (5.5) ---------------------------------------------
    layout.add (makeFloat (id::sensitivity, "Sensitivity", percent(), 50.0f, label ("%")));

    layout.add (makeBool (id::retriggerOn, "Retrigger", false));
    layout.add (makeFloat (id::retriggerCount, "Burst Count",
                           juce::NormalisableRange<float> (1.0f, 16.0f, 1.0f), 4.0f));
    layout.add (makeFloat (id::retriggerRate, "Burst Rate", skewed (10.0f, 1000.0f, 125.0f), 125.0f, label ("ms")));
    layout.add (makeChoice (id::retriggerDivision, "Burst Div", divisionNames(), kDefaultShortDivision));
    layout.add (makeFloat (id::retriggerAmount, "Burst Amt", percent(), 100.0f, label ("%")));
    layout.add (makeFloat (id::retriggerOffset, "Burst Offset",
                           juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("ms")));

    layout.add (makeBool (id::duckOn, "Duck", false));
    layout.add (makeFloat (id::duckDepth, "Duck Depth", percent(), 50.0f, label ("%")));
    layout.add (makeFloat (id::duckAttack, "Duck Attack", skewed (1.0f, 50.0f, 8.0f), 5.0f, label ("ms")));
    layout.add (makeFloat (id::duckRelease, "Duck Release", skewed (20.0f, 2000.0f, 250.0f), 250.0f, label ("ms")));

    layout.add (makeBool (id::chokeOn, "Choke", false));
    layout.add (makeFloat (id::chokeAmount, "Choke Amt", percent(), 100.0f, label ("%")));
    layout.add (makeFloat (id::chokeFade, "Choke Fade", skewed (1.0f, 200.0f, 30.0f), 30.0f, label ("ms")));

    layout.add (makeFloat (id::envDensity, "Env > Density",
                           juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, label ("%")));
    layout.add (makeFloat (id::envSpread, "Env > Spread",
                           juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f, label ("%")));

    // ---- Sync & Swing (5.5) -------------------------------------------------
    layout.add (makeBool (id::sync, "Sync", false));
    layout.add (makeChoice (id::grainDivision, "Grain Div", divisionNames(), kDefaultShortDivision));
    layout.add (makeFloat (id::swing, "Swing", percent(), 0.0f, label ("%")));

    // ---- Age & per-pass Degrade (5.6) ---------------------------------------
    layout.add (makeFloat (id::lifetime, "Lifetime", skewed (100.0f, 30000.0f, 2000.0f), 2000.0f, label ("ms")));
    layout.add (makeFloat (id::degradeBits, "Bits/pass",
                           juce::NormalisableRange<float> (0.0f, 2.0f), 0.0f, label ("bit")));
    layout.add (makeFloat (id::degradeRate, "SR/pass",
                           juce::NormalisableRange<float> (0.0f, 10.0f), 0.0f, label ("%")));
    layout.add (makeFloat (id::degradeNoise, "Noise/pass", percent(), 0.0f, label ("%")));
    layout.add (makeFloat (id::degradeTilt, "LP tilt/pass",
                           juce::NormalisableRange<float> (0.0f, 500.0f), 0.0f, label ("Hz")));
    layout.add (makeFloat (id::degradeDrift, "Drift/pass",
                           juce::NormalisableRange<float> (0.0f, 50.0f), 0.0f, label ("ct")));
    layout.add (makeChoice (id::degradeDriftDir, "Drift Dir",
                            juce::StringArray { "Up", "Down", "Random" }, 0));

    for (size_t d = 0; d < id::curveEnable.size(); ++d)
        layout.add (makeBool (id::curveEnable[d],
                              juce::String ("Curve: ") + lifetime::kDestinationNames[d], false));

    // ---- Rewind (5.7) -------------------------------------------------------
    layout.add (makeBool (id::rewindOn, "Rewind", false));
    layout.add (makeFloat (id::rewindLength, "Rewind Length", skewed (100.0f, 8000.0f, 1000.0f), 1000.0f, label ("ms")));
    layout.add (makeChoice (id::rewindTrigger, "Rewind Trig",
                            juce::StringArray { "Timer", "Transient", "Threshold", "Manual" }, 3));
    layout.add (makeFloat (id::rewindInterval, "Rewind Every", skewed (0.1f, 30.0f, 2.0f), 2.0f, label ("s")));
    layout.add (makeChoice (id::rewindDivision, "Rewind Div", longDivisionNames(), 2));
    layout.add (makeFloat (id::rewindThreshold, "Rewind Thresh",
                           juce::NormalisableRange<float> (-60.0f, 0.0f), -40.0f, label ("dB")));
    layout.add (makeFloat (id::rewindLevel, "Rewind Level", percent(), 100.0f, label ("%")));
    layout.add (makeFloat (id::rewindPitch, "Rewind Pitch",
                           juce::NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f, label ("st")));

    // Momentary: fires a rewind on the rising edge.
    layout.add (makeBool (id::rewindManual, "Rewind Now", false));

    // ---- Output & global (5.11) ---------------------------------------------
    layout.add (makeFloat (id::mix, "Mix", percent(), 30.0f, label ("%")));
    layout.add (makeFloat (id::output, "Output",
                           juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, label ("dB")));

    // Used for synced parameters when the host provides no tempo.
    layout.add (makeFloat (id::fallbackBpm, "Fallback BPM",
                           juce::NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f));

    // Momentary: clears every buffer and grain on the rising edge.
    layout.add (makeBool (id::panic, "Panic", false));

    return layout;
}

} // namespace params
