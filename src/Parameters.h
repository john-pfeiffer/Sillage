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

inline constexpr int kDefaultDivision = 10; // 1/8

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace params
