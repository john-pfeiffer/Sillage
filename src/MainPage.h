#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Conditions.h"
#include "PluginProcessor.h"
#include "SillageLookAndFeel.h"
#include "Visuals.h"

// The page the plugin opens on: twelve knobs on four cards, each card with a
// picture of what its knobs do.
//
//   Space   Time (with Sync), Decay, Spread, Feedback   the tail, as it happens
//   Grain   Density, Size, Pitch                        the cloud, as the knobs stand
//   Colour  Damping, Shimmer, Chaos                     one trip round the loop
//   Output  Width, Mix                                  levels and the loop limiter
//
// Everything behind them lives on the other tabs. A knob that the current
// state has taken over (Time under Sync, Density under grain Sync, Damping
// under its Lifetime Curve) is greyed out, and a knob a mod slot is moving
// shows where it is.
class MainPage : public juce::Component,
                 private juce::Timer
{
public:
    MainPage (SillageAudioProcessor& processor, const ui::MeterFeed& feed);
    ~MainPage() override;

    static constexpr int kNumKnobs = 12;
    static constexpr int kColumnW = 70, kKnobH = 76, kLabelH = 15, kVisualH = 84, kSyncRowH = 22;
    static constexpr int kPad = 8, kGap = 8;

    static int cardHeight() noexcept
    {
        return ui::kCardTitleH + kVisualH + 6 + kLabelH + kKnobH + 4 + kSyncRowH + ui::kCardPad;
    }
    static int requiredHeight() noexcept { return kPad + cardHeight() + kPad; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        const char* id = nullptr;
        std::vector<ui::Condition> needs;
        juce::Label  title;
        juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        bool  enabled   = true;
        float modOffset = 0.0f;
    };

    struct Card
    {
        const char* name = "";
        int firstKnob = 0, numKnobs = 0;
        juce::Component* visual = nullptr;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;

    SillageAudioProcessor& processor;
    std::array<Knob, kNumKnobs> knobs;
    std::array<Card, 4> cards;

    ui::TailView   tailView;
    ui::CloudView  cloudView;
    ui::LoopView   loopView;
    ui::LevelMeter levelMeter;
    uint32_t cloudSignature = 0, loopSignature = 0;

    // Sync lives under Time: when it is on, the division is what Time means.
    juce::ToggleButton syncToggle { "Sync" };
    juce::ComboBox     division;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;
    bool divisionEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPage)
};
