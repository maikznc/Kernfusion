#pragma once

#include <ms_core/ms_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Engine.h"
#include "Assistant.h"

//==============================================================================
class KernfusionProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::Timer
{
public:
    KernfusionProcessor();
    ~KernfusionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.2; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    kf::Engine engine;
    ms::LiveAnalyser inputAnalyser, outputAnalyser;

    // --- Ein-Klick-Assistent
    enum class Phase { idle, listening, working };
    Phase getPhase() const noexcept;
    float getPhaseProgress() const noexcept;
    bool hasCapture() const noexcept { return capture.getState() == kf::Capture::done; }
    bool canFinishListening() const noexcept { return capture.getState() == kf::Capture::listening && capture.hasEnough(); }

    /** „Ein Klick mastern“: hört mit (falls noch nichts aufgenommen) und mastert dann. */
    void oneClick (bool listenAgain = false);
    void finishListening()                  { capture.finish(); }
    void cancelListening();
    /** Nach dem Drehen an einem Makro: Lautheit wieder auf das Ziel bringen (wenn Audio aufgenommen ist). */
    void requestLoudnessHold();

    std::vector<kf::AssistantJob::Note> getNotes() const;

    // --- Klangziel
    std::shared_ptr<const ms::TargetCurve> getToneTarget() const;
    juce::String getToneKey() const;
    void setToneTarget (std::shared_ptr<const ms::TargetCurve>, const juce::String& key);
    bool analyseReferences (const juce::Array<juce::File>&);
    bool isAnalysingReferences() const noexcept { return refJob.isBusy(); }
    float getReferenceProgress() const noexcept { return refJob.getProgress(); }

    // --- Vorher/Nachher und Plattform-Vorschau (werden nicht gespeichert)
    void setBypass (bool b)                 { engine.bypass = b; }
    bool isBypassed() const noexcept        { return engine.bypass.load(); }
    void setPreview (int platform, float gainDb);
    int getPreview() const noexcept         { return previewPlatform.load(); }

    // --- Statuszeile
    struct Status { juce::String text; int kind = 0; uint32_t serial = 0; };   // kind: 0 neutral, 1 gut, 2 Fehler
    Status getStatus() const;
    void setStatus (const juce::String&, int kind = 0);

    // --- Oberflächen-Einstellungen, die mit dem Projekt gespeichert werden
    std::atomic<int> waveColour { 2 };   // 0 rot, 1 blau, 2 spektral
    std::atomic<int> lastTab { 0 };

    double getSampleRateSafe() const noexcept { const double fs = getSampleRate(); return fs > 0 ? fs : 48000.0; }

    /** Führt die Hintergrund-Abfrage sofort aus (sonst 10-mal pro Sekunde per Timer) – für Tests. */
    void poll() { timerCallback(); }
    bool isAssistantBusy() const noexcept { return job.isBusy(); }

private:
    void parameterChanged (const juce::String&, float) override { paramsDirty = true; }
    void timerCallback() override;
    void startJob (kf::AssistantJob::Mode);
    void applyResult (const kf::AssistantJob::Result&);

    kf::params::Reader reader;
    std::atomic<bool> paramsDirty { true };
    std::vector<float> monoR;
    int maxBlock = 512;

    kf::Capture capture;
    kf::AssistantJob job;
    std::atomic<bool> listeningForMaster { false };
    double calGain = 0, calLufs = -HUGE_VAL;
    float lastTarget = -11.0f;
    juce::int64 holdDue = 0;

    mutable std::mutex lock;
    std::vector<kf::AssistantJob::Note> notes;
    std::shared_ptr<const ms::TargetCurve> toneTarget;
    juce::String toneKey;
    Status status;
    std::atomic<int> previewPlatform { -1 };

    std::mutex refLock;
    std::optional<ms::ReferenceAnalysisJob::Result> refResult;
    ms::ReferenceAnalysisJob refJob;   // zuletzt deklariert → zuerst zerstört

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KernfusionProcessor)
};
