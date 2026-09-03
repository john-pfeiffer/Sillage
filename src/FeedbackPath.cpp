#include "FeedbackPath.h"

namespace
{
// Limiter ceiling. Kept just below full scale so the buffer never clips even
// with the interpolation overshoot a cubic read can produce.
constexpr float kLimiterThreshold = 0.98f;
constexpr double kLimiterReleaseSeconds = 0.05;

// Shimmer crossfade window. Long enough to keep the artefacts musical, short
// enough that the loop delay it adds stays inside the grain buffer.
constexpr double kShimmerWindowSeconds = 0.05;
constexpr double kShimmerMinDelay = 4.0;

// Prime-ish allpass lengths in milliseconds, offset per channel so the two
// sides decorrelate instead of smearing into a mono cloud.
constexpr std::array<double, 4> kDiffuseMsLeft  { 4.7, 8.3, 13.9, 22.7 };
constexpr std::array<double, 4> kDiffuseMsRight { 5.3, 9.7, 15.1, 25.9 };

// Triangle wavefolder: reflects around +/-1 instead of clipping at it.
float fold (float x) noexcept
{
    float folded = std::fmod (std::abs (x) + 1.0f, 4.0f);
    folded = folded < 2.0f ? folded - 1.0f : 3.0f - folded;
    return x < 0.0f ? -folded : folded;
}

float saturate (float x, int type, float gain) noexcept
{
    const float driven = x * gain;
    // Normalising by sqrt(gain) rather than gain keeps Drive from silently
    // changing the loop gain: the tail gets denser and brighter, not just
    // louder or quieter.
    const float makeup = 1.0f / std::sqrt (gain);

    switch (type)
    {
        case 1:  return juce::jlimit (-1.0f, 1.0f, driven) * makeup;
        case 2:  return fold (driven) * makeup;
        default: return std::tanh (driven) * makeup;
    }
}
} // namespace

// ---- PitchShifter -----------------------------------------------------------

void PitchShifter::prepare (double newSampleRate, int numChannels)
{
    windowSamples = kShimmerWindowSeconds * newSampleRate;
    lineLength    = (int) std::ceil (windowSamples + kShimmerMinDelay) + 4;

    for (int channel = 0; channel < (int) lines.size(); ++channel)
        lines[(size_t) channel].assign ((size_t) lineLength, 0.0f);

    juce::ignoreUnused (numChannels);
    reset();
}

void PitchShifter::reset()
{
    for (auto& line : lines)
        std::fill (line.begin(), line.end(), 0.0f);

    writeIndex = 0;
    phase      = 0.0;
}

void PitchShifter::setSemitones (float semitones) noexcept
{
    rate = std::pow (2.0, (double) semitones / 12.0);

    // Pitching up means the read head must gain on the write head, so the
    // delay shrinks — hence the negative increment for rate > 1.
    phaseIncrement = windowSamples > 0.0 ? (1.0 - rate) / windowSamples : 0.0;
}

float PitchShifter::readLine (int channel, double delaySamples) const noexcept
{
    const auto& line = lines[(size_t) channel];

    double position = (double) writeIndex - delaySamples;
    while (position < 0.0)
        position += (double) lineLength;

    const auto index = (int) position;
    const auto frac  = (float) (position - (double) index);

    const auto a = line[(size_t) (index % lineLength)];
    const auto b = line[(size_t) ((index + 1) % lineLength)];
    return a + frac * (b - a);
}

void PitchShifter::processFrame (float* samples, int numChannels) noexcept
{
    for (int channel = 0; channel < numChannels && channel < (int) lines.size(); ++channel)
        lines[(size_t) channel][(size_t) writeIndex] = samples[channel];

    phase += phaseIncrement;
    while (phase >= 1.0) phase -= 1.0;
    while (phase < 0.0)  phase += 1.0;

    double secondPhase = phase + 0.5;
    if (secondPhase >= 1.0)
        secondPhase -= 1.0;

    const auto delayA = phase * windowSamples + kShimmerMinDelay;
    const auto delayB = secondPhase * windowSamples + kShimmerMinDelay;

    // Complementary Hann gains: the pair sums to exactly 1 at every phase.
    const auto cosine = (float) std::cos (juce::MathConstants<double>::twoPi * phase);
    const auto gainA  = 0.5f - 0.5f * cosine;
    const auto gainB  = 0.5f + 0.5f * cosine;

    for (int channel = 0; channel < numChannels && channel < (int) lines.size(); ++channel)
        samples[channel] = gainA * readLine (channel, delayA)
                         + gainB * readLine (channel, delayB);

    if (++writeIndex >= lineLength)
        writeIndex = 0;
}

// ---- FeedbackPath::Allpass --------------------------------------------------

void FeedbackPath::Allpass::setLength (int length)
{
    line.assign ((size_t) juce::jmax (1, length), 0.0f);
    index = 0;
}

void FeedbackPath::Allpass::reset()
{
    std::fill (line.begin(), line.end(), 0.0f);
    index = 0;
}

float FeedbackPath::Allpass::process (float input, float coefficient) noexcept
{
    const auto delayed = line[(size_t) index];
    const auto stored  = input + coefficient * delayed;

    line[(size_t) index] = stored;
    if (++index >= (int) line.size())
        index = 0;

    return delayed - coefficient * stored;
}

// ---- FeedbackPath -----------------------------------------------------------

void FeedbackPath::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels   = juce::jlimit (1, 2, numChannels);

    juce::dsp::ProcessSpec spec { newSampleRate, 512, (juce::uint32) juce::jmax (1, channels) };

    highpass.prepare (spec);
    highpass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowpass.prepare (spec);
    lowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    shimmer.prepare (newSampleRate, channels);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& lengths = channel == 0 ? kDiffuseMsLeft : kDiffuseMsRight;
        for (int stage = 0; stage < kAllpassStages; ++stage)
            diffusers[(size_t) channel][(size_t) stage].setLength (
                (int) std::round (lengths[(size_t) stage] * 0.001 * newSampleRate));
    }

    limiterRelease = (float) (1.0 - std::exp (-1.0 / (kLimiterReleaseSeconds * newSampleRate)));

    reset();
}

void FeedbackPath::reset()
{
    highpass.reset();
    lowpass.reset();
    shimmer.reset();

    for (auto& channel : diffusers)
        for (auto& stage : channel)
            stage.reset();

    limiterGain = 1.0f;
}

void FeedbackPath::setSettings (const Settings& settings)
{
    current = settings;

    highpass.setCutoffFrequency (juce::jlimit (20.0f, (float) (sampleRate * 0.45), settings.highpassHz));
    lowpass.setCutoffFrequency  (juce::jlimit (20.0f, (float) (sampleRate * 0.45), settings.lowpassHz));

    const auto q = 0.5f + settings.resonance * 5.5f;
    highpass.setResonance (q);
    lowpass.setResonance (q);

    shimmer.setSemitones (settings.shimmerSemitones);

    driveGain    = 1.0f + settings.drive * 11.0f;
    diffuseCoeff = settings.diffuse * 0.7f;
}

void FeedbackPath::processFrame (float* samples, int numChannels) noexcept
{
    const auto active = juce::jmin (numChannels, 2);

    for (int channel = 0; channel < active; ++channel)
    {
        auto value = highpass.processSample (channel, samples[channel]);
        samples[channel] = lowpass.processSample (channel, value);
    }

    if (current.shimmerAmount > 0.0f)
    {
        float shifted[2] { samples[0], active > 1 ? samples[1] : 0.0f };
        shimmer.processFrame (shifted, active);

        const auto amount = current.shimmerAmount;
        for (int channel = 0; channel < active; ++channel)
            samples[channel] += amount * (shifted[channel] - samples[channel]);
    }
    else
    {
        // Keep the shifter's delay line moving so engaging Shimmer mid-tail
        // does not splice in a line full of stale audio.
        float discard[2] { samples[0], active > 1 ? samples[1] : 0.0f };
        shimmer.processFrame (discard, active);
    }

    if (diffuseCoeff > 0.0f)
    {
        for (int channel = 0; channel < active; ++channel)
        {
            auto value = samples[channel];
            for (auto& stage : diffusers[(size_t) channel])
                value = stage.process (value, diffuseCoeff);
            samples[channel] = value;
        }
    }

    for (int channel = 0; channel < active; ++channel)
        samples[channel] = saturate (samples[channel], current.satType, driveGain);

    // Brickwall, stereo-linked so the image does not shift under gain
    // reduction. Instant attack keeps it a true ceiling; the slow release keeps
    // it inaudible below threshold.
    float magnitude = 0.0f;
    for (int channel = 0; channel < active; ++channel)
        magnitude = juce::jmax (magnitude, std::abs (samples[channel]));

    const auto target = magnitude > kLimiterThreshold ? kLimiterThreshold / magnitude : 1.0f;

    if (target < limiterGain)
        limiterGain = target;
    else
        limiterGain += (target - limiterGain) * limiterRelease;

    for (int channel = 0; channel < active; ++channel)
        samples[channel] *= limiterGain;
}
