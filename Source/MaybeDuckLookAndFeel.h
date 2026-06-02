#pragma once

#include <JuceHeader.h>

class MaybeDuckLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MaybeDuckLookAndFeel();
    ~MaybeDuckLookAndFeel() override = default;

    // ============================ HOW TO RESTYLE ============================
    // PALETTE — this is the single place to change every colour in the plugin.
    // Each value is a 32-bit ARGB hex literal: 0xAARRGGBB
    //   AA = opacity (ff = opaque), RR/GG/BB = red/green/blue, 00..ff each.
    // e.g. 0xffE8A23E -> fully opaque, R=E8 G=A2 B=3E (the amber accent).
    // Change a constant here and everything that references it updates at once
    // (knobs, meters, buttons, engraved text all pull from these). To recolour
    // just one widget instead, edit its draw method in MaybeDuckLookAndFeel.cpp.
    //
    //   kChassis*  : the brushed-metal background gradient (top->bottom).
    //   kDivider*  : engraved section divider lines + tick highlights/shadows.
    //   kKnobMatteBlack / kAccentWhite / kAccentAmber : knob body + indicators.
    //   kAccentAmber doubles as the "hot"/over-threshold colour on meters.
    //   kEngravedText : the carved-label tone used for titles and knob labels.
    //   kLed* : the little on/off dot on toggle + push buttons.
    //   kMeterClipRed : the red clip-segment colour on the level-meter strips.
    // ========================================================================
    // --- Palette (warm brushed nickel + matte black + white/amber accents)
    static constexpr juce::uint32 kChassisHighlight = 0xffC7BFB3;
    static constexpr juce::uint32 kChassisMid       = 0xffA8A097;
    static constexpr juce::uint32 kChassisShadow    = 0xff6E6863;
    static constexpr juce::uint32 kDividerHighlight = 0xffD0C8BC;
    static constexpr juce::uint32 kDividerShadow    = 0xff5A5450;

    static constexpr juce::uint32 kKnobMatteBlack   = 0xff1A1A1A;
    static constexpr juce::uint32 kAccentWhite      = 0xffFFFFFF;
    static constexpr juce::uint32 kAccentAmber      = 0xffE8A23E;
    static constexpr juce::uint32 kEngravedText     = 0xffEFE7D7;

    static constexpr juce::uint32 kLedOff           = 0xff5A5450;
    static constexpr juce::uint32 kLedOn            = 0xffFFFFFF;

    static constexpr juce::uint32 kMeterClipRed     = 0xffCC2A2A; // proper red (was orange-leaning C84030)

    // --- 1176-style push-button palette (brushed-metal face, recessed socket).
    // The face is a touch lighter than the chassis so the buttons read as raised
    // hardware caps; the socket is the dark hole they sit in.
    static constexpr juce::uint32 kButtonFaceHi = 0xffCEC7BB; // button face, top (lit from above)
    static constexpr juce::uint32 kButtonFaceLo = 0xff968E84; // button face, bottom
    static constexpr juce::uint32 kButtonSocket = 0xff322E2B; // recessed socket the cap sits in

    // --- Band-switch backlight colours (silver = active, blue = solo, red = mute/bypass)
    // Semantic state colours — kept theme-independent (same in light + dark).
    static constexpr juce::uint32 kBandSolo = 0xff3E8EE6; // SOLO backlight (blue)
    static constexpr juce::uint32 kBandMute = 0xffCC2A2A; // MUTE / BYPASS backlight (proper red)

    // ---- Runtime light/dark palette ------------------------------------------
    // The constants above are the canonical LIGHT values (and still feed the level
    // meter). Dark mode swaps to darkPalette at runtime via setDarkMode()
    // (triple-click the wordmark, persisted by ThemeState). The LookAndFeel draw
    // methods read the active `pal`; band/meter state colours stay constant.
    struct Palette
    {
        juce::Colour chassisHighlight, chassisMid, chassisShadow;
        juce::Colour dividerHighlight, dividerShadow;
        juce::Colour knobMatteBlack, accentWhite, accentAmber, engravedText;
        juce::Colour buttonFaceHi, buttonFaceLo, buttonSocket;
        juce::Colour tickColor;        // knob tick-ring stroke (must read on a dark panel)
        juce::Colour knobUnderGlow;    // LED-like halo around the knob (dark mode; transparent in light)
    };
    static const Palette lightPalette;
    static const Palette darkPalette;

    const Palette& palette() const noexcept { return pal; }
    bool isDarkMode()        const noexcept { return dark; }
    void setDarkMode (bool shouldBeDark);   // swaps palette, re-applies colours, regenerates grain

    juce::Typeface::Ptr getRegularTypeface() const { return regularTypeface; }
    juce::Typeface::Ptr getMediumTypeface()  const { return mediumTypeface; }

    // --- Rotary angles: classic 270° travel, 7 o'clock to 5 o'clock.
    // Angles are radians measured clockwise from 12 o'clock per JUCE convention.
    // Want a different knob sweep? Change these two values (in radians). Common
    // choices: ±135° (270° sweep, default below) or ±150° (300° sweep ≈ ±2.618).
    // They feed applyRotaryConfiguration() and drawRotarySlider() together, so
    // the indicator and the engraved tick marks always stay in agreement.
    static constexpr float kRotaryStartAngle = -2.35619449f; // -135°
    static constexpr float kRotaryEndAngle   =  2.35619449f; // +135°

    // --- Draw helpers (used by the editor paint() and overridden draw methods)
    void drawBrushedChassis (juce::Graphics&, juce::Rectangle<float> area) const;
    // Paints a recessed rack-unit module inset into the chassis (darker brushed
    // face, inset bevel, side mounting rails + rack screws). Draw behind a
    // section's controls (e.g. the Dynamics group) in the editor's paint().
    void drawRackInset      (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawScrew          (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngleRadians) const;
    void drawEngravedText   (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                             juce::Justification just, juce::Font font) const;

    // Set sliding behavior + 270° rotary angles on a slider — call from the editor
    // for every rotary so the drawRotarySlider override sees the right angle range.
    void applyRotaryConfiguration (juce::Slider&) const;

    // Mark a slider as bipolar (with a meaningful 'centre' value). drawRotarySlider
    // will draw a bold centre tick + a fill arc from centre to the current value,
    // shifting to amber once deflection exceeds ~75 % of either side.
    // Also enables double-click-to-reset to the centre.
    void applyBipolarBehavior (juce::Slider&, double centreValue) const;

    // --- LookAndFeel overrides
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawGroupComponentOutline (juce::Graphics&, int width, int height,
                                    const juce::String& text,
                                    const juce::Justification& position,
                                    juce::GroupComponent&) override;

private:
    // 1176-style hardware push button. `litColour` is the backlight tint when the
    // button is lit; `softProfile` = the low-relief momentary band-switch variant
    // (always backlit in `litColour`) vs the chunky latching cap (lit only when on).
    void drawPushButtonBody  (juce::Graphics&, juce::Rectangle<float> bounds,
                              bool isOn, bool isMouseOver, bool isDown, bool isEnabled,
                              juce::Colour litColour, bool softProfile) const;
    void drawPushButtonLabel (juce::Graphics&, juce::Rectangle<float> bounds,
                              const juce::String& text,
                              bool isOn, bool isMouseOver, bool isDown, bool isEnabled,
                              bool softProfile) const;

    // Pull the optional per-button styling a caller may have stashed in the
    // component properties: "softSwitch" (bool) and "litColour" (ARGB uint32).
    static juce::Colour litColourFor (const juce::Button&);
    static bool         isSoftSwitch (const juce::Button&);

    // Geometry of an extruded button cap: the recessed socket, the seated base
    // footprint (at the socket floor) and the raised top face. Walls are the band
    // between base and top (front + right, top-left light). Shared by the body and
    // label draws so the legend always lands on the top face.
    struct CapGeom { juce::Rectangle<float> socket, base, top; float corner; };
    CapGeom computeCapGeometry (juce::Rectangle<float> bounds, bool pressedLook, bool softProfile) const;

public:

private:
    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr mediumTypeface;

    juce::Image grainImage;    // cached brushed-grain noise overlay (regenerated on theme switch)
    juce::Image metalCapImage; // cached spun-aluminium knob-cap disc, generated once

    Palette pal;          // active palette (set in the constructor / setDarkMode)
    bool    dark = false; // current theme
    void applyPaletteColours();  // pushes palette tones into the slider value-box colour IDs
    void regenerateGrain();      // rebuilds grainImage from the active chassis tones

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckLookAndFeel)
};
