#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Conditions.h"
#include "PluginProcessor.h"
#include "SillageLookAndFeel.h"

// One control on a card. `needs` is everything that has to hold for the
// control to mean anything in the current state — its master switch, an
// amount above zero, Sync on or off, a trigger mode; while any of it does
// not, the control is shown greyed out. That is how a card tells you which
// knob turns the others on, and which of two alternatives is the live one.
struct ControlSpec
{
    ControlSpec (const char* parameterId) : id (parameterId) {}
    ControlSpec (const char* parameterId, const char* masterId)
        : id (parameterId), needs { ui::Condition::on (masterId) } {}
    ControlSpec (const char* parameterId, std::vector<ui::Condition> conditions)
        : id (parameterId), needs (std::move (conditions)) {}

    const char* id;
    std::vector<ui::Condition> needs;
};

// One card: a titled group of knobs, combos and toggles described by a table
// of parameter IDs. Cards that fit side by side share a band; a card may carry
// a larger component beside its knobs (the curve editor, the mod panel), which
// then fills the band.
//
// `power` is the card's own switch, drawn as the dot at its top right: the
// stage's On switch on the card that leads a tab, or a feature's own switch
// (Retrigger, Duck, Sync). `gate` is the stage switch the card answers to.
// While either is off every control on the card is greyed out.
struct SectionSpec
{
    const char* name;
    std::vector<ControlSpec> controls;
    juce::Component* extra = nullptr; // not owned
    int extraMinWidth = 0;
    int extraHeight   = 0;
    const char* gate  = nullptr;
    const char* power = nullptr;
    const char* note  = nullptr; // a line of dim text in the card, for a card that needs explaining
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

    static constexpr int kKnobW = 72, kKnobH = 74, kLabelH = 15, kPad = 8, kGap = 8;

private:
    enum class Kind { knob, combo, toggle };

    struct Control
    {
        Kind kind = Kind::knob;
        const char* id = nullptr;
        std::vector<ui::Condition> needs;
        bool  enabled   = true;
        float modOffset = 0.0f;
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
        const char* power = nullptr;
        juce::String note;
        std::unique_ptr<ui::PowerDot> dot;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dotAttachment;
        bool live         = true; // gate and power both on
        bool extraEnabled = true;
        juce::Rectangle<int> card; // filled in by layout(), read by paint()
    };

    int  layout (int width, bool apply);
    bool isOn (const char* parameterId) const noexcept;
    void refresh();
    void timerCallback() override { refresh(); }

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPage)
};
