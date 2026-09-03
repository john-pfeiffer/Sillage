#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// One tab of the advanced UI: labelled sections of plain knobs, combos and
// toggles, described by a table of parameter IDs. Sections that fit side by
// side share a band; a section may carry a larger component beside its knobs
// (the curve editor, the mod panel), which then fills the band.
struct SectionSpec
{
    const char* name;
    std::vector<const char*> ids;
    juce::Component* extra = nullptr; // not owned
    int extraMinWidth = 0;
    int extraHeight   = 0;
};

class SectionPage : public juce::Component
{
public:
    SectionPage (SillageAudioProcessor& processor, std::vector<SectionSpec> specs);

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
        juce::Rectangle<int> header; // filled in by layout(), read by paint()
    };

    int layout (int width, bool apply);

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionPage)
};
