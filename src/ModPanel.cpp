#include "ModPanel.h"

namespace
{
constexpr int kNumberW = 26, kComboW = 128, kCurveW = 84, kTextBoxW = 56;
constexpr int kLfoNameW = 50, kShapeW = 104, kSyncW = 62, kDivW = 84, kPhaseW = 170;
} // namespace

ModPanel::ModPanel (SillageAudioProcessor& p)
    : processor (p)
{
    const auto font = juce::FontOptions (12.0f);

    for (size_t s = 0; s < slots.size(); ++s)
    {
        auto& slot = slots[s];

        slot.number.setText (juce::String (s + 1), juce::dontSendNotification);
        slot.number.setJustificationType (juce::Justification::centred);
        slot.number.setFont (font);
        addAndMakeVisible (slot.number);

        addAndMakeVisible (slot.source);
        addAndMakeVisible (slot.destination);
        addAndMakeVisible (slot.curve);

        slot.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
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
    }

    decayLabel.setText ("Transient Decay", juce::dontSendNotification);
    decayLabel.setFont (font);
    addAndMakeVisible (decayLabel);
    decay.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxW, kRowH - 4);
    addAndMakeVisible (decay);
    decayAttachment = std::make_unique<Apvts::SliderAttachment> (processor.apvts, params::id::modTransientDecay, decay);
}

void ModPanel::paint (juce::Graphics&) {}

void ModPanel::resized()
{
    auto area = getLocalBounds();

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

        lfo.name.setBounds (row.removeFromLeft (kLfoNameW));
        lfo.shape.setBounds (row.removeFromLeft (kShapeW).reduced (2, 1));
        lfo.phase.setBounds (row.removeFromRight (kPhaseW).reduced (2, 0));
        lfo.division.setBounds (row.removeFromRight (kDivW).reduced (2, 1));
        lfo.sync.setBounds (row.removeFromRight (kSyncW));
        lfo.rate.setBounds (row.reduced (2, 0));
    }

    auto row = area.removeFromTop (kRowH);
    decayLabel.setBounds (row.removeFromLeft (kLfoNameW + kShapeW));
    decay.setBounds (row.reduced (2, 0));
}
