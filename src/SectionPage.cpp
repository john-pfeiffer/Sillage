#include "SectionPage.h"

namespace
{
constexpr int kTimerHz = 20;
} // namespace

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
        section.gate          = spec.gate;
        section.power         = spec.power;
        section.note          = spec.note != nullptr ? juce::String (spec.note) : juce::String();

        if (section.extra != nullptr)
            addAndMakeVisible (*section.extra);

        if (section.power != nullptr)
        {
            section.dot = std::make_unique<ui::PowerDot>();
            if (auto* param = processor.apvts.getParameter (section.power))
                section.dot->setTooltip (param->getName (32) + ": on / off");
            addAndMakeVisible (*section.dot);
            section.dotAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                processor.apvts, section.power, *section.dot);
        }

        for (const auto& controlSpec : spec.controls)
        {
            auto* param = processor.apvts.getParameter (controlSpec.id);
            jassert (param != nullptr);

            Control control;
            control.id    = controlSpec.id;
            control.needs = controlSpec.needs;
            // On a card named for them, "Rewind Length" and "Duck Depth" are
            // "Length" and "Depth".
            auto name = param->getName (32);
            if (name.startsWith (section.name + " ") && name.length() > section.name.length() + 1)
                name = name.substring (section.name.length() + 1);

            control.label = std::make_unique<juce::Label> (juce::String(), name);
            control.label->setJustificationType (juce::Justification::centred);
            control.label->setFont (ui::font (11.5f));
            control.label->setColour (juce::Label::textColourId, ui::colour::dim);
            control.label->setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (*control.label);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            {
                auto box = std::make_unique<juce::ComboBox>();
                box->addItemList (choice->choices, 1);
                addAndMakeVisible (*box);
                control.kind = Kind::combo;
                control.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    processor.apvts, controlSpec.id, *box);
                control.comp = std::move (box);
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            {
                auto button = std::make_unique<juce::ToggleButton>();
                addAndMakeVisible (*button);
                control.kind = Kind::toggle;
                control.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    processor.apvts, controlSpec.id, *button);
                control.comp = std::move (button);
            }
            else
            {
                auto slider = std::make_unique<juce::Slider> (
                    juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW, 16);
                if (param->getNormalisableRange().start < 0.0f)
                    slider->getProperties().set (ui::kBipolarProperty, true);
                addAndMakeVisible (*slider);
                control.kind = Kind::knob;
                control.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    processor.apvts, controlSpec.id, *slider);
                control.comp = std::move (slider);
            }

            section.controls.push_back (std::move (control));
        }

        sections.push_back (std::move (section));
    }

    refresh();
    startTimerHz (kTimerHz);
}

SectionPage::~SectionPage()
{
    stopTimer();
}

bool SectionPage::isOn (const char* parameterId) const noexcept
{
    return ui::holds (processor.apvts, ui::Condition::on (parameterId));
}

// A control is live when its stage is on, its card is on, and everything it
// needs holds. The card's own switch only answers to the stage.
void SectionPage::refresh()
{
    bool repaintCards = false;

    for (auto& section : sections)
    {
        const auto gateOn = isOn (section.gate);
        const auto live   = gateOn && isOn (section.power);

        if (live != section.live)
        {
            section.live = live;
            repaintCards = true;
        }

        if (section.dot != nullptr)
        {
            // The switch that leads a stage is always live; a feature's own
            // switch answers to its stage.
            const auto leadsStage = section.gate == nullptr || std::strcmp (section.gate, section.power) == 0;
            const auto dotLive    = leadsStage || gateOn;
            section.dot->setEnabled (dotLive);
            section.dot->setAlpha (dotLive ? 1.0f : ui::kDisabledAlpha);
        }

        for (auto& control : section.controls)
        {
            const auto enabled = live && ui::allHold (processor.apvts, control.needs);
            if (enabled != control.enabled)
            {
                control.enabled = enabled;
                control.comp->setEnabled (enabled);
                control.comp->setAlpha (enabled ? 1.0f : ui::kDisabledAlpha);
                control.label->setAlpha (enabled ? 1.0f : ui::kDisabledAlpha);
            }

            // Where a mod slot has this knob right now.
            if (control.kind == Kind::knob)
            {
                const auto offset = processor.getModOffset (control.id);
                if (std::abs (offset - control.modOffset) > 0.002f)
                {
                    control.modOffset = offset;
                    control.comp->getProperties().set (ui::kModOffsetProperty, offset);
                    control.comp->repaint();
                }
            }
        }

        if (section.extra != nullptr && live != section.extraEnabled)
        {
            section.extraEnabled = live;
            section.extra->setEnabled (live);
            section.extra->setAlpha (live ? 1.0f : ui::kDisabledAlpha);
        }
    }

    if (repaintCards)
        repaint();
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
    for (const auto& section : sections)
    {
        ui::drawCard (g, section.card, section.name, section.live);

        if (section.note.isNotEmpty())
        {
            g.setColour (ui::colour::dim.withAlpha (section.live ? 1.0f : 0.5f));
            g.setFont (ui::font (11.5f));
            g.drawFittedText (section.note,
                              section.card.reduced (ui::kCardPad + 2, 0).withTrimmedTop (ui::kCardTitleH + 4)
                                          .withTrimmedBottom (ui::kCardPad),
                              juce::Justification::topLeft, 4);
        }
    }
}

int SectionPage::layout (int width, bool apply)
{
    const auto setBounds = [apply] (juce::Component& c, juce::Rectangle<int> r) { if (apply) c.setBounds (r); };

    const auto rowH   = kLabelH + kKnobH;
    const auto inside = [] (int cardWidth) { return cardWidth - 2 * ui::kCardPad; };

    // Flow layout: a card starts a new band unless it fits beside the
    // previous one. A card with an extra component fills the rest of its band.
    int bandX = kPad, bandY = kPad, bandH = 0;
    size_t bandStart = 0;

    const auto closeBand = [&] (size_t end)
    {
        // Cards in a band are all as tall as the tallest.
        if (apply)
            for (size_t i = bandStart; i < end; ++i)
                sections[i].card.setHeight (bandH);
        bandStart = end;
    };

    for (size_t s = 0; s < sections.size(); ++s)
    {
        auto& section = sections[s];
        const auto count    = (int) section.controls.size();
        const auto hasExtra = section.extra != nullptr;
        const auto noteOnly = count == 0 && ! hasExtra && section.note.isNotEmpty();

        const auto maxPerRow = juce::jmax (1, inside (width - 2 * kPad) / kKnobW);
        const auto wanted    = juce::jmax (noteOnly ? 2 : 1, juce::jmin (count, maxPerRow)) * kKnobW + 2 * ui::kCardPad
                             + (hasExtra ? (count > 0 ? kGap : -kKnobW) + section.extraMinWidth : 0);

        if (bandX > kPad && bandX + wanted > width - kPad)
        {
            closeBand (s);
            bandY += bandH + kGap;
            bandX  = kPad;
            bandH  = 0;
        }

        const auto cardW  = hasExtra ? width - kPad - bandX : juce::jmin (wanted, width - kPad - bandX);
        const auto knobsW = hasExtra ? juce::jmin (count, juce::jmax (1, (inside (cardW) - section.extraMinWidth - kGap) / kKnobW)) * kKnobW
                                     : inside (cardW);
        const auto perRow = juce::jmax (1, knobsW / kKnobW);
        const auto rows   = count > 0 ? (count + perRow - 1) / perRow : 0;

        const auto contentX = bandX + ui::kCardPad;
        const auto contentY = bandY + ui::kCardTitleH + 4;

        int index = 0;
        for (auto& control : section.controls)
        {
            const auto cx  = contentX + (index % perRow) * kKnobW;
            const auto top = contentY + (index / perRow) * rowH;

            setBounds (*control.label, { cx, top, kKnobW, kLabelH });

            switch (control.kind)
            {
                case Kind::combo:
                    setBounds (*control.comp, { cx + 3, top + kLabelH + (kKnobH - 24) / 2, kKnobW - 6, 24 });
                    break;
                case Kind::toggle:
                    setBounds (*control.comp, { cx + kKnobW / 2 - 8, top + kLabelH + (kKnobH - 20) / 2, 20, 20 });
                    break;
                case Kind::knob:
                default:
                    setBounds (*control.comp, { cx, top + kLabelH, kKnobW, kKnobH });
                    break;
            }

            ++index;
        }

        auto cardH = ui::kCardTitleH + 4 + juce::jmax (rows, noteOnly ? 1 : 0) * rowH + ui::kCardPad;

        if (hasExtra)
        {
            const auto extraX = contentX + (count > 0 ? juce::jmin (count, perRow) * kKnobW + kGap : 0);
            setBounds (*section.extra, { extraX, contentY, bandX + cardW - ui::kCardPad - extraX, section.extraHeight });
            cardH = juce::jmax (cardH, ui::kCardTitleH + 4 + section.extraHeight + ui::kCardPad);
        }

        if (apply)
        {
            section.card = { bandX, bandY, cardW, cardH };
            if (section.dot != nullptr)
                section.dot->setBounds (bandX + cardW - ui::kCardPad - ui::PowerDot::kSize + 4, bandY + 6,
                                        ui::PowerDot::kSize, ui::PowerDot::kSize);
        }

        bandH  = juce::jmax (bandH, cardH);
        bandX += cardW + kGap;
    }

    closeBand (sections.size());
    return bandY + bandH + kPad;
}
