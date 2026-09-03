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

// Degrade floors from the handoff.
constexpr float  kMinBits         = 4.0f;
constexpr double kMinSampleRateHz = 4000.0;
constexpr float  kMinTiltHz       = 200.0f;
constexpr float  kNoiseFullScale  = 0.01f;   // -40 dB at Noise = 100 %

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
    drift.prepare (newSampleRate, channels);

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
    drift.reset();

    for (auto& channel : diffusers)
        for (auto& stage : channel)
            stage.reset();

    lastPasses = -1;
    holdPhase  = 0.0;
    held.fill (0.0f);
    tiltState.fill (0.0f);
    for (auto& state : pinkState)
        state.fill (0.0f);
    driftRedrawLeft = 0.0;

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

    degradeActive = settings.degradeBitsPerPass > 0.0f
                 || settings.baseBits < 24.0f
                 || settings.degradeRatePerPass > 0.0f
                 || (settings.baseSampleRateHz > 0.0f && settings.baseSampleRateHz < sampleRate * 0.999)
                 || settings.degradeTiltHzPerPass > 0.0f
                 || settings.degradeNoise > 0.0f;

    driftActive = settings.degradeDriftCents > 0.0f;
    if (settings.driftDirection == 0)      driftSign = 1.0f;
    else if (settings.driftDirection == 1) driftSign = -1.0f;
    drift.setSemitones (driftSign * settings.degradeDriftCents * 0.01f);

    lastPasses = -1; // force a recompute on the next frame
}

void FeedbackPath::updateDegradeForPasses (int passes) noexcept
{
    lastPasses = passes;
    const auto p = (float) passes;

    const auto bits = juce::jlimit (kMinBits, 24.0f, current.baseBits - p * current.degradeBitsPerPass);
    quantStep = bits < 23.99f ? std::exp2 (1.0f - std::round (bits)) : 0.0f;

    const auto baseRate = current.baseSampleRateHz > 0.0f ? (double) current.baseSampleRateHz : sampleRate;
    const auto rate     = juce::jmax (kMinSampleRateHz,
                                      baseRate * std::pow (1.0 - (double) current.degradeRatePerPass, (double) passes));
    holdRatio = rate < sampleRate * 0.999 ? sampleRate / rate : 1.0;

    if (current.degradeTiltHzPerPass > 0.0f)
    {
        const auto cutoff = juce::jmax (kMinTiltHz, current.lowpassHz - p * current.degradeTiltHzPerPass);
        tiltCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * (double) cutoff / sampleRate));
    }
    else
    {
        tiltCoeff = 1.0f;
    }
}

float FeedbackPath::pinkNoise (int channel) noexcept
{
    // Paul Kellet's economy pink filter: three one-poles on white noise.
    const auto white = noiseRng.nextFloat() * 2.0f - 1.0f;
    auto& s = pinkState[(size_t) channel];
    s[0] = 0.99765f * s[0] + white * 0.0990460f;
    s[1] = 0.96300f * s[1] + white * 0.2965164f;
    s[2] = 0.57000f * s[2] + white * 1.0526913f;
    return (s[0] + s[1] + s[2] + white * 0.1848f) * 0.2f;
}

void FeedbackPath::processFrame (float* samples, int numChannels, float ageSeconds) noexcept
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

    // Degrade: cheap, cumulative, and driven by how old the audio is — the way
    // tape and BBD delays fall apart a little more on every pass.
    if (degradeActive)
    {
        const auto passes = (int) (juce::jmax (0.0f, ageSeconds) / (float) juce::jmax (0.001, current.passSeconds));
        if (passes != lastPasses)
            updateDegradeForPasses (passes);

        if (holdRatio > 1.0)
        {
            holdPhase += 1.0;
            if (holdPhase >= holdRatio)
            {
                holdPhase -= holdRatio;
                for (int channel = 0; channel < active; ++channel)
                    held[(size_t) channel] = samples[channel];
            }
            for (int channel = 0; channel < active; ++channel)
                samples[channel] = held[(size_t) channel];
        }

        if (quantStep > 0.0f)
            for (int channel = 0; channel < active; ++channel)
                samples[channel] = std::round (samples[channel] / quantStep) * quantStep;

        if (tiltCoeff < 1.0f)
            for (int channel = 0; channel < active; ++channel)
            {
                auto& state = tiltState[(size_t) channel];
                state += (samples[channel] - state) * tiltCoeff;
                samples[channel] = state;
            }

        if (current.degradeNoise > 0.0f)
            for (int channel = 0; channel < active; ++channel)
                samples[channel] += pinkNoise (channel) * current.degradeNoise * kNoiseFullScale;
    }

    if (driftActive)
    {
        // Random direction re-draws once per pass so the drift wanders rather
        // than running away in one direction.
        if (current.driftDirection == 2)
        {
            driftRedrawLeft -= 1.0;
            if (driftRedrawLeft <= 0.0)
            {
                driftRedrawLeft = juce::jmax (1.0, current.passSeconds * sampleRate);
                driftSign = noiseRng.nextBool() ? 1.0f : -1.0f;
                drift.setSemitones (driftSign * current.degradeDriftCents * 0.01f);
            }
        }

        float shifted[2] { samples[0], active > 1 ? samples[1] : 0.0f };
        drift.processFrame (shifted, active);
        for (int channel = 0; channel < active; ++channel)
            samples[channel] = shifted[channel];
    }
    else
    {
        float discard[2] { samples[0], active > 1 ? samples[1] : 0.0f };
        drift.processFrame (discard, active);
    }

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
