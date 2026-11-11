// Measurement probe for Sillage: renders a fixed phrase through the real
// processor and prints what the controls do to it. It is how the levels, the
// tail lengths and Randomize's yield were tuned, and how to check them again
// after a DSP change. Development only: build with -DSILLAGE_BUILD_PROBE=ON.
//
//   SillageProbe types                       wet level and tail of Init and every Type
//   SillageProbe sweep <base>                each main knob across its range
//   SillageProbe rt60 <base> [id=value ...]  tail length over Decay x Spread x Feedback
//   SillageProbe randomize <base> <amount%> <count> <dump.tsv>
//                                            rolls, one line of measurements and parameters each
//   SillageProbe attrib <base> <count>       which parameter groups alone silence a roll
//   SillageProbe sens <base>                 how much each parameter changes the output
//   SillageProbe diff <base> "<a>" "<b>"     do these two sets of id=value changes sound different?
//   SillageProbe case <base> [id=value ...]  one setting, with its level every 250 ms
//   SillageProbe sine <base> [id=value ...]  the same on a steady 440 Hz tone
//   SillageProbe ripple                      how steady each Type holds a steady tone
//
// <base> is init or a Type's name in lower case. Add --pad as the last
// argument to measure on a sustained chord instead of the drum-and-arpeggio
// phrase. Levels are dB relative to the dry input; "change" figures are the
// mean difference, in dB, between two renders over 200 ms x octave-band cells
// (so they see level and spectrum, not fine texture), and come with the
// figure two renders of the same setting give, since the grains are random.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "LifetimeCurves.h"
#include "Modulation.h"
#include "PluginProcessor.h"
#include "Randomize.h"
#include "Types.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kInputSeconds = 4.0;

using Audio = std::vector<std::vector<float>>;

// ---- Input phrase: kick on the beat, hat on the offbeat, an arpeggio --------
std::vector<std::vector<float>> makePhrase()
{
    const auto n = (size_t) (kInputSeconds * kSampleRate);
    std::vector<std::vector<float>> in (2, std::vector<float> (n, 0.0f));
    const double notes[8] { 220.0, 261.63, 329.63, 440.0, 392.0, 329.63, 261.63, 293.66 };
    uint32_t lcg = 12345u;
    float prevNoise = 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / kSampleRate;
        double l = 0.0, r = 0.0;

        // Kick every 0.5 s.
        {
            const auto tb = std::fmod (t, 0.5);
            const auto f0 = 50.0, f1 = 150.0, tau = 0.03;
            // phase = integral of f0 + (f1-f0) exp(-t/tau)
            const auto phase = 2.0 * juce::MathConstants<double>::pi * (f0 * tb + (f1 - f0) * tau * (1.0 - std::exp (-tb / tau)));
            const auto s = 0.5 * std::sin (phase) * std::exp (-tb / 0.12);
            l += s; r += s;
        }
        // Hat on the offbeat.
        {
            lcg = lcg * 1664525u + 1013904223u;
            const auto white = (float) ((lcg >> 8) & 0xffff) / 32768.0f - 1.0f;
            const auto hp = white - prevNoise;
            prevNoise = white;
            const auto tb = std::fmod (t + 0.25, 0.5);
            const auto s = 0.12 * (double) hp * std::exp (-tb / 0.02);
            l += s; r += 0.6 * s;
        }
        // Arpeggio on eighths.
        {
            const auto idx = (int) (t / 0.25) % 8;
            const auto tb  = std::fmod (t, 0.25);
            double s = 0.0;
            for (int k = 1; k <= 6; ++k)
                s += std::sin (2.0 * juce::MathConstants<double>::pi * notes[idx] * k * tb) / k;
            s *= 0.2 * std::exp (-tb / 0.2) * std::min (1.0, tb / 0.002);
            l += 0.8 * s; r += s;
        }
        in[0][i] = (float) l;
        in[1][i] = (float) r;
    }
    return in;
}

int inputMode = 0; // 0 phrase, 1 pad, 2 steady sine

std::vector<std::vector<float>> makePad()
{
    const auto n = (size_t) (kInputSeconds * kSampleRate);
    std::vector<std::vector<float>> in (2, std::vector<float> (n, 0.0f));
    const double chord[4] { 220.0, 261.63, 329.63, 392.0 };
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = (double) i / kSampleRate;
        double l = 0.0, r = 0.0;
        for (int c = 0; c < 4; ++c)
            for (int k = 1; k <= 8; ++k)
            {
                l += std::sin (2.0 * juce::MathConstants<double>::pi * chord[c] * 0.998 * k * t) / (k * k * 0.5 + 0.5);
                r += std::sin (2.0 * juce::MathConstants<double>::pi * chord[c] * 1.002 * k * t + 0.7) / (k * k * 0.5 + 0.5);
            }
        const auto env = std::min ({ 1.0, t / 0.4, (kInputSeconds - t) / 0.3 });
        in[0][i] = (float) (0.06 * env * l);
        in[1][i] = (float) (0.06 * env * r);
    }
    return in;
}

std::vector<std::vector<float>> makeSine()
{
    const auto n = (size_t) (kInputSeconds * kSampleRate);
    std::vector<std::vector<float>> in (2, std::vector<float> (n, 0.0f));
    for (size_t i = 0; i < n; ++i)
        in[0][i] = in[1][i] = (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / kSampleRate));
    return in;
}

// 250 ms of noise, then silence: an impulse with enough energy to measure a tail.
std::vector<std::vector<float>> makeBurst()
{
    const auto n = (size_t) (kInputSeconds * kSampleRate);
    std::vector<std::vector<float>> in (2, std::vector<float> (n, 0.0f));
    uint32_t lcg = 777u;
    for (size_t i = 0; i < (size_t) (0.25 * kSampleRate); ++i)
    {
        lcg = lcg * 1664525u + 1013904223u;
        const auto white = (float) ((lcg >> 8) & 0xffff) / 32768.0f - 1.0f;
        in[0][i] = in[1][i] = 0.3f * white;
    }
    return in;
}

const std::vector<std::vector<float>>& phrase()
{
    static const auto p = inputMode == 1 ? makePad() : inputMode == 2 ? makeSine() : inputMode == 3 ? makeBurst() : makePhrase();
    return p;
}

double rmsOf (const std::vector<float>& x, double from, double to)
{
    const auto a = (size_t) (from * kSampleRate);
    const auto b = std::min (x.size(), (size_t) (to * kSampleRate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i) sum += (double) x[i] * x[i];
    return b > a ? std::sqrt (sum / (double) (b - a)) : 0.0;
}

double dryRms()
{
    static const double v = [] {
        std::vector<float> mid (phrase()[0].size());
        for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (phrase()[0][i] + phrase()[1][i]);
        return rmsOf (mid, 0.0, kInputSeconds);
    }();
    return v;
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-9)); }

std::unique_ptr<SillageAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<SillageAudioProcessor>();
    p->setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    p->prepareToPlay (kSampleRate, kBlockSize);
    return p;
}

void setPlain (SillageAudioProcessor& p, const char* id, float plain)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

float getPlain (SillageAudioProcessor& p, const juce::String& id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

Audio render (SillageAudioProcessor& p, double seconds)
{
    const auto total = (int64_t) (seconds * kSampleRate);
    Audio out (2);
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    const auto& in = phrase();

    for (int64_t pos = 0; pos < total; pos += kBlockSize)
    {
        const int n = (int) std::min ((int64_t) kBlockSize, total - pos);
        buffer.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const auto idx = (size_t) (pos + i);
                d[i] = idx < in[(size_t) ch].size() ? in[(size_t) ch][idx] : 0.0f;
            }
        }
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n);
    }
    return out;
}

// ---- Features: 200 ms blocks x 9 octave bands, mid and side, in dB ----------
struct Features
{
    std::vector<float> mid, side; // blocks * bands
    int blocks = 0;
    double wetInDb = -180, tail0Db = -180, tail1Db = -180, tail3Db = -180, tailEndDb = -180, peak = 0, t60 = 0, centroid = 0;
    bool finite = true;
};

constexpr int kBands = 9, kFftOrder = 11, kFft = 1 << kFftOrder, kFramesPerBlock = 5;
constexpr float kFloorDb = -70.0f; // relative to the dry phrase

Features analyse (const Audio& a, double seconds)
{
    Features f;
    static juce::dsp::FFT fft (kFftOrder);
    static std::vector<float> window = [] {
        std::vector<float> w ((size_t) kFft);
        for (int i = 0; i < kFft; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) kFft);
        return w;
    }();

    const auto n = a[0].size();
    std::vector<float> mid (n), side (n);
    for (size_t i = 0; i < n; ++i)
    {
        if (! std::isfinite (a[0][i]) || ! std::isfinite (a[1][i])) f.finite = false;
        mid[i]  = 0.5f * (a[0][i] + a[1][i]);
        side[i] = 0.5f * (a[0][i] - a[1][i]);
        f.peak = std::max ({ f.peak, (double) std::abs (a[0][i]), (double) std::abs (a[1][i]) });
    }

    const auto ref = dryRms();
    const auto both = [&] (double from, double to) {
        return db (std::sqrt (std::pow (rmsOf (mid, from, to), 2.0) + std::pow (rmsOf (side, from, to), 2.0)) / ref);
    };
    f.wetInDb   = both (0.5, kInputSeconds);
    f.tail0Db   = both (kInputSeconds, kInputSeconds + 0.5);
    f.tail1Db   = both (kInputSeconds + 1.0, kInputSeconds + 1.5);
    f.tail3Db   = both (kInputSeconds + 3.0, kInputSeconds + 3.5);
    f.tailEndDb = both (seconds - 1.0, seconds);

    // T60: last 100 ms window after the input stops that is above -60 dB re dry.
    {
        double last = 0.0;
        for (double t = kInputSeconds; t + 0.1 <= seconds; t += 0.1)
            if (both (t, t + 0.1) > -60.0)
                last = t + 0.1 - kInputSeconds;
        f.t60 = last;
    }

    const int frames = (int) (n / (size_t) kFft);
    f.blocks = frames / kFramesPerBlock;
    f.mid.assign ((size_t) (f.blocks * kBands), 0.0f);
    f.side.assign ((size_t) (f.blocks * kBands), 0.0f);
    std::vector<float> scratch ((size_t) kFft * 2);
    double centroidNum = 0.0, centroidDen = 0.0;

    const auto bandOf = [] (int bin) {
        const auto hz = (double) bin * kSampleRate / kFft;
        if (hz < 44.0) return -1;
        return std::min (kBands - 1, (int) std::floor (std::log2 (hz / 44.0)));
    };

    for (int which = 0; which < 2; ++which)
    {
        const auto& x = which == 0 ? mid : side;
        auto& dest = which == 0 ? f.mid : f.side;
        std::vector<double> power ((size_t) (f.blocks * kBands), 0.0);

        for (int frame = 0; frame < f.blocks * kFramesPerBlock; ++frame)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);
            for (int i = 0; i < kFft; ++i)
                scratch[(size_t) i] = x[(size_t) frame * kFft + (size_t) i] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (scratch.data());
            const auto block = frame / kFramesPerBlock;
            for (int bin = 1; bin < kFft / 2; ++bin)
            {
                const auto band = bandOf (bin);
                if (band < 0) continue;
                const auto pw = (double) scratch[(size_t) bin] * scratch[(size_t) bin];
                power[(size_t) (block * kBands + band)] += pw;
                if (which == 0 && (double) frame * kFft / kSampleRate < kInputSeconds)
                {
                    centroidNum += pw * (double) bin * kSampleRate / kFft;
                    centroidDen += pw;
                }
            }
        }

        // Normalise so a full-band signal at the dry RMS reads about 0 dB.
        const auto norm = ref * ref * (double) kFft * (double) kFft * 0.375 * 0.5 * (double) kFramesPerBlock;
        for (size_t i = 0; i < power.size(); ++i)
            dest[i] = std::max (kFloorDb, (float) (10.0 * std::log10 (std::max (power[i] / norm, 1.0e-12))));
    }
    f.centroid = centroidDen > 0.0 ? centroidNum / centroidDen : 0.0;
    return f;
}

// Mean absolute dB difference over the cells where either render is audible.
double distance (const Features& a, const Features& b)
{
    double sum = 0.0; int count = 0;
    const auto add = [&] (const std::vector<float>& x, const std::vector<float>& y)
    {
        for (size_t i = 0; i < std::min (x.size(), y.size()); ++i)
            if (x[i] > kFloorDb + 20.0f || y[i] > kFloorDb + 20.0f)
            {
                sum += std::abs ((double) x[i] - (double) y[i]);
                ++count;
            }
    };
    add (a.mid, b.mid);
    add (a.side, b.side);
    return count > 0 ? sum / count : 0.0;
}

// ---- Bases -------------------------------------------------------------------
bool applyBase (SillageAudioProcessor& p, const std::string& base)
{
    p.resetToDefaults();
    if (base == "init") return true;
    for (int t = 0; t < types::kNumTypes; ++t)
        if (juce::String (types::kTypeNames[(size_t) t]).toLowerCase() == juce::String (base))
        {
            types::apply (p, (types::Type) t);
            return true;
        }
    return false;
}

void printMetricsHeader()
{
    std::printf ("%-22s %8s %8s %8s %8s %8s %6s %6s %8s\n", "case", "wetIn", "tail0", "tail+1s", "tail+3s", "tailEnd", "t60", "peak", "centroid");
}

void printMetrics (const char* name, const Features& f)
{
    std::printf ("%-22s %8.1f %8.1f %8.1f %8.1f %8.1f %6.1f %6.2f %8.0f\n", name, f.wetInDb, f.tail0Db, f.tail1Db, f.tail3Db, f.tailEndDb, f.t60, f.peak, f.centroid);
}

// ---- types -------------------------------------------------------------------
int runTypes()
{
    const char* bases[] { "init", "delay", "room", "reverb", "shimmer", "wash", "granular", "shards", "cascade", "undertow", "rot", "stutter", "swarm" };
    std::printf ("Wet only (Mix 100), dB relative to the dry phrase RMS. 4 s of input then 6 s of tail.\n");
    printMetricsHeader();
    std::vector<Features> feats;
    for (auto* base : bases)
    {
        auto p = makeProcessor();
        applyBase (*p, base);
        setPlain (*p, params::id::mix, 100.0f);
        auto f = analyse (render (*p, 10.0), 10.0);
        printMetrics (base, f);
        feats.push_back (f);
    }
    std::printf ("\nPairwise distance between bases (mean |dB| over audible time/frequency cells):\n%-10s", "");
    for (auto* base : bases) std::printf ("%9s", base);
    std::printf ("\n");
    for (size_t i = 0; i < feats.size(); ++i)
    {
        std::printf ("%-10s", bases[i]);
        for (size_t j = 0; j < feats.size(); ++j) std::printf ("%9.1f", distance (feats[i], feats[j]));
        std::printf ("\n");
    }
    return 0;
}

// ---- randomize ---------------------------------------------------------------
int runRandomize (const std::string& base, float amountPercent, int count, const char* dumpPath)
{
    FILE* dump = std::fopen (dumpPath, "w");

    // Reference render of the base, for "how far did the roll move".
    Features baseFeatures;
    {
        auto p = makeProcessor();
        applyBase (*p, base);
        setPlain (*p, params::id::mix, 100.0f);
        baseFeatures = analyse (render (*p, 10.0), 10.0);
    }

    bool headerDone = false;
    for (int i = 0; i < count; ++i)
    {
        auto p = makeProcessor();
        applyBase (*p, base);

        // Exactly what the Randomize button does.
        p->randomizeAll (amountPercent * 0.01f);

        // Measurement only: listen to the wet alone, and note (then neutralise)
        // any live mod slot that would be moving Mix.
        int mixSlots = 0;
        const auto modOn = getPlain (*p, params::id::modOn) >= 0.5f;
        for (size_t s = 0; s < params::id::modSource.size(); ++s)
        {
            const auto src  = (int) getPlain (*p, params::id::modSource[s]);
            const auto dest = (int) getPlain (*p, params::id::modDestination[s]);
            const auto amt  = getPlain (*p, params::id::modAmount[s]);
            if (modOn && src != 0 && dest == (int) mod::Destination::mix && std::abs (amt) > 1.0f)
            {
                ++mixSlots;
                setPlain (*p, params::id::modAmount[s], 0.0f);
            }
        }
        setPlain (*p, params::id::mix, 100.0f);
        setPlain (*p, params::id::output, 0.0f);

        const auto f = analyse (render (*p, 10.0), 10.0);
        const auto d = distance (f, baseFeatures);

        if (! headerDone)
        {
            std::fprintf (dump, "roll\twetIn\ttail0\ttail1\ttail3\ttailEnd\tt60\tpeak\tcentroid\tdist\tmixSlots\tfinite");
            for (auto* parameter : p->getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                    std::fprintf (dump, "\t%s", withId->paramID.toRawUTF8());
            std::fprintf (dump, "\n");
            headerDone = true;
        }
        std::fprintf (dump, "%d\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f\t%.0f\t%.2f\t%d\t%d", i, f.wetInDb, f.tail0Db, f.tail1Db, f.tail3Db, f.tailEndDb, f.t60, f.peak, f.centroid, d, mixSlots, f.finite ? 1 : 0);
        for (auto* parameter : p->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
                std::fprintf (dump, "\t%.4f", getPlain (*p, withId->paramID));
        std::fprintf (dump, "\n");
        std::fflush (dump);
    }
    std::fclose (dump);
    return 0;
}

// ---- sens --------------------------------------------------------------------
int runSens (const std::string& base)
{
    constexpr double seconds = 8.0;
    const auto renderWith = [&] (const juce::String& id, float normalised) -> Features
    {
        auto p = makeProcessor();
        applyBase (*p, base);
        setPlain (*p, params::id::mix, 100.0f);
        if (id.isNotEmpty())
            p->apvts.getParameter (id)->setValueNotifyingHost (normalised);
        return analyse (render (*p, seconds), seconds);
    };

    // Noise floor: the same settings rendered three times (the grain RNG is unseeded).
    const auto a = renderWith ({}, 0.0f), b = renderWith ({}, 0.0f), c = renderWith ({}, 0.0f);
    const auto floorDistance = std::max ({ distance (a, b), distance (a, c), distance (b, c) });
    std::printf ("base\t%s\tnoiseFloor\t%.2f\n", base.c_str(), floorDistance);

    auto probe = makeProcessor();
    applyBase (*probe, base);
    for (auto* parameter : probe->getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        if (withId == nullptr) continue;
        const auto id = withId->paramID;
        if (id == "mix" || id == "output" || id == "panic" || id == "rewindManual" || id == "fallbackBpm"
            || id == "randomizeAmount" || id.startsWith ("mod") || id.startsWith ("lfo"))
            continue;

        const auto current = parameter->getValue();
        double range = 0.0, lowVsBase = 0.0, highVsBase = 0.0;
        const auto isBool   = dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr;
        const auto isChoice = dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr;

        if (isBool)
        {
            const auto other = renderWith (id, current >= 0.5f ? 0.0f : 1.0f);
            range = lowVsBase = highVsBase = distance (other, a);
        }
        else
        {
            const auto lo = renderWith (id, isChoice ? 0.0f : 0.1f);
            const auto hi = renderWith (id, isChoice ? 1.0f : 0.9f);
            range      = distance (lo, hi);
            lowVsBase  = distance (lo, a);
            highVsBase = distance (hi, a);
        }
        std::printf ("%s\t%s\t%.2f\t%.2f\t%.2f\t%.3f\n", base.c_str(), id.toRawUTF8(), range, lowVsBase, highVsBase, current);
        std::fflush (stdout);
    }
    return 0;
}
// ---- sweep: the main-page knobs across their range ------------------------------
int runSweep (const std::string& base)
{
    const char* ids[] { params::id::time, params::id::decay, params::id::spread, params::id::feedback,
                        params::id::density, params::id::size, params::id::pitch,
                        params::id::fbLowpass, params::id::shimmerAmount, params::id::chaos, params::id::width };
    std::printf ("base %s: wetIn dB re dry / t60 s at knob positions 0, 25, 50, 75, 100 %%; then distance between neighbours\n", base.c_str());
    for (auto* id : ids)
    {
        std::vector<Features> feats;
        std::printf ("%-14s", id);
        for (float norm : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto p = makeProcessor();
            applyBase (*p, base);
            setPlain (*p, params::id::mix, 100.0f);
            p->apvts.getParameter (id)->setValueNotifyingHost (norm);
            feats.push_back (analyse (render (*p, 10.0), 10.0));
            std::printf ("  %6.1f/%3.1f", feats.back().wetInDb, feats.back().t60);
        }
        std::printf ("   |");
        for (size_t i = 1; i < feats.size(); ++i)
            std::printf (" %5.1f", distance (feats[i - 1], feats[i]));
        std::printf ("\n");
    }
    return 0;
}

// ---- ripple: how steady is the wet level on a steady sine? ---------------------
int runRipple()
{
    const char* bases[] { "init", "delay", "room", "reverb", "shimmer", "wash", "granular" };
    std::printf ("Steady 440 Hz sine in, wet only. Level of the wet 5 ms envelope between 2 s and 4 s.\n");
    std::printf ("%-10s %8s %8s %8s %10s\n", "base", "p5 dB", "p50 dB", "p95 dB", "p95-p5 dB");
    for (auto* base : bases)
    {
        auto p = makeProcessor();
        applyBase (*p, base);
        setPlain (*p, params::id::mix, 100.0f);
        const auto out = render (*p, 4.0);
        std::vector<double> env;
        const auto step = (size_t) (0.005 * kSampleRate);
        for (size_t i = (size_t) (2.0 * kSampleRate); i + step <= out[0].size(); i += step)
        {
            double sum = 0.0;
            for (size_t k = 0; k < step; ++k) sum += (double) out[0][i + k] * out[0][i + k];
            env.push_back (db (std::sqrt (sum / (double) step) / (0.3 / std::sqrt (2.0))));
        }
        std::sort (env.begin(), env.end());
        const auto at = [&] (double q) { return env[(size_t) (q * (double) (env.size() - 1))]; };
        std::printf ("%-10s %8.1f %8.1f %8.1f %10.1f\n", base, at (0.05), at (0.5), at (0.95), at (0.95) - at (0.05));
    }
    return 0;
}
// ---- attrib: which parameter groups, on their own, kill a Randomize roll? -------
struct Fix { const char* name; std::function<void (SillageAudioProcessor&)> apply; };

int runAttrib (const std::string& base, int count)
{
    namespace id = params::id;
    const std::vector<Fix> fixes {
        { "wet HP/LP (Output tab)", [] (auto& p) { setPlain (p, id::wetHighpass, 20.0f); setPlain (p, id::wetLowpass, 20000.0f); } },
        { "loop HP/LP/Res",         [] (auto& p) { setPlain (p, id::fbHighpass, 20.0f); setPlain (p, id::fbLowpass, 20000.0f); setPlain (p, id::fbResonance, 0.0f); } },
        { "lifetime curves",        [] (auto& p) { for (auto* c : id::curveEnable) setPlain (p, c, 0.0f); } },
        { "decay (set Off)",        [] (auto& p) { setPlain (p, id::decay, params::kDecayOffMs); } },
        { "duck/choke/displace/env", [] (auto& p) { setPlain (p, id::duckOn, 0.0f); setPlain (p, id::chokeOn, 0.0f); setPlain (p, id::displace, 0.0f);
                                                     setPlain (p, id::envDensity, 0.0f); setPlain (p, id::envSpread, 0.0f); } },
        { "sync + time sync",       [] (auto& p) { setPlain (p, id::sync, 0.0f); setPlain (p, id::timeSync, 0.0f); } },
        { "density (floor 20/s)",   [] (auto& p) { setPlain (p, id::density, std::max (20.0f, getPlain (p, id::density))); } },
        { "size (cap 60 ms)",       [] (auto& p) { setPlain (p, id::size, std::min (60.0f, getPlain (p, id::size))); } },
        { "mod slots",              [] (auto& p) { setPlain (p, id::modOn, 0.0f); } },
        { "pitch/spread/reverse",   [] (auto& p) { setPlain (p, id::pitch, 0.0f); setPlain (p, id::pitchSpread, 0.0f); setPlain (p, id::reverse, 0.0f); } },
        { "degrade",                [] (auto& p) { for (auto* d : { id::degradeBits, id::degradeRate, id::degradeNoise, id::degradeTilt, id::degradeDrift }) setPlain (p, d, 0.0f); } },
        { "rewind",                 [] (auto& p) { setPlain (p, id::rewindOn, 0.0f); } },
        { "time (floor 100 ms)",    [] (auto& p) { setPlain (p, id::time, std::max (100.0f, getPlain (p, id::time))); } },
    };

    const auto isDead = [] (SillageAudioProcessor& p)
    {
        setPlain (p, id::mix, 100.0f);
        setPlain (p, id::output, 0.0f);
        const auto f = analyse (render (p, 6.0), 6.0);
        return f.wetInDb < -30.0 && std::max (f.tail0Db, f.tail1Db) < -30.0;
    };

    int deadAsIs = 0, deadAllFixed = 0;
    std::vector<int> deadWithOnly (fixes.size(), 0), deadWithoutFix (fixes.size(), 0);

    for (int i = 0; i < count; ++i)
    {
        auto rolled = makeProcessor();
        applyBase (*rolled, base);
        rolled->randomizeAll (1.0f);
        for (size_t s = 0; s < id::modSource.size(); ++s)
            if ((int) getPlain (*rolled, id::modDestination[s]) == (int) mod::Destination::mix)
                setPlain (*rolled, id::modAmount[s], 0.0f);

        juce::MemoryBlock state;
        rolled->getStateInformation (state);
        const auto clone = [&] {
            auto p = makeProcessor();
            p->setStateInformation (state.getData(), (int) state.getSize());
            return p;
        };

        { auto p = clone(); if (isDead (*p)) ++deadAsIs; }
        { auto p = clone(); for (auto& fix : fixes) fix.apply (*p); if (isDead (*p)) ++deadAllFixed; }

        for (size_t k = 0; k < fixes.size(); ++k)
        {
            // Everything neutralised except group k: is group k alone enough to kill it?
            auto p = clone();
            for (size_t j = 0; j < fixes.size(); ++j)
                if (j != k) fixes[j].apply (*p);
            if (isDead (*p)) ++deadWithOnly[k];

            // Only group k neutralised: does fixing it alone revive the roll?
            auto q = clone();
            fixes[k].apply (*q);
            if (isDead (*q)) ++deadWithoutFix[k];
        }
    }

    std::printf ("base %s, %d full rolls. dead as rolled: %d (%.0f%%). dead with every group below neutralised: %d (%.0f%%)\n",
                 base.c_str(), count, deadAsIs, 100.0 * deadAsIs / count, deadAllFixed, 100.0 * deadAllFixed / count);
    std::printf ("%-28s %28s %30s\n", "group", "kills on its own (others off)", "still dead if only it is fixed");
    for (size_t k = 0; k < fixes.size(); ++k)
        std::printf ("%-28s %26.0f%% %28.0f%%\n", fixes[k].name, 100.0 * deadWithOnly[k] / count, 100.0 * deadWithoutFix[k] / count);
    return 0;
}

// ---- diff: does changing these settings audibly change the base? ---------------
// Features are averaged over several renders so random grain placement cancels.
Features averaged (const std::string& base, const juce::String& overrides, int renders)
{
    Features sum;
    for (int k = 0; k < renders; ++k)
    {
        auto p = makeProcessor();
        applyBase (*p, base);
        setPlain (*p, params::id::mix, 100.0f);
        for (const auto& arg : juce::StringArray::fromTokens (overrides, " ", ""))
            if (arg.containsChar ('='))
                setPlain (*p, arg.upToFirstOccurrenceOf ("=", false, false).toRawUTF8(),
                          arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        const auto f = analyse (render (*p, 8.0), 8.0);
        if (k == 0) { sum = f; continue; }
        for (size_t i = 0; i < sum.mid.size(); ++i)  sum.mid[i]  += f.mid[i];
        for (size_t i = 0; i < sum.side.size(); ++i) sum.side[i] += f.side[i];
        sum.wetInDb += f.wetInDb; sum.t60 += f.t60; sum.centroid += f.centroid; sum.tail1Db += f.tail1Db;
    }
    for (auto& v : sum.mid)  v /= (float) renders;
    for (auto& v : sum.side) v /= (float) renders;
    sum.wetInDb /= renders; sum.t60 /= renders; sum.centroid /= renders; sum.tail1Db /= renders;
    return sum;
}

int runDiff (char** argv)
{
    const std::string base = argv[2];
    const juce::String a = argv[3], b = argv[4];
    const int renders = 8;
    const auto fa = averaged (base, a, renders), fa2 = averaged (base, a, renders), fb = averaged (base, b, renders);
    std::printf ("%-9s [%s] vs [%s]: change %.1f (floor %.1f)  wet %.1f -> %.1f dB  t60 %.1f -> %.1f s  centroid %.0f -> %.0f Hz\n",
                 base.c_str(), a.toRawUTF8(), b.toRawUTF8(), distance (fa, fb), distance (fa, fa2),
                 fa.wetInDb, fb.wetInDb, fa.t60, fb.t60, fa.centroid, fb.centroid);
    return 0;
}

// ---- rt60: Schroeder decay time of a short burst --------------------------------
double schroederRt60 (const Audio& out, double burstEnd)
{
    const auto start = (size_t) (burstEnd * kSampleRate);
    const auto n = out[0].size();
    std::vector<double> energy (n - start + 1, 0.0);
    for (size_t i = n; i-- > start;)
        energy[i - start] = energy[i - start + 1] + (double) out[0][i] * out[0][i] + (double) out[1][i] * out[1][i];
    if (energy[0] <= 1.0e-18) return 0.0;
    double t5 = -1.0, t35 = -1.0;
    for (size_t i = 0; i < energy.size(); ++i)
    {
        const auto level = 10.0 * std::log10 (std::max (energy[i] / energy[0], 1.0e-30));
        if (t5 < 0.0 && level <= -5.0)   t5 = (double) i / kSampleRate;
        if (t35 < 0.0 && level <= -35.0) { t35 = (double) i / kSampleRate; break; }
    }
    return (t5 >= 0.0 && t35 >= 0.0) ? 2.0 * (t35 - t5) : 99.0;
}

int runRt60 (int argc, char** argv)
{
    inputMode = 3;
    std::printf ("RT60 in seconds (Schroeder, -5 to -35 dB, doubled) of a 250 ms noise burst. overrides:");
    for (int i = 3; i < argc; ++i) std::printf (" %s", argv[i]);
    std::printf ("\n%-8s %-8s", "decay", "spread");
    for (float fb : { 0.0f, 50.0f, 80.0f, 100.0f, 110.0f }) std::printf ("   fb %3.0f", fb);
    std::printf ("\n");
    for (float decayMs : { 1000.0f, 3000.0f })
        for (float spread : { 0.0f, 25.0f, 50.0f, 100.0f })
        {
            std::printf ("%-8.0f %-8.0f", decayMs, spread);
            for (float fb : { 0.0f, 50.0f, 80.0f, 100.0f, 110.0f })
            {
                double sum = 0.0; const int renders = 3;
                for (int k = 0; k < renders; ++k)
                {
                    auto p = makeProcessor();
                    applyBase (*p, argv[2]);
                    setPlain (*p, params::id::mix, 100.0f);
                    for (int i = 3; i < argc; ++i)
                    {
                        const juce::String arg (argv[i]);
                        setPlain (*p, arg.upToFirstOccurrenceOf ("=", false, false).toRawUTF8(), arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
                    }
                    setPlain (*p, params::id::decay, decayMs);
                    setPlain (*p, params::id::spread, spread);
                    setPlain (*p, params::id::feedback, fb);
                    sum += schroederRt60 (render (*p, 10.0), 0.25);
                }
                std::printf ("   %6.2f", sum / renders);
            }
            std::printf ("\n");
        }
    return 0;
}

// ---- case: a base plus id=plainValue overrides, with a coarse level trace -------
int runCase (int argc, char** argv)
{
    auto p = makeProcessor();
    applyBase (*p, argv[2]);
    setPlain (*p, params::id::mix, 100.0f);
    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        setPlain (*p, arg.upToFirstOccurrenceOf ("=", false, false).toRawUTF8(),
                  arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    }
    const auto out = render (*p, 10.0);
    const auto f = analyse (out, 10.0);
    printMetricsHeader();
    printMetrics (argv[2], f);
    std::printf ("level per 250 ms (dB re dry):");
    std::vector<float> mid (out[0].size());
    for (size_t i = 0; i < mid.size(); ++i) mid[i] = 0.5f * (out[0][i] + out[1][i]);
    for (double t = 0.0; t < 10.0; t += 0.25)
        std::printf (" %.0f", std::max (-99.0, db (rmsOf (mid, t, t + 0.25) / dryRms())));
    std::printf ("\n");
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc >= 2 && std::strcmp (argv[argc - 1], "--pad") == 0) { inputMode = 1; --argc; }
    if (argc >= 2 && std::strcmp (argv[1], "ripple") == 0) { inputMode = 2; return runRipple(); }
    if (argc >= 4 && std::strcmp (argv[1], "attrib") == 0) return runAttrib (argv[2], std::atoi (argv[3]));
    if (argc >= 3 && std::strcmp (argv[1], "rt60") == 0) return runRt60 (argc, argv);
    if (argc >= 5 && std::strcmp (argv[1], "diff") == 0) return runDiff (argv);
    if (argc >= 3 && std::strcmp (argv[1], "sine") == 0) { inputMode = 2; return runCase (argc, argv); }
    if (argc >= 3 && std::strcmp (argv[1], "case") == 0) return runCase (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "sweep") == 0) return runSweep (argv[2]);
    if (argc >= 2 && std::strcmp (argv[1], "types") == 0)
        return runTypes();
    if (argc >= 6 && std::strcmp (argv[1], "randomize") == 0)
        return runRandomize (argv[2], (float) std::atof (argv[3]), std::atoi (argv[4]), argv[5]);
    if (argc >= 3 && std::strcmp (argv[1], "sens") == 0)
        return runSens (argv[2]);
    std::printf ("usage: SillageProbe types | sweep <base> | rt60 <base> | randomize <base> <amount%%> <count> <dump> |\n"
                 "       attrib <base> <count> | sens <base> | diff <base> \"<a>\" \"<b>\" | case <base> | sine <base> | ripple\n");
    return 2;
}
