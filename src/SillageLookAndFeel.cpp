#include "SillageLookAndFeel.h"

namespace ui
{

juce::FontOptions font (float height, bool bold)
{
    return juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain);
}

void drawCard (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, bool live)
{
    g.setColour (colour::card);
    g.fillRoundedRectangle (bounds.toFloat(), kCardCorner);

    if (title.isNotEmpty())
    {
        g.setColour (colour::text.withAlpha (live ? 0.92f : 0.35f));
        g.setFont (font (12.5f));
        g.drawText (title, bounds.getX() + kCardPad + 2, bounds.getY() + 4, bounds.getWidth() - 2 * kCardPad - PowerDot::kSize,
                    kCardTitleH, juce::Justification::centredLeft);
    }
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::background);

    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::Label::backgroundWhenEditingColourId, colour::control);
    setColour (juce::Label::textWhenEditingColourId, colour::text);
    setColour (juce::Label::outlineWhenEditingColourId, colour::faint);

    setColour (juce::Slider::textBoxTextColourId, colour::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colour::accent.withAlpha (0.5f));
    setColour (juce::TextEditor::highlightColourId, colour::accent.withAlpha (0.5f));
    setColour (juce::TextEditor::backgroundColourId, colour::control);
    setColour (juce::TextEditor::textColourId, colour::text);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, colour::faint);
    setColour (juce::CaretComponent::caretColourId, colour::text);

    setColour (juce::ComboBox::backgroundColourId, colour::control);
    setColour (juce::ComboBox::textColourId, colour::text);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, colour::dim);

    setColour (juce::PopupMenu::backgroundColourId, colour::card);
    setColour (juce::PopupMenu::textColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::controlHot);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::text);

    setColour (juce::TextButton::buttonColourId, colour::control);
    setColour (juce::TextButton::buttonOnColourId, colour::accent);
    setColour (juce::TextButton::textColourOffId, colour::text);
    setColour (juce::TextButton::textColourOnId, colour::text);

    setColour (juce::ToggleButton::textColourId, colour::text);
    setColour (juce::TooltipWindow::backgroundColourId, colour::control);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TooltipWindow::outlineColourId, colour::faint);

    setColour (juce::TabbedComponent::backgroundColourId, colour::background);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
}

// ---- Knobs -------------------------------------------------------------------

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (5.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const auto centre = bounds.getCentre();
    const auto angle  = startAngle + position * (endAngle - startAngle);

    const auto arc = [&] (float from, float to, float r)
    {
        juce::Path path;
        path.addCentredArc (centre.x, centre.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        return path;
    };

    g.setColour (colour::track);
    g.strokePath (arc (startAngle, endAngle, radius), juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The value: from the start, or from the middle for a knob that goes both ways.
    const auto bipolar = (bool) slider.getProperties().getWithDefault (kBipolarProperty, false);
    const auto origin  = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    g.setColour (colour::text);
    if (std::abs (angle - origin) > 0.01f)
        g.strokePath (arc (origin, angle, radius), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The pointer: a line in from the arc, so the knob reads as a knob.
    const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.drawLine ({ centre + direction * (radius * 0.45f), centre + direction * radius }, 2.0f);

    // Where a mod slot has it right now.
    const auto offset = (float) slider.getProperties().getWithDefault (kModOffsetProperty, 0.0f);
    if (std::abs (offset) > 0.002f)
    {
        const auto moved      = juce::jlimit (0.0f, 1.0f, position + offset);
        const auto movedAngle = startAngle + moved * (endAngle - startAngle);
        const auto inner      = radius - 5.0f;

        g.setColour (colour::accent);
        g.strokePath (arc (angle, movedAngle, inner), juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto at = centre + juce::Point<float> (std::sin (movedAngle), -std::cos (movedAngle)) * inner;
        g.fillEllipse (at.x - 2.5f, at.y - 2.5f, 5.0f, 5.0f);
    }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                    float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearBar)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (colour::control);
        g.fillRoundedRectangle (bounds, 5.0f);

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (bounds, 5.0f);
        g.reduceClipRegion (clip);
        g.setColour (colour::track);
        g.fillRect (bounds.withRight (position));
        g.restoreState();
        return;
    }

    const auto centreY = (float) y + (float) height * 0.5f;
    const auto left = (float) x, right = (float) (x + width);
    const auto bipolar = (bool) slider.getProperties().getWithDefault (kBipolarProperty, false);
    const auto origin  = bipolar ? (left + right) * 0.5f : left;

    g.setColour (colour::track);
    g.drawLine (left, centreY, right, centreY, 1.5f);

    g.setColour (colour::text);
    g.drawLine (juce::jmin (origin, position), centreY, juce::jmax (origin, position), centreY, 2.0f);
    g.fillEllipse (position - 4.0f, centreY - 4.0f, 8.0f, 8.0f);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (font (12.0f));
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = LookAndFeel_V4::getSliderLayout (slider);

    // A bar carries its value on top of itself.
    if (slider.getSliderStyle() == juce::Slider::LinearBar)
    {
        layout.sliderBounds  = slider.getLocalBounds();
        layout.textBoxBounds = slider.getLocalBounds();
    }
    return layout;
}

// ---- Combos and menus ----------------------------------------------------------

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 5.0f);

    juce::Path chevron;
    const auto cx = (float) width - 11.0f, cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (colour::dim);
    g.strokePath (chevron, juce::PathStrokeType (1.3f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::Font (font (12.0f)); }
juce::Font LookAndFeel::getPopupMenuFont()                { return juce::Font (font (13.0f)); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

// ---- Buttons -------------------------------------------------------------------

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat();
    auto fill = button.getToggleState() ? colour::accent : (down || highlighted ? colour::controlHot : colour::control);
    if (button.getToggleState() && down)
        fill = fill.darker (0.2f);

    g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (bounds, 5.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return juce::Font (font (12.0f)); }

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (colour::text.withAlpha (button.isEnabled() ? 1.0f : 0.35f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto on = button.getToggleState();
    const auto dot = juce::Rectangle<float> (4.0f, (float) button.getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);

    if (on)
    {
        g.setColour (colour::accent);
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (highlighted ? colour::dim : colour::faint);
        g.drawEllipse (dot.reduced (0.5f), 1.2f);
    }

    g.setFont (font (12.0f));
    g.setColour (on ? colour::text : colour::dim);
    g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (17), juce::Justification::centredLeft);
}

void PowerDot::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto dot = getLocalBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    if (getToggleState())
    {
        g.setColour (colour::accent);
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (highlighted ? colour::dim : colour::faint);
        g.drawEllipse (dot.reduced (0.5f), 1.2f);
    }
}

// ---- Tabs ----------------------------------------------------------------------

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int)
{
    return juce::GlyphArrangement::getStringWidthInt (juce::Font (font (12.5f)), button.getButtonText()) + 26;
}

void LookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const auto area  = button.getActiveArea();
    const auto front = button.isFrontTab();

    g.setFont (font (12.5f));
    g.setColour (front ? colour::text : (isMouseOver ? colour::text.withAlpha (0.75f) : colour::dim));
    g.drawText (button.getButtonText(), area, juce::Justification::centred);

    if (front)
    {
        g.setColour (colour::accent);
        g.fillRect (area.getX() + 13, area.getBottom() - 3, area.getWidth() - 26, 2);
    }
}

void LookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics& g)
{
    g.fillAll (colour::background);
}

juce::Font LookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool isMouseOver, bool)
{
    g.setColour (isMouseOver ? colour::dim : colour::faint);
    for (int i = 1; i <= 3; ++i)
    {
        const auto inset = (float) i * 4.0f;
        g.drawLine ((float) width - inset, (float) height - 1.0f, (float) width - 1.0f, (float) height - inset, 1.0f);
    }
}

} // namespace ui
