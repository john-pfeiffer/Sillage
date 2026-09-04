#include "SectionPage.h"

SectionPage::SectionPage (SillageAudioProcessor& p, std::vector<SectionSpec> specs)
    : processor (p)
{
    for (const auto& spec : specs)
    {
        Section section;
        section.name          = spec.name;
        section.extra         = spec.extra;
        section.extraMinWidth = spec.extraMinWidth;
        section.extraHeight   = spec.extraHeight;

        if (section.extra != nullptr)
            addAndMakeVisible (*section.extra);

        for (auto* paramId : spec.ids)
        {
            auto* param = processor.apvts.getParameter (paramId);
            jassert (param != nullptr);

            Control control;
            control.label = std::make_unique<juce::Label> (juce::String(), param->getName (32));
            control.label->setJustificationType (juce::Justification::centred);
            control.label->setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (*control.label);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            {
                auto box = std::make_unique<juce::ComboBox>();
                box->addItemList (choice->choices, 1);
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

int SectionPage::getRequiredHeight (int width)
{
    return layout (width, false);
}

void SectionPage::resized()
{
    layout (getWidth(), true);
}

void SectionPage::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    for (const auto& section : sections)
        g.drawText (section.name, section.header, juce::Justification::centredLeft);
}

int SectionPage::layout (int width, bool apply)
{
    const auto setBounds = [apply] (juce::Component& c, juce::Rectangle<int> r) { if (apply) c.setBounds (r); };

    const auto rowH        = kKnobH + kLabelH;
    const auto totalPerRow = juce::jmax (1, (width - kPad * 2) / kKnobW);

    // Flow layout: a section starts a new band unless it fits beside the
    // previous one. A section with an extra component fills the band.
    int bandX = kPad, bandY = kPad, bandH = 0;

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
