#include "Modulation.h"

namespace mod
{

namespace
{
template <typename Array>
juce::StringArray toStringArray (const Array& names)
{
    juce::StringArray result;
    for (const auto* name : names)
        result.add (name);
    return result;
}
} // namespace

juce::StringArray sourceNames()      { return toStringArray (kSourceNames); }
juce::StringArray destinationNames() { return toStringArray (kDestinationNames); }
juce::StringArray curveNames()       { return toStringArray (kCurveNames); }
juce::StringArray lfoShapeNames()    { return toStringArray (kLfoShapeNames); }

const char* destinationParameterId (Destination destination) noexcept
{
    namespace id = params::id;
    switch (destination)
    {
        case Destination::time:          return id::time;
        case Destination::density:       return id::density;
        case Destination::spread:        return id::spread;
        case Destination::size:          return id::size;
        case Destination::feedback:      return id::feedback;
        case Destination::pitch:         return id::pitch;
        case Destination::pitchSpread:   return id::pitchSpread;
        case Destination::panSpread:     return id::panSpread;
        case Destination::reverse:       return id::reverse;
        case Destination::loopHighpass:  return id::fbHighpass;
        case Destination::loopLowpass:   return id::fbLowpass;
        case Destination::shimmerAmount: return id::shimmerAmount;
        case Destination::shimmerFine:   return id::shimmerFine;
        case Destination::diffuse:       return id::diffuse;
        case Destination::drive:         return id::drive;
        case Destination::degradeBits:   return id::degradeBits;
        case Destination::degradeRate:   return id::degradeRate;
        case Destination::degradeNoise:  return id::degradeNoise;
        case Destination::degradeTilt:   return id::degradeTilt;
        case Destination::degradeDrift:  return id::degradeDrift;
        case Destination::rewindLevel:   return id::rewindLevel;
        case Destination::displace:      return id::displace;
        case Destination::mix:           return id::mix;
        case Destination::decay:         return id::decay;
        case Destination::none:
        case Destination::count:
        default:                         return nullptr;
    }
}

// ---- Lfo ---------------------------------------------------------------------

void Lfo::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void Lfo::reset()
{
    phase     = 0.0;
    value     = 0.0f;
    held      = 0.5f;
    cycles    = 0;
    lastCycle = -1;
}

float Lfo::advance (int numSamples, double hz, float phaseOffset, LfoShape shape) noexcept
{
    phase += juce::jmax (0.0, hz) * (double) numSamples / sampleRate;
    const auto wraps = std::floor (phase);
    phase  -= wraps;
    cycles += (int64_t) wraps;

    auto t = phase + (double) phaseOffset;
    t -= std::floor (t);

    // Sample-and-hold draws once per cycle, at the wrap.
    if (shape == LfoShape::sampleHold && cycles != lastCycle)
    {
        held      = rng.nextFloat();
        lastCycle = cycles;
    }

    switch (shape)
    {
        case LfoShape::sine:       value = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * t); break;
        case LfoShape::triangle:   value = (float) (t < 0.5 ? t * 2.0 : 2.0 - t * 2.0); break;
        case LfoShape::saw:        value = (float) t; break;
        case LfoShape::square:     value = t < 0.5 ? 1.0f : 0.0f; break;
        case LfoShape::sampleHold: value = held; break;
        case LfoShape::count:
        default:                   value = 0.0f; break;
    }

    return value;
}

// ---- TransientEnvelope -------------------------------------------------------

void TransientEnvelope::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void TransientEnvelope::reset()
{
    value = 0.0f;
}

float TransientEnvelope::advance (int numSamples, double decaySeconds) noexcept
{
    // Decay is the time to fall to -60 dB.
    const auto tau = juce::jmax (1.0e-3, decaySeconds) / 6.9078;
    value *= (float) std::exp (-(double) numSamples / (tau * sampleRate));
    if (value < 1.0e-6f)
        value = 0.0f;
    return value;
}

} // namespace mod
