#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// Modulation panel (5.9): six slot rows (Source, Destination, Amount, Curve),
// two LFO rows and the Transient decay — a table, not a matrix, so every
// routing is visible at once. Pure-function: stock combos, sliders, toggles.
class ModPanel : public juce::Component
{
public:
    explicit ModPanel (SillageAudioProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kRowH      = 26;
    static constexpr int kRowGap    = 4;
    static constexpr int kMinWidth  = 640;
    static int requiredHeight() noexcept
    {
        return (params::id::kNumModSlots + params::id::kNumLfos + 1) * (kRowH + kRowGap);
    }

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    struct Slot
    {
        juce::Label     number;
        juce::ComboBox  source, destination, curve;
        juce::Slider    amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<Apvts::ComboBoxAttachment> sourceAttachment, destinationAttachment, curveAttachment;
        std::unique_ptr<Apvts::SliderAttachment>   amountAttachment;
    };

    struct LfoRow
    {
        juce::Label        name;
        juce::ComboBox     shape, division;
        juce::Slider       rate  { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Slider       phase { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::ToggleButton sync { "Sync" };
        std::unique_ptr<Apvts::ComboBoxAttachment> shapeAttachment, divisionAttachment;
        std::unique_ptr<Apvts::SliderAttachment>   rateAttachment, phaseAttachment;
        std::unique_ptr<Apvts::ButtonAttachment>   syncAttachment;
    };

    SillageAudioProcessor& processor;
    std::array<Slot, params::id::kNumModSlots> slots;
    std::array<LfoRow, params::id::kNumLfos> lfos;

    juce::Label  decayLabel;
    juce::Slider decay { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<Apvts::SliderAttachment> decayAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModPanel)
};
