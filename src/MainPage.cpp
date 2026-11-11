#include "MainPage.h"

namespace
{
namespace id = params::id;
using ui::Condition;

struct KnobSpec
{
    const char* id;
    const char* title;
    const char* hint;
    std::vector<Condition> needs;
};

// Left to right, card by card. Damping is the loop low-pass under its reverb
// name; Shimmer is the shimmer amount (the interval is on the Feedback tab).
// `needs` is what has to hold for the knob to be doing anything.
const std::array<KnobSpec, MainPage::kNumKnobs>& knobSpecs()
{
    static const std::array<KnobSpec, MainPage::kNumKnobs> specs { {
        { id::time,     "Time",     "When the sound arrives: the echo, or the cloud's pre-delay. Sync takes it from the tempo",
                                    { Condition::off (id::timeSync) } },
        { id::decay,    "Decay",    "How long the tail lasts. All the way up is Off: Feedback alone decides", {} },
        { id::spread,   "Spread",   "From an echo at Time to a cloud behind it: delay to reverb", {} },
        { id::feedback, "Feedback", "How much of the tail the loop rebuilds. Over 100 % it outlasts Decay", {} },

        { id::density,  "Density",  "Grains per second. Grain Sync on the Grain tab takes it from the tempo",
                                    { Condition::off (id::sync) } },
        { id::size,     "Size",     "Grain length", { Condition::unlessBoth (id::curveEnable[4], id::ageOn) } },
        { id::pitch,    "Pitch",    "Grain pitch in semitones", {} },

        { id::fbLowpass,     "Damping", "The loop's low-pass: every trip round loses more top",
                                        { Condition::unlessBoth (id::curveEnable[0], id::ageOn) } },
        { id::shimmerAmount, "Shimmer", "Pitch shift in the loop, so it stacks every trip. The interval is on the Feedback tab",
                                        { Condition::on (id::loopOn) } },
        { id::chaos,         "Chaos",   "Slow random drift of position, pitch, density and feedback",
                                        { Condition::on (id::chaosOn) } },

        { id::width, "Width", "Stereo width of the wet signal", {} },
        { id::mix,   "Mix",   "Dry / wet", {} },
    } };
    return specs;
}

struct CardSpec { const char* name; int numKnobs; };
constexpr std::array<CardSpec, 4> kCards { { { "Space", 4 }, { "Grain", 3 }, { "Colour", 3 }, { "Output", 2 } } };
} // namespace

MainPage::MainPage (SillageAudioProcessor& p, const ui::MeterFeed& feed)
    : processor (p), tailView (feed), cloudView (p), loopView (p), levelMeter (feed, false)
{
    juce::Component* visuals[] { &tailView, &cloudView, &loopView, &levelMeter };
    int first = 0;
    for (size_t c = 0; c < cards.size(); ++c)
    {
        cards[c].name      = kCards[c].name;
        cards[c].firstKnob = first;
        cards[c].numKnobs  = kCards[c].numKnobs;
        cards[c].visual    = visuals[c];
        addAndMakeVisible (*visuals[c]);
        first += kCards[c].numKnobs;
    }

    const auto& specs = knobSpecs();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.id    = specs[i].id;
        knob.needs = specs[i].needs;

        knob.title.setText (specs[i].title, juce::dontSendNotification);
        knob.title.setJustificationType (juce::Justification::centred);
        knob.title.setFont (ui::font (11.5f));
        knob.title.setColour (juce::Label::textColourId, ui::colour::dim);
        addAndMakeVisible (knob.title);

        knob.knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kColumnW, 16);
        knob.knob.setTooltip (specs[i].hint);
        addAndMakeVisible (knob.knob);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, knob.id, knob.knob);

        if (auto* parameter = processor.apvts.getParameter (knob.id))
            if (parameter->getNormalisableRange().start < 0.0f)
                knob.knob.getProperties().set (ui::kBipolarProperty, true);
    }

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id::timeDivision)))
        division.addItemList (choice->choices, 1);
    syncToggle.setTooltip ("Take Time from the host tempo");
    addAndMakeVisible (syncToggle);
    addAndMakeVisible (division);
    syncAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, id::timeSync, syncToggle);
    divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, id::timeDivision, division);

    timerCallback();
    startTimerHz (30);
}

MainPage::~MainPage()
{
    stopTimer();
}

void MainPage::timerCallback()
{
    // The division only means something while Sync is on.
    const auto synced = processor.apvts.getRawParameterValue (id::timeSync)->load() >= 0.5f;
    if (synced != divisionEnabled)
    {
        divisionEnabled = synced;
        division.setEnabled (synced);
        division.setAlpha (synced ? 1.0f : ui::kDisabledAlpha);
    }

    for (auto& knob : knobs)
    {
        // A knob the current state has taken over is greyed out, not hidden:
        // it still shows what it is set to.
        const auto enabled = ui::allHold (processor.apvts, knob.needs);
        if (enabled != knob.enabled)
        {
            knob.enabled = enabled;
            knob.knob.setAlpha (enabled ? 1.0f : ui::kDisabledAlpha);
            knob.title.setAlpha (enabled ? 1.0f : ui::kDisabledAlpha);
        }

        const auto offset = processor.getModOffset (knob.id);
        if (std::abs (offset - knob.modOffset) > 0.002f)
        {
            knob.modOffset = offset;
            knob.knob.getProperties().set (ui::kModOffsetProperty, offset);
            knob.knob.repaint();
        }
    }

    if (! isShowing())
        return;

    tailView.repaint();
    levelMeter.repaint();

    if (const auto signature = cloudView.signature(); signature != cloudSignature)
    {
        cloudSignature = signature;
        cloudView.repaint();
    }
    if (const auto signature = loopView.signature(); signature != loopSignature)
    {
        loopSignature = signature;
        loopView.repaint();
    }
}

void MainPage::paint (juce::Graphics& g)
{
    for (const auto& card : cards)
        ui::drawCard (g, card.bounds, card.name);
}

void MainPage::resized()
{
    // Four cards in a row, as wide as their knobs and as tall as the page:
    // whatever height the tallest tab gave it goes to the pictures.
    const auto top     = kPad;
    const auto cardH   = juce::jmax (cardHeight(), getHeight() - 2 * kPad);
    const auto visualH = kVisualH + (cardH - cardHeight());
    int x = kPad;

    for (auto& card : cards)
    {
        const auto width = card.numKnobs * kColumnW + 2 * ui::kCardPad;
        card.bounds = { x, top, width, cardH };

        const auto inner = card.bounds.reduced (ui::kCardPad, 0);
        const auto visualTop = top + ui::kCardTitleH;
        card.visual->setBounds (inner.getX(), visualTop, inner.getWidth(), visualH);

        const auto labelTop = visualTop + visualH + 6;
        for (int k = 0; k < card.numKnobs; ++k)
        {
            auto& knob = knobs[(size_t) (card.firstKnob + k)];
            const auto kx = inner.getX() + k * kColumnW;
            knob.title.setBounds (kx, labelTop, kColumnW, kLabelH);
            knob.knob.setBounds (kx, labelTop + kLabelH, kColumnW, kKnobH);
        }

        x += width + kGap;
    }

    // Sync and its division under the Time knob.
    const auto& space = cards[0].bounds;
    const auto syncTop = space.getBottom() - ui::kCardPad - kSyncRowH;
    syncToggle.setBounds (space.getX() + ui::kCardPad, syncTop, 56, kSyncRowH);
    division.setBounds (space.getX() + ui::kCardPad + 58, syncTop + 1, 70, kSyncRowH - 2);
}
