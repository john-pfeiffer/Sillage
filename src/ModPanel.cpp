#include "ModPanel.h"
#include "SillageLookAndFeel.h"

namespace
{
constexpr int kNumberW = 26, kComboW = 128, kCurveW = 84, kTextBoxW = 56;
constexpr int kLfoNameW = 50, kShapeW = 104, kSyncW = 62, kDivW = 84, kPhaseW = 170, kCaptionW = 40;
} // namespace

ModPanel::ModPanel (SillageAudioProcessor& p)
    : processor (p)
{
    const auto font = ui::font (12.0f);

    const auto caption = [this] (juce::Label& label, const char* text, juce::Justification justification)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (ui::font (11.0f));
        label.setColour (juce::Label::textColourId, ui::colour::dim);
        label.setJustificationType (justification);
        addAndMakeVisible (label);
    };

    static constexpr const char* kHeaders[] { "Source", "Destination", "Amount", "Curve" };
    for (size_t h = 0; h < headers.size(); ++h)
        caption (headers[h], kHeaders[h], juce::Justification::centredLeft);

    // Attachments sync the selection; the items come from the parameter.
    const auto fill = [this] (juce::ComboBox& box, const char* id)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
            box.addItemList (choice->choices, 1);
    };

    for (size_t s = 0; s < slots.size(); ++s)
    {
        auto& slot = slots[s];

        slot.number.setText (juce::String (s + 1), juce::dontSendNotification);
        slot.number.setJustificationType (juce::Justification::centred);
        slot.number.setFont (font);
        addAndMakeVisible (slot.number);

        fill (slot.source, params::id::modSource[s]);
        fill (slot.destination, params::id::modDestination[s]);
        fill (slot.curve, params::id::modCurve[s]);
        addAndMakeVisible (slot.source);
        addAndMakeVisible (slot.destination);
        addAndMakeVisible (slot.curve);

        slot.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
        slot.amount.getProperties().set (ui::kBipolarProperty, true);
        slot.amount.setTooltip ("How far the source moves the destination: 100 % is the knob's whole range");
        addAndMakeVisible (slot.amount);

        slot.sourceAttachment      = std::make_unique<Apvts::ComboBoxAttachment> (processor.apvts, params::id::modSource[s], slot.source);
        slot.destinationAttachment = std::make_unique<Apvts::ComboBoxAttachment> (processor.apvts, params::id::modDestination[s], slot.destination);
        slot.curveAttachment       = std::make_unique<Apvts::ComboBoxAttachment> (processor.apvts, params::id::modCurve[s], slot.curve);
        slot.amountAttachment      = std::make_unique<Apvts::SliderAttachment> (processor.apvts, params::id::modAmount[s], slot.amount);
    }

    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& lfo = lfos[l];

        lfo.name.setText ("LFO " + juce::String (l + 1), juce::dontSendNotification);
        lfo.name.setFont (font);
        addAndMakeVisible (lfo.name);

        caption (lfo.rateLabel, "Rate", juce::Justification::centredRight);
        caption (lfo.phaseLabel, "Phase", juce::Justification::centredRight);

        fill (lfo.shape, params::id::lfoShape[l]);
        fill (lfo.division, params::id::lfoDivision[l]);
        addAndMakeVisible (lfo.shape);
        addAndMakeVisible (lfo.division);
        addAndMakeVisible (lfo.sync);

        lfo.rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
        lfo.phase.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
        addAndMakeVisible (lfo.rate);
        addAndMakeVisible (lfo.phase);

        lfo.shapeAttachment    = std::make_unique<Apvts::ComboBoxAttachment> (processor.apvts, params::id::lfoShape[l], lfo.shape);
        lfo.divisionAttachment = std::make_unique<Apvts::ComboBoxAttachment> (processor.apvts, params::id::lfoDivision[l], lfo.division);
        lfo.rateAttachment     = std::make_unique<Apvts::SliderAttachment> (processor.apvts, params::id::lfoRate[l], lfo.rate);
        lfo.phaseAttachment    = std::make_unique<Apvts::SliderAttachment> (processor.apvts, params::id::lfoPhase[l], lfo.phase);
        lfo.syncAttachment     = std::make_unique<Apvts::ButtonAttachment> (processor.apvts, params::id::lfoSync[l], lfo.sync);

        // A synced LFO runs at its division; a free one at its rate.
        lfo.sync.onStateChange = [this] { resized(); };
        lfo.sync.setTooltip ("Lock this LFO to the host tempo");
    }

    decayLabel.setText ("Transient Decay", juce::dontSendNotification);
    decayLabel.setFont (font);
    decay.setTooltip ("How long the Transient source takes to fall back after a hit");
    addAndMakeVisible (decayLabel);
    decay.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
    addAndMakeVisible (decay);
    decayAttachment = std::make_unique<Apvts::SliderAttachment> (processor.apvts, params::id::modTransientDecay, decay);
}

void ModPanel::paint (juce::Graphics&) {}

void ModPanel::resized()
{
    auto area = getLocalBounds();

    // Column captions sit over the columns the first slot row lays out.
    {
        auto row = area.removeFromTop (kHeaderH);
        row.removeFromLeft (kNumberW);
        headers[0].setBounds (row.removeFromLeft (kComboW).reduced (4, 0));
        headers[1].setBounds (row.removeFromLeft (kComboW).reduced (4, 0));
        headers[3].setBounds (row.removeFromRight (kCurveW).reduced (4, 0));
        headers[2].setBounds (row.reduced (4, 0));
    }

    for (auto& slot : slots)
    {
        auto row = area.removeFromTop (kRowH);
        area.removeFromTop (kRowGap);

        slot.number.setBounds (row.removeFromLeft (kNumberW));
        slot.source.setBounds (row.removeFromLeft (kComboW).reduced (2, 1));
        slot.destination.setBounds (row.removeFromLeft (kComboW).reduced (2, 1));
        slot.curve.setBounds (row.removeFromRight (kCurveW).reduced (2, 1));
        slot.amount.setBounds (row.reduced (2, 0));
    }

    for (auto& lfo : lfos)
    {
        auto row = area.removeFromTop (kRowH);
        area.removeFromTop (kRowGap);

        // Shape | Rate (or, synced, the division) | Phase. Whichever of rate
        // and division Sync has not chosen is greyed out.
        lfo.name.setBounds (row.removeFromLeft (kLfoNameW));
        lfo.shape.setBounds (row.removeFromLeft (kShapeW).reduced (2, 1));
        lfo.phase.setBounds (row.removeFromRight (kPhaseW).reduced (2, 0));
        lfo.phaseLabel.setBounds (row.removeFromRight (kCaptionW));
        lfo.division.setBounds (row.removeFromRight (kDivW).reduced (2, 1));
        lfo.sync.setBounds (row.removeFromRight (kSyncW));
        lfo.rateLabel.setBounds (row.removeFromLeft (kCaptionW));
        lfo.rate.setBounds (row.reduced (2, 0));

        const auto synced = lfo.sync.getToggleState();
        lfo.rate.setAlpha (synced ? ui::kDisabledAlpha : 1.0f);
        lfo.division.setAlpha (synced ? 1.0f : ui::kDisabledAlpha);
    }

    auto row = area.removeFromTop (kRowH);
    decayLabel.setBounds (row.removeFromLeft (kLfoNameW + kShapeW));
    decay.setBounds (row.reduced (2, 0));
}
