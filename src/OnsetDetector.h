#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

// Spectral-flux onset detector on the mono-summed input — the Refonte
// approach. A 512-point FFT every 128 samples, half-wave-rectified flux
// between consecutive magnitude frames, an adaptive threshold scaled by
// Sensitivity, and a minimum gap between onsets.
//
// Detection lags the hit by roughly half a window plus a hop (~5–8 ms at
// 48 kHz). That is not plugin latency — the dry path is untouched — it is the
// reaction time of every response driven from here, and kDetectionLagSamples
// is what Retrigger uses to look back to where the hit actually landed.
class OnsetDetector
{
public:
    static constexpr int kFftOrder = 9;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kHop      = 128;
    static constexpr int kDetectionLagSamples = kFftSize / 2;

    void prepare (double sampleRate);
    void reset();

    // 0..1. Higher = more onsets.
    void setSensitivity (float sensitivity) noexcept;

    // Consumes a block. Writes the block-relative sample offset of each onset
    // detected into `onsets` (up to maxOnsets) and returns how many.
    int process (const float* const* input, int numChannels, int numSamples,
                 int* onsets, int maxOnsets) noexcept;

    // Monotonic hit counter for the UI indicator.
    uint64_t getOnsetCount() const noexcept { return onsetCount.load (std::memory_order_relaxed); }

private:
    bool analyseFrame() noexcept;

    juce::dsp::FFT fft { kFftOrder };
    std::array<float, kFftSize> window {};
    std::vector<float> history;   // last kFftSize input samples
    int historyPos = 0;
    int hopCounter = 0;

    std::vector<float> fftData;   // 2 * kFftSize scratch
    std::vector<float> previousMagnitude;

    float fluxMean      = 0.0f;
    float previousFlux  = 0.0f;
    float previousEnergy = 0.0f;
    float meanCoeff     = 0.0f;
    float sensitivity   = 0.5f;
    int   minGapSamples = 0;
    int   samplesSinceOnset = 1 << 30;

    std::atomic<uint64_t> onsetCount { 0 };
};
