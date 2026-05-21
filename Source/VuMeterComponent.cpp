/*
  ==============================================================================

    VuMeterComponent.cpp

    Implementation of the VU dial + IN/SC LED-strip meters.

  ==============================================================================
*/

#include "VuMeterComponent.h"

namespace
{
    // --- Ballistic time constants ---------------------------------------------
    constexpr double kTimerHz          = 60.0;
    constexpr double kTimeStepSec      = 1.0 / kTimerHz;
    constexpr double kAttackTauSec     = 0.010;    // 10 ms PPM-fast attack
    constexpr double kReleaseTauSec    = 1.500;    // 1500 ms release
    constexpr int    kPeakHoldTicks    = 90;       // ~1.5 s @ 60 Hz

    // --- Dial geometry --------------------------------------------------------
    // The needle sweeps a horizontal arc.  0 dB GR (no ducking) sits on the
    // right at +60° measured from the upward axis; -24 dB GR (heavy ducking)
    // sits on the left at -60°.  Total sweep = 120°.
    constexpr float kSweepHalfDegrees = 60.0f;
    constexpr float kDegToRad         = juce::MathConstants<float>::pi / 180.0f;

    // --- Level-strip range ----------------------------------------------------
    constexpr float kStripMinDb       = -48.0f;
    constexpr float kStripMaxDb       =   0.0f;
    constexpr int   kStripSegments    = 16;
    constexpr float kStripAmberDb     =  -6.0f;
    constexpr float kStripClipDb      =  -1.0f;

    // --- GR meter range -------------------------------------------------------
    constexpr float kGrMinDb          = -24.0f;
    constexpr float kGrMaxDb          =   0.0f;
    constexpr float kGrAmberZoneDb    = -12.0f;
}

//==============================================================================
VuMeterComponent::VuMeterComponent()
{
    setOpaque(false);
    startTimerHz(static_cast<int>(kTimerHz));
}

VuMeterComponent::~VuMeterComponent() = default;

//==============================================================================
void VuMeterComponent::setLevels(float inputDb, float sidechainDb, float gainReductionDb)
{
    // Store as targets — the timer applies the ballistic smoothing.
    targetInputDb     = inputDb;
    targetSidechainDb = sidechainDb;
    // gainReductionDb is positive when ducking (e.g. 8 -> -8 dB of GR).
    // Convert to a negative dB value so the needle maps cleanly to the dial
    // (which is laid out from 0 dB on the right down to -24 dB on the left).
    targetGrDb        = -juce::jlimit(0.0f, 24.0f, gainReductionDb);
}

//==============================================================================
void VuMeterComponent::timerCallback()
{
    // --- PPM-fast ballistic smoothing on the needle --------------------------
    const float diff = targetGrDb - displayedGrDb;
    const float tau  = (targetGrDb < displayedGrDb) ? (float) kAttackTauSec
                                                    : (float) kReleaseTauSec;
    // Note: "attack" of the needle = needle moving further LEFT (more GR ->
    // more negative dB value). Release = needle returning to 0 dB on the right.
    const float coeff = 1.0f - std::exp(- (float) kTimeStepSec / tau);
    displayedGrDb += diff * coeff;

    // --- Peak hold (most-ducked value) --------------------------------------
    // peakHoldGrDb tracks the lowest (most-negative) value seen recently.
    if (targetGrDb < peakHoldGrDb)
    {
        peakHoldGrDb  = targetGrDb;
        peakHoldTicks = kPeakHoldTicks;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
    }
    else
    {
        // Decay back toward the displayed value.
        peakHoldGrDb += (displayedGrDb - peakHoldGrDb) * 0.05f;
    }

    repaint();
}

//==============================================================================
void VuMeterComponent::resized()
{
    auto r = getLocalBounds().reduced(6);

    // Carve off the two strips on the left.
    constexpr int stripWidth   = 10;
    constexpr int stripGap     = 4;
    constexpr int leftBlockGap = 8;

    auto leftBlock = r.removeFromLeft(stripWidth * 2 + stripGap);
    inStripArea = leftBlock.removeFromLeft(stripWidth);
    leftBlock.removeFromLeft(stripGap);
    scStripArea = leftBlock.removeFromLeft(stripWidth);

    r.removeFromLeft(leftBlockGap);

    // Remaining region splits into the dial (top) and peak readout (bottom).
    peakReadoutArea = r.removeFromBottom(juce::jmin(22, r.getHeight() / 4));
    dialArea        = r;
}

//==============================================================================
void VuMeterComponent::paint(juce::Graphics& g)
{
    // Background chassis fill behind the whole component.
    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kChassisShadow).withAlpha(0.35f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    drawDialFace(g);
    drawNeedle(g, displayedGrDb);
    drawPeakReadout(g, peakHoldGrDb);

    drawLevelStrip(g, inStripArea, targetInputDb,     "IN");
    drawLevelStrip(g, scStripArea, targetSidechainDb, "SC");
}

//==============================================================================
float VuMeterComponent::grDbToAngle(float grDb)
{
    // grDb is negative (0 dB = no GR, -24 dB = heavy GR).
    // Map -24 dB -> -60°, 0 dB -> +60°.  120° sweep total.
    const float clamped = juce::jlimit(kGrMinDb, kGrMaxDb, grDb);
    const float norm    = (clamped - kGrMinDb) / (kGrMaxDb - kGrMinDb); // 0..1
    const float deg     = juce::jmap(norm, 0.0f, 1.0f, -kSweepHalfDegrees, kSweepHalfDegrees);
    return deg * kDegToRad;
}

//==============================================================================
void VuMeterComponent::drawDialFace(juce::Graphics& g) const
{
    if (dialArea.isEmpty())
        return;

    auto face = dialArea.toFloat().reduced(4.0f);

    // Pivot is below the bottom-centre of the face so that the arc sweeps
    // above it.  We use a virtual radius slightly larger than the face so
    // the arc fills the upper portion nicely.
    const float pivotX = face.getCentreX();
    const float pivotY = face.getBottom() - 2.0f;
    const float radius = juce::jmin(face.getWidth() * 0.5f, face.getHeight()) * 1.05f;

    // Cream dial face — drawn as a filled pie slice (a sector) so it reads
    // like a classic VU window.
    {
        juce::Path face_path;
        const float startAngle = -kSweepHalfDegrees * kDegToRad;
        const float endAngle   =  kSweepHalfDegrees * kDegToRad;
        face_path.addPieSegment(pivotX - radius, pivotY - radius,
                                radius * 2.0f, radius * 2.0f,
                                startAngle, endAngle, 0.0f);
        g.setColour(juce::Colour(MaybeDuckLookAndFeel::kVuDialCream));
        g.fillPath(face_path);

        // Subtle border.
        g.setColour(juce::Colour(MaybeDuckLookAndFeel::kEngravedText).withAlpha(0.35f));
        g.strokePath(face_path, juce::PathStrokeType(1.0f));
    }

    // Engraved dB scale.  Tick at each labelled value; amber tint past -12.
    static constexpr float kScaleDbs[] = { 0.0f, -3.0f, -6.0f, -12.0f, -18.0f, -24.0f };

    const float tickOuter = radius * 0.96f;
    const float tickInner = radius * 0.86f;
    const float labelRad  = radius * 0.78f;

    g.setFont(juce::Font(10.0f, juce::Font::bold));

    for (float dbMark : kScaleDbs)
    {
        const float angle = grDbToAngle(dbMark);   // radians, 0 = straight up
        // In JUCE's screen coords (+y down), to draw a tick rotating around
        // the pivot we use sin/cos with angle measured from "up".
        const float sinA = std::sin(angle);
        const float cosA = std::cos(angle);

        const auto p1 = juce::Point<float>(pivotX + sinA * tickOuter,
                                           pivotY - cosA * tickOuter);
        const auto p2 = juce::Point<float>(pivotX + sinA * tickInner,
                                           pivotY - cosA * tickInner);
        const auto pl = juce::Point<float>(pivotX + sinA * labelRad,
                                           pivotY - cosA * labelRad);

        const bool inAmber = (dbMark < kGrAmberZoneDb);
        const juce::Colour tickColour = inAmber
            ? juce::Colour(MaybeDuckLookAndFeel::kAccentAmber)
            : juce::Colour(MaybeDuckLookAndFeel::kEngravedText);

        g.setColour(tickColour);
        g.drawLine({ p1, p2 }, 1.5f);

        // Label centred at pl.
        const juce::String label = juce::String(static_cast<int>(dbMark));
        const auto textBox = juce::Rectangle<float>(20.0f, 12.0f).withCentre(pl);
        g.drawFittedText(label, textBox.toNearestInt(),
                         juce::Justification::centred, 1);
    }

    // Small dark pivot circle.
    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
    g.fillEllipse(pivotX - 3.5f, pivotY - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
void VuMeterComponent::drawNeedle(juce::Graphics& g, float grDb) const
{
    if (dialArea.isEmpty())
        return;

    auto face = dialArea.toFloat().reduced(4.0f);
    const float pivotX = face.getCentreX();
    const float pivotY = face.getBottom() - 2.0f;
    const float radius = juce::jmin(face.getWidth() * 0.5f, face.getHeight()) * 1.05f;

    const float angle  = grDbToAngle(grDb);
    const float length = radius * 0.92f;

    const float sinA = std::sin(angle);
    const float cosA = std::cos(angle);

    const juce::Point<float> tip  { pivotX + sinA * length,
                                    pivotY - cosA * length };
    const juce::Point<float> root { pivotX, pivotY };

    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kVuNeedleRed));
    g.drawLine({ root, tip }, 2.0f);

    // Pivot cap drawn on top.
    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
    g.fillEllipse(pivotX - 2.5f, pivotY - 2.5f, 5.0f, 5.0f);
}

//==============================================================================
void VuMeterComponent::drawPeakReadout(juce::Graphics& g, float peakDb) const
{
    if (peakReadoutArea.isEmpty())
        return;

    // peakDb is negative (e.g. -8.4); show with one decimal.
    const float shown = juce::jlimit(kGrMinDb, kGrMaxDb, peakDb);
    const juce::String text = juce::String(shown, 1) + " dB";

    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawFittedText(text, peakReadoutArea, juce::Justification::centred, 1);
}

//==============================================================================
void VuMeterComponent::drawLevelStrip(juce::Graphics& g,
                                      juce::Rectangle<int> area,
                                      float dbValue,
                                      const juce::String& caption) const
{
    if (area.isEmpty())
        return;

    // Reserve a small caption strip at the bottom.
    auto stripArea = area;
    auto captionArea = stripArea.removeFromBottom(12);

    // Frame.
    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kChassisShadow));
    g.fillRoundedRectangle(stripArea.toFloat(), 2.0f);

    const auto inner = stripArea.reduced(1);
    const int segH = juce::jmax(2, inner.getHeight() / kStripSegments);
    const int gap  = 1;

    // Determine how many segments are lit.
    const float norm = juce::jlimit(0.0f, 1.0f,
                                    (dbValue - kStripMinDb) / (kStripMaxDb - kStripMinDb));
    const int litCount = juce::jlimit(0, kStripSegments,
                                      (int) std::round(norm * kStripSegments));

    for (int i = 0; i < kStripSegments; ++i)
    {
        // Segment i = 0 is bottom (quietest).
        const int y = inner.getBottom() - (i + 1) * segH + gap;
        const int h = juce::jmax(1, segH - gap);
        juce::Rectangle<int> seg(inner.getX(), y, inner.getWidth(), h);

        const float segDb = juce::jmap((float) i / (float) (kStripSegments - 1),
                                       kStripMinDb, kStripMaxDb);

        juce::Colour segColour;
        if (segDb >= kStripClipDb)
            segColour = juce::Colour(MaybeDuckLookAndFeel::kVuNeedleRed);
        else if (segDb >= kStripAmberDb)
            segColour = juce::Colour(MaybeDuckLookAndFeel::kAccentAmber);
        else
            segColour = juce::Colour(MaybeDuckLookAndFeel::kAccentWhite);

        if (i < litCount)
        {
            g.setColour(segColour);
            g.fillRect(seg);
        }
        else
        {
            g.setColour(segColour.withAlpha(0.12f));
            g.fillRect(seg);
        }
    }

    // Caption (IN / SC).
    g.setColour(juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawFittedText(caption, captionArea, juce::Justification::centred, 1);
}
