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

    // Pre-render a 256x256 tileable brushed-grain noise overlay — drawn once,
    // composited over the gradient on every chassis paint. Cheaper than
    // generating noise per-paint, and the texture is small enough to keep
    // resident.
    grainImage = juce::Image(juce::Image::ARGB, 256, 256, true);
    juce::Random rng(0xD11CC11D); // fixed seed → reproducible grain
    for (int y = 0; y < grainImage.getHeight(); ++y)
    {
        for (int x = 0; x < grainImage.getWidth(); ++x)
        {
            // Bias toward horizontal streaks: alpha varies with x more than y.
            const float a = juce::jlimit(0.0f, 1.0f,
                                         rng.nextFloat() * 0.18f - 0.07f);
            const auto tint = (rng.nextBool() ? juce::Colour(kChassisHighlight)
                                              : juce::Colour(kChassisShadow));
            grainImage.setPixelAt(x, y, tint.withAlpha(a));
        }
    }
}

void MaybeDuckLookAndFeel::drawBrushedChassis (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Vertical 3-stop gradient: highlight near top, mid in the middle, shadow at the bottom.
    juce::ColourGradient grad(
        juce::Colour(kChassisHighlight), area.getX(),      area.getY(),
        juce::Colour(kChassisShadow),    area.getX(),      area.getBottom(),
        false);
    grad.addColour(0.55, juce::Colour(kChassisMid));
    g.setGradientFill(grad);
    g.fillRect(area);

    // Composite the cached grain overlay, tiled across the area at full size.
    g.setOpacity(0.45f);
    g.setTiledImageFill(grainImage, 0, 0, 0.45f);
    g.fillRect(area);
    g.setOpacity(1.0f);

    // Soft inner vignette — slight darkening at chassis edges.
    juce::ColourGradient edge(
        juce::Colours::transparentBlack,           area.getCentreX(), area.getCentreY(),
        juce::Colour(kChassisShadow).withAlpha(0.25f), area.getX(), area.getY(),
        true);
    g.setGradientFill(edge);
    g.fillRect(area);
}

void MaybeDuckLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius,
                                      float slotAngleRadians) const
{
    juce::Rectangle<float> body(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Recessed shadow ring beneath the screw — sells the inset look.
    g.setColour(juce::Colour(kChassisShadow).withAlpha(0.55f));
    g.fillEllipse(body.expanded(1.5f).translated(0.5f, 1.0f));

    // Screw head: radial gradient, brighter toward upper-left (light from above).
    juce::ColourGradient head(
        juce::Colour(0xff8E8780), body.getX() + radius * 0.6f, body.getY() + radius * 0.6f,
        juce::Colour(0xff3E3936), body.getRight(),              body.getBottom(),
        true);
    g.setGradientFill(head);
    g.fillEllipse(body);

    // Thin dark rim.
    g.setColour(juce::Colour(0xff2A2622));
    g.drawEllipse(body, 0.6f);

    // Engraved slot.
    const float halfLen = radius * 0.75f;
    juce::Point<float> a(centre.x + std::cos(slotAngleRadians) * halfLen,
                         centre.y + std::sin(slotAngleRadians) * halfLen);
    juce::Point<float> b(centre.x - std::cos(slotAngleRadians) * halfLen,
                         centre.y - std::sin(slotAngleRadians) * halfLen);
    g.setColour(juce::Colour(kDividerShadow));
    g.drawLine(a.x, a.y, b.x, b.y, 1.4f);
    g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.35f));
    g.drawLine(a.x + 0.5f, a.y + 0.5f, b.x + 0.5f, b.y + 0.5f, 0.6f);
}

void MaybeDuckLookAndFeel::drawEngravedText (juce::Graphics& g, const juce::String& text,
                                             juce::Rectangle<float> area, juce::Justification just,
                                             juce::Font font) const
{
    g.setFont(font);

    // Highlight (top-left) — fakes a 'lifted' edge.
    g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.6f));
    g.drawText(text, area.translated(-0.6f, -0.6f), just, false);

    // Shadow (bottom-right) — fakes the 'recess'.
    g.setColour(juce::Colour(kDividerShadow).withAlpha(0.7f));
    g.drawText(text, area.translated(0.8f, 0.8f), just, false);

    // Main engraved tone.
    g.setColour(juce::Colour(kEngravedText));
    g.drawText(text, area, just, false);
}

void MaybeDuckLookAndFeel::applyRotaryConfiguration (juce::Slider& s) const
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters(kRotaryStartAngle, kRotaryEndAngle, true);
}

void MaybeDuckLookAndFeel::applyBipolarBehavior (juce::Slider& s, double centreValue) const
{
    s.getProperties().set("bipolarCentre", centreValue);
    s.setDoubleClickReturnValue(true, centreValue);
}

void MaybeDuckLookAndFeel::drawPushButtonBody (juce::Graphics& g, juce::Rectangle<float> bounds,
                                               bool isOn, bool isMouseOver, bool isDown) const
{
    // Slight inset when pressed.
    auto rect = bounds.reduced(2.0f);
    if (isDown) rect = rect.translated(0.0f, 1.0f);

    const float corner = juce::jmin(rect.getHeight() * 0.5f, 10.0f);

    // Drop shadow under the body.
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRoundedRectangle(rect.translated(0.6f, 1.6f).expanded(0.3f), corner);

    // Body fill: matte-black gradient, light from above.
    juce::ColourGradient body(
        juce::Colour(0xff333231), rect.getX(), rect.getY(),
        juce::Colour(kKnobMatteBlack), rect.getX(), rect.getBottom(),
        false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(rect, corner);

    // Hover lift: very subtle top highlight.
    if (isMouseOver)
    {
        g.setColour(juce::Colour(kAccentWhite).withAlpha(0.06f));
        g.fillRoundedRectangle(rect.removeFromTop(rect.getHeight() * 0.5f), corner);
    }

    // Thin rim.
    g.setColour(juce::Colour(0xff0A0A0A));
    g.drawRoundedRectangle(bounds.reduced(1.5f), corner, 0.8f);
    g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.10f));
    g.drawRoundedRectangle(bounds.reduced(2.5f), juce::jmax(corner - 1.0f, 1.0f), 0.6f);

    // LED dot on the left.
    const float dotR = juce::jlimit(2.5f, 4.5f, rect.getHeight() * 0.18f);
    const float dotX = rect.getX() + rect.getHeight() * 0.45f;
    const float dotY = rect.getCentreY();
    const juce::Rectangle<float> dot(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

    if (isOn)
    {
        // Bright halo + crisp core.
        g.setColour(juce::Colour(kAccentWhite).withAlpha(0.55f));
        g.fillEllipse(dot.expanded(2.0f));
        g.setColour(juce::Colour(kAccentWhite));
        g.fillEllipse(dot);
    }
    else
    {
        // Recessed dark dot when off.
        g.setColour(juce::Colour(kLedOff));
        g.fillEllipse(dot);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(dot, 0.5f);
    }
}

void MaybeDuckLookAndFeel::drawPushButtonLabel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                                const juce::String& text,
                                                bool isOn, bool /*isMouseOver*/, bool isDown) const
{
    if (text.isEmpty()) return;

    auto rect = bounds.reduced(2.0f);
    if (isDown) rect = rect.translated(0.0f, 1.0f);

    // Carve out the LED-dot column on the left.
    rect.removeFromLeft(rect.getHeight() * 0.9f);
    rect.removeFromRight(4.0f);

    const float fontSize = juce::jlimit(9.5f, 13.5f, rect.getHeight() * 0.45f);
    juce::Font font(getMediumTypeface());
    font.setHeight(fontSize);
    font.setExtraKerningFactor(0.04f);
    g.setFont(font);

    // Soft drop-shadow under the text.
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawText(text.toUpperCase(),
               rect.translated(0.0f, 0.7f).toNearestInt(),
               juce::Justification::centredLeft, false);

    // Main text — bright when on, slightly muted when off.
    const juce::Colour textColour = isOn ? juce::Colour(kAccentWhite)
                                         : juce::Colour(0xffB4ADA0);
    g.setColour(textColour);
    g.drawText(text.toUpperCase(), rect.toNearestInt(),
               juce::Justification::centredLeft, false);
}

void MaybeDuckLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool isOver, bool isDown)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const bool isOn = b.getToggleState();
    drawPushButtonBody (g, bounds, isOn, isOver, isDown);
    drawPushButtonLabel(g, bounds, b.getButtonText(), isOn, isOver, isDown);
}

void MaybeDuckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour& /*bg*/,
                                                 bool isOver, bool isDown)
{
    drawPushButtonBody(g, b.getLocalBounds().toFloat(),
                       b.getToggleState(), isOver, isDown);
}

void MaybeDuckLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                           bool isOver, bool isDown)
{
    drawPushButtonLabel(g, b.getLocalBounds().toFloat(),
                        b.getButtonText(), b.getToggleState(), isOver, isDown);
}

void MaybeDuckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos,
                                             float rotaryStart, float rotaryEnd,
                                             juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    const float discR       = radius * 0.66f;
    const float tickInnerR  = radius * 0.83f;
    const float tickOuterR  = radius * 0.94f;

    // --- Detect bipolar configuration (set via applyBipolarBehavior)
    bool isBipolar = false;
    float centreProp = 0.5f;
    {
        const auto& props = slider.getProperties();
        if (props.contains("bipolarCentre"))
        {
            const double centreValue = static_cast<double>(props["bipolarCentre"]);
            const double prop = slider.valueToProportionOfLength(centreValue);
            isBipolar = true;
            centreProp = juce::jlimit(0.0f, 1.0f, static_cast<float>(prop));
        }
    }

    auto angleAt = [&](float prop) {
        return rotaryStart + prop * (rotaryEnd - rotaryStart);
    };

    // --- Engraved chassis tick marks (11 across the 270° sweep)
    constexpr int kNumTicks = 11;
    for (int i = 0; i < kNumTicks; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
        const float angle = rotaryStart + t * (rotaryEnd - rotaryStart);

        // JUCE rotary angle = 0 at 12 o'clock, clockwise positive.
        // For x/y we want screen-standard math: 0 = right, counter-clockwise positive.
        const float screenAngle = angle - juce::MathConstants<float>::halfPi;
        const float c = std::cos(screenAngle);
        const float s = std::sin(screenAngle);

        const juce::Point<float> inner(centre.x + c * tickInnerR, centre.y + s * tickInnerR);
        const juce::Point<float> outer(centre.x + c * tickOuterR, centre.y + s * tickOuterR);

        // Engraved style: dark shadow line + faint highlight line offset.
        g.setColour(juce::Colour(kDividerShadow).withAlpha(0.85f));
        g.drawLine(inner.x, inner.y, outer.x, outer.y, 1.4f);
        g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.35f));
        g.drawLine(inner.x + 0.6f, inner.y + 0.6f,
                   outer.x + 0.6f, outer.y + 0.6f, 0.6f);
    }

    // --- Bold centre tick for bipolar sliders (drawn on top of the 11 ticks
    // since the centre may not coincide with one of them).
    if (isBipolar)
    {
        const float screenAngle = angleAt(centreProp) - juce::MathConstants<float>::halfPi;
        const float c = std::cos(screenAngle);
        const float s = std::sin(screenAngle);
        const float innerR = tickInnerR - 1.5f;
        const float outerR = tickOuterR + 1.5f;
        const juce::Point<float> inner(centre.x + c * innerR, centre.y + s * innerR);
        const juce::Point<float> outer(centre.x + c * outerR, centre.y + s * outerR);

        g.setColour(juce::Colour(kEngravedText));
        g.drawLine(inner.x, inner.y, outer.x, outer.y, 2.4f);
    }

    // --- Drop shadow under the disc
    {
        juce::Rectangle<float> shadowRect(0.0f, 0.0f, discR * 2.0f, discR * 2.0f);
        shadowRect = shadowRect.withCentre({ centre.x + 1.6f, centre.y + 2.4f });
        g.setColour(juce::Colours::black.withAlpha(0.32f));
        g.fillEllipse(shadowRect.expanded(0.5f));
    }

    // --- Matte black disc with subtle domed highlight (light coming from upper-left)
    juce::Rectangle<float> discRect(0.0f, 0.0f, discR * 2.0f, discR * 2.0f);
    discRect = discRect.withCentre(centre);

    {
        juce::ColourGradient domeGrad(
            juce::Colour(0xff3C3936),                 // highlight peak (upper-left)
            centre.x - discR * 0.35f, centre.y - discR * 0.45f,
            juce::Colour(kKnobMatteBlack),            // body tone (lower-right)
            centre.x + discR * 0.55f, centre.y + discR * 0.55f,
            true);
        g.setGradientFill(domeGrad);
        g.fillEllipse(discRect);
    }

    // Inner rim for separation against the chassis.
    g.setColour(juce::Colour(0xff0A0A0A));
    g.drawEllipse(discRect, 1.0f);
    g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.12f));
    g.drawEllipse(discRect.reduced(0.8f), 0.6f);

    // --- Bipolar fill arc: thin ring just outside the disc, filled from centre to current value.
    if (isBipolar)
    {
        const float arcRadius = discR * 1.10f;
        const float startAngle = angleAt(centreProp);
        const float endAngle   = angleAt(sliderPos);

        // Heuristic for "heavy": >75 % of available deflection from centre.
        const float deflection    = std::abs(sliderPos - centreProp);
        const float maxDeflection = juce::jmax(centreProp, 1.0f - centreProp);
        const float deflectionRatio = (maxDeflection > 0.0001f) ? deflection / maxDeflection : 0.0f;
        const bool  heavy = deflectionRatio > 0.75f;

        juce::Path arc;
        if (endAngle >= startAngle)
            arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              startAngle, endAngle, true);
        else
            arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              endAngle, startAngle, true);

        g.setColour(juce::Colour(heavy ? kAccentAmber : kAccentWhite));
        g.strokePath(arc, juce::PathStrokeType(2.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // --- White V-notch indicator at sliderPos
    const float indicatorAngle = rotaryStart + sliderPos * (rotaryEnd - rotaryStart);

    {
        // Build the V-notch as a triangular path in canonical orientation (pointing up),
        // then rotate it to the indicator angle and translate to disc centre.
        const float notchOuter = discR * 0.95f;
        const float notchInner = discR * 0.45f;
        const float halfWidth  = juce::jlimit(1.2f, 4.0f, discR * 0.10f);

        juce::Path notch;
        notch.startNewSubPath(-halfWidth, -notchOuter);  // top-left of the wide outer edge
        notch.lineTo         ( halfWidth, -notchOuter);  // top-right
        notch.lineTo         ( 0.0f,      -notchInner);  // converges toward centre
        notch.closeSubPath();

        const auto transform = juce::AffineTransform::rotation(indicatorAngle)
                                                     .translated(centre.x, centre.y);

        // Soft inner glow first (sells the recessed-paint look).
        g.setColour(juce::Colour(kAccentWhite).withAlpha(0.18f));
        g.fillPath(notch, transform.scaled(1.5f, 1.5f).translated(centre.x * -0.5f,
                                                                  centre.y * -0.5f));

        g.setColour(juce::Colour(kAccentWhite));
        g.fillPath(notch, transform);
    }
}
