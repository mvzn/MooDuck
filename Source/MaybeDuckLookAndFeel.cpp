#include "MaybeDuckLookAndFeel.h"

MaybeDuckLookAndFeel::MaybeDuckLookAndFeel()
{
    regularTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexSansCondensedRegular_ttf,
        BinaryData::IBMPlexSansCondensedRegular_ttfSize);

    mediumTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexSansCondensedMedium_ttf,
        BinaryData::IBMPlexSansCondensedMedium_ttfSize);

    setDefaultSansSerifTypeface(regularTypeface);
}
