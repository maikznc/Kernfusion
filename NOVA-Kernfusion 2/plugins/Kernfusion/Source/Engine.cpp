#include "Engine.h"

namespace kf
{

void Engine::prepare (double newFs, int maxBlockSize)
{
    fs = newFs;
    chain.prepare (fs, maxBlockSize);
    meterIn.prepare (fs);
    meterOut.prepare (fs);

    inL.assign ((size_t) maxBlockSize, 0.0f);
    inR.assign ((size_t) maxBlockSize, 0.0f);
    const int byLen = chain.latency() + 1;
    byL.assign ((size_t) byLen, 0.0f);
    byR.assign ((size_t) byLen, 0.0f);
    byPos = 0;

    abStep = 1.0 / (fs * 0.015);          // 15 ms Überblendung Vorher/Nachher
    monitorStep = 1.0 - std::exp (-1.0 / (fs * 0.05));

    colLen = std::max (32, (int) std::lround (fs * 0.005));
    aLo = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 200.0 / fs);
    aHi = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 3500.0 / fs);
    colBuf.assign ((size_t) colFifo.getTotalSize(), WaveColumn {});
    scopeBuf.assign ((size_t) scopeFifo.getTotalSize() * 2, 0.0f);
    colFifo.reset();
    scopeFifo.reset();
    seconds = 0;
    abMix = bypass.load() ? 0.0 : 1.0;
}

void Engine::process (float* L, float* R, int n) noexcept
{
    if (resetRequested.exchange (false))
    {
        meterIn.reset();
        meterOut.reset();
        seconds = 0;
    }

    std::copy (L, L + n, inL.data());
    std::copy (R, R + n, inR.data());

    chain.process (L, R, n);

    const int byLen = (int) byL.size();
    const double abTarget = bypass.load (std::memory_order_relaxed) ? 0.0 : 1.0;
    const double monTarget = dbToGain (monitorDb.load (std::memory_order_relaxed));

    // Lautheitsangleich für Vorher/Nachher: Original wird auf die Kurzzeit-Lautheit des Masters gebracht
    if (std::isfinite (meterOut.shortTerm) && std::isfinite (meterIn.shortTerm) && meterOut.shortTerm > -70 && meterIn.shortTerm > -70)
        matchDb += (meterOut.shortTerm - meterIn.shortTerm - matchDb) * 0.02;

    const double byTarget = dbToGain (matchDb);
    bool newBlock = false;

    for (int i = 0; i < n; ++i)
    {
        // Original um die Latenz der Kette verzögern
        byL[(size_t) byPos] = inL[(size_t) i];
        byR[(size_t) byPos] = inR[(size_t) i];
        byPos = byPos + 1 == byLen ? 0 : byPos + 1;
        const float dl = byL[(size_t) byPos], dr = byR[(size_t) byPos];
        const float ol = L[i], orr = R[i];

        newBlock |= meterIn.push (dl, dr);
        meterOut.push (ol, orr);

        // Waveform-Spalte
        const float m = 0.5f * (ol + orr), im = 0.5f * (dl + dr);
        cMin = std::min (cMin, m); cMax = std::max (cMax, m);
        iMin = std::min (iMin, im); iMax = std::max (iMax, im);
        lpLo += aLo * (m - lpLo); lpLo2 += aLo * (lpLo - lpLo2);
        lpHi += aHi * (m - lpHi); lpHi2 += aHi * (lpHi - lpHi2);
        const double lo = lpLo2, hi = m - lpHi2, mid = lpHi2 - lpLo2;
        eLo += lo * lo; eMid += mid * mid; eHi += hi * hi;
        corrNum += (double) ol * orr; corrL += (double) ol * ol; corrR += (double) orr * orr;

        if (++scopeDecim >= 8)
        {
            scopeDecim = 0;
            if (scopeFifo.getFreeSpace() > 0)
            {
                const auto w = scopeFifo.write (1);
                const int idx = w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2;
                scopeBuf[(size_t) idx * 2] = ol;
                scopeBuf[(size_t) idx * 2 + 1] = orr;
            }
        }

        if (++col >= colLen)
        {
            if (colFifo.getFreeSpace() > 0)
            {
                const auto w = colFifo.write (1);
                const int idx = w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2;
                colBuf[(size_t) idx] = { cMin, cMax, iMin, iMax,
                                         (float) std::sqrt (eLo / col), (float) std::sqrt (eMid / col), (float) std::sqrt (eHi / col) };
            }

            col = 0; cMin = cMax = iMin = iMax = 0; eLo = eMid = eHi = 0;
        }

        // Ausgang: Master oder Original (mit kurzer Überblendung), dann Vorschau-Pegel
        abMix += abMix < abTarget ? std::min (abStep, abTarget - abMix) : -std::min (abStep, abMix - abTarget);
        bypassGain += (byTarget - bypassGain) * 0.001;
        monitor += (monTarget - monitor) * monitorStep;
        const double yl = abMix * ol + (1.0 - abMix) * dl * bypassGain;
        const double yr = abMix * orr + (1.0 - abMix) * dr * bypassGain;
        L[i] = (float) (yl * monitor);
        R[i] = (float) (yr * monitor);
    }

    // Flush gegen Subnormale in den Detektoren
    if (std::abs (lpLo) < 1e-20) lpLo = 0;  if (std::abs (lpLo2) < 1e-20) lpLo2 = 0;
    if (std::abs (lpHi) < 1e-20) lpHi = 0;  if (std::abs (lpHi2) < 1e-20) lpHi2 = 0;

    seconds += n / fs;

    if (newBlock)
        publish();
}

void Engine::publish() noexcept
{
    auto& m = meters;
    auto safe = [] (double v) { return std::isfinite (v) ? (float) v : -100.0f; };

    m.inShort = safe (meterIn.shortTerm);
    m.outMomentary = safe (meterOut.momentary);
    m.outShort = safe (meterOut.shortTerm);
    m.outTruePeak = safe (gainToDb (meterOut.truePeak));
    m.outMaxShort = safe (meterOut.maxShortTerm());
    m.matchDb = (float) matchDb;
    m.measuredSeconds = seconds;

    // Integrated/LRA kosten je ~760 Additionen – 2,5-mal pro Sekunde reicht
    if (++blocksSincePublish >= 4)
    {
        blocksSincePublish = 0;
        m.outIntegrated = safe (meterOut.integrated());
        m.inIntegrated = safe (meterIn.integrated());
        m.outLra = (float) meterOut.loudnessRange();
    }

    auto raiseTo = [] (std::atomic<float>& a, float v) { float cur = a.load(); while (v > cur && ! a.compare_exchange_weak (cur, v)) {} };

    auto& mx = chain.maximizer();
    raiseTo (m.limiterGr, (float) mx.grMax);
    mx.grMax = 0;

    auto& dyn = chain.dynamics();
    for (size_t b = 0; b < 3; ++b)
    {
        raiseTo (m.bandGr[b], dyn.on ? (float) dyn.comps[b].grMax : 0.0f);
        dyn.comps[b].grMax = 0;
    }

    const double den = std::sqrt (corrL * corrR);
    m.correlation = den > 1e-12 ? (float) (corrNum / den) : 1.0f;
    corrNum *= 0.5; corrL *= 0.5; corrR *= 0.5;
}

int Engine::readColumns (WaveColumn* dest, int maxNum) noexcept
{
    const int num = std::min (maxNum, colFifo.getNumReady());
    const auto r = colFifo.read (num);

    for (int i = 0; i < r.blockSize1; ++i) dest[i] = colBuf[(size_t) (r.startIndex1 + i)];
    for (int i = 0; i < r.blockSize2; ++i) dest[r.blockSize1 + i] = colBuf[(size_t) (r.startIndex2 + i)];
    return num;
}

int Engine::readScope (float* dest, int maxPairs) noexcept
{
    const int num = std::min (maxPairs, scopeFifo.getNumReady());
    const auto r = scopeFifo.read (num);
    int k = 0;

    for (int i = 0; i < r.blockSize1; ++i, ++k) { dest[k * 2] = scopeBuf[(size_t) (r.startIndex1 + i) * 2]; dest[k * 2 + 1] = scopeBuf[(size_t) (r.startIndex1 + i) * 2 + 1]; }
    for (int i = 0; i < r.blockSize2; ++i, ++k) { dest[k * 2] = scopeBuf[(size_t) (r.startIndex2 + i) * 2]; dest[k * 2 + 1] = scopeBuf[(size_t) (r.startIndex2 + i) * 2 + 1]; }
    return num;
}

} // namespace kf
