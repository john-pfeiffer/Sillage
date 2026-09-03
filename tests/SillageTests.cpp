// Headless render harness for Sillage. Runs as a plain console app (CTest).
// Each check pushes audio through the real SillageAudioProcessor and asserts
// on the rendered output — no host, no UI.

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

#include "OnsetDetector.h"
#include "PluginProcessor.h"
#include "Randomize.h"
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

    check (exclusionsHeld, "randomize never touches Mix, Output, Freeze, Panic, Fallback BPM or itself");
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

    std::printf (failures == 0 ? "\nAll tests passed.\n"
                               : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
