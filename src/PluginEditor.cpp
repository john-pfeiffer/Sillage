#include "PluginEditor.h"

namespace
{
// One row per UI section; extend as build phases land.
const std::vector<std::pair<const char*, std::vector<const char*>>> kSections = {
    { "Output", { params::id::mix, params::id::output, params::id::fallbackBpm } },
};

constexpr int kKnobW = 84, kKnobH = 96, kLabelH = 16, kHeaderH = 22, kPad = 8;
} // namespace

SillageAudioProcessorEditor::SillageAudioProcessorEditor (SillageAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    buildSections();

    setResizable (true, true);
    setResizeLimits (420, 240, 2400, 1600);
    setSize (640, 320);
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

            Control c;
            c.label = std::make_unique<juce::Label> (juce::String(), param->getName (32));
            c.label->setJustificationType (juce::Justification::centred);
            c.label->setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (*c.label);

            if (dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr)
            {
                auto box = std::make_unique<juce::ComboBox>();
                addAndMakeVisible (*box);
                c.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    processor.apvts, paramId, *box);
                c.comp = std::move (box);
            }
            else
            {
                auto slider = std::make_unique<juce::Slider> (
                    juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW - 8, 16);
                addAndMakeVisible (*slider);
                c.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    processor.apvts, paramId, *slider);
                c.comp = std::move (slider);
            }

            section.controls.push_back (std::move (c));
        }

        sections.push_back (std::move (section));
    }
}

void SillageAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    // Section headers are painted where resized() left room for them.
    int y = kPad;
    const int perRow = juce::jmax (1, (getWidth() - kPad * 2) / kKnobW);

    for (const auto& section : sections)
    {
        g.drawText (section.name, kPad, y, getWidth() - kPad * 2, kHeaderH,
                    juce::Justification::centredLeft);
        const int rows = ((int) section.controls.size() + perRow - 1) / perRow;
        y += kHeaderH + rows * (kKnobH + kLabelH) + kPad;
    }
}

void SillageAudioProcessorEditor::resized()
{
    const int perRow = juce::jmax (1, (getWidth() - kPad * 2) / kKnobW);
    int y = kPad;

    for (auto& section : sections)
    {
        y += kHeaderH;
        int i = 0;
        for (auto& control : section.controls)
        {
            const int col = i % perRow, row = i / perRow;
            const int x  = kPad + col * kKnobW;
            const int cy = y + row * (kKnobH + kLabelH);

            control.label->setBounds (x, cy, kKnobW, kLabelH);
            if (dynamic_cast<juce::ComboBox*> (control.comp.get()) != nullptr)
                control.comp->setBounds (x + 2, cy + kLabelH + (kKnobH - 24) / 2, kKnobW - 4, 24);
            else
                control.comp->setBounds (x, cy + kLabelH, kKnobW, kKnobH);
            ++i;
        }
        const int rows = ((int) section.controls.size() + perRow - 1) / perRow;
        y += rows * (kKnobH + kLabelH) + kPad;
    }
}
