#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LifetimeCurves.h"
#include "PluginProcessor.h"

// Lifetime Curve editor (5.6 B). Pure-function: a destination picker, an
// enable toggle, and a plain XY panel. Click to add a point, drag to move,
// double-click to delete, drag a segment's midpoint up or down to bend it.
// First and last points stay pinned to x = 0 and x = 1.
class CurveEditor : public juce::Component,
                    private juce::ValueTree::Listener
{
public:
    explicit CurveEditor (SillageAudioProcessor& processor);
    ~CurveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Drag { none, point, bend };

    lifetime::Curve& current() noexcept { return set.curves[(size_t) destination]; }
    juce::Rectangle<float> panel() const noexcept;
    juce::Point<float> toScreen (float x, float y) const noexcept;
    juce::Point<float> toCurve (juce::Point<float> screen) const noexcept;
    int   nearestPoint (juce::Point<float> screen, float radius) const noexcept;
    int   nearestSegment (juce::Point<float> screen, float radius) const noexcept;
    void  selectDestination (int newDestination);
    void  reloadFromProcessor();
    void  commit();

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

    SillageAudioProcessor& processor;
    lifetime::CurveSet set;
    int destination = (int) lifetime::Destination::level;

    juce::ComboBox destinationBox;
    juce::ToggleButton enableToggle { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;

    Drag  drag       = Drag::none;
    int   dragIndex  = -1;
    float dragStartY = 0.0f;
    float dragStartBend = 0.0f;
    bool  writing    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
