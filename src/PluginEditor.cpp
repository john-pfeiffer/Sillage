#include "PluginEditor.h"
#include "Randomize.h"

namespace
{
// One row per UI section; extend as build phases land.
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
    { "Freeze & Chaos", { params::id::freeze, params::id::freezeFade, params::id::chaos,
                          params::id::randomizeAmount } },
    { "Transients", { params::id::sensitivity,
                      params::id::retriggerOn, params::id::retriggerCount, params::id::retriggerRate,
                      params::id::retriggerDivision, params::id::retriggerAmount, params::id::retriggerOffset,
                      params::id::duckOn, params::id::duckDepth, params::id::duckAttack, params::id::duckRelease,
                      params::id::chokeOn, params::id::chokeAmount, params::id::chokeFade,
                      params::id::envDensity, params::id::envSpread } },
    { "Sync",       { params::id::sync, params::id::grainDivision, params::id::swing } },
    { "Output",     { params::id::mix, params::id::output, params::id::fallbackBpm } },
};

constexpr int kKnobW = 76, kKnobH = 84, kLabelH = 16, kHeaderH = 22, kPad = 8;
constexpr int kTopBarH = 32, kButtonW = 96, kIndicatorSize = 14;
constexpr int kTimerHz = 30;
} // namespace

SillageAudioProcessorEditor::SillageAudioProcessorEditor (SillageAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    buildSections();

    randomizeButton.onClick = [this] { randomize(); };
    panicButton.onClick     = [this] { panic(); };
    addAndMakeVisible (randomizeButton);
    addAndMakeVisible (panicButton);

    lastOnsetCount = processor.getOnsetCount();
    startTimerHz (kTimerHz);

    setResizable (true, true);
    setResizeLimits (640, 480, 2600, 1800);
    setSize (1240, 960);
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
}

void SillageAudioProcessorEditor::panic()
{
    // Pulse the momentary parameter; the processor clears on the rising edge.
    if (auto* param = processor.apvts.getParameter (params::id::panic))
    {
        param->setValueNotifyingHost (1.0f);
        param->setValueNotifyingHost (0.0f);
    }
}

void SillageAudioProcessorEditor::timerCallback()
{
    const auto count = processor.getOnsetCount();
    if (count != lastOnsetCount)
    {
        lastOnsetCount = count;
        hitGlow = 1.0f;
    }
    else if (hitGlow > 0.0f)
    {
        hitGlow = juce::jmax (0.0f, hitGlow - 4.0f / (float) kTimerHz); // ~250 ms decay
    }
    else
    {
        return;
    }

    repaint (hitIndicatorBounds.expanded (2));
}

void SillageAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (const auto& section : sections)
        g.drawText (section.name, kPad, section.headerY, getWidth() - kPad * 2, kHeaderH,
                    juce::Justification::centredLeft);

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
    hitIndicatorBounds = { kPad * 3 + kButtonW * 2 + 8, kPad + (kTopBarH - 8 - kIndicatorSize) / 2,
                           kIndicatorSize, kIndicatorSize };

    const auto perRow = juce::jmax (1, (getWidth() - kPad * 2) / kKnobW);
    int y = kPad + kTopBarH;

    for (auto& section : sections)
    {
        section.headerY = y;
        y += kHeaderH;

        int index = 0;
        for (auto& control : section.controls)
        {
            const auto column = index % perRow;
            const auto row    = index / perRow;
            const auto x      = kPad + column * kKnobW;
            const auto top    = y + row * (kKnobH + kLabelH);

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

        const auto rows = ((int) section.controls.size() + perRow - 1) / perRow;
        y += rows * (kKnobH + kLabelH) + kPad;
    }
}
