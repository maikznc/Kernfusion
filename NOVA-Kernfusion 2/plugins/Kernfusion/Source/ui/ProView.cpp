#include "ProView.h"
#include "../PluginProcessor.h"

namespace kf::ui
{

namespace
{
    const char* moduleNames[] = { "Pre-EQ", "Dynamik", "Exciter", "Imager", "Post-EQ", "Maximizer" };
    const char* modulePowerIds[] = { "pre_on", "dyn_on", "exc_on", "img_on", "post_on", "max_on" };
    const char* bandNames[] = { "Low Cut", "Tiefen", "Bell 1", "Bell 2", "H\xc3\xb6hen" };
    const char* threeBands[] = { "Bass", "Mitten", "H\xc3\xb6hen" };
}

//==============================================================================
SliderRow::SliderRow (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& labelText)
    : param (s.getParameter (id)), name (labelText)
{
    jassert (param != nullptr);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setTitle (labelText);
    addAndMakeVisible (slider);

    if (param != nullptr)
    {
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, nullptr);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }
}

void SliderRow::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedLeft (-2).withTrimmedRight (-2));
}

void SliderRow::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (20).toFloat();
    g.setColour (C::dim);
    g.setFont (label (14.0f));
    g.drawText (name, top, juce::Justification::centredLeft);

    if (param != nullptr)
    {
        g.setColour (C::fg);
        g.setFont (mono (13.0f));
        g.drawText (param->getCurrentValueAsText(), top, juce::Justification::centredRight);
    }
}

//==============================================================================
Group::Group (juce::String t) : title (std::move (t)) {}

SliderRow& Group::add (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& labelText)
{
    auto* r = rows.add (new SliderRow (s, id, labelText));
    addAndMakeVisible (r);
    return *r;
}

void Group::addExtra (juce::Component& c, int height)
{
    extras.emplace_back (&c, height);
    addAndMakeVisible (c);
}

int Group::idealHeight() const
{
    int h = 40 + (meter >= 0 ? 14 : 0) + rows.size() * 48;
    for (const auto& e : extras) h += e.second + 8;
    return h + 6;
}

void Group::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (C::raised);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    auto head = r.reduced (12.0f, 0.0f).withHeight (36.0f);
    g.setColour (C::dim);
    g.setFont (caps (13.5f, 2, 0.12f));
    g.drawText (title.toUpperCase(), head, juce::Justification::centredLeft);

    if (headerValue)
    {
        g.setFont (mono (13.0f));
        g.drawText (headerValue(), head, juce::Justification::centredRight);
    }

    if (meter >= 0)
    {
        auto bar = r.reduced (12.0f, 0.0f).withY (r.getY() + 36.0f).withHeight (6.0f);
        g.setColour (C::line);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (C::ember);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, meter)), 3.0f);
    }
}

void Group::resized()
{
    auto r = getLocalBounds().reduced (12, 0);
    r.removeFromTop (meter >= 0 ? 50 : 38);

    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (42));
        r.removeFromTop (6);
    }

    for (auto& [c, h] : extras)
    {
        c->setBounds (r.removeFromTop (h).withWidth (juce::jmin (r.getWidth(), 150)));
        r.removeFromTop (8);
    }
}

//==============================================================================
/** EQ-Kurve mit ziehbaren Punkten und dem Spektrum dahinter. */
class ModulePanel::EqGraph : public juce::Component
{
public:
    EqGraph (KernfusionProcessor& p, juce::String pre) : proc (p), reader (p.apvts), prefix (std::move (pre)) {}

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (C::vizBg);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (C::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        const auto A = area();
        g.setFont (mono (11.0f));

        for (int db = -12; db <= 12; db += 6)
        {
            const float y = dy (db);
            g.setColour (db == 0 ? C::line : C::grid);
            g.drawHorizontalLine (juce::roundToInt (y), A.getX(), A.getRight());
            g.setColour (C::faint);
            g.drawText (db > 0 ? "+" + juce::String (db) : (db < 0 ? de ("\xe2\x88\x92") + juce::String (-db) : juce::String ("0")),
                        juce::Rectangle<float> (r.getX(), y - 7, A.getX() - r.getX() - 6, 14), juce::Justification::centredRight);
        }

        for (const auto& [f, l] : std::initializer_list<std::pair<double, const char*>> { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } })
        {
            const float x = fx (f);
            g.setColour (C::grid);
            g.drawVerticalLine (juce::roundToInt (x), A.getY(), A.getBottom());
            g.setColour (C::faint);
            g.drawText (l, juce::Rectangle<float> (x - 20, A.getBottom() + 4, 40, 14), juce::Justification::centred);
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (A.toNearestInt());

        // Spektrum (Pre-EQ: Eingang, Post-EQ: Ausgang), 3 dB/Okt geneigt
        const auto res = (prefix == "pre" ? proc.inputAnalyser : proc.outputAnalyser).getResults();
        if (res.shortTerm.valid)
        {
            juce::Path p;
            bool started = false;
            for (int b = 0; b < ms::BandLayout::numBands; ++b)
            {
                const float v = res.shortTerm.bands[(size_t) b];
                if (! std::isfinite (v)) continue;
                const double f = ms::BandLayout::getCentre (b);
                const float y = A.getBottom() - juce::jlimit (0.0f, 1.0f, (float) ((v + 3.0 * std::log2 (f / 1000.0) + 30.0) / 45.0)) * A.getHeight();
                if (! started) { p.startNewSubPath (fx (f), A.getBottom()); started = true; }
                p.lineTo (fx (f), y);
            }
            if (started)
            {
                p.lineTo (A.getRight(), A.getBottom());
                p.closeSubPath();
                g.setGradientFill (juce::ColourGradient (C::cool.withAlpha (0.28f), 0, A.getY(), C::cool.withAlpha (0.02f), 0, A.getBottom(), false));
                g.fillPath (p);
            }
        }

        // EQ-Kurve
        const auto eq = params();
        juce::Path curve;
        for (float x = A.getX(); x <= A.getRight(); x += 2.0f)
        {
            const float y = dy ((float) juce::jlimit (-14.0, 14.0, eqResponseDb (eq, 48000.0, xf (x))));
            if (x == A.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (eq.on ? C::ember : C::faint);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Punkte
        for (int i = 0; i < 5; ++i)
        {
            const auto& b = eq.bands[(size_t) i];
            const bool active = b.on && (b.type == FilterType::highpass || std::abs (b.gain) > 0.05f);
            const auto c = juce::Rectangle<float> (16.0f, 16.0f).withCentre (pointPos (b));
            g.setColour (active ? C::plasma : C::raised);
            g.fillEllipse (c);
            g.setColour (i == dragging ? C::fg : (active ? C::ember : C::dim));
            g.drawEllipse (c, 1.5f);
            g.setColour (C::fg);
            g.setFont (mono (10.5f, 1));
            g.drawText (juce::String (i + 1), c, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = pick (e.position);
        if (dragging >= 0)
        {
            juce::StringArray ids { params::eq (prefix.toRawUTF8(), dragging, "f") };
            ids.add (dragging == 0 ? params::eq (prefix.toRawUTF8(), 0, "on") : params::eq (prefix.toRawUTF8(), dragging, "g"));
            gesture = std::make_unique<params::GestureScope> (proc.apvts, ids);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        params::setValue (proc.apvts, params::eq (prefix.toRawUTF8(), dragging, "f"), (float) juce::jlimit (20.0, 20000.0, xf (e.position.x)));
        if (dragging == 0) params::setValue (proc.apvts, params::eq (prefix.toRawUTF8(), 0, "on"), 1.0f);
        else params::setValue (proc.apvts, params::eq (prefix.toRawUTF8(), dragging, "g"), juce::jlimit (-12.0f, 12.0f, gainAt (e.position.y)));
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = -1;
        gesture.reset();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int i = pick (e.position);
        if (i < 0) return;
        const auto id = params::eq (prefix.toRawUTF8(), i, "q");
        params::setValue (proc.apvts, id, juce::jlimit (0.3f, 4.0f, params::getValue (proc.apvts, id) * (w.deltaY > 0 ? 1.1f : 0.9f)));
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = pick (e.position);
        if (i > 0) params::setValue (proc.apvts, params::eq (prefix.toRawUTF8(), i, "g"), 0.0f);
        else if (i == 0) params::setValue (proc.apvts, params::eq (prefix.toRawUTF8(), 0, "on"), 0.0f);
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().withTrimmedLeft (40).withTrimmedRight (12).withTrimmedTop (10).withTrimmedBottom (24); }
    float fx (double f) const { const auto A = area(); return A.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * A.getWidth(); }
    double xf (float x) const { const auto A = area(); return 20.0 * std::pow (1000.0, (x - A.getX()) / A.getWidth()); }
    float dy (float db) const { const auto A = area(); return A.getY() + (12.0f - db) / 24.0f * A.getHeight(); }
    float gainAt (float y) const { const auto A = area(); return std::round ((12.0f - (y - A.getY()) / A.getHeight() * 24.0f) * 10.0f) / 10.0f; }
    juce::Point<float> pointPos (const EqBand& b) const { return { fx (b.freq), dy (b.type == FilterType::highpass ? 0.0f : b.gain) }; }

    const EqParams params() const { const auto p = reader.read(); return prefix == "pre" ? p.pre : p.post; }

    int pick (juce::Point<float> pos) const
    {
        const auto eq = params();
        int best = -1;
        float bd = 20.0f;
        for (int i = 0; i < 5; ++i)
        {
            const float d = pos.getDistanceFrom (pointPos (eq.bands[(size_t) i]));
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

    KernfusionProcessor& proc;
    kf::params::Reader reader;
    juce::String prefix;
    int dragging = -1;
    std::unique_ptr<params::GestureScope> gesture;
};

/** Goniometer (Stereobild) mit Korrelationsanzeige. */
class ModulePanel::Goniometer : public juce::Component
{
public:
    explicit Goniometer (const UiState& s) : state (s) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (C::vizBg);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (C::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        const float cx = r.getCentreX(), cy = r.getY() + (r.getHeight() - 40.0f) / 2.0f + 8.0f;
        const float rad = juce::jmin (r.getWidth(), r.getHeight() - 40.0f) / 2.0f - 12.0f;
        g.setColour (C::grid);
        g.drawLine (cx - rad, cy, cx + rad, cy);
        g.drawLine (cx, cy - rad, cx, cy + rad);
        g.drawLine (cx - rad * 0.7f, cy - rad * 0.7f, cx + rad * 0.7f, cy + rad * 0.7f);
        g.drawLine (cx + rad * 0.7f, cy - rad * 0.7f, cx - rad * 0.7f, cy + rad * 0.7f);

        g.setColour (C::cool.withAlpha (0.55f));
        for (int i = 0; i < state.scopeCount; ++i)
        {
            const float l = state.scope[(size_t) i * 2], rr = state.scope[(size_t) i * 2 + 1];
            const float m = (l + rr) * 0.7071f, s = (l - rr) * 0.7071f;
            g.fillRect (cx + juce::jlimit (-1.0f, 1.0f, s) * rad - 1.0f, cy - juce::jlimit (-1.0f, 1.0f, m) * rad - 1.0f, 2.0f, 2.0f);
        }

        g.setColour (C::faint);
        g.setFont (caps (11.0f, 2));
        g.drawText ("MONO", juce::Rectangle<float> (cx - 40, r.getY() + 4, 80, 14), juce::Justification::centred);

        const float corr = state.correlation;
        auto bar = r.reduced (16.0f, 0.0f).withY (r.getBottom() - 16.0f).withHeight (6.0f);
        g.setColour (C::raised);
        g.fillRect (bar);
        const float x0 = bar.getCentreX(), x1 = bar.getX() + (corr + 1.0f) / 2.0f * bar.getWidth();
        g.setColour (corr < 0 ? C::bad : (corr < 0.3f ? C::warn : C::ok));
        g.fillRect (juce::jmin (x0, x1), bar.getY(), juce::jmax (2.0f, std::abs (x1 - x0)), bar.getHeight());
        g.setColour (C::dim);
        g.setFont (mono (11.0f));
        g.drawText ("Korrelation " + num (corr, 2), bar.withY (bar.getY() - 18.0f).withHeight (14.0f), juce::Justification::centred);
        g.drawText (de ("\xe2\x88\x92") + "1", bar.withY (bar.getY() - 18.0f).withHeight (14.0f), juce::Justification::centredLeft);
        g.drawText ("+1", bar.withY (bar.getY() - 18.0f).withHeight (14.0f), juce::Justification::centredRight);
    }

private:
    const UiState& state;
};

/** Verlauf der Limiter-Reduktion über die letzten 20 Sekunden. */
class ModulePanel::GrHistory : public juce::Component
{
public:
    explicit GrHistory (const UiState& s) : state (s) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (C::vizBg);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (C::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        const auto A = r.withTrimmedLeft (36).withTrimmedRight (10).withTrimmedTop (12).withTrimmedBottom (30);
        const float maxDb = 12.0f;
        g.setFont (mono (11.0f));

        for (int d = 0; d <= 12; d += 3)
        {
            const float y = A.getY() + d / maxDb * A.getHeight();
            g.setColour (C::grid);
            g.drawHorizontalLine (juce::roundToInt (y), A.getX(), A.getRight());
            g.setColour (C::faint);
            g.drawText (d ? de ("\xe2\x88\x92") + juce::String (d) : juce::String ("0"), juce::Rectangle<float> (r.getX(), y - 7, 30, 14), juce::Justification::centredRight);
        }

        const int n = (int) state.grHistory.size();
        juce::Path p;
        p.startNewSubPath (A.getX(), A.getY());
        for (int i = 0; i < n; ++i)
        {
            const float v = state.grHistory[(size_t) ((state.grHead + i) % n)];
            p.lineTo (A.getX() + (float) i / (n - 1) * A.getWidth(), A.getY() + juce::jlimit (0.0f, maxDb, v) / maxDb * A.getHeight());
        }
        p.lineTo (A.getRight(), A.getY());
        p.closeSubPath();
        g.setColour (C::ember.withAlpha (0.35f));
        g.fillPath (p);
        g.setColour (C::ember);
        g.strokePath (p, juce::PathStrokeType (1.0f));

        g.setColour (C::dim);
        g.setFont (caps (11.5f, 2));
        g.drawText (de ("GAIN REDUCTION \xc2\xb7 LETZTE 20 SEKUNDEN"), r.reduced (40.0f, 6.0f), juce::Justification::bottomLeft);
    }

private:
    const UiState& state;
};

//==============================================================================
ModulePanel::ModulePanel (KernfusionProcessor& p, const UiState& s, int m) : proc (p), state (s), module (m)
{
    auto& ap = proc.apvts;
    power.setButtonText (de (moduleNames[m]));
    power.setTitle (de (moduleNames[m]) + " an/aus");
    if (auto* prm = ap.getParameter (modulePowerIds[m]))
        powerAttachment = std::make_unique<juce::ButtonParameterAttachment> (*prm, power, nullptr);
    addAndMakeVisible (power);

    if (m == 0 || m == 4)
    {
        const char* prefix = m == 0 ? "pre" : "post";
        viz = std::make_unique<EqGraph> (proc, prefix);
        addAndMakeVisible (*viz);

        for (int b = 0; b < 5; ++b)
        {
            auto* grp = groups.add (new Group (de (bandNames[b])));
            if (b == 0)
            {
                auto* t = toggles.add (new juce::ToggleButton ("Aktiv"));
                if (auto* prm = ap.getParameter (params::eq (prefix, 0, "on")))
                    buttonAttachments.push_back (std::make_unique<juce::ButtonParameterAttachment> (*prm, *t, nullptr));
                grp->addExtra (*t, 26);
            }
            grp->add (ap, params::eq (prefix, b, "f"), "Frequenz");
            if (b > 0) grp->add (ap, params::eq (prefix, b, "g"), "Gain");
            grp->add (ap, params::eq (prefix, b, "q"), de ("G\xc3\xbcte (Q)"));
        }

        if (m == 0)
            groups.add (new Group ("Eingang"))->add (ap, params::inGain, "Pegel");

        hint = de ("Punkte ziehen \xc2\xb7 Mausrad \xc3\xbc" "ber einem Punkt = G\xc3\xbcte \xc2\xb7 Doppelklick = zur\xc3\xbc" "cksetzen \xc2\xb7 Hintergrund: ")
             + (m == 0 ? de ("Spektrum des Eingangs") : de ("Spektrum des Masters"));
    }
    else if (m == 1)
    {
        auto* set = groups.add (new Group ("Weichen & Zeiten"));
        set->add (ap, "dyn_x1", "Tief/Mitte");
        set->add (ap, "dyn_x2", "Mitte/Hoch");
        set->add (ap, "dyn_att", "Attack");
        set->add (ap, "dyn_rel", "Release");
        set->add (ap, "dyn_amt", de ("Intensit\xc3\xa4t"));

        for (int b = 0; b < 3; ++b)
        {
            auto* grp = groups.add (new Group (de (threeBands[b])));
            grp->meter = 0.0f;
            grp->headerValue = [this, b] { return de ("\xe2\x88\x92") + num (state.bandGr[(size_t) b]) + " dB"; };
            grp->add (ap, params::dynBand (b, "thr"), "Schwelle");
            grp->add (ap, params::dynBand (b, "ratio"), "Ratio");
            grp->add (ap, params::dynBand (b, "mk"), "Make-up");
        }

        hint = de ("Drei B\xc3\xa4nder mit phasenrichtiger Weiche (Linkwitz-Riley), RMS-Erkennung, Soft Knee. Intensit\xc3\xa4t skaliert alle Ratios (Punch-Regler).");
    }
    else if (m == 2)
    {
        const char* modes[] = { "Band", "R\xc3\xb6hre", "Warm" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = modeButtons[(size_t) i];
            b.setButtonText (de (modes[i]));
            b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
            b.onClick = [this, i] { params::setValue (proc.apvts, "exc_mode", (float) i); };
            addAndMakeVisible (b);
        }

        auto* x = groups.add (new Group ("Weichen"));
        x->add (ap, "exc_x1", "Tief/Mitte");
        x->add (ap, "exc_x2", "Mitte/Hoch");
        for (int b = 0; b < 3; ++b)
            groups.add (new Group (de (threeBands[b])))->add (ap, params::excBand (b), "Menge");

        hint = de ("Band: weiche ungerade Obert\xc3\xb6ne \xc2\xb7 R\xc3\xb6hre: gerade + ungerade, w\xc3\xa4rmer \xc2\xb7 Warm: vor allem 2. Oberton. L\xc3\xa4uft mit doppelter Abtastrate (weniger Aliasing).");
    }
    else if (m == 3)
    {
        auto* x = groups.add (new Group ("Weichen"));
        x->add (ap, "img_x1", "Tief/Mitte");
        x->add (ap, "img_x2", "Mitte/Hoch");

        for (int b = 0; b < 3; ++b)
        {
            auto* grp = groups.add (new Group (de (threeBands[b])));
            grp->add (ap, params::imgBand (b), "Breite");

            if (b == 0)
            {
                actionButton.setButtonText ("Bass mono");
                actionButton.setTooltip (de ("Alles unter der Weiche auf Mono \xe2\x80\x93 tr\xc3\xa4gt im Club und auf Vinyl"));
                actionButton.onClick = [this] { params::setValue (proc.apvts, params::imgBand (0), 0.0f); params::setValue (proc.apvts, "img_on", 1.0f); };
                grp->addExtra (actionButton, 30);
            }
        }

        viz = std::make_unique<Goniometer> (state);
        addAndMakeVisible (*viz);
        sideViz = viz.get();
        hint = de ("Breite 0 % = Mono, 100 % = unver\xc3\xa4ndert, 200 % = doppelt so breit. Korrelation unter 0 bedeutet Phasenprobleme in Mono.");
    }
    else
    {
        auto* grp = groups.add (new Group ("True-Peak-Limiter"));
        grp->add (ap, "max_gain", "Gain");
        grp->add (ap, "max_ceil", "Ceiling");
        grp->add (ap, "max_rel", "Release");
        actionButton.setButtonText ("Auf Ziel einmessen");
        actionButton.setTooltip (de ("Gain so einstellen, dass die Ziel-Lautheit getroffen wird (braucht mitgeh\xc3\xb6rtes Audio)"));
        actionButton.onClick = [this]
        {
            if (proc.hasCapture()) proc.requestLoudnessHold();
            else proc.setStatus (de ("Zum Einmessen erst einmal \xe2\x80\x9e") + "Ein Klick mastern" + de ("\xe2\x80\x9c ausf\xc3\xbchren, damit Kernfusion den Song geh\xc3\xb6rt hat."), 2);
        };
        grp->addExtra (actionButton, 30);

        viz = std::make_unique<GrHistory> (state);
        addAndMakeVisible (*viz);
        sideViz = viz.get();
        hint = de ("1,5 ms Lookahead, Spitzen werden 4-fach \xc3\xbc" "berabgetastet erkannt (True Peak). Kurzes Release = lauter, aber mehr Pumpen.");
    }

    for (auto* g : groups)
        addAndMakeVisible (g);
}

ModulePanel::~ModulePanel() = default;

void ModulePanel::tick()
{
    if (module == 1)
        for (int b = 0; b < 3; ++b)
        {
            groups[b + 1]->meter = juce::jlimit (0.0f, 1.0f, state.bandGr[(size_t) b] / 12.0f);
            groups[b + 1]->repaint();
        }

    if (module == 2)
    {
        const int mode = juce::roundToInt (params::getValue (proc.apvts, "exc_mode"));
        for (int i = 0; i < 3; ++i)
            modeButtons[(size_t) i].setToggleState (i == mode, juce::dontSendNotification);
    }

    if (viz != nullptr)
        viz->repaint();
}

void ModulePanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    g.setColour (C::faint);
    g.setFont (label (13.5f));
    g.drawFittedText (hint, hintArea, juce::Justification::centredLeft, 2);
}

void ModulePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 14);
    headArea = r.removeFromTop (34);
    auto head = headArea;
    power.setBounds (head.removeFromLeft (220));

    if (module == 2)
    {
        head.removeFromRight (0);
        auto seg = head.removeFromRight (240).withSizeKeepingCentre (240, 30);
        for (auto& b : modeButtons) b.setBounds (seg.removeFromLeft (80));
    }

    r.removeFromTop (12);
    hintArea = r.removeFromBottom (34);
    r.removeFromBottom (6);

    if (module == 0 || module == 4)
    {
        viz->setBounds (r.removeFromTop (juce::jmax (180, r.getHeight() - 250)));
        r.removeFromTop (12);
    }
    else if (sideViz != nullptr)
    {
        sideViz->setBounds (r.removeFromRight (juce::jmin (360, r.getWidth() / 3)));
        r.removeFromRight (12);
    }

    const int n = groups.size();
    const int gap = 12;
    const int w = (r.getWidth() - gap * (n - 1)) / juce::jmax (1, n);
    int maxH = 0;
    for (auto* g : groups) maxH = juce::jmax (maxH, g->idealHeight());

    for (auto* g : groups)
    {
        g->setBounds (r.removeFromLeft (w).withHeight (juce::jmin (r.getHeight(), juce::jmax (maxH, g->idealHeight()))));
        r.removeFromLeft (gap);
    }
}

//==============================================================================
ProView::Tile::Tile (int i, juce::String name) : juce::Button (name), index (i) {}

void ProView::Tile::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool sel = getToggleState();
    g.setColour (highlighted ? C::panel.brighter (0.04f) : C::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (sel ? C::plasma.withAlpha (0.8f) : C::line);
    g.drawRoundedRectangle (r, 10.0f, sel ? 1.5f : 1.0f);

    auto in = r.reduced (12.0f, 9.0f);
    g.setColour (C::faint);
    g.setFont (mono (11.0f, 1));
    g.drawText (juce::String (index + 1), in, juce::Justification::topRight);
    g.setColour (active ? C::fg : C::faint);
    g.setFont (caps (15.0f, 2, 0.1f));
    g.drawText (getName().toUpperCase(), in.removeFromTop (20.0f), juce::Justification::centredLeft);
    g.setColour (active ? C::dim : C::faint);
    g.setFont (mono (12.5f));
    g.drawText (active ? state : juce::String ("aus"), in.removeFromTop (20.0f), juce::Justification::centredLeft, true);
}

ProView::ProView (KernfusionProcessor& p, const UiState& s) : proc (p), uiState (s)
{
    for (int i = 0; i < 6; ++i)
    {
        auto* t = tiles.add (new Tile (i, de (moduleNames[i])));
        t->setClickingTogglesState (false);
        t->onClick = [this, i] { select (i); };
        addAndMakeVisible (t);
    }

    select (0);
}

void ProView::select (int m)
{
    selected = m;
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setToggleState (i == m, juce::dontSendNotification);

    panel = std::make_unique<ModulePanel> (proc, uiState, m);
    addAndMakeVisible (*panel);
    resized();
}

juce::String ProView::moduleState (int m) const
{
    auto& ap = proc.apvts;
    auto v = [&ap] (const juce::String& id) { return params::getValue (ap, id); };

    switch (m)
    {
        case 0: case 4:
        {
            const char* prefix = m == 0 ? "pre" : "post";
            int n = v (params::eq (prefix, 0, "on")) > 0.5f ? 1 : 0;
            for (int b = 1; b < 5; ++b) if (std::abs (v (params::eq (prefix, b, "g"))) >= 0.1f) ++n;
            return n == 0 ? juce::String ("neutral") : juce::String (n) + (n == 1 ? " Band aktiv" : de (" B\xc3\xa4nder aktiv"));
        }
        case 1: return "Reduktion " + num (juce::jmax (uiState.bandGr[0], juce::jmax (uiState.bandGr[1], uiState.bandGr[2]))) + " dB";
        case 2:
        {
            const char* modes[] = { "Band", "R\xc3\xb6hre", "Warm" };
            const float amt = juce::jmax (v (params::excBand (0)), juce::jmax (v (params::excBand (1)), v (params::excBand (2))));
            return de (modes[juce::jlimit (0, 2, juce::roundToInt (v ("exc_mode")))]) + de (" \xc2\xb7 ") + juce::String (juce::roundToInt (amt * 100)) + " %";
        }
        case 3: return juce::String (juce::roundToInt (v (params::imgBand (0)) * 100)) + " / " + juce::String (juce::roundToInt (v (params::imgBand (1)) * 100))
                     + " / " + juce::String (juce::roundToInt (v (params::imgBand (2)) * 100)) + " %";
        case 5: return "+" + num (v ("max_gain")) + de (" dB \xe2\x86\x92 ") + num (v ("max_ceil")) + " dBTP";
        default: return {};
    }
}

void ProView::tick()
{
    if (++frame % 6 == 0)
        for (int i = 0; i < tiles.size(); ++i)
        {
            tiles[i]->state = moduleState (i);
            tiles[i]->active = params::getValue (proc.apvts, modulePowerIds[i]) > 0.5f;
            tiles[i]->repaint();
        }

    if (panel != nullptr)
        panel->tick();
}

void ProView::resized()
{
    auto r = getLocalBounds();
    auto rack = r.removeFromTop (60);
    const int gap = 8, w = (rack.getWidth() - gap * 5) / 6;

    for (auto* t : tiles)
    {
        t->setBounds (rack.removeFromLeft (w));
        rack.removeFromLeft (gap);
    }

    r.removeFromTop (12);
    if (panel != nullptr)
        panel->setBounds (r);
}

} // namespace kf::ui
