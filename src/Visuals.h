#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

#include "PluginProcessor.h"

// The pictures on the cards. Each one shows something a knob beside it does,
// drawn from the parameters or from the audio — never decoration.
namespace ui
{

// The meters and their recent history, read from the processor once per frame
// by the panel and shared by every view that draws them.
struct MeterFeed
{
    static constexpr int kHistory = 180; // six seconds at 30 frames a second

    // 0..1 over a 60 dB range.
    static float toScale (float linear) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (linear, -60.0f) + 60.0f) / 60.0f);
    }

    void push (const SillageAudioProcessor::Meter& meter) noexcept;

    std::array<float, kHistory> inputHistory {}, wetHistory {};
    int   head = 0; // the next slot to write; head - 1 is the newest
    float input = 0.0f, wet = 0.0f, output = 0.0f; // 0..1, falling back slowly
    float limiterDb = 0.0f;                        // gain reduction, positive dB
};

// Six seconds of what went in (grey) and what the plugin made of it (white):
// the tail, so Decay and Feedback can be seen as well as heard.
class TailView : public juce::Component
{
public:
    explicit TailView (const MeterFeed& feedToDraw) : feed (feedToDraw) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;

private:
    const MeterFeed& feed;
};

// The grain cloud as the knobs stand: each dash is a grain, placed by how far
// back it reads (Time, Spread, Decay), as long as Size, as high as its pitch,
// as bright as it is loud; a reversed grain is red.
class CloudView : public juce::Component
{
public:
    explicit CloudView (SillageAudioProcessor& p) : processor (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;

    // A hash of everything the picture depends on, so it repaints only then.
    uint32_t signature() const noexcept;

private:
    SillageAudioProcessor& processor;
};

// What one trip round the loop does to the spectrum: the loop filters, their
// resonance, and Shimmer's copies stepping away from it.
class LoopView : public juce::Component
{
public:
    explicit LoopView (SillageAudioProcessor& p) : processor (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    uint32_t signature() const noexcept;

private:
    SillageAudioProcessor& processor;
};

// In, wet and out levels, and a light for the loop limiter. `compact` drops
// the captions for the top bar.
class LevelMeter : public juce::Component
{
public:
    LevelMeter (const MeterFeed& feedToDraw, bool isCompact) : feed (feedToDraw), compact (isCompact)
    {
        setInterceptsMouseClicks (false, false);
    }
    void paint (juce::Graphics&) override;

private:
    const MeterFeed& feed;
    bool compact = false;
};

} // namespace ui
