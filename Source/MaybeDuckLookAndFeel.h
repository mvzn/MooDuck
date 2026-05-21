#pragma once

#include <JuceHeader.h>

class MaybeDuckLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MaybeDuckLookAndFeel();
    ~MaybeDuckLookAndFeel() override = default;

    // --- Palette (warm brushed nickel + matte black + white/amber accents)
    static constexpr juce::uint32 kChassisHighlight = 0xffC7BFB3;
    static constexpr juce::uint32 kChassisMid       = 0xffA8A097;
    static constexpr juce::uint32 kChassisShadow    = 0xff6E6863;
    static constexpr juce::uint32 kDividerHighlight = 0xffD0C8BC;
    static constexpr juce::uint32 kDividerShadow    = 0xff5A5450;

    static constexpr juce::uint32 kKnobMatteBlack   = 0xff1A1A1A;
    static constexpr juce::uint32 kAccentWhite      = 0xffFFFFFF;
    static constexpr juce::uint32 kAccentAmber      = 0xffE8A23E;
    static constexpr juce::uint32 kEngravedText     = 0xff3C3633;

    static constexpr juce::uint32 kLedOff           = 0xff5A5450;
    static constexpr juce::uint32 kLedOn            = 0xffFFFFFF;

    static constexpr juce::uint32 kVuDialCream      = 0xffEDE3CC;
    static constexpr juce::uint32 kVuNeedleRed      = 0xffC84030;

    juce::Typeface::Ptr getRegularTypeface() const { return regularTypeface; }
    juce::Typeface::Ptr getMediumTypeface()  const { return mediumTypeface; }

private:
    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr mediumTypeface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckLookAndFeel)
};
