/*
  ==============================================================================

    LevelMeterComponent.cpp

    Three-strip segmented LED meter: IN, SC, GR.

  ==============================================================================
*/

#include "LevelMeterComponent.h"

namespace
{
    // METER TUNING — change these to re-scale or re-colour the three meter strips.
    // The IN/SC strips span kStripMinDb..kStripMaxDb over kStripSegments LED blocks
    // and switch colour at the kStripAmber/Clip thresholds (white -> amber -> red).
    // The GR strip spans 0..kGrMaxDb of reduction and is solid amber. Raise
    // kGrMaxDb for a meter that shows more dB of ducking before pegging. The
    // ballistics below control only how fast the GR readout reacts, not its scale.
    // The strip WIDTHS/positions are set in resized(); the segment COLOURS come
    // from the palette in MooDuckLookAndFeel.h (kAccentWhite/Amber, kMeterClipRed).

    // --- Ballistic time constants (used for the GR strip) ---------------------
    constexpr double kTimerHz       = 30.0;
    constexpr double kTimeStepSec   = 1.0 / kTimerHz;
    constexpr double kAttackTauSec  = 0.010;   // 10 ms PPM-fast attack
    constexpr double kReleaseTauSec = 1.500;   // 1500 ms release

    // --- IN / SC strip range --------------------------------------------------
    constexpr float kStripMinDb    = -48.0f;
    constexpr float kStripMaxDb    =   0.0f;
    constexpr int   kStripSegments = 16;
    constexpr float kStripAmberDb  =  -6.0f;
    constexpr float kStripClipDb   =  -1.0f;

    // --- GR strip range -------------------------------------------------------
    constexpr float kGrMinDb       =  0.0f;    // no reduction
    constexpr float kGrMaxDb       = 24.0f;    // full reduction (dB, positive)
    constexpr int   kGrSegments    = 16;
}

//==============================================================================
LevelMeterComponent::LevelMeterComponent()
{
    setOpaque(false);
    startTimerHz(static_cast<int>(kTimerHz));
}

LevelMeterComponent::~LevelMeterComponent() = default;

//==============================================================================
void LevelMeterComponent::setLevels(float inputDb, float sidechainDb, float gainReductionDb)
{
    targetInputDb     = inputDb;
    targetSidechainDb = sidechainDb;
    // GR is supplied positive (e.g. 8.0 = 8 dB of reduction).
    targetGrDb        = juce::jlimit(kGrMinDb, kGrMaxDb, gainReductionDb);
}

//==============================================================================
void LevelMeterComponent::timerCallback()
{
    // PPM-fast ballistic smoothing on the GR strip:
    //   attack (= GR increasing) is fast, release (= GR returning toward 0) is slow.
    const float diff = targetGrDb - displayedGrDb;
    const float tau  = (targetGrDb > displayedGrDb) ? (float) kAttackTauSec
                                                    : (float) kReleaseTauSec;
    const float coeff = 1.0f - std::exp(- (float) kTimeStepSec / tau);
    displayedGrDb += diff * coeff;

    // Only repaint when the display would actually change — keeps an idle meter (and
    // the panel behind this transparent component) from redrawing every tick. 0.2 dB
    // is well below one segment step, so nothing visible is dropped.
    constexpr float kEps = 0.2f;
    if (std::abs(targetInputDb     - lastDrawnInDb) > kEps
     || std::abs(targetSidechainDb - lastDrawnScDb) > kEps
     || std::abs(displayedGrDb     - lastDrawnGrDb) > kEps)
    {
        lastDrawnInDb = targetInputDb;
        lastDrawnScDb = targetSidechainDb;
        lastDrawnGrDb = displayedGrDb;
        repaint();
    }
}

//==============================================================================
void LevelMeterComponent::resized()
{
    auto r = getLocalBounds().reduced(6);

    // Three vertical strips, left-to-right: IN, SC, GR.
    // IN and SC are narrow (10 px); GR is wider (~18 px) as the primary
    // gain-reduction readout.
    constexpr int sideStripWidth = 10;
    constexpr int stripGap       = 4;
    constexpr int grStripWidth   = 18;
    constexpr int grLeftGap      = 8;

    inStripArea = r.removeFromLeft(sideStripWidth);
    r.removeFromLeft(stripGap);
    scStripArea = r.removeFromLeft(sideStripWidth);
    r.removeFromLeft(grLeftGap);
    // GR strip is centred in the remaining space, capped at grStripWidth.
    auto grColumn = r;
    const int useWidth = juce::jmin(grStripWidth, grColumn.getWidth());
    grStripArea = grColumn.withWidth(useWidth)
                          .withX(grColumn.getX() + (grColumn.getWidth() - useWidth) / 2);
}

//==============================================================================
const MooDuckLookAndFeel::Palette& LevelMeterComponent::pal() const
{
    if (auto* laf = dynamic_cast<MooDuckLookAndFeel*>(&getLookAndFeel()))
        return laf->palette();
    return MooDuckLookAndFeel::lightPalette;
}

void LevelMeterComponent::paint(juce::Graphics& g)
{
    // Background chassis fill behind the whole component.
    g.setColour(pal().chassisShadow.withAlpha(0.35f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    drawLevelStrip(g, inStripArea, targetInputDb,     "IN");
    drawLevelStrip(g, scStripArea, targetSidechainDb, "SC");
    drawGrStrip   (g, grStripArea, displayedGrDb,     "GR");
}

//==============================================================================
void LevelMeterComponent::drawLevelStrip(juce::Graphics& g,
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
    g.setColour(pal().chassisShadow);
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
        // Segment i = 0 is bottom (quietest); fills upward.
        const int y = inner.getBottom() - (i + 1) * segH + gap;
        const int h = juce::jmax(1, segH - gap);
        juce::Rectangle<int> seg(inner.getX(), y, inner.getWidth(), h);

        const float segDb = juce::jmap((float) i / (float) (kStripSegments - 1),
                                       kStripMinDb, kStripMaxDb);

        juce::Colour segColour;
        if (segDb >= kStripClipDb)
            segColour = juce::Colour(MooDuckLookAndFeel::kMeterClipRed);
        else if (segDb >= kStripAmberDb)
            segColour = juce::Colour(MooDuckLookAndFeel::kAccentAmber);
        else
            segColour = juce::Colour(MooDuckLookAndFeel::kAccentWhite);

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
    g.setColour(pal().engravedText);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawFittedText(caption, captionArea, juce::Justification::centred, 1);
}

//==============================================================================
void LevelMeterComponent::drawGrStrip(juce::Graphics& g,
                                   juce::Rectangle<int> area,
                                   float grDb,
                                   const juce::String& caption) const
{
    if (area.isEmpty())
        return;

    // Reserve a small caption strip at the bottom.
    auto stripArea = area;
    auto captionArea = stripArea.removeFromBottom(12);

    // Frame.
    g.setColour(pal().chassisShadow);
    g.fillRoundedRectangle(stripArea.toFloat(), 2.0f);

    const auto inner = stripArea.reduced(1);
    const int segH = juce::jmax(2, inner.getHeight() / kGrSegments);
    const int gap  = 1;

    // 0 dB GR → empty; full GR → fully lit from the top.
    const float norm = juce::jlimit(0.0f, 1.0f,
                                    (grDb - kGrMinDb) / (kGrMaxDb - kGrMinDb));
    const int litCount = juce::jlimit(0, kGrSegments,
                                      (int) std::round(norm * kGrSegments));

    const juce::Colour amber(MooDuckLookAndFeel::kAccentAmber);

    for (int i = 0; i < kGrSegments; ++i)
    {
        // Segment i = 0 is the TOP, growing downward as i increases.
        const int y = inner.getY() + i * segH + gap;
        const int h = juce::jmax(1, segH - gap);
        juce::Rectangle<int> seg(inner.getX(), y, inner.getWidth(), h);

        if (i < litCount)
        {
            g.setColour(amber);
            g.fillRect(seg);
        }
        else
        {
            g.setColour(amber.withAlpha(0.12f));
            g.fillRect(seg);
        }
    }

    // Caption (GR).
    g.setColour(pal().engravedText);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawFittedText(caption, captionArea, juce::Justification::centred, 1);
}
