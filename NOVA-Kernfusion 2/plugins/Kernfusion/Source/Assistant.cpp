#include "Assistant.h"

namespace kf
{

namespace
{
    juce::String de (const char* s) { return juce::String::fromUTF8 (s); }

    juce::String signedDb (double v)
    {
        if (std::abs (v) < 0.05) return "0,0 dB";
        return (v > 0 ? "+" : de ("\xe2\x88\x92")) + juce::String (std::abs (v), 1).replaceCharacter ('.', ',') + " dB";
    }

    juce::String num (double v)
    {
        return (v < 0 ? de ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (v), 1).replaceCharacter ('.', ',');
    }

    double round1 (double v) { return std::round (v * 10.0) / 10.0; }

    /** Durchschnittliche Bandleistung (ohne stille Frames). */
    struct BandAnalysis
    {
        ms::BandPowers powers {};
        ms::BandFlags valid {};
        ms::SpectrumSnapshot snapshot;
    };

    BandAnalysis analyseBands (const float* const* chans, int numChans, int n, double fs)
    {
        ms::SpectrumEngine engine;
        engine.prepare (fs);
        ms::SpectrumAccumulator acc;

        engine.process (chans, numChans, n, [&] (const ms::BandPowers& frame)
        {
            if (ms::SpectrumSnapshot::computeLoudness (frame, engine.getValidBands()) > ms::SpectrumSnapshot::silenceThresholdLufs)
                acc.push (frame);
        });

        BandAnalysis a;
        a.valid = engine.getValidBands();
        a.powers = acc.getAverage();
        a.snapshot = ms::SpectrumSnapshot::fromBandPowers (a.powers, a.valid);
        return a;
    }

    template <typename Get>
    double meanOver (double lo, double hi, Get&& get)
    {
        double s = 0;
        int n = 0;

        for (int b = 0; b < ms::BandLayout::numBands; ++b)
        {
            const double f = ms::BandLayout::getCentre (b), v = get (b);
            if (f >= lo && f < hi && std::isfinite (v)) { s += v; ++n; }
        }

        return n > 0 ? s / n : 0.0;
    }
}

//==============================================================================
void Capture::prepare (double fs)
{
    const auto n = (size_t) (fs * maxSeconds);

    // Aufnahme behalten, wenn sich nur die Blockgröße ändert (DAWs rufen prepareToPlay oft auf)
    if (bufL.size() == n && std::abs (fs - sampleRate) < 0.5)
        return;

    state = idle;
    sampleRate = fs;
    bufL.assign (n, 0.0f);
    bufR.assign (n, 0.0f);
    written = 0;
}

void Capture::clear() noexcept
{
    state = idle;
    written = 0;
}

void Capture::start() noexcept
{
    state = idle;
    written = 0;
    state = listening;
}

void Capture::finish() noexcept
{
    int expected = listening;
    state.compare_exchange_strong (expected, done);
}

void Capture::push (const float* L, const float* R, int n) noexcept
{
    if (state.load (std::memory_order_acquire) != listening || bufL.empty())
        return;

    // Stille überspringen (Transport steht, Pausen) – Spitzenwert unter −60 dBFS
    float peak = 0;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));

    if (peak < 0.001f)
        return;

    const int pos = written.load (std::memory_order_relaxed);
    const int cap = (int) bufL.size();
    const int todo = std::min (n, cap - pos);
    std::copy (L, L + todo, bufL.data() + pos);
    std::copy (R, R + todo, bufR.data() + pos);
    written.store (pos + todo, std::memory_order_release);

    if (pos + todo >= cap || (pos + todo) / sampleRate >= wantedSeconds)
        state.store (done, std::memory_order_release);
}

bool Capture::copyTo (std::vector<float>& L, std::vector<float>& R, double& fs) const
{
    if (state.load (std::memory_order_acquire) != done)
        return false;

    const int n = written.load (std::memory_order_acquire);
    L.assign (bufL.begin(), bufL.begin() + n);
    R.assign (bufR.begin(), bufR.begin() + n);
    fs = sampleRate;
    return n > 0;
}

//==============================================================================
namespace tone
{
    const std::array<Preset, 5>& presets()
    {
        static const std::array<Preset, 5> list {{
            { "balanced", "Ausgewogen (Pop)" },
            { "club", "Club / EDM" },
            { "hiphop", "Hip-Hop / Trap" },
            { "rock", "Rock / Indie" },
            { "acoustic", "Akustisch" },
        }};
        return list;
    }

    /*  Klangmodelle (Startwerte, keine echten Songs): Langzeitspektrum relativ zu rosa Rauschen.
        Grundneigung ≈ −1,5 dB/Okt, Bass-Anhebung, Sub-Abfall, Abfall der obersten Höhen.
        Identisch zum Web-Prototyp. */
    ms::TargetCurve makePreset (const juce::String& key)
    {
        struct Def { const char* key; const char* name; double slope, bass, bassF, sub, top, topK, spread, bumpF, bumpDb; };
        static const Def defs[] = {
            { "balanced", "Ausgewogen (Pop)", -1.5, 4.0, 120, 35, 12000, 6, 2.2, 0, 0 },
            { "club", "Club / EDM", -1.2, 7.0, 100, 28, 14000, 5, 2.4, 0, 0 },
            { "hiphop", "Hip-Hop / Trap", -1.8, 9.0, 90, 25, 12000, 6, 2.6, 0, 0 },
            { "rock", "Rock / Indie", -1.3, 2.5, 120, 45, 11000, 7, 2.2, 2500, 1.5 },
            { "acoustic", "Akustisch", -1.8, 1.0, 150, 50, 11000, 6, 2.4, 250, 1.0 },
        };

        const Def* d = &defs[0];
        for (const auto& x : defs)
            if (key == x.key)
                d = &x;

        constexpr int NB = ms::BandLayout::numBands;
        std::array<double, NB> shape {}, spread {};
        double sum = 0;

        for (int b = 0; b < NB; ++b)
        {
            const double f = ms::BandLayout::getCentre (b), o = std::log2 (f / 1000.0);
            double v = d->slope * o + d->bass / (1.0 + std::pow (f / d->bassF, 2.0));
            if (f < d->sub) v -= 12.0 * std::log2 (d->sub / f);
            if (f > d->top) v -= d->topK * std::log2 (f / d->top);
            if (d->bumpF > 0) v += d->bumpDb * std::exp (-std::pow (std::log2 (f / d->bumpF) / 0.8, 2.0));
            shape[(size_t) b] = v;
            spread[(size_t) b] = d->spread * (f < 80 ? 1.5 : f > 8000 ? 1.4 : 1.0);
            sum += ms::kWeightingPowerGain (f) * std::pow (10.0, v / 10.0);
        }

        // gleiche Normierung wie gemessene Spektren (Σ K·P = Anzahl Bänder)
        const double c = 10.0 * std::log10 (sum / NB);
        ms::TargetCurve t;
        t.name = de (d->name);
        t.sources.add (de ("Klangmodell"));

        const double ks[] = { -1.0, -0.5, 0.0, 0.5, 1.0 };
        float ms::Percentiles::* members[] = { &ms::Percentiles::p10, &ms::Percentiles::p25, &ms::Percentiles::p50, &ms::Percentiles::p75, &ms::Percentiles::p90 };

        for (int b = 0; b < NB; ++b)
            for (int k = 0; k < 5; ++k)
                t.bands[(size_t) b].*members[k] = (float) (shape[(size_t) b] - c + ks[k] * spread[(size_t) b]);

        const auto& regions = ms::getRegions();

        for (int r = 0; r < ms::numRegions; ++r)
        {
            double s = 0;
            int n = 0;

            for (int b = 0; b < NB; ++b)
            {
                const double f = ms::BandLayout::getCentre (b);
                if (f >= regions[(size_t) r].lowFrequency && f < regions[(size_t) r].highFrequency)
                {
                    s += std::pow (10.0, t.bands[(size_t) b].p50 / 10.0);
                    ++n;
                }
            }

            const double med = 10.0 * std::log10 (s / std::max (1, n));

            for (int k = 0; k < 5; ++k)
                t.regions[(size_t) r].*members[k] = (float) (med + ks[k] * d->spread * 0.6);
        }

        return t;
    }
}

//==============================================================================
AssistantJob::AssistantJob() : juce::Thread ("NOVA Kernfusion Assistent") {}

AssistantJob::~AssistantJob()
{
    stopThread (10000);
}

bool AssistantJob::start (Mode m, std::vector<float> L, std::vector<float> R, double fs, const ChainParams& current,
                          float targetLufs, std::shared_ptr<const ms::TargetCurve> toneTarget)
{
    if (busy.load() || L.empty())
        return false;

    stopThread (2000);
    mode = m;
    inL = std::move (L);
    inR = std::move (R);
    sampleRate = fs;
    currentParams = current;
    target = targetLufs;
    curve = std::move (toneTarget);
    progress = 0.0f;
    busy = true;

    if (! startThread (juce::Thread::Priority::low))
    {
        busy = false;
        return false;
    }

    return true;
}

std::optional<AssistantJob::Result> AssistantJob::takeResult()
{
    const std::scoped_lock lock (resultLock);
    auto r = std::move (result);
    result.reset();
    return r;
}

void AssistantJob::run()
{
    Result r;
    auto onProgress = [this] (float p) { progress = p; };

    try
    {
        if (mode == Mode::full)
        {
            r = runFull (inL, inR, sampleRate, target, curve != nullptr ? *curve : tone::makePreset ("balanced"), onProgress);
        }
        else
        {
            r.mode = Mode::calibrate;
            r.params = currentParams;
            calibrate (r, inL, inR, sampleRate, target, onProgress);
        }
    }
    catch (const std::exception& e)
    {
        r.error = e.what();
    }

    {
        const std::scoped_lock lock (resultLock);
        result = std::move (r);
    }

    progress = 1.0f;
    busy = false;
}

//==============================================================================
AssistantJob::Result AssistantJob::runFull (const std::vector<float>& L, const std::vector<float>& R, double fs,
                                            float targetLufs, const ms::TargetCurve& toneTarget, const std::function<void (float)>& onProgress)
{
    Result res;
    res.mode = Mode::full;
    const int n = (int) std::min (L.size(), R.size());
    auto report = [&] (float p) { if (onProgress) onProgress (p); };

    if (n < (int) (fs * 1.0))
    {
        res.error = de ("Zu wenig Audio aufgenommen – mindestens ein paar Sekunden Musik abspielen.");
        return res;
    }

    // 1) Eingangslautheit → Arbeitspegel −18 LUFS, damit die Dynamik-Schwellen passen
    const auto input = measure (L.data(), R.data(), n, fs);
    auto p = ChainParams::defaults();
    p.inputGain = std::isfinite (input.integrated) ? (float) juce::jlimit (-12.0, 12.0, round1 (-18.0 - input.integrated)) : 0.0f;
    report (0.1f);

    // 2) Spektren: Stereo, Mitte, Seite
    const float* st[2] = { L.data(), R.data() };
    const auto stereo = analyseBands (st, 2, n, fs);
    std::vector<float> mid ((size_t) n), side ((size_t) n);
    for (int i = 0; i < n; ++i) { mid[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]); side[(size_t) i] = 0.5f * (L[(size_t) i] - R[(size_t) i]); }
    const float* mc[1] = { mid.data() };
    const float* sc[1] = { side.data() };
    const auto midA = analyseBands (mc, 1, n, fs);
    const auto sideA = analyseBands (sc, 1, n, fs);
    report (0.35f);

    // 3) Tonale Korrektur Richtung Klangziel (Pre-EQ): 70 %, max. ±4 dB
    std::array<double, ms::BandLayout::numBands> diff {};
    for (size_t b = 0; b < diff.size(); ++b)
        diff[b] = (double) toneTarget.bands[b].p50 - (double) stereo.snapshot.bands[b];

    auto move = [&] (double lo, double hi) { return (float) round1 (juce::jlimit (-4.0, 4.0, meanOver (lo, hi, [&] (int b) { return diff[(size_t) b]; }) * 0.7)); };

    p.pre.bands = {{ { FilterType::highpass, true, 25, 0.71f, 0 },
                     { FilterType::lowshelf, true, 90, 0.71f, move (30, 120) },
                     { FilterType::peak, true, 320, 0.8f, move (180, 550) },
                     { FilterType::peak, true, 2500, 0.8f, move (1500, 4200) },
                     { FilterType::highshelf, true, 9000, 0.71f, move (7000, 16000) } }};

    juce::StringArray eqParts;
    const juce::String eqNamesDe[] = { "Bass", "Tiefmitten", de ("Pr\xc3\xa4senz"), de ("H\xc3\xb6hen") };
    for (int i = 0; i < 4; ++i)
        if (std::abs (p.pre.bands[(size_t) i + 1].gain) >= 0.3f)
            eqParts.add (eqNamesDe[i] + " " + signedDb (p.pre.bands[(size_t) i + 1].gain));

    res.notes.push_back ({ de ("EQ Richtung \xe2\x80\x9e") + toneTarget.name + de ("\xe2\x80\x9c:"),
                           eqParts.isEmpty() ? de ("passt schon, kaum Korrektur") : eqParts.joinIntoString (", ") });

    // 4) Multiband-Dynamik: Schwellen aus den Bandpegeln (Bandleistung enthält beide Kanäle)
    auto regionLevel = [&] (double lo, double hi)
    {
        double s = 0;
        for (int b = 0; b < ms::BandLayout::numBands; ++b)
        {
            const double f = ms::BandLayout::getCentre (b);
            if (f >= lo && f < hi && stereo.valid[(size_t) b]) s += stereo.powers[(size_t) b];
        }
        return 10.0 * std::log10 (s + 1e-20) + p.inputGain;
    };

    const double lv[3] = { regionLevel (20, 120), regionLevel (120, 2500), regionLevel (2500, 20000) };
    const float dynAmount = (float) (std::round (juce::jlimit (0.8, 1.7, 1.0 + (targetLufs + 11.0) / 5.0) * 100.0) / 100.0);
    p.dyn.on = true; p.dyn.x1 = 120; p.dyn.x2 = 2500; p.dyn.attack = 25; p.dyn.release = 160; p.dyn.amount = dynAmount;
    p.dyn.bands = {{ { (float) std::round (lv[0] - 5), 2.0f, 0 }, { (float) std::round (lv[1] - 5), 1.8f, 0 }, { (float) std::round (lv[2] - 5), 2.0f, 0 } }};
    res.notes.push_back ({ "Dynamik:", de ("sanfter Multiband-Kleber (Ratio 1,8\xe2\x80\x93") + "2), damit Bass, Mitten und " + de ("H\xc3\xb6hen zusammenhalten") });

    // 5) Stereobild: Bass mono, Höhen etwas breiter (wenn das Original nicht schon sehr breit ist)
    auto sideRatio = [&] (double lo, double hi)
    {
        const double m = meanOver (lo, hi, [&] (int b) { return midA.valid[(size_t) b] ? 10.0 * std::log10 (midA.powers[(size_t) b] + 1e-20) : NAN; });
        const double s = meanOver (lo, hi, [&] (int b) { return sideA.valid[(size_t) b] ? 10.0 * std::log10 (sideA.powers[(size_t) b] + 1e-20) : NAN; });
        return s - m;
    };

    const double sLow = sideRatio (30, 150), sHigh = sideRatio (5000, 16000);
    p.img.on = true; p.img.x1 = 150; p.img.x2 = 5000;
    p.img.width = { sLow > -18 ? 0.0f : 0.5f, 1.0f, sHigh < -6 ? 1.15f : 1.0f };
    res.notes.push_back ({ "Stereo:", juce::String ("Bass unter 150 Hz ") + (sLow > -18 ? "auf Mono" : de ("schmaler")) + (sHigh < -6 ? de (", H\xc3\xb6hen etwas breiter") : juce::String()) });

    // 6) Exciter: etwas Glanz, wenn die Höhen unter dem Ziel liegen
    const double dull = meanOver (6000, 16000, [&] (int b) { return diff[(size_t) b]; });
    p.exc.on = true; p.exc.mode = 0; p.exc.x1 = 250; p.exc.x2 = 4000;
    p.exc.amount = { 0.0f, dull > 1 ? 0.06f : 0.0f, dull > 1 ? 0.22f : 0.08f };
    res.notes.push_back ({ "Exciter:", dull > 1 ? de ("Glanz in den H\xc3\xb6hen, weil der Mix dumpfer ist als das Ziel")
                                                : de ("nur ein Hauch Band-S\xc3\xa4ttigung oben") });

    // 7) Maximizer: True Peak −1 dBTP, Gain einmessen
    const double roughGain = juce::jlimit (0.0, 20.0, targetLufs - (input.integrated + p.inputGain) + 1.5);
    p.max = { true, (float) round1 (std::isfinite (roughGain) ? roughGain : 6.0), -1.0f, 120.0f };
    res.params = p;
    report (0.45f);

    calibrate (res, L, R, fs, targetLufs, [&] (float x) { report (0.45f + 0.55f * x); });

    res.notes.push_back ({ "Maximizer:", res.reachedTarget
        ? "+" + num (res.params.max.gain) + de (" dB, Ausgang begrenzt auf \xe2\x88\x92") + "1,0 dBTP " + de ("\xe2\x86\x92") + " Ziel " + num (targetLufs) + " LUFS"
        : "+" + num (res.params.max.gain) + de (" dB, \xe2\x88\x92") + "1,0 dBTP. Ziel " + num (targetLufs) + " LUFS nicht ganz erreicht (" + num (res.achievedLufs)
              + " LUFS) " + de ("\xe2\x80\x93 der Mix ist sehr dynamisch, mehr ginge nur mit Verzerrung.") });
    return res;
}

void AssistantJob::calibrate (Result& r, const std::vector<float>& L, const std::vector<float>& R, double fs, float targetLufs,
                              const std::function<void (float)>& onProgress)
{
    const int n = (int) std::min (L.size(), R.size());
    std::vector<float> oL, oR;
    double gain = r.params.max.gain;
    std::vector<std::pair<double, double>> pts;
    constexpr int maxIterations = 6;

    // Sekantenverfahren: je stärker der Limiter arbeitet, desto weniger Lautheit bringt jedes dB Gain
    for (int it = 0; it < maxIterations; ++it)
    {
        auto p = r.params;
        p.max.gain = (float) gain;
        renderOffline (p, fs, L.data(), R.data(), n, oL, oR);
        const auto m = measure (oL.data(), oR.data(), n, fs);
        if (onProgress) onProgress ((float) (it + 1) / maxIterations);

        if (! std::isfinite (m.integrated))
            break;

        r.calGain = gain; r.calLufs = m.integrated; r.achievedLufs = m.integrated; r.truePeak = m.truePeakDb;
        pts.emplace_back (gain, m.integrated);
        const double d = targetLufs - m.integrated;

        if (std::abs (d) < 0.15)
            break;

        double slope = 0.85;
        if (pts.size() >= 2)
        {
            const auto [g1, l1] = pts[pts.size() - 2];
            const auto [g2, l2] = pts[pts.size() - 1];
            if (std::abs (g2 - g1) > 0.05) slope = juce::jlimit (0.12, 1.2, (l2 - l1) / (g2 - g1));
        }

        const double next = juce::jlimit (0.0, 30.0, gain + d / slope);
        if (std::abs (next - gain) < 0.05) break;   // am Anschlag
        gain = next;
    }

    r.params.max.gain = (float) round1 (gain);
    r.reachedTarget = ! (std::isfinite (r.achievedLufs) && targetLufs - r.achievedLufs > 0.5);
}

} // namespace kf
