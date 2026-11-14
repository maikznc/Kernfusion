#include "Params.h"

namespace kf::params
{

using APVTS = juce::AudioProcessorValueTreeState;
using Range = juce::NormalisableRange<float>;

namespace
{
    juce::String de (const char* s) { return juce::String::fromUTF8 (s); }

    juce::String fmtSigned (float v, int dec = 1)
    {
        if (std::abs (v) < 0.05f) return "0,0";
        return (v > 0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (v), dec).replaceCharacter ('.', ',');
    }

    juce::String fmtNum (float v, int dec = 1)
    {
        return (v < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (v), dec).replaceCharacter ('.', ',');
    }

    juce::String fmtHz (float f)
    {
        if (f < 1000.0f) return juce::String (juce::roundToInt (f)) + " Hz";
        return juce::String (f / 1000.0f, f < 10000.0f ? 2 : 1).replaceCharacter ('.', ',') + " kHz";
    }

    Range freqRange (float lo, float hi, float centre)
    {
        Range r (lo, hi, 0.0f);
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<juce::AudioParameterFloat> fp (const juce::String& id, const juce::String& name, Range range, float def,
                                                    std::function<juce::String (float, int)> toText, const juce::String& label = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                                                            juce::AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (std::move (toText)));
    }

    std::unique_ptr<juce::AudioParameterBool> bp (const juce::String& id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
    }

    auto dbText    = [] (float v, int) { return fmtSigned (v) + " dB"; };
    auto hzText    = [] (float v, int) { return fmtHz (v); };
    auto qText     = [] (float v, int) { return juce::String (v, 2).replaceCharacter ('.', ','); };
    auto msText    = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; };
    auto pctText   = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

}

APVTS::ParameterLayout createLayout()
{
    APVTS::ParameterLayout layout;
    const auto d = ChainParams::defaults();

    layout.add (fp (inGain, de ("Eingang"), Range (-12.0f, 12.0f, 0.1f), 0.0f, dbText, "dB"));
    layout.add (fp (target, de ("Ziel-Lautheit"), Range (-18.0f, -6.0f, 0.5f), -11.0f,
                    [] (float v, int) { return fmtNum (v) + " LUFS"; }, "LUFS"));

    // ---- EQs
    const char* bandNames[] = { "Low Cut", "Tiefen", "Bell 1", "Bell 2", "H\xc3\xb6hen" };

    auto addEq = [&] (const char* prefix, const char* title, const EqParams& def)
    {
        layout.add (bp (juce::String (prefix) + "_on", de (title) + " an", true));

        for (int b = 0; b < 5; ++b)
        {
            const auto& band = def.bands[(size_t) b];
            const auto name = de (title) + " " + de (bandNames[b]);

            if (b == 0)
                layout.add (bp (eq (prefix, b, "on"), name + " an", false));

            layout.add (fp (eq (prefix, b, "f"), name + " Frequenz", freqRange (20.0f, 20000.0f, 1000.0f), band.freq, hzText, "Hz"));
            layout.add (fp (eq (prefix, b, "q"), name + " Q", Range (0.3f, 4.0f, 0.01f, 0.5f), band.q, qText));

            if (b > 0)
                layout.add (fp (eq (prefix, b, "g"), name + " Gain", Range (-12.0f, 12.0f, 0.1f), band.gain, dbText, "dB"));
        }
    };

    addEq ("pre", "Pre-EQ", d.pre);
    addEq ("post", "Post-EQ", d.post);

    // ---- Dynamik
    layout.add (bp ("dyn_on", de ("Dynamik an"), true));
    layout.add (fp ("dyn_x1", de ("Dynamik Weiche Tief/Mitte"), freqRange (40.0f, 600.0f, 150.0f), d.dyn.x1, hzText, "Hz"));
    layout.add (fp ("dyn_x2", de ("Dynamik Weiche Mitte/Hoch"), freqRange (800.0f, 12000.0f, 3000.0f), d.dyn.x2, hzText, "Hz"));
    layout.add (fp ("dyn_att", de ("Dynamik Attack"), Range (1.0f, 100.0f, 1.0f, 0.6f), d.dyn.attack, msText, "ms"));
    layout.add (fp ("dyn_rel", de ("Dynamik Release"), Range (20.0f, 800.0f, 5.0f, 0.5f), d.dyn.release, msText, "ms"));
    layout.add (fp ("dyn_amt", de ("Dynamik Intensit\xc3\xa4t"), Range (0.0f, 2.0f, 0.01f), d.dyn.amount,
                    [] (float v, int) { return juce::String (juce::roundToInt (v * 50.0f)) + " %"; }));

    const char* dynNames[] = { "Bass", "Mitten", "H\xc3\xb6hen" };

    for (int b = 0; b < 3; ++b)
    {
        const auto n = de ("Dynamik ") + de (dynNames[b]);
        layout.add (fp (dynBand (b, "thr"), n + " Schwelle", Range (-60.0f, 0.0f, 0.5f), d.dyn.bands[(size_t) b].threshold,
                        [] (float v, int) { return fmtNum (v) + " dB"; }, "dB"));
        layout.add (fp (dynBand (b, "ratio"), n + " Ratio", Range (1.0f, 8.0f, 0.1f, 0.6f), d.dyn.bands[(size_t) b].ratio,
                        [] (float v, int) { return fmtNum (v) + ":1"; }));
        layout.add (fp (dynBand (b, "mk"), n + " Make-up", Range (-6.0f, 12.0f, 0.1f), d.dyn.bands[(size_t) b].makeup, dbText, "dB"));
    }

    // ---- Exciter
    layout.add (bp ("exc_on", de ("Exciter an"), true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "exc_mode", 1 }, de ("Exciter Charakter"),
                                                              juce::StringArray { "Band", de ("R\xc3\xb6hre"), "Warm" }, 0));
    layout.add (fp ("exc_x1", de ("Exciter Weiche Tief/Mitte"), freqRange (60.0f, 1000.0f, 250.0f), d.exc.x1, hzText, "Hz"));
    layout.add (fp ("exc_x2", de ("Exciter Weiche Mitte/Hoch"), freqRange (1500.0f, 12000.0f, 4000.0f), d.exc.x2, hzText, "Hz"));
    for (int b = 0; b < 3; ++b)
        layout.add (fp (excBand (b), de ("Exciter ") + de (dynNames[b]), Range (0.0f, 1.0f, 0.01f), 0.0f, pctText));

    // ---- Imager
    layout.add (bp ("img_on", de ("Imager an"), true));
    layout.add (fp ("img_x1", de ("Imager Weiche Tief/Mitte"), freqRange (40.0f, 600.0f, 150.0f), d.img.x1, hzText, "Hz"));
    layout.add (fp ("img_x2", de ("Imager Weiche Mitte/Hoch"), freqRange (1500.0f, 12000.0f, 5000.0f), d.img.x2, hzText, "Hz"));
    for (int b = 0; b < 3; ++b)
        layout.add (fp (imgBand (b), de ("Imager Breite ") + de (dynNames[b]), Range (0.0f, 2.0f, 0.01f), 1.0f, pctText));

    // ---- Maximizer
    layout.add (bp ("max_on", de ("Maximizer an"), true));
    layout.add (fp ("max_gain", de ("Maximizer Gain"), Range (0.0f, 30.0f, 0.1f), 0.0f,
                    [] (float v, int) { return "+" + fmtNum (v) + " dB"; }, "dB"));
    layout.add (fp ("max_ceil", de ("Maximizer Ceiling"), Range (-3.0f, -0.1f, 0.1f), -1.0f,
                    [] (float v, int) { return fmtNum (v) + " dBTP"; }, "dBTP"));
    layout.add (fp ("max_rel", de ("Maximizer Release"), Range (10.0f, 500.0f, 5.0f, 0.5f), 120.0f, msText, "ms"));

    return layout;
}

//==============================================================================
Reader::Reader (APVTS& s)
{
    auto raw = [&s] (const juce::String& id)
    {
        auto* p = s.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    auto eqRefs = [&] (const char* prefix)
    {
        EqRefs r {};
        r.on = raw (juce::String (prefix) + "_on");
        r.hpOn = raw (eq (prefix, 0, "on"));

        for (int b = 0; b < 5; ++b)
        {
            r.f[(size_t) b] = raw (eq (prefix, b, "f"));
            r.q[(size_t) b] = raw (eq (prefix, b, "q"));
            r.g[(size_t) b] = b > 0 ? raw (eq (prefix, b, "g")) : nullptr;
        }

        return r;
    };

    pre = eqRefs ("pre");
    post = eqRefs ("post");
    in = raw (inGain);
    dynOn = raw ("dyn_on"); dynX1 = raw ("dyn_x1"); dynX2 = raw ("dyn_x2"); dynAtt = raw ("dyn_att"); dynRel = raw ("dyn_rel"); dynAmt = raw ("dyn_amt");

    for (int b = 0; b < 3; ++b)
    {
        dynThr[(size_t) b] = raw (dynBand (b, "thr"));
        dynRatio[(size_t) b] = raw (dynBand (b, "ratio"));
        dynMk[(size_t) b] = raw (dynBand (b, "mk"));
        excAmt[(size_t) b] = raw (excBand (b));
        imgW[(size_t) b] = raw (imgBand (b));
    }

    excOn = raw ("exc_on"); excMode = raw ("exc_mode"); excX1 = raw ("exc_x1"); excX2 = raw ("exc_x2");
    imgOn = raw ("img_on"); imgX1 = raw ("img_x1"); imgX2 = raw ("img_x2");
    maxOn = raw ("max_on"); maxGain = raw ("max_gain"); maxCeil = raw ("max_ceil"); maxRel = raw ("max_rel");
}

ChainParams Reader::read() const noexcept
{
    auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto b = [&] (const std::atomic<float>* a) { return v (a) >= 0.5f; };

    ChainParams p = ChainParams::defaults();
    p.inputGain = v (in);

    auto readEq = [&] (const EqRefs& r, EqParams& e)
    {
        e.on = b (r.on);

        for (size_t i = 0; i < 5; ++i)
        {
            auto& band = e.bands[i];
            band.freq = v (r.f[i]);
            band.q = v (r.q[i]);
            band.gain = i > 0 ? v (r.g[i]) : 0.0f;
            band.on = i == 0 ? b (r.hpOn) : true;
        }
    };

    readEq (pre, p.pre);
    readEq (post, p.post);

    p.dyn.on = b (dynOn); p.dyn.x1 = v (dynX1); p.dyn.x2 = std::max (v (dynX2), p.dyn.x1 * 1.5f);
    p.dyn.attack = v (dynAtt); p.dyn.release = v (dynRel); p.dyn.amount = v (dynAmt);

    for (size_t i = 0; i < 3; ++i)
    {
        p.dyn.bands[i] = { v (dynThr[i]), v (dynRatio[i]), v (dynMk[i]) };
        p.exc.amount[i] = v (excAmt[i]);
        p.img.width[i] = v (imgW[i]);
    }

    p.exc.on = b (excOn); p.exc.mode = juce::roundToInt (v (excMode)); p.exc.x1 = v (excX1); p.exc.x2 = std::max (v (excX2), p.exc.x1 * 1.5f);
    p.img.on = b (imgOn); p.img.x1 = v (imgX1); p.img.x2 = std::max (v (imgX2), p.img.x1 * 1.5f);
    p.max.on = b (maxOn); p.max.gain = v (maxGain); p.max.ceiling = v (maxCeil); p.max.release = v (maxRel);
    return p;
}

//==============================================================================
int GestureScope::depth = 0;

GestureScope::GestureScope (APVTS& s, const juce::StringArray& ids)
{
    for (const auto& id : ids)
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            params.push_back (p);
        }

    ++depth;
}

GestureScope::~GestureScope()
{
    for (auto* p : params)
        p->endChangeGesture();

    --depth;
}

void setValue (APVTS& s, const juce::String& id, float plain)
{
    if (auto* p = s.getParameter (id))
    {
        const float norm = p->convertTo0to1 (plain);

        if (std::abs (norm - p->getValue()) < 1.0e-7f)
            return;

        if (GestureScope::active())   // ein Regler wird gerade gezogen: eine Geste für den ganzen Zug
        {
            p->setValueNotifyingHost (norm);
            return;
        }

        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

float getValue (APVTS& s, const juce::String& id)
{
    if (auto* p = s.getRawParameterValue (id))
        return p->load();

    return 0.0f;
}

void writeAll (APVTS& s, const ChainParams& p)
{
    setValue (s, inGain, p.inputGain);

    auto writeEq = [&] (const char* prefix, const EqParams& e)
    {
        setValue (s, juce::String (prefix) + "_on", e.on ? 1.0f : 0.0f);
        setValue (s, eq (prefix, 0, "on"), e.bands[0].on ? 1.0f : 0.0f);

        for (int b = 0; b < 5; ++b)
        {
            setValue (s, eq (prefix, b, "f"), e.bands[(size_t) b].freq);
            setValue (s, eq (prefix, b, "q"), e.bands[(size_t) b].q);
            if (b > 0) setValue (s, eq (prefix, b, "g"), e.bands[(size_t) b].gain);
        }
    };

    writeEq ("pre", p.pre);
    writeEq ("post", p.post);

    setValue (s, "dyn_on", p.dyn.on ? 1.0f : 0.0f);
    setValue (s, "dyn_x1", p.dyn.x1); setValue (s, "dyn_x2", p.dyn.x2);
    setValue (s, "dyn_att", p.dyn.attack); setValue (s, "dyn_rel", p.dyn.release); setValue (s, "dyn_amt", p.dyn.amount);

    for (int b = 0; b < 3; ++b)
    {
        setValue (s, dynBand (b, "thr"), p.dyn.bands[(size_t) b].threshold);
        setValue (s, dynBand (b, "ratio"), p.dyn.bands[(size_t) b].ratio);
        setValue (s, dynBand (b, "mk"), p.dyn.bands[(size_t) b].makeup);
        setValue (s, excBand (b), p.exc.amount[(size_t) b]);
        setValue (s, imgBand (b), p.img.width[(size_t) b]);
    }

    setValue (s, "exc_on", p.exc.on ? 1.0f : 0.0f); setValue (s, "exc_mode", (float) p.exc.mode);
    setValue (s, "exc_x1", p.exc.x1); setValue (s, "exc_x2", p.exc.x2);
    setValue (s, "img_on", p.img.on ? 1.0f : 0.0f); setValue (s, "img_x1", p.img.x1); setValue (s, "img_x2", p.img.x2);
    setValue (s, "max_on", p.max.on ? 1.0f : 0.0f); setValue (s, "max_gain", p.max.gain);
    setValue (s, "max_ceil", p.max.ceiling); setValue (s, "max_rel", p.max.release);
}

//==============================================================================
const std::array<Macro, 7>& macros()
{
    static const std::array<Macro, 7> list {{
        { "loud", "Lautheit", -18.0f, -6.0f, 0.5f, -11.0f,
          [] (float v) { return fmtNum (v) + " LUFS"; },
          [] (APVTS& s) { return getValue (s, target); },
          [] (APVTS& s, float v) { setValue (s, target, v); },
          [] { return juce::StringArray { target }; } },

        { "bass", "Bass", -6.0f, 6.0f, 0.1f, 0.0f,
          [] (float v) { return fmtSigned (v) + " dB"; },
          [] (APVTS& s) { return getValue (s, eq ("post", 1, "g")); },
          [] (APVTS& s, float v) { setValue (s, eq ("post", 1, "g"), v); },
          [] { return juce::StringArray { eq ("post", 1, "g") }; } },

        { "warm", "W\xc3\xa4rme", -4.0f, 4.0f, 0.1f, 0.0f,
          [] (float v) { return fmtSigned (v) + " dB"; },
          [] (APVTS& s) { return getValue (s, eq ("post", 2, "g")); },
          [] (APVTS& s, float v) { setValue (s, eq ("post", 2, "g"), v); setValue (s, excBand (0), v > 0 ? v / 4.0f * 0.3f : 0.0f); },
          [] { return juce::StringArray { eq ("post", 2, "g"), excBand (0) }; } },

        { "punch", "Punch", 0.0f, 100.0f, 1.0f, 50.0f,
          [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
          [] (APVTS& s) { return getValue (s, "dyn_amt") * 50.0f; },
          [] (APVTS& s, float v) { setValue (s, "dyn_amt", v / 50.0f); setValue (s, "dyn_att", 10.0f + v * 0.3f); },
          [] { return juce::StringArray { "dyn_amt", "dyn_att" }; } },

        { "width", "Breite", 50.0f, 160.0f, 1.0f, 100.0f,
          [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
          [] (APVTS& s) { return getValue (s, imgBand (1)) * 100.0f; },
          [] (APVTS& s, float v) { setValue (s, imgBand (1), v / 100.0f); setValue (s, imgBand (2), std::min (2.0f, v / 100.0f * 1.05f)); setValue (s, "img_on", 1.0f); },
          [] { return juce::StringArray { imgBand (1), imgBand (2), "img_on" }; } },

        { "air", "Luft", -6.0f, 6.0f, 0.1f, 0.0f,
          [] (float v) { return fmtSigned (v) + " dB"; },
          [] (APVTS& s) { return getValue (s, eq ("post", 4, "g")); },
          [] (APVTS& s, float v) { setValue (s, eq ("post", 4, "g"), v); },
          [] { return juce::StringArray { eq ("post", 4, "g") }; } },

        { "shine", "Glanz", 0.0f, 100.0f, 1.0f, 0.0f,
          [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; },
          [] (APVTS& s) { return getValue (s, excBand (2)) / 0.6f * 100.0f; },
          [] (APVTS& s, float v) { setValue (s, excBand (2), v / 100.0f * 0.6f); setValue (s, excBand (1), v / 100.0f * 0.12f); setValue (s, "exc_on", 1.0f); },
          [] { return juce::StringArray { excBand (2), excBand (1), "exc_on" }; } },
    }};

    return list;
}

} // namespace kf::params
