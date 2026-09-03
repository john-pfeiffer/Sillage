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

// Crossfade when a new Rewind trigger restarts a running playback.
constexpr double kRewindFadeSeconds = 0.02;

// Samples of clearance kept between any read head and the write head.
constexpr double kReadGuard = 8.0;
} // namespace

void GrainEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels   = juce::jlimit (1, 2, numChannels);
    capacity   = (int) std::ceil (kBufferSeconds * newSampleRate);

    ring.setSize (channels, capacity);
    ageRing.assign ((size_t) capacity, 0.0f);
    feedbackHistory.setSize (channels, kFeedbackHistorySize);
    stealFadeStep = (float) (1.0 / (kStealFadeSeconds * newSampleRate));

    rewindCapacity = (int) std::ceil (kRewindCaptureSeconds * newSampleRate);
    rewindRing.setSize (channels, rewindCapacity);
    rewindAgeRing.assign ((size_t) rewindCapacity, 0.0f);
    rewindFadeStep = (float) (1.0 / (kRewindFadeSeconds * newSampleRate));

    feedbackSmoothed.reset (newSampleRate, 0.02);

    feedbackPath.prepare (newSampleRate, channels);
    reset();
}

void GrainEngine::reset()
{
    ring.clear();
    std::fill (ageRing.begin(), ageRing.end(), 0.0f);
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

    rewindRing.clear();
    std::fill (rewindAgeRing.begin(), rewindAgeRing.end(), 0.0f);
    rewindWritePos  = 0;
    rewindRequested = false;
    for (auto& voice : rewindVoices)
        voice = {};

    averageAgeSeconds.store (0.0f);

    feedbackSmoothed.setCurrentAndTargetValue (0.0f);
    feedbackPath.reset();
}

bool GrainEngine::isRewinding() const noexcept
{
    for (const auto& voice : rewindVoices)
        if (voice.active && ! voice.fadingOut)
            return true;
    return false;
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

float GrainEngine::ageAt (double position) const noexcept
{
    // Age now = age when written + how long ago that was, which the distance
    // behind the write head tells us exactly.
    auto index = (int) position;
    index %= capacity;
    if (index < 0) index += capacity;

    auto behind = writePos - index;
    if (behind < 0) behind += capacity;

    return ageRing[(size_t) index] + (float) behind / (float) sampleRate;
}

float GrainEngine::ageOfRegion (double startPos, double span) const noexcept
{
    // The handoff's "average across the grain's window": start, middle, end.
    return (ageAt (startPos) + ageAt (startPos + span * 0.5) + ageAt (startPos + span)) * (1.0f / 3.0f);
}

int GrainEngine::claimSlot()
{
    for (int slot = 0; slot < kGrainSlots; ++slot)
        if (! grains[(size_t) slot].active)
            return slot;

    return -1;
}

GrainEngine::GrainShape GrainEngine::resolveShape (const Settings& settings, float ageSeconds) noexcept
{
    GrainShape shape;
    shape.lengthSamples      = juce::jmax (1.0, settings.sizeSamples);
    shape.pitchOffset        = 0.0f;
    shape.reverseProbability = settings.reverseProbability;
    shape.panSpread          = settings.panSpread;
    shape.level              = 1.0f;

    if (settings.curves == nullptr)
        return shape;

    using lifetime::Destination;
    const auto normalised = juce::jlimit (0.0f, 1.0f,
                                          ageSeconds / (float) juce::jmax (0.001, settings.lifetimeSeconds));
    const auto enabled = [&] (Destination d) { return settings.curveEnabled[(size_t) d]; };
    const auto value   = [&] (Destination d) { return settings.curves->curves[(size_t) d].evaluate (normalised); };

    if (enabled (Destination::grainSize))
        shape.lengthSamples = juce::jmax (1.0, lifetime::grainSizeMs (value (Destination::grainSize)) * 0.001 * sampleRate);
    if (enabled (Destination::pitchOffset))
        shape.pitchOffset = lifetime::pitchSemitones (value (Destination::pitchOffset));
    if (enabled (Destination::reverse))
        shape.reverseProbability = value (Destination::reverse);
    if (enabled (Destination::panSpread))
        shape.panSpread = value (Destination::panSpread);
    if (enabled (Destination::level))
        shape.level = value (Destination::level);

    return shape;
}

float GrainEngine::resolvePitchRate (const Settings& settings, float pitchOffset) noexcept
{
    // Pitch resolves once, at birth, and holds for the grain's life. Chaos and
    // the Lifetime curve land before Quantize so the result still snaps.
    auto semitones = settings.pitchSemitones + settings.pitchModSemitones + pitchOffset;
    if (settings.pitchSpread > 0.0f)
        semitones += settings.pitchSpread * 12.0f * (rng.nextFloat() * 2.0f - 1.0f);
    semitones = scales::snapToScale (semitones, settings.quantize, settings.quantizeRoot);

    return (float) std::pow (2.0, (double) semitones / 12.0);
}

void GrainEngine::activateGrain (double startPos, double rate, const Settings& settings,
                                 const GrainShape& shape, float gainScale, float ageSeconds)
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
    const auto length = juce::jmax (1.0, shape.lengthSamples);

    while (startPos < 0.0)                startPos += (double) capacity;
    while (startPos >= (double) capacity) startPos -= (double) capacity;

    // Equal-power pan that stays exactly unity per channel at centre, so Pan
    // Spread at 0 leaves the source's own stereo image untouched.
    const auto pan   = shape.panSpread * (rng.nextFloat() * 2.0f - 1.0f);
    const auto theta = ((double) pan + 1.0) * juce::MathConstants<double>::pi * 0.25;

    // Overlapping grains sum, so compensate by the expected overlap or Density
    // and Size would double as volume controls — and Feedback would not mean
    // what it says. At Spread 0 every grain reads the same sample at the same
    // instant, so N Hann windows sum coherently to N/2; at Spread 100 their
    // phases are random and they sum in power to sqrt(3N/8). Blend between
    // the two so loop gain stays honest across the whole delay-to-reverb range.
    //
    // Coherence needs more than Spread 0: a grain playing at any rate other
    // than 1 drifts in phase against its neighbours (the comb from the pitch
    // test), and a reversed grain runs the other way entirely. Those count as
    // fully decorrelated.
    const auto overlap   = juce::jmax (1.0, settings.density * (length / sampleRate));
    const auto coherent  = juce::jmin (1.0, 2.0 / overlap);
    const auto incoherent = juce::jmin (1.0, 1.0 / std::sqrt (0.375 * overlap));
    const auto pitched   = std::abs (rate - 1.0) > 0.005;
    const auto blend     = pitched ? 1.0
                         : juce::jlimit (0.0, 1.0, juce::jmax ((double) settings.spread,
                                                                (double) shape.reverseProbability * 2.0));
    const auto compensation = (float) std::exp ((1.0 - blend) * std::log (coherent) + blend * std::log (incoherent))
                            * gainScale * shape.level;

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
    grain.ageSeconds = juce::jmax (0.0f, ageSeconds);
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
    // Spread interpolates from "exactly Time" to "anywhere in the buffer".
    // Interpolating rather than clamping a random deviation is what keeps the
    // distribution smooth instead of piling grains up at the limits. Chaos
    // then pushes the result around on its own slow clock.
    const auto looseMin = kReadGuard;
    const auto looseMax = (double) capacity - kReadGuard;
    const auto base         = juce::jlimit (looseMin, looseMax, settings.timeSamples);
    const auto randomOffset = looseMin + rng.nextDouble() * (looseMax - looseMin);
    const auto probeOffset  = juce::jlimit (looseMin, looseMax,
                                            base + (double) settings.spread * (randomOffset - base)
                                                 + settings.timeModSamples);

    // The audio this grain is about to play has an age; the Lifetime curves
    // shape the grain from it before anything else is decided.
    const auto ageSeconds = ageOfRegion ((double) writePos - probeOffset, juce::jmax (1.0, settings.sizeSamples));
    const auto shape      = resolveShape (settings, ageSeconds);

    const auto rate     = (double) resolvePitchRate (settings, shape.pitchOffset);
    const auto reversed = shape.reverseProbability > 0.0f
                       && rng.nextFloat() < shape.reverseProbability;

    const auto length = juce::jmax (1.0, shape.lengthSamples);
    const auto span   = length * rate; // source samples this grain consumes

    // Keep the read head clear of the write head for the grain's whole life. A
    // grain pitched up gains on the write head; a reversed grain needs its
    // whole source region to already exist behind it.
    auto minOffset = kReadGuard + (reversed ? span : juce::jmax (0.0, length * (rate - 1.0)));
    auto maxOffset = (double) capacity - kReadGuard
                   - (reversed ? length : length * juce::jmax (0.0, 1.0 - rate));

    if (minOffset > maxOffset)
        minOffset = maxOffset;

    const auto offset   = juce::jlimit (minOffset, maxOffset, probeOffset);
    const auto startPos = (double) writePos - offset + (reversed ? span : 0.0);
    activateGrain (startPos, reversed ? -rate : rate, settings, shape, 1.0f, ageSeconds);
}

void GrainEngine::spawnBurstGrain (const Settings& settings)
{
    // Every grain in a burst reads the same hit, so the hit stutters rather
    // than smears. Pitched-up grains still need their guard against the head.
    auto offsetBehind = (double) writePos - burst.hitPos;
    while (offsetBehind < 0.0) offsetBehind += (double) capacity;

    const auto ageSeconds = ageOfRegion (burst.hitPos, juce::jmax (1.0, settings.sizeSamples));
    const auto shape      = resolveShape (settings, ageSeconds);
    const auto rate       = (double) resolvePitchRate (settings, shape.pitchOffset);
    const auto length     = juce::jmax (1.0, shape.lengthSamples);

    const auto minOffset = kReadGuard + juce::jmax (0.0, length * (rate - 1.0));
    const auto startPos  = offsetBehind < minOffset ? (double) writePos - minOffset : burst.hitPos;

    activateGrain (startPos, rate, settings, shape, burst.amount, ageSeconds);
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

float GrainEngine::readRewind (int channel, double position) const noexcept
{
    const auto* data = rewindRing.getReadPointer (juce::jmin (channel, channels - 1));

    auto index = (int) std::floor (position);
    const auto frac = (float) (position - (double) index);
    index %= rewindCapacity;
    if (index < 0) index += rewindCapacity;
    const auto next = index + 1 >= rewindCapacity ? 0 : index + 1;

    return data[(size_t) index] + frac * (data[(size_t) next] - data[(size_t) index]);
}

void GrainEngine::beginRewind (const Settings& settings) noexcept
{
    // Rewinds do not stack: whatever is playing fades out over the crossfade
    // while the new playback fades in from the most recent capture.
    RewindVoice* fresh = nullptr;
    for (auto& voice : rewindVoices)
    {
        if (voice.active && ! voice.fadingOut)
        {
            voice.fadingOut = true;
            voice.fadeStep  = -rewindFadeStep;
        }
        else if (fresh == nullptr && (! voice.active || voice.fadingOut))
        {
            fresh = &voice;
        }
    }

    if (fresh == nullptr)
        fresh = &rewindVoices[0];

    const auto length = juce::jlimit (1.0, kRewindMaxSeconds * sampleRate, settings.rewindLengthSamples);
    const auto rate   = juce::jlimit (0.25, 4.0, settings.rewindRate);

    fresh->active    = true;
    fresh->fadingOut = false;
    fresh->readPos   = (double) rewindWritePos - 1.0;
    fresh->remaining = length / rate;
    fresh->fade      = 0.0f;
    fresh->fadeStep  = rewindFadeStep;
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
    const auto invSampleRate = (float) (1.0 / sampleRate);

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

    if (rewindRequested)
    {
        rewindRequested = false;
        if (settings.rewindOn)
            beginRewind (settings);
    }

    const auto rewindRate = juce::jlimit (0.25, 4.0, settings.rewindRate);

    double blockAgeSum = 0.0, blockWeightSum = 0.0;
    int onsetIndex = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (onsetIndex < numOnsets && onsets[onsetIndex] <= i)
        {
            if (response.retrigger)
                beginBurst (response);
            if (response.choke)
                beginChoke (response);
            if (response.rewind && settings.rewindOn)
                beginRewind (settings);
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
        float ageSum = 0.0f, weightSum = 0.0f;

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

            const auto outLeft  = left  * envelope * grain.gainLeft;
            const auto outRight = right * envelope * grain.gainRight;
            sum[0] += outLeft;
            sum[1] += outRight;

            // The age of this sample of the grain sum is the amplitude-weighted
            // age of what the grains are playing right now.
            const auto weight = std::abs (outLeft) + std::abs (outRight);
            ageSum    += weight * (grain.ageSeconds + (float) grain.pos * invSampleRate);
            weightSum += weight;

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

        const auto sampleAge = weightSum > 1.0e-9f ? ageSum / weightSum : 0.0f;
        blockAgeSum    += (double) sampleAge * (double) weightSum;
        blockWeightSum += (double) weightSum;

        advanceChoke();

        // Rewind: capture the wet tail continuously, and play any triggered
        // rewind backward into the feedback path so it swells up through the
        // colour stages and lands in the main buffer.
        float rewindOut[2] { 0.0f, 0.0f };
        float rewindAge = 0.0f, rewindWeight = 0.0f;

        if (settings.rewindOn)
        {
            for (int channel = 0; channel < channels; ++channel)
                rewindRing.getWritePointer (channel)[rewindWritePos] = sum[juce::jmin (channel, 1)];
            rewindAgeRing[(size_t) rewindWritePos] = sampleAge;

            for (auto& voice : rewindVoices)
            {
                if (! voice.active)
                    continue;

                const auto level = voice.fade * settings.rewindLevel;
                float voiceOut[2] { readRewind (0, voice.readPos) * level,
                                    (active > 1 ? readRewind (1, voice.readPos) : readRewind (0, voice.readPos)) * level };
                rewindOut[0] += voiceOut[0];
                rewindOut[1] += voiceOut[1];

                // Age of what is being rewound: captured age plus time since.
                auto behind = rewindWritePos - (int) voice.readPos;
                if (behind < 0) behind += rewindCapacity;
                auto index = (int) voice.readPos % rewindCapacity;
                if (index < 0) index += rewindCapacity;
                const auto w = std::abs (voiceOut[0]) + std::abs (voiceOut[1]);
                rewindAge    += w * (rewindAgeRing[(size_t) index] + (float) behind * invSampleRate);
                rewindWeight += w;

                voice.readPos -= rewindRate;
                while (voice.readPos < 0.0) voice.readPos += (double) rewindCapacity;
                voice.remaining -= 1.0;

                voice.fade += voice.fadeStep;
                if (voice.fade >= 1.0f) { voice.fade = 1.0f; voice.fadeStep = 0.0f; }

                // The capture head only ever approaches from behind; stop if it
                // is about to overwrite what this voice is reading.
                const auto overrun = behind >= rewindCapacity - 64;

                if (voice.remaining <= 0.0 || overrun || (voice.fadingOut && voice.fade <= 0.0f))
                    voice.active = false;
            }

            if (++rewindWritePos >= rewindCapacity)
                rewindWritePos = 0;
        }

        // Feedback: grain sum -> colour stages -> limiter -> buffer. The path
        // runs even while frozen so its state stays warm for the release.
        const auto feedback  = feedbackSmoothed.getNextValue()
                             * (chokeSamplesLeft > 0 ? chokeGain : 1.0f);
        float returned[2] { sum[0] * feedback + rewindOut[0], sum[1] * feedback + rewindOut[1] };

        // Age of what goes round: the grain sum's age and the rewind's age,
        // weighted by how much of each is in the frame.
        const auto fbWeight   = (std::abs (sum[0]) + std::abs (sum[1])) * feedback;
        const auto returnedAge = (fbWeight + rewindWeight) > 1.0e-9f
                               ? (fbWeight * sampleAge + (rewindWeight > 0.0f ? rewindAge : 0.0f)) / (fbWeight + rewindWeight)
                               : 0.0f;

        feedbackPath.processFrame (returned, active, returnedAge);

        if (writeGain < writeTarget)      writeGain = juce::jmin (writeTarget, writeGain + writeStep);
        else if (writeGain > writeTarget) writeGain = juce::jmax (writeTarget, writeGain - writeStep);

        // Frozen means the head stops: Time then chooses where in the held
        // buffer grains read, and everything else keeps playing it.
        if (writeGain > 0.0f)
        {
            float inputWeight = 0.0f, feedbackWeight = 0.0f;

            for (int channel = 0; channel < channels; ++channel)
            {
                const auto source   = input[juce::jmin (channel, numChannels - 1)][i];
                const auto back     = returned[juce::jmin (channel, 1)];
                const auto incoming = source + back;
                auto* slot          = ring.getWritePointer (channel) + writePos;
                *slot += (incoming - *slot) * writeGain;
                feedbackHistory.setSample (channel, writePos & (kFeedbackHistorySize - 1), back * writeGain);

                inputWeight    += std::abs (source);
                feedbackWeight += std::abs (back);
            }

            // Fresh input is age 0; what came back is as old as it was.
            const auto total   = inputWeight + feedbackWeight;
            const auto written = total > 1.0e-9f ? returnedAge * (feedbackWeight / total) : 0.0f;
            auto& ageSlot = ageRing[(size_t) writePos];
            ageSlot += (written - ageSlot) * writeGain;

            if (++writePos >= capacity)
                writePos = 0;
        }

        for (int channel = 0; channel < numChannels; ++channel)
            wet[channel][i] = sum[juce::jmin (channel, 1)];
    }

    averageAgeSeconds.store (blockWeightSum > 1.0e-9 ? (float) (blockAgeSum / blockWeightSum) : 0.0f,
                             std::memory_order_relaxed);
}
