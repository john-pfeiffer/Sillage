// Headless render harness for Sillage. Runs as a plain console app (CTest).
// Each check pushes audio through the real SillageAudioProcessor and asserts
// on the rendered output — no host, no UI.

#include <cmath>
#include <cstdio>
#include <vector>

#include "PluginProcessor.h"

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

// Renders `seconds` of the given input generator through the processor,
// appending output to `out` (interleaved as separate channel vectors).
void render (SillageAudioProcessor& p, double seconds,
             const std::function<float (int64_t sample, int ch)>& gen,
             std::vector<std::vector<float>>& out)
{
    const auto totalSamples = (int64_t) (seconds * kSampleRate);
    out.resize (2);

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
    }
}

bool allFinite (const std::vector<std::vector<float>>& audio)
{
    for (const auto& ch : audio)
        for (float s : ch)
            if (! std::isfinite (s))
                return false;
    return true;
}

float peak (const std::vector<float>& ch, size_t begin, size_t end)
{
    float m = 0.0f;
    for (size_t i = begin; i < std::min (end, ch.size()); ++i)
        m = std::max (m, std::abs (ch[i]));
    return m;
}

// ---- Tests ------------------------------------------------------------------

void testPassThroughAtFullDry()
{
    auto p = makeProcessor();
    setParam (*p, params::id::mix, 0.0f);
    setParam (*p, params::id::output, 0.0f);

    std::vector<std::vector<float>> out;
    render (*p, 1.0, [] (int64_t i, int) { return std::sin (0.05 * (double) i) * 0.5f; }, out);

    // After the 20 ms smoothing settles, dry-only output must match the input.
    const auto start = (size_t) (0.1 * kSampleRate);
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

    juce::MemoryBlock state;
    p->getStateInformation (state);

    auto q = makeProcessor();
    q->setStateInformation (state.getData(), (int) state.getSize());

    const float mix = q->apvts.getRawParameterValue (params::id::mix)->load();
    const float out = q->apvts.getRawParameterValue (params::id::output)->load();
    check (std::abs (mix - 72.0f) < 0.01f && std::abs (out - (-6.0f)) < 0.01f,
           "parameter state round-trips through get/setStateInformation");
}

void testSilenceStaysSilent()
{
    auto p = makeProcessor();
    std::vector<std::vector<float>> out;
    render (*p, 2.0, [] (int64_t, int) { return 0.0f; }, out);
    check (peak (out[0], 0, out[0].size()) == 0.0f, "silence in, silence out");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testPassThroughAtFullDry();
    testStateRoundTrip();
    testSilenceStaysSilent();

    std::printf (failures == 0 ? "\nAll tests passed.\n"
                               : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
