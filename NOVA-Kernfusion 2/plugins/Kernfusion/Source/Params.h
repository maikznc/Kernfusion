#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Dsp.h"

/*  Parameter von Kernfusion. Alle Modul-Parameter sind automatisierbar und werden mit dem Projekt gespeichert.
    Die Makro-Regler der Ein-Klick-Ansicht schreiben in diese Parameter (siehe Macros).
*/
namespace kf::params
{

// IDs – bewusst kurz und stabil (Projekte speichern sie)
inline juce::String eq (const char* prefix, int band, const char* what) { return juce::String (prefix) + juce::String (band) + "_" + what; }
inline juce::String dynBand (int band, const char* what)                { return "dyn" + juce::String (band) + "_" + what; }
inline juce::String excBand (int band)                                  { return "exc" + juce::String (band) + "_amt"; }
inline juce::String imgBand (int band)                                  { return "img" + juce::String (band) + "_w"; }

inline constexpr const char* inGain = "in_gain";
inline constexpr const char* target = "target";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Liest alle Parameter (Audio-Thread-tauglich: nur atomare Werte, keine Allokation). */
class Reader
{
public:
    explicit Reader (juce::AudioProcessorValueTreeState&);
    ChainParams read() const noexcept;

private:
    struct EqRefs { std::atomic<float>* on; std::array<std::atomic<float>*, 5> f, q, g; std::atomic<float>* hpOn; };
    EqRefs pre, post;
    std::atomic<float>* in;
    std::atomic<float>* dynOn; std::atomic<float>* dynX1; std::atomic<float>* dynX2; std::atomic<float>* dynAtt; std::atomic<float>* dynRel; std::atomic<float>* dynAmt;
    std::array<std::atomic<float>*, 3> dynThr, dynRatio, dynMk;
    std::atomic<float>* excOn; std::atomic<float>* excMode; std::atomic<float>* excX1; std::atomic<float>* excX2;
    std::array<std::atomic<float>*, 3> excAmt;
    std::atomic<float>* imgOn; std::atomic<float>* imgX1; std::atomic<float>* imgX2;
    std::array<std::atomic<float>*, 3> imgW;
    std::atomic<float>* maxOn; std::atomic<float>* maxGain; std::atomic<float>* maxCeil; std::atomic<float>* maxRel;
};

/** Hält eine Änderungs-Geste über mehrere Parameter offen (z. B. während ein Makro-Regler gezogen wird). */
class GestureScope
{
public:
    GestureScope (juce::AudioProcessorValueTreeState&, const juce::StringArray& ids);
    ~GestureScope();
    static bool active() noexcept { return depth > 0; }

private:
    std::vector<juce::RangedAudioParameter*> params;
    static int depth;   // nur Message-Thread
    JUCE_DECLARE_NON_COPYABLE (GestureScope)
};

/** Setzt einen Parameter vom Message-Thread aus (mit Geste, damit die DAW Undo/Automation sauber aufzeichnet). */
void setValue (juce::AudioProcessorValueTreeState&, const juce::String& id, float plainValue);
float getValue (juce::AudioProcessorValueTreeState&, const juce::String& id);

/** Schreibt eine komplette Kette in die Parameter (z. B. nach dem Ein-Klick-Assistenten). */
void writeAll (juce::AudioProcessorValueTreeState&, const ChainParams&);

//==============================================================================
/** Makro-Regler der Ein-Klick-Ansicht: ein Wert → mehrere Modul-Parameter. */
struct Macro
{
    const char* id;
    const char* name;      // UTF-8
    float min, max, step, def;
    juce::String (*format) (float);
    float (*get) (juce::AudioProcessorValueTreeState&);
    void (*set) (juce::AudioProcessorValueTreeState&, float);
    juce::StringArray (*touched)();   // Parameter, die der Makro verändert (für Gesten)
};

const std::array<Macro, 7>& macros();

} // namespace kf::params
