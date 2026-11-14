#pragma once

#include "Theme.h"
#include "../Engine.h"

class KernfusionProcessor;

namespace kf::ui
{

/** Alles, was die Anzeigen pro Frame brauchen. Wird vom Editor-Timer (30 Hz) aus dem Prozessor gefüllt,
    damit nicht jede Komponente selbst auf die Engine zugreift. */
struct UiState
{
    static constexpr int numCols = 4096;

    std::vector<WaveColumn> cols = std::vector<WaveColumn> ((size_t) numCols);
    int head = 0, count = 0;

    std::vector<float> scope = std::vector<float> (1024, 0.0f);   // L,R-Paare (512)
    int scopeCount = 0;

    float outShort = -100, outMomentary = -100, outIntegrated = -100, outTruePeak = -100, outLra = 0, outMaxShort = -100;
    float inIntegrated = -100;
    double measuredSeconds = 0;
    float limiterGr = 0;                  // größte Reduktion im letzten Frame (dB)
    std::array<float, 3> bandGr {};       // geglättet
    std::array<float, 600> grHistory {};  // 20 s bei 30 Hz
    int grHead = 0;
    float correlation = 1;
    bool signal = false;                  // in den letzten ~0,5 s kam Audio an
    int framesSinceAudio = 1000;

    /** Spalte i Schritte vor der neuesten (0 = neueste). */
    const WaveColumn& column (int back) const noexcept
    {
        return cols[(size_t) ((head - 1 - back + numCols * 2) % numCols)];
    }
};

} // namespace kf::ui
