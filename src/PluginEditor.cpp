#include "PluginEditor.h"
#include "Randomize.h"

namespace
{
// One entry per UI section; extend as build phases land. Sections that fit
// side by side share a row.
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
    { "Output",     { params::id::mix, params::id::output, params::id::fallbackBpm } },
    { "Transients", { params::id::sensitivity,
                      params::id::retriggerOn, params::id::retriggerCount, params::id::retriggerRate,
                      params::id::retriggerDivision, params::id::retriggerAmount, params::id::retriggerOffset,
                      params::id::duckOn, params::id::duckDepth, params::id::duckAttack, params::id::duckRelease,
                      params::id::chokeOn, params::id::chokeAmount, params::id::chokeFade,
                      params::id::envDensity, params::id::envSpread } },
    { "Freeze & Chaos", { params::id::freeze, params::id::freezeFade, params::id::chaos,
                          params::id::randomizeAmount } },
    { "Rewind",     { params::id::rewindOn, params::id::rewindLength, params::id::rewindTrigger,
                      params::id::rewindInterval, params::id::rewindDivision,
                      params::id::rewindThreshold, params::id::rewindLevel, params::id::rewindPitch } },
    { "Age",        { params::id::lifetime, params::id::degradeBits, params::id::degradeRate,
                      params::id::degradeNoise, params::id::degradeTilt, params::id::degradeDrift,
                      params::id::degradeDriftDir } },
};

// The Age section gets the curve editor beside its knobs.
constexpr const char* kCurveSection = "Age";

constexpr int kKnobW = 76, kKnobH = 84, kLabelH = 16, kHeaderH = 22, kPad = 8;
constexpr int kTopBarH = 32, kButtonW = 96, kIndicatorSize = 14;
constexpr int kCurveEditorH = 210, kCurveEditorMinW = 320;
constexpr int kTimerHz = 30;
} // namespace

SillageAudioProcessorEditor::SillageAudioProcessorEditor (SillageAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), curveEditor (p)
{
    buildSections();
    addAndMakeVisible (curveEditor);

    randomizeButton.onClick = [this] { randomize(); };
    panicButton.onClick     = [this] { pulse (params::id::panic); };
    rewindButton.onClick    = [this] { pulse (params::id::rewindManual); };
    addAndMakeVisible (randomizeButton);
    addAndMakeVisible (panicButton);
    addAndMakeVisible (rewindButton);

    lastOnsetCount = processor.getOnsetCount();
    startTimerHz (kTimerHz);

    setResizable (true, true);
    setResizeLimits (720, 520, 2600, 1800);
    setSize (1280, 860);
}

SillageAudioProcessorEditor::~SillageAudioProcessorEditor()
{
    stopTimer();
}

void SillageAudioProcessorEditor::buildSections()
{
    for (const auto& [name, ids] : kSections)
    {
        Section section;
        section.name = name;

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

void SillageAudioProcessorEditor::randomize()
{
    const auto amount = processor.apvts.getRawParameterValue (params::id::randomizeAmount)->load() * 0.01f;
    randomize::apply (processor.apvts, amount, randomizeRng);

    // Curves are randomised too (5.10); they live in the state tree.
    auto curves = processor.getCurves();
    lifetime::randomise (curves, amount, randomizeRng);
    processor.setCurves (curves);
}

void SillageAudioProcessorEditor::pulse (const char* parameterId)
{
    // Momentary parameters: the processor acts on the rising edge.
    if (auto* param = processor.apvts.getParameter (parameterId))
    {
        param->setValueNotifyingHost (1.0f);
        param->setValueNotifyingHost (0.0f);
    }
}

void SillageAudioProcessorEditor::timerCallback()
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
}

void SillageAudioProcessorEditor::paint (juce::Graphics& g)
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

void SillageAudioProcessorEditor::resized()
{
    randomizeButton.setBounds (kPad, kPad, kButtonW, kTopBarH - 8);
    panicButton.setBounds (kPad * 2 + kButtonW, kPad, kButtonW, kTopBarH - 8);
    rewindButton.setBounds (kPad * 3 + kButtonW * 2, kPad, kButtonW, kTopBarH - 8);
    hitIndicatorBounds = { kPad * 4 + kButtonW * 3 + 8, kPad + (kTopBarH - 8 - kIndicatorSize) / 2,
                           kIndicatorSize, kIndicatorSize };

    const auto width  = getWidth();
    const auto rowH   = kKnobH + kLabelH;
    const auto totalPerRow = juce::jmax (1, (width - kPad * 2) / kKnobW);

    // Flow layout: a section starts a new band unless it fits beside the
    // previous one on a single row.
    int bandX = kPad, bandY = kPad + kTopBarH, bandH = 0;

    for (auto& section : sections)
    {
        const auto count      = (int) section.controls.size();
        const auto isCurve    = section.name == kCurveSection;
        const auto sectionW   = juce::jmin (count, totalPerRow) * kKnobW
                              + (isCurve ? kPad + kCurveEditorMinW : 0);
        const auto fitsBeside = bandX > kPad && bandX + sectionW <= width - kPad;

        if (! fitsBeside && bandX > kPad)
        {
            bandY += bandH + kPad;
            bandX  = kPad;
            bandH  = 0;
        }

        const auto perRow = juce::jmax (1, (width - bandX - kPad - (isCurve ? kPad + kCurveEditorMinW : 0)) / kKnobW);
        section.header = { bandX, bandY, juce::jmin (count, perRow) * kKnobW, kHeaderH };

        int index = 0;
        for (auto& control : section.controls)
        {
            const auto column = index % perRow;
            const auto row    = index / perRow;
            const auto x      = bandX + column * kKnobW;
            const auto top    = bandY + kHeaderH + row * rowH;

            control.label->setBounds (x, top, kKnobW, kLabelH);

            switch (control.kind)
            {
                case Kind::combo:
                    control.comp->setBounds (x + 2, top + kLabelH + (kKnobH - 24) / 2, kKnobW - 4, 24);
                    break;
                case Kind::toggle:
                    control.comp->setBounds (x + kKnobW / 2 - 12, top + kLabelH + (kKnobH - 24) / 2, 24, 24);
                    break;
                case Kind::knob:
                default:
                    control.comp->setBounds (x, top + kLabelH, kKnobW, kKnobH);
                    break;
            }

            ++index;
        }

        const auto rows     = (count + perRow - 1) / perRow;
        auto sectionH       = kHeaderH + rows * rowH;
        auto usedW          = juce::jmin (count, perRow) * kKnobW;

        if (isCurve)
        {
            const auto editorX = bandX + usedW + kPad;
            curveEditor.setBounds (editorX, bandY + kHeaderH, width - kPad - editorX, kCurveEditorH);
            sectionH = juce::jmax (sectionH, kHeaderH + kCurveEditorH);
            usedW    = width - kPad - bandX;
        }

        bandH  = juce::jmax (bandH, sectionH);
        bandX += usedW + kPad * 2;
    }
}
