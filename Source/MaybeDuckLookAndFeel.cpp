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

    // Knob value-readout box: a dark matte plate (matching the knob bodies) with
    // a thin warm-grey hairline border, so the engraved off-white number stays
    // legible. These map onto the slider's internal value Label/TextEditor, so
    // every rotary picks them up without per-slider setColour calls.
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(kKnobMatteBlack)); // #1A1A1A plate
    setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(kEngravedText));   // border = same off-white as the number
    setColour(juce::Slider::textBoxTextColourId,       juce::Colour(kEngravedText));   // off-white number
    setColour(juce::Slider::textBoxHighlightColourId,  juce::Colour(kAccentAmber).withAlpha(0.35f)); // edit selection

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
            // Bumped from 0.18f -> 0.26f so individual streaks read from a metre away
            // without becoming wallpaper-textured at close range.
            const float a = juce::jlimit(0.0f, 1.0f,
                                         rng.nextFloat() * 0.26f - 0.07f);
            const auto tint = (rng.nextBool() ? juce::Colour(kChassisHighlight)
                                              : juce::Colour(kChassisShadow));
            grainImage.setPixelAt(x, y, tint.withAlpha(a));
        }
    }

    // Pre-render the spun-aluminium top cap once (drawn into every knob's metal
    // cap). A "brushed-from-centre" finish: a silver radial base, many faint
    // radial streaks, then a soft upper-left light catch + lower-right shade.
    // The cap is the ONLY metallic part — the skirt and body stay matte black.
    {
        constexpr int capSize = 256;
        metalCapImage = juce::Image(juce::Image::ARGB, capSize, capSize, true);
        juce::Graphics cg(metalCapImage);

        const juce::Point<float> c(capSize * 0.5f, capSize * 0.5f);
        const float R = capSize * 0.5f - 1.0f;
        auto disc = juce::Rectangle<float>(R * 2.0f, R * 2.0f).withCentre(c);

        // Base radial gradient: bright centre -> mid-grey edge.
        juce::ColourGradient base(juce::Colour(0xffE9E9EC), c.x, c.y,
                                  juce::Colour(0xff8C8E92), c.x, c.y + R, true);
        cg.setGradientFill(base);
        cg.fillEllipse(disc);

        // Keep the streaks inside the disc.
        juce::Path clip; clip.addEllipse(disc);
        cg.reduceClipRegion(clip);

        juce::Random crng(0x5A1AD0);
        constexpr int streaks = 220;
        for (int i = 0; i < streaks; ++i)
        {
            const float a  = (static_cast<float>(i) / streaks) * juce::MathConstants<float>::twoPi
                             + crng.nextFloat() * 0.02f;
            const float r0 = R * (0.05f + crng.nextFloat() * 0.05f);
            const float r1 = R * (0.92f + crng.nextFloat() * 0.08f);
            cg.setColour((crng.nextBool() ? juce::Colours::white : juce::Colours::black)
                            .withAlpha(0.025f + crng.nextFloat() * 0.05f));
            cg.drawLine(c.x + std::cos(a) * r0, c.y + std::sin(a) * r0,
                        c.x + std::cos(a) * r1, c.y + std::sin(a) * r1, 0.8f);
        }

        // Directional sheen (upper-left) + shade (lower-right) for a little dome.
        juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.30f),
                                   c.x - R * 0.5f, c.y - R * 0.55f,
                                   juce::Colours::transparentWhite,
                                   c.x + R * 0.15f, c.y + R * 0.15f, true);
        cg.setGradientFill(sheen);
        cg.fillEllipse(disc);

        juce::ColourGradient shade(juce::Colours::transparentBlack, c.x, c.y,
                                   juce::Colours::black.withAlpha(0.22f),
                                   c.x + R * 0.5f, c.y + R * 0.6f, true);
        cg.setGradientFill(shade);
        cg.fillEllipse(disc);
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
    // Tile opacity bumped 0.45 -> 0.55 in tandem with the denser per-pixel alpha
    // so the streaks read at distance without becoming wallpaper-textured.
    g.setOpacity(0.55f);
    g.setTiledImageFill(grainImage, 0, 0, 0.55f);
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
                                               bool isOn, bool isMouseOver, bool isDown, bool isEnabled) const
{
    // Slight inset when pressed.
    auto rect = bounds.reduced(2.0f);
    if (isDown) rect = rect.translated(0.0f, 1.0f);

    const float corner = juce::jmin(rect.getHeight() * 0.5f, 10.0f);

    // Drop shadow under the body.
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillRoundedRectangle(rect.translated(0.6f, 1.6f).expanded(0.3f), corner);

    // Body fill: matte-black gradient, light from above. When disabled, mix the
    // stops toward the chassis-shadow tone so it reads inert (same as the knobs).
    const auto bodyTop = isEnabled ? juce::Colour(0xff333231)
                                   : juce::Colour(0xff333231).interpolatedWith(juce::Colour(kChassisShadow), 0.5f);
    const auto bodyBot = isEnabled ? juce::Colour(kKnobMatteBlack)
                                   : juce::Colour(kKnobMatteBlack).interpolatedWith(juce::Colour(kChassisShadow), 0.45f);
    juce::ColourGradient body(
        bodyTop, rect.getX(), rect.getY(),
        bodyBot, rect.getX(), rect.getBottom(),
        false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(rect, corner);

    // Hover lift: very subtle top highlight.
    // NB: use withHeight (non-mutating) — removeFromTop would shrink `rect`
    // and shift the LED-dot computation below into the lower half.
    if (isMouseOver)
    {
        g.setColour(juce::Colour(kAccentWhite).withAlpha(0.06f));
        const auto highlight = rect.withHeight(rect.getHeight() * 0.5f);
        g.fillRoundedRectangle(highlight, corner);
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
        // Bright halo + crisp core — dimmed when disabled.
        g.setColour(juce::Colour(kAccentWhite).withAlpha(isEnabled ? 0.55f : 0.15f));
        g.fillEllipse(dot.expanded(2.0f));
        g.setColour(juce::Colour(kAccentWhite).withAlpha(isEnabled ? 1.0f : 0.30f));
        g.fillEllipse(dot);
    }
    else
    {
        // Recessed dark dot when off.
        g.setColour(juce::Colour(kLedOff).withAlpha(isEnabled ? 1.0f : 0.5f));
        g.fillEllipse(dot);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(dot, 0.5f);
    }
}

void MaybeDuckLookAndFeel::drawPushButtonLabel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                                const juce::String& text,
                                                bool isOn, bool /*isMouseOver*/, bool isDown, bool isEnabled) const
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

    // Main text — bright when on, slightly muted when off; greyed when disabled.
    juce::Colour textColour = isOn ? juce::Colour(kAccentWhite)
                                   : juce::Colour(0xffB4ADA0);
    if (! isEnabled)
        textColour = textColour.interpolatedWith(juce::Colour(kChassisShadow), 0.55f).withMultipliedAlpha(0.85f);
    g.setColour(textColour);
    g.drawText(text.toUpperCase(), rect.toNearestInt(),
               juce::Justification::centredLeft, false);
}

void MaybeDuckLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool isOver, bool isDown)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const bool isOn = b.getToggleState();
    drawPushButtonBody (g, bounds, isOn, isOver, isDown, b.isEnabled());
    drawPushButtonLabel(g, bounds, b.getButtonText(), isOn, isOver, isDown, b.isEnabled());
}

void MaybeDuckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour& /*bg*/,
                                                 bool isOver, bool isDown)
{
    drawPushButtonBody(g, b.getLocalBounds().toFloat(),
                       b.getToggleState(), isOver, isDown, b.isEnabled());
}

void MaybeDuckLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                           bool isOver, bool isDown)
{
    drawPushButtonLabel(g, b.getLocalBounds().toFloat(),
                        b.getButtonText(), b.getToggleState(), isOver, isDown, b.isEnabled());
}

juce::Font MaybeDuckLookAndFeel::getLabelFont (juce::Label& l)
{
    // Honour the label's explicitly-set font size when set; otherwise pick a
    // sensible default for panel-label text.
    juce::Font font(regularTypeface);
    const float requestedHeight = l.getFont().getHeight();
    font.setHeight(requestedHeight > 0.0f ? requestedHeight : 12.0f);
    font.setExtraKerningFactor(0.04f);
    return font;
}

void MaybeDuckLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    g.fillAll(l.findColour(juce::Label::backgroundColourId));

    if (!l.isBeingEdited())
    {
        auto area = l.getBorderSize().subtractedFrom(l.getLocalBounds()).toFloat();
        auto font = getLabelFont(l);
        const auto text = l.getText().toUpperCase();
        drawEngravedText(g, text, area, l.getJustificationType(), font);
    }
    else if (l.isEnabled())
    {
        // Let JUCE's TextEditor handle the editing state; we don't draw text here.
    }

    g.setColour(l.findColour(juce::Label::outlineColourId));
    g.drawRect(l.getLocalBounds());
}

void MaybeDuckLookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int width, int height,
                                                     const juce::String& text,
                                                     const juce::Justification& /*position*/,
                                                     juce::GroupComponent& /*group*/)
{
    // We don't draw the enclosing box at all — just an engraved title in the top-left
    // and a thin recessed horizontal divider line extending across the section width.
    const float padX = 8.0f;
    const float topY = 4.0f;
    const float titleHeight = 18.0f;

    juce::Font font(mediumTypeface);
    font.setHeight(13.5f);
    font.setExtraKerningFactor(0.10f);
    g.setFont(font);

    const juce::String upper = text.toUpperCase();
    const float titleWidth = font.getStringWidthFloat(upper) + 6.0f;

    // Engraved title at left.
    juce::Rectangle<float> titleArea(padX, topY, titleWidth, titleHeight);
    drawEngravedText(g, upper, titleArea, juce::Justification::centredLeft, font);

    // Divider line — starts after the title, runs to the right edge, with engraved styling.
    const float lineY      = topY + titleHeight * 0.5f - 0.5f;
    const float lineStartX = padX + titleWidth + 10.0f;
    const float lineEndX   = static_cast<float>(width) - padX;

    if (lineEndX > lineStartX + 4.0f)
    {
        g.setColour(juce::Colour(kDividerShadow).withAlpha(0.7f));
        g.drawLine(lineStartX, lineY, lineEndX, lineY, 1.0f);
        g.setColour(juce::Colour(kDividerHighlight).withAlpha(0.5f));
        g.drawLine(lineStartX, lineY + 1.0f, lineEndX, lineY + 1.0f, 0.7f);
    }

    juce::ignoreUnused(height);
}

void MaybeDuckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos,
                                             float rotaryStart, float rotaryEnd,
                                             juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    // KNOB PROPORTIONS — fractions of `radius` (half the slot the editor hands
    // us) so knobs scale with the window. Vintage fluted-skirt knob:
    //   skirtR : the wide MATTE-black base flange
    //   bodyR  : the scalloped (fluted) MATTE-black body sitting on the skirt
    //   capR   : the spun-aluminium top cap (the only metallic part)
    // The skirt is sized to leave a band outside it for the Moog tick ring.
    // Draw order: ticks -> shadow -> skirt -> fluted body -> cap recess ->
    // metal cap -> bipolar arc -> pointer dot.
    const float skirtR = radius * 0.80f;   // narrower flange than before
    const float bodyR  = radius * 0.66f;
    const float capR   = radius * 0.44f;

    // The "active" knob deliberately uses the soft, desaturated warm-grey look
    // (what used to be the disabled style). An UNAVAILABLE knob goes further: it
    // washes toward the panel tone, drops its pointer AND bipolar arc (no value
    // shown), and fades its ticks — so it reads as genuinely inert, not just dim.
    const bool  isEnabled = slider.isEnabled();
    const float bodyMix   = isEnabled ? 0.45f : 0.80f;  // desaturation toward the chassis tone
    const float skirtMix  = isEnabled ? 0.0f  : 0.50f;  // active skirt stays matte black
    const float capWash   = isEnabled ? 0.45f : 0.72f;  // warm wash muting the metal cap

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

    // --- Tick ring (engraved into the chassis). Two flavours:
    //   * "stepTicks" knobs (Ratio, Lookahead) get one tick at each discrete step
    //     position — i.e. exactly where the pointer dot can land. Step positions
    //     are evenly spaced in knob travel, so step i sits at t = i/(n-1).
    //   * everything else gets a Moog graduation: 5 major ticks with 4 minor
    //     "slices" between each. On bipolar knobs the centre major lands on 0.
    {
        const float rOuter   = radius * 0.98f;
        const float rMajorIn = radius * 0.86f;
        const float rMinorIn = radius * 0.915f;
        const float dim      = isEnabled ? 1.0f : 0.35f;

        // Draw one engraved tick at sweep-proportion t (col = groove tone; a faint
        // offset highlight is added when hiAlpha > 0 for the engraved look).
        auto drawTick = [&](float t, float rIn, float thick, juce::Colour col, float hiAlpha)
        {
            const float angle = rotaryStart + t * (rotaryEnd - rotaryStart);
            const float sa    = angle - juce::MathConstants<float>::halfPi;
            const float c = std::cos(sa), s = std::sin(sa);
            const juce::Point<float> inner(centre.x + c * rIn,    centre.y + s * rIn);
            const juce::Point<float> outer(centre.x + c * rOuter, centre.y + s * rOuter);
            g.setColour(col);
            g.drawLine(inner.x, inner.y, outer.x, outer.y, thick);
            if (hiAlpha > 0.0f)
            {
                g.setColour(juce::Colour(kDividerHighlight).withAlpha(hiAlpha));
                g.drawLine(inner.x + 0.6f, inner.y + 0.6f, outer.x + 0.6f, outer.y + 0.6f, thick * 0.5f);
            }
        };

        if (slider.getProperties().contains("stepTicks"))
        {
            const int n = juce::jmax(1, static_cast<int>(slider.getProperties()["stepTicks"]));
            for (int i = 0; i < n; ++i)
            {
                const float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.5f;
                drawTick(t, rMajorIn, 1.3f,
                         juce::Colour(kDividerShadow).withAlpha(0.9f * dim), 0.4f * dim);
            }
        }
        else
        {
            constexpr int kMajorTicks = 5;
            constexpr int kMinorPer   = 4;                                  // minors between majors
            constexpr int kTotalTicks = (kMajorTicks - 1) * (kMinorPer + 1) + 1; // -> 21
            constexpr int kCentreTick = (kTotalTicks - 1) / 2;              // -> 10 (t = 0.5 = the 0 mark)
            for (int i = 0; i < kTotalTicks; ++i)
            {
                const float t       = static_cast<float>(i) / static_cast<float>(kTotalTicks - 1);
                const bool  major   = (i % (kMinorPer + 1)) == 0;
                const bool  centre0 = isBipolar && (i == kCentreTick);      // the bipolar "0" marker
                if (centre0)
                    drawTick(t, rMajorIn, 2.0f, juce::Colour(kEngravedText).withAlpha(dim), 0.0f);
                else
                    drawTick(t, major ? rMajorIn : rMinorIn, major ? 1.5f : 0.8f,
                             juce::Colour(kDividerShadow).withAlpha((major ? 0.9f : 0.6f) * dim),
                             (major ? 0.4f : 0.28f) * dim);
            }
        }
    }

    // --- Drop shadow under the whole knob
    {
        auto shadowRect = juce::Rectangle<float>(skirtR * 2.0f, skirtR * 2.0f)
                              .withCentre({ centre.x + 1.4f, centre.y + 2.6f });
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillEllipse(shadowRect.expanded(1.0f));
    }

    // --- MATTE black base skirt (flange). Near-flat fill — only the faintest
    // form shading and a dark contact rim. Deliberately no glossy sheen.
    {
        auto skirtRect = juce::Rectangle<float>(skirtR * 2.0f, skirtR * 2.0f).withCentre(centre);
        juce::ColourGradient skirtGrad(
            juce::Colour(0xff121212).interpolatedWith(juce::Colour(kChassisShadow), skirtMix),        // top
            centre.x, centre.y - skirtR * 0.4f,
            juce::Colour(0xff070707).interpolatedWith(juce::Colour(kChassisShadow), skirtMix * 0.9f), // base
            centre.x, centre.y + skirtR,
            true);
        g.setGradientFill(skirtGrad);
        g.fillEllipse(skirtRect);

        g.setColour(juce::Colours::black.withAlpha(0.55f));                              // dark contact rim
        g.drawEllipse(skirtRect, 1.0f);
        g.setColour(juce::Colour(kDividerHighlight).withAlpha(isEnabled ? 0.07f : 0.03f)); // whisper of an edge catch
        g.drawEllipse(skirtRect.reduced(0.7f), 0.6f);
    }

    // --- MATTE fluted body: a lobed (scalloped) black disc raised on the skirt.
    // The flutes read from their outline + a low-contrast form gradient, never a
    // specular highlight, so the black stays matte.
    {
        constexpr int   kFlutes    = 14;
        constexpr float fluteDepth = 0.10f;       // lobe depth as a fraction of bodyR
        constexpr int   steps      = kFlutes * 12;

        juce::Path body;
        for (int i = 0; i <= steps; ++i)
        {
            const float t     = static_cast<float>(i) / static_cast<float>(steps);
            const float theta = t * juce::MathConstants<float>::twoPi;
            const float rr    = bodyR * (1.0f - fluteDepth * (0.5f - 0.5f * std::cos(kFlutes * theta)));
            const float sa    = theta - juce::MathConstants<float>::halfPi;
            const juce::Point<float> p(centre.x + std::cos(sa) * rr, centre.y + std::sin(sa) * rr);
            if (i == 0) body.startNewSubPath(p);
            else        body.lineTo(p);
        }
        body.closeSubPath();

        const auto bodyHi = juce::Colour(0xff242220).interpolatedWith(juce::Colour(kChassisShadow), bodyMix);
        const auto bodyLo = juce::Colour(kKnobMatteBlack).interpolatedWith(juce::Colour(kChassisShadow), bodyMix * 0.9f);
        juce::ColourGradient bodyGrad(bodyHi,
                                      centre.x - bodyR * 0.4f, centre.y - bodyR * 0.5f,
                                      bodyLo,
                                      centre.x + bodyR * 0.5f, centre.y + bodyR * 0.6f,
                                      true);
        g.setGradientFill(bodyGrad);
        g.fillPath(body);

        // Crisp dark outline so each flute reads against the skirt.
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.strokePath(body, juce::PathStrokeType(1.0f));

        // Soft ambient-occlusion ring where the body meets the skirt.
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawEllipse(juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre).expanded(1.2f), 2.0f);
    }

    // --- Cap recess: a dark seat ring, then the spun-aluminium cap on top.
    {
        auto recessRect = juce::Rectangle<float>((capR + 2.2f) * 2.0f, (capR + 2.2f) * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(recessRect);
    }

    {
        auto capRect = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(centre);

        // Pre-rendered spun-metal disc, scaled onto the cap rect.
        const float capScale = (capR * 2.0f) / static_cast<float>(metalCapImage.getWidth());
        const auto  capXform = juce::AffineTransform::scale(capScale)
                                   .translated(capRect.getX(), capRect.getY());
        g.drawImageTransformed(metalCapImage, capXform);

        // Warm matte wash over the metal: this de-shined cap is the default look;
        // an unavailable knob washes it nearly flat (capWash).
        g.setColour(juce::Colour(kChassisShadow).withAlpha(capWash));
        g.fillEllipse(capRect);

        // Thin bright bevel + dark seat around the cap edge (bevel dimmed when off).
        g.setColour(juce::Colour(0xffF2F2F2).withAlpha(isEnabled ? 0.4f : 0.12f));
        g.drawEllipse(capRect.reduced(0.5f), 0.8f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawEllipse(capRect.expanded(0.4f), 0.8f);
    }

    // --- Bipolar fill arc: thin ring just outside the skirt, from centre to value.
    // Hidden on unavailable knobs — no value is shown when the control is inert.
    if (isBipolar && isEnabled)
    {
        const float arcRadius = skirtR + 3.0f;
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

        const auto arcColour = juce::Colour(heavy ? kAccentAmber : kAccentWhite)
                                  .withAlpha(isEnabled ? 1.0f : 0.18f);
        g.setColour(arcColour);
        g.strokePath(arc, juce::PathStrokeType(2.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // --- White pointer dot on the fluted body (the value indicator). Omitted on
    // unavailable knobs — a knob with no pointer reads as clearly inert.
    if (isEnabled)
    {
        const float indicatorAngle = angleAt(sliderPos);
        const float sa = indicatorAngle - juce::MathConstants<float>::halfPi;
        const float dotR     = juce::jlimit(2.0f, 4.0f, radius * 0.075f);
        const float dotOrbit = bodyR * 0.80f;   // on the body, just inside the flutes
        const juce::Point<float> dot(centre.x + std::cos(sa) * dotOrbit,
                                     centre.y + std::sin(sa) * dotOrbit);

        g.setColour(juce::Colours::black.withAlpha(0.5f));            // soft shadow
        g.fillEllipse(juce::Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(dot.translated(0.4f, 0.7f)));
        g.setColour(juce::Colour(kAccentWhite));                      // dot
        g.fillEllipse(juce::Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(dot));
    }
}
