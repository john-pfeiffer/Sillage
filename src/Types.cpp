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

    // Hann grains at exactly two per length sum to a constant, so the repeats
    // are the input and nothing else.
    static const std::vector<Override> delayTable {
        { id::time, 375.0f }, { id::decay, kDecayOffMs }, { id::density, 25.0f }, { id::spread, 0.0f },
        { id::size, 80.0f }, { id::window, 0.0f }, { id::feedback, 45.0f }, { id::panSpread, 0.0f },
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
        { id::time, 80.0f }, { id::decay, 8000.0f }, { id::density, 80.0f }, { id::spread, 100.0f },
        { id::size, 200.0f }, { id::window, 2.0f }, { id::feedback, 90.0f }, { id::panSpread, 100.0f },
        { id::fbHighpass, 80.0f }, { id::fbLowpass, 4000.0f }, { id::diffuse, 80.0f }, { id::shimmerAmount, 0.0f },
        { id::degradeTilt, 60.0f }, { id::lifetime, 6000.0f },
        { id::width, 150.0f },
    };

    // Sparse, pitched, half-reversed grains over a three-second window.
    static const std::vector<Override> granularTable {
        { id::time, 250.0f }, { id::decay, 3000.0f }, { id::density, 30.0f }, { id::spread, 60.0f },
        { id::size, 60.0f }, { id::window, 3.0f }, { id::feedback, 60.0f }, { id::panSpread, 70.0f },
        { id::fbHighpass, 100.0f }, { id::fbLowpass, 12000.0f }, { id::diffuse, 0.0f }, { id::shimmerAmount, 0.0f },
        { id::pitchSpread, 20.0f }, { id::reverse, 30.0f }, { id::chaos, 25.0f },
    };

    // Scale index 4 is Pentatonic; saturation 1 is Hard, 2 is Fold; drift
    // direction 1 is Down; division 7 is 1/16, 10 is 1/8.

    // Short plucked grains scattered over two octaves of a pentatonic scale,
    // half of them backwards.
    static const std::vector<Override> shardsTable {
        { id::time, 180.0f }, { id::decay, 3500.0f }, { id::density, 28.0f }, { id::spread, 70.0f },
        { id::size, 45.0f }, { id::window, 3.0f }, { id::feedback, 65.0f }, { id::panSpread, 100.0f },
        { id::fbHighpass, 180.0f }, { id::fbLowpass, 11000.0f }, { id::diffuse, 20.0f },
        { id::pitchSpread, 60.0f }, { id::quantize, 4.0f }, { id::reverse, 50.0f },
        { id::width, 140.0f },
    };

    // Nearly all of the tail goes round the loop, and every trip is an
    // octave up: the tail climbs out of what was played.
    static const std::vector<Override> cascadeTable {
        { id::time, 40.0f }, { id::decay, 6000.0f }, { id::density, 160.0f }, { id::spread, 100.0f },
        { id::size, 70.0f }, { id::window, 0.0f }, { id::feedback, 92.0f }, { id::panSpread, 90.0f },
        { id::fbHighpass, 250.0f }, { id::fbLowpass, 9000.0f }, { id::diffuse, 70.0f },
        { id::shimmerInterval, 6.0f }, { id::shimmerAmount, 60.0f }, { id::shimmerFine, 6.0f },
        { id::width, 140.0f },
    };

    // The same loop pointed down: an octave lower each trip, drifting flat
    // and losing its top as it goes.
    static const std::vector<Override> undertowTable {
        { id::time, 60.0f }, { id::decay, 7000.0f }, { id::density, 120.0f }, { id::spread, 100.0f },
        { id::size, 120.0f }, { id::window, 2.0f }, { id::feedback, 90.0f }, { id::panSpread, 80.0f },
        { id::fbHighpass, 40.0f }, { id::fbLowpass, 3500.0f }, { id::diffuse, 60.0f },
        { id::shimmerInterval, 0.0f }, { id::shimmerAmount, 55.0f },
        { id::degradeDrift, 12.0f }, { id::degradeDriftDir, 1.0f }, { id::degradeTilt, 80.0f },
        { id::lifetime, 5000.0f },
    };

    // Repeats that fall apart: fewer bits, a lower sample rate and less top
    // every time round, through a hard clip.
    static const std::vector<Override> rotTable {
        { id::time, 330.0f }, { id::decay, 5000.0f }, { id::density, 25.0f }, { id::spread, 12.0f },
        { id::size, 80.0f }, { id::window, 0.0f }, { id::feedback, 88.0f }, { id::panSpread, 30.0f },
        { id::fbHighpass, 120.0f }, { id::fbLowpass, 9000.0f }, { id::diffuse, 0.0f },
        { id::degradeBits, 4.0f }, { id::degradeRate, 25.0f }, { id::degradeTilt, 250.0f },
        { id::degradeNoise, 20.0f }, { id::drive, 30.0f }, { id::satType, 1.0f },
    };

    // Every hit is caught and repeated in sixteenths, and cuts off what the
    // last one left.
    static const std::vector<Override> stutterTable {
        { id::timeSync, 1.0f }, { id::timeDivision, 10.0f }, { id::sync, 1.0f }, { id::grainDivision, 7.0f },
        { id::decay, 1200.0f }, { id::spread, 0.0f }, { id::size, 70.0f }, { id::window, 1.0f },
        { id::feedback, 40.0f }, { id::panSpread, 40.0f }, { id::fbHighpass, 150.0f }, { id::fbLowpass, 10000.0f },
        { id::sensitivity, 60.0f }, { id::retriggerOn, 1.0f }, { id::retriggerCount, 6.0f },
        { id::retriggerDivision, 7.0f }, { id::retriggerAmount, 100.0f },
        { id::chokeOn, 1.0f }, { id::chokeAmount, 70.0f }, { id::chokeFade, 20.0f },
    };

    // A dense cloud of tiny grains that Chaos keeps moving: position, pitch,
    // density and feedback all wander.
    static const std::vector<Override> swarmTable {
        { id::time, 120.0f }, { id::decay, 4000.0f }, { id::density, 220.0f }, { id::spread, 85.0f },
        { id::size, 25.0f }, { id::window, 0.0f }, { id::feedback, 75.0f }, { id::panSpread, 100.0f },
        { id::fbHighpass, 150.0f }, { id::fbLowpass, 12000.0f }, { id::diffuse, 30.0f },
        { id::chaos, 70.0f }, { id::pitchSpread, 35.0f }, { id::reverse, 20.0f }, { id::envDensity, 50.0f },
        { id::width, 160.0f },
    };

    switch (type)
    {
        case Type::shards:   return shardsTable;
        case Type::cascade:  return cascadeTable;
        case Type::undertow: return undertowTable;
        case Type::rot:      return rotTable;
        case Type::stutter:  return stutterTable;
        case Type::swarm:    return swarmTable;
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
