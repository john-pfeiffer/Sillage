#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// The look: near-black, rounded cards, thin line art, white for what is live
// and one red for what is on. Pure-function still holds (EVS standard): a
// knob is a knob — an arc and a pointer — a switch is a dot that lights, and
// nothing is drawn that does not show a value.
namespace ui
{

namespace colour
{
    inline const juce::Colour background { 0xff0b0b0b };
    inline const juce::Colour card       { 0xff171717 };
    inline const juce::Colour control    { 0xff222222 };
    inline const juce::Colour controlHot { 0xff2e2e2e };
    inline const juce::Colour track      { 0xff343434 };
    inline const juce::Colour text       { 0xffececec };
    inline const juce::Colour dim        { 0xff7a7a7a };
    inline const juce::Colour faint      { 0xff454545 };
    inline const juce::Colour accent     { 0xffe5392d };
}

inline constexpr float kCardCorner     = 10.0f;
inline constexpr float kDisabledAlpha  = 0.3f;

// Slider properties the look reads. "mod": the normalised offset a mod slot
// is putting on the parameter right now (absent or 0 for none). "bipolar":
// the arc grows from the middle rather than the start.
inline const juce::Identifier kModOffsetProperty { "sillageModOffset" };
inline const juce::Identifier kBipolarProperty   { "sillageBipolar" };

juce::FontOptions font (float height, bool bold = false);

// A card: the rounded panel every group of controls sits on, with its title.
void drawCard (juce::Graphics&, juce::Rectangle<int> bounds, const juce::String& title, bool live = true);

// Where a card's content starts below its title.
inline constexpr int kCardTitleH = 22;
inline constexpr int kCardPad    = 10;

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    int  getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}

    juce::Font getLabelFont (juce::Label&) override;
    void drawCornerResizer (juce::Graphics&, int width, int height, bool isMouseOver, bool isMouseDragging) override;
};

// A stage's On switch: the dot at the top right of its card. Red when on.
class PowerDot : public juce::Button
{
public:
    PowerDot() : juce::Button ({}) { setClickingTogglesState (true); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    static constexpr int kSize = 18;
};

} // namespace ui
