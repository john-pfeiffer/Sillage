#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// One control on a tab. `master` (optional) is the parameter that has to be
// on (a switch) or above zero (an amount) for this control to mean anything;
// while it is not, the control is shown greyed out. That is how a tab tells
// you which knob turns the others on.
struct ControlSpec
{
    ControlSpec (const char* parameterId, const char* masterId = nullptr)
        : id (parameterId), master (masterId) {}

    const char* id;
    const char* master;
};

// One tab of the advanced UI: labelled sections of plain knobs, combos and
// toggles, described by a table of parameter IDs. Sections that fit side by
// side share a band; a section may carry a larger component beside its knobs
// (the curve editor, the mod panel), which then fills the band. `gate` is the
// stage switch for the section: while it is off every control except the
// switch itself is greyed out.
struct SectionSpec
{
    const char* name;
    std::vector<ControlSpec> controls;
    juce::Component* extra = nullptr; // not owned
    int extraMinWidth = 0;
    int extraHeight   = 0;
    const char* gate  = nullptr;
};

class SectionPage : public juce::Component,
                    private juce::Timer
{
public:
    SectionPage (SillageAudioProcessor& processor, std::vector<SectionSpec> specs);
    ~SectionPage() override;

    // Height the layout needs at `width`.
    int getRequiredHeight (int width);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kKnobW = 76, kKnobH = 84, kLabelH = 16, kHeaderH = 22, kPad = 8;

private:
    enum class Kind { knob, combo, toggle };

    struct Control
    {
        Kind kind = Kind::knob;
        const char* id     = nullptr;
        const char* master = nullptr;
        bool enabled = true;
        std::unique_ptr<juce::Component> comp;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct Section
    {
        juce::String name;
        std::vector<Control> controls;
        juce::Component* extra = nullptr;
        int extraMinWidth = 0;
        int extraHeight   = 0;
        const char* gate  = nullptr;
        bool extraEnabled = true;
        juce::Rectangle<int> header; // filled in by layout(), read by paint()
    };

    int  layout (int width, bool apply);
    bool isOn (const char* parameterId) const noexcept;
    void refreshEnabled();
    void timerCallback() override { refreshEnabled(); }

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPage)
};
