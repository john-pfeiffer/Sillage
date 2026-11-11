#include "PluginEditor.h"
#include "Types.h"

namespace
{
constexpr int kPad = 8, kTopBarH = 34, kTabBarH = 28, kBarItemH = 24;
constexpr int kCurveEditorH = 210, kCurveEditorMinW = 320;
constexpr int kTimerHz = 30;
constexpr int kMinWindowW = 640, kMaxWindowW = 2560;

// One tab per group of cards. Parameters shown on the Main page or in the top
// bar are not repeated here, so nothing has two controls.
struct TabSpec
{
    const char* name;
    std::vector<SectionSpec> sections;
};
} // namespace

// ---- SillagePanel ------------------------------------------------------------

SillagePanel::SillagePanel (SillageAudioProcessor& p)
    : processor (p), curveEditor (p), modPanel (p)
{
    setLookAndFeel (&lookAndFeel);
    tooltips.setLookAndFeel (&lookAndFeel);

    buildPages();
    tabs.setTabBarDepth (kTabBarH);
    tabs.setOutline (0);
    tabs.setIndent (0);
    addAndMakeVisible (tabs);

    presetName.setJustificationType (juce::Justification::centredLeft);
    presetName.setFont (ui::font (13.0f));
    presetName.setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (presetName);
    refreshPresetName();

    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };
    initButton.onClick = [this] { processor.pushUndoState(); processor.resetToDefaults(); refreshPresetName(); };
    initButton.setTooltip ("Every parameter and curve back to its default");
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (initButton);

    // Type is an action: pick one, it applies, and the box goes back to its
    // prompt so picking the same one again works too.
    typeBox.setTextWhenNothingSelected ("Type");
    typeBox.setTooltip ("A tuned starting point. The second group are the ones only Sillage does");
    for (int t = 0; t < types::kNumTypes; ++t)
    {
        if (t == types::kFirstCharacterType)
            typeBox.addSeparator();
        typeBox.addItem (types::kTypeNames[(size_t) t], t + 1);
    }
    typeBox.onChange = [this] { applyType(); };
    addAndMakeVisible (typeBox);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setTooltip ("Hold the buffer: the tail stays playable, nothing new gets in");
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::id::freeze, freezeButton);
    addAndMakeVisible (freezeButton);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (params::id::wakeMode)))
        wakeBox.addItemList (choice->choices, 1);
    wakeBox.setTooltip ("Wake: one Shared tail, or an Isolated tail for every hit");
    wakeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, params::id::wakeMode, wakeBox);
    addAndMakeVisible (wakeBox);

    randomizeButton.onClick = [this]
    {
        processor.randomizeAll (processor.apvts.getRawParameterValue (params::id::randomizeAmount)->load() * 0.01f);
    };
    randomizeButton.setTooltip ("Roll everything except Mix, Output, Freeze, Wake and the stage switches");
    addAndMakeVisible (randomizeButton);

    // How far a roll goes sits beside the button that rolls: 100 % is a new
    // sound, 20 % is "like this, but different".
    randomizeAmount.setTooltip ("How far Randomize goes: 100 % is a new sound, 20 % is like this but different");
    randomizeAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, params::id::randomizeAmount, randomizeAmount);
    addAndMakeVisible (randomizeAmount);

    undoButton.onClick = [this] { processor.undo(); refreshPresetName(); };
    redoButton.onClick = [this] { processor.redo(); refreshPresetName(); };
    undoButton.setTooltip ("Step back over the last Randomize, Type, Init or Load");
    redoButton.setTooltip ("Step forward again");
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    panicButton.onClick  = [this] { pulse (params::id::panic); };
    rewindButton.onClick = [this] { pulse (params::id::rewindManual); };
    panicButton.setTooltip ("Clear every buffer and grain now");
    rewindButton.setTooltip ("Play the captured tail backwards into the loop. Needs Rewind on (Rewind & Chaos tab)");
    addAndMakeVisible (panicButton);
    addAndMakeVisible (rewindButton);

    addAndMakeVisible (meter);

    // Sliders build their value boxes when they are created, which was before
    // they had this look: have them build again.
    sendLookAndFeelChange();

    lastOnsetCount = processor.getOnsetCount();
    timerCallback();
    startTimerHz (kTimerHz);
}

SillagePanel::~SillagePanel()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SillagePanel::buildPages()
{
    namespace id = params::id;
    using ui::Condition;

    const auto background = ui::colour::background;

    mainPage = new MainPage (processor, meterFeed);
    tabs.addTab ("Main", background, mainPage, true);

    // Rewind Trig indices: 0 Timer, 1 Transient, 2 Threshold, 3 Manual.
    constexpr int kTimer = 0, kThreshold = 2;

    // A card's `power` is its own switch (the dot at its top right) and its
    // `gate` the stage it answers to. A control lists what else has to hold
    // for it to be doing anything, so of two alternatives (a time in ms or a
    // division under Sync) only the live one is lit.
    const std::vector<TabSpec> specs = {
        { "Grain", {
            { "Grain", { id::window } },
            { "Pitch", { id::pitchFine, id::pitchSpread, id::quantize, { id::quantizeRoot, id::quantize },
                         id::panSpread, id::reverse } },
            { "Grain Sync", { id::grainDivision, id::swing }, nullptr, 0, 0, nullptr, id::sync } } },
        { "Feedback", {
            { "Loop", { id::diffuse, id::drive, { id::satType, id::drive },
                        id::fbHighpass, id::fbResonance,
                        { id::shimmerInterval, id::shimmerAmount }, { id::shimmerFine, id::shimmerAmount } },
              nullptr, 0, 0, id::loopOn, id::loopOn } } },
        { "Transients", {
            { "Transients", { id::sensitivity }, nullptr, 0, 0, id::transientsOn, id::transientsOn },
            { "Retrigger", { id::retriggerCount,
                             { id::retriggerRate, { Condition::off (id::sync) } },
                             { id::retriggerDivision, { Condition::on (id::sync) } },
                             id::retriggerAmount, id::retriggerOffset },
              nullptr, 0, 0, id::transientsOn, id::retriggerOn },
            { "Duck", { id::duckDepth, id::duckAttack, id::duckRelease },
              nullptr, 0, 0, id::transientsOn, id::duckOn },
            { "Choke", { id::chokeAmount, id::chokeFade }, nullptr, 0, 0, id::transientsOn, id::chokeOn },
            { "Envelope", { id::envDensity, id::envSpread }, nullptr, 0, 0, id::transientsOn },
            { "Wake", { id::displace }, nullptr, 0, 0, id::transientsOn } } },
        { "Age", {
            { "Age", { id::lifetime, id::degradeBits, id::degradeRate, id::degradeNoise,
                       id::degradeTilt, id::degradeDrift, { id::degradeDriftDir, id::degradeDrift } },
              &curveEditor, kCurveEditorMinW, kCurveEditorH, id::ageOn, id::ageOn } } },
        { "Rewind & Chaos", {
            { "Rewind", { id::rewindLength, id::rewindTrigger,
                          { id::rewindInterval, { Condition::equals (id::rewindTrigger, kTimer), Condition::off (id::sync) } },
                          { id::rewindDivision, { Condition::equals (id::rewindTrigger, kTimer), Condition::on (id::sync) } },
                          { id::rewindThreshold, { Condition::equals (id::rewindTrigger, kThreshold) } },
                          id::rewindLevel, id::rewindPitch },
              nullptr, 0, 0, id::rewindOn, id::rewindOn },
            { "Chaos", {}, nullptr, 0, 0, id::chaosOn, id::chaosOn,
              "The stage switch for Chaos. Its amount is the Chaos knob on the Main page." },
            { "Freeze", { id::freezeFade } } } },
        { "Mod", {
            { "Modulation", {}, &modPanel, ModPanel::kMinWidth, ModPanel::requiredHeight(), id::modOn, id::modOn } } },
        { "Output", {
            { "Output", { id::output, id::wetHighpass, id::wetLowpass, id::fallbackBpm } } } },
    };

    for (const auto& spec : specs)
    {
        auto* page = new SectionPage (processor, spec.sections);
        sectionPages.push_back (page);
        tabs.addTab (spec.name, background, page, true);
    }
}

void SillagePanel::pulse (const char* parameterId)
{
    // Momentary parameters: the processor acts on the rising edge.
    if (auto* param = processor.apvts.getParameter (parameterId))
    {
        param->setValueNotifyingHost (1.0f);
        param->setValueNotifyingHost (0.0f);
    }
}

void SillagePanel::applyType()
{
    const auto id = typeBox.getSelectedId();
    if (id <= 0)
        return;

    processor.pushUndoState();
    types::apply (processor, (types::Type) (id - 1));
    refreshPresetName();
    typeBox.setSelectedId (0, juce::dontSendNotification);
}

void SillagePanel::refreshPresetName()
{
    presetName.setText (processor.getPresetName(), juce::dontSendNotification);
}

void SillagePanel::savePreset()
{
    auto directory = SillageAudioProcessor::getPresetDirectory();
    directory.createDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                   directory.getChildFile (processor.getPresetName() + SillageAudioProcessor::getPresetExtension()),
                                                   "*" + SillageAudioProcessor::getPresetExtension());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (! file.hasFileExtension (SillageAudioProcessor::getPresetExtension()))
                                  file = file.withFileExtension (SillageAudioProcessor::getPresetExtension());
                              processor.savePreset (file);
                              refreshPresetName();
                          });
}

void SillagePanel::loadPreset()
{
    auto directory = SillageAudioProcessor::getPresetDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Load preset", directory,
                                                   "*" + SillageAudioProcessor::getPresetExtension());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                              {
                                  processor.pushUndoState();
                                  processor.loadPreset (file);
                                  refreshPresetName();
                              }
                          });
}

void SillagePanel::timerCallback()
{
    meterFeed.push (processor.takeMeter());
    meter.repaint();

    const auto count = processor.getOnsetCount();
    bool repaintIndicator = false;

    if (count != lastOnsetCount)
    {
        lastOnsetCount = count;
        hitGlow = 1.0f;
        repaintIndicator = true;
    }
    else if (hitGlow > 0.0f)
    {
        hitGlow = juce::jmax (0.0f, hitGlow - 4.0f / (float) kTimerHz); // ~250 ms decay
        repaintIndicator = true;
    }

    if (repaintIndicator)
        repaint (hitIndicatorBounds.expanded (2));

    // The curve editor shows where the live tail sits on its curve.
    if (curveEditor.isShowing())
        curveEditor.repaint();

    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());

    // The Rewind button plays the captured tail, and nothing is captured
    // while Rewind is off.
    rewindButton.setEnabled (processor.apvts.getRawParameterValue (params::id::rewindOn)->load() >= 0.5f);

    // Host-side preset changes show up here too.
    if (presetName.getText() != processor.getPresetName())
        refreshPresetName();
}

void SillagePanel::paint (juce::Graphics& g)
{
    g.fillAll (ui::colour::background);

    // Hit light: a dot that flashes on a transient and fades.
    const auto dot = hitIndicatorBounds.toFloat().withSizeKeepingCentre (8.0f, 8.0f).withX ((float) hitIndicatorBounds.getX());
    g.setColour (ui::colour::faint);
    g.drawEllipse (dot.reduced (0.5f), 1.2f);
    if (hitGlow > 0.0f)
    {
        g.setColour (ui::colour::accent.withAlpha (hitGlow));
        g.fillEllipse (dot);
    }

    g.setColour (ui::colour::dim);
    g.setFont (ui::font (11.5f));
    g.drawText ("Hit", hitIndicatorBounds.withTrimmedLeft (13), juce::Justification::centredLeft);
}

int SillagePanel::getRequiredHeight()
{
    int pageH = MainPage::requiredHeight();
    for (auto* page : sectionPages)
        pageH = juce::jmax (pageH, page->getRequiredHeight (kBaseWidth));

    return kPad + kTopBarH + kTabBarH + pageH;
}

void SillagePanel::resized()
{
    // Top bar: what is loaded, what to start from, the two things that hold
    // the sound (Freeze, Wake), the roll and its way back, then the actions.
    int x = kPad + 4;
    const auto barY = kPad + (kTopBarH - kBarItemH) / 2 - 2;
    const auto place = [&] (juce::Component& c, int w, int gapAfter)
    {
        c.setBounds (x, barY, w, kBarItemH);
        x += w + gapAfter;
    };

    place (presetName, 88, 6);
    place (saveButton, 40, 4);
    place (loadButton, 40, 4);
    place (initButton, 36, 14);
    place (typeBox, 88, 14);
    place (freezeButton, 56, 4);
    place (wakeBox, 84, 14);
    place (randomizeButton, 80, 4);
    place (randomizeAmount, 54, 4);
    place (undoButton, 42, 4);
    place (redoButton, 42, 14);
    place (panicButton, 48, 4);
    place (rewindButton, 56, 14);

    hitIndicatorBounds = { x, barY, 36, kBarItemH };
    meter.setBounds (getWidth() - kPad - 44, barY, 40, kBarItemH);

    tabs.setBounds (0, kPad + kTopBarH, getWidth(), getHeight() - kPad - kTopBarH);
}

// ---- SillageAudioProcessorEditor ---------------------------------------------

SillageAudioProcessorEditor::SillageAudioProcessorEditor (SillageAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), panel (p)
{
    baseHeight = juce::jmax (1, panel.getRequiredHeight());
    panel.setSize (SillagePanel::kBaseWidth, baseHeight);
    addAndMakeVisible (panel);

    const auto aspect = (double) SillagePanel::kBaseWidth / (double) baseHeight;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (aspect);
    setResizeLimits (kMinWindowW, juce::roundToInt (kMinWindowW / aspect),
                     kMaxWindowW, juce::roundToInt (kMaxWindowW / aspect));

    setSize (SillagePanel::kBaseWidth, baseHeight);
}

SillageAudioProcessorEditor::~SillageAudioProcessorEditor() = default;

void SillageAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colour::background);
}

void SillageAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) SillagePanel::kBaseWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setTopLeftPosition (0, 0);
}
