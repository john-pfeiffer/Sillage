#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "CurveEditor.h"
#include "MainPage.h"
#include "ModPanel.h"
#include "PluginProcessor.h"
#include "SectionPage.h"
#include "SillageLookAndFeel.h"
#include "Visuals.h"

// The whole UI at its design size: a top bar (presets, Type, Freeze, Wake,
// Randomize with its amount, Undo / Redo, Panic, Rewind, the hit light and a
// small meter) over a tab strip. The Main tab is twelve knobs on four cards;
// every other parameter lives on a card on the tab it belongs to.
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
    void pulse (const char* parameterId);
    void applyType();
    void savePreset();
    void loadPreset();
    void refreshPresetName();

    SillageAudioProcessor& processor;

    // First in, last out: everything below is drawn with it.
    ui::LookAndFeel lookAndFeel;
    ui::MeterFeed   meterFeed;

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
    juce::Slider     randomizeAmount { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton panicButton { "Panic" };
    juce::TextButton rewindButton { "Rewind" };
    ui::LevelMeter   meter { meterFeed, true };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   freezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> wakeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   randomizeAmountAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

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
