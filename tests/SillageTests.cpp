// Headless render harness for Sillage. Runs as a plain console app (CTest).
// Each check pushes audio through the real SillageAudioProcessor and asserts
// on the rendered output — no host, no UI.

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

#include "LifetimeCurves.h"
#include "Modulation.h"
#include "OnsetDetector.h"
#include "PluginProcessor.h"
#include "Randomize.h"
#include "Scales.h"
#include "Types.h"

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    std::printf ("%s %s\n", condition ? "[PASS]" : "[FAIL]", what);
    if (! condition)
        ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

using Audio = std::vector<std::vector<float>>;
using Generator = std::function<float (int64_t sample, int ch)>;
using BlockHook = std::function<void (int64_t blockStart)>;

std::unique_ptr<SillageAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<SillageAudioProcessor>();
    p->setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    p->prepareToPlay (kSampleRate, kBlockSize);
    return p;
}

void setParam (SillageAudioProcessor& p, const char* id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

float getParam (SillageAudioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

// Puts the engine in a plain, fully-wet delay configuration so a test only has
// to change the one thing it is actually about.
void configureAsPlainDelay (SillageAudioProcessor& p)
{
    setParam (p, params::id::mix, 100.0f);
    setParam (p, params::id::output, 0.0f);
    setParam (p, params::id::feedback, 0.0f);
    setParam (p, params::id::decay, params::kDecayOffMs); // Feedback alone decides
    setParam (p, params::id::spread, 0.0f);
    setParam (p, params::id::density, 40.0f);
    setParam (p, params::id::size, 80.0f);
    setParam (p, params::id::window, 0.0f);
    setParam (p, params::id::pitch, 0.0f);
    setParam (p, params::id::pitchFine, 0.0f);
    setParam (p, params::id::pitchSpread, 0.0f);
    setParam (p, params::id::panSpread, 0.0f);
    setParam (p, params::id::reverse, 0.0f);
    setParam (p, params::id::quantize, 0.0f);
    setParam (p, params::id::shimmerAmount, 0.0f);
    setParam (p, params::id::diffuse, 0.0f);
    setParam (p, params::id::drive, 0.0f);
    setParam (p, params::id::chaos, 0.0f);
    setParam (p, params::id::freeze, 0.0f);
    setParam (p, params::id::sync, 0.0f);
    setParam (p, params::id::swing, 0.0f);
    setParam (p, params::id::retriggerOn, 0.0f);
    setParam (p, params::id::duckOn, 0.0f);
    setParam (p, params::id::chokeOn, 0.0f);
    setParam (p, params::id::envDensity, 0.0f);
    setParam (p, params::id::envSpread, 0.0f);
    setParam (p, params::id::lifetime, 2000.0f);
    setParam (p, params::id::degradeBits, 0.0f);
    setParam (p, params::id::degradeRate, 0.0f);
    setParam (p, params::id::degradeNoise, 0.0f);
    setParam (p, params::id::degradeTilt, 0.0f);
    setParam (p, params::id::degradeDrift, 0.0f);
    setParam (p, params::id::rewindOn, 0.0f);
    for (auto* id : params::id::curveEnable)
        setParam (p, id, 0.0f);
    setParam (p, params::id::wakeMode, 0.0f);
    setParam (p, params::id::displace, 0.0f);
    for (auto* id : params::id::modSource)
        setParam (p, id, 0.0f);
    setParam (p, params::id::width, 100.0f);
    setParam (p, params::id::wetHighpass, 20.0f);
    setParam (p, params::id::wetLowpass, 20000.0f);
    for (auto* id : { params::id::loopOn, params::id::transientsOn, params::id::ageOn,
                      params::id::chaosOn, params::id::modOn })
        setParam (p, id, 1.0f);
}

// Installs a two-point straight line for one Lifetime Curve destination and
// optionally enables it.
void setLine (SillageAudioProcessor& p, lifetime::Destination destination,
              float startY, float endY, bool enable)
{
    auto curves = p.getCurves();
    auto& curve = curves.curves[(size_t) destination];
    curve.numPoints = 2;
    curve.points[0] = { 0.0f, startY, 0.0f };
    curve.points[1] = { 1.0f, endY, 0.0f };
    p.setCurves (curves);
    setParam (p, params::id::curveEnable[(size_t) destination], enable ? 1.0f : 0.0f);
}


// Renders `seconds` of the given input generator through the processor,
// appending output to `out` (one vector per channel). `afterBlock` runs after
// each block with the block's starting sample, so a test can flip parameters
// mid-render.
void render (SillageAudioProcessor& p, double seconds, const Generator& gen,
             Audio& out, const BlockHook& afterBlock = {})
{
    const auto totalSamples = (int64_t) (seconds * kSampleRate);
    out.assign (2, {});

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    int64_t pos = 0;
    while (pos < totalSamples)
    {
        const int n = (int) std::min ((int64_t) kBlockSize, totalSamples - pos);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = gen (pos + i, ch);
        }

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            out[(size_t) ch].insert (out[(size_t) ch].end(),
                                     buffer.getReadPointer (ch),
                                     buffer.getReadPointer (ch) + n);

        if (afterBlock)
            afterBlock (pos);
        pos += n;
    }
}

bool allFinite (const Audio& audio)
{
    for (const auto& ch : audio)
        for (float s : ch)
            if (! std::isfinite (s))
                return false;
    return true;
}

size_t atSecond (double seconds)
{
    return (size_t) (seconds * kSampleRate);
}

// Peak magnitude over [begin, end) in samples, across both channels.
float peak (const Audio& audio, size_t begin, size_t end)
{
    float m = 0.0f;
    for (const auto& ch : audio)
        for (size_t i = begin; i < std::min (end, ch.size()); ++i)
            m = std::max (m, std::abs (ch[i]));
    return m;
}

float peakSeconds (const Audio& audio, double from, double to)
{
    return peak (audio, atSecond (from), atSecond (to));
}

double rms (const std::vector<float>& channel, size_t begin, size_t end)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = begin; i < std::min (end, channel.size()); ++i, ++n)
        sum += (double) channel[i] * (double) channel[i];
    return n > 0 ? std::sqrt (sum / (double) n) : 0.0;
}

// Dominant frequency by autocorrelation. Robust to the amplitude modulation a
// grain cloud produces, where a zero-crossing count is not.
double dominantFrequency (const std::vector<float>& channel, size_t begin, size_t end)
{
    constexpr int minLag = 40, maxLag = 400; // 120 Hz .. 1200 Hz
    const auto last = std::min (end, channel.size());
    if (last < begin + (size_t) maxLag * 2)
        return 0.0;

    std::vector<double> correlation ((size_t) maxLag + 1, 0.0);
    double best = 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (size_t i = begin; i + (size_t) lag < last; ++i)
            sum += (double) channel[i] * (double) channel[i + (size_t) lag];

        correlation[(size_t) lag] = sum;
        best = std::max (best, sum);
    }

    if (best <= 0.0)
        return 0.0;

    // The first *peak* that correlates nearly as well as the best one is the
    // fundamental. Taking the global maximum would happily land on an octave,
    // and taking the first lag over the threshold lands on a peak's rising edge.
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const auto here = correlation[(size_t) lag];
        if (here >= best * 0.85
            && here > correlation[(size_t) lag - 1]
            && here >= correlation[(size_t) lag + 1])
            return kSampleRate / (double) lag;
    }

    return 0.0;
}

// Times (in samples) at which the signal goes from silence to sound.
std::vector<size_t> soundOnsets (const std::vector<float>& channel, size_t begin, size_t end,
                                 float silence = 1.0e-4f)
{
    std::vector<size_t> onsets;
    bool sounding = false;
    for (size_t i = begin; i < std::min (end, channel.size()); ++i)
    {
        const auto loud = std::abs (channel[i]) > silence;
        if (loud && ! sounding)
            onsets.push_back (i);
        sounding = loud;
    }
    return onsets;
}

float sine (int64_t sample, double hz)
{
    return (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) sample / kSampleRate);
}

Generator toneThenSilence (double hz, double untilSeconds, float amplitude = 0.5f)
{
    const auto until = (int64_t) (untilSeconds * kSampleRate);
    return [=] (int64_t i, int) { return i < until ? sine (i, hz) * amplitude : 0.0f; };
}

Generator clickAt (double seconds, float amplitude = 1.0f)
{
    const auto at = (int64_t) (seconds * kSampleRate);
    return [=] (int64_t i, int) { return i == at ? amplitude : 0.0f; };
}

Generator decayingBurst (double hz, double untilSeconds, double decaySeconds, float amplitude = 0.5f)
{
    const auto until = (int64_t) (untilSeconds * kSampleRate);
    return [=] (int64_t i, int)
    {
        if (i >= until) return 0.0f;
        const auto t = (double) i / kSampleRate;
        return sine (i, hz) * amplitude * (float) std::exp (-t / decaySeconds);
    };
}

// ---- Phase 1 + 2 ----------------------------------------------------------------

void testPassThroughAtFullDry()
{
    auto p = makeProcessor();
    setParam (*p, params::id::mix, 0.0f);
    setParam (*p, params::id::output, 0.0f);

    Audio out;
    render (*p, 1.0, [] (int64_t i, int) { return std::sin (0.05 * (double) i) * 0.5f; }, out);

    // After the 20 ms smoothing settles, dry-only output must match the input.
    const auto start = atSecond (0.1);
    bool matches = true;
    for (size_t i = start; i < out[0].size(); ++i)
    {
        const float expected = (float) (std::sin (0.05 * (double) i) * 0.5f);
        if (std::abs (out[0][i] - expected) > 1.0e-4f)
        {
            matches = false;
            break;
        }
    }
    check (matches, "mix=0 output equals input");
    check (allFinite (out), "pass-through output is finite");
}

void testStateRoundTrip()
{
    auto p = makeProcessor();
    setParam (*p, params::id::mix, 72.0f);
    setParam (*p, params::id::output, -6.0f);
    setParam (*p, params::id::density, 123.0f);
    setParam (*p, params::id::shimmerAmount, 44.0f);
    setParam (*p, params::id::chaos, 33.0f);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (getParam (*q, params::id::mix) - 72.0f) < 0.01f
               && std::abs (getParam (*q, params::id::output) - (-6.0f)) < 0.01f
               && std::abs (getParam (*q, params::id::density) - 123.0f) < 0.5f
               && std::abs (getParam (*q, params::id::shimmerAmount) - 44.0f) < 0.01f
               && std::abs (getParam (*q, params::id::chaos) - 33.0f) < 0.01f,
           "parameter state round-trips through get/setStateInformation");
}

void testSilenceStaysSilent()
{
    auto p = makeProcessor();
    Audio out;
    render (*p, 2.0, [] (int64_t, int) { return 0.0f; }, out);
    check (peak (out, 0, out[0].size()) < 1.0e-12f, "silence in, silence out");
    check (p->getOnsetCount() == 0, "silence produces no onsets");
}

// Spread 0 means every grain reads exactly Time behind the write head, so an
// impulse comes back as a single echo at exactly Time — a delay, not a smear.
void testDelayLandsAtTime()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 250.0f);

    Audio out;
    render (*p, 1.0, clickAt (0.0), out);

    check (peakSeconds (out, 0.245, 0.265) > 0.1f, "spread=0 puts an echo at Time");
    check (peakSeconds (out, 0.010, 0.230) < 1.0e-4f, "spread=0 leaves everything before Time silent");
    check (allFinite (out), "delay output is finite");
}

// Spread 100 % scatters reads across the whole buffer. With Time set beyond how
// much audio exists yet, spread=0 can only read silence while spread=100 reaches
// recent audio — a sharp, direct test of the delay/reverb continuum.
void testSpreadReachesRecentAudio()
{
    const auto renderWithSpread = [] (float spread)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 2000.0f);
        setParam (*p, params::id::density, 100.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::spread, spread);

        Audio out;
        render (*p, 1.5, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
        return peakSeconds (out, 0.2, 1.5);
    };

    check (renderWithSpread (0.0f) < 1.0e-5f, "spread=0 reads only at Time (silent before the buffer fills)");
    check (renderWithSpread (100.0f) > 0.01f, "spread=100 reaches audio far from Time");
}

void testFeedbackSustainsTail()
{
    const auto tailAfter = [] (float feedback)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, feedback);

        Audio out;
        render (*p, 3.0, toneThenSilence (330.0, 0.2), out);
        return peakSeconds (out, 2.5, 3.0);
    };

    check (tailAfter (0.0f) < 1.0e-5f, "feedback=0 tail stops after Time");
    check (tailAfter (90.0f) > 1.0e-3f, "feedback=90 sustains the tail past 2.5 s");
}

// The loop limiter is the invariant that makes Feedback > 100 % and heavy drive
// musical instead of destructive. Bounded is the assertion; a finite check alone
// would miss a slow runaway, so the tail is compared against itself over time.
void testLimiterKeepsLoopBounded()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 150.0f);
    setParam (*p, params::id::feedback, 120.0f);
    setParam (*p, params::id::density, 200.0f);
    setParam (*p, params::id::spread, 100.0f);
    setParam (*p, params::id::size, 200.0f);
    setParam (*p, params::id::satType, 2.0f);   // Fold
    setParam (*p, params::id::drive, 80.0f);
    setParam (*p, params::id::shimmerAmount, 50.0f);
    setParam (*p, params::id::diffuse, 100.0f);

    juce::Random noise (20260903);
    Audio out;
    render (*p, 8.0, [&noise] (int64_t, int) { return noise.nextFloat() * 2.0f - 1.0f; }, out);

    const auto middle = peakSeconds (out, 4.0, 5.0);
    const auto last   = peakSeconds (out, 7.0, 8.0);

    check (allFinite (out), "feedback=120 with drive stays finite");
    check (last < 20.0f, "feedback=120 output stays bounded");
    check (last <= middle * 2.0f + 0.1f, "feedback=120 does not run away over time");
}

void testSyncedTimeFollowsTempo()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::fallbackBpm, 120.0f);
    setParam (*p, params::id::timeSync, 1.0f);
    setParam (*p, params::id::timeDivision, 13.0f); // 1/4

    Audio out;
    render (*p, 1.0, clickAt (0.0), out);

    check (peakSeconds (out, 0.495, 0.515) > 0.1f, "synced 1/4 at 120 BPM echoes at 500 ms");
    check (peakSeconds (out, 0.010, 0.480) < 1.0e-4f, "synced echo arrives no earlier");
}

void testPitchShiftsGrainsUpAnOctave()
{
    const auto pitchOf = [] (float semitones)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 500.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::pitch, semitones);

        // A little Spread is deliberate. A perfectly regular scheduler gives
        // every pitched grain a fixed phase offset from the last, and on a
        // steady tone those can comb to exact silence — inherent to granular
        // pitch shifting, and precisely what Spread exists to break up.
        setParam (*p, params::id::spread, 25.0f);

        Audio out;
        render (*p, 2.0, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
        return dominantFrequency (out[0], atSecond (1.0), atSecond (1.8));
    };

    check (std::abs (pitchOf (0.0f) - 220.0) < 12.0, "pitch=0 preserves the input pitch");
    check (std::abs (pitchOf (12.0f) - 440.0) < 25.0, "pitch=+12 returns the grain an octave up");
}

void testShimmerClimbsThroughTheLoop()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 200.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::size, 100.0f);
    setParam (*p, params::id::feedback, 80.0f);
    setParam (*p, params::id::shimmerInterval, 6.0f);   // +12 st
    setParam (*p, params::id::shimmerAmount, 100.0f);

    Audio out;
    render (*p, 1.5, toneThenSilence (220.0, 0.3), out);

    const auto early = dominantFrequency (out[0], atSecond (0.40), atSecond (0.65));
    const auto later = dominantFrequency (out[0], atSecond (0.80), atSecond (1.05));

    check (early > 100.0 && later > 100.0, "shimmer tail keeps a measurable pitch");
    check (later > early * 1.5, "shimmer compounds upward pass after pass");
}

void testLoopLowpassDampsTheTail()
{
    const auto tailWithLowpass = [] (float cutoffHz)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 150.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 90.0f);
        setParam (*p, params::id::fbLowpass, cutoffHz);

        Audio out;
        render (*p, 2.5, toneThenSilence (4000.0, 0.3), out);
        return peakSeconds (out, 2.0, 2.5);
    };

    const auto open   = tailWithLowpass (20000.0f);
    const auto closed = tailWithLowpass (400.0f);

    check (open > 1.0e-3f, "an open loop LP lets a bright tail ring on");
    check (closed < open * 0.25f, "closing the loop LP damps the tail every pass");
}

void testGrainCapIsNeverExceeded()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::density, 500.0f);
    setParam (*p, params::id::size, 2000.0f);
    setParam (*p, params::id::spread, 100.0f);

    int highWater = 0;
    Audio out;
    render (*p, 3.0, [] (int64_t i, int) { return sine (i, 180.0) * 0.5f; }, out,
            [&] (int64_t) { highWater = std::max (highWater, p->getActiveGrainCount()); });

    check (highWater <= kMaxGrains, "active grains never exceed kMaxGrains");
    check (highWater > kMaxGrains / 2, "max density actually loads the grain pool");
    check (allFinite (out), "grain stealing under overload stays finite");
}

void testRandomisedGrainPathRendersCleanly()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 300.0f);
    setParam (*p, params::id::spread, 60.0f);
    setParam (*p, params::id::reverse, 100.0f);
    setParam (*p, params::id::panSpread, 100.0f);
    setParam (*p, params::id::pitchSpread, 100.0f);
    setParam (*p, params::id::quantize, (float) scales::Scale::minor);
    setParam (*p, params::id::quantizeRoot, 3.0f);
    setParam (*p, params::id::feedback, 60.0f);

    Audio out;
    render (*p, 3.0, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);

    check (allFinite (out), "reverse + pan + quantised pitch spread stays finite");
    check (peakSeconds (out, 1.0, 3.0) > 1.0e-3f, "randomised grain path still produces a tail");
    check (peakSeconds (out, 1.0, 3.0) < 8.0f, "randomised grain path stays bounded");
}

void testScaleSnapping()
{
    using scales::Scale;
    using scales::snapToScale;

    const auto close = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };

    check (close (snapToScale (3.4f, Scale::off, 0), 3.4f), "Quantize off passes pitch through");
    check (close (snapToScale (3.4f, Scale::chromatic, 0), 3.0f), "chromatic snaps to semitones");
    check (close (snapToScale (1.0f, Scale::major, 0), 0.0f), "major snaps C# down to C");
    check (close (snapToScale (11.6f, Scale::major, 0), 12.0f), "snapping can cross into the next octave");
    check (close (snapToScale (-1.0f, Scale::octavesAndFifths, 0), 0.0f), "negative offsets snap correctly");
    check (close (snapToScale (4.0f, Scale::minor, 3), 3.0f), "root offsets the scale");
}

// ---- Phase 3: Freeze, Chaos, Randomize, Panic ---------------------------------

// Freeze stops the write head: whatever is in the buffer becomes an infinite
// source, and releasing it lets fresh (silent) input flush the tail again.
void testFreezeHoldsTheBuffer()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 200.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::freezeFade, 50.0f);

    Audio out;
    render (*p, 8.0, toneThenSilence (220.0, 1.0), out, [&] (int64_t blockStart)
    {
        if (blockStart == atSecond (0.9) / kBlockSize * kBlockSize)
            setParam (*p, params::id::freeze, 1.0f);
        if (blockStart == atSecond (6.0) / kBlockSize * kBlockSize)
            setParam (*p, params::id::freeze, 0.0f);
    });

    check (peakSeconds (out, 4.0, 5.0) > 0.01f, "frozen buffer sustains with no input and no feedback");
    check (peakSeconds (out, 7.5, 8.0) < 1.0e-4f, "releasing Freeze lets the tail flush out");
    check (allFinite (out), "freeze output is finite");
}

// Panic clears every buffer and grain on the spot, then the engine keeps working.
void testPanicClearsEverything()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 95.0f);

    Audio out;
    render (*p, 3.0, [] (int64_t i, int)
    {
        const auto t = (double) i / kSampleRate;
        return (t < 0.3 || (t >= 2.0 && t < 2.3)) ? sine (i, 220.0) * 0.5f : 0.0f;
    }, out, [&] (int64_t blockStart)
    {
        if (blockStart == atSecond (1.5) / kBlockSize * kBlockSize)
        {
            setParam (*p, params::id::panic, 1.0f);
            setParam (*p, params::id::panic, 0.0f);
        }
    });

    check (peakSeconds (out, 1.2, 1.5) > 1.0e-3f, "tail is sustaining before Panic");
    check (peakSeconds (out, 1.55, 2.0) < 1.0e-6f, "Panic silences the tail at once");
    check (peakSeconds (out, 2.3, 2.6) > 1.0e-3f, "engine keeps working after Panic");
}

// Chaos is the only randomness left once Spread and Pitch Spread are 0, so
// with a deterministic input it is what separates two otherwise identical runs.
void testChaosMovesTheTailAndStaysBounded()
{
    const auto renderWithChaos = [] (float chaos, float feedback, Audio& out)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 250.0f);
        setParam (*p, params::id::density, 40.0f);
        setParam (*p, params::id::size, 80.0f);
        setParam (*p, params::id::feedback, feedback);
        setParam (*p, params::id::chaos, chaos);
        render (*p, 3.0, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
    };

    Audio calm, wild;
    renderWithChaos (0.0f, 0.0f, calm);
    renderWithChaos (100.0f, 0.0f, wild);

    double difference = 0.0;
    for (size_t i = atSecond (1.0); i < atSecond (3.0); ++i)
        difference += std::pow ((double) calm[0][i] - (double) wild[0][i], 2.0);
    difference = std::sqrt (difference / (double) (atSecond (2.0)));

    check (rms (calm[0], atSecond (1.0), atSecond (3.0)) > 0.01, "chaos=0 renders a steady tail");
    check (difference > 0.3 * rms (calm[0], atSecond (1.0), atSecond (3.0)),
           "chaos=100 audibly moves the tail away from the chaos=0 render");

    Audio pushed;
    renderWithChaos (100.0f, 100.0f, pushed);
    check (allFinite (pushed), "chaos=100 at feedback=100 stays finite");
    check (peakSeconds (pushed, 2.0, 3.0) < 20.0f, "chaos=100 past unity stays bounded by the limiter");
}

// Randomize touches every parameter except the ones it must never touch — and
// "every" is enforced by iteration, so a parameter a later phase adds cannot be
// forgotten.
void testRandomizeCoversEverythingExceptTheExclusions()
{
    auto p = makeProcessor();
    juce::Random rng (77);

    std::map<juce::String, float> before;
    for (auto* param : p->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            before[withId->paramID] = param->getValue();

    randomize::apply (p->apvts, 0.0f, rng);
    bool unchangedAtZero = true;
    for (auto* param : p->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            unchangedAtZero = unchangedAtZero && std::abs (before[withId->paramID] - param->getValue()) < 1.0e-9f;
    check (unchangedAtZero, "randomize amount=0 changes nothing");

    std::map<juce::String, bool> changed;
    for (int i = 0; i < 50; ++i)
    {
        randomize::apply (p->apvts, 1.0f, rng);
        for (auto* param : p->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                if (std::abs (param->getValue() - before[withId->paramID]) > 1.0e-6f)
                    changed[withId->paramID] = true;
    }

    bool exclusionsHeld = true, everythingElseMoved = true;
    int moved = 0;
    for (auto* param : p->getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
        if (withId == nullptr)
            continue;

        if (randomize::isExcluded (withId->paramID))
            exclusionsHeld = exclusionsHeld && ! changed[withId->paramID];
        else
        {
            everythingElseMoved = everythingElseMoved && changed[withId->paramID];
            if (! changed[withId->paramID])
                std::printf ("       never randomised: %s\n", withId->paramID.toRawUTF8());
            ++moved;
        }
    }

    check (exclusionsHeld, "randomize never touches Mix, Output, Freeze, Wake, Panic, Fallback BPM, the stage switches or itself");
    check (randomize::isExcluded (params::id::loopOn) && randomize::isExcluded (params::id::transientsOn)
               && randomize::isExcluded (params::id::ageOn) && randomize::isExcluded (params::id::chaosOn)
               && randomize::isExcluded (params::id::modOn) && ! randomize::isExcluded (params::id::decay),
           "the five stage switches are excluded; Decay is not");
    check (randomize::isExcluded (params::id::wakeMode) && ! randomize::isExcluded (params::id::modSource[0])
               && ! randomize::isExcluded (params::id::displace),
           "Wake mode is excluded; mod slots and Displace are not");
    check (everythingElseMoved && moved > 30, "randomize moves every other parameter");
}

// ---- Phase 4: transient system ------------------------------------------------

void testOnsetDetectorFindsClicksNotSustain()
{
    OnsetDetector detector;
    detector.prepare (kSampleRate);
    detector.setSensitivity (0.5f);

    std::vector<float> mono ((size_t) (2.0 * kSampleRate), 0.0f);
    for (size_t i = 0; i < mono.size(); i += 12000) // 4 Hz click train
        mono[i] = 1.0f;

    int found = 0;
    std::array<int, 64> offsets {};
    for (size_t pos = 0; pos < mono.size(); pos += kBlockSize)
    {
        const float* channels[2] { mono.data() + pos, mono.data() + pos };
        const auto n = (int) std::min ((size_t) kBlockSize, mono.size() - pos);
        found += detector.process (channels, 2, n, offsets.data(), 64);
    }
    check (found >= 7 && found <= 9, "detector finds a 4 Hz click train (8 in 2 s)");

    detector.reset();
    std::vector<float> tone ((size_t) (2.0 * kSampleRate));
    for (size_t i = 0; i < tone.size(); ++i)
        tone[i] = sine ((int64_t) i, 220.0) * 0.5f;

    int afterStart = 0;
    for (size_t pos = 0; pos < tone.size(); pos += kBlockSize)
    {
        const float* channels[2] { tone.data() + pos, tone.data() + pos };
        const auto n = (int) std::min ((size_t) kBlockSize, tone.size() - pos);
        const auto here = detector.process (channels, 2, n, offsets.data(), 64);
        if (pos > atSecond (0.2))
            afterStart += here;
    }
    check (afterStart == 0, "a sustained tone produces no onsets after its start");

    // An abrupt note-off splatters energy across bins, which reads as flux;
    // the energy gate is what keeps a cut-off from counting as a hit.
    detector.reset();
    int atTheStop = 0;
    for (size_t pos = 0; pos < tone.size(); pos += kBlockSize)
    {
        std::vector<float> block ((size_t) kBlockSize, 0.0f);
        for (size_t i = 0; i < block.size() && pos + i < tone.size(); ++i)
            block[i] = pos + i < atSecond (1.0) ? tone[pos + i] : 0.0f;
        const float* channels[2] { block.data(), block.data() };
        const auto here = detector.process (channels, 2, kBlockSize, offsets.data(), 64);
        if (pos > atSecond (0.2))
            atTheStop += here;
    }
    check (atTheStop == 0, "a note-off is not an onset");
}

// A hit fires a burst of grains that all read the same audio, so the hit
// stutters at the burst rate instead of smearing.
void testRetriggerStuttersTheHit()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 2000.0f);      // scheduled grains read silence
    setParam (*p, params::id::density, 0.5f);
    setParam (*p, params::id::size, 40.0f);
    setParam (*p, params::id::window, 3.0f);        // expo-decay: reads as a hit
    setParam (*p, params::id::retriggerOn, 1.0f);
    setParam (*p, params::id::retriggerCount, 4.0f);
    setParam (*p, params::id::retriggerRate, 100.0f);
    setParam (*p, params::id::retriggerAmount, 100.0f);
    setParam (*p, params::id::retriggerOffset, 0.0f);

    Audio out;
    render (*p, 1.5, clickAt (0.5), out);

    check (p->getOnsetCount() == 1, "one click is one onset");

    // Detection lands a few ms after the click; each burst grain is 40 ms.
    bool fourBursts = true, gapsSilent = true;
    for (int k = 0; k < 4; ++k)
    {
        const auto start = 0.5 + 0.1 * k;
        fourBursts = fourBursts && peakSeconds (out, start, start + 0.06) > 0.1f;
        gapsSilent = gapsSilent && peakSeconds (out, start + 0.06, start + 0.098) < 1.0e-3f;
    }
    check (fourBursts, "retrigger fires 4 bursts at the burst rate");
    check (gapsSilent, "the gaps between bursts are silent");
    check (peakSeconds (out, 0.92, 1.5) < 1.0e-3f, "the burst stops after Count grains");
}

// Duck pushes the wet signal down on a hit and lets it bloom back.
void testDuckDipsTheWetOnAHit()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 100.0f);
    setParam (*p, params::id::duckOn, 1.0f);
    setParam (*p, params::id::duckDepth, 100.0f);
    setParam (*p, params::id::duckAttack, 5.0f);
    setParam (*p, params::id::duckRelease, 300.0f);

    Audio out;
    render (*p, 3.5, [] (int64_t i, int)
    {
        const auto t = (double) i / kSampleRate;
        if (t < 0.5)                    return sine (i, 220.0) * 0.5f;
        if (i == (int64_t) atSecond (2.0)) return 0.5f; // the hit
        return 0.0f;
    }, out);

    const auto before   = peakSeconds (out, 1.8, 2.0);
    const auto ducked   = peakSeconds (out, 2.03, 2.06);
    const auto restored = peakSeconds (out, 3.0, 3.5);

    check (before > 1.0e-3f, "tail is sustaining before the hit");
    check (ducked < before * 0.25f, "duck pushes the wet down within Attack");
    check (restored > before * 0.3f, "duck releases and the tail blooms back");
}

// Choke kills the existing tail — the grains in flight and the buffer they
// would keep reading from — so each hit gets its own space.
void testChokeKillsTheTail()
{
    const auto tailAfterHit = [] (bool choke)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::chokeOn, choke ? 1.0f : 0.0f);
        setParam (*p, params::id::chokeAmount, 100.0f);
        setParam (*p, params::id::chokeFade, 20.0f);

        Audio out;
        render (*p, 2.6, [] (int64_t i, int)
        {
            const auto t = (double) i / kSampleRate;
            if (t < 0.5)                       return sine (i, 220.0) * 0.5f;
            if (i == (int64_t) atSecond (2.0)) return 0.05f; // a quiet hit
            return 0.0f;
        }, out);

        return std::make_pair (peakSeconds (out, 1.8, 2.0), peakSeconds (out, 2.1, 2.6));
    };

    const auto [beforeChoked, afterChoked]     = tailAfterHit (true);
    const auto [beforeOpen,   afterOpen]       = tailAfterHit (false);

    check (beforeChoked > 1.0e-3f && beforeOpen > 1.0e-3f, "tail is sustaining before the hit");
    check (afterChoked < afterOpen * 0.3f, "choke removes the tail that an un-choked run keeps");
    check (afterChoked < beforeChoked * 0.3f, "choked tail is well below its pre-hit level");
}

// The input envelope drives Density: positive means loud = dense, and the
// sign flips the relationship.
void testEnvelopeDrivesDensity()
{
    const auto grainsLoudVsQuiet = [] (float envDensity)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 20.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::envDensity, envDensity);

        double loudSum = 0.0, quietSum = 0.0;
        int loudN = 0, quietN = 0;
        Audio out;
        render (*p, 2.0, [] (int64_t i, int)
        {
            const auto amplitude = i < (int64_t) atSecond (1.0) ? 0.8f : 0.05f;
            return sine (i, 220.0) * amplitude;
        }, out, [&] (int64_t blockStart)
        {
            if (blockStart >= (int64_t) atSecond (0.5) && blockStart < (int64_t) atSecond (1.0))
            { loudSum += p->getActiveGrainCount(); ++loudN; }
            if (blockStart >= (int64_t) atSecond (1.5))
            { quietSum += p->getActiveGrainCount(); ++quietN; }
        });

        return std::make_pair (loudSum / loudN, quietSum / quietN);
    };

    const auto [loudPos, quietPos] = grainsLoudVsQuiet (100.0f);
    const auto [loudNeg, quietNeg] = grainsLoudVsQuiet (-100.0f);

    check (loudPos > quietPos * 1.5, "env->density +100: hits are dense, sustains sparse");
    check (quietNeg > loudNeg * 1.5, "env->density -100: hits are discrete, quiet washes out");
}

// With Sync on, grain slots are a musical division; Swing delays every other
// slot toward a triplet feel.
void testSyncAndSwingPlaceGrainsOnTheGrid()
{
    const auto blipGaps = [] (float swing)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::fallbackBpm, 120.0f);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::size, 10.0f);
        setParam (*p, params::id::sync, 1.0f);
        setParam (*p, params::id::grainDivision, 10.0f); // 1/8 = 250 ms at 120
        setParam (*p, params::id::swing, swing);

        Audio out;
        render (*p, 3.0, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);

        std::vector<double> gaps;
        const auto onsets = soundOnsets (out[0], atSecond (1.0), atSecond (3.0));
        for (size_t i = 1; i < onsets.size(); ++i)
            gaps.push_back ((double) (onsets[i] - onsets[i - 1]) * 1000.0 / kSampleRate);
        return gaps;
    };

    const auto straight = blipGaps (0.0f);
    bool allQuarterBeats = straight.size() >= 6;
    for (auto gap : straight)
        allQuarterBeats = allQuarterBeats && std::abs (gap - 250.0) < 3.0;
    check (allQuarterBeats, "sync: grains land every 1/8 (250 ms at 120 BPM)");

    const auto swung = blipGaps (100.0f);
    double longest = 0.0, shortest = 1.0e9;
    for (auto gap : swung) { longest = std::max (longest, gap); shortest = std::min (shortest, gap); }
    check (swung.size() >= 6 && std::abs (longest - 333.3) < 5.0 && std::abs (shortest - 166.7) < 5.0,
           "swing=100: slots alternate 2:1 (333 / 167 ms)");
}


// ---- Phase 5: Age, per-pass Degrade, Lifetime Curves ---------------------------

// Age is tracked per buffer sample in seconds, so a grain reading far back is
// genuinely older, and a tail that has been round the loop is older still.
void testAgeTracksHowOldTheAudioIs()
{
    const auto averageAgeAt = [] (float spread, float feedback, double toneUntil, double readAt)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 100.0f);
        setParam (*p, params::id::spread, spread);
        setParam (*p, params::id::feedback, feedback);

        // Average over a full second of blocks: at Spread 100 a single block's
        // few sounding grains can all happen to be young.
        double ageSum = 0.0;
        int blocks = 0;
        Audio out;
        render (*p, readAt + 0.05, toneThenSilence (220.0, toneUntil), out, [&] (int64_t blockStart)
        {
            if (blockStart >= (int64_t) atSecond (readAt - 1.0) && blockStart < (int64_t) atSecond (readAt))
            {
                ageSum += p->getAverageAgeSeconds();
                ++blocks;
            }
        });
        return (float) (ageSum / juce::jmax (1, blocks));
    };

    const auto plainDelay = averageAgeAt (0.0f, 0.0f, 10.0, 3.0);
    const auto scattered  = averageAgeAt (100.0f, 0.0f, 10.0, 3.0);
    const auto recirculated = averageAgeAt (0.0f, 90.0f, 0.3, 2.0);

    check (plainDelay > 0.05f && plainDelay < 0.2f, "a plain 100 ms delay plays ~100 ms old audio");
    check (scattered > plainDelay * 5.0f, "spread=100 plays much older audio than Time alone");
    check (recirculated > 1.0f, "a tail that has recirculated for 2 s is over a second old");
}

// A Level curve from 1 to 0 over Lifetime is a hard stop on the tail's age,
// whatever Feedback says.
void testLevelCurveKillsTheTail()
{
    const auto tailAt = [] (bool enableCurve)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::lifetime, 1000.0f);
        setLine (*p, lifetime::Destination::level, 1.0f, 0.0f, enableCurve);

        Audio out;
        render (*p, 3.0, toneThenSilence (220.0, 0.3), out);
        return peakSeconds (out, 2.0, 3.0);
    };

    check (tailAt (false) > 1.0e-2f, "without the Level curve a 95 % tail is still loud at 2 s");
    check (tailAt (true) < 1.0e-3f, "a Level curve to zero over a 1 s Lifetime ends the tail");
}

// A Pitch curve maps age to pitch. With feedback off, Time *is* the age of
// what a grain plays, so audio read from a second ago has to come back higher
// than audio read from 100 ms ago. (Compounding through the loop is the same
// mechanism the shimmer test already proves.)
void testPitchCurveRaisesOlderAudio()
{
    const auto pitchAtTime = [] (float timeMs)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, timeMs);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::pitchSpread, 5.0f); // decorrelates the comb, one pass so no random walk
        setParam (*p, params::id::lifetime, 2000.0f);
        setLine (*p, lifetime::Destination::pitchOffset, 0.5f, 0.75f, true); // 0 -> +12 st over 2 s

        Audio out;
        render (*p, 2.5, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
        return dominantFrequency (out[0], atSecond (2.0), atSecond (2.4));
    };

    const auto young = pitchAtTime (100.0f);   // age 0.1 s -> +0.6 st
    const auto old   = pitchAtTime (1000.0f);  // age 1.0 s -> +6 st

    check (young > 210.0 && young < 245.0, "pitch curve leaves 100 ms-old audio near its own pitch");
    check (old > young * 1.25, "pitch curve plays second-old audio a good fourth higher");
}

// LP tilt per pass compounds: every pass lowers the cutoff, so a bright tail
// dies far sooner than its feedback alone would let it.
void testDegradeTiltDampsTheTail()
{
    const auto tailWithTilt = [] (float tiltHz)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 90.0f);
        setParam (*p, params::id::fbLowpass, 8000.0f);
        setParam (*p, params::id::degradeTilt, tiltHz);

        Audio out;
        render (*p, 2.2, toneThenSilence (4000.0, 0.3), out);
        return peakSeconds (out, 1.8, 2.2);
    };

    const auto clean  = tailWithTilt (0.0f);
    const auto tilted = tailWithTilt (500.0f);

    check (clean > 1.0e-4f, "an untilted bright tail is still audible at 2 s");
    check (tilted < clean * 0.3f, "500 Hz/pass tilt kills the bright tail well before that");
}

// Bit and sample-rate reduction per pass compound with age and stay bounded.
void testDegradeBitsAndRateChangeTheTail()
{
    const auto renderTail = [] (float bitsPerPass, float ratePerPass, Audio& out)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 90.0f);
        setParam (*p, params::id::degradeBits, bitsPerPass);
        setParam (*p, params::id::degradeRate, ratePerPass);
        render (*p, 2.0, toneThenSilence (220.0, 0.3), out);
    };

    Audio clean, crushed;
    renderTail (0.0f, 0.0f, clean);
    renderTail (2.0f, 10.0f, crushed);

    double difference = 0.0;
    for (size_t i = atSecond (1.0); i < atSecond (2.0); ++i)
        difference += std::pow ((double) clean[0][i] - (double) crushed[0][i], 2.0);
    difference = std::sqrt (difference / (double) atSecond (1.0));

    check (allFinite (crushed) && peakSeconds (crushed, 0.0, 2.0) < 4.0f, "degraded tail stays finite and bounded");
    check (difference > 0.2 * rms (clean[0], atSecond (1.0), atSecond (2.0)),
           "bits/pass and SR/pass audibly change the tail as it ages");
}

// Noise per pass is the one Degrade control that adds rather than removes,
// so it shows up even with nothing coming in.
void testDegradeNoiseAddsHiss()
{
    const auto hiss = [] (float noise)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::degradeNoise, noise);

        Audio out;
        render (*p, 2.0, [] (int64_t, int) { return 0.0f; }, out);
        return peakSeconds (out, 1.0, 2.0);
    };

    check (hiss (0.0f) < 1.0e-12f, "noise/pass at 0 keeps silence-in-silence-out");
    const auto level = hiss (100.0f);
    check (level > 1.0e-4f && level < 0.05f, "noise/pass at 100 adds hiss around -40 dB");
}

// Pitch drift per pass compounds through the loop: after a dozen passes at
// +50 cents the tail is a good half-octave above where it started.
void testDegradeDriftRaisesThePitch()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::size, 100.0f);
    // The drift shifter is in the loop, not per grain: grains stay at rate 1,
    // read coherently at Spread 0, and the whole tail climbs together.
    setParam (*p, params::id::feedback, 90.0f);
    setParam (*p, params::id::degradeDrift, 50.0f);
    setParam (*p, params::id::degradeDriftDir, 0.0f); // Up

    Audio out;
    render (*p, 1.6, toneThenSilence (220.0, 0.3), out);

    const auto later = dominantFrequency (out[0], atSecond (1.2), atSecond (1.5));
    check (later > 220.0 * 1.25, "+50 ct/pass drift has raised the tail by over 4 st at 1.2 s");
}

// Reverse driven by Age (5.7) is the Reverse curve destination.
void testReverseCurveRenders()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 200.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 80.0f);
    setParam (*p, params::id::lifetime, 1000.0f);
    setLine (*p, lifetime::Destination::reverse, 0.0f, 1.0f, true);

    Audio out;
    render (*p, 2.0, toneThenSilence (220.0, 0.3), out);

    check (allFinite (out), "age-driven Reverse renders finite audio");
    check (peakSeconds (out, 1.0, 2.0) > 1.0e-3f, "age-driven Reverse still produces a tail");
}

void testCurveStateRoundTrips()
{
    auto p = makeProcessor();
    auto curves = p->getCurves();
    auto& curve = curves.curves[(size_t) lifetime::Destination::lowpass];
    curve.numPoints = 3;
    curve.points[0] = { 0.0f, 1.0f, 0.0f };
    curve.points[1] = { 0.3f, 0.2f, 0.6f };
    curve.points[2] = { 1.0f, 0.9f, -0.4f };
    p->setCurves (curves);
    setParam (*p, params::id::curveEnable[(size_t) lifetime::Destination::lowpass], 1.0f);

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());
    const auto restored = q->getCurves().curves[(size_t) lifetime::Destination::lowpass];

    const auto same = restored.numPoints == 3
                   && std::abs (restored.points[1].x - 0.3f) < 1.0e-5f
                   && std::abs (restored.points[1].y - 0.2f) < 1.0e-5f
                   && std::abs (restored.points[1].bend - 0.6f) < 1.0e-5f
                   && std::abs (restored.points[2].bend + 0.4f) < 1.0e-5f;
    check (same, "Lifetime Curve points survive get/setStateInformation");
    check (getParam (*q, params::id::curveEnable[(size_t) lifetime::Destination::lowpass]) >= 0.5f,
           "curve enable state survives too");
    check (std::abs (restored.evaluate (0.3f) - 0.2f) < 1.0e-4f, "restored curve evaluates through its points");
}

// ---- Phase 6: Rewind ----------------------------------------------------------

// A decaying tail captured and played back reversed comes out *rising*, and
// it arrives through the feedback path into the main buffer.
void testRewindManualSwellsReversed()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::rewindOn, 1.0f);
    setParam (*p, params::id::rewindTrigger, 3.0f); // Manual
    setParam (*p, params::id::rewindLength, 500.0f);
    setParam (*p, params::id::rewindLevel, 100.0f);
    setParam (*p, params::id::rewindPitch, 0.0f);

    Audio out;
    render (*p, 1.6, decayingBurst (220.0, 0.3, 0.1), out, [&] (int64_t blockStart)
    {
        if (blockStart == atSecond (0.6) / kBlockSize * kBlockSize)
        {
            setParam (*p, params::id::rewindManual, 1.0f);
            setParam (*p, params::id::rewindManual, 0.0f);
        }
    });

    check (peakSeconds (out, 0.55, 0.78) < 1.0e-4f, "the tail is gone before the rewind arrives");
    check (peakSeconds (out, 0.9, 1.25) > 1.0e-2f, "the rewound tail swells up through the loop");
    check (peakSeconds (out, 1.05, 1.25) > peakSeconds (out, 0.85, 0.95) * 1.5f,
           "the rewound tail rises where the original fell");
    check (allFinite (out), "rewind output is finite");
}

// Threshold trigger: when the tail falls below the threshold, it is rewound
// and swells back — audible where an un-rewound tail would be gone.
void testRewindThresholdRefiresTheTail()
{
    const auto lateLevel = [] (bool rewind)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 70.0f);
        setParam (*p, params::id::rewindOn, rewind ? 1.0f : 0.0f);
        setParam (*p, params::id::rewindTrigger, 2.0f); // Threshold
        setParam (*p, params::id::rewindThreshold, -40.0f);
        setParam (*p, params::id::rewindLength, 1000.0f);
        setParam (*p, params::id::rewindLevel, 100.0f);

        Audio out;
        render (*p, 4.5, toneThenSilence (220.0, 0.3), out);
        return std::make_pair (peakSeconds (out, 3.0, 4.0), allFinite (out));
    };

    const auto [withRewind, finite] = lateLevel (true);
    const auto [without, finite2]   = lateLevel (false);

    check (finite && finite2, "threshold-triggered rewinds stay finite");
    check (withRewind > 1.0e-3f && withRewind > without * 3.0f,
           "threshold rewind keeps the tail alive where it would have died");
}

// Timer trigger: every interval the last capture is added back, which lifts
// the wet level for the length of the rewind.
void testRewindTimerBumpsPeriodically()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::rewindOn, 1.0f);
    setParam (*p, params::id::rewindTrigger, 0.0f); // Timer
    setParam (*p, params::id::rewindInterval, 1.0f);
    setParam (*p, params::id::rewindLength, 500.0f);
    setParam (*p, params::id::rewindLevel, 100.0f);

    juce::Random noise (7);
    Audio out;
    render (*p, 3.0, [&noise] (int64_t, int) { return noise.nextFloat() * 0.6f - 0.3f; }, out);

    const auto quiet1 = rms (out[0], atSecond (0.60), atSecond (0.95));
    const auto bump1  = rms (out[0], atSecond (1.15), atSecond (1.50));
    const auto quiet2 = rms (out[0], atSecond (1.65), atSecond (1.95));
    const auto bump2  = rms (out[0], atSecond (2.15), atSecond (2.50));

    check (bump1 > quiet1 * 1.25 && bump2 > quiet2 * 1.25, "timer rewinds lift the wet level once a second");
}

void testRewindRestartStaysClean()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::feedback, 60.0f);
    setParam (*p, params::id::rewindOn, 1.0f);
    setParam (*p, params::id::rewindTrigger, 3.0f); // Manual
    setParam (*p, params::id::rewindLength, 800.0f);
    setParam (*p, params::id::rewindPitch, -12.0f);

    Audio out;
    render (*p, 2.5, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out, [&] (int64_t blockStart)
    {
        const auto first  = atSecond (1.0) / kBlockSize * kBlockSize;
        const auto second = atSecond (1.1) / kBlockSize * kBlockSize;
        if (blockStart == first || blockStart == second)
        {
            setParam (*p, params::id::rewindManual, 1.0f);
            setParam (*p, params::id::rewindManual, 0.0f);
        }
    });

    check (allFinite (out), "restarting a rewind mid-playback stays finite");
    check (peakSeconds (out, 1.0, 2.5) < 4.0f, "restarting a rewind stays bounded");
}

} // namespace

// ---- Phase 7: Wake ------------------------------------------------------------

// Displace is Choke made continuous: in Shared mode a hit pushes the tail
// down by that much, with no Choke switch involved.
void testDisplacePushesTheTailDown()
{
    const auto tailAfterHit = [] (float displace)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::chokeFade, 20.0f);
        setParam (*p, params::id::displace, displace);

        Audio out;
        render (*p, 2.6, [] (int64_t i, int)
        {
            const auto t = (double) i / kSampleRate;
            if (t < 0.5)                       return sine (i, 220.0) * 0.5f;
            if (i == (int64_t) atSecond (2.0)) return 0.05f; // a quiet hit
            return 0.0f;
        }, out);

        return std::make_pair (peakSeconds (out, 1.8, 2.0), peakSeconds (out, 2.1, 2.6));
    };

    const auto [beforeFull, afterFull] = tailAfterHit (100.0f);
    const auto [beforeNone, afterNone] = tailAfterHit (0.0f);

    check (beforeFull > 1.0e-3f && beforeNone > 1.0e-3f, "tail is sustaining before the hit");
    check (afterFull < afterNone * 0.3f, "displace=100 wipes the tail a hit lands on");
    check (afterNone > beforeNone * 0.3f, "displace=0 lets hits pile up on the tail");
}

// Isolated: every hit gets its own instance, so a later hit's Choke cannot
// touch an earlier tail — that is what "stays articulate" means.
void testIsolatedProtectsEarlierTails()
{
    const auto run = [] (bool isolated)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::wakeMode, isolated ? 1.0f : 0.0f);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::chokeOn, 1.0f);
        setParam (*p, params::id::chokeAmount, 100.0f);
        setParam (*p, params::id::chokeFade, 20.0f);

        int instances = 0;
        Audio out;
        render (*p, 2.6, [] (int64_t i, int)
        {
            const auto t = (double) i / kSampleRate;
            if (t < 0.5)                       return sine (i, 220.0) * 0.5f;
            if (i == (int64_t) atSecond (2.0)) return 0.05f;
            return 0.0f;
        }, out, [&] (int64_t) { instances = juce::jmax (instances, p->getActiveInstanceCount()); });

        return std::make_tuple (peakSeconds (out, 1.8, 2.0), peakSeconds (out, 2.1, 2.6), instances, allFinite (out));
    };

    const auto [sharedBefore, sharedAfter, sharedInstances, sharedFinite] = run (false);
    const auto [isoBefore, isoAfter, isoInstances, isoFinite]             = run (true);

    check (sharedFinite && isoFinite, "both Wake modes render finite audio");
    check (sharedBefore > 1.0e-3f && isoBefore > 1.0e-3f, "a tail is sustaining before the second hit in both modes");
    check (sharedInstances == 0 && isoInstances >= 2, "Isolated spawns an instance per hit; Shared spawns none");
    check (sharedAfter < sharedBefore * 0.3f, "Shared: the second hit chokes the first tail");
    check (isoAfter > isoBefore * 0.4f, "Isolated: the first tail survives the second hit's choke");
}

// A new instance is primed with the input that spawned it, so Retrigger can
// still stutter the hit even though the instance's buffer is seconds old.
void testIsolatedRetriggerReadsTheHit()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::wakeMode, 1.0f);
    setParam (*p, params::id::time, 2000.0f);
    setParam (*p, params::id::density, 0.5f);
    setParam (*p, params::id::size, 40.0f);
    setParam (*p, params::id::window, 3.0f);
    setParam (*p, params::id::retriggerOn, 1.0f);
    setParam (*p, params::id::retriggerCount, 4.0f);
    setParam (*p, params::id::retriggerRate, 100.0f);
    setParam (*p, params::id::retriggerAmount, 100.0f);
    setParam (*p, params::id::retriggerOffset, 0.0f);

    Audio out;
    render (*p, 1.5, clickAt (0.5), out);

    bool fourBursts = true;
    for (int k = 0; k < 4; ++k)
    {
        const auto start = 0.5 + 0.1 * k;
        fourBursts = fourBursts && peakSeconds (out, start, start + 0.06) > 0.1f;
    }
    check (fourBursts, "Isolated: a fresh instance still stutters the hit that spawned it");
    check (p->getActiveInstanceCount() >= 1, "Isolated: the hit spawned an instance");
}

void testIsolatedInstanceCap()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::wakeMode, 1.0f);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 90.0f);

    int highWater = 0;
    Audio out;
    render (*p, 3.0, [] (int64_t i, int)
    {
        // Twelve hits 150 ms apart from 0.2 s.
        for (int k = 0; k < 12; ++k)
            if (i == (int64_t) atSecond (0.2 + 0.15 * k))
                return 1.0f;
        return 0.0f;
    }, out, [&] (int64_t) { highWater = juce::jmax (highWater, p->getActiveInstanceCount()); });

    check (allFinite (out), "twelve hits in Isolated mode stay finite");
    check (highWater >= 4, "Isolated spawns an instance per hit");
    check (highWater <= WakeEngine::kMaxInstances, "Isolated never sounds more than eight instances");
}

// Switching modes re-routes the input; the tail that is already ringing
// keeps ringing on the side it lives.
void testWakeSwitchKeepsTheTail()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 95.0f);

    Audio out;
    render (*p, 3.0, toneThenSilence (220.0, 0.5), out, [&] (int64_t blockStart)
    {
        if (blockStart == (int64_t) atSecond (1.0) / kBlockSize * kBlockSize) setParam (*p, params::id::wakeMode, 1.0f);
        if (blockStart == (int64_t) atSecond (2.0) / kBlockSize * kBlockSize) setParam (*p, params::id::wakeMode, 0.0f);
    });

    const auto before = peakSeconds (out, 0.8, 1.0);
    const auto after  = peakSeconds (out, 1.2, 1.5);
    const auto later  = peakSeconds (out, 2.2, 2.5);

    check (allFinite (out), "switching Wake modes mid-tail stays finite");
    check (after > before * 0.4f, "switching to Isolated keeps the shared tail ringing");
    check (later > after * 0.3f, "switching back to Shared keeps it ringing too");
}

// Between onsets input feeds the newest instance — and with no onset at all,
// sustained material still gets one.
void testIsolatedSustainedInputGetsATail()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::wakeMode, 1.0f);
    setParam (*p, params::id::sensitivity, 0.0f);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 60.0f);
    setParam (*p, params::id::feedback, 90.0f);

    Audio out;
    render (*p, 2.0, [] (int64_t i, int)
    {
        const auto t = (double) i / kSampleRate;
        if (t < 0.2 || t >= 1.0)
            return 0.0f;
        const auto swell = (float) juce::jmin (1.0, (t - 0.2) / 0.2); // no edge to detect
        return sine (i, 220.0) * 0.4f * swell;
    }, out);

    check (peakSeconds (out, 1.1, 1.5) > 1.0e-3f, "Isolated: a swelled tone with no onset still gets a tail");
}

// ---- Phase 8: modulation ------------------------------------------------------

void testModulationMaths()
{
    using namespace mod;
    check (std::abs (shape (0.5f, Curve::linear) - 0.5f) < 1.0e-6f
               && std::abs (shape (0.5f, Curve::exponential) - 0.25f) < 1.0e-6f
               && std::abs (shape (0.5f, Curve::logarithmic) - 0.70710678f) < 1.0e-5f,
           "mod curves: linear passes, exp squares, log roots");

    const juce::NormalisableRange<float> range (0.0f, 100.0f);
    check (std::abs (applyOffset (range, 50.0f, 0.25f) - 75.0f) < 1.0e-4f
               && std::abs (applyOffset (range, 90.0f, 0.5f) - 100.0f) < 1.0e-4f
               && std::abs (applyOffset (range, 10.0f, -0.5f)) < 1.0e-4f,
           "mod offsets move in normalised space and clamp at the ends");

    check (perGrainIndex (Destination::size) >= 0 && perGrainIndex (Destination::feedback) < 0,
           "Size is a per-grain destination, Feedback a global one");
}

// An LFO on Mix: the dry level dips every half cycle, free-running or synced.
void testLfoModulatesMix()
{
    const auto amplitudeAt = [] (bool synced, double from, double to)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 3000.0f); // the wet stays silent throughout
        setParam (*p, params::id::mix, 50.0f);
        setParam (*p, params::id::modSource[0], (float) mod::Source::lfo1);
        setParam (*p, params::id::modDestination[0], (float) mod::Destination::mix);
        setParam (*p, params::id::modAmount[0], 100.0f);
        setParam (*p, params::id::lfoShape[0], (float) mod::LfoShape::sine);
        setParam (*p, params::id::lfoRate[0], 2.0f);
        setParam (*p, params::id::lfoSync[0], synced ? 1.0f : 0.0f);
        setParam (*p, params::id::lfoDivision[0], 13.0f); // 1/4: 2 Hz at 120 BPM
        setParam (*p, params::id::fallbackBpm, 120.0f);

        Audio out;
        render (*p, 1.2, [] (int64_t i, int) { return sine (i, 1000.0) * 0.5f; }, out);
        return peakSeconds (out, from, to);
    };

    // Mix rides 50..100 %, so the dry gain runs from 0.71 down to 0.
    const auto low1  = amplitudeAt (false, 0.23, 0.27);
    const auto high1 = amplitudeAt (false, 0.48, 0.52);
    const auto low2  = amplitudeAt (false, 0.73, 0.77);
    check (high1 > 0.25f && low1 < high1 * 0.2f && low2 < high1 * 0.2f,
           "a 2 Hz LFO on Mix dips the dry level twice a second");

    const auto lowSynced  = amplitudeAt (true, 0.23, 0.27);
    const auto highSynced = amplitudeAt (true, 0.48, 0.52);
    check (highSynced > 0.25f && lowSynced < highSynced * 0.2f,
           "a synced 1/4-note LFO at 120 BPM runs at the same 2 Hz");
}

// The Transient source is a one-shot: a hit makes the scheduler dense for
// the decay time.
void testTransientSourceRaisesDensity()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 100.0f);
    setParam (*p, params::id::density, 5.0f);
    setParam (*p, params::id::size, 80.0f);
    setParam (*p, params::id::modSource[1], (float) mod::Source::transient);
    setParam (*p, params::id::modDestination[1], (float) mod::Destination::density);
    setParam (*p, params::id::modAmount[1], 100.0f);
    setParam (*p, params::id::modTransientDecay, 300.0f);

    int before = 0, after = 0;
    Audio out;
    render (*p, 1.5, clickAt (1.0), out, [&] (int64_t blockStart)
    {
        if (blockStart >= (int64_t) atSecond (0.5) && blockStart < (int64_t) atSecond (0.95))
            before = juce::jmax (before, p->getActiveGrainCount());
        if (blockStart >= (int64_t) atSecond (1.02) && blockStart < (int64_t) atSecond (1.15))
            after = juce::jmax (after, p->getActiveGrainCount());
    });

    check (before <= 2, "at 5 grains/s only a grain or two sounds at once");
    check (after >= before + 6, "a hit through the Transient source makes the scheduler dense");
}

// Per-grain Age modulating Pitch resolves per grain: with feedback off, Time
// is the age of what a grain plays, so second-old audio comes back higher
// than 100 ms-old audio — the Lifetime-curve test, through a mod slot.
void testAgeModRaisesOlderGrains()
{
    const auto pitchAtTime = [] (float timeMs)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, timeMs);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::pitchSpread, 5.0f);
        setParam (*p, params::id::lifetime, 2000.0f);
        setParam (*p, params::id::modSource[2], (float) mod::Source::agePerGrain);
        setParam (*p, params::id::modDestination[2], (float) mod::Destination::pitch);
        setParam (*p, params::id::modAmount[2], 25.0f); // a quarter of ±24 st: +12 st at full Lifetime

        Audio out;
        render (*p, 2.5, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
        return dominantFrequency (out[0], atSecond (2.0), atSecond (2.4));
    };

    const auto young = pitchAtTime (100.0f);   // age 0.05 of Lifetime -> +0.6 st
    const auto old   = pitchAtTime (1000.0f);  // age 0.5 of Lifetime -> +6 st

    check (young > 210.0 && young < 245.0, "per-grain Age mod leaves 100 ms-old audio near its pitch");
    check (old > young * 1.25, "per-grain Age mod plays second-old audio a good fourth higher");
}

// ---- Phase 9: output stage, presets ------------------------------------------

// Width is mid/side on the wet signal: 0 folds it to mono, 200 doubles the side.
void testWidthCollapsesAndWidens()
{
    const auto side = [] (float width)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::width, width);

        // Left-only input: a deterministic stereo image.
        Audio out;
        render (*p, 1.5, [] (int64_t i, int ch) { return ch == 0 ? sine (i, 220.0) * 0.5f : 0.0f; }, out);

        double energy = 0.0;
        float maxDiff = 0.0f;
        for (auto i = atSecond (0.5); i < atSecond (1.5); ++i)
        {
            const auto d = out[0][i] - out[1][i];
            energy += (double) d * (double) d;
            maxDiff = juce::jmax (maxDiff, std::abs (d));
        }
        return std::make_pair (std::sqrt (energy / (double) atSecond (1.0)), maxDiff);
    };

    const auto [sideMono, diffMono]     = side (0.0f);
    const auto [sideNormal, diffNormal] = side (100.0f);
    const auto [sideWide, diffWide]     = side (200.0f);
    juce::ignoreUnused (diffNormal, diffWide);

    check (diffMono < 1.0e-5f, "width=0 folds the wet signal to mono");
    check (sideNormal > 1.0e-3, "a left-only input gives the wet a stereo image at width=100");
    check (std::abs (sideWide / sideNormal - 2.0) < 0.1, "width=200 doubles the side signal");
}

// Wet HP/LP sit after the loop: they clean the output without touching the
// feedback behaviour or the dry path.
void testWetFiltersCleanTheOutput()
{
    const auto level = [] (double hz, float wetHp, float wetLp)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::wetHighpass, wetHp);
        setParam (*p, params::id::wetLowpass, wetLp);

        Audio out;
        render (*p, 1.5, [hz] (int64_t i, int) { return sine (i, hz) * 0.5f; }, out);
        return rms (out[0], atSecond (0.5), atSecond (1.5));
    };

    const auto brightOpen = level (3520.0, 20.0f, 20000.0f);
    const auto brightLp   = level (3520.0, 20.0f, 300.0f);
    const auto lowOpen    = level (220.0, 20.0f, 20000.0f);
    const auto lowHp      = level (220.0, 3000.0f, 20000.0f);

    check (brightLp < brightOpen * 0.1, "wet LP at 300 Hz takes a 3.5 kHz tail down by over 20 dB");
    check (lowHp < lowOpen * 0.1, "wet HP at 3 kHz takes a 220 Hz tail down by over 20 dB");

    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::mix, 0.0f);
    setParam (*p, params::id::wetLowpass, 300.0f);
    setParam (*p, params::id::wetHighpass, 3000.0f);
    Audio out;
    render (*p, 0.5, [] (int64_t i, int) { return sine (i, 1000.0) * 0.5f; }, out);
    bool untouched = true;
    for (auto i = atSecond (0.1); i < atSecond (0.5); ++i)
        untouched = untouched && std::abs (out[0][i] - sine ((int64_t) i, 1000.0) * 0.5f) < 1.0e-4f;
    check (untouched, "wet filters leave the dry path alone");
}

void testPresetFileRoundTrip()
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SillageTest.sillage");

    {
        auto p = makeProcessor();
        setParam (*p, params::id::mix, 72.0f);
        setParam (*p, params::id::wakeMode, 1.0f);
        setParam (*p, params::id::modSource[3], (float) mod::Source::lfo2);
        setLine (*p, lifetime::Destination::level, 1.0f, 0.2f, true);
        check (p->savePreset (file), "a preset file can be written");
        check (p->getPresetName() == "SillageTest", "saving names the preset after its file");
    }

    auto q = makeProcessor();
    check (q->loadPreset (file), "a preset file can be read back");
    check (std::abs (getParam (*q, params::id::mix) - 72.0f) < 0.01f
               && getParam (*q, params::id::wakeMode) >= 0.5f
               && (int) getParam (*q, params::id::modSource[3]) == (int) mod::Source::lfo2,
           "preset parameters restore");
    check (std::abs (q->getCurves().curves[(size_t) lifetime::Destination::level].evaluate (1.0f) - 0.2f) < 1.0e-4f
               && getParam (*q, params::id::curveEnable[(size_t) lifetime::Destination::level]) >= 0.5f,
           "preset curves restore");
    check (q->getPresetName() == "SillageTest", "loading sets the preset name");

    q->resetToDefaults();
    check (std::abs (getParam (*q, params::id::mix) - 30.0f) < 0.01f
               && getParam (*q, params::id::wakeMode) < 0.5f
               && (int) getParam (*q, params::id::modSource[3]) == 0
               && q->getPresetName() == "Init",
           "Init returns every parameter to its default");

    file.deleteFile();
}

// ---- Two-tier UI: quick-start Types --------------------------------------------

// A Type is a tuned full starting point: it must leave the user's Mix, Output,
// Freeze and Wake mode alone, name the preset, and hand over something that
// makes a tail on its own.
void testTypesGiveUsableStartingPoints()
{
    for (int t = 0; t < types::kNumTypes; ++t)
    {
        const auto type = (types::Type) t;
        const auto name = juce::String (types::kTypeNames[(size_t) t]);

        auto p = makeProcessor();
        setParam (*p, params::id::mix, 64.0f);
        setParam (*p, params::id::output, -3.0f);
        setParam (*p, params::id::wakeMode, 1.0f);
        setParam (*p, params::id::chokeOn, 1.0f); // something a Type must reset
        types::apply (*p, type);

        const bool untouched = std::abs (getParam (*p, params::id::mix) - 64.0f) < 0.01f
                            && std::abs (getParam (*p, params::id::output) + 3.0f) < 0.01f
                            && getParam (*p, params::id::wakeMode) >= 0.5f
                            && getParam (*p, params::id::freeze) < 0.5f;
        check (untouched, (name + ": leaves Mix, Output, Freeze and Wake alone").toRawUTF8());
        check (getParam (*p, params::id::chokeOn) < 0.5f, (name + ": resets what it does not set").toRawUTF8());
        check (p->getPresetName() == name, (name + ": names the preset after itself").toRawUTF8());

        setParam (*p, params::id::mix, 100.0f);
        setParam (*p, params::id::wakeMode, 0.0f);
        Audio out;
        render (*p, 3.0, toneThenSilence (220.0, 1.0), out);
        check (allFinite (out) && peakSeconds (out, 0.0, 3.0) < 8.0f, (name + ": renders finite, bounded audio").toRawUTF8());
        // Room is over within its 250 ms Decay; the others get the full window.
        const auto tailEnd = type == types::Type::room ? 1.4 : 3.0;
        check (peakSeconds (out, 1.02, tailEnd) > 1.0e-3f, (name + ": a tone leaves a tail behind").toRawUTF8());
    }

    auto delay = makeProcessor();
    types::apply (*delay, types::Type::delay);
    check (getParam (*delay, params::id::spread) < 0.5f, "the Delay type sits at Spread 0");

    auto reverb = makeProcessor();
    types::apply (*reverb, types::Type::reverb);
    check (getParam (*reverb, params::id::spread) > 99.5f, "the Reverb type sits at Spread 100");
}

// ---- Decay and the stage switches -----------------------------------------------

// Decay is an RT60 on the audio's age: whatever Feedback says, a tail ends at
// Decay. At Off, Feedback alone decides (the behaviour every test above relies on).
void testDecayEndsTheTail()
{
    const auto tailAt = [] (float decayMs)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::decay, decayMs);

        Audio out;
        render (*p, 1.5, toneThenSilence (220.0, 0.3), out);
        return peakSeconds (out, 1.0, 1.5);
    };

    check (tailAt (params::kDecayOffMs) > 1.0e-2f, "Decay Off: a 95 % tail is still loud at 1 s");
    check (tailAt (300.0f) < 1.0e-3f, "Decay 300 ms ends the same tail well before 1 s");
    check (params::decayIsOff (params::kDecayOffMs) && ! params::decayIsOff (29000.0f),
           "only the top of the Decay range reads Off");
}

// Decay also bounds how far back Spread may read, so a short Decay at Spread
// 100 is a room around the last few hundred milliseconds, not a smear across
// the whole buffer. That is what makes the delay-to-reverb slider continuous.
void testDecayLimitsSpreadReach()
{
    const auto oldAudioAt = [] (float decayMs)
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 50.0f);
        setParam (*p, params::id::spread, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::decay, decayMs);

        Audio out;
        render (*p, 2.5, toneThenSilence (220.0, 0.5), out);
        return peakSeconds (out, 1.5, 2.5);
    };

    check (oldAudioAt (params::kDecayOffMs) > 1.0e-3f, "Decay Off: Spread 100 keeps playing the tone a second later");
    check (oldAudioAt (200.0f) < 1.0e-4f, "Decay 200 ms: Spread 100 no longer reaches audio that old");
}

// The Room type: 100 %-wet drums keep their hit and the room is gone within
// half a second.
void testRoomKeepsTheHit()
{
    auto p = makeProcessor();
    types::apply (*p, types::Type::room);
    setParam (*p, params::id::mix, 100.0f);

    // A 20 ms burst at 0.5 s: a drum hit, not a single sample, so 15 ms grains
    // have something to land on.
    Audio out;
    render (*p, 2.0, [] (int64_t i, int)
    {
        const auto t = (double) i / kSampleRate;
        return t >= 0.5 && t < 0.52 ? sine (i, 1000.0) : 0.0f;
    }, out);

    const auto hit  = peakSeconds (out, 0.5, 0.75);
    const auto room = peakSeconds (out, 0.55, 0.8);
    const auto gone = peakSeconds (out, 1.0, 2.0);
    std::printf ("       room: hit %.4f  tail %.4f  at 1 s %.6f\n", hit, room, gone);

    check (allFinite (out), "Room renders finite audio");
    check (hit > 0.02f, "Room: the hit comes straight through the wet path");
    check (room > 1.0e-4f, "Room: a short tail follows the hit");
    check (gone < hit * 0.01f, "Room: the tail is gone half a second later");
}

// Each stage switch turns its whole stage off: the stage's knobs may say
// anything and the render behaves as if they were neutral.
void testStageSwitchesSilenceTheirStage()
{
    // Transients off: a Choke that is on does not fire.
    {
        const auto tailAfterHit = [] (bool transientsOn)
        {
            auto p = makeProcessor();
            configureAsPlainDelay (*p);
            setParam (*p, params::id::time, 100.0f);
            setParam (*p, params::id::density, 60.0f);
            setParam (*p, params::id::feedback, 95.0f);
            setParam (*p, params::id::chokeOn, 1.0f);
            setParam (*p, params::id::chokeAmount, 100.0f);
            setParam (*p, params::id::chokeFade, 20.0f);
            setParam (*p, params::id::transientsOn, transientsOn ? 1.0f : 0.0f);

            Audio out;
            render (*p, 2.6, [] (int64_t i, int)
            {
                const auto t = (double) i / kSampleRate;
                if (t < 0.5)                       return sine (i, 220.0) * 0.5f;
                if (i == (int64_t) atSecond (2.0)) return 0.05f;
                return 0.0f;
            }, out);
            return std::make_pair (peakSeconds (out, 1.8, 2.0), peakSeconds (out, 2.1, 2.6));
        };

        const auto [beforeOn,  afterOn]  = tailAfterHit (true);
        const auto [beforeOff, afterOff] = tailAfterHit (false);
        check (beforeOn > 1.0e-3f && afterOn < beforeOn * 0.3f, "Transients on: the choke fires");
        check (afterOff > beforeOff * 0.5f, "Transients off: the same choke leaves the tail alone");
    }

    // Loop off: a full shimmer does not climb.
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 200.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::size, 100.0f);
        setParam (*p, params::id::feedback, 80.0f);
        setParam (*p, params::id::shimmerInterval, 6.0f);
        setParam (*p, params::id::shimmerAmount, 100.0f);
        setParam (*p, params::id::loopOn, 0.0f);

        Audio out;
        render (*p, 1.5, toneThenSilence (220.0, 0.3), out);
        const auto early = dominantFrequency (out[0], atSecond (0.40), atSecond (0.65));
        const auto later = dominantFrequency (out[0], atSecond (0.80), atSecond (1.05));
        check (early > 180.0 && early < 260.0 && std::abs (later - early) < early * 0.1,
               "Loop off: a +12 shimmer at 100 % leaves the pitch where it was");
    }

    // Age off: a Level curve to zero does not end the tail.
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 100.0f);
        setParam (*p, params::id::density, 60.0f);
        setParam (*p, params::id::feedback, 95.0f);
        setParam (*p, params::id::lifetime, 1000.0f);
        setLine (*p, lifetime::Destination::level, 1.0f, 0.0f, true);
        setParam (*p, params::id::ageOn, 0.0f);

        Audio out;
        render (*p, 3.0, toneThenSilence (220.0, 0.3), out);
        check (peakSeconds (out, 2.0, 3.0) > 1.0e-2f, "Age off: an enabled Level curve to zero is ignored");
    }

    // Mod off: an LFO on Mix leaves the dry level alone.
    {
        auto p = makeProcessor();
        configureAsPlainDelay (*p);
        setParam (*p, params::id::time, 3000.0f);
        setParam (*p, params::id::mix, 50.0f);
        setParam (*p, params::id::modSource[0], (float) mod::Source::lfo1);
        setParam (*p, params::id::modDestination[0], (float) mod::Destination::mix);
        setParam (*p, params::id::modAmount[0], 100.0f);
        setParam (*p, params::id::lfoRate[0], 2.0f);
        setParam (*p, params::id::modOn, 0.0f);

        Audio out;
        render (*p, 1.2, [] (int64_t i, int) { return sine (i, 1000.0) * 0.5f; }, out);
        const auto a = peakSeconds (out, 0.23, 0.27), b = peakSeconds (out, 0.48, 0.52);
        check (a > 0.25f && b > 0.25f && std::abs (a - b) < 0.05f, "Mod off: an LFO on Mix does nothing");
    }

    // Chaos off: Chaos 100 renders exactly like Chaos 0.
    {
        const auto renderWith = [] (float chaos, bool chaosOn, Audio& out)
        {
            auto p = makeProcessor();
            configureAsPlainDelay (*p);
            setParam (*p, params::id::time, 250.0f);
            setParam (*p, params::id::chaos, chaos);
            setParam (*p, params::id::chaosOn, chaosOn ? 1.0f : 0.0f);
            render (*p, 2.0, [] (int64_t i, int) { return sine (i, 220.0) * 0.5f; }, out);
        };

        Audio calm, muted;
        renderWith (0.0f, true, calm);
        renderWith (100.0f, false, muted);

        float difference = 0.0f;
        for (size_t i = 0; i < calm[0].size(); ++i)
            difference = std::max (difference, std::abs (calm[0][i] - muted[0][i]));
        check (rms (calm[0], atSecond (1.0), atSecond (2.0)) > 0.01 && difference < 1.0e-4f,
               "Chaos off: Chaos 100 renders like Chaos 0");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testPassThroughAtFullDry();
    testStateRoundTrip();
    testSilenceStaysSilent();
    testDelayLandsAtTime();
    testSpreadReachesRecentAudio();
    testFeedbackSustainsTail();
    testLimiterKeepsLoopBounded();
    testSyncedTimeFollowsTempo();
    testPitchShiftsGrainsUpAnOctave();
    testShimmerClimbsThroughTheLoop();
    testLoopLowpassDampsTheTail();
    testGrainCapIsNeverExceeded();
    testRandomisedGrainPathRendersCleanly();
    testScaleSnapping();

    testFreezeHoldsTheBuffer();
    testPanicClearsEverything();
    testChaosMovesTheTailAndStaysBounded();
    testRandomizeCoversEverythingExceptTheExclusions();

    testOnsetDetectorFindsClicksNotSustain();
    testRetriggerStuttersTheHit();
    testDuckDipsTheWetOnAHit();
    testChokeKillsTheTail();
    testEnvelopeDrivesDensity();
    testSyncAndSwingPlaceGrainsOnTheGrid();

    testAgeTracksHowOldTheAudioIs();
    testLevelCurveKillsTheTail();
    testPitchCurveRaisesOlderAudio();
    testDegradeTiltDampsTheTail();
    testDegradeBitsAndRateChangeTheTail();
    testDegradeNoiseAddsHiss();
    testDegradeDriftRaisesThePitch();
    testReverseCurveRenders();
    testCurveStateRoundTrips();

    testRewindManualSwellsReversed();
    testRewindThresholdRefiresTheTail();
    testRewindTimerBumpsPeriodically();
    testRewindRestartStaysClean();

    testDisplacePushesTheTailDown();
    testIsolatedProtectsEarlierTails();
    testIsolatedRetriggerReadsTheHit();
    testIsolatedInstanceCap();
    testWakeSwitchKeepsTheTail();
    testIsolatedSustainedInputGetsATail();

    testModulationMaths();
    testLfoModulatesMix();
    testTransientSourceRaisesDensity();
    testAgeModRaisesOlderGrains();

    testWidthCollapsesAndWidens();
    testWetFiltersCleanTheOutput();
    testPresetFileRoundTrip();

    testTypesGiveUsableStartingPoints();

    testDecayEndsTheTail();
    testDecayLimitsSpreadReach();
    testRoomKeepsTheHit();
    testStageSwitchesSilenceTheirStage();

    std::printf (failures == 0 ? "\nAll tests passed.\n"
                               : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
