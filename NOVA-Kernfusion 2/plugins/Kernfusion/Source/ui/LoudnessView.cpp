#include "LoudnessView.h"
#include "../PluginProcessor.h"

namespace kf::ui
{

const std::array<LoudnessView::Platform, 7>& LoudnessView::platforms()
{
    // Spotify: offizielle Angaben (support.spotify.com). Übrige: verbreitete Richtwerte.
    static const std::array<Platform, 7> list {{
        { "Spotify", -14.0f, true, false, true },
        { "Spotify \xe2\x80\x9eLaut\xe2\x80\x9c", -11.0f, true, true, true },
        { "Apple Music", -16.0f, false, false, false },
        { "YouTube", -14.0f, false, false, false },
        { "Amazon Music", -14.0f, true, false, false },
        { "TIDAL", -14.0f, true, false, false },
        { "Deezer", -15.0f, true, false, false },
    }};
    return list;
}

std::pair<float, juce::String> LoudnessView::platformChange (const Platform& p, float I, float tp)
{
    const float want = p.target - I;

    if (want <= 0)
        return { want, std::abs (want) < 0.3f ? de ("l\xc3\xa4uft praktisch unver\xc3\xa4ndert") : de ("wird um ") + num (-want) + de (" dB leiser gemacht") };

    if (! p.turnsUp)
        return { 0.0f, de ("bleibt ") + num (want) + de (" dB unter dem Ziel (wird nicht angehoben)") };

    if (p.limiter)
        return { want, de ("wird um ") + num (want) + de (" dB angehoben, Spotifys Limiter f\xc3\xa4ngt die Spitzen ab") };

    const float room = juce::jmax (0.0f, -1.0f - tp), g = juce::jmin (want, room);
    return { g, g < want - 0.05f ? de ("nur um ") + num (g) + de (" dB angehoben (Spitzen lassen nicht mehr zu)")
                                 : de ("wird um ") + num (g) + de (" dB angehoben") };
}

LoudnessView::LoudnessView (KernfusionProcessor& p, const UiState& s) : proc (p), state (s)
{
    for (int i = 0; i < 7; ++i)
    {
        auto& b = listen[(size_t) i];
        b.setButtonText (de ("Anh\xc3\xb6ren"));
        b.setTooltip (de ("Deinen Master so laut h\xc3\xb6ren, wie ihn diese Plattform abspielen w\xc3\xbc" "rde"));
        b.onClick = [this, i]
        {
            if (proc.getPreview() == i) { proc.setPreview (-1, 0.0f); return; }
            const auto ch = platformChange (platforms()[(size_t) i], state.outIntegrated, state.outTruePeak);
            proc.setPreview (i, ch.first);
        };
        addAndMakeVisible (b);
    }

    resetButton.setButtonText (de ("Messung neu starten"));
    resetButton.onClick = [this] { proc.engine.resetRequested = true; proc.outputAnalyser.resetIntegrated(); proc.inputAnalyser.resetIntegrated(); };
    previewOffButton.setButtonText ("Vorschau aus");
    previewOffButton.onClick = [this] { proc.setPreview (-1, 0.0f); };
    ceilingButton.setButtonText (de ("Ceiling auf \xe2\x88\x92") + "2 dBTP");
    ceilingButton.onClick = [this] { params::setValue (proc.apvts, "max_ceil", -2.0f); proc.requestLoudnessHold(); };
    addAndMakeVisible (resetButton);
    addAndMakeVisible (previewOffButton);
    addChildComponent (ceilingButton);
}

void LoudnessView::tick()
{
    const int prev = proc.getPreview();

    for (int i = 0; i < 7; ++i)
    {
        listen[(size_t) i].setToggleState (prev == i, juce::dontSendNotification);
        listen[(size_t) i].setButtonText (prev == i ? de ("L\xc3\xa4uft") : de ("Anh\xc3\xb6ren"));
        listen[(size_t) i].setEnabled (state.outIntegrated > -70);
    }

    // Vorschau-Pegel nachführen, falls sich die Messung ändert
    if (prev >= 0)
        proc.setPreview (prev, platformChange (platforms()[(size_t) prev], state.outIntegrated, state.outTruePeak).first);

    previewOffButton.setEnabled (prev >= 0);
    const bool tip = state.outIntegrated > -14.0f && state.outTruePeak > -2.0f && params::getValue (proc.apvts, "max_ceil") > -1.95f;

    if (tip != showTip)
    {
        showTip = tip;
        ceilingButton.setVisible (tip);
        resized();
    }

    repaint();
}

void LoudnessView::resized()
{
    auto r = getLocalBounds();
    ladderArea = r.removeFromLeft (juce::jlimit (300, 380, r.getWidth() / 4));
    r.removeFromLeft (14);
    verdictArea = r.removeFromTop (showTip ? 282 : 242);
    r.removeFromTop (14);
    tableArea = r;

    auto v = verdictArea.reduced (20, 16);
    titleArea = v.removeFromTop (34);  v.removeFromTop (6);
    textArea = v.removeFromTop (44);   v.removeFromTop (8);
    statsArea = v.removeFromTop (74);  v.removeFromTop (10);

    if (showTip)
    {
        tipArea = v.removeFromTop (30);
        ceilingButton.setBounds (tipArea.removeFromRight (170));
        v.removeFromTop (10);
    }

    auto buttons = v.removeFromTop (32);
    resetButton.setBounds (buttons.removeFromLeft (170));
    buttons.removeFromLeft (8);
    previewOffButton.setBounds (buttons.removeFromLeft (120));

    auto t = tableArea.reduced (14, 10);
    t.removeFromTop (34);
    t.removeFromBottom (40);
    const int rowH = juce::jmin (46, t.getHeight() / 7);

    for (int i = 0; i < 7; ++i)
    {
        rowAreas[(size_t) i] = t.removeFromTop (rowH);
        listen[(size_t) i].setBounds (rowAreas[(size_t) i].removeFromRight (100).withSizeKeepingCentre (96, 30));
    }
}

void LoudnessView::paint (juce::Graphics& g)
{
    drawPanel (g, ladderArea.toFloat());
    drawPanel (g, verdictArea.toFloat());
    drawPanel (g, tableArea.toFloat());
    paintLadder (g, ladderArea.toFloat());
    paintVerdict (g, verdictArea.toFloat());
    paintTable (g, tableArea.toFloat());
}

void LoudnessView::paintLadder (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float top = r.getY() + 26, bot = r.getBottom() - 26, lo = -24, hi = -4;
    auto y = [&] (float v) { return top + (hi - v) / (hi - lo) * (bot - top); };
    const float x0 = r.getX() + 64, x1 = juce::jmin (r.getRight() - 150.0f, x0 + 120.0f);

    struct Zone { float a, b; const char* name; juce::Colour c; float alpha; };
    const Zone zones[] = { { -24, -16, "DYNAMISCH / LEISE", C::cool, 0.12f }, { -16, -13, "STREAMING-NORM", C::ok, 0.16f },
                           { -13, -9, "LAUT", C::ember, 0.14f }, { -9, -6, "INDUSTRIE / CLUB", C::plasma, 0.2f }, { -6, -4, "EXTREM", C::bad, 0.22f } };

    for (const auto& z : zones)
    {
        g.setColour (z.c.withAlpha (z.alpha));
        g.fillRect (x0, y (z.b), x1 - x0, y (z.a) - y (z.b));
        g.setColour (z.c.withAlpha (0.95f));
        g.setFont (caps (12.0f, 2, 0.08f));
        g.drawText (z.name, juce::Rectangle<float> (x0 + 8, y (z.b) + 4, x1 - x0 - 10, 16), juce::Justification::centredLeft);
    }

    g.setFont (mono (11.5f));
    for (float v = hi; v >= lo; v -= 2)
    {
        g.setColour (C::grid);
        g.drawHorizontalLine (juce::roundToInt (y (v)), x0 - 6, x0);
        g.setColour (C::faint);
        g.drawText (num (v, 0), juce::Rectangle<float> (r.getX() + 8, y (v) - 7, x0 - r.getX() - 18, 14), juce::Justification::centredRight);
    }

    // Plattform-Marken
    const std::pair<float, const char*> marks[] = { { -14, "Spotify \xc2\xb7 YouTube \xc2\xb7 Amazon \xc2\xb7 TIDAL" }, { -11, "Spotify Laut" },
                                                    { -15, "Deezer" }, { -16, "Apple Music" }, { -19, "Spotify Leise" } };
    g.setFont (label (12.5f));
    const float dashes[] = { 3.0f, 3.0f };
    for (const auto& [v, name] : marks)
    {
        g.setColour (C::fg.withAlpha (0.35f));
        g.drawDashedLine ({ x0, y (v), x1 + 6, y (v) }, dashes, 2, 1.0f);
        g.setColour (C::dim);
        g.drawFittedText (de (name), juce::Rectangle<int> ((int) x1 + 10, (int) y (v) - 8, (int) (r.getRight() - x1 - 16), 16), juce::Justification::centredLeft, 1, 0.8f);
    }

    auto marker = [&] (float v, juce::Colour c, const juce::String& text, float thick)
    {
        if (! (v > -70)) return;
        const float yy = y (juce::jlimit (lo, hi, v));
        g.setColour (c);
        g.fillRect (x0 - 2, yy - thick / 2, x1 - x0 + 4, thick);
        juce::Path arrow;
        arrow.addTriangle (x1 + 2, yy, x1 + 9, yy - 5, x1 + 9, yy + 5);
        g.fillPath (arrow);
        g.setColour (C::bg);
        g.setFont (caps (12.0f, 2, 0.05f));
        g.drawText (text, juce::Rectangle<float> (x0, yy - thick / 2, x1 - x0, thick), juce::Justification::centred);
    };

    marker (state.inIntegrated, C::dim.withAlpha (0.9f), "VORHER " + lufs (state.inIntegrated), 16);
    marker (state.outIntegrated, C::fg, "MASTER " + lufs (state.outIntegrated), 20);

    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getX() + 16, r.getCentreY()));
    g.setColour (C::faint);
    g.setFont (caps (11.5f, 2));
    g.drawText ("LUFS INTEGRIERT", juce::Rectangle<float> (r.getX() + 16 - 80, r.getCentreY() - 8, 160, 16), juce::Justification::centred);
    g.restoreState();
}

void LoudnessView::paintVerdict (juce::Graphics& g, juce::Rectangle<float>)
{
    const float I = state.outIntegrated, tp = state.outTruePeak, plr = tp - I;
    const bool has = I > -70;

    juce::String title, text;
    if (! has)                { title = "Noch keine Messung"; text = de ("Spiel den Song einmal komplett ab \xe2\x80\x93 Kernfusion misst den Master nach ITU-R BS.1770 und zeigt dir, ob er laut genug ist und was die Plattformen damit machen."); }
    else if (I >= -6)         { title = "Extrem laut"; text = de ("Lauter als fast alles, was ver\xc3\xb6" "ffentlicht wird. Verzerrung und Pumpen sind wahrscheinlich \xe2\x80\x93 und alle Plattformen drehen dich deutlich herunter."); }
    else if (I >= -9)         { title = "Industrie-laut"; text = de ("So laut wie aktuelle Club-, EDM- und Pop-Releases. F\xc3\xbcr DJ-Sets und Clubs passt das. Spotify & Co. drehen dich herunter \xe2\x80\x93 mehr Lautheit bringt dort nichts mehr."); }
    else if (I >= -12.5f)     { title = "Laut genug"; text = de ("Kommerziell konkurrenzf\xc3\xa4hig f\xc3\xbcr die meisten Genres und bereit f\xc3\xbcr alle Streaming-Plattformen. Im Club etwas leiser als die lautesten Tracks."); }
    else if (I >= -16)        { title = "Streaming-optimiert"; text = de ("Spotify und YouTube spielen dich nahezu unver\xc3\xa4ndert ab, du beh\xc3\xa4ltst die volle Dynamik. Neben lauten Club-Tracks wirkt der Song leiser."); }
    else                      { title = "Zu leise"; text = de ("Spotify hebt dich nur an, wenn die Spitzen Platz lassen, Apple Music und YouTube gar nicht. Du klingst dort leiser als andere Songs."); }

    auto head = titleArea.toFloat();
    g.setColour (C::fg);
    g.setFont (label (30.0f, 2));
    const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title);
    g.drawText (title, head, juce::Justification::centredLeft);

    // Status-Pille: wie lange gemessen wurde
    const auto secs = state.measuredSeconds;
    const juce::String pill = has ? de ("misst seit ") + juce::String ((int) secs / 60) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2)
                                  : juce::String ("wartet auf Audio");
    g.setFont (label (13.0f, 2));
    const float pw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), pill) + 18.0f;
    const auto pr = juce::Rectangle<float> (head.getX() + tw + 14, head.getCentreY() - 11, pw, 22);
    g.setColour ((has ? C::ok : C::warn).withAlpha (0.18f));
    g.fillRoundedRectangle (pr, 11.0f);
    g.setColour (has ? C::ok : C::warn);
    g.drawText (pill, pr, juce::Justification::centred);

    g.setColour (C::dim);
    g.setFont (label (15.5f));
    g.drawFittedText (text, textArea, juce::Justification::topLeft, 2, 0.9f);

    // Kennzahlen
    const bool tpOk = tp <= params::getValue (proc.apvts, "max_ceil") + 0.05f;
    const juce::String dyn = ! has ? juce::String() : plr < 7 ? de ("sehr dicht") : plr < 9.5f ? de ("dicht, typisch f\xc3\xbcr laute Master") : plr < 13 ? de ("ausgewogen, mit Punch") : de ("sehr dynamisch");
    struct S { const char* name; juce::String value, sub; juce::Colour c; };
    const S stats[] = {
        { "INTEGRIERT", lufs (I), "LUFS, seit Messstart", C::fg },
        { "TRUE PEAK", tp > -90 ? num (tp) : de ("\xe2\x80\x93"), tpOk ? de ("dBTP \xc2\xb7 sicher f\xc3\xbcrs Encoding") : de ("dBTP \xc2\xb7 \xc3\xbc" "ber dem Ceiling"), tpOk ? C::fg : C::bad },
        { "DYNAMIK (PLR)", has ? num (plr) : de ("\xe2\x80\x93"), de ("dB \xc2\xb7 ") + dyn, C::fg },
        { "LAUTHEITSUMFANG", has ? num (state.outLra) : de ("\xe2\x80\x93"), "LU (LRA)", C::fg },
    };

    auto row = statsArea.toFloat();
    const float w = (row.getWidth() - 30.0f) / 4.0f;

    for (const auto& st : stats)
    {
        auto c = row.removeFromLeft (w);
        row.removeFromLeft (10.0f);
        g.setColour (C::raised);
        g.fillRoundedRectangle (c, 10.0f);
        g.setColour (C::line);
        g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);
        c = c.reduced (12.0f, 8.0f);
        g.setColour (C::dim);
        g.setFont (caps (12.0f, 2));
        g.drawText (st.name, c.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (st.c);
        g.setFont (mono (24.0f, 1));
        g.drawText (st.value, c.removeFromTop (28.0f), juce::Justification::centredLeft);
        g.setColour (C::dim);
        g.setFont (label (12.5f));
        g.drawText (st.sub, c, juce::Justification::centredLeft, true);
    }

    if (showTip)
    {
        auto t = tipArea.toFloat();
        g.setColour (C::warn.withAlpha (0.18f));
        g.fillRoundedRectangle (t.removeFromLeft (44.0f).withSizeKeepingCentre (44, 22), 11.0f);
        g.setColour (C::warn);
        g.setFont (label (13.0f, 2));
        g.drawText ("Tipp", tipArea.toFloat().removeFromLeft (44.0f), juce::Justification::centred);
        g.setColour (C::dim);
        g.setFont (label (14.5f));
        g.drawFittedText (de ("Spotify empfiehlt f\xc3\xbcr Master lauter als \xe2\x88\x92") + "14 LUFS einen True Peak unter " + de ("\xe2\x88\x92") + "2 dBTP.",
                          tipArea.withTrimmedLeft (54), juce::Justification::centredLeft, 1, 0.8f);
    }
}

void LoudnessView::paintTable (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto t = r.reduced (14.0f, 10.0f);
    auto head = t.removeFromTop (34.0f);
    const float cName = 0.22f, cTarget = 0.12f, cPeaks = 0.14f;
    const float W = t.getWidth() - 100.0f;

    g.setColour (C::faint);
    g.setFont (caps (12.0f, 2));
    auto h = head;
    g.drawText ("PLATTFORM", h.removeFromLeft (W * cName), juce::Justification::centredLeft);
    g.drawText ("ZIEL", h.removeFromLeft (W * cTarget), juce::Justification::centredLeft);
    g.drawText ("WAS MIT DEINEM MASTER PASSIERT", h.removeFromLeft (W * (1.0f - cName - cTarget - cPeaks)), juce::Justification::centredLeft);
    g.drawText ("PEAKS", h.removeFromLeft (W * cPeaks), juce::Justification::centredLeft);
    g.setColour (C::line);
    g.drawHorizontalLine ((int) head.getBottom(), t.getX(), t.getRight());

    const float I = state.outIntegrated, tp = state.outTruePeak;
    const bool has = I > -70;
    const int prev = proc.getPreview();

    for (int i = 0; i < 7; ++i)
    {
        const auto& p = platforms()[(size_t) i];
        auto row = rowAreas[(size_t) i].toFloat();

        if (prev == i)
        {
            g.setColour (C::cool.withAlpha (0.1f));
            g.fillRect (rowAreas[(size_t) i].toFloat().withRight (t.getRight()));
        }

        g.setColour (C::grid);
        g.drawHorizontalLine ((int) row.getBottom(), t.getX(), t.getRight());

        auto c = row.removeFromLeft (W * cName);
        g.setColour (C::fg);
        g.setFont (label (16.0f, 2));
        g.drawText (de (p.name), c, juce::Justification::centredLeft);
        if (! p.official)
        {
            g.setColour (C::faint);
            g.setFont (label (12.5f));
            g.drawText ("Richtwert", c.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (label (16.0f, 2), de (p.name)) + 8.0f), juce::Justification::centredLeft);
        }

        g.setColour (C::fg);
        g.setFont (mono (14.0f));
        g.drawText (num (p.target, 0) + " LUFS", row.removeFromLeft (W * cTarget), juce::Justification::centredLeft);

        g.setFont (label (15.0f));
        g.setColour (has ? C::fg : C::faint);
        g.drawText (has ? platformChange (p, I, tp).second : de ("\xe2\x80\x93"), row.removeFromLeft (W * (1.0f - cName - cTarget - cPeaks)).withTrimmedRight (8), juce::Justification::centredLeft, true);

        if (has)
        {
            const bool over = tp > -0.95f;
            const bool better2 = ! over && i == 0 && I > -14 && tp > -2.0f;
            const juce::String txt = over ? de ("\xc3\xbc" "ber \xe2\x88\x92") + "1 dBTP" : (better2 ? de ("besser \xe2\x88\x92") + "2" : juce::String ("ok"));
            const auto col = over ? C::bad : (better2 ? C::warn : C::ok);
            g.setFont (label (13.0f, 2));
            const float pw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 16.0f;
            const auto pill = row.removeFromLeft (W * cPeaks).withWidth (pw).withSizeKeepingCentre (pw, 22);
            g.setColour (col.withAlpha (0.18f));
            g.fillRoundedRectangle (pill, 11.0f);
            g.setColour (col);
            g.drawText (txt, pill, juce::Justification::centred);
        }
    }

    g.setColour (C::faint);
    g.setFont (label (12.5f));
    g.drawFittedText (de ("Spotify: offizielle Angaben (Normal \xe2\x88\x92") + "14, Laut " + de ("\xe2\x88\x92") + "11, Leise " + de ("\xe2\x88\x92") + "19 LUFS; True Peak unter "
                          + de ("\xe2\x88\x92") + "1 dBTP, " + de ("\xc3\xbc") + "ber " + de ("\xe2\x88\x92") + "14 LUFS besser " + de ("\xe2\x88\x92")
                          + de ("2). \xc3\x9c") + "brige Plattformen: verbreitete Richtwerte. " + de ("\xe2\x80\x9e") + "Anh" + de ("\xc3\xb6") + "ren" + de ("\xe2\x80\x9c")
                          + " spielt den Master so laut, wie die Plattform ihn abspielen " + de ("w\xc3\xbc") + "rde " + de ("\xe2\x80\x93 vor dem Bounce ausschalten."),
                      r.reduced (14.0f, 8.0f).removeFromBottom (36.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
}

} // namespace kf::ui
