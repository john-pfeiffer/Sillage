#include "GrainEngine.h"

#include <limits>

namespace
{
// Grain envelopes are looked up rather than evaluated: no cos/exp in the
// per-sample loop keeps the cost of a grain flat, which is what the 8-instance
// CPU budget needs.
constexpr int kWindowTableSize = 1024;

using WindowTable  = std::array<float, kWindowTableSize + 1>;
using WindowTables = std::array<WindowTable, (size_t) WindowShape::numShapes>;

WindowTables buildWindowTables()
{
    WindowTables tables {};

    for (size_t shape = 0; shape < tables.size(); ++shape)
    {
        for (int i = 0; i <= kWindowTableSize; ++i)
        {
            const double t = (double) i / (double) kWindowTableSize;
            double value = 0.0;

            switch ((WindowShape) shape)
            {
                case WindowShape::hann:
                    value = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t);
                    break;

                case WindowShape::trapezoid:
                {
                    constexpr double ramp = 0.1;
                    value = t < ramp        ? t / ramp
                          : t > 1.0 - ramp  ? (1.0 - t) / ramp
                                            : 1.0;
                    break;
                }

                case WindowShape::tukey:
                {
                    constexpr double taper = 0.25;
                    if (t < taper)
                        value = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t / taper);
                    else if (t > 1.0 - taper)
                        value = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (1.0 - t) / taper);
                    else
                        value = 1.0;
                    break;
                }

                case WindowShape::expoDecay:
                {
                    // Fast attack then an exponential fall: reads as a plucked hit.
                    constexpr double attack = 0.01;
                    value = t < attack ? t / attack
                                       : std::exp (-5.0 * (t - attack) / (1.0 - attack));
                    // Force the tail to zero so a decaying grain still ends silently.
                    constexpr double release = 0.02;
                    if (t > 1.0 - release)
                        value *= (1.0 - t) / release;
                    break;
                }

                case WindowShape::numShapes:
                default:
                    break;
            }

            tables[shape][(size_t) i] = (float) value;
        }

        tables[shape][kWindowTableSize] = 0.0f;
    }

    return tables;
}

const WindowTables& windowTables()
{
    static const WindowTables tables = buildWindowTables();
    return tables;
}

float windowValue (const WindowTable& table, double normalisedPosition) noexcept
{
    const double scaled = normalisedPosition * (double) kWindowTableSize;

    if (scaled <= 0.0)
        return table[0];
    if (scaled >= (double) kWindowTableSize)
        return table[kWindowTableSize];

    const auto index = (size_t) scaled;
    const auto frac  = (float) (scaled - (double) index);
    return table[index] + frac * (table[index + 1] - table[index]);
}

// Milliseconds of fade applied to a grain that gets stolen under overload.
constexpr double kStealFadeSeconds = 0.002;

// Samples of clearance kept between any read head and the write head.
constexpr double kReadGuard = 8.0;
} // namespace

void GrainEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels   = juce::jlimit (1, 2, numChannels);
    capacity   = (int) std::ceil (kBufferSeconds * newSampleRate);

    ring.setSize (channels, capacity);
    feedbackHistory.setSize (channels, kFeedbackHistorySize);
    stealFadeStep = (float) (1.0 / (kStealFadeSeconds * newSampleRate));

    feedbackSmoothed.reset (newSampleRate, 0.02);

    feedbackPath.prepare (newSampleRate, channels);
    reset();
}

void GrainEngine::reset()
{
    ring.clear();
    feedbackHistory.clear();
    writePos = 0;

    for (auto& grain : grains)
    {
        grain.active = false;
        grain.stolen = false;
    }

    activeGrains          = 0;
    samplesUntilNextGrain = 0.0;
    slotParity            = false;
    writeGain             = 1.0f;
    burst                 = {};
    chokeSamplesLeft      = 0;
    chokePositionsLeft    = 0;

    feedbackSmoothed.setCurrentAndTargetValue (0.0f);
    feedbackPath.reset();
}

float GrainEngine::readInterpolated (int channel, double position) const noexcept
{
    const auto* data = ring.getReadPointer (juce::jmin (channel, channels - 1));

    const auto base = (int) position;
    const auto frac = position - (double) base;

    // Grains at rate 1 sit exactly on samples, which is the common case in a
    // plain delay — take the cheap path there and interpolate only when pitched.
    if (frac < 1.0e-9)
        return data[(size_t) (base % capacity)];

    const auto wrap = [this] (int index) noexcept
    {
        index %= capacity;
        return index < 0 ? index + capacity : index;
    };

    const auto y0 = data[(size_t) wrap (base - 1)];
    const auto y1 = data[(size_t) wrap (base)];
    const auto y2 = data[(size_t) wrap (base + 1)];
    const auto y3 = data[(size_t) wrap (base + 2)];

    // Catmull-Rom.
    const auto f  = (float) frac;
    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 =         y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;

    return ((a0 * f + a1) * f + a2) * f + y1;
}

int GrainEngine::claimSlot()
{
    for (int slot = 0; slot < kGrainSlots; ++slot)
        if (! grains[(size_t) slot].active)
            return slot;

    return -1;
}

float GrainEngine::resolvePitchRate (const Settings& settings) noexcept
{
    // Pitch resolves once, at birth, and holds for the grain's life. Chaos
    // lands before Quantize so a chaotic tail still snaps to the scale.
    auto semitones = settings.pitchSemitones + settings.pitchModSemitones;
    if (settings.pitchSpread > 0.0f)
        semitones += settings.pitchSpread * 12.0f * (rng.nextFloat() * 2.0f - 1.0f);
    semitones = scales::snapToScale (semitones, settings.quantize, settings.quantizeRoot);

    return (float) std::pow (2.0, (double) semitones / 12.0);
}

void GrainEngine::activateGrain (double startPos, double rate, const Settings& settings, float gainScale)
{
    if (activeGrains >= kMaxGrains)
    {
        // At the cap: steal the oldest sounding grain. It keeps its slot while
        // it fades over a couple of milliseconds — a fade, not a click — but it
        // stops counting against the cap right away so this grain gets in.
        int     oldest      = -1;
        int64_t oldestBirth = std::numeric_limits<int64_t>::max();

        for (int slot = 0; slot < kGrainSlots; ++slot)
        {
            auto& candidate = grains[(size_t) slot];
            if (candidate.active && ! candidate.stolen && candidate.birth < oldestBirth)
            {
                oldestBirth = candidate.birth;
                oldest      = slot;
            }
        }

        if (oldest >= 0)
        {
            grains[(size_t) oldest].stolen   = true;
            grains[(size_t) oldest].fadeStep = stealFadeStep;
            --activeGrains;
        }
    }

    const auto slot = claimSlot();
    if (slot < 0)
        return; // every slot is busy fading; dropping one trigger is inaudible

    auto& grain = grains[(size_t) slot];
    const auto length = juce::jmax (1.0, settings.sizeSamples);

    while (startPos < 0.0)                startPos += (double) capacity;
    while (startPos >= (double) capacity) startPos -= (double) capacity;

    // Equal-power pan that stays exactly unity per channel at centre, so Pan
    // Spread at 0 leaves the source's own stereo image untouched.
    const auto pan   = settings.panSpread * (rng.nextFloat() * 2.0f - 1.0f);
    const auto theta = ((double) pan + 1.0) * juce::MathConstants<double>::pi * 0.25;

    // Overlapping grains sum, so compensate by the expected overlap or Density
    // and Size would double as volume controls.
    const auto overlap      = settings.density * (length / sampleRate);
    const auto compensation = (float) (1.0 / std::sqrt (juce::jmax (1.0, overlap))) * gainScale;

    grain.gainLeft  = juce::MathConstants<float>::sqrt2 * (float) std::cos (theta) * compensation;
    grain.gainRight = juce::MathConstants<float>::sqrt2 * (float) std::sin (theta) * compensation;

    grain.readPos   = startPos;
    grain.rate      = rate;
    grain.pos       = 0.0;
    grain.length    = length;
    grain.invLength = 1.0 / length;
    grain.window    = settings.window;
    grain.fade      = 1.0f;
    grain.fadeStep  = 0.0f;
    grain.stolen    = false;
    grain.scale     = 1.0f;
    grain.scaleTarget = 1.0f;
    grain.scaleStep = 0.0f;
    grain.scaleSamplesLeft = 0;

    // A grain born while a choke is still fading may read audio the sweep has
    // not reached yet, so it joins the same ramp instead of cutting off later.
    if (chokeSamplesLeft > 0)
    {
        grain.scaleTarget      = chokeGain;
        grain.scaleStep        = (chokeGain - 1.0f) / (float) chokeSamplesLeft;
        grain.scaleSamplesLeft = chokeSamplesLeft;
    }

    grain.birth     = grainCounter++;
    grain.active    = true;

    ++activeGrains;
}

void GrainEngine::spawnScheduledGrain (const Settings& settings)
{
    const auto rate     = (double) resolvePitchRate (settings);
    const auto reversed = settings.reverseProbability > 0.0f
                       && rng.nextFloat() < settings.reverseProbability;

    const auto length = juce::jmax (1.0, settings.sizeSamples);
    const auto span   = length * rate; // source samples this grain consumes

    // Keep the read head clear of the write head for the grain's whole life. A
    // grain pitched up gains on the write head; a reversed grain needs its
    // whole source region to already exist behind it.
    auto minOffset = kReadGuard + (reversed ? span : juce::jmax (0.0, length * (rate - 1.0)));
    auto maxOffset = (double) capacity - kReadGuard
                   - (reversed ? length : length * juce::jmax (0.0, 1.0 - rate));

    if (minOffset > maxOffset)
        minOffset = maxOffset;

    // Spread interpolates from "exactly Time" to "anywhere in the buffer".
    // Interpolating rather than clamping a random deviation is what keeps the
    // distribution smooth instead of piling grains up at the limits. Chaos
    // then pushes the result around on its own slow clock.
    const auto base         = juce::jlimit (minOffset, maxOffset, settings.timeSamples);
    const auto randomOffset = minOffset + rng.nextDouble() * (maxOffset - minOffset);
    const auto offset       = juce::jlimit (minOffset, maxOffset,
                                            base + (double) settings.spread * (randomOffset - base)
                                                 + settings.timeModSamples);

    const auto startPos = (double) writePos - offset + (reversed ? span : 0.0);
    activateGrain (startPos, reversed ? -rate : rate, settings, 1.0f);
}

void GrainEngine::spawnBurstGrain (const Settings& settings)
{
    // Every grain in a burst reads the same hit, so the hit stutters rather
    // than smears. Pitched-up grains still need their guard against the head.
    const auto rate   = (double) resolvePitchRate (settings);
    const auto length = juce::jmax (1.0, settings.sizeSamples);

    auto offsetBehind = (double) writePos - burst.hitPos;
    while (offsetBehind < 0.0) offsetBehind += (double) capacity;

    const auto minOffset = kReadGuard + juce::jmax (0.0, length * (rate - 1.0));
    const auto startPos  = offsetBehind < minOffset ? (double) writePos - minOffset : burst.hitPos;

    activateGrain (startPos, rate, settings, burst.amount);
}

void GrainEngine::beginBurst (const TransientResponse& response)
{
    burst.remaining = juce::jlimit (1, 16, response.burstCount);
    burst.interval  = juce::jmax (1.0, response.burstIntervalSamples);
    burst.nextIn    = 0.0; // first grain fires on this sample
    burst.amount    = response.burstAmount;

    auto hitPos = (double) writePos - juce::jmax (kReadGuard, response.burstOffsetSamples);
    while (hitPos < 0.0) hitPos += (double) capacity;
    burst.hitPos = hitPos;
}

void GrainEngine::beginChoke (const TransientResponse& response)
{
    const auto fade   = juce::jmax (1, (int) response.chokeFadeSamples);
    const auto factor = 1.0f - juce::jlimit (0.0f, 1.0f, response.chokeAmount);

    // In-flight grains ramp down over the fade...
    for (auto& grain : grains)
    {
        if (! grain.active)
            continue;

        grain.scaleTarget      = grain.scale * factor;
        grain.scaleStep        = (grain.scaleTarget - grain.scale) / (float) fade;
        grain.scaleSamplesLeft = fade;
    }

    // ...and the buffer they would keep reading from is swept by the same
    // factor, a chunk per sample. The sweep runs backward from just behind
    // the head, so what grains read next is cleared first, and it leaves the
    // hit itself plus the input arriving during the fade untouched — that is
    // the new material the choke is making room for.
    const auto protect = juce::jlimit (0, kFeedbackHistorySize, (int) response.chokeProtectSamples);

    // The protected samples hold the hit *and* the last few ms of old tail
    // that feedback wrote alongside it. Take out exactly that part.
    for (int k = 1; k <= protect; ++k)
    {
        auto pos = writePos - k;
        if (pos < 0) pos += capacity;
        const auto slot = (size_t) (pos & (kFeedbackHistorySize - 1));
        for (int channel = 0; channel < channels; ++channel)
            ring.getWritePointer (channel)[pos] -= feedbackHistory.getSample (channel, (int) slot) * (1.0f - factor);
    }

    chokePositionsLeft = juce::jmax (0, capacity - protect - fade);
    chokeChunk         = (chokePositionsLeft + fade - 1) / fade;
    chokeSweepPos      = writePos - protect - 1;
    while (chokeSweepPos < 0) chokeSweepPos += capacity;
    chokeGain          = factor;
    chokeSamplesLeft   = fade;
}

void GrainEngine::advanceChoke() noexcept
{
    if (chokeSamplesLeft <= 0)
        return;

    for (int n = 0; n < chokeChunk && chokePositionsLeft > 0; ++n, --chokePositionsLeft)
    {
        for (int channel = 0; channel < channels; ++channel)
            ring.getWritePointer (channel)[chokeSweepPos] *= chokeGain;

        if (--chokeSweepPos < 0)
            chokeSweepPos = capacity - 1;
    }

    --chokeSamplesLeft;
}

void GrainEngine::process (const float* const* input,
                           float* const* wet,
                           int numChannels,
                           int numSamples,
                           const Settings& settings,
                           const int* onsets,
                           int numOnsets,
                           const TransientResponse& response)
{
    const auto active   = juce::jmin (numChannels, channels);
    const auto& tables  = windowTables();

    // With Sync on the grain slots are a musical division and Swing delays
    // every other slot; free-running, Density sets the spacing and Spread
    // jitters it so a dense setting does not comb.
    const auto synced   = settings.sync && settings.syncIntervalSamples >= 1.0;
    const auto interval = synced ? settings.syncIntervalSamples
                                 : sampleRate / juce::jmax (0.5, settings.density);
    const auto swingDelay = synced ? (double) settings.swing * interval / 3.0 : 0.0;

    // Freeze crossfades the write over Freeze Fade; 0 ms is an instant stop.
    const auto writeTarget = settings.freeze ? 0.0f : 1.0f;
    const auto writeStep   = settings.freezeFadeSamples >= 1.0
                           ? (float) (1.0 / settings.freezeFadeSamples) : 1.0f;

    feedbackSmoothed.setTargetValue (settings.feedback);

    int onsetIndex = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (onsetIndex < numOnsets && onsets[onsetIndex] <= i)
        {
            if (response.retrigger)
                beginBurst (response);
            if (response.choke)
                beginChoke (response);
            ++onsetIndex;
        }

        samplesUntilNextGrain -= 1.0;

        for (int spawned = 0; samplesUntilNextGrain <= 0.0 && spawned < kGrainSlots; ++spawned)
        {
            spawnScheduledGrain (settings);

            double next = interval;
            if (synced)
            {
                next += slotParity ? -swingDelay : swingDelay;
                slotParity = ! slotParity;
            }
            else
            {
                next *= 1.0 + (double) settings.spread * (rng.nextDouble() * 2.0 - 1.0) * 0.75;
            }

            samplesUntilNextGrain += juce::jmax (1.0, next);
        }

        if (samplesUntilNextGrain <= 0.0)
            samplesUntilNextGrain = 1.0;

        if (burst.remaining > 0)
        {
            burst.nextIn -= 1.0;
            while (burst.nextIn <= 0.0 && burst.remaining > 0)
            {
                spawnBurstGrain (settings);
                --burst.remaining;
                burst.nextIn += burst.interval;
            }
        }

        float sum[2] { 0.0f, 0.0f };

        for (auto& grain : grains)
        {
            if (! grain.active)
                continue;

            if (grain.scaleSamplesLeft > 0)
            {
                grain.scale += grain.scaleStep;
                if (--grain.scaleSamplesLeft == 0)
                    grain.scale = grain.scaleTarget;
            }

            const auto envelope = windowValue (tables[(size_t) grain.window],
                                               grain.pos * grain.invLength)
                                * grain.fade * grain.scale;

            const auto left  = readInterpolated (0, grain.readPos);
            const auto right = active > 1 ? readInterpolated (1, grain.readPos) : left;

            sum[0] += left  * envelope * grain.gainLeft;
            sum[1] += right * envelope * grain.gainRight;

            grain.pos     += 1.0;
            grain.readPos += grain.rate;
            while (grain.readPos < 0.0)                grain.readPos += (double) capacity;
            while (grain.readPos >= (double) capacity) grain.readPos -= (double) capacity;

            auto finished = grain.pos >= grain.length
                         || (grain.scaleSamplesLeft == 0 && grain.scaleTarget <= 0.0f);

            if (grain.stolen)
            {
                grain.fade -= grain.fadeStep;
                if (grain.fade <= 0.0f)
                    finished = true;
            }

            if (finished)
            {
                grain.active = false;
                if (! grain.stolen)
                    --activeGrains;
            }
        }

        advanceChoke();

        // Feedback: grain sum -> colour stages -> limiter -> buffer. The path
        // runs even while frozen so its state stays warm for the release.
        const auto feedback  = feedbackSmoothed.getNextValue()
                             * (chokeSamplesLeft > 0 ? chokeGain : 1.0f);
        float returned[2] { sum[0] * feedback, sum[1] * feedback };
        feedbackPath.processFrame (returned, active);

        if (writeGain < writeTarget)      writeGain = juce::jmin (writeTarget, writeGain + writeStep);
        else if (writeGain > writeTarget) writeGain = juce::jmax (writeTarget, writeGain - writeStep);

        // Frozen means the head stops: Time then chooses where in the held
        // buffer grains read, and everything else keeps playing it.
        if (writeGain > 0.0f)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto source   = input[juce::jmin (channel, numChannels - 1)][i];
                const auto incoming = source + returned[juce::jmin (channel, 1)];
                auto* slot          = ring.getWritePointer (channel) + writePos;
                *slot += (incoming - *slot) * writeGain;
                feedbackHistory.setSample (channel, writePos & (kFeedbackHistorySize - 1),
                                           returned[juce::jmin (channel, 1)] * writeGain);
            }

            if (++writePos >= capacity)
                writePos = 0;
        }

        for (int channel = 0; channel < numChannels; ++channel)
            wet[channel][i] = sum[juce::jmin (channel, 1)];
    }
}
