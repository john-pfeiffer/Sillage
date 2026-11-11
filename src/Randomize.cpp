#include "Randomize.h"

#include "Modulation.h"
#include "Parameters.h"

namespace randomize
{

namespace
{
namespace id = params::id;

// Division indices (params::kDivisions): 4 is 1/32, 13 is 1/4, 16 is 1/2.
const std::vector<Spec>& specs()
{
    static const std::vector<Spec> table = []
    {
        std::vector<Spec> t {
            // Grain: always something to hear.
            { id::time,          20.0f, 1200.0f },
            { id::timeSync,      0.0f, 1.0f, 0.2f },
            { id::timeDivision,  4.0f, 16.0f },
            { id::density,       8.0f, 300.0f },
            { id::size,          10.0f, 400.0f },
            { id::feedback,      0.0f, 100.0f },
            { id::decay,         300.0f, 12000.0f },

            // Pitch: mostly where it was played.
            { id::pitch,         -12.0f, 12.0f, 0.3f, 0.0f },
            { id::pitchFine,     -15.0f, 15.0f, 0.3f, 0.0f },
            { id::pitchSpread,   0.0f, 40.0f, 0.4f, 0.0f },
            { id::reverse,       0.0f, 70.0f, 0.4f, 0.0f },

            // Loop colour.
            { id::fbHighpass,    20.0f, 400.0f },
            { id::fbLowpass,     1500.0f, 20000.0f },
            { id::fbResonance,   5.0f, 40.0f, 0.3f, 0.0f },
            { id::shimmerAmount, 10.0f, 70.0f, 0.35f, 0.0f },
            { id::shimmerFine,   -20.0f, 20.0f, 0.4f, 0.0f },
            { id::drive,         5.0f, 50.0f, 0.4f, 0.0f },
            { id::chaos,         5.0f, 50.0f, 0.4f, 0.0f },

            // Transients: the responses are switches, and mostly off.
            { id::sensitivity,     30.0f, 80.0f },
            { id::retriggerOn,     0.0f, 1.0f, 0.2f },
            { id::retriggerRate,   30.0f, 400.0f },
            { id::retriggerAmount, 50.0f, 100.0f },
            { id::duckOn,          0.0f, 1.0f, 0.2f },
            { id::duckDepth,       20.0f, 80.0f },
            { id::chokeOn,         0.0f, 1.0f, 0.12f },
            { id::chokeAmount,     20.0f, 60.0f },
            { id::envDensity,      -60.0f, 60.0f, 0.3f, 0.0f },
            { id::envSpread,       -60.0f, 60.0f, 0.3f, 0.0f },
            { id::displace,        0.0f, 50.0f, 0.2f, 0.0f },

            // Sync: a long grain division is one grain a bar.
            { id::sync,          0.0f, 1.0f, 0.15f },
            { id::grainDivision, 4.0f, 13.0f },
            { id::swing,         0.0f, 60.0f },

            // Age.
            { id::lifetime,      500.0f, 10000.0f },
            { id::degradeBits,   0.5f, 3.0f, 0.3f, 0.0f },
            { id::degradeRate,   2.0f, 20.0f, 0.3f, 0.0f },
            { id::degradeNoise,  5.0f, 40.0f, 0.25f, 0.0f },
            { id::degradeTilt,   20.0f, 200.0f, 0.35f, 0.0f },
            { id::degradeDrift,  2.0f, 25.0f, 0.3f, 0.0f },

            // Rewind.
            // Rewind: Timer and Threshold re-fire the tail for as long as
            // they are on, so a rolled Rewind is quiet enough to die away.
            { id::rewindOn,        0.0f, 1.0f, 0.12f },
            { id::rewindInterval,  0.5f, 8.0f },
            { id::rewindThreshold, -50.0f, -20.0f },
            { id::rewindLevel,     25.0f, 55.0f },
            { id::rewindPitch,     -12.0f, 12.0f, 0.3f, 0.0f },

            // Output: the wet filters trim, they do not carve.
            { id::width,        60.0f, 160.0f },
            { id::wetHighpass,  40.0f, 300.0f, 0.4f, 20.0f },
            { id::wetLowpass,   3000.0f, 16000.0f, 0.4f, 20000.0f },
        };

        for (auto* curve : id::curveEnable)
            t.push_back ({ curve, 0.0f, 1.0f, 0.15f });

        for (auto* amount : id::modAmount)
            t.push_back ({ amount, -50.0f, 50.0f });

        for (auto* rate : id::lfoRate)
            t.push_back ({ rate, 0.05f, 8.0f });

        for (auto* sync : id::lfoSync)
            t.push_back ({ sync, 0.0f, 1.0f, 0.3f });

        return t;
    }();

    return table;
}

float plain (juce::AudioProcessorValueTreeState& apvts, const char* parameterId)
{
    return apvts.getRawParameterValue (parameterId)->load();
}

void setPlain (juce::AudioProcessorValueTreeState& apvts, const char* parameterId, float value)
{
    if (auto* parameter = apvts.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

// A low-pass at least two octaves above its high-pass.
void keepOpen (juce::AudioProcessorValueTreeState& apvts, const char* highpassId, const char* lowpassId)
{
    constexpr float kOctaves = 4.0f;
    if (plain (apvts, lowpassId) >= plain (apvts, highpassId) * kOctaves)
        return;

    setPlain (apvts, lowpassId, juce::jmin (20000.0f, plain (apvts, highpassId) * kOctaves));
    if (plain (apvts, lowpassId) < plain (apvts, highpassId) * kOctaves)
        setPlain (apvts, highpassId, plain (apvts, lowpassId) / kOctaves);
}
} // namespace

bool isExcluded (const juce::String& parameterId)
{
    for (const auto* excluded : kExcluded)
        if (parameterId == excluded)
            return true;
    return false;
}

const Spec* specFor (const juce::String& parameterId)
{
    for (const auto& spec : specs())
        if (parameterId == spec.id)
            return &spec;
    return nullptr;
}

void apply (juce::AudioProcessorValueTreeState& apvts, float amount, juce::Random& rng)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (amount <= 0.0f)
        return;

    for (auto* parameter : apvts.processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (withId == nullptr || isExcluded (withId->paramID))
            continue;

        const auto* spec     = specFor (withId->paramID);
        const auto  isSwitch = dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr;
        const auto  isChoice = dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr;

        float next;
        if (isSwitch)
        {
            // A switch either flips or it doesn't; "a little" means "less often".
            if (rng.nextFloat() >= amount)
                continue;

            next = rng.nextFloat() < (spec != nullptr ? spec->chance : 0.5f) ? 1.0f : 0.0f;
        }
        else if (isChoice)
        {
            if (rng.nextFloat() >= amount)
                continue;

            const auto last  = juce::jmax (1, parameter->getNumSteps()) - 1;
            const auto first = spec != nullptr ? juce::jlimit (0, last, (int) spec->low) : 0;
            const auto end   = spec != nullptr ? juce::jlimit (first, last, (int) spec->high) : last;
            next = withId->convertTo0to1 ((float) (first + rng.nextInt (end - first + 1)));
        }
        else
        {
            // Lerp in normalised space so skewed ranges move perceptually.
            const auto low  = spec != nullptr ? withId->convertTo0to1 (spec->low) : 0.0f;
            const auto high = spec != nullptr ? withId->convertTo0to1 (spec->high) : 1.0f;

            auto target = low + rng.nextFloat() * (high - low);
            if (spec != nullptr && rng.nextFloat() >= spec->chance)
                target = withId->convertTo0to1 (spec->rest);

            const auto current = parameter->getValue();
            next = current + amount * (target - current);
        }

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, next));
        parameter->endChangeGesture();
    }

    constrain (apvts);
}

void constrain (juce::AudioProcessorValueTreeState& apvts)
{
    keepOpen (apvts, id::fbHighpass, id::fbLowpass);
    keepOpen (apvts, id::wetHighpass, id::wetLowpass);

    // A Decay shorter than Time is one slap and nothing after it.
    const auto decayMs = plain (apvts, id::decay), timeMs = plain (apvts, id::time);
    if (! params::decayIsOff (decayMs) && decayMs < timeMs * 2.0f)
        setPlain (apvts, id::decay, juce::jmin (params::kDecayOffMs - 100.0f, timeMs * 2.0f));

    // Chaos pushes Feedback up by as much as 30 % of itself; together they
    // stay at or under 100 %, where the tail still ends.
    const auto ceiling = 100.0f - 0.3f * plain (apvts, id::chaos);
    if (plain (apvts, id::feedback) > ceiling)
        setPlain (apvts, id::feedback, ceiling);

    // Mod slots: never the user's Mix, and only a few at once — six random
    // routings at full depth are noise, not modulation.
    int live = 0;
    for (size_t s = 0; s < id::modSource.size(); ++s)
    {
        if ((int) plain (apvts, id::modDestination[s]) == (int) mod::Destination::mix)
            setPlain (apvts, id::modDestination[s], (float) mod::Destination::none);

        const auto isLive = (int) plain (apvts, id::modSource[s]) != (int) mod::Source::none
                         && (int) plain (apvts, id::modDestination[s]) != (int) mod::Destination::none
                         && std::abs (plain (apvts, id::modAmount[s])) > 0.5f;

        if (isLive && ++live > kMaxLiveModSlots)
            setPlain (apvts, id::modSource[s], (float) mod::Source::none);
    }
}

void constrain (lifetime::CurveSet& curves)
{
    using lifetime::Destination;

    // y limits per destination: Level never below a third, the low-pass never
    // under about 1.2 kHz, the high-pass never over about 300 Hz, 8 bits and
    // 8 kHz at worst, grains between 8 and 500 ms, pitch within an octave.
    const auto limit = [&] (Destination destination, float low, float high)
    {
        auto& curve = curves.curves[(size_t) destination];
        for (int i = 0; i < juce::jlimit (0, lifetime::kMaxPoints, curve.numPoints); ++i)
            curve.points[(size_t) i].y = juce::jlimit (low, high, curve.points[(size_t) i].y);
    };

    limit (Destination::level,       0.3f, 1.0f);
    limit (Destination::lowpass,     0.4f, 1.0f);
    limit (Destination::highpass,    0.0f, 0.5f);
    limit (Destination::bitDepth,    0.2f, 1.0f);
    limit (Destination::sampleRate,  0.3f, 1.0f);
    limit (Destination::grainSize,   0.2f, 0.8f);
    limit (Destination::pitchOffset, 0.25f, 0.75f);
}

} // namespace randomize
