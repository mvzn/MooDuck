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

    static constexpr juce::uint32 kMeterClipRed     = 0xffC84030;

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
    void drawPushButtonBody  (juce::Graphics&, juce::Rectangle<float> bounds,
                              bool isOn, bool isMouseOver, bool isDown) const;
    void drawPushButtonLabel (juce::Graphics&, juce::Rectangle<float> bounds,
                              const juce::String& text,
                              bool isOn, bool isMouseOver, bool isDown) const;

public:

private:
    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr mediumTypeface;

    juce::Image grainImage; // cached brushed-grain noise overlay, generated once

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckLookAndFeel)
};
