#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// The page the plugin opens on: nine large knobs that read like a reverb or
// delay — Time, Decay, Spread (the delay-to-reverb slider), Feedback, Size,
// Damping, Shimmer, Width, Mix — and nothing else. Everything behind them
// lives on the other tabs. Pure-function: stock rotary sliders, bigger.
class MainPage : public juce::Component,
                 private juce::Timer
{
public:
    explicit MainPage (SillageAudioProcessor& processor);
    ~MainPage() override;

    static constexpr int kNumKnobs = 9;
    static constexpr int kKnobSize = 100, kTitleH = 20, kSubtitleH = 14, kPad = 8;
    static constexpr int kSyncRowH = 24;

    static int requiredHeight() noexcept
    {
        return kPad + kTitleH + kSubtitleH + kKnobSize + kPad + kSyncRowH + kPad;
    }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct BigKnob
    {
        juce::Label  title, subtitle;
        juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;

    SillageAudioProcessor& processor;
    std::array<BigKnob, kNumKnobs> knobs;

    // Sync lives under Time: when it is on, the division is what Time means.
    juce::ToggleButton syncToggle { "Sync" };
    juce::ComboBox     division;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;
    bool divisionEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPage)
};
