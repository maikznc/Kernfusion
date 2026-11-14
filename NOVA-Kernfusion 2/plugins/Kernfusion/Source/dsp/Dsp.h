#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

/*  NOVA Kernfusion – DSP-Kette
    Gleiche Mathematik wie der Web-Prototyp (web/kernfusion-dsp.js), dazu im Plugin:
    Exciter mit 2-facher Überabtastung und feste Latenz (für die Laufzeitkorrektur der DAW).

    Kette: Eingang → Pre-EQ → Multiband-Dynamik → Exciter → Imager → Post-EQ → Maximizer
*/
namespace kf
{

inline double dbToGain (double db) noexcept   { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept    { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

//==============================================================================
enum class FilterType { highpass, lowpass, allpass, peak, lowshelf, highshelf };

struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

Coeffs makeCoeffs (FilterType, double fs, double freq, double q, double gainDb = 0.0) noexcept;
double magnitudeAt (const Coeffs&, double fs, double freq) noexcept;

/** Stereo-Biquad (Transposed Direct Form II). */
struct Biquad
{
    Coeffs c;
    double l1 = 0, l2 = 0, r1 = 0, r2 = 0;

    void reset() noexcept { l1 = l2 = r1 = r2 = 0; }

    inline double L (double x) noexcept { const double y = c.b0 * x + l1; l1 = c.b1 * x - c.a1 * y + l2; l2 = c.b2 * x - c.a2 * y; return y; }
    inline double R (double x) noexcept { const double y = c.b0 * x + r1; r1 = c.b1 * x - c.a1 * y + r2; r2 = c.b2 * x - c.a2 * y; return y; }

    void flush() noexcept
    {
        if (std::abs (l1) < 1e-25) l1 = 0;  if (std::abs (l2) < 1e-25) l2 = 0;
        if (std::abs (r1) < 1e-25) r1 = 0;  if (std::abs (r2) < 1e-25) r2 = 0;
    }
};

//==============================================================================
// Parameter
struct EqBand    { FilterType type = FilterType::peak; bool on = true; float freq = 1000, q = 0.71f, gain = 0; };
struct EqParams  { bool on = true; std::array<EqBand, 5> bands; };
struct DynBand   { float threshold = 0, ratio = 1.5f, makeup = 0; };
struct DynParams { bool on = true; float x1 = 120, x2 = 2500, attack = 25, release = 160, amount = 1; std::array<DynBand, 3> bands; };
struct ExcParams { bool on = true; int mode = 0; float x1 = 250, x2 = 4000; std::array<float, 3> amount { 0, 0, 0 }; };
struct ImgParams { bool on = true; float x1 = 150, x2 = 5000; std::array<float, 3> width { 1, 1, 1 }; };
struct MaxParams { bool on = true; float gain = 0, ceiling = -1, release = 120; };

struct ChainParams
{
    float inputGain = 0;
    EqParams pre, post;
    DynParams dyn;
    ExcParams exc;
    ImgParams img;
    MaxParams max;

    static ChainParams defaults();
};

/** Gesamtkurve eines EQs in dB bei Frequenz f (für die Anzeige). */
double eqResponseDb (const EqParams&, double fs, double freq) noexcept;

//==============================================================================
class EQ
{
public:
    void set (const EqParams&, double fs) noexcept;
    inline double L (double x) noexcept { if (! on) return x; for (int i = 0; i < numActive; ++i) x = filters[(size_t) active[(size_t) i]].L (x); return x; }
    inline double R (double x) noexcept { if (! on) return x; for (int i = 0; i < numActive; ++i) x = filters[(size_t) active[(size_t) i]].R (x); return x; }
    void reset() noexcept { for (auto& f : filters) f.reset(); }
    void flush() noexcept { for (auto& f : filters) f.flush(); }

private:
    bool on = false;
    std::array<Biquad, 5> filters;
    std::array<int, 5> active {};
    int numActive = 0;
};

/** 3-Band-Weiche, Linkwitz-Riley 4. Ordnung, summiert phasenrichtig zu einem flachen Allpass. */
class Crossover3
{
public:
    void set (double fs, double f1, double f2) noexcept;
    void reset() noexcept;
    void flush() noexcept;
    inline void L (double x, double* out) noexcept
    {
        out[0] = ap2.L (lp1b.L (lp1a.L (x)));
        const double rest = hp1b.L (hp1a.L (x));
        out[1] = lp2b.L (lp2a.L (rest));
        out[2] = hp2b.L (hp2a.L (rest));
    }
    inline void R (double x, double* out) noexcept
    {
        out[0] = ap2.R (lp1b.R (lp1a.R (x)));
        const double rest = hp1b.R (hp1a.R (x));
        out[1] = lp2b.R (lp2a.R (rest));
        out[2] = hp2b.R (hp2a.R (rest));
    }

private:
    Biquad lp1a, lp1b, hp1a, hp1b, lp2a, lp2b, hp2a, hp2b, ap2;
};

class BandCompressor
{
public:
    void set (double fs, const DynBand&, double attackMs, double releaseMs, double amount) noexcept;
    void reset() noexcept { ms = 0; gr = 0; }
    /** Stereo-gekoppelt: RMS-Pegel → Kennlinie (Soft Knee) → Attack/Release auf die Reduktion. */
    double gain (double l, double r) noexcept;

    double gr = 0;      // aktuelle Reduktion in dB (≤ 0)
    double grMax = 0;   // größte Reduktion seit dem letzten Abholen (≥ 0)

private:
    double ms = 0, thr = 0, ratio = 1, makeup = 0, knee = 6, amount = 1, aRms = 0, aAtt = 0, aRel = 0;
};

class Dynamics
{
public:
    void set (const DynParams&, double fs) noexcept;
    void reset() noexcept;
    void process (double& l, double& r) noexcept;
    void flush() noexcept { xo.flush(); }

    bool on = false;
    std::array<BandCompressor, 3> comps;

private:
    Crossover3 xo;
    double bl[3] {}, br[3] {};
};

/** Exciter: Oberton-Erzeugung je Band. Läuft im Plugin mit doppelter Abtastrate. */
class Exciter
{
public:
    void set (const ExcParams&, double fsInternal) noexcept;
    void reset() noexcept;
    void process (double& l, double& r) noexcept;
    void flush() noexcept;
    bool isActive() const noexcept { return on; }

private:
    double shape (double x, int band) const noexcept;

    bool on = false;
    int mode = 0;
    Crossover3 xo;
    double mix[3] {}, drive[3] {};
    double dcCoef = 0, dcL = 0, dcR = 0, dcXL = 0, dcXR = 0;
    double bl[3] {}, br[3] {};
};

class Imager
{
public:
    void set (const ImgParams&, double fs) noexcept;
    void reset() noexcept { xo.reset(); }
    void process (double& l, double& r) noexcept;
    void flush() noexcept { xo.flush(); }

private:
    bool on = false;
    Crossover3 xo;
    double w[3] { 1, 1, 1 };
    double bl[3] {}, br[3] {};
};

//==============================================================================
/** True-Peak-Erkennung: 4-fache Überabtastung mit 48-Tap-Polyphasenfilter (wie ITU-R BS.1770 Anhang 2). */
class TruePeakDetector
{
public:
    static constexpr int phases = 4, taps = 12;
    static int delay() noexcept { return (phases * taps - 1) / 2 / phases + 1; }   // ≈ 6 Samples
    double push (double x) noexcept;
    void reset() noexcept { buf.fill (0); pos = 0; }

private:
    std::array<double, taps> buf {};
    int pos = 0;
};

/** Lookahead-Limiter auf True Peak. Latenz ist konstant (auch wenn ausgeschaltet). */
class Maximizer
{
public:
    void prepare (double fs);
    void set (const MaxParams&) noexcept;
    void reset() noexcept;
    void process (double& l, double& r) noexcept;
    int latency() const noexcept { return delaySamples; }

    double g = 1;          // aktuelle Verstärkung des Limiters
    double grMax = 0;      // größte Reduktion seit dem letzten Abholen (dB)

private:
    double fs = 48000;
    bool on = true;
    int look = 72, delaySamples = 0, dpos = 0, rpos = 0, mpos = 0;
    std::vector<double> dl, dr, req, mins;
    double msum = 0, targetGain = 1, gainSm = 1, ceil = 0.89, rel = 0.001, smooth = 0.001;
    TruePeakDetector tpl, tpr;
};

//==============================================================================
/** Lautheitsmessung nach ITU-R BS.1770 / EBU R128 ohne Speicherwachstum
    (Integrated und LRA über Histogramme mit 0,1-dB-Klassen, wie libebur128). */
class LoudnessMeter
{
public:
    void prepare (double fs);
    void reset() noexcept;
    /** Gibt true zurück, wenn ein neuer 100-ms-Block fertig ist. */
    bool push (double l, double r) noexcept;

    double momentary = -HUGE_VAL, shortTerm = -HUGE_VAL;
    double truePeak = 0, samplePeak = 0;   // linear

    double integrated() const noexcept;
    double loudnessRange() const noexcept;
    double maxShortTerm() const noexcept { return maxST; }

private:
    static constexpr int numBins = 760;          // −70 … +6 LUFS
    static int binFor (double lufs) noexcept;

    Biquad pre, rlb;
    int step = 4800, n = 0, rpos = 0, filled = 0;
    double acc = 0, maxST = -HUGE_VAL;
    std::array<double, 30> ring {};
    std::array<uint32_t, numBins> blockCount {}, stCount {};
    std::array<double, numBins> blockEnergy {}, stEnergy {};
    TruePeakDetector tpl, tpr;
};

//==============================================================================
/** Die komplette Kette. Block-basiert, Exciter überabgetastet. */
class Chain
{
public:
    void prepare (double fs, int maxBlockSize);
    void reset();
    void set (const ChainParams&) noexcept;
    /** Verarbeitet in place. n ≤ maxBlockSize. */
    void process (float* L, float* R, int n) noexcept;
    int latency() const noexcept;

    const Dynamics& dynamics() const noexcept { return dyn; }
    Dynamics& dynamics() noexcept { return dyn; }
    Maximizer& maximizer() noexcept { return max; }

private:
    double fs = 48000;
    double inGain = 1, inGainTarget = 1, smooth = 0.001;
    EQ preEq, postEq;
    Dynamics dyn;
    Exciter exc;
    Imager img;
    Maximizer max;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int osLatency = 0;
};

/** Rendert ein Stereo-Signal komplett durch eine frische Kette (für Assistent und Tests).
    Die Latenz wird herausgerechnet: Ausgang ist zeitgleich zum Eingang. */
void renderOffline (const ChainParams&, double fs, const float* inL, const float* inR, int numSamples,
                    std::vector<float>& outL, std::vector<float>& outR);

/** Misst Integrated, True Peak usw. eines Stereo-Signals. */
struct Measurement { double integrated = -HUGE_VAL, truePeakDb = -HUGE_VAL, samplePeakDb = -HUGE_VAL, lra = 0, maxShortTerm = -HUGE_VAL; };
Measurement measure (const float* L, const float* R, int numSamples, double fs);

} // namespace kf
