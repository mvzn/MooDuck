/*
  ==============================================================================

    VuMeterComponent.h

    Drop-in metering component for MaybeDuck.

    Layout:
      [IN] [SC]  |   <-------  Horizontal VU dial  ------->
                 |        cream face, red needle, ~120° sweep
                 |        engraved dB scale 0/-3/-6/-12/-18/-24
                 |        digital peak readout below

    The two vertical strips on the left show IN (main input) and SC
    (sidechain) levels as segmented LEDs (white -> amber > -6 dBFS ->
    red at clip).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MaybeDuckLookAndFeel.h"

class VuMeterComponent : public juce::Component, private juce::Timer
{
public:
    VuMeterComponent();
    ~VuMeterComponent() override;

    // Called from the editor's timerCallback() with raw values read from the
    // processor's atomics. levels are in dB; gainReductionDb is positive
    // (e.g. 8.0 means -8 dB of GR — i.e. signal pulled down by 8 dB).
    void setLevels(float inputDb, float sidechainDb, float gainReductionDb);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Smoothed / ballistic state
    float displayedGrDb = 0.0f;      // PPM-smoothed value used for the needle
    float peakHoldGrDb  = 0.0f;
    int   peakHoldTicks = 0;

    float targetInputDb     = -100.0f;
    float targetSidechainDb = -100.0f;
    float targetGrDb        = 0.0f;

    // Layout rectangles computed in resized()
    juce::Rectangle<int> dialArea, peakReadoutArea, inStripArea, scStripArea;

    void drawDialFace(juce::Graphics&) const;
    void drawNeedle(juce::Graphics&, float grDb) const;
    void drawPeakReadout(juce::Graphics&, float peakDb) const;
    void drawLevelStrip(juce::Graphics&, juce::Rectangle<int> area, float dbValue,
                        const juce::String& caption) const;

    static float grDbToAngle(float grDb);    // -24..0 dB → angle in radians for needle

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeterComponent)
};
