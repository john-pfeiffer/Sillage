#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Pure-function UI (EVS standard): plain rotary knobs, combo boxes and toggles
// grouped into labelled sections, resizable, no decoration. Sections are
// described by a table of parameter IDs, so a new build phase only extends
// kSections in the .cpp.
class SillageAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SillageAudioProcessorEditor (SillageAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

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
        int headerY = 0; // filled in by resized(), read by paint()
    };

    void buildSections();

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessorEditor)
};
