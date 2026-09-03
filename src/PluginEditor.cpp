#include "PluginEditor.h"
#include "Randomize.h"

namespace
{
// One entry per UI section; extend as build phases land. Sections that fit
// side by side share a band.
const std::vector<std::pair<const char*, std::vector<const char*>>> kSections = {
    { "Grain",      { params::id::time, params::id::timeSync, params::id::timeDivision,
                      params::id::density, params::id::spread, params::id::size,
                      params::id::window } },
    { "Pitch",      { params::id::pitch, params::id::pitchFine, params::id::pitchSpread,
                      params::id::quantize, params::id::quantizeRoot,
                      params::id::panSpread, params::id::reverse } },
    { "Feedback",   { params::id::feedback, params::id::fbHighpass, params::id::fbLowpass,
                      params::id::fbResonance, params::id::shimmerInterval,
                      params::id::shimmerAmount, params::id::shimmerFine,
                      params::id::diffuse, params::id::satType, params::id::drive } },
    { "Sync",       { params::id::sync, params::id::grainDivision, params::id::swing } },
    { "Transients", { params::id::sensitivity,
                      params::id::retriggerOn, params::id::retriggerCount, params::id::retriggerRate,
                      params::id::retriggerDivision, params::id::retriggerAmount, params::id::retriggerOffset,
                      params::id::duckOn, params::id::duckDepth, params::id::duckAttack, params::id::duckRelease,
                      params::id::chokeOn, params::id::chokeAmount, params::id::chokeFade,
                      params::id::envDensity, params::id::envSpread } },
    { "Freeze & Chaos", { params::id::freeze, params::id::freezeFade, params::id::chaos,
                          params::id::randomizeAmount } },
    { "Wake",       { params::id::wakeMode, params::id::displace } },
    { "Rewind",     { params::id::rewindOn, params::id::rewindLength, params::id::rewindTrigger,
                      params::id::rewindInterval, params::id::rewindDivision,
                      params::id::rewindThreshold, params::id::rewindLevel, params::id::rewindPitch } },
    { "Age",        { params::id::lifetime, params::id::degradeBits, params::id::degradeRate,
                      params::id::degradeNoise, params::id::degradeTilt, params::id::degradeDrift,
                      params::id::degradeDriftDir } },
    { "Output",     { params::id::mix, params::id::output, params::id::width,
                      params::id::wetHighpass, params::id::wetLowpass, params::id::fallbackBpm } },
    { "Modulation", {} },
};

// Sections that carry a larger component beside their knobs.
constexpr const char* kCurveSection = "Age";
constexpr const char* kModSection   = "Modulation";

constexpr int kKnobW = 76, kKnobH = 84, kLabelH = 16, kHeaderH = 22, kPad = 8;
constexpr int kTopBarH = 32, kButtonW = 88, kSmallButtonW = 56, kIndicatorSize = 14, kPresetNameW = 180;
constexpr int kCurveEditorH = 210, kCurveEditorMinW = 320;
constexpr int kTimerHz = 30;
constexpr int kMinWindowW = 640, kMaxWindowW = 2560;
} // namespace

// ---- SillagePanel ------------------------------------------------------------

SillagePanel::SillagePanel (SillageAudioProcessor& p)
    : processor (p), curveEditor (p), modPanel (p)
{
    buildSections();
    addAndMakeVisible (curveEditor);
    addAndMakeVisible (modPanel);

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

void SillagePanel::buildSections()
{
    for (const auto& [name, ids] : kSections)
    {
        Section section;
        section.name = name;

        if (section.name == kCurveSection)
        {
            section.extra         = &curveEditor;
            section.extraMinWidth = kCurveEditorMinW;
            section.extraHeight   = kCurveEditorH;
        }
        else if (section.name == kModSection)
        {
            section.extra         = &modPanel;
            section.extraMinWidth = ModPanel::kMinWidth;
            section.extraHeight   = ModPanel::requiredHeight();
        }

        for (auto* paramId : ids)
        {
            auto* param = processor.apvts.getParameter (paramId);
            jassert (param != nullptr);

            Control control;
            control.label = std::make_unique<juce::Label> (juce::String(), param->getName (32));
            control.label->setJustificationType (juce::Justification::centred);
            control.label->setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (*control.label);

            if (dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr)
            {
                auto box = std::make_unique<juce::ComboBox>();
                addAndMakeVisible (*box);
                control.kind = Kind::combo;
                control.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    processor.apvts, paramId, *box);
                control.comp = std::move (box);
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            {
                auto button = std::make_unique<juce::ToggleButton>();
                addAndMakeVisible (*button);
                control.kind = Kind::toggle;
                control.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    processor.apvts, paramId, *button);
                control.comp = std::move (button);
            }
            else
            {
                auto slider = std::make_unique<juce::Slider> (
                    juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW - 8, 16);
                addAndMakeVisible (*slider);
                control.kind = Kind::knob;
                control.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    processor.apvts, paramId, *slider);
                control.comp = std::move (slider);
            }

            section.controls.push_back (std::move (control));
        }

        sections.push_back (std::move (section));
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
    curveEditor.repaint();

    // Host-side preset changes show up here too.
    if (presetName.getText() != processor.getPresetName())
        refreshPresetName();
}

void SillagePanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (const auto& section : sections)
        g.drawText (section.name, section.header, juce::Justification::centredLeft);

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
    return layout (false);
}

void SillagePanel::resized()
{
    layout (true);
}

int SillagePanel::layout (bool apply)
{
    const auto width = kBaseWidth;
    const auto setBounds = [apply] (juce::Component& c, juce::Rectangle<int> r) { if (apply) c.setBounds (r); };

    // Top bar: presets on the left, actions and the hit indicator after them.
    int x = kPad;
    const auto barY = kPad, barH = kTopBarH - 8;
    setBounds (presetName, { x, barY, kPresetNameW, barH });   x += kPresetNameW + kPad;
    setBounds (saveButton, { x, barY, kSmallButtonW, barH });  x += kSmallButtonW + kPad;
    setBounds (loadButton, { x, barY, kSmallButtonW, barH });  x += kSmallButtonW + kPad;
    setBounds (initButton, { x, barY, kSmallButtonW, barH });  x += kSmallButtonW + kPad * 3;
    setBounds (randomizeButton, { x, barY, kButtonW, barH });  x += kButtonW + kPad;
    setBounds (panicButton, { x, barY, kButtonW, barH });      x += kButtonW + kPad;
    setBounds (rewindButton, { x, barY, kButtonW, barH });     x += kButtonW + kPad * 2;
    if (apply)
        hitIndicatorBounds = { x, barY + (barH - kIndicatorSize) / 2, kIndicatorSize, kIndicatorSize };

    const auto rowH        = kKnobH + kLabelH;
    const auto totalPerRow = juce::jmax (1, (width - kPad * 2) / kKnobW);

    // Flow layout: a section starts a new band unless it fits beside the
    // previous one. A section with an extra component fills the band.
    int bandX = kPad, bandY = kPad + kTopBarH, bandH = 0;

    for (auto& section : sections)
    {
        const auto count      = (int) section.controls.size();
        const auto hasExtra   = section.extra != nullptr;
        const auto sectionW   = juce::jmin (count, totalPerRow) * kKnobW
                              + (hasExtra ? (count > 0 ? kPad : 0) + section.extraMinWidth : 0);
        const auto fitsBeside = bandX > kPad && bandX + sectionW <= width - kPad;

        if (! fitsBeside && bandX > kPad)
        {
            bandY += bandH + kPad;
            bandX  = kPad;
            bandH  = 0;
        }

        const auto available = width - bandX - kPad - (hasExtra ? kPad + section.extraMinWidth : 0);
        const auto perRow    = juce::jmax (1, available / kKnobW);
        const auto headerW   = juce::jmax (juce::jmin (count, perRow) * kKnobW, hasExtra ? section.extraMinWidth : 0);
        if (apply)
            section.header = { bandX, bandY, headerW, kHeaderH };

        int index = 0;
        for (auto& control : section.controls)
        {
            const auto column = index % perRow;
            const auto row    = index / perRow;
            const auto cx     = bandX + column * kKnobW;
            const auto top    = bandY + kHeaderH + row * rowH;

            setBounds (*control.label, { cx, top, kKnobW, kLabelH });

            switch (control.kind)
            {
                case Kind::combo:
                    setBounds (*control.comp, { cx + 2, top + kLabelH + (kKnobH - 24) / 2, kKnobW - 4, 24 });
                    break;
                case Kind::toggle:
                    setBounds (*control.comp, { cx + kKnobW / 2 - 12, top + kLabelH + (kKnobH - 24) / 2, 24, 24 });
                    break;
                case Kind::knob:
                default:
                    setBounds (*control.comp, { cx, top + kLabelH, kKnobW, kKnobH });
                    break;
            }

            ++index;
        }

        const auto rows = (count + perRow - 1) / perRow;
        auto sectionH   = kHeaderH + rows * rowH;
        auto usedW      = juce::jmin (count, perRow) * kKnobW;

        if (hasExtra)
        {
            const auto extraX = bandX + usedW + (count > 0 ? kPad : 0);
            setBounds (*section.extra, { extraX, bandY + kHeaderH, width - kPad - extraX, section.extraHeight });
            sectionH = juce::jmax (sectionH, kHeaderH + section.extraHeight);
            usedW    = width - kPad - bandX;
        }

        bandH  = juce::jmax (bandH, sectionH);
        bandX += usedW + kPad * 2;
    }

    return bandY + bandH + kPad;
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

    // Open at the design size, or as large as fits an ordinary display.
    const auto openW = juce::jmin (SillagePanel::kBaseWidth, 1180);
    setSize (openW, juce::roundToInt (openW / aspect));
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
