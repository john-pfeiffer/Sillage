#include "PluginEditor.h"
#include "Randomize.h"
#include "Types.h"

namespace
{
constexpr int kPad = 8, kTopBarH = 32, kTabBarH = 26;
constexpr int kButtonW = 78, kSmallButtonW = 46, kIndicatorSize = 14, kPresetNameW = 140;
constexpr int kTypeW = 100, kFreezeW = 62, kWakeW = 92;
constexpr int kCurveEditorH = 210, kCurveEditorMinW = 320;
constexpr int kTimerHz = 30;
constexpr int kMinWindowW = 640, kMaxWindowW = 2560;

// One tab per group of sections. Parameters shown on the Main page or in the
// top bar (Time + Sync, Decay, Spread, Feedback, Size, Loop LP, Shimmer Amt,
// Width, Mix, Freeze, Wake) are not repeated here, so nothing has two controls.
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
    buildPages();
    tabs.setTabBarDepth (kTabBarH);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    presetName.setJustificationType (juce::Justification::centredLeft);
    presetName.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (presetName);
    refreshPresetName();

    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };
    initButton.onClick = [this] { processor.resetToDefaults(); refreshPresetName(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (initButton);

    // Type is an action: pick one, it applies, and the box goes back to its
    // prompt so picking the same one again works too.
    typeBox.setTextWhenNothingSelected ("Type...");
    for (int t = 0; t < types::kNumTypes; ++t)
        typeBox.addItem (types::kTypeNames[(size_t) t], t + 1);
    typeBox.onChange = [this] { applyType(); };
    addAndMakeVisible (typeBox);

    freezeButton.setClickingTogglesState (true);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::id::freeze, freezeButton);
    addAndMakeVisible (freezeButton);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (params::id::wakeMode)))
        wakeBox.addItemList (choice->choices, 1);
    wakeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, params::id::wakeMode, wakeBox);
    addAndMakeVisible (wakeBox);

    randomizeButton.onClick = [this] { randomize(); };
    panicButton.onClick     = [this] { pulse (params::id::panic); };
    rewindButton.onClick    = [this] { pulse (params::id::rewindManual); };
    addAndMakeVisible (randomizeButton);
    addAndMakeVisible (panicButton);
    addAndMakeVisible (rewindButton);

    lastOnsetCount = processor.getOnsetCount();
    startTimerHz (kTimerHz);
}

SillagePanel::~SillagePanel()
{
    stopTimer();
}

void SillagePanel::buildPages()
{
    namespace id = params::id;

    const auto background = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);

    mainPage = new MainPage (processor);
    tabs.addTab ("Main", background, mainPage, true);

    // Each stage tab leads with its On switch (the section gate) and marks the
    // knob that turns the rest on (the master), so a dead stage looks dead.
    const std::vector<TabSpec> specs = {
        { "Grain", {
            { "Grain", { id::density, id::window } },
            { "Pitch", { id::pitch, id::pitchFine, id::pitchSpread, id::quantize, id::quantizeRoot,
                         id::panSpread, id::reverse } } } },
        { "Feedback", {
            { "Loop", { id::loopOn, id::diffuse, id::drive, { id::satType, id::drive },
                        id::fbHighpass, id::fbResonance,
                        { id::shimmerInterval, id::shimmerAmount }, { id::shimmerFine, id::shimmerAmount } },
              nullptr, 0, 0, id::loopOn },
            { "Sync", { id::sync, { id::grainDivision, id::sync }, { id::swing, id::sync } } } } },
        { "Transients", {
            { "Transients", { id::transientsOn, id::sensitivity }, nullptr, 0, 0, id::transientsOn },
            { "Retrigger", { id::retriggerOn,
                             { id::retriggerCount, id::retriggerOn }, { id::retriggerRate, id::retriggerOn },
                             { id::retriggerDivision, id::retriggerOn }, { id::retriggerAmount, id::retriggerOn },
                             { id::retriggerOffset, id::retriggerOn } },
              nullptr, 0, 0, id::transientsOn },
            { "Duck", { id::duckOn, { id::duckDepth, id::duckOn }, { id::duckAttack, id::duckOn },
                        { id::duckRelease, id::duckOn } },
              nullptr, 0, 0, id::transientsOn },
            { "Choke", { id::chokeOn, { id::chokeAmount, id::chokeOn }, { id::chokeFade, id::chokeOn } },
              nullptr, 0, 0, id::transientsOn },
            { "Envelope", { id::envDensity, id::envSpread }, nullptr, 0, 0, id::transientsOn },
            { "Wake", { id::displace }, nullptr, 0, 0, id::transientsOn } } },
        { "Age", {
            { "Age", { id::ageOn, id::lifetime, id::degradeBits, id::degradeRate, id::degradeNoise,
                       id::degradeTilt, id::degradeDrift, { id::degradeDriftDir, id::degradeDrift } },
              &curveEditor, kCurveEditorMinW, kCurveEditorH, id::ageOn } } },
        { "Rewind & Chaos", {
            { "Rewind", { id::rewindOn, { id::rewindLength, id::rewindOn }, { id::rewindTrigger, id::rewindOn },
                          { id::rewindInterval, id::rewindOn }, { id::rewindDivision, id::rewindOn },
                          { id::rewindThreshold, id::rewindOn }, { id::rewindLevel, id::rewindOn },
                          { id::rewindPitch, id::rewindOn } },
              nullptr, 0, 0, id::rewindOn },
            { "Chaos", { id::chaosOn, { id::chaos, id::chaosOn } }, nullptr, 0, 0, id::chaosOn },
            { "Freeze & Randomize", { id::freezeFade, id::randomizeAmount } } } },
        { "Mod", {
            { "Modulation", { id::modOn }, &modPanel, ModPanel::kMinWidth, ModPanel::requiredHeight(), id::modOn } } },
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

void SillagePanel::randomize()
{
    const auto amount = processor.apvts.getRawParameterValue (params::id::randomizeAmount)->load() * 0.01f;
    randomize::apply (processor.apvts, amount, randomizeRng);

    // Curves are randomised too (5.10); they live in the state tree.
    auto curves = processor.getCurves();
    lifetime::randomise (curves, amount, randomizeRng);
    processor.setCurves (curves);
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
                                  processor.loadPreset (file);
                                  refreshPresetName();
                              }
                          });
}

void SillagePanel::timerCallback()
{
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

    // Host-side preset changes show up here too.
    if (presetName.getText() != processor.getPresetName())
        refreshPresetName();
}

void SillagePanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // Hit indicator: a plain square that lights on a transient and fades.
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRect (hitIndicatorBounds);
    g.setColour (juce::Colours::white.withAlpha (0.15f + 0.85f * hitGlow));
    g.fillRect (hitIndicatorBounds.reduced (2));

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Hit", hitIndicatorBounds.getRight() + 6, hitIndicatorBounds.getY(),
                40, hitIndicatorBounds.getHeight(), juce::Justification::centredLeft);
}

int SillagePanel::getRequiredHeight()
{
    int pageH = MainPage::requiredHeight();
    for (auto* page : sectionPages)
        pageH = juce::jmax (pageH, page->getRequiredHeight (kBaseWidth));

    return kPad + kTopBarH + kTabBarH + pageH + kPad;
}

void SillagePanel::resized()
{
    // Top bar: presets, Type, Freeze, Wake, then the actions and the hit indicator.
    int x = kPad;
    const auto barY = kPad, barH = kTopBarH - 8;
    const auto place = [&] (juce::Component& c, int w, int gapAfter)
    {
        c.setBounds (x, barY, w, barH);
        x += w + gapAfter;
    };

    place (presetName, kPresetNameW, kPad);
    place (saveButton, kSmallButtonW, 4);
    place (loadButton, kSmallButtonW, 4);
    place (initButton, kSmallButtonW, kPad * 2);
    place (typeBox, kTypeW, kPad * 2);
    place (freezeButton, kFreezeW, 4);
    place (wakeBox, kWakeW, kPad * 2);
    place (randomizeButton, kButtonW, 4);
    place (panicButton, kSmallButtonW + 8, 4);
    place (rewindButton, kSmallButtonW + 14, kPad * 2);
    hitIndicatorBounds = { x, barY + (barH - kIndicatorSize) / 2, kIndicatorSize, kIndicatorSize };

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
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SillageAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) SillagePanel::kBaseWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setTopLeftPosition (0, 0);
}
