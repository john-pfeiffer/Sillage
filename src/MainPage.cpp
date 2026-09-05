#include "MainPage.h"

namespace
{
struct KnobSpec
{
    const char* id;
    const char* title;
    const char* subtitle; // UTF-8
};

// Left to right. Damping is the loop LP under its reverb name; Shimmer is the
// shimmer amount (the interval sits on the Feedback tab, +12 by default).
const std::array<KnobSpec, MainPage::kNumKnobs> kKnobs { {
    { params::id::time,          "Time",     "ms, or a division with Sync" },
    { params::id::decay,         "Decay",    "tail length \xc2\xb7 Off = no limit" },
    { params::id::spread,        "Spread",   "delay \xe2\x86\x90 \xe2\x86\x92 reverb" },
    { params::id::feedback,      "Feedback", "over 100 % is safe" },
    { params::id::size,          "Size",     "grain length" },
    { params::id::fbLowpass,     "Damping",  "loop low-pass" },
    { params::id::shimmerAmount, "Shimmer",  "+12 in the loop" },
    { params::id::width,         "Width",    "wet stereo" },
    { params::id::mix,           "Mix",      "dry / wet" },
} };
} // namespace

MainPage::MainPage (SillageAudioProcessor& p)
    : processor (p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        const auto& spec = kKnobs[i];

        knob.title.setText (spec.title, juce::dontSendNotification);
        knob.title.setJustificationType (juce::Justification::centred);
        knob.title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (knob.title);

        knob.subtitle.setText (juce::String::fromUTF8 (spec.subtitle), juce::dontSendNotification);
        knob.subtitle.setJustificationType (juce::Justification::centred);
        knob.subtitle.setFont (juce::FontOptions (11.0f));
        knob.subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addAndMakeVisible (knob.subtitle);

        knob.knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobSize - 14, 18);
        addAndMakeVisible (knob.knob);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, spec.id, knob.knob);
    }

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (params::id::timeDivision)))
        division.addItemList (choice->choices, 1);
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (division);
    syncAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::id::timeSync, syncToggle);
    divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, params::id::timeDivision, division);

    timerCallback();
    startTimerHz (15);
}

MainPage::~MainPage()
{
    stopTimer();
}

void MainPage::timerCallback()
{
    // The division only means something while Sync is on.
    const auto synced = processor.apvts.getRawParameterValue (params::id::timeSync)->load() >= 0.5f;
    if (synced != divisionEnabled)
    {
        divisionEnabled = synced;
        division.setEnabled (synced);
        division.setAlpha (synced ? 1.0f : 0.35f);
    }
}

void MainPage::paint (juce::Graphics&) {}

void MainPage::resized()
{
    // Nine columns spread across the width, knobs centred in each, and the
    // row centred in whatever height the tallest tab gave us.
    const auto columnW  = (getWidth() - kPad * 2) / (int) knobs.size();
    const auto contentH = kTitleH + kSubtitleH + kKnobSize + kPad + kSyncRowH;
    const auto top      = juce::jmax (kPad, (getHeight() - contentH) / 2);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        const auto x  = kPad + (int) i * columnW;
        const auto cx = x + columnW / 2;

        knob.title.setBounds (x, top, columnW, kTitleH);
        knob.subtitle.setBounds (x, top + kTitleH, columnW, kSubtitleH);
        knob.knob.setBounds (cx - kKnobSize / 2, top + kTitleH + kSubtitleH, kKnobSize, kKnobSize);
    }

    // Sync toggle and division under the Time knob.
    const auto syncTop = top + kTitleH + kSubtitleH + kKnobSize + kPad;
    const auto timeX   = kPad + columnW / 2;
    syncToggle.setBounds (timeX - kKnobSize / 2, syncTop, 50, kSyncRowH);
    division.setBounds (timeX - kKnobSize / 2 + 52, syncTop + 1, kKnobSize - 52, kSyncRowH - 2);
}
