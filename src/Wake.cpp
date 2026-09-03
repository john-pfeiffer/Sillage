#include "Wake.h"

#include <limits>

namespace
{
// An engine whose wet output has been below this for this many blocks is
// dormant and skipped until input is routed to it again.
constexpr float kSilence       = 1.0e-7f;
constexpr int   kDormantBlocks = 8;

// Input above this while nothing is sounding starts an instance on its own,
// so sustained material without a detectable onset still gets a tail.
constexpr float kAutoSpawnLevel = 1.0e-3f;

float blockPeak (const float* const* data, int numChannels, int from, int to) noexcept
{
    float peak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = from; i < to; ++i)
            peak = juce::jmax (peak, std::abs (data[channel][i]));
    return peak;
}
} // namespace

void WakeEngine::prepare (double newSampleRate, int numChannels, int maxBlockSize)
{
    sampleRate = newSampleRate;
    channels   = juce::jlimit (1, 2, numChannels);

    shared.prepare (newSampleRate, channels);
    for (auto& instance : instances)
        instance.engine.prepare (newSampleRate, channels, kIsolatedBufferSeconds, false);

    isolatedRewind.prepare (newSampleRate, channels);

    const auto block = juce::jmax (1, maxBlockSize);
    sharedInput.setSize (channels, block);
    isolatedInput.setSize (channels, block);
    silence.setSize (channels, block);
    silence.clear();
    scratchWet.setSize (channels, block);
    isolatedWet.setSize (channels, block);
    injectBuffer.setSize (channels, block);
    injectAge.assign ((size_t) block, 0.0f);

    historyCapacity = (int) std::ceil (kPrimeSeconds * newSampleRate) + block;
    history.setSize (channels, historyCapacity);

    routingStep = (float) (1.0 / (kModeFadeSeconds * newSampleRate));
    reset();
}

void WakeEngine::reset()
{
    shared.reset();
    sharedAlive        = true;
    sharedSilentBlocks = 0;

    for (auto& instance : instances)
    {
        instance.engine.reset();
        instance.active   = false;
        instance.stealing = false;
        instance.gain     = 1.0f;
        instance.gainTarget = 1.0f;
        instance.gainLeft = 0;
        instance.silentBlocks = 0;
    }
    newest = -1;

    isolatedRewind.reset();
    rewindRequested = false;

    history.clear();
    historyPos = 0;

    averageAgeSeconds.store (0.0f);
    activeInstances.store (0);
}

bool WakeEngine::isRewinding() const noexcept
{
    return shared.isRewinding() || isolatedRewind.isPlaying();
}

int WakeEngine::getActiveGrainCount() const noexcept
{
    int count = shared.getActiveGrainCount();
    for (const auto& instance : instances)
        if (instance.active)
            count += instance.engine.getActiveGrainCount();
    return count;
}

bool WakeEngine::isFrozen() const noexcept
{
    if (routing >= 0.5f && newest >= 0)
        return instances[(size_t) newest].engine.isFrozen();
    return shared.isFrozen();
}

void WakeEngine::rampGain (Instance& instance, float target, double samples) noexcept
{
    const auto n = juce::jmax (1, (int) samples);
    instance.gainTarget = target;
    instance.gainStep   = (target - instance.gain) / (float) n;
    instance.gainLeft   = n;
}

void WakeEngine::recordHistory (const float* const* input, int numChannels, int from, int to) noexcept
{
    for (int i = from; i < to; ++i)
    {
        for (int channel = 0; channel < channels; ++channel)
            history.setSample (channel, historyPos, input[juce::jmin (channel, numChannels - 1)][i]);
        if (++historyPos >= historyCapacity)
            historyPos = 0;
    }
}

void WakeEngine::spawnInstance (const Settings& wake, const GrainEngine::Settings& grain)
{
    // Count what is sounding; at the cap the oldest is stolen with a fade.
    int sounding = 0, oldest = -1, oldestFading = -1;
    int64_t oldestBirth = std::numeric_limits<int64_t>::max();
    int64_t oldestFadingBirth = std::numeric_limits<int64_t>::max();

    for (int i = 0; i < kInstanceSlots; ++i)
    {
        const auto& instance = instances[(size_t) i];
        if (! instance.active)
            continue;

        if (instance.stealing)
        {
            if (instance.birth < oldestFadingBirth) { oldestFadingBirth = instance.birth; oldestFading = i; }
            continue;
        }

        ++sounding;
        if (instance.birth < oldestBirth) { oldestBirth = instance.birth; oldest = i; }
    }

    if (sounding >= kMaxInstances && oldest >= 0)
    {
        auto& victim = instances[(size_t) oldest];
        victim.stealing = true;
        rampGain (victim, 0.0f, kStealFadeSeconds * sampleRate);
    }

    // A free slot, else the spare is still busy fading: cut that one short.
    int slot = -1;
    for (int i = 0; i < kInstanceSlots && slot < 0; ++i)
        if (! instances[(size_t) i].active)
            slot = i;

    if (slot < 0)
        slot = oldestFading >= 0 ? oldestFading : 0;

    // Displace: the hit pushes every older instance down.
    if (wake.displace > 0.0f)
        for (int i = 0; i < kInstanceSlots; ++i)
            if (i != slot && instances[(size_t) i].active && ! instances[(size_t) i].stealing)
                rampGain (instances[(size_t) i], instances[(size_t) i].gain * (1.0f - wake.displace),
                          wake.displaceFadeSamples);

    auto& instance = instances[(size_t) slot];
    instance.engine.reset();
    instance.active       = true;
    instance.stealing     = false;
    instance.gain         = 1.0f;
    instance.gainTarget   = 1.0f;
    instance.gainStep     = 0.0f;
    instance.gainLeft     = 0;
    instance.birth        = instanceCounter++;
    instance.silentBlocks = 0;

    // Hand it the hit: the last few hundred ms of input, oldest first.
    const auto prime = juce::jmin (historyCapacity, (int) (kPrimeSeconds * sampleRate));
    auto start = historyPos - prime;
    while (start < 0) start += historyCapacity;

    const auto firstRun = juce::jmin (prime, historyCapacity - start);
    const float* run1[2] { history.getReadPointer (0) + start,
                           history.getReadPointer (juce::jmin (1, channels - 1)) + start };
    instance.engine.primeInput (run1, channels, firstRun);

    if (firstRun < prime)
    {
        const float* run2[2] { history.getReadPointer (0), history.getReadPointer (juce::jmin (1, channels - 1)) };
        instance.engine.primeInput (run2, channels, prime - firstRun);
    }

    juce::ignoreUnused (grain);
    newest = slot;
}

void WakeEngine::shareGrainCap (bool sharedSounding)
{
    int engines = sharedSounding ? 1 : 0;
    for (const auto& instance : instances)
        if (instance.active)
            ++engines;

    const auto cap = kMaxGrains / juce::jmax (1, engines);
    shared.setGrainCap (sharedSounding ? cap : kMaxGrains);
    for (auto& instance : instances)
        instance.engine.setGrainCap (cap);
}

void WakeEngine::process (const float* const* input,
                          float* const* wet,
                          int numChannels,
                          int numSamples,
                          const GrainEngine::Settings& grain,
                          const Settings& wake,
                          const int* onsets,
                          int numOnsets,
                          const GrainEngine::TransientResponse& response)
{
    const auto active = juce::jmin (numChannels, channels);

    // Route the input: shared or isolated, crossfading between them.
    const auto target = wake.isolated ? 1.0f : 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        if (routing < target)      routing = juce::jmin (target, routing + routingStep);
        else if (routing > target) routing = juce::jmax (target, routing - routingStep);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto source = input[juce::jmin (channel, numChannels - 1)][i];
            sharedInput.setSample (channel, i, source * (1.0f - routing));
            isolatedInput.setSample (channel, i, source * routing);
        }
    }

    const float* sharedIn[2]   { sharedInput.getReadPointer (0),   sharedInput.getReadPointer (juce::jmin (1, channels - 1)) };
    const float* isolatedIn[2] { isolatedInput.getReadPointer (0), isolatedInput.getReadPointer (juce::jmin (1, channels - 1)) };
    const float* silent[2]     { silence.getReadPointer (0),       silence.getReadPointer (juce::jmin (1, channels - 1)) };

    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::clear (wet[channel], numSamples);

    // The shared side wakes whenever input is routed to it, and goes dormant
    // once its tail has died with nothing coming in.
    if (routing < 1.0f)
        sharedAlive = true;

    // An isolated instance is only ever spawned while the input is headed
    // that way and nothing is frozen.
    const auto spawning = wake.isolated && ! grain.freeze;

    // Count engines for the grain cap before anything sounds this block.
    shareGrainCap (sharedAlive);

    double ageSum = 0.0, ageWeight = 0.0;

    // ---- Shared side --------------------------------------------------------
    if (sharedAlive)
    {
        shared.getFeedbackPath().setSettings (feedbackSettings);
        if (rewindRequested && ! wake.isolated)
            shared.triggerRewind();

        // In Shared mode the hits belong to this buffer; Displace rides on
        // the Choke mechanism.
        auto sharedResponse = response;
        if (! wake.isolated && wake.displace > 0.0f)
        {
            sharedResponse.choke       = true;
            sharedResponse.chokeAmount = response.choke ? juce::jmax (response.chokeAmount, wake.displace) : wake.displace;
            sharedResponse.chokeFadeSamples = wake.displaceFadeSamples;
        }

        float* out[2] { scratchWet.getWritePointer (0), scratchWet.getWritePointer (juce::jmin (1, channels - 1)) };
        shared.process (sharedIn, out, active, numSamples, grain,
                        onsets, wake.isolated ? 0 : numOnsets, sharedResponse);

        float weight = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* src = scratchWet.getReadPointer (juce::jmin (channel, channels - 1));
            juce::FloatVectorOperations::add (wet[channel], src, numSamples);
            for (int i = 0; i < numSamples; ++i)
                weight += std::abs (src[i]);
        }

        ageSum    += (double) weight * (double) shared.getAverageAgeSeconds();
        ageWeight += (double) weight;

        if (routing >= 1.0f && weight < kSilence * (float) numSamples)
        {
            if (++sharedSilentBlocks > kDormantBlocks)
            {
                sharedAlive = false;
                shared.reset();
            }
        }
        else
        {
            sharedSilentBlocks = 0;
        }
    }

    // ---- Isolated side ------------------------------------------------------
    bool anyInstance = false;
    for (const auto& instance : instances)
        anyInstance = anyInstance || instance.active;

    if (spawning || anyInstance || routing > 0.0f)
    {
        const auto rewindOn = grain.rewindOn;
        if (rewindRequested && wake.isolated && rewindOn)
            isolatedRewind.begin (grain.rewindLengthSamples, grain.rewindRate);

        // Nothing sounding but something arriving: start an instance so a
        // sustained input without a clean onset still gets its tail.
        if (spawning && newest < 0 && blockPeak (input, numChannels, 0, numSamples) > kAutoSpawnLevel)
        {
            spawnInstance (wake, grain);
            shareGrainCap (sharedAlive);
        }

        isolatedWet.clear();

        for (auto& instance : instances)
            if (instance.active)
                instance.engine.getFeedbackPath().setSettings (feedbackSettings);

        // Walk the block in segments split at onsets, so a new instance
        // starts on its hit and receives the input from that sample.
        int segmentStart = 0;
        int onsetIndex   = 0;

        while (segmentStart < numSamples)
        {
            // Skip any onsets already behind us.
            while (onsetIndex < numOnsets && onsets[onsetIndex] < segmentStart)
                ++onsetIndex;

            bool startsOnHit = false;
            if (onsetIndex < numOnsets && onsets[onsetIndex] == segmentStart)
            {
                startsOnHit = true;
                ++onsetIndex;
                if (spawning)
                {
                    spawnInstance (wake, grain);
                    shareGrainCap (sharedAlive);
                    if (response.rewind && rewindOn)
                        isolatedRewind.begin (grain.rewindLengthSamples, grain.rewindRate);
                }
            }

            const auto segmentEnd = onsetIndex < numOnsets ? juce::jmin (numSamples, onsets[onsetIndex]) : numSamples;
            const auto length     = segmentEnd - segmentStart;
            if (length <= 0)
                break;

            // Rewind playback for this segment, headed for the newest instance.
            const bool injecting = rewindOn && newest >= 0 && isolatedRewind.isPrepared();
            if (injecting)
            {
                injectBuffer.clear();
                for (int i = 0; i < length; ++i)
                {
                    float frame[2] { 0.0f, 0.0f };
                    float age = 0.0f, weight = 0.0f;
                    isolatedRewind.render (frame, age, weight, grain.rewindLevel, grain.rewindRate, active);
                    injectBuffer.setSample (0, i, frame[0]);
                    if (channels > 1) injectBuffer.setSample (1, i, frame[1]);
                    injectAge[(size_t) i] = weight > 1.0e-9f ? age / weight : 0.0f;
                }
            }

            const float* injectPtrs[2] { injectBuffer.getReadPointer (0), injectBuffer.getReadPointer (juce::jmin (1, channels - 1)) };
            const int hitOffset = 0;

            for (int slot = 0; slot < kInstanceSlots; ++slot)
            {
                auto& instance = instances[(size_t) slot];
                if (! instance.active)
                    continue;

                const bool isNewest = slot == newest;
                const float* in[2] { (isNewest ? isolatedIn[0] : silent[0]) + segmentStart,
                                     (isNewest ? isolatedIn[1] : silent[1]) + segmentStart };
                float* out[2] { scratchWet.getWritePointer (0) + segmentStart,
                                scratchWet.getWritePointer (juce::jmin (1, channels - 1)) + segmentStart };

                // Only the newest instance hears the hit; it is the one the
                // hit spawned. Older tails are what Isolated protects.
                const auto passOnsets = isNewest && startsOnHit && spawning ? 1 : 0;

                instance.engine.process (in, out, active, length, grain,
                                         &hitOffset, passOnsets, response,
                                         (injecting && isNewest) ? injectPtrs : nullptr,
                                         (injecting && isNewest) ? injectAge.data() : nullptr);

                float weight = 0.0f;
                for (int i = 0; i < length; ++i)
                {
                    if (instance.gainLeft > 0)
                    {
                        instance.gain += instance.gainStep;
                        if (--instance.gainLeft == 0)
                            instance.gain = instance.gainTarget;
                    }

                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const auto sample = out[channel][i] * instance.gain;
                        isolatedWet.addSample (channel, segmentStart + i, sample);
                        weight += std::abs (sample);
                    }
                }

                ageSum    += (double) weight * (double) instance.engine.getAverageAgeSeconds();
                ageWeight += (double) weight;

                // A stolen instance leaves once its fade is done; any other
                // that has fallen silent and is not receiving input goes too.
                const auto silentSegment = weight < kSilence * (float) length;
                if (instance.stealing && instance.gainLeft == 0)
                {
                    instance.active = false;
                }
                else if (silentSegment && ! isNewest)
                {
                    if (++instance.silentBlocks > kDormantBlocks)
                        instance.active = false;
                }
                else if (silentSegment && isNewest && ! wake.isolated && routing <= 0.0f)
                {
                    // Back in Shared mode with nothing left: release it.
                    if (++instance.silentBlocks > kDormantBlocks)
                    {
                        instance.active = false;
                        newest = -1;
                    }
                }
                else
                {
                    instance.silentBlocks = 0;
                }

                if (! instance.active && slot == newest)
                    newest = -1;
            }

            // Capture the summed isolated tail for Rewind, tagged with the age
            // of the instance that is being fed.
            if (rewindOn)
            {
                const auto age = newest >= 0 ? instances[(size_t) newest].engine.getAverageAgeSeconds() : 0.0f;
                for (int i = 0; i < length; ++i)
                {
                    const float frame[2] { isolatedWet.getSample (0, segmentStart + i),
                                           isolatedWet.getSample (juce::jmin (1, channels - 1), segmentStart + i) };
                    isolatedRewind.capture (frame, age);
                }
            }

            recordHistory (input, numChannels, segmentStart, segmentEnd);
            segmentStart = segmentEnd;
        }

        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::add (wet[channel], isolatedWet.getReadPointer (juce::jmin (channel, channels - 1)), numSamples);
    }
    else
    {
        recordHistory (input, numChannels, 0, numSamples);
    }

    rewindRequested = false;

    int count = 0;
    for (const auto& instance : instances)
        if (instance.active && ! instance.stealing)
            ++count;
    activeInstances.store (count, std::memory_order_relaxed);

    averageAgeSeconds.store (ageWeight > 1.0e-9 ? (float) (ageSum / ageWeight) : 0.0f,
                             std::memory_order_relaxed);
}
