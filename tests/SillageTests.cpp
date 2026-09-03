// Headless render harness for Sillage. Runs as a plain console app (CTest).
// Each check pushes audio through the real SillageAudioProcessor and asserts
// on the rendered output — no host, no UI.

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "PluginProcessor.h"
#include "Scales.h"

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

// Puts the engine in a plain, fully-wet delay configuration so a test only has
// to change the one thing it is actually about.
void configureAsPlainDelay (SillageAudioProcessor& p)
{
    setParam (p, params::id::mix, 100.0f);
    setParam (p, params::id::output, 0.0f);
    setParam (p, params::id::feedback, 0.0f);
    setParam (p, params::id::spread, 0.0f);
    setParam (p, params::id::density, 40.0f);
    setParam (p, params::id::size, 80.0f);
    setParam (p, params::id::pitch, 0.0f);
    setParam (p, params::id::pitchFine, 0.0f);
    setParam (p, params::id::pitchSpread, 0.0f);
    setParam (p, params::id::panSpread, 0.0f);
    setParam (p, params::id::reverse, 0.0f);
    setParam (p, params::id::quantize, 0.0f);
    setParam (p, params::id::shimmerAmount, 0.0f);
    setParam (p, params::id::diffuse, 0.0f);
    setParam (p, params::id::drive, 0.0f);
}

// Renders `seconds` of the given input generator through the processor,
// appending output to `out` (one vector per channel).
void render (SillageAudioProcessor& p, double seconds,
             const std::function<float (int64_t sample, int ch)>& gen,
             Audio& out,
             const std::function<void()>& afterBlock = {})
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
        pos += n;

        if (afterBlock)
            afterBlock();
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

float sine (int64_t sample, double hz)
{
    return (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) sample / kSampleRate);
}

// ---- Tests ------------------------------------------------------------------

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

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());

    const auto mix     = q->apvts.getRawParameterValue (params::id::mix)->load();
    const auto out     = q->apvts.getRawParameterValue (params::id::output)->load();
    const auto density = q->apvts.getRawParameterValue (params::id::density)->load();
    const auto shimmer = q->apvts.getRawParameterValue (params::id::shimmerAmount)->load();

    check (std::abs (mix - 72.0f) < 0.01f && std::abs (out - (-6.0f)) < 0.01f
               && std::abs (density - 123.0f) < 0.5f && std::abs (shimmer - 44.0f) < 0.01f,
           "parameter state round-trips through get/setStateInformation");
}

void testSilenceStaysSilent()
{
    auto p = makeProcessor();
    Audio out;
    render (*p, 2.0, [] (int64_t, int) { return 0.0f; }, out);
    check (peak (out, 0, out[0].size()) < 1.0e-12f, "silence in, silence out");
}

// Spread 0 means every grain reads exactly Time behind the write head, so an
// impulse comes back as a single echo at exactly Time — a delay, not a smear.
void testDelayLandsAtTime()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::time, 250.0f);

    Audio out;
    render (*p, 1.0, [] (int64_t i, int) { return i == 0 ? 1.0f : 0.0f; }, out);

    const auto echo   = peakSeconds (out, 0.245, 0.265);
    const auto before = peakSeconds (out, 0.010, 0.230);

    check (echo > 0.1f, "spread=0 puts an echo at Time");
    check (before < 1.0e-4f, "spread=0 leaves everything before Time silent");
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

    const auto narrow = renderWithSpread (0.0f);
    const auto wide   = renderWithSpread (100.0f);

    check (narrow < 1.0e-5f, "spread=0 reads only at Time (silent before the buffer fills)");
    check (wide > 0.01f, "spread=100 reaches audio far from Time");
}

// Feedback writes the grain sum back into the buffer, so the tail outlives the
// input. Without it the tail must stop one Time after the input does.
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
        render (*p, 3.0, [] (int64_t i, int)
        {
            return i < (int64_t) (0.2 * kSampleRate) ? sine (i, 330.0) * 0.5f : 0.0f;
        }, out);

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

// With no playhead the processor falls back to the BPM parameter, so a synced
// quarter note at 120 BPM has to land at exactly 500 ms.
void testSyncedTimeFollowsTempo()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::fallbackBpm, 120.0f);
    setParam (*p, params::id::timeSync, 1.0f);
    setParam (*p, params::id::timeDivision, 13.0f); // 1/4

    Audio out;
    render (*p, 1.0, [] (int64_t i, int) { return i == 0 ? 1.0f : 0.0f; }, out);

    check (peakSeconds (out, 0.495, 0.515) > 0.1f, "synced 1/4 at 120 BPM echoes at 500 ms");
    check (peakSeconds (out, 0.010, 0.480) < 1.0e-4f, "synced echo arrives no earlier");
}

// Per-grain Pitch resamples the buffer read, so +12 st has to come back an
// octave up.
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

    const auto unity = pitchOf (0.0f);
    const auto up    = pitchOf (12.0f);

    check (std::abs (unity - 220.0) < 12.0, "pitch=0 preserves the input pitch");
    check (std::abs (up - 440.0) < 25.0, "pitch=+12 returns the grain an octave up");
}

// The loop shimmer compounds: every pass through the feedback path shifts the
// tail again, so +12 keeps climbing rather than settling an octave up.
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
    render (*p, 1.5, [] (int64_t i, int)
    {
        return i < (int64_t) (0.3 * kSampleRate) ? sine (i, 220.0) * 0.5f : 0.0f;
    }, out);

    const auto early = dominantFrequency (out[0], atSecond (0.40), atSecond (0.65));
    const auto later = dominantFrequency (out[0], atSecond (0.80), atSecond (1.05));

    check (early > 100.0 && later > 100.0, "shimmer tail keeps a measurable pitch");
    check (later > early * 1.5, "shimmer compounds upward pass after pass");
}

// The loop filters sit inside the feedback path, so they shape what survives
// each pass rather than just colouring the output once.
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
        render (*p, 2.5, [] (int64_t i, int)
        {
            return i < (int64_t) (0.3 * kSampleRate) ? sine (i, 4000.0) * 0.5f : 0.0f;
        }, out);

        return peakSeconds (out, 2.0, 2.5);
    };

    const auto open   = tailWithLowpass (20000.0f);
    const auto closed = tailWithLowpass (400.0f);

    check (open > 1.0e-3f, "an open loop LP lets a bright tail ring on");
    check (closed < open * 0.25f, "closing the loop LP damps the tail every pass");
}

// Grain count is a hard compile-time cap, not a soft target.
void testGrainCapIsNeverExceeded()
{
    auto p = makeProcessor();
    configureAsPlainDelay (*p);
    setParam (*p, params::id::density, 500.0f);
    setParam (*p, params::id::size, 2000.0f);
    setParam (*p, params::id::spread, 100.0f);

    int highWater = 0;
    Audio out;
    render (*p, 3.0,
            [] (int64_t i, int) { return sine (i, 180.0) * 0.5f; },
            out,
            [&p, &highWater] { highWater = std::max (highWater, p->getActiveGrainCount()); });

    check (highWater <= kMaxGrains, "active grains never exceed kMaxGrains");
    check (highWater > kMaxGrains / 2, "max density actually loads the grain pool");
    check (allFinite (out), "grain stealing under overload stays finite");
}

// Reverse, pan spread and quantised pitch spread all resolve per grain; this is
// the smoke test that the whole randomised path renders sane audio.
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

} // namespace

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

    std::printf (failures == 0 ? "\nAll tests passed.\n"
                               : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
