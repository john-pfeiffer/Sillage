#include "PluginEditor.h"

namespace
{
// One row per UI section; extend as build phases land.
const std::vector<std::pair<const char*, std::vector<const char*>>> kSections = {
    { "Grain",    { params::id::time, params::id::timeSync, params::id::timeDivision,
                    params::id::density, params::id::spread, params::id::size,
                    params::id::window } },
    { "Pitch",    { params::id::pitch, params::id::pitchFine, params::id::pitchSpread,
                    params::id::quantize, params::id::quantizeRoot,
                    params::id::panSpread, params::id::reverse } },
    { "Feedback", { params::id::feedback, params::id::fbHighpass, params::id::fbLowpass,
                    params::id::fbResonance, params::id::shimmerInterval,
                    params::id::shimmerAmount, params::id::shimmerFine,
                    params::id::diffuse, params::id::satType, params::id::drive } },
    { "Output",   { params::id::mix, params::id::output, params::id::fallbackBpm } },
};

constexpr int kKnobW = 84, kKnobH = 96, kLabelH = 16, kHeaderH = 22, kPad = 8;
} // namespace

SillageAudioProcessorEditor::SillageAudioProcessorEditor (SillageAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    buildSections();

    setResizable (true, true);
    setResizeLimits (520, 360, 2400, 1600);
    setSize (900, 620);
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

void SillageAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (const auto& section : sections)
        g.drawText (section.name, kPad, section.headerY, getWidth() - kPad * 2, kHeaderH,
                    juce::Justification::centredLeft);
}

void SillageAudioProcessorEditor::resized()
{
    const auto perRow = juce::jmax (1, (getWidth() - kPad * 2) / kKnobW);
    int y = kPad;

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
