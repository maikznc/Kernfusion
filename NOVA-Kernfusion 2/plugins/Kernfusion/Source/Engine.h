#pragma once

#include "dsp/Dsp.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace kf
{

/** Eine Spalte der Mini-Meter-Waveform (5 ms): Ausgang min/max, Eingang min/max, Energie tief/mitte/hoch. */
struct WaveColumn { float outMin, outMax, inMin, inMax, lo, mid, hi; };

/** Echtzeit-Teil von Kernfusion: Kette, Meter, Vorher/Nachher, Plattform-Vorschau, Daten für die Oberfläche.
    process() läuft im Audio-Thread – keine Allokation, keine Locks. Die Oberfläche liest nur atomare Werte
    und lock-freie FIFOs. */
class Engine
{
public:
    void prepare (double fs, int maxBlockSize);
    void setParams (const ChainParams& p) noexcept { chain.set (p); }
    void process (float* L, float* R, int n) noexcept;
    int latency() const noexcept { return chain.latency(); }

    // --- Steuerung (beliebiger Thread)
    std::atomic<bool>  bypass { false };          // Vorher (Original, lautheitsangeglichen)
    std::atomic<float> monitorDb { 0.0f };        // Plattform-Vorschau: Pegel wie nach der Normalisierung
    std::atomic<bool>  resetRequested { false };

    // --- Messwerte für die Oberfläche
    struct Meters
    {
        std::atomic<float> inShort { -100.0f }, inIntegrated { -100.0f };
        std::atomic<float> outMomentary { -100.0f }, outShort { -100.0f }, outIntegrated { -100.0f };
        std::atomic<float> outLra { 0.0f }, outTruePeak { -100.0f }, outMaxShort { -100.0f };
        std::atomic<float> limiterGr { 0.0f };                 // größte Reduktion seit dem letzten Abholen
        std::array<std::atomic<float>, 3> bandGr {};           // dito, je Dynamik-Band
        std::atomic<float> correlation { 1.0f };
        std::atomic<float> matchDb { 0.0f };
        std::atomic<double> measuredSeconds { 0.0 };
    };

    Meters meters;

    /** Holt neue Waveform-Spalten (Message-Thread). */
    int readColumns (WaveColumn* dest, int maxNum) noexcept;
    /** Holt neue Stereo-Punkte für das Goniometer (L, R abwechselnd). */
    int readScope (float* dest, int maxPairs) noexcept;

    float takeLimiterGr() noexcept   { return meters.limiterGr.exchange (0.0f); }
    float takeBandGr (int b) noexcept { return meters.bandGr[(size_t) b].exchange (0.0f); }

private:
    void publish() noexcept;

    double fs = 48000;
    Chain chain;
    LoudnessMeter meterIn, meterOut;

    std::vector<float> inL, inR, byL, byR;
    int byPos = 0;
    double matchDb = 0, bypassGain = 1, abMix = 1, abStep = 0.002, monitor = 1, monitorStep = 0.001;

    // Waveform
    int colLen = 240, col = 0;
    float cMin = 0, cMax = 0, iMin = 0, iMax = 0;
    double eLo = 0, eMid = 0, eHi = 0, lpLo = 0, lpLo2 = 0, lpHi = 0, lpHi2 = 0, aLo = 0, aHi = 0;
    juce::AbstractFifo colFifo { 4096 };
    std::vector<WaveColumn> colBuf;

    // Goniometer + Korrelation
    juce::AbstractFifo scopeFifo { 2048 };
    std::vector<float> scopeBuf;
    int scopeDecim = 0;
    double corrNum = 0, corrL = 0, corrR = 0;
    int blocksSincePublish = 0;
    double seconds = 0;
};

} // namespace kf
