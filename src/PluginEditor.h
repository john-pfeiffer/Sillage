#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Pure-function UI (EVS standard): plain rotary knobs, combo boxes and toggles
// grouped into labelled sections, resizable, no decoration. Sections are
// described by a table of parameter IDs, so a new build phase only extends
// kSections in the .cpp. A top bar holds the two actions (Randomize, Panic)
// and the transient hit indicator.
class SillageAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SillageAudioProcessorEditor (SillageAudioProcessor&);
    ~SillageAudioProcessorEditor() override;

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
    void timerCallback() override;
    void randomize();
    void panic();

    SillageAudioProcessor& processor;
    std::vector<Section> sections;

    juce::TextButton randomizeButton { "Randomize" };
    juce::TextButton panicButton { "Panic" };
    juce::Random randomizeRng;

    uint64_t lastOnsetCount = 0;
    float    hitGlow        = 0.0f;
    juce::Rectangle<int> hitIndicatorBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessorEditor)
};
