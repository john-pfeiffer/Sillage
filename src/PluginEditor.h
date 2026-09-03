#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "CurveEditor.h"
#include "ModPanel.h"
#include "PluginProcessor.h"

// The whole UI at its design size. Pure-function (EVS standard): plain rotary
// knobs, combo boxes and toggles grouped into labelled sections, no
// decoration. Sections are described by a table of parameter IDs in the .cpp;
// sections that fit side by side share a band, and a section can carry a
// larger component beside its knobs (the curve editor, the mod panel). The
// top bar holds presets, the actions (Randomize, Panic, Rewind) and the hit
// indicator.
class SillagePanel : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1280;

    explicit SillagePanel (SillageAudioProcessor&);
    ~SillagePanel() override;

    // Height the layout needs at kBaseWidth.
    int getRequiredHeight();

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
        juce::Component* extra = nullptr; // laid out beside the knobs, fills the band
        int extraMinWidth = 0;
        int extraHeight   = 0;
        juce::Rectangle<int> header; // filled in by layout(), read by paint()
    };

    void buildSections();
    int  layout (bool apply);
    void timerCallback() override;
    void randomize();
    void pulse (const char* parameterId);
    void savePreset();
    void loadPreset();
    void refreshPresetName();

    SillageAudioProcessor& processor;
    std::vector<Section> sections;
    CurveEditor curveEditor;
    ModPanel    modPanel;

    juce::Label      presetName;
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton initButton { "Init" };
    juce::TextButton randomizeButton { "Randomize" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton rewindButton { "Rewind" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Random randomizeRng;

    uint64_t lastOnsetCount = 0;
    float    hitGlow        = 0.0f;
    juce::Rectangle<int> hitIndicatorBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillagePanel)
};

// Resizable window (EVS standard): the panel is laid out once at its design
// size and scaled as a whole, keeping its aspect ratio, so every knob stays
// the same knob at any window size.
class SillageAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SillageAudioProcessorEditor (SillageAudioProcessor&);
    ~SillageAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SillageAudioProcessor& processor;
    SillagePanel panel;
    int baseHeight = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessorEditor)
};
