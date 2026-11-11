#pragma once

#include <algorithm>
#include <cmath>

// Decay: how long the tail lives, whatever Feedback says — and Feedback: how
// much of that tail is rebuilt by the loop rather than played once.
//
// With Decay on, one trip through the buffer costs 60 dB per `window` of time
// the audio sat there. At Spread 100 that alone is the whole tail: a cloud of
// reads falling to -60 dB at the window. Feedback then hands part of the tail
// to the loop. The loop's share q of the energy grows with Feedback, and the
// one-pass window shrinks to match, so the two together still reach -60 dB at
// Decay while everything in the loop (Damping, Shimmer, Diffuse, Drive,
// Degrade) colours a share q of it:
//
//     window = Decay * (1 - q) / Spread        (never longer than Decay)
//     loop power gain per trip = q * (the fixed delay's own loss)
//
// With taps spread exponentially over the window and a loop gain of q, the
// summed response is again exponential with the rate of Decay — exactly at
// Spread 100, and shorter than Decay below it, where the cloud is shorter.
// At Spread 0 there is no cloud: every repeat is the loop, and its gain is
// Feedback times the loss of one Time at the rate of Decay.
//
// Off is not a long Decay: it resolves to a neutral model and the engine
// takes none of these paths.
namespace decay
{

// ln(1000): a level that falls by this exponent per window is -60 dB there.
inline constexpr double kRt60 = 6.907755;

// How the one-trip loss is split between how often old audio is read and how
// loud it is when it is. All in the level (0) wastes most grains on audio
// that is inaudible; all in the odds (1) plays the far tail as rare full-level
// grains. Half and half keeps the cloud dense and the far tail quiet.
inline constexpr double kDensityShare = 0.5;

// The loop's share of the tail at Feedback 100 %, Spread 100. Below 1 so the
// tail still ends; Feedback past about 108 % takes it past 1 on purpose.
inline constexpr double kLoopShareAtUnity = 0.85;

// The one-pass window never shrinks below this fraction of Decay.
inline constexpr double kMinWindowShare = 0.1;

struct Model
{
    double windowSeconds = 0.0; // one-trip -60 dB window; <= 0 is Off
    double loopGain      = 1.0; // multiplies Feedback on the way back in
    double wetGain       = 1.0; // holds the wet level as the loop takes over
    double tripSeconds   = 0.35; // expected length of one trip, for per-pass Degrade
};

inline Model resolve (double decaySeconds, double timeSeconds, double spread,
                      double feedback, double grainSeconds, double bufferSeconds) noexcept
{
    Model model;
    spread   = std::clamp (spread, 0.0, 1.0);
    feedback = std::max (0.0, feedback);

    if (decaySeconds <= 0.0)
    {
        // Off: Spread runs from Time to anywhere in the buffer.
        model.tripSeconds = std::max (1.0e-3, std::max (0.0, timeSeconds) * (1.0 - spread)
                                                  + spread * bufferSeconds * 0.5);
        return model;
    }

    // The fixed part of every trip: Time, plus the grain length the cloud
    // starts behind it (see spawnScheduledGrain for why).
    const auto fixedSeconds = std::max (0.0, timeSeconds) + spread * std::max (0.0, grainSeconds);

    // The loop's share of the tail: Feedback squared, held just under 1 at
    // Feedback 100 % where there is a cloud to rebuild. The window follows it
    // down to its floor; past that the share keeps growing and the tail
    // outlives Decay, which is what Feedback above 100 % is for.
    const auto headroom = 1.0 - (1.0 - kLoopShareAtUnity) * spread;
    const auto share    = headroom * feedback * feedback;
    const auto followed = std::min (share, 1.0 - kMinWindowShare);

    model.windowSeconds = std::min (decaySeconds,
                                    decaySeconds * (1.0 - followed) / std::max (spread, 1.0e-3));

    // One trip always costs the fixed delay at the rate of Decay.
    const auto fixedLoss = std::exp (-kRt60 * fixedSeconds / decaySeconds);
    model.loopGain = std::sqrt (headroom) * fixedLoss;

    // The tail's energy is 1 / (1 - loop power gain); take that back out of
    // the cloud so Feedback changes what the tail is made of, not how loud it
    // is. A plain delay keeps its first repeat at unity.
    const auto loopPower = std::min (share * fixedLoss * fixedLoss, 0.9);
    model.wetGain = std::sqrt (1.0 - spread * loopPower);

    const auto meanCloud = model.windowSeconds / (2.0 * kRt60 * kDensityShare);
    model.tripSeconds = std::max (1.0e-3, fixedSeconds + spread * meanCloud);
    return model;
}

} // namespace decay
