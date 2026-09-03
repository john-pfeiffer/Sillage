#include "OnsetDetector.h"

namespace
{
constexpr double kMinGapSeconds  = 0.040;
constexpr double kMeanSeconds    = 0.500; // adaptive-threshold memory
constexpr float  kFluxFloor      = 0.01f; // never fire on silence noise
constexpr float  kEnergyHoldRatio = 1.0f; // an onset never lowers frame energy

// Sensitivity maps to the multiple of the running mean a frame must exceed.
float thresholdMultiple (float sensitivity) noexcept
{
    return 1.5f + (1.0f - sensitivity) * 6.0f;
}
} // namespace

void OnsetDetector::prepare (double sampleRate)
{
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) i / (float) kFftSize);

    history.assign ((size_t) kFftSize, 0.0f);
    fftData.assign ((size_t) kFftSize * 2, 0.0f);
    previousMagnitude.assign ((size_t) kFftSize / 2 + 1, 0.0f);

    minGapSamples = (int) (kMinGapSeconds * sampleRate);
    meanCoeff     = (float) (1.0 - std::exp (-(double) kHop / (kMeanSeconds * sampleRate)));

    reset();
}

void OnsetDetector::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (previousMagnitude.begin(), previousMagnitude.end(), 0.0f);
    historyPos        = 0;
    hopCounter        = 0;
    fluxMean          = 0.0f;
    previousFlux      = 0.0f;
    previousEnergy    = 0.0f;
    samplesSinceOnset = 1 << 30;
}

void OnsetDetector::setSensitivity (float newSensitivity) noexcept
{
    sensitivity = juce::jlimit (0.0f, 1.0f, newSensitivity);
}

bool OnsetDetector::analyseFrame() noexcept
{
    // Unroll the history ring into the FFT buffer, oldest sample first.
    for (int i = 0; i < kFftSize; ++i)
    {
        const auto index = (historyPos + i) % kFftSize;
        fftData[(size_t) i] = history[(size_t) index] * window[(size_t) i];
    }
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // Normalise so a full-scale sine reads ~1 in its bin, then take the
    // half-wave-rectified difference against the previous frame.
    constexpr float scale = 2.0f / (float) kFftSize;
    float flux = 0.0f, energy = 0.0f;
    for (size_t bin = 1; bin < previousMagnitude.size(); ++bin)
    {
        const auto magnitude = fftData[bin] * scale;
        flux   += juce::jmax (0.0f, magnitude - previousMagnitude[bin]);
        energy += magnitude * magnitude;
        previousMagnitude[bin] = magnitude;
    }

    // An abrupt *offset* splatters energy across bins too, which reads as
    // positive flux. A hit raises the frame's energy; a cut-off lowers it —
    // and energy has to be the sum of squares, since leakage can raise the
    // sum of magnitudes while the actual energy falls.
    const auto threshold  = juce::jmax (kFluxFloor, fluxMean * thresholdMultiple (sensitivity));
    const auto rising     = flux > threshold && previousFlux <= threshold;
    const auto notFalling = energy >= previousEnergy * kEnergyHoldRatio;
    const auto onset      = rising && notFalling && samplesSinceOnset >= minGapSamples;

    fluxMean      += (flux - fluxMean) * meanCoeff;
    previousFlux   = flux;
    previousEnergy = energy;

    return onset;
}

int OnsetDetector::process (const float* const* input, int numChannels, int numSamples,
                            int* onsets, int maxOnsets) noexcept
{
    int found = 0;
    const auto channelScale = 1.0f / (float) juce::jmax (1, numChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            mono += input[channel][i];

        history[(size_t) historyPos] = mono * channelScale;
        if (++historyPos >= kFftSize)
            historyPos = 0;

        ++samplesSinceOnset;

        if (++hopCounter >= kHop)
        {
            hopCounter = 0;

            if (analyseFrame())
            {
                samplesSinceOnset = 0;
                onsetCount.fetch_add (1, std::memory_order_relaxed);
                if (found < maxOnsets)
                    onsets[found++] = i;
            }
        }
    }

    return found;
}
