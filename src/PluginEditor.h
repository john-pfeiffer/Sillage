#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "CurveEditor.h"
#include "MainPage.h"
#include "ModPanel.h"
#include "PluginProcessor.h"
#include "SectionPage.h"

// The whole UI at its design size: a top bar (presets, Type, Freeze, Wake,
// the actions and the hit indicator) over a tab strip. The Main tab is eight
// large knobs that read like a reverb or delay; every other parameter lives
// on the tab it belongs to. Pure-function (EVS standard): stock knobs, combos
// and toggles, no decoration.
class SillagePanel : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kBaseWidth = 960;

    explicit SillagePanel (SillageAudioProcessor&);
    ~SillagePanel() override;

    // Height the layout needs at kBaseWidth.
    int getRequiredHeight();

    // For the snapshot tool.
    int  getNumTabs() const { return tabs.getNumTabs(); }
    juce::String getTabName (int index) const { return tabs.getTabNames()[index]; }
    void showTab (int index) { tabs.setCurrentTabIndex (index); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buildPages();
    void timerCallback() override;
    void randomize();
    void pulse (const char* parameterId);
    void applyType();
    void savePreset();
    void loadPreset();
    void refreshPresetName();

    SillageAudioProcessor& processor;

    // Declared before the tabs so the pages (which show them) go first.
    CurveEditor curveEditor;
    ModPanel    modPanel;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<SectionPage*> sectionPages; // owned by `tabs`
    MainPage* mainPage = nullptr;           // owned by `tabs`

    juce::Label      presetName;
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton initButton { "Init" };
    juce::ComboBox   typeBox;
    juce::TextButton freezeButton { "Freeze" };
    juce::ComboBox   wakeBox;
    juce::TextButton randomizeButton { "Randomize" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton rewindButton { "Rewind" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> wakeAttachment;
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

    SillagePanel& getPanel() { return panel; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SillageAudioProcessor& processor;
    SillagePanel panel;
    int baseHeight = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SillageAudioProcessorEditor)
};
