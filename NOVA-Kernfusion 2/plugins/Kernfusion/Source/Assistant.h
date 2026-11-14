#pragma once

#include <ms_core/ms_core.h>
#include "dsp/Dsp.h"

namespace kf
{

//==============================================================================
/** Hört beim Abspielen mit: speichert bis zu 30 s Eingangssignal (Stille wird übersprungen).
    push() läuft im Audio-Thread und allokiert nicht. */
class Capture
{
public:
    enum State { idle = 0, listening = 1, done = 2 };

    static constexpr double maxSeconds = 30.0;
    static constexpr double wantedSeconds = 20.0;   // so viel Musik genügt dem Assistenten
    static constexpr double minSeconds = 5.0;

    void prepare (double fs);
    void start() noexcept;                 // Message-Thread
    void finish() noexcept;                // Message-Thread: vorzeitig beenden
    void clear() noexcept;                 // Message-Thread: Aufnahme verwerfen
    void push (const float* L, const float* R, int n) noexcept;

    State getState() const noexcept       { return (State) state.load(); }
    double getSeconds() const noexcept    { return written.load() / sampleRate; }
    double getProgress() const noexcept   { return std::min (1.0, getSeconds() / wantedSeconds); }
    bool hasEnough() const noexcept       { return getSeconds() >= minSeconds; }

    /** Kopiert das Aufgenommene (nur im Zustand done gültig). */
    bool copyTo (std::vector<float>& L, std::vector<float>& R, double& fs) const;

private:
    std::vector<float> bufL, bufR;
    std::atomic<int> state { idle }, written { 0 };
    double sampleRate = 48000;
};

//==============================================================================
/** Klangziele: Genre-Modelle (Startwerte), Leitstern-Ziele (JSON) und Referenztracks. */
namespace tone
{
    struct Preset { const char* key; const char* name; };
    const std::array<Preset, 5>& presets();
    ms::TargetCurve makePreset (const juce::String& key);
}

//==============================================================================
/** Der Ein-Klick-Assistent. Läuft im Hintergrund.
    full: analysiert die Aufnahme und baut eine komplette Einstellung.
    calibrate: misst nur die Lautheit neu und stellt den Maximizer auf das Ziel. */
class AssistantJob : private juce::Thread
{
public:
    enum class Mode { full, calibrate };

    struct Note { juce::String label, text; };

    struct Result
    {
        Mode mode = Mode::full;
        ChainParams params;
        double achievedLufs = -HUGE_VAL, truePeak = -HUGE_VAL;
        double calGain = 0, calLufs = -HUGE_VAL;
        bool reachedTarget = true;
        std::vector<Note> notes;
        juce::String error;
    };

    AssistantJob();
    ~AssistantJob() override;

    /** false, wenn gerade schon ein Auftrag läuft. */
    bool start (Mode, std::vector<float> L, std::vector<float> R, double fs, const ChainParams& current,
                float targetLufs, std::shared_ptr<const ms::TargetCurve> toneTarget);

    bool isBusy() const noexcept { return busy.load(); }
    float getProgress() const noexcept { return progress.load(); }

    /** Holt ein fertiges Ergebnis ab (Message-Thread). */
    std::optional<Result> takeResult();

    /** Kern des Assistenten, ohne Thread – auch für Tests. */
    static Result runFull (const std::vector<float>& L, const std::vector<float>& R, double fs,
                           float targetLufs, const ms::TargetCurve& toneTarget, const std::function<void (float)>& onProgress = {});
    static void calibrate (Result&, const std::vector<float>& L, const std::vector<float>& R, double fs, float targetLufs,
                           const std::function<void (float)>& onProgress = {});

private:
    void run() override;

    Mode mode = Mode::full;
    std::vector<float> inL, inR;
    double sampleRate = 48000;
    ChainParams currentParams;
    float target = -11.0f;
    std::shared_ptr<const ms::TargetCurve> curve;

    std::atomic<bool> busy { false };
    std::atomic<float> progress { 0.0f };
    std::mutex resultLock;
    std::optional<Result> result;
};

} // namespace kf
