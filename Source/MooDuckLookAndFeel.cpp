#include "MooDuckLookAndFeel.h"

// ============================== THEME PALETTES ==============================
// Light = the original brushed-nickel look; dark = a higher-contrast charcoal.
// Field order matches the Palette struct. setDarkMode() selects between them.
// Band/meter state colours (kBandSolo/kBandMute/kMeterClipRed) stay constant.
const MooDuckLookAndFeel::Palette MooDuckLookAndFeel::lightPalette {
    juce::Colour(0xffC7BFB3), juce::Colour(0xffA8A097), juce::Colour(0xff6E6863),
    juce::Colour(0xffD0C8BC), juce::Colour(0xff5A5450),
    juce::Colour(0xff1A1A1A), juce::Colour(0xffFFFFFF), juce::Colour(0xffE8A23E), juce::Colour(0xffEFE7D7),
    juce::Colour(0xffCEC7BB), juce::Colour(0xff968E84), juce::Colour(0xff322E2B),
    juce::Colour(0xff5A5450),  // tickColor = divider shadow (dark groove on a light panel)
    juce::Colour(0x00000000)   // knobUnderGlow: off in light mode (transparent)
};
const MooDuckLookAndFeel::Palette MooDuckLookAndFeel::darkPalette {
    juce::Colour(0xff4A4A53), juce::Colour(0xff2B2B31), juce::Colour(0xff121216),
    juce::Colour(0xff6C6C77), juce::Colour(0xff070709),
    juce::Colour(0xff0E0E10), juce::Colour(0xffFFFFFF), juce::Colour(0xffEBA94A), juce::Colour(0xffE6E1D7),
    juce::Colour(0xff52525C), juce::Colour(0xff34343C), juce::Colour(0xff0C0C0E),
    juce::Colour(0xff9E9EA8),  // tickColor = lit graphite (reads as raised on a dark panel)
    juce::Colour(0xffEAE6D6)   // knobUnderGlow = warm off-white "dimmed-LED" halo
};

MooDuckLookAndFeel::MooDuckLookAndFeel()
{
    regularTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexSansCondensedRegular_ttf,
        BinaryData::IBMPlexSansCondensedRegular_ttfSize);

    mediumTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexSansCondensedMedium_ttf,
        BinaryData::IBMPlexSansCondensedMedium_ttfSize);

    setDefaultSansSerifTypeface(regularTypeface);

    // Install the light palette: pushes its tones into the slider value-box colour
    // IDs and builds the brushed-grain overlay. setDarkMode() swaps to the dark one
    // at runtime (and re-runs both of those steps).
    setDarkMode(false);

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

void MooDuckLookAndFeel::setDarkMode (bool shouldBeDark)
{
    dark = shouldBeDark;
    pal  = shouldBeDark ? darkPalette : lightPalette;
    applyPaletteColours();
    regenerateGrain();
}

void MooDuckLookAndFeel::applyPaletteColours()
{
    // Knob value-readout box: matte plate + engraved-tone border/text from the
    // active palette (re-run on every theme switch).
    setColour(juce::Slider::textBoxBackgroundColourId, pal.knobMatteBlack);
    setColour(juce::Slider::textBoxOutlineColourId,    pal.engravedText);
    setColour(juce::Slider::textBoxTextColourId,       pal.engravedText);
    setColour(juce::Slider::textBoxHighlightColourId,  pal.accentAmber.withAlpha(0.35f));
}

void MooDuckLookAndFeel::regenerateGrain()
{
    // 256x256 tileable brushed-grain overlay, tinted from the active chassis tones.
    grainImage = juce::Image(juce::Image::ARGB, 256, 256, true);
    juce::Random rng(0xD11CC11D); // fixed seed → reproducible grain
    for (int y = 0; y < grainImage.getHeight(); ++y)
        for (int x = 0; x < grainImage.getWidth(); ++x)
        {
            const float a = juce::jlimit(0.0f, 1.0f, rng.nextFloat() * 0.26f - 0.07f);
            const auto tint = (rng.nextBool() ? pal.chassisHighlight : pal.chassisShadow);
            grainImage.setPixelAt(x, y, tint.withAlpha(a));
        }
}

void MooDuckLookAndFeel::drawBrushedChassis (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // Vertical 3-stop gradient: highlight near top, mid in the middle, shadow at the bottom.
    juce::ColourGradient grad(
        pal.chassisHighlight, area.getX(),      area.getY(),
        pal.chassisShadow,    area.getX(),      area.getBottom(),
        false);
    grad.addColour(0.55, pal.chassisMid);
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
        pal.chassisShadow.withAlpha(0.25f), area.getX(), area.getY(),
        true);
    g.setGradientFill(edge);
    g.fillRect(area);
}

juce::Colour MooDuckLookAndFeel::chassisToneAt (float yNorm) const
{
    // Mirrors drawBrushedChassis: highlight at top -> chassisMid at 0.55 -> shadow.
    yNorm = juce::jlimit(0.0f, 1.0f, yNorm);
    if (yNorm <= 0.55f)
        return pal.chassisHighlight.interpolatedWith(pal.chassisMid, yNorm / 0.55f);
    return pal.chassisMid.interpolatedWith(pal.chassisShadow, (yNorm - 0.55f) / 0.45f);
}

void MooDuckLookAndFeel::drawRackInset (juce::Graphics& g, juce::Rectangle<float> area,
                                         juce::Colour localChassis) const
{
    if (area.isEmpty()) return;
    const float corner = 5.0f;

    // Lip shadow around the cutout the module drops into.
    g.setColour(juce::Colours::black.withAlpha(0.33f));
    g.fillRoundedRectangle(area.expanded(1.5f), corner + 2.0f);

    // Recessed module face. CRITICAL: the panel is derived from the chassis tone AT
    // THIS RACK'S POSITION (localChassis), darkened a FIXED amount — so it always
    // reads ~the same amount darker than its surroundings, whether the rack sits
    // high (light chassis) or low (dark chassis). A fixed absolute panel colour
    // would look recessed up top but like a raised light patch down low.
    const auto faceTop = localChassis.darker(0.42f);
    const auto faceMid = localChassis.darker(0.55f);
    const auto faceBot = localChassis.darker(0.72f);
    juce::ColourGradient pg(faceTop, area.getX(), area.getY(),
                            faceBot, area.getX(), area.getBottom(), false);
    pg.addColour(0.5, faceMid);
    g.setGradientFill(pg);
    g.fillRoundedRectangle(area, corner);

    // Brushed grain inside the module, anchored to the panel's own top-left so every
    // rack samples the identical part of the noise tile (consistent texture).
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(area, corner);
        g.reduceClipRegion(clip);
        g.setTiledImageFill(grainImage, (int) area.getX(), (int) area.getY(), 0.34f);
        g.fillRoundedRectangle(area, corner);
    }

    // Inset bevel: shadow just inside the TOP + LEFT, lit lip along BOTTOM + RIGHT
    // (the inverse of a raised cap — sells the recess).
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawLine(area.getX() + corner, area.getY() + 1.2f, area.getRight() - corner, area.getY() + 1.2f, 1.8f);
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.drawLine(area.getX() + 1.2f, area.getY() + corner, area.getX() + 1.2f, area.getBottom() - corner, 1.4f);
    g.setColour(pal.dividerHighlight.withAlpha(0.40f));
    g.drawLine(area.getX() + corner, area.getBottom() - 1.0f, area.getRight() - corner, area.getBottom() - 1.0f, 1.4f);
    g.setColour(pal.dividerHighlight.withAlpha(0.18f));
    g.drawLine(area.getRight() - 1.0f, area.getY() + corner, area.getRight() - 1.0f, area.getBottom() - corner, 1.0f);

    // Mounting ears: thin brushed rails down each side with rack screws top+bottom.
    const float railW  = juce::jlimit(7.0f, 12.0f, area.getWidth() * 0.018f);
    const float screwR = juce::jmin(3.6f, railW * 0.42f);
    for (int side = 0; side < 2; ++side)
    {
        auto rail = (side == 0)
            ? juce::Rectangle<float>(area.getX(),            area.getY(), railW, area.getHeight())
            : juce::Rectangle<float>(area.getRight() - railW, area.getY(), railW, area.getHeight());
        rail = rail.reduced(1.0f, 3.0f);
        juce::ColourGradient rg(pal.chassisHighlight.withAlpha(0.55f), rail.getX(),     rail.getY(),
                                pal.chassisShadow.withAlpha(0.55f),    rail.getRight(), rail.getY(), false);
        g.setGradientFill(rg);
        g.fillRoundedRectangle(rail, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(rail, 2.0f, 0.6f);
        drawScrew(g, { rail.getCentreX(), rail.getY()    + railW * 0.7f }, screwR, 0.6f);
        drawScrew(g, { rail.getCentreX(), rail.getBottom() - railW * 0.7f }, screwR, 2.1f);
    }
}

void MooDuckLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius,
                                      float slotAngleRadians) const
{
    juce::Rectangle<float> body(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Recessed shadow ring beneath the screw — sells the inset look.
    g.setColour(pal.chassisShadow.withAlpha(0.55f));
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
    g.setColour(pal.dividerShadow);
    g.drawLine(a.x, a.y, b.x, b.y, 1.4f);
    g.setColour(pal.dividerHighlight.withAlpha(0.35f));
    g.drawLine(a.x + 0.5f, a.y + 0.5f, b.x + 0.5f, b.y + 0.5f, 0.6f);
}

void MooDuckLookAndFeel::drawEngravedText (juce::Graphics& g, const juce::String& text,
                                             juce::Rectangle<float> area, juce::Justification just,
                                             juce::Font font) const
{
    g.setFont(font);

    // Highlight (top-left) — fakes a 'lifted' edge.
    g.setColour(pal.dividerHighlight.withAlpha(0.6f));
    g.drawText(text, area.translated(-0.6f, -0.6f), just, false);

    // Shadow (bottom-right) — fakes the 'recess'.
    g.setColour(pal.dividerShadow.withAlpha(0.7f));
    g.drawText(text, area.translated(0.8f, 0.8f), just, false);

    // Main engraved tone.
    g.setColour(pal.engravedText);
    g.drawText(text, area, just, false);
}

void MooDuckLookAndFeel::applyRotaryConfiguration (juce::Slider& s) const
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters(kRotaryStartAngle, kRotaryEndAngle, true);
}

void MooDuckLookAndFeel::applyBipolarBehavior (juce::Slider& s, double centreValue) const
{
    s.getProperties().set("bipolarCentre", centreValue);
    s.setDoubleClickReturnValue(true, centreValue);
}

juce::Colour MooDuckLookAndFeel::litColourFor (const juce::Button& b)
{
    const auto& p = b.getProperties();
    if (p.contains("litColour"))
        return juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(p["litColour"])));
    return juce::Colour(kAccentWhite);   // static method — use the constant, not the runtime palette
}

bool MooDuckLookAndFeel::isSoftSwitch (const juce::Button& b)
{
    return b.getProperties().contains("softSwitch");
}

MooDuckLookAndFeel::CapGeom
MooDuckLookAndFeel::computeCapGeometry (juce::Rectangle<float> bounds, bool pressedLook, bool softProfile,
                                          bool flatRelief) const
{
    const float corner = flatRelief ? 0.5f : (softProfile ? 2.5f : 3.5f);   // flat = near-square
    auto socket = bounds.reduced(1.5f);
    const float gap = softProfile ? 2.5f : 4.0f;     // glowing ring between cap silhouette and socket
    auto inner = socket.reduced(gap);

    // Visible wall height ("how proud"). The cap extrudes straight DOWN, so only the
    // FRONT (bottom) wall shows under top-left light — the top face keeps its full
    // width. The top edge is ANCHORED, so pressing only shrinks the wall: the cap
    // flattens in place rather than sinking and leaving a hole at the top.
    // flatRelief = barely proud of the chassis (~1 px wall) — flatter than the others.
    const float maxWall = flatRelief  ? 1.0f
                        : softProfile ? 2.5f
                                      : juce::jlimit(4.0f, 9.0f, inner.getHeight() * 0.18f);
    const float wall    = pressedLook ? (softProfile ? 0.4f : 1.2f) : maxWall;

    auto top  = juce::Rectangle<float>(inner.getX(), inner.getY(),
                                       inner.getWidth(), inner.getHeight() - wall);
    auto base = top.translated(0.0f, wall);          // seated footprint, straight down (front wall only)
    return { socket, base, top, corner };
}

void MooDuckLookAndFeel::drawPushButtonBody (juce::Graphics& g, juce::Rectangle<float> bounds,
                                               bool isOn, bool isMouseOver, bool isDown, bool isEnabled,
                                               juce::Colour litColour, bool softProfile, bool alwaysLit,
                                               bool flatRelief) const
{
    // A real, extruded brushed-metal cap standing proud of a recessed socket.
    // The front (bottom) wall is visible (top-left light). The cap is raised when
    // OFF and flattens flush into the socket when ENGAGED (latching cap) or
    // DISABLED; momentary soft switches only dip while held. The state colour
    // glows in the socket gap ALL AROUND the cap, not on the face.
    // Engaged (latched) caps read as pushed IN — both the hard caps AND the soft
    // toggle switches (Bypass / Sat), so all the buttons depress consistently. The
    // "always lit" soft switches (band solo/mute) keep glowing without depressing.
    const bool pressedLook = isDown || (isOn && ! alwaysLit) || (! softProfile && ! isEnabled);
    // Soft switches that are "always lit" (the band switches) glow whenever enabled;
    // soft toggles (Pultec Bypass/Sat) and hard caps glow only when engaged.
    const bool lit         = isEnabled && (alwaysLit || isOn);

    const auto geo    = computeCapGeometry(bounds, pressedLook, softProfile, flatRelief);
    const float corner = geo.corner;
    const auto socket = geo.socket;
    const auto base   = geo.base;
    const auto top    = geo.top;

    // --- Outer cast shadow on the chassis: the cap throws a soft shadow BELOW it
    // (outside the button), more when raised, less when pressed. This is the main
    // depth cue; the soft toggles (Bypass/Sat) cast a lighter shadow, and a flatRelief
    // cap (barely proud) throws almost none.
    if (isEnabled)
    {
        const float shadowAlpha = flatRelief ? 0.05f : (softProfile ? 0.20f : 0.28f);
        const int   shadowRad   = flatRelief ? 1 : (pressedLook ? 3 : 5);
        const int   shadowOff   = flatRelief ? 1 : (pressedLook ? 1 : 3);
        juce::DropShadow(juce::Colours::black.withAlpha(shadowAlpha), shadowRad,
                         juce::Point<int>(0, shadowOff))
            .drawForRectangle(g, socket.toNearestInt());
    }

    // --- Recessed socket (the dark hole the cap rises out of). flatRelief skips the
    // dark outline ring (minimal borders).
    g.setColour(pal.buttonSocket.withAlpha(flatRelief ? 0.55f : (softProfile ? 0.85f : 1.0f)));
    g.fillRoundedRectangle(socket, corner + (flatRelief ? 0.5f : 2.0f));
    if (! flatRelief)
    {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawRoundedRectangle(socket.reduced(0.5f), corner + 2.0f, 1.0f);
    }

    // --- Backlight halo in the gap (state colour), blooming onto the chassis.
    if (lit)
    {
        juce::ColourGradient bloom(litColour.withAlpha(0.30f), socket.getCentreX(), socket.getCentreY(),
                                   litColour.withAlpha(0.0f),  socket.getCentreX(), socket.getY() - 7.0f, true);
        g.setGradientFill(bloom);
        g.fillRoundedRectangle(socket.expanded(7.0f), corner + 7.0f);   // soft bloom past the socket
        g.setColour(litColour.withAlpha(0.75f));
        g.fillRoundedRectangle(socket.reduced(0.8f), corner + 1.0f);    // fills the gap; cap covers the centre
        g.setColour(litColour.brighter(0.3f).withAlpha(0.9f));
        g.drawRoundedRectangle(socket.reduced(1.4f), corner + 0.5f, 1.0f);
    }

    // --- Cap front wall = the seated base footprint (darker brushed), peeking out
    // at the front (bottom) of the raised top face. Darkest at the front edge.
    {
        juce::Colour wallTop = pal.buttonFaceLo.darker(0.35f);
        juce::Colour wallBot = juce::Colour(0xff17150F);
        if (! isEnabled)
        {
            wallTop = wallTop.interpolatedWith(pal.chassisShadow, 0.55f);
            wallBot = wallBot.interpolatedWith(pal.chassisShadow, 0.45f);
        }
        juce::ColourGradient wall(wallTop, base.getX(), base.getY(),
                                  wallBot, base.getX(), base.getBottom(), false); // vertical: darkest at the front (bottom)
        g.setGradientFill(wall);
        g.fillRoundedRectangle(base, corner);
        if (! flatRelief)
        {
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.drawRoundedRectangle(base, corner, 0.7f);
        }
    }

    // --- Top face: brushed metal, lighter than the walls; light from top-left.
    juce::Colour faceHi = pal.buttonFaceHi;
    juce::Colour faceLo = pal.buttonFaceLo;
    if (pressedLook) { faceHi = faceHi.darker(0.10f); faceLo = faceLo.darker(0.12f); }
    if (! isEnabled)
    {
        faceHi = faceHi.interpolatedWith(pal.chassisShadow, 0.55f);
        faceLo = faceLo.interpolatedWith(pal.chassisShadow, 0.55f);
    }
    juce::ColourGradient fg(faceHi, top.getX(), top.getY(),
                            faceLo, top.getRight(), top.getBottom(), false);
    g.setGradientFill(fg);
    g.fillRoundedRectangle(top, corner);

    // Brushed grain on the top face (clipped), so the cap matches the chassis.
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip; clip.addRoundedRectangle(top, corner);
        g.reduceClipRegion(clip);
        g.setTiledImageFill(grainImage, 0, 0, isEnabled ? 0.25f : 0.12f);
        g.fillRoundedRectangle(top, corner);
    }

    if (pressedLook && isEnabled)
    {
        // Pushed IN: just a FAINT top-edge inner shadow as a recess hint — the main
        // depth comes from the outer cast shadow above. Lighter still on soft toggles.
        const float ia = softProfile ? 0.16f : 0.26f;
        juce::ColourGradient topSh(juce::Colours::black.withAlpha(ia), top.getX(), top.getY(),
                                   juce::Colours::black.withAlpha(0.0f), top.getX(),
                                   top.getY() + top.getHeight() * 0.55f, false);
        g.setGradientFill(topSh);
        g.fillRoundedRectangle(top, corner);
        g.setColour(juce::Colours::black.withAlpha(ia));
        g.drawRoundedRectangle(top.translated(-0.4f, -0.4f), corner, 1.0f);
    }
    else
    {
        if (flatRelief)
        {
            // Minimal edge: a single faint top-left highlight, NO dark machined edge.
            g.setColour(pal.chassisHighlight.withAlpha(isEnabled ? 0.28f : 0.10f));
            g.drawRoundedRectangle(top.translated(-0.3f, -0.3f), corner, 0.6f);
        }
        else
        {
            // Raised cap bevel: highlight top-left, shadow bottom-right (a machined edge).
            g.setColour(pal.chassisHighlight.brighter(0.4f).withAlpha(isEnabled ? 0.85f : 0.30f));
            g.drawRoundedRectangle(top.translated(-0.5f, -0.5f), corner, 1.0f);
            g.setColour(juce::Colours::black.withAlpha(0.40f));
            g.drawRoundedRectangle(top.translated(0.5f, 0.6f), corner, 1.0f);
        }

        // Hover: faint brighten on the upper top-face.
        if (isMouseOver && isEnabled && ! isDown)
        {
            g.setColour(pal.accentWhite.withAlpha(0.08f));
            g.fillRoundedRectangle(top.withHeight(top.getHeight() * 0.5f), corner);
        }
    }
}

void MooDuckLookAndFeel::drawPushButtonLabel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                                const juce::String& text,
                                                bool isOn, bool /*isMouseOver*/, bool isDown, bool isEnabled,
                                                bool softProfile, bool alwaysLit, bool flatRelief) const
{
    if (text.isEmpty()) return;

    const bool pressedLook = isDown || (isOn && ! alwaysLit) || (! softProfile && ! isEnabled);
    const auto geo = computeCapGeometry(bounds, pressedLook, softProfile, flatRelief);

    // Legend sits on the raised top face. The flat chip is narrow, so eat less
    // horizontal padding there (more room for "COLOURED") and nudge down to optical centre.
    auto rect = geo.top.reduced(flatRelief ? 1.5f : 3.0f, 1.5f);
    if (flatRelief) rect.translate(0.0f, 0.8f);

    // Legends compensate for the global kUIScale shrink (they'd otherwise dip
    // below comfortable reading size), then shrink-to-fit any legend the smaller
    // cap can no longer hold at that height.
    const float fontSize = juce::jlimit(8.5f, 13.0f, rect.getHeight() * 0.5f) / kUIScale;
    juce::Font font(getMediumTypeface());
    font.setHeight(fontSize);
    font.setExtraKerningFactor(0.04f);

    const auto upper = text.toUpperCase();
    if (const float w = font.getStringWidthFloat(upper); w > rect.getWidth())
        font.setHeight(fontSize * rect.getWidth() / w);
    g.setFont(font);

    // Legend tone follows the theme: dark text engraved into the LIGHT metal cap,
    // but the palette WHITE on the darker dark-mode cap (where dark text vanishes).
    juce::Colour textColour = dark ? pal.accentWhite : juce::Colour(0xff2A2622);
    if (! isEnabled)
        textColour = textColour.interpolatedWith(pal.chassisShadow, 0.45f).withMultipliedAlpha(0.85f);

    // Emboss: light mode lifts a highlight above; dark mode drops a shadow below
    // (so the white legend reads as raised, not carved).
    if (dark)
    {
        g.setColour(juce::Colours::black.withAlpha(isEnabled ? 0.5f : 0.25f));
        g.drawText(upper, rect.translated(0.0f, 0.7f).toNearestInt(), juce::Justification::centred, false);
    }
    else
    {
        g.setColour(pal.chassisHighlight.withAlpha(isEnabled ? 0.5f : 0.2f));
        g.drawText(upper, rect.translated(0.0f, -0.7f).toNearestInt(), juce::Justification::centred, false);
    }
    g.setColour(textColour);
    g.drawText(upper, rect.toNearestInt(), juce::Justification::centred, false);
}

void MooDuckLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                             bool isOver, bool isDown)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const bool isOn = b.getToggleState();
    const bool soft = isSoftSwitch(b);
    const bool alwaysLit = b.getProperties().contains("alwaysLit");
    const bool flatRelief = b.getProperties().contains("flatRelief");
    drawPushButtonBody (g, bounds, isOn, isOver, isDown, b.isEnabled(), litColourFor(b), soft, alwaysLit, flatRelief);
    drawPushButtonLabel(g, bounds, b.getButtonText(), isOn, isOver, isDown, b.isEnabled(), soft, alwaysLit, flatRelief);
}

void MooDuckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                 const juce::Colour& /*bg*/,
                                                 bool isOver, bool isDown)
{
    drawPushButtonBody(g, b.getLocalBounds().toFloat(),
                       b.getToggleState(), isOver, isDown, b.isEnabled(),
                       litColourFor(b), isSoftSwitch(b), b.getProperties().contains("alwaysLit"),
                       b.getProperties().contains("flatRelief"));
}

void MooDuckLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                           bool isOver, bool isDown)
{
    drawPushButtonLabel(g, b.getLocalBounds().toFloat(),
                        b.getButtonText(), b.getToggleState(), isOver, isDown,
                        b.isEnabled(), isSoftSwitch(b), b.getProperties().contains("alwaysLit"),
                        b.getProperties().contains("flatRelief"));
}

juce::Font MooDuckLookAndFeel::getLabelFont (juce::Label& l)
{
    // Honour the label's explicitly-set font size when set; otherwise pick a
    // sensible default for panel-label text.
    juce::Font font(regularTypeface);
    const float requestedHeight = l.getFont().getHeight();
    font.setHeight(requestedHeight > 0.0f ? requestedHeight : 12.0f);
    font.setExtraKerningFactor(0.04f);
    return font;
}

void MooDuckLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
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

void MooDuckLookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int width, int height,
                                                     const juce::String& text,
                                                     const juce::Justification& /*position*/,
                                                     juce::GroupComponent& group)
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

    // Divider line — starts after the title, runs to the right edge, with engraved
    // styling. A group may host an inline widget on its title row (the Crossover
    // Coloured/Clean toggle) and set "dividerStartX" (group-local) so the rule begins
    // AFTER the widget rather than underneath it.
    const float lineY      = topY + titleHeight * 0.5f - 0.5f;
    float lineStartX       = padX + titleWidth + 10.0f;
    if (group.getProperties().contains("dividerStartX"))
        lineStartX = juce::jmax(lineStartX, (float) (double) group.getProperties()["dividerStartX"]);
    const float lineEndX   = static_cast<float>(width) - padX;

    if (lineEndX > lineStartX + 4.0f)
    {
        g.setColour(pal.dividerShadow.withAlpha(0.7f));
        g.drawLine(lineStartX, lineY, lineEndX, lineY, 1.0f);
        g.setColour(pal.dividerHighlight.withAlpha(0.5f));
        g.drawLine(lineStartX, lineY + 1.0f, lineEndX, lineY + 1.0f, 0.7f);
    }

    juce::ignoreUnused(height);
}

float MooDuckLookAndFeel::groupTitleRightEdge (const juce::String& title) const
{
    const float padX = 8.0f;
    juce::Font font(mediumTypeface);
    font.setHeight(13.5f);
    font.setExtraKerningFactor(0.10f);
    return padX + font.getStringWidthFloat(title.toUpperCase()) + 6.0f;
}

void MooDuckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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
                g.setColour(pal.dividerHighlight.withAlpha(hiAlpha));
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
                         pal.tickColor.withAlpha(0.9f * dim), 0.4f * dim);
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
                    drawTick(t, rMajorIn, 2.0f, pal.engravedText.withAlpha(dim), 0.0f);
                else
                    drawTick(t, major ? rMajorIn : rMinorIn, major ? 1.5f : 0.8f,
                             pal.tickColor.withAlpha((major ? 0.9f : 0.6f) * dim),
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

    // --- Knob under-glow (dark mode): a soft warm halo ringing the skirt, like a
    // dimmed LED behind the knob. Transparent in light mode, so this is a no-op there.
    if (! pal.knobUnderGlow.isTransparent() && isEnabled)
    {
        juce::ColourGradient glow(pal.knobUnderGlow.withAlpha(0.22f), centre.x, centre.y,
                                  pal.knobUnderGlow.withAlpha(0.0f),  centre.x, centre.y - skirtR * 1.35f, true);
        g.setGradientFill(glow);
        g.fillEllipse(juce::Rectangle<float>(skirtR * 2.7f, skirtR * 2.7f).withCentre(centre));
    }

    // --- MATTE black base skirt (flange). Near-flat fill — only the faintest
    // form shading and a dark contact rim. Deliberately no glossy sheen.
    {
        auto skirtRect = juce::Rectangle<float>(skirtR * 2.0f, skirtR * 2.0f).withCentre(centre);
        juce::ColourGradient skirtGrad(
            juce::Colour(0xff121212).interpolatedWith(pal.chassisShadow, skirtMix),        // top
            centre.x, centre.y - skirtR * 0.4f,
            juce::Colour(0xff070707).interpolatedWith(pal.chassisShadow, skirtMix * 0.9f), // base
            centre.x, centre.y + skirtR,
            true);
        g.setGradientFill(skirtGrad);
        g.fillEllipse(skirtRect);

        g.setColour(juce::Colours::black.withAlpha(0.55f));                              // dark contact rim
        g.drawEllipse(skirtRect, 1.0f);
        g.setColour(pal.dividerHighlight.withAlpha(isEnabled ? 0.07f : 0.03f)); // whisper of an edge catch
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

        const auto bodyHi = juce::Colour(0xff242220).interpolatedWith(pal.chassisShadow, bodyMix);
        const auto bodyLo = pal.knobMatteBlack.interpolatedWith(pal.chassisShadow, bodyMix * 0.9f);
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
        g.setColour(pal.chassisShadow.withAlpha(capWash));
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
        g.setColour(pal.accentWhite);                      // dot
        g.fillEllipse(juce::Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(dot));
    }
}
