#include "Modulators.h"

namespace
{
constexpr double kChaosMinHz = 0.1;
constexpr double kChaosMaxHz = 20.0;

// Fraction of a hold interval the slew takes to settle: fast sources jitter,
// slow ones drift.
constexpr double kSlewFraction = 0.3;

float coefficientFor (double seconds, double sampleRate) noexcept
{
    return (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0e-5, seconds) * sampleRate)));
}
} // namespace

// ---- SmoothedRandom ---------------------------------------------------------

void SmoothedRandom::prepare (double newSampleRate, juce::Random& rng)
{
    sampleRate = newSampleRate;
    reset (rng);
}

void SmoothedRandom::reset (juce::Random& rng)
{
    current = 0.0f;
    drawNext (rng);
}

void SmoothedRandom::drawNext (juce::Random& rng) noexcept
{
    target = rng.nextFloat() * 2.0f - 1.0f;

    const auto hz = kChaosMinHz * std::pow (kChaosMaxHz / kChaosMinHz, rng.nextDouble());
    holdRemaining = sampleRate / hz;
    slewSamples   = juce::jmax (1.0, holdRemaining * kSlewFraction);
}

float SmoothedRandom::advance (int numSamples, juce::Random& rng) noexcept
{
    holdRemaining -= (double) numSamples;
    while (holdRemaining <= 0.0)
    {
        const auto overshoot = -holdRemaining;
        drawNext (rng);
        holdRemaining -= overshoot;
    }

    const auto coefficient = (float) (1.0 - std::exp (-(double) numSamples / slewSamples));
    current += (target - current) * coefficient;
    return current;
}

// ---- ChaosModulator ---------------------------------------------------------

void ChaosModulator::prepare (double sampleRate)
{
    for (auto& source : sources)
        source.prepare (sampleRate, rng);
}

void ChaosModulator::reset()
{
    for (auto& source : sources)
        source.reset (rng);
}

ChaosValues ChaosModulator::advance (int numSamples) noexcept
{
    ChaosValues values;
    values.position    = sources[0].advance (numSamples, rng);
    values.pitch       = sources[1].advance (numSamples, rng);
    values.feedback    = sources[2].advance (numSamples, rng);
    values.density     = sources[3].advance (numSamples, rng);
    values.shimmerFine = sources[4].advance (numSamples, rng);
    return values;
}

// ---- EnvelopeFollower -------------------------------------------------------

void EnvelopeFollower::prepare (double sampleRate)
{
    attackCoeff      = coefficientFor (0.005, sampleRate);
    releaseCoeff     = coefficientFor (0.100, sampleRate);
    slowReleaseCoeff = coefficientFor (2.000, sampleRate);
    reset();
}

void EnvelopeFollower::reset()
{
    fast = slow = normalised = 0.0f;
}

float EnvelopeFollower::process (const float* const* input, int numChannels, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        float magnitude = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            magnitude = juce::jmax (magnitude, std::abs (input[channel][i]));

        fast += (magnitude - fast) * (magnitude > fast ? attackCoeff : releaseCoeff);

        // The slow tracker rides the fast one's peaks and decays over seconds,
        // so fast/slow is "how loud right now, relative to lately".
        if (fast > slow)
            slow = fast;
        else
            slow += (fast - slow) * slowReleaseCoeff;
    }

    normalised = slow > 1.0e-4f ? juce::jlimit (0.0f, 1.0f, fast / slow) : 0.0f;
    return normalised;
}

// ---- DuckEnvelope -----------------------------------------------------------

void DuckEnvelope::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    setTimes (5.0f, 250.0f);
    reset();
}

void DuckEnvelope::reset()
{
    envelope   = 0.0f;
    attackLeft = 0;
}

void DuckEnvelope::setTimes (float attackMs, float releaseMs) noexcept
{
    attackSamples = juce::jmax (1, (int) (attackMs * 0.001f * (float) sampleRate));
    // Reach ~95 % of the way within the attack time.
    attackCoeff  = (float) (1.0 - std::exp (-3.0 / (double) attackSamples));
    releaseCoeff = coefficientFor ((double) releaseMs * 0.001, sampleRate);
}

void DuckEnvelope::trigger() noexcept
{
    attackLeft = attackSamples;
}

float DuckEnvelope::next (float depth) noexcept
{
    if (attackLeft > 0)
    {
        --attackLeft;
        envelope += (1.0f - envelope) * attackCoeff;
    }
    else
    {
        envelope += (0.0f - envelope) * releaseCoeff;
    }

    return 1.0f - depth * envelope;
}
