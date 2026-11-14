#pragma once

#include "UiState.h"
#include "../Params.h"
#include <ms_core/ms_core.h>

class KernfusionProcessor;

namespace kf::ui
{

/** Der „Fusionskern“: Kurzzeit-Lautheit als glühender Kern gegen den Zielring,
    außen die Kompression der drei Bänder, innen rot der Limiter. */
class FusionCore : public juce::Component
{
public:
    FusionCore (const UiState&, juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void tick();

private:
    const UiState& state;
    juce::AudioProcessorValueTreeState& apvts;
    float loud = -40, hue = 0.5f, phase = 0;
};

/** Makro-Regler mit Name und Wert darunter. */
class MacroKnob : public juce::Component
{
public:
    MacroKnob (const params::Macro&, juce::AudioProcessorValueTreeState&, std::function<void()> onUserChange);
    void resized() override;
    void paint (juce::Graphics&) override;
    void sync();
    bool isBig() const noexcept { return big; }

private:
    const params::Macro& macro;
    juce::AudioProcessorValueTreeState& apvts;
    std::function<void()> changed;
    juce::Slider slider;
    std::unique_ptr<params::GestureScope> gesture;
    bool big = false, syncing = false;
};

/** Vier Bereiche + Kurve: Master (orange) und Original (grau) gegen das Klangziel (blau). */
class ToneBalance : public juce::Component
{
public:
    explicit ToneBalance (KernfusionProcessor&);
    void paint (juce::Graphics&) override;

private:
    void paintBars (juce::Graphics&, juce::Rectangle<float>, const ms::TargetCurve*, const ms::SpectrumSnapshot& master, const ms::SpectrumSnapshot& orig);
    void paintCurve (juce::Graphics&, juce::Rectangle<float>, const ms::TargetCurve*, const ms::SpectrumSnapshot& master, const ms::SpectrumSnapshot& orig);
    KernfusionProcessor& proc;
};

/** Hinweise des Assistenten (fett: Bereich, normal: was gemacht wurde). */
class NotesView : public juce::Component
{
public:
    void setNotes (const juce::AttributedString&);
    void paint (juce::Graphics&) override;

private:
    juce::AttributedString text;
};

//==============================================================================
class OneClickView : public juce::Component
{
public:
    OneClickView (KernfusionProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    std::function<void()> onChooseReferences;

private:
    class FuseButton : public juce::Button
    {
    public:
        FuseButton() : juce::Button ("fuse") {}
        juce::String text;
        float progress = -1.0f;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void refreshToneBox();
    void toneSelected();
    void syncTargetBox();
    void updateNotes();

    KernfusionProcessor& proc;
    FusionCore core;
    juce::ComboBox targetBox, toneBox;
    FuseButton fuse;
    juce::TextButton relistenButton, finishButton, cancelButton;
    NotesView notes;
    std::vector<std::unique_ptr<MacroKnob>> knobs;
    ToneBalance toneView;
    juce::Array<juce::File> leitsternFiles;
    juce::Rectangle<int> leftPanel, rightPanel, targetLabel, toneLabel, macroHead, toneHead;
    size_t lastNotesHash = 0;
    juce::String lastToneKey;
};

} // namespace kf::ui
