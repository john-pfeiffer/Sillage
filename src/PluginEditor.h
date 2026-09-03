#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Pure-function UI (EVS standard): plain rotary knobs and combo boxes grouped
// into labelled sections, resizable, no decoration. Sections are described by
// a table of parameter IDs so new phases only extend kSections.
class SillageAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SillageAudioProcessorEditor (SillageAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct SectionDesc
    {
        const char* name;
        std::vector<const char*> paramIds;
    };

    struct Control
    {
        std::unique_ptr<juce::Component> comp;   // Slider or ComboBox
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    };

    struct Section
    {
        juce::String name;
        std::vector<Control> controls;
    };

    void buildSections();

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessorEditor)
};
