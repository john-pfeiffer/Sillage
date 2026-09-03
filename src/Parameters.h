#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

// All parameter IDs live here so the processor, editor, and tests agree on
// one spelling. Every user-facing parameter is host-automatable (EVS standard).
namespace params
{
namespace id
{
    // Output & global (5.11)
    inline constexpr auto mix         = "mix";
    inline constexpr auto output      = "output";
    inline constexpr auto fallbackBpm = "fallbackBpm";
    inline constexpr auto panic       = "panic";

    // Grain engine (5.1)
    inline constexpr auto time         = "time";
    inline constexpr auto timeSync     = "timeSync";
    inline constexpr auto timeDivision = "timeDivision";
    inline constexpr auto density      = "density";
    inline constexpr auto spread       = "spread";
    inline constexpr auto size         = "size";
    inline constexpr auto window       = "window";
    inline constexpr auto feedback     = "feedback";

    // Per-grain pitch & placement (5.1)
    inline constexpr auto pitch        = "pitch";
    inline constexpr auto pitchFine    = "pitchFine";
    inline constexpr auto pitchSpread  = "pitchSpread";
    inline constexpr auto quantize     = "quantize";
    inline constexpr auto quantizeRoot = "quantizeRoot";
    inline constexpr auto panSpread    = "panSpread";
    inline constexpr auto reverse      = "reverse";

    // Feedback path (5.2)
    inline constexpr auto fbHighpass      = "fbHighpass";
    inline constexpr auto fbLowpass       = "fbLowpass";
    inline constexpr auto fbResonance     = "fbResonance";
    inline constexpr auto shimmerInterval = "shimmerInterval";
    inline constexpr auto shimmerAmount   = "shimmerAmount";
    inline constexpr auto shimmerFine     = "shimmerFine";
    inline constexpr auto diffuse         = "diffuse";
    inline constexpr auto satType         = "satType";
    inline constexpr auto drive           = "drive";

    // Freeze (5.3), Chaos (5.4), Randomize (5.10)
    inline constexpr auto freeze          = "freeze";
    inline constexpr auto freezeFade      = "freezeFade";
    inline constexpr auto chaos           = "chaos";
    inline constexpr auto randomizeAmount = "randomizeAmount";

    // Transient system (5.5)
    inline constexpr auto sensitivity       = "sensitivity";
    inline constexpr auto retriggerOn       = "retriggerOn";
    inline constexpr auto retriggerCount    = "retriggerCount";
    inline constexpr auto retriggerRate     = "retriggerRate";
    inline constexpr auto retriggerDivision = "retriggerDivision";
    inline constexpr auto retriggerAmount   = "retriggerAmount";
    inline constexpr auto retriggerOffset   = "retriggerOffset";
    inline constexpr auto duckOn            = "duckOn";
    inline constexpr auto duckDepth         = "duckDepth";
    inline constexpr auto duckAttack        = "duckAttack";
    inline constexpr auto duckRelease       = "duckRelease";
    inline constexpr auto chokeOn           = "chokeOn";
    inline constexpr auto chokeAmount       = "chokeAmount";
    inline constexpr auto chokeFade         = "chokeFade";
    inline constexpr auto envDensity        = "envDensity";
    inline constexpr auto envSpread         = "envSpread";

    // Sync & Swing (5.5)
    inline constexpr auto sync          = "sync";
    inline constexpr auto grainDivision = "grainDivision";
    inline constexpr auto swing         = "swing";
}

// Musical divisions for synced Time, measured in beats (a quarter note = 1 beat).
// `beats < 0` is the sentinel for "one bar", whose length depends on the host
// time signature and so can only be resolved at process time.
struct Division
{
    const char* name;
    double beats;
};

inline constexpr double kBarDivision = -1.0;

inline constexpr std::array<Division, 19> kDivisions { {
    { "1/64T", 1.0 / 24.0 },
    { "1/64",  1.0 / 16.0 },
    { "1/32T", 1.0 / 12.0 },
    { "1/64.", 3.0 / 32.0 },
    { "1/32",  1.0 / 8.0  },
    { "1/16T", 1.0 / 6.0  },
    { "1/32.", 3.0 / 16.0 },
    { "1/16",  1.0 / 4.0  },
    { "1/8T",  1.0 / 3.0  },
    { "1/16.", 3.0 / 8.0  },
    { "1/8",   1.0 / 2.0  },
    { "1/4T",  2.0 / 3.0  },
    { "1/8.",  3.0 / 4.0  },
    { "1/4",   1.0        },
    { "1/2T",  4.0 / 3.0  },
    { "1/4.",  3.0 / 2.0  },
    { "1/2",   2.0        },
    { "1/2.",  3.0        },
    { "1 bar", kBarDivision },
} };

inline constexpr int kDefaultDivision       = 10; // 1/8
inline constexpr int kDefaultShortDivision  = 7;  // 1/16

// Resolves a division to seconds. `barBeats` is the host's beats-per-bar.
inline double divisionSeconds (int index, double bpm, double barBeats) noexcept
{
    const auto clamped  = (size_t) juce::jlimit (0, (int) kDivisions.size() - 1, index);
    const auto division = kDivisions[clamped];
    const auto beats    = division.beats < 0.0 ? barBeats : division.beats;
    return beats * 60.0 / juce::jmax (1.0, bpm);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace params
