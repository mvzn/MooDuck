/*
  ==============================================================================

    LevelMeterComponent.h

    Three vertical segmented LED meter strips for MooDuck.

    Layout:
      [IN] [SC] [ ----- GR ----- ]

    IN  : main input level (white -> amber > -6 dBFS -> red at clip), fills upward.
    SC  : sidechain level, same scale and colouring as IN, fills upward.
    GR  : gain reduction, solid amber throughout, fills DOWNWARD from the top.
          Range 0..24 dB of reduction maps to empty..full.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MooDuckLookAndFeel.h"

class LevelMeterComponent : public juce::Component, private juce::Timer
{
public:
    LevelMeterComponent();
    ~LevelMeterComponent() override;

    // Called from the editor's timerCallback() with raw values read from the
    // processor's atomics. inputDb / sidechainDb are in dBFS (negative). The
    // gainReductionDb argument is positive (e.g. 8.0 means -8 dB of GR).
    void setLevels(float inputDb, float sidechainDb, float gainReductionDb);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Ballistic state for the GR strip (PPM-fast attack, slow release).
    float displayedGrDb = 0.0f;

    float targetInputDb     = -100.0f;
    float targetSidechainDb = -100.0f;
    float targetGrDb        = 0.0f;

    // Last values actually painted — the timer only repaints when one moves enough
    // to change the display, so an idle meter costs nothing.
    float lastDrawnInDb = -1000.0f, lastDrawnScDb = -1000.0f, lastDrawnGrDb = -1000.0f;

    // Layout rectangles computed in resized().
    juce::Rectangle<int> inStripArea, scStripArea, grStripArea;

    void drawLevelStrip(juce::Graphics&, juce::Rectangle<int> area, float dbValue,
                        const juce::String& caption) const;

    // GR strip is solid amber and fills from the top down — separate drawer.
    void drawGrStrip(juce::Graphics&, juce::Rectangle<int> area, float grDb,
                     const juce::String& caption) const;

    // Active theme palette, pulled from the editor's LookAndFeel so the meter
    // follows dark/light. Falls back to the light palette if the LAF isn't ours.
    const MooDuckLookAndFeel::Palette& pal() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeterComponent)
};
