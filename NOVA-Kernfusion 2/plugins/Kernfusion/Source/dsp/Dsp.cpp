#include "Dsp.h"

namespace kf
{

static constexpr double twoPi = 6.283185307179586476925286766559;

template <typename T> static T clampv (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

//==============================================================================
Coeffs makeCoeffs (FilterType type, double fs, double f, double q, double gainDb) noexcept
{
    f = clampv (f, 10.0, fs * 0.49);
    q = q > 0 ? q : 0.7071;
    const double w0 = twoPi * f / fs, cw = std::cos (w0), sw = std::sin (w0);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double al = sw / (2.0 * q);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::lowpass:   b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::highpass:  b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::allpass:   b0 = 1 - al; b1 = -2 * cw; b2 = 1 + al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FilterType::peak:      b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case FilterType::lowshelf:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
            a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case FilterType::highshelf:
        {
            const double s = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
            a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
    }

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

double magnitudeAt (const Coeffs& c, double fs, double f) noexcept
{
    const double w = twoPi * f / fs;
    const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = -(c.b1 * s1 + c.b2 * s2);
    const double dr = 1 + c.a1 * c1 + c.a2 * c2, di = -(c.a1 * s1 + c.a2 * s2);
    return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
}

//==============================================================================
ChainParams ChainParams::defaults()
{
    ChainParams p;
    p.pre.bands = {{ { FilterType::highpass, false, 25, 0.71f, 0 },
                     { FilterType::lowshelf, true, 100, 0.71f, 0 },
                     { FilterType::peak, true, 350, 0.9f, 0 },
                     { FilterType::peak, true, 2500, 0.9f, 0 },
                     { FilterType::highshelf, true, 10000, 0.71f, 0 } }};
    p.post.bands = {{ { FilterType::highpass, false, 25, 0.71f, 0 },
                      { FilterType::lowshelf, true, 90, 0.71f, 0 },
                      { FilterType::peak, true, 250, 0.8f, 0 },
                      { FilterType::peak, true, 3000, 0.9f, 0 },
                      { FilterType::highshelf, true, 12000, 0.71f, 0 } }};
    return p;
}

static bool eqBandActive (const EqBand& b) noexcept
{
    return b.on && (b.type == FilterType::highpass || b.type == FilterType::lowpass || std::abs (b.gain) > 0.01f);
}

double eqResponseDb (const EqParams& p, double fs, double f) noexcept
{
    double m = 1;
    for (const auto& b : p.bands)
        if (eqBandActive (b))
            m *= magnitudeAt (makeCoeffs (b.type, fs, b.freq, b.q, b.gain), fs, f);
    return gainToDb (m);
}

//==============================================================================
void EQ::set (const EqParams& p, double fs) noexcept
{
    on = p.on;
    numActive = 0;

    for (size_t i = 0; i < p.bands.size(); ++i)
    {
        const auto& b = p.bands[i];
        filters[i].c = makeCoeffs (b.type, fs, b.freq, b.q, b.gain);

        if (eqBandActive (b))
            active[(size_t) numActive++] = (int) i;
    }
}

//==============================================================================
void Crossover3::set (double fs, double f1, double f2) noexcept
{
    const double q = 0.70710678118654752;
    const auto lp1 = makeCoeffs (FilterType::lowpass, fs, f1, q), hp1 = makeCoeffs (FilterType::highpass, fs, f1, q);
    const auto lp2 = makeCoeffs (FilterType::lowpass, fs, f2, q), hp2 = makeCoeffs (FilterType::highpass, fs, f2, q);
    lp1a.c = lp1b.c = lp1; hp1a.c = hp1b.c = hp1;
    lp2a.c = lp2b.c = lp2; hp2a.c = hp2b.c = hp2;
    ap2.c = makeCoeffs (FilterType::allpass, fs, f2, q);   // Phasenausgleich des Bass-Bands an der oberen Trennfrequenz
}

void Crossover3::reset() noexcept
{
    for (auto* b : { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b, &ap2 })
        b->reset();
}

void Crossover3::flush() noexcept
{
    for (auto* b : { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b, &ap2 })
        b->flush();
}

//==============================================================================
void BandCompressor::set (double fs, const DynBand& b, double attackMs, double releaseMs, double amt) noexcept
{
    thr = b.threshold;
    ratio = std::max (1.0, (double) b.ratio);
    makeup = b.makeup;
    amount = clampv (amt, 0.0, 2.0);
    aRms = 1.0 - std::exp (-1.0 / (fs * 0.010));
    aAtt = std::exp (-1.0 / (fs * std::max (0.1, attackMs) / 1000.0));
    aRel = std::exp (-1.0 / (fs * std::max (1.0, releaseMs) / 1000.0));
}

double BandCompressor::gain (double l, double r) noexcept
{
    const double p = std::max (l * l, r * r);
    ms += aRms * (p - ms);
    const double lvl = 10.0 * std::log10 (ms + 1e-20);
    const double over = lvl - thr, slope = (1.0 / ratio - 1.0) * amount;
    double target = 0;

    if (over > knee / 2)       target = slope * over;
    else if (over > -knee / 2) target = slope * (over + knee / 2) * (over + knee / 2) / (2 * knee);

    gr = target < gr ? aAtt * gr + (1 - aAtt) * target
                     : aRel * gr + (1 - aRel) * target;

    if (-gr > grMax)
        grMax = -gr;

    return dbToGain (gr + makeup);
}

void Dynamics::set (const DynParams& p, double fs) noexcept
{
    on = p.on;
    xo.set (fs, p.x1, p.x2);

    for (size_t i = 0; i < 3; ++i)
        comps[i].set (fs, p.bands[i], p.attack, p.release, p.amount);
}

void Dynamics::reset() noexcept
{
    xo.reset();
    for (auto& c : comps) c.reset();
}

void Dynamics::process (double& l, double& r) noexcept
{
    if (! on)
        return;

    xo.L (l, bl);
    xo.R (r, br);
    const double g0 = comps[0].gain (bl[0], br[0]), g1 = comps[1].gain (bl[1], br[1]), g2 = comps[2].gain (bl[2], br[2]);
    l = bl[0] * g0 + bl[1] * g1 + bl[2] * g2;
    r = br[0] * g0 + br[1] * g1 + br[2] * g2;
}

//==============================================================================
void Exciter::set (const ExcParams& p, double fsInternal) noexcept
{
    on = p.on && (p.amount[0] > 0.001f || p.amount[1] > 0.001f || p.amount[2] > 0.001f);
    mode = p.mode;
    xo.set (fsInternal, p.x1, p.x2);

    for (size_t i = 0; i < 3; ++i)
    {
        const double a = clampv ((double) p.amount[i], 0.0, 1.0);
        mix[i] = a;
        drive[i] = 1.5 + 10.0 * a;
    }

    dcCoef = std::exp (-twoPi * 12.0 / fsInternal);
}

void Exciter::reset() noexcept
{
    xo.reset();
    dcL = dcR = dcXL = dcXR = 0;
}

void Exciter::flush() noexcept
{
    xo.flush();
    if (std::abs (dcL) < 1e-25) dcL = 0;
    if (std::abs (dcR) < 1e-25) dcR = 0;
}

double Exciter::shape (double x, int i) const noexcept
{
    const double m = mix[i];

    if (m <= 0.001)
        return x;

    const double d = drive[i], u = x * d;
    double s;

    switch (mode)
    {
        case 1:  s = std::tanh (u + 0.25 * u * u); break;                    // Röhre: gerade + ungerade
        case 2:  s = u / (1.0 + std::abs (u)) + 0.12 * u * u; break;         // Warm: betont 2. Oberton
        default: s = std::tanh (u); break;                                   // Band: weich, ungerade
    }

    return x + m * (s / d - x);
}

void Exciter::process (double& l, double& r) noexcept
{
    if (! on)
        return;

    xo.L (l, bl);
    xo.R (r, br);
    const double yl = shape (bl[0], 0) + shape (bl[1], 1) + shape (bl[2], 2);
    const double yr = shape (br[0], 0) + shape (br[1], 1) + shape (br[2], 2);

    // Gleichanteil aus geraden Obertönen entfernen (Hochpass 12 Hz)
    const double ol = yl - dcXL + dcCoef * dcL;  dcXL = yl;  dcL = ol;
    const double orr = yr - dcXR + dcCoef * dcR; dcXR = yr;  dcR = orr;
    l = ol;
    r = orr;
}

//==============================================================================
void Imager::set (const ImgParams& p, double fs) noexcept
{
    on = p.on && (std::abs (p.width[0] - 1) > 0.001f || std::abs (p.width[1] - 1) > 0.001f || std::abs (p.width[2] - 1) > 0.001f);
    xo.set (fs, p.x1, p.x2);

    for (size_t i = 0; i < 3; ++i)
        w[i] = clampv ((double) p.width[i], 0.0, 2.0);
}

void Imager::process (double& l, double& r) noexcept
{
    if (! on)
        return;

    xo.L (l, bl);
    xo.R (r, br);
    double yl = 0, yr = 0;

    for (int i = 0; i < 3; ++i)
    {
        const double m = 0.5 * (bl[i] + br[i]), s = 0.5 * (bl[i] - br[i]) * w[i];
        yl += m + s;
        yr += m - s;
    }

    l = yl;
    r = yr;
}

//==============================================================================
namespace
{
    struct TruePeakTable
    {
        std::array<std::array<double, TruePeakDetector::taps>, TruePeakDetector::phases> poly {};

        TruePeakTable()
        {
            constexpr int P = TruePeakDetector::phases, T = TruePeakDetector::taps, N = P * T;

            for (int n = 0; n < N; ++n)
            {
                const double t = (n - (N - 1) / 2.0) / P;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double w = 0.42 - 0.5 * std::cos (twoPi * n / (N - 1)) + 0.08 * std::cos (2 * twoPi * n / (N - 1));
                poly[(size_t) (n % P)][(size_t) (n / P)] = sinc * w;
            }
        }
    };

    const TruePeakTable& tpTable()
    {
        static const TruePeakTable t;
        return t;
    }
}

double TruePeakDetector::push (double x) noexcept
{
    const auto& poly = tpTable().poly;
    buf[(size_t) pos] = x;
    double peak = 0;

    for (int p = 0; p < phases; ++p)
    {
        const auto& c = poly[(size_t) p];
        double s = 0;
        int j = pos;

        for (int k = 0; k < taps; ++k)
        {
            s += c[(size_t) k] * buf[(size_t) j];
            j = j == 0 ? taps - 1 : j - 1;
        }

        const double a = std::abs (s);
        if (a > peak) peak = a;
    }

    pos = pos + 1 == taps ? 0 : pos + 1;
    return peak;
}

//==============================================================================
void Maximizer::prepare (double newFs)
{
    fs = newFs;
    look = std::max (8, (int) std::lround (fs * 0.0015));            // 1,5 ms Lookahead
    delaySamples = look - 1 + TruePeakDetector::delay();
    dl.assign ((size_t) delaySamples + 1, 0.0);
    dr.assign ((size_t) delaySamples + 1, 0.0);
    req.assign ((size_t) look, 1.0);
    mins.assign ((size_t) look, 1.0);
    reset();
}

void Maximizer::reset() noexcept
{
    std::fill (dl.begin(), dl.end(), 0.0);
    std::fill (dr.begin(), dr.end(), 0.0);
    std::fill (req.begin(), req.end(), 1.0);
    std::fill (mins.begin(), mins.end(), 1.0);
    msum = look;
    dpos = rpos = mpos = 0;
    g = 1;
    gainSm = targetGain;
    tpl.reset(); tpr.reset();
}

void Maximizer::set (const MaxParams& p) noexcept
{
    on = p.on;
    targetGain = dbToGain (p.gain);
    ceil = dbToGain (clampv ((double) p.ceiling, -12.0, 0.0));
    rel = 1.0 - std::exp (-1.0 / (fs * std::max (5.0, (double) p.release) / 1000.0));
    smooth = 1.0 - std::exp (-1.0 / (fs * 0.03));
}

void Maximizer::process (double& l, double& r) noexcept
{
    const int n = (int) dl.size();

    if (! on)
    {
        // gleiche Latenz wie eingeschaltet, damit die Laufzeitkorrektur der DAW stimmt
        dl[(size_t) dpos] = l; dr[(size_t) dpos] = r;
        dpos = dpos + 1 == n ? 0 : dpos + 1;
        l = dl[(size_t) dpos]; r = dr[(size_t) dpos];
        g = 1;
        return;
    }

    gainSm += (targetGain - gainSm) * smooth;
    l *= gainSm;
    r *= gainSm;

    const double pk = std::max (tpl.push (l), tpr.push (r));
    const double need = pk > ceil ? ceil / pk : 1.0;

    // Minimum der benötigten Verstärkung über das Lookahead-Fenster …
    req[(size_t) rpos] = need;
    rpos = rpos + 1 == look ? 0 : rpos + 1;
    double m = 1.0;
    for (int i = 0; i < look; ++i)
        if (req[(size_t) i] < m) m = req[(size_t) i];

    // … als Rechteck-Mittel geglättet: erreicht das Ziel genau rechtzeitig
    msum += m - mins[(size_t) mpos];
    mins[(size_t) mpos] = m;
    mpos = mpos + 1 == look ? 0 : mpos + 1;

    if (mpos == 0)   // Rundungsfehler der laufenden Summe gelegentlich bereinigen
    {
        msum = 0;
        for (auto v : mins) msum += v;
    }

    const double box = msum / look;
    g = box < g ? box : g + (box - g) * rel;
    if (g > box) g = box;

    const double gr = -gainToDb (g);
    if (gr > grMax) grMax = gr;

    dl[(size_t) dpos] = l; dr[(size_t) dpos] = r;
    dpos = dpos + 1 == n ? 0 : dpos + 1;
    double ol = dl[(size_t) dpos] * g, orr = dr[(size_t) dpos] * g;

    // Sicherheits-Clipper auf Sample-Ebene (greift praktisch nie)
    ol = clampv (ol, -ceil, ceil);
    orr = clampv (orr, -ceil, ceil);
    l = ol;
    r = orr;
}

//==============================================================================
void LoudnessMeter::prepare (double fs)
{
    // K-Gewichtung für beliebige Abtastraten (Herleitung wie libebur128)
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
    const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
    double a0 = 1 + K / Q + K * K;
    pre.c = { (Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 };

    f0 = 38.13547087602444; Q = 0.5003270373238773;
    K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
    a0 = 1 + K / Q + K * K;
    rlb.c = { 1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 };

    step = std::max (1, (int) std::lround (fs * 0.1));   // 100-ms-Teilblöcke
    reset();
}

void LoudnessMeter::reset() noexcept
{
    pre.reset(); rlb.reset(); tpl.reset(); tpr.reset();
    acc = 0; n = 0; rpos = 0; filled = 0;
    ring.fill (0);
    blockCount.fill (0); stCount.fill (0); blockEnergy.fill (0); stEnergy.fill (0);
    momentary = shortTerm = maxST = -HUGE_VAL;
    truePeak = samplePeak = 0;
}

int LoudnessMeter::binFor (double lufs) noexcept
{
    return clampv ((int) std::floor ((lufs + 70.0) * 10.0), 0, numBins - 1);
}

bool LoudnessMeter::push (double l, double r) noexcept
{
    const double kl = rlb.L (pre.L (l)), kr = rlb.R (pre.R (r));
    acc += kl * kl + kr * kr;

    const double pk = std::max (tpl.push (l), tpr.push (r));
    if (pk > truePeak) truePeak = pk;
    const double sp = std::max (std::abs (l), std::abs (r));
    if (sp > samplePeak) samplePeak = sp;

    if (++n < step)
        return false;

    ring[(size_t) rpos] = acc / n;
    rpos = (rpos + 1) % 30;
    filled = std::min (30, filled + 1);
    acc = 0; n = 0;
    pre.flush(); rlb.flush();

    auto meanOf = [this] (int k) { double s = 0; for (int i = 1; i <= k; ++i) s += ring[(size_t) ((rpos - i + 30) % 30)]; return s / k; };

    if (filled >= 4)
    {
        const double m = meanOf (4);
        momentary = -0.691 + 10.0 * std::log10 (m + 1e-20);

        if (momentary > -70.0)
        {
            const int b = binFor (momentary);
            ++blockCount[(size_t) b];
            blockEnergy[(size_t) b] += m;
        }
    }

    if (filled >= 30)
    {
        const double s = meanOf (30);
        shortTerm = -0.691 + 10.0 * std::log10 (s + 1e-20);
        if (shortTerm > maxST) maxST = shortTerm;

        if (shortTerm > -70.0)
        {
            const int b = binFor (shortTerm);
            ++stCount[(size_t) b];
            stEnergy[(size_t) b] += s;
        }
    }
    else if (filled >= 4)
    {
        shortTerm = -0.691 + 10.0 * std::log10 (meanOf (filled) + 1e-20);
    }

    return true;
}

double LoudnessMeter::integrated() const noexcept
{
    double e = 0;
    uint64_t c = 0;

    for (int i = 0; i < numBins; ++i) { e += blockEnergy[(size_t) i]; c += blockCount[(size_t) i]; }

    if (c == 0)
        return -HUGE_VAL;

    const double relGate = -0.691 + 10.0 * std::log10 (e / (double) c) - 10.0;
    double e2 = 0;
    uint64_t c2 = 0;

    for (int i = 0; i < numBins; ++i)
    {
        const double centre = -70.0 + (i + 0.5) * 0.1;

        if (centre > relGate)
        {
            e2 += blockEnergy[(size_t) i];
            c2 += blockCount[(size_t) i];
        }
    }

    return c2 > 0 ? -0.691 + 10.0 * std::log10 (e2 / (double) c2) : -HUGE_VAL;
}

double LoudnessMeter::loudnessRange() const noexcept
{
    double e = 0;
    uint64_t c = 0;

    for (int i = 0; i < numBins; ++i) { e += stEnergy[(size_t) i]; c += stCount[(size_t) i]; }

    if (c < 2)
        return 0;

    const double gate = -0.691 + 10.0 * std::log10 (e / (double) c) - 20.0;
    const int start = binFor (gate) + 1;
    uint64_t total = 0;

    for (int i = start; i < numBins; ++i)
        total += stCount[(size_t) i];

    if (total < 2)
        return 0;

    auto percentile = [&] (double q)
    {
        const auto want = (uint64_t) std::llround (q * (double) (total - 1));
        uint64_t cum = 0;

        for (int i = start; i < numBins; ++i)
        {
            cum += stCount[(size_t) i];
            if (cum > want)
                return -70.0 + (i + 0.5) * 0.1;
        }

        return -70.0 + (numBins - 0.5) * 0.1;
    };

    return percentile (0.95) - percentile (0.10);
}

//==============================================================================
void Chain::prepare (double newFs, int maxBlockSize)
{
    fs = newFs;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) std::max (1, maxBlockSize));
    osLatency = (int) std::lround (os->getLatencyInSamples());
    max.prepare (fs);
    smooth = 1.0 - std::exp (-1.0 / (fs * 0.03));
    set (ChainParams::defaults());
    reset();
}

void Chain::reset()
{
    preEq.reset(); postEq.reset(); dyn.reset(); exc.reset(); img.reset(); max.reset();
    if (os) os->reset();
    inGain = inGainTarget;
}

void Chain::set (const ChainParams& p) noexcept
{
    inGainTarget = dbToGain (p.inputGain);
    preEq.set (p.pre, fs);
    dyn.set (p.dyn, fs);
    exc.set (p.exc, fs * 2.0);
    img.set (p.img, fs);
    postEq.set (p.post, fs);
    max.set (p.max);
}

int Chain::latency() const noexcept
{
    return osLatency + max.latency();
}

void Chain::process (float* L, float* R, int n) noexcept
{
    // 1) Eingang, Pre-EQ, Dynamik
    for (int i = 0; i < n; ++i)
    {
        inGain += (inGainTarget - inGain) * smooth;
        double l = preEq.L (L[i] * inGain), r = preEq.R (R[i] * inGain);
        dyn.process (l, r);
        L[i] = (float) l;
        R[i] = (float) r;
    }

    // 2) Exciter mit doppelter Abtastrate (weniger Aliasing). Läuft immer, damit die Latenz konstant bleibt.
    if (os != nullptr)
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = os->processSamplesUp (block);

        if (exc.isActive())
        {
            auto* ul = up.getChannelPointer (0);
            auto* ur = up.getChannelPointer (1);

            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                double l = ul[i], r = ur[i];
                exc.process (l, r);
                ul[i] = (float) l;
                ur[i] = (float) r;
            }
        }

        os->processSamplesDown (block);
    }

    // 3) Imager, Post-EQ, Maximizer
    for (int i = 0; i < n; ++i)
    {
        double l = L[i], r = R[i];
        img.process (l, r);
        l = postEq.L (l);
        r = postEq.R (r);
        max.process (l, r);
        L[i] = (float) l;
        R[i] = (float) r;
    }

    preEq.flush(); dyn.flush(); exc.flush(); img.flush(); postEq.flush();
}

//==============================================================================
void renderOffline (const ChainParams& p, double fs, const float* inL, const float* inR, int numSamples,
                    std::vector<float>& outL, std::vector<float>& outR)
{
    constexpr int block = 1024;
    Chain chain;
    chain.prepare (fs, block);
    chain.set (p);
    chain.reset();

    const int lat = chain.latency(), total = numSamples + lat;
    outL.assign ((size_t) numSamples, 0.0f);
    outR.assign ((size_t) numSamples, 0.0f);
    std::array<float, block> bl {}, br {};

    for (int pos = 0; pos < total; pos += block)
    {
        const int len = std::min (block, total - pos);

        for (int i = 0; i < len; ++i)
        {
            const int k = pos + i;
            bl[(size_t) i] = k < numSamples ? inL[k] : 0.0f;
            br[(size_t) i] = k < numSamples ? inR[k] : 0.0f;
        }

        chain.process (bl.data(), br.data(), len);

        for (int i = 0; i < len; ++i)
        {
            const int j = pos + i - lat;
            if (j >= 0 && j < numSamples) { outL[(size_t) j] = bl[(size_t) i]; outR[(size_t) j] = br[(size_t) i]; }
        }
    }
}

Measurement measure (const float* L, const float* R, int numSamples, double fs)
{
    LoudnessMeter m;
    m.prepare (fs);

    for (int i = 0; i < numSamples; ++i)
        m.push (L[i], R[i]);

    Measurement r;
    r.integrated = m.integrated();
    r.truePeakDb = gainToDb (m.truePeak);
    r.samplePeakDb = gainToDb (m.samplePeak);
    r.lra = m.loudnessRange();
    r.maxShortTerm = m.maxShortTerm();
    return r;
}

} // namespace kf
