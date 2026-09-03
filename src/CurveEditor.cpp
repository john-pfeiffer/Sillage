#include "CurveEditor.h"

namespace
{
constexpr int   kBarH        = 24;
constexpr float kPointRadius = 5.0f;
constexpr float kHitRadius   = 9.0f;
constexpr int   kCurveSteps  = 128;
} // namespace

CurveEditor::CurveEditor (SillageAudioProcessor& p)
    : processor (p)
{
    for (int d = 0; d < lifetime::kNumDestinations; ++d)
        destinationBox.addItem (lifetime::kDestinationNames[(size_t) d], d + 1);
    destinationBox.onChange = [this] { selectDestination (destinationBox.getSelectedId() - 1); };
    addAndMakeVisible (destinationBox);
    addAndMakeVisible (enableToggle);

    reloadFromProcessor();
    destinationBox.setSelectedId (destination + 1, juce::dontSendNotification);
    selectDestination (destination);

    processor.apvts.state.addListener (this);
}

CurveEditor::~CurveEditor()
{
    processor.apvts.state.removeListener (this);
}

void CurveEditor::selectDestination (int newDestination)
{
    destination = juce::jlimit (0, lifetime::kNumDestinations - 1, newDestination);
    enableAttachment.reset();
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, params::id::curveEnable[(size_t) destination], enableToggle);
    repaint();
}

void CurveEditor::reloadFromProcessor()
{
    set = processor.getCurves();
    repaint();
}

void CurveEditor::commit()
{
    current().sortPoints();
    writing = true;
    processor.setCurves (set);
    writing = false;
    repaint();
}

void CurveEditor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (! writing && (child.getType() == lifetime::kCurvesType || child.getType() == lifetime::kCurveType))
        reloadFromProcessor();
}

void CurveEditor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (! writing && child.getType() == lifetime::kCurvesType)
        reloadFromProcessor();
}

void CurveEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (! writing && tree.getType() == lifetime::kPointType)
        reloadFromProcessor();
}

// ---- Geometry ---------------------------------------------------------------

juce::Rectangle<float> CurveEditor::panel() const noexcept
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) kBarH + 4.0f).reduced (kPointRadius + 2.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const noexcept
{
    const auto area = panel();
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

juce::Point<float> CurveEditor::toCurve (juce::Point<float> screen) const noexcept
{
    const auto area = panel();
    return { juce::jlimit (0.0f, 1.0f, (screen.x - area.getX()) / juce::jmax (1.0f, area.getWidth())),
             juce::jlimit (0.0f, 1.0f, (area.getBottom() - screen.y) / juce::jmax (1.0f, area.getHeight())) };
}

int CurveEditor::nearestPoint (juce::Point<float> screen, float radius) const noexcept
{
    const auto& curve = set.curves[(size_t) destination];
    int best = -1;
    float bestDistance = radius;
    for (int i = 0; i < curve.numPoints; ++i)
    {
        const auto distance = toScreen (curve.points[(size_t) i].x, curve.points[(size_t) i].y).getDistanceFrom (screen);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int CurveEditor::nearestSegment (juce::Point<float> screen, float radius) const noexcept
{
    const auto& curve = set.curves[(size_t) destination];
    int best = -1;
    float bestDistance = radius;
    for (int i = 0; i + 1 < curve.numPoints; ++i)
    {
        const auto& a = curve.points[(size_t) i];
        const auto& b = curve.points[(size_t) i + 1];
        const auto midX = (a.x + b.x) * 0.5f;
        const auto mid  = toScreen (midX, curve.evaluate (midX));
        const auto distance = mid.getDistanceFrom (screen);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

// ---- Interaction -------------------------------------------------------------

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto screen = e.position;
    if (! panel().expanded (kHitRadius).contains (screen))
        return;

    auto& curve = current();

    if (const auto index = nearestPoint (screen, kHitRadius); index >= 0)
    {
        drag      = Drag::point;
        dragIndex = index;
        return;
    }

    if (const auto segment = nearestSegment (screen, kHitRadius); segment >= 0)
    {
        drag          = Drag::bend;
        dragIndex     = segment;
        dragStartY    = screen.y;
        dragStartBend = curve.points[(size_t) segment].bend;
        return;
    }

    if (curve.numPoints < lifetime::kMaxPoints)
    {
        const auto pos = toCurve (screen);
        curve.points[(size_t) curve.numPoints] = { pos.x, pos.y, 0.0f };
        ++curve.numPoints;
        curve.sortPoints();
        drag      = Drag::point;
        dragIndex = nearestPoint (screen, kHitRadius * 2.0f);
        repaint();
    }
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    auto& curve = current();

    if (drag == Drag::point && dragIndex >= 0 && dragIndex < curve.numPoints)
    {
        auto& point = curve.points[(size_t) dragIndex];
        const auto pos = toCurve (e.position);
        point.y = pos.y;

        // End points stay pinned; inner points may not cross their neighbours.
        if (dragIndex > 0 && dragIndex < curve.numPoints - 1)
            point.x = juce::jlimit (curve.points[(size_t) dragIndex - 1].x + 0.005f,
                                    curve.points[(size_t) dragIndex + 1].x - 0.005f, pos.x);
        repaint();
    }
    else if (drag == Drag::bend && dragIndex >= 0 && dragIndex + 1 < curve.numPoints)
    {
        const auto delta = (dragStartY - e.position.y) / juce::jmax (1.0f, panel().getHeight());
        auto& segmentStart = curve.points[(size_t) dragIndex];
        const auto rising  = curve.points[(size_t) dragIndex + 1].y >= segmentStart.y;
        // Dragging up always pushes the curve up, whichever way the segment goes.
        segmentStart.bend = juce::jlimit (-1.0f, 1.0f, dragStartBend + (rising ? -delta : delta) * 2.0f);
        repaint();
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::none)
        commit();
    drag      = Drag::none;
    dragIndex = -1;
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto& curve = current();
    const auto index = nearestPoint (e.position, kHitRadius);
    if (index <= 0 || index >= curve.numPoints - 1 || curve.numPoints <= 2)
        return;

    for (int i = index; i + 1 < curve.numPoints; ++i)
        curve.points[(size_t) i] = curve.points[(size_t) i + 1];
    --curve.numPoints;
    drag = Drag::none;
    commit();
}

// ---- Drawing -----------------------------------------------------------------

void CurveEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (kBarH);
    destinationBox.setBounds (bar.removeFromLeft (150));
    bar.removeFromLeft (8);
    enableToggle.setBounds (bar.removeFromLeft (60));
}

void CurveEditor::paint (juce::Graphics& g)
{
    const auto area  = panel();
    const auto& curve = set.curves[(size_t) destination];
    const auto enabled = enableToggle.getToggleState();

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRect (area);

    // Quarter grid.
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (int i = 1; i < 4; ++i)
    {
        const auto fx = area.getX() + area.getWidth() * (float) i / 4.0f;
        const auto fy = area.getY() + area.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine ((int) fx, area.getY(), area.getBottom());
        g.drawHorizontalLine ((int) fy, area.getX(), area.getRight());
    }

    // The curve itself.
    juce::Path path;
    for (int i = 0; i <= kCurveSteps; ++i)
    {
        const auto x = (float) i / (float) kCurveSteps;
        const auto point = toScreen (x, curve.evaluate (x));
        if (i == 0) path.startNewSubPath (point);
        else        path.lineTo (point);
    }
    g.setColour (juce::Colours::white.withAlpha (enabled ? 0.9f : 0.35f));
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // Points and bend handles.
    for (int i = 0; i < curve.numPoints; ++i)
    {
        const auto p = toScreen (curve.points[(size_t) i].x, curve.points[(size_t) i].y);
        g.setColour (juce::Colours::white.withAlpha (enabled ? 0.95f : 0.5f));
        g.fillEllipse (p.x - kPointRadius, p.y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);

        if (i + 1 < curve.numPoints)
        {
            const auto midX = (curve.points[(size_t) i].x + curve.points[(size_t) i + 1].x) * 0.5f;
            const auto mid  = toScreen (midX, curve.evaluate (midX));
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawEllipse (mid.x - 3.0f, mid.y - 3.0f, 6.0f, 6.0f, 1.0f);
        }
    }

    // Age readout: where the live tail currently sits on this curve.
    const auto age = processor.getAverageAgeNormalised();
    const auto marker = toScreen (age, curve.evaluate (age));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawVerticalLine ((int) marker.x, area.getY(), area.getBottom());

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("age 0", (int) area.getX() + 3, (int) area.getBottom() - 14, 60, 12, juce::Justification::left);
    g.drawText ("Lifetime", (int) area.getRight() - 63, (int) area.getBottom() - 14, 60, 12, juce::Justification::right);
}
