#include "Types.h"

#include "LifetimeCurves.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "Randomize.h"

namespace types
{

namespace
{
struct Override
{
    const char* id;
    float plainValue;
};

// Window indices: Hann 0, Trapezoid 1, Tukey 2, Expo-decay 3.
// Shimmer interval index 6 is +12.
const std::vector<Override>& overridesFor (Type type)
{
    namespace id = params::id;
    using params::kDecayOffMs;

    static const std::vector<Override> delayTable {
        { id::time, 375.0f }, { id::decay, kDecayOffMs }, { id::density, 24.0f }, { id::spread, 0.0f },
        { id::size, 60.0f }, { id::window, 1.0f }, { id::feedback, 45.0f }, { id::panSpread, 0.0f },
        { id::fbHighpass, 150.0f }, { id::fbLowpass, 8000.0f }, { id::diffuse, 0.0f }, { id::shimmerAmount, 0.0f },
    };

    // Short and dense, transients intact: the 100 %-wet-drums reverb.
    static const std::vector<Override> roomTable {
        { id::time, 15.0f }, { id::decay, 250.0f }, { id::density, 300.0f }, { id::spread, 100.0f },
        { id::size, 15.0f }, { id::window, 2.0f }, { id::feedback, 40.0f }, { id::panSpread, 60.0f },
        { id::fbHighpass, 150.0f }, { id::fbLowpass, 9000.0f }, { id::diffuse, 40.0f }, { id::shimmerAmount, 0.0f },
        { id::width, 120.0f },
    };

    static const std::vector<Override> reverbTable {
        { id::time, 30.0f }, { id::decay, 1500.0f }, { id::density, 200.0f }, { id::spread, 100.0f },
        { id::size, 40.0f }, { id::window, 0.0f }, { id::feedback, 70.0f }, { id::panSpread, 80.0f },
        { id::fbHighpass, 100.0f }, { id::fbLowpass, 7000.0f }, { id::diffuse, 70.0f }, { id::shimmerAmount, 0.0f },
        { id::width, 130.0f },
    };

    static const std::vector<Override> shimmerTable {
        { id::time, 60.0f }, { id::decay, 4000.0f }, { id::density, 150.0f }, { id::spread, 100.0f },
        { id::size, 80.0f }, { id::window, 0.0f }, { id::feedback, 80.0f }, { id::panSpread, 80.0f },
        { id::fbHighpass, 200.0f }, { id::fbLowpass, 9000.0f }, { id::diffuse, 60.0f },
        { id::shimmerInterval, 6.0f }, { id::shimmerAmount, 45.0f },
        { id::width, 130.0f },
    };

    static const std::vector<Override> washTable {
        { id::time, 1200.0f }, { id::decay, 8000.0f }, { id::density, 60.0f }, { id::spread, 100.0f },
        { id::size, 400.0f }, { id::window, 2.0f }, { id::feedback, 90.0f }, { id::panSpread, 100.0f },
        { id::fbHighpass, 80.0f }, { id::fbLowpass, 4000.0f }, { id::diffuse, 80.0f }, { id::shimmerAmount, 0.0f },
        { id::degradeTilt, 60.0f }, { id::lifetime, 6000.0f },
        { id::width, 150.0f },
    };

    static const std::vector<Override> granularTable {
        { id::time, 250.0f }, { id::decay, kDecayOffMs }, { id::density, 20.0f }, { id::spread, 60.0f },
        { id::size, 40.0f }, { id::window, 3.0f }, { id::feedback, 55.0f }, { id::panSpread, 70.0f },
        { id::fbHighpass, 100.0f }, { id::fbLowpass, 12000.0f }, { id::diffuse, 0.0f }, { id::shimmerAmount, 0.0f },
        { id::pitchSpread, 20.0f }, { id::reverse, 30.0f }, { id::chaos, 25.0f },
    };

    switch (type)
    {
        case Type::room:     return roomTable;
        case Type::reverb:   return reverbTable;
        case Type::shimmer:  return shimmerTable;
        case Type::wash:     return washTable;
        case Type::granular: return granularTable;
        case Type::delay:
        case Type::count:
        default:             return delayTable;
    }
}
} // namespace

void apply (SillageAudioProcessor& processor, Type type)
{
    auto& apvts = processor.apvts;

    // A clean slate first, so a Type means the same thing whatever was set
    // before — but never the user's mix, level, Freeze or Wake mode.
    for (auto* parameter : processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        if (withId == nullptr || randomize::isExcluded (withId->paramID))
            continue;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }

    for (const auto& item : overridesFor (type))
    {
        if (auto* parameter = apvts.getParameter (item.id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (item.plainValue));
            parameter->endChangeGesture();
        }
    }

    processor.setCurves (lifetime::defaultCurveSet());
    processor.setPresetName (kTypeNames[(size_t) juce::jlimit (0, kNumTypes - 1, (int) type)]);
}

} // namespace types
