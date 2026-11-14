#include "OneClickView.h"
#include "../PluginProcessor.h"

namespace kf::ui
{

namespace
{
    constexpr float targetValues[] = { -14.0f, -11.0f, -9.0f, -7.5f };

    /** Text als Pfad mit dunkler Kontur (gut lesbar auf dem hellen Kern). */
    void drawOutlinedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area, juce::Colour fill)
    {
        juce::GlyphArrangement ga;
        ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), juce::Justification::centred, 1);
        juce::Path p;
        ga.createPath (p);
        g.setColour (juce::Colour (0x8c0a080c));
        g.strokePath (p, juce::PathStrokeType (font.getHeight() * 0.14f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (fill);
        g.fillPath (p);
    }

    template <typename Get>
    double meanOver (double lo, double hi, Get&& get)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < ms::BandLayout::numBands; ++b)
        {
            const double f = ms::BandLayout::getCentre (b), v = get (b);
            if (f >= lo && f < hi && std::isfinite (v)) { s += v; ++n; }
        }
        return n ? s / n : 0.0;
    }
}

//==============================================================================
FusionCore::FusionCore (const UiState& s, juce::AudioProcessorValueTreeState& a) : state (s), apvts (a)
{
    setInterceptsMouseClicks (false, false);
}

void FusionCore::tick()
{
    const float s = state.signal && state.outShort > -70 ? state.outShort : -40.0f;
    loud += (s - loud) * 0.12f;

    // Farbe aus der spektralen Balance der letzten 200 ms
    float lo = 0, hi = 0;
    for (int i = 0; i < juce::jmin (40, state.count); ++i) { const auto& c = state.column (i); lo += c.lo; hi += c.hi * 2.2f; }
    const float bal = lo + hi > 1e-6f ? hi / (lo + hi) : 0.5f;
    hue += (bal - hue) * 0.05f;
    phase += 0.08f;
    repaint();
}

void FusionCore::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float cx = b.getCentreX(), cy = b.getCentreY() - 6.0f, R = juce::jmin (b.getWidth(), b.getHeight() - 24.0f) * 0.46f;
    auto lufsToR = [R] (float v) { return R * 0.18f + juce::jlimit (0.0f, 1.0f, (v + 30.0f) / 26.0f) * R * 0.66f; };
    const float target = params::getValue (apvts, params::target);

    // Ringe
    g.setColour (C::grid);
    for (int k = 1; k <= 4; ++k)
        g.drawEllipse (cx - R * k / 4, cy - R * k / 4, R * k / 2, R * k / 2, 1.0f);

    // Zielring
    const float rt = lufsToR (target);
    const bool reached = state.signal && loud >= target - 0.7f;
    juce::Path ring;
    ring.addEllipse (cx - rt, cy - rt, rt * 2, rt * 2);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 6.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, ring, dashes, 2);
    g.setColour (reached ? C::ok : C::fg.withAlpha (0.55f));
    g.fillPath (dashed);
    g.setFont (mono (13.0f, 1));
    g.setColour (reached ? C::ok : C::dim);
    g.drawText ("Ziel " + num (target) + " LUFS", juce::Rectangle<float> (cx - 100, cy - rt - 24, 200, 18), juce::Justification::centred);

    // Kern
    const float rc = lufsToR (loud);
    const float k = juce::jlimit (0.0f, 1.0f, (hue - 0.25f) * 1.6f);
    const auto warm = juce::Colour::fromFloatRGBA (1.0f, (90.0f + 80.0f * (1.0f - hue)) / 255.0f, 60.0f / 255.0f, 1.0f);
    const auto col = warm.interpolatedWith (C::cool, k);
    const float pulse = 0.5f + 0.5f * std::sin (phase);
    juce::ColourGradient glow (juce::Colour (0xf2fff5e6), cx, cy, juce::Colours::transparentBlack, cx + rc * 1.6f, cy, true);
    glow.addColour (0.22, col.withAlpha (0.85f));
    glow.addColour (0.45, col.withAlpha (0.18f + 0.08f * pulse));
    g.setGradientFill (glow);
    g.fillEllipse (cx - rc * 1.6f, cy - rc * 1.6f, rc * 3.2f, rc * 3.2f);

    // Kompressions-Bögen (Bass · Mitten · Höhen) und Limiter
    const juce::Colour arcCols[] = { C::plasma, C::ember, C::cool };
    const float third = juce::MathConstants<float>::twoPi / 3.0f;

    for (int i = 0; i < 3; ++i)
    {
        const float a0 = i * third + 0.12f, full = third - 0.24f;
        const float span = full * juce::jlimit (0.0f, 1.0f, state.bandGr[(size_t) i] / 8.0f);
        juce::Path bgArc, arc;
        bgArc.addCentredArc (cx, cy, R * 0.97f, R * 0.97f, 0.0f, a0, a0 + full, true);
        g.setColour (arcCols[i].withAlpha (0.22f));
        g.strokePath (bgArc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (span > 0.005f)
        {
            arc.addCentredArc (cx, cy, R * 0.97f, R * 0.97f, 0.0f, a0, a0 + span, true);
            g.setColour (arcCols[i]);
            g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    const float a0 = juce::MathConstants<float>::pi * 1.1f, sweep = juce::MathConstants<float>::pi * 1.8f;
    juce::Path limBg, lim;
    limBg.addCentredArc (cx, cy, R * 0.9f, R * 0.9f, 0.0f, a0, a0 + sweep, true);
    g.setColour (C::fg.withAlpha (0.1f));
    g.strokePath (limBg, juce::PathStrokeType (3.0f));
    const float limAmt = juce::jlimit (0.0f, 1.0f, state.limiterGr / 6.0f);
    if (limAmt > 0.01f)
    {
        lim.addCentredArc (cx, cy, R * 0.9f, R * 0.9f, 0.0f, a0, a0 + sweep * limAmt, true);
        g.setColour (C::bad);
        g.strokePath (lim, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Wert
    if (state.signal && state.outShort > -70)
    {
        drawOutlinedText (g, num (state.outShort), mono (R * 0.17f, 1), { cx - R, cy - R * 0.12f, R * 2, R * 0.24f }, juce::Colours::white);
        drawOutlinedText (g, "LUFS KURZZEIT", caps (R * 0.065f, 2, 0.1f), { cx - R, cy + R * 0.11f, R * 2, R * 0.09f }, juce::Colours::white.withAlpha (0.85f));
    }
    else
    {
        g.setColour (C::dim);
        g.setFont (caps (R * 0.07f, 2, 0.12f));
        g.drawText ("WARTET AUF AUDIO", juce::Rectangle<float> (cx - R, cy + R * 0.2f, R * 2, R * 0.1f), juce::Justification::centred);
    }

    g.setColour (C::faint);
    g.setFont (label (12.5f));
    g.drawText (de ("B\xc3\xb6gen: Kompression Bass \xc2\xb7 Mitten \xc2\xb7 H\xc3\xb6hen   \xc2\xb7   innen rot: Limiter"),
                b.removeFromBottom (18.0f), juce::Justification::centred);
}

//==============================================================================
MacroKnob::MacroKnob (const params::Macro& m, juce::AudioProcessorValueTreeState& s, std::function<void()> onUserChange)
    : macro (m), apvts (s), changed (std::move (onUserChange))
{
    big = juce::String (m.id) == "loud";
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (m.min, m.max, m.step);
    slider.setDoubleClickReturnValue (true, m.def);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setTitle (de (m.name));
    slider.setTooltip (de ("Ziehen nach oben/unten \xc2\xb7 Doppelklick = Ausgangswert"));
    slider.getProperties().set ("fromMin", big);
    slider.setValue (m.get (s), juce::dontSendNotification);

    slider.onDragStart = [this] { gesture = std::make_unique<params::GestureScope> (apvts, macro.touched()); };
    slider.onDragEnd = [this] { gesture.reset(); if (changed) changed(); };
    slider.onValueChange = [this]
    {
        if (syncing) return;
        macro.set (apvts, (float) slider.getValue());
        if (gesture == nullptr && changed) changed();
        repaint();
    };

    addAndMakeVisible (slider);
}

void MacroKnob::sync()
{
    if (slider.isMouseButtonDown() || gesture != nullptr)
        return;

    const auto v = (double) macro.get (apvts);

    if (std::abs (v - slider.getValue()) > 1e-4)
    {
        syncing = true;
        slider.setValue (v, juce::dontSendNotification);
        syncing = false;
        repaint();
    }
}

void MacroKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (big ? 46 : 42);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void MacroKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (big ? 46 : 42).toFloat();
    g.setColour (C::fg);
    g.setFont (caps (big ? 16.0f : 14.5f, 2, 0.14f));
    g.drawText (de (macro.name).toUpperCase(), r.removeFromTop (22.0f), juce::Justification::centred);
    g.setColour (C::dim);
    g.setFont (mono (big ? 16.0f : 14.5f));
    g.drawText (macro.format ((float) slider.getValue()), r.removeFromTop (20.0f), juce::Justification::centred);
}

//==============================================================================
ToneBalance::ToneBalance (KernfusionProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void ToneBalance::paint (juce::Graphics& g)
{
    const auto target = proc.getToneTarget();
    const auto outR = proc.outputAnalyser.getResults();
    const auto inR = proc.inputAnalyser.getResults();
    const auto& master = outR.integratedSeconds >= 3.0 && outR.integrated.valid ? outR.integrated : outR.shortTerm;
    const auto& orig = inR.integratedSeconds >= 3.0 && inR.integrated.valid ? inR.integrated : inR.shortTerm;

    auto r = getLocalBounds().toFloat();
    auto head = r.removeFromTop (28.0f);
    g.setColour (C::dim);
    g.setFont (caps (14.0f, 2, 0.14f));
    g.drawText (target != nullptr ? de ("KLANGBALANCE GEGEN \xe2\x80\x9e") + target->name.toUpperCase() + de ("\xe2\x80\x9c") : juce::String ("KLANGBALANCE"),
                head, juce::Justification::centredLeft, true);
    g.setColour (C::faint);
    g.setFont (label (13.0f));
    g.drawText (master.valid ? de ("live \xc2\xb7 orange = Master \xc2\xb7 grau = Original") : de ("Play dr\xc3\xbc" "cken, dann erscheint der Vergleich"),
                head, juce::Justification::centredRight, true);

    r.removeFromTop (6.0f);
    auto bars = r.removeFromLeft (juce::jlimit (240.0f, 320.0f, r.getWidth() * 0.32f));
    r.removeFromLeft (12.0f);
    paintBars (g, bars, target.get(), master, orig);
    paintCurve (g, r, target.get(), master, orig);
}

void ToneBalance::paintBars (juce::Graphics& g, juce::Rectangle<float> area, const ms::TargetCurve* t,
                             const ms::SpectrumSnapshot& master, const ms::SpectrumSnapshot& orig)
{
    g.setColour (C::vizBg);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    const char* names[] = { "Bass", "Tiefmitten", "Hochmitten", "H\xc3\xb6hen" };
    const float colW = (area.getWidth() - 16.0f) / 4.0f, scale = 9.0f;

    for (int r = 0; r < 4; ++r)
    {
        const float cx = area.getX() + 8.0f + colW * (r + 0.5f);
        const float top = area.getY() + 30.0f, bot = area.getBottom() - 42.0f, tw = juce::jmin (26.0f, colW - 24.0f);
        auto vy = [&] (float d) { return top + (scale - juce::jlimit (-scale, scale, d)) / (2 * scale) * (bot - top); };

        g.setColour (C::fg);
        g.setFont (label (13.5f, 1));
        g.drawFittedText (de (names[r]), juce::Rectangle<int> ((int) (cx - colW / 2), (int) area.getY() + 6, (int) colW, 18), juce::Justification::centred, 1, 0.7f);
        g.setColour (C::raised);
        g.fillRect (cx - tw / 2, top, tw, bot - top);

        if (t == nullptr || ! t->regions[(size_t) r].isValid())
            continue;

        const auto& p = t->regions[(size_t) r];
        const float lo = p.p10 - p.p50, hi = p.p90 - p.p50;
        g.setColour (C::cool.withAlpha (0.25f));
        g.fillRect (cx - tw / 2, vy (hi), tw, vy (lo) - vy (hi));
        const float dashes[] = { 3.0f, 3.0f };
        g.setColour (C::cool.withAlpha (0.9f));
        g.drawDashedLine ({ cx - tw / 2, vy (0), cx + tw / 2, vy (0) }, dashes, 2, 1.0f);

        if (orig.valid && std::isfinite (orig.regions[(size_t) r]))
        {
            g.setColour (C::dim.withAlpha (0.8f));
            g.fillRect (cx - tw / 2 - 5, vy (orig.regions[(size_t) r] - p.p50) - 1.5f, tw + 10, 3.0f);
        }

        if (master.valid && std::isfinite (master.regions[(size_t) r]))
        {
            const float d = master.regions[(size_t) r] - p.p50;
            const float out = d < lo ? d - lo : (d > hi ? d - hi : 0.0f);
            const auto colour = out == 0.0f ? C::ok : C::warn;
            g.setColour (colour);
            g.fillRoundedRectangle (cx - tw / 2 - 5, vy (d) - 2.5f, tw + 10, 5.0f, 2.0f);
            g.setFont (mono (14.0f, 1));
            g.drawText (signedNum (d), juce::Rectangle<float> (cx - colW / 2, bot + 4, colW, 18), juce::Justification::centred);
            g.setFont (label (13.0f));
            g.setColour (colour.withAlpha (0.9f));
            g.drawText (out == 0.0f ? de ("im Ziel") : (out > 0 ? de ("zu viel") : de ("zu wenig")),
                        juce::Rectangle<float> (cx - colW / 2, bot + 21, colW, 16), juce::Justification::centred);
        }
    }
}

void ToneBalance::paintCurve (juce::Graphics& g, juce::Rectangle<float> area, const ms::TargetCurve* t,
                              const ms::SpectrumSnapshot& master, const ms::SpectrumSnapshot& orig)
{
    g.setColour (C::vizBg);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    const auto A = area.withTrimmedLeft (36.0f).withTrimmedBottom (22.0f).withTrimmedTop (8.0f).withTrimmedRight (10.0f);
    auto tilt = [] (double f) { return 3.0 * std::log2 (f / 1000.0); };
    auto fx = [&] (double f) { return A.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * A.getWidth(); };

    double lo = 1e9, hi = -1e9;
    if (t != nullptr)
        for (int b = 0; b < ms::BandLayout::numBands; ++b)
            if (t->bands[(size_t) b].isValid())
            {
                const double k = tilt (ms::BandLayout::getCentre (b));
                lo = std::min (lo, t->bands[(size_t) b].p10 + k);
                hi = std::max (hi, t->bands[(size_t) b].p90 + k);
            }
    if (! (hi > lo)) { lo = -12; hi = 12; }
    lo = std::floor ((lo - 6) / 6) * 6; hi = std::ceil ((hi + 6) / 6) * 6;
    auto fy = [&] (double v) { return A.getY() + (float) ((hi - v) / (hi - lo)) * A.getHeight(); };

    g.setFont (mono (11.0f));
    for (const auto& [f, l] : std::initializer_list<std::pair<double, const char*>> { { 50, "50" }, { 200, "200" }, { 1000, "1k" }, { 5000, "5k" }, { 15000, "15k" } })
    {
        const float x = fx (f);
        g.setColour (C::grid);
        g.drawVerticalLine (juce::roundToInt (x), A.getY(), A.getBottom());
        g.setColour (C::faint);
        g.drawText (l, juce::Rectangle<float> (x - 20, A.getBottom() + 4, 40, 14), juce::Justification::centred);
    }

    for (double v = lo; v <= hi; v += 6)
    {
        const float y = fy (v);
        g.setColour (C::grid);
        g.drawHorizontalLine (juce::roundToInt (y), A.getX(), A.getRight());
        g.setColour (C::faint);
        g.drawText (v > 0 ? "+" + juce::String ((int) v) : (v < 0 ? de ("\xe2\x88\x92") + juce::String ((int) -v) : juce::String ("0")),
                    juce::Rectangle<float> (area.getX(), y - 7, 30, 14), juce::Justification::centredRight);
    }

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (A.toNearestInt());

    auto curve = [&] (auto&& get)
    {
        juce::Path p;
        bool started = false;
        for (int b = 0; b < ms::BandLayout::numBands; ++b)
        {
            const double v = get (b);
            if (! std::isfinite (v)) continue;
            const double f = ms::BandLayout::getCentre (b);
            const juce::Point<float> pt (fx (f), fy (v + tilt (f)));
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
        }
        return p;
    };

    if (t != nullptr)
    {
        juce::Path band;
        bool first = true;
        for (int b = 0; b < ms::BandLayout::numBands; ++b)
        {
            const auto& p = t->bands[(size_t) b];
            if (! p.isValid()) continue;
            const double f = ms::BandLayout::getCentre (b);
            const juce::Point<float> pt (fx (f), fy (p.p90 + tilt (f)));
            if (first) { band.startNewSubPath (pt); first = false; } else band.lineTo (pt);
        }
        for (int b = ms::BandLayout::numBands - 1; b >= 0; --b)
        {
            const auto& p = t->bands[(size_t) b];
            if (! p.isValid()) continue;
            const double f = ms::BandLayout::getCentre (b);
            band.lineTo (fx (f), fy (p.p10 + tilt (f)));
        }
        band.closeSubPath();
        g.setColour (C::cool.withAlpha (0.16f));
        g.fillPath (band);

        juce::Path med = curve ([&] (int b) { return t->bands[(size_t) b].isValid() ? (double) t->bands[(size_t) b].p50 : NAN; }), dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, med, dashes, 2);
        g.setColour (C::cool.withAlpha (0.9f));
        g.fillPath (dashed);
    }

    if (orig.valid)
    {
        g.setColour (C::dim.withAlpha (0.75f));
        g.strokePath (curve ([&] (int b) { return (double) orig.bands[(size_t) b]; }), juce::PathStrokeType (1.4f));
    }

    if (master.valid)
    {
        g.setColour (C::ember);
        g.strokePath (curve ([&] (int b) { return (double) master.bands[(size_t) b]; }), juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (C::faint);
    g.setFont (label (11.5f));
    g.drawText ("Neigung 3 dB/Okt", A.reduced (6.0f, 4.0f), juce::Justification::topLeft);
}

//==============================================================================
void NotesView::setNotes (const juce::AttributedString& s)
{
    text = s;
    repaint();
}

void NotesView::paint (juce::Graphics& g)
{
    juce::TextLayout layout;
    layout.createLayout (text, (float) getWidth());
    layout.draw (g, getLocalBounds().toFloat());
}

//==============================================================================
void OneClickView::FuseButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool enabled = isEnabled();

    if (progress >= 0.0f)
    {
        g.setColour (C::raised);
        g.fillRoundedRectangle (r, 10.0f);
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, progress)).toNearestInt());
        g.setGradientFill (plasmaGradient (r));
        g.fillRoundedRectangle (r, 10.0f);
        g.restoreState();
        g.setColour (C::plasma.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        g.setColour (C::fg);
    }
    else
    {
        g.setGradientFill (plasmaGradient (r));
        g.setOpacity (enabled ? (down ? 0.85f : (highlighted ? 1.0f : 0.95f)) : 0.4f);
        g.fillRoundedRectangle (r, 10.0f);
        g.setOpacity (1.0f);
        g.setColour (juce::Colour (0xff1a0b06));
    }

    g.setFont (caps (19.0f, 2, 0.16f));
    g.drawText (text, r, juce::Justification::centred);

    if (hasKeyboardFocus (false))
    {
        g.setColour (C::fg);
        g.drawRoundedRectangle (r.expanded (2.0f), 11.0f, 1.5f);
    }
}

OneClickView::OneClickView (KernfusionProcessor& p, const UiState& s)
    : proc (p), core (s, p.apvts), toneView (p)
{
    addAndMakeVisible (core);

    targetBox.addItem (de ("Streaming (\xe2\x88\x92") + "14 LUFS)", 1);
    targetBox.addItem (de ("Laut (\xe2\x88\x92") + "11 LUFS)", 2);
    targetBox.addItem (de ("Pop / Rock laut (\xe2\x88\x92") + "9 LUFS)", 3);
    targetBox.addItem (de ("Club / EDM (\xe2\x88\x92") + "7,5 LUFS)", 4);
    targetBox.setTooltip (de ("Wie laut der Master werden soll. Spotify normalisiert auf \xe2\x88\x92") + "14 LUFS.");
    targetBox.onChange = [this]
    {
        const int id = targetBox.getSelectedId();
        if (id >= 1 && id <= 4)
            params::setValue (proc.apvts, params::target, targetValues[id - 1]);
    };
    addAndMakeVisible (targetBox);

    toneBox.setTooltip (de ("Klangziel f\xc3\xbcr den Assistenten: Genre-Modell, Leitstern-Ziel oder eigene Referenztracks"));
    toneBox.onChange = [this] { toneSelected(); };
    addAndMakeVisible (toneBox);
    refreshToneBox();

    fuse.text = "EIN KLICK MASTERN";
    fuse.setTooltip (de ("Spiel den lautesten Teil deines Songs ab \xe2\x80\x93 Kernfusion h\xc3\xb6rt mit und stellt alles ein"));
    fuse.onClick = [this] { proc.oneClick(); };
    addAndMakeVisible (fuse);

    relistenButton.setButtonText (de ("Neu anh\xc3\xb6ren"));
    relistenButton.setTooltip (de ("Einen anderen Teil des Songs mith\xc3\xb6ren und neu mastern"));
    relistenButton.onClick = [this] { proc.oneClick (true); };
    finishButton.setButtonText ("Jetzt mastern");
    finishButton.onClick = [this] { proc.finishListening(); };
    cancelButton.setButtonText ("Abbrechen");
    cancelButton.onClick = [this] { proc.cancelListening(); };
    addChildComponent (relistenButton);
    addChildComponent (finishButton);
    addChildComponent (cancelButton);

    addAndMakeVisible (notes);

    for (const auto& m : params::macros())
    {
        const bool isLoud = juce::String (m.id) == "loud";
        knobs.push_back (std::make_unique<MacroKnob> (m, proc.apvts, isLoud ? std::function<void()> {} : std::function<void()> ([this] { proc.requestLoudnessHold(); })));
        addAndMakeVisible (*knobs.back());
    }

    addAndMakeVisible (toneView);
    syncTargetBox();
    updateNotes();
}

void OneClickView::refreshToneBox()
{
    toneBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& pr : tone::presets())
        toneBox.addItem (de (pr.name), id++);

    const auto key = proc.getToneKey();
    leitsternFiles = ms::TargetCurve::getUserTargetsFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    leitsternFiles.sort();

    if (! leitsternFiles.isEmpty())
    {
        toneBox.addSeparator();
        toneBox.addSectionHeading ("Leitstern-Ziele");
        for (int i = 0; i < leitsternFiles.size(); ++i)
            toneBox.addItem (leitsternFiles[i].getFileNameWithoutExtension(), 100 + i);
    }

    if (key.startsWith ("ref:"))
    {
        toneBox.addSeparator();
        toneBox.addItem ("Referenz: " + key.fromFirstOccurrenceOf ("ref:", false, false), 60);
    }

    toneBox.addSeparator();
    toneBox.addItem (de ("Referenztracks laden \xe2\x80\xa6"), 90);
    toneBox.addItem (de ("Leitstern-Zielordner \xc3\xb6" "ffnen \xe2\x80\xa6"), 91);

    // Auswahl wiederherstellen
    int sel = 0;
    if (key.startsWith ("preset:"))
    {
        const auto k = key.fromFirstOccurrenceOf ("preset:", false, false);
        for (int i = 0; i < (int) tone::presets().size(); ++i)
            if (k == tone::presets()[(size_t) i].key) sel = i + 1;
    }
    else if (key.startsWith ("file:"))
    {
        const auto n = key.fromFirstOccurrenceOf ("file:", false, false);
        for (int i = 0; i < leitsternFiles.size(); ++i)
            if (leitsternFiles[i].getFileNameWithoutExtension() == n) sel = 100 + i;
        if (sel == 0) toneBox.setText ("Leitstern: " + n, juce::dontSendNotification);
    }
    else if (key.startsWith ("ref:"))
        sel = 60;

    if (sel != 0)
        toneBox.setSelectedId (sel, juce::dontSendNotification);

    lastToneKey = key;
}

void OneClickView::toneSelected()
{
    const int id = toneBox.getSelectedId();

    if (id >= 1 && id <= (int) tone::presets().size())
    {
        const auto& pr = tone::presets()[(size_t) id - 1];
        proc.setToneTarget (std::make_shared<const ms::TargetCurve> (tone::makePreset (pr.key)), juce::String ("preset:") + pr.key);
        proc.setStatus (de ("Klangziel: ") + de (pr.name) + de (". \xe2\x80\x9e") + "Ein Klick mastern" + de ("\xe2\x80\x9c wendet es an."));
    }
    else if (id >= 100)
    {
        const auto f = leitsternFiles[id - 100];
        if (auto c = ms::TargetCurve::loadFromFile (f))
        {
            c->name = f.getFileNameWithoutExtension();
            proc.setToneTarget (std::make_shared<const ms::TargetCurve> (std::move (*c)), "file:" + f.getFileNameWithoutExtension());
            proc.setStatus (de ("Leitstern-Ziel geladen: ") + f.getFileNameWithoutExtension(), 1);
        }
        else
            proc.setStatus (f.getFileName() + de (" ist keine g\xc3\xbcltige Zielkurve."), 2);
    }
    else if (id == 90)
    {
        if (onChooseReferences) onChooseReferences();
    }
    else if (id == 91)
    {
        const auto folder = ms::TargetCurve::getUserTargetsFolder();
        folder.createDirectory();
        folder.revealToUser();
    }

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<OneClickView> (this)] { if (safe) safe->refreshToneBox(); });
}

void OneClickView::syncTargetBox()
{
    const float t = params::getValue (proc.apvts, params::target);
    for (int i = 0; i < 4; ++i)
        if (std::abs (targetValues[i] - t) < 0.01f)
        {
            if (targetBox.getSelectedId() != i + 1)
                targetBox.setSelectedId (i + 1, juce::dontSendNotification);
            return;
        }

    targetBox.setText ("Eigene (" + num (t) + " LUFS)", juce::dontSendNotification);
}

void OneClickView::updateNotes()
{
    const auto list = proc.getNotes();
    size_t h = list.size();
    for (const auto& n : list) h = h * 31 + (size_t) n.label.hashCode64() + (size_t) n.text.hashCode64() * 7;

    if (h == lastNotesHash && lastNotesHash != 0)
        return;

    lastNotesHash = h;
    juce::AttributedString s;
    s.setLineSpacing (3.0f);

    if (list.empty())
    {
        s.append (de ("So geht\xe2\x80\x99s: "), label (15.0f, 2), C::fg);
        s.append (de ("Ziel-Lautheit und Klangziel w\xc3\xa4hlen, Wiedergabe starten und \xe2\x80\x9e") + "Ein Klick mastern"
                  + de ("\xe2\x80\x9c dr\xc3\xbc" "cken. Kernfusion h\xc3\xb6rt etwa 20 Sekunden mit und stellt dann alle Module ein. Danach mit den Reglern rechts feinjustieren."),
                  label (15.0f), C::dim);
    }
    else
    {
        s.append (de ("Fertig gemastert.\n"), label (15.0f, 2), C::fg);
        for (const auto& n : list)
        {
            s.append (de ("\xe2\x80\xa2 ") + n.label + " ", label (14.5f, 2), C::fg);
            s.append (n.text + "\n", label (14.5f), C::dim);
        }
    }

    notes.setNotes (s);
}

void OneClickView::tick()
{
    core.tick();

    for (auto& k : knobs)
        k->sync();

    if (! targetBox.isPopupActive())
        syncTargetBox();

    if (proc.getToneKey() != lastToneKey && ! toneBox.isPopupActive())
        refreshToneBox();

    // Hauptknopf je nach Phase
    const auto phase = proc.getPhase();
    const float pr = proc.getPhaseProgress();

    if (phase == KernfusionProcessor::Phase::listening)
    {
        fuse.text = de ("H\xc3\x96RE MIT \xe2\x80\xa6 ") + juce::String (juce::roundToInt (pr * 100)) + " %";
        fuse.progress = pr;
    }
    else if (phase == KernfusionProcessor::Phase::working)
    {
        fuse.text = de ("MASTERE \xe2\x80\xa6 ") + juce::String (juce::roundToInt (pr * 100)) + " %";
        fuse.progress = pr;
    }
    else
    {
        fuse.text = "EIN KLICK MASTERN";
        fuse.progress = -1.0f;
    }

    fuse.setEnabled (phase == KernfusionProcessor::Phase::idle);
    fuse.repaint();

    const bool listening = phase == KernfusionProcessor::Phase::listening;
    const bool showRelisten = phase == KernfusionProcessor::Phase::idle && proc.hasCapture();
    const bool showFinish = listening && proc.canFinishListening();

    if (relistenButton.isVisible() != showRelisten || cancelButton.isVisible() != listening || finishButton.isVisible() != showFinish)
    {
        relistenButton.setVisible (showRelisten);
        cancelButton.setVisible (listening);
        finishButton.setVisible (showFinish);
        resized();
    }

    updateNotes();
    toneView.repaint();
}

void OneClickView::paint (juce::Graphics& g)
{
    drawPanel (g, leftPanel.toFloat());
    drawPanel (g, rightPanel.toFloat());

    g.setColour (C::dim);
    g.setFont (caps (12.5f, 2, 0.14f));
    g.drawText ("ZIEL-LAUTHEIT", targetLabel, juce::Justification::bottomLeft);
    g.drawText ("KLANGZIEL", toneLabel, juce::Justification::bottomLeft);

    g.setFont (caps (16.0f, 2, 0.16f));
    g.drawText ("FEINSCHLIFF", macroHead, juce::Justification::centredLeft);
    g.setColour (C::faint);
    g.setFont (label (13.5f));
    g.drawText (de ("Ziehen nach oben/unten \xc2\xb7 Doppelklick = Ausgangswert \xc2\xb7 Lautheit bleibt beim Drehen auf dem Ziel"),
                macroHead, juce::Justification::centredRight, true);

    g.setColour (C::line);
    g.drawHorizontalLine (toneHead.getY() - 8, (float) toneHead.getX(), (float) toneHead.getRight());
}

void OneClickView::resized()
{
    auto r = getLocalBounds();
    leftPanel = r.removeFromLeft (juce::jlimit (360, 480, juce::roundToInt ((float) r.getWidth() * 0.34f)));
    r.removeFromLeft (14);
    rightPanel = r;

    // links: Kern, Auswahl, Knopf, Hinweise
    auto L = leftPanel.reduced (18, 16);
    const int coreSize = juce::jmin (L.getWidth(), juce::jmax (220, L.getHeight() - 330));
    core.setBounds (L.removeFromTop (coreSize).withSizeKeepingCentre (L.getWidth(), coreSize));
    L.removeFromTop (8);
    auto labels = L.removeFromTop (20);
    auto boxes = L.removeFromTop (36);
    const int half = (L.getWidth() - 10) / 2;
    targetLabel = labels.removeFromLeft (half); labels.removeFromLeft (10); toneLabel = labels;
    targetBox.setBounds (boxes.removeFromLeft (half)); boxes.removeFromLeft (10); toneBox.setBounds (boxes);
    L.removeFromTop (12);
    fuse.setBounds (L.removeFromTop (52));
    L.removeFromTop (8);

    if (relistenButton.isVisible() || cancelButton.isVisible())
    {
        auto row = L.removeFromTop (30);
        if (relistenButton.isVisible()) relistenButton.setBounds (row.removeFromLeft (140));
        if (finishButton.isVisible()) { finishButton.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8); }
        if (cancelButton.isVisible()) cancelButton.setBounds (row.removeFromLeft (110));
        L.removeFromTop (10);
    }

    notes.setBounds (L);

    // rechts: Makros oben, Klangbalance unten
    auto R = rightPanel.reduced (20, 16);
    macroHead = R.removeFromTop (28);
    R.removeFromTop (8);
    const int toneH = juce::jlimit (230, 300, R.getHeight() / 2 - 20);
    auto toneArea = R.removeFromBottom (toneH);
    R.removeFromBottom (24);
    toneHead = toneArea;
    toneView.setBounds (toneArea);

    // großer Lautheits-Regler links, sechs Regler in 3×2 daneben
    auto bigArea = R.removeFromLeft (juce::jmin (240, R.getWidth() / 4));
    R.removeFromLeft (8);
    const int cellW = R.getWidth() / 3, cellH = R.getHeight() / 2;
    int i = 0;

    for (auto& k : knobs)
    {
        if (k->isBig())
        {
            k->setBounds (bigArea.withSizeKeepingCentre (bigArea.getWidth(), juce::jmin (bigArea.getHeight(), bigArea.getWidth() + 60)));
            continue;
        }

        const int col = i % 3, row = i / 3;
        k->setBounds (R.getX() + col * cellW, R.getY() + row * cellH, cellW, cellH);
        ++i;
    }
}

} // namespace kf::ui
