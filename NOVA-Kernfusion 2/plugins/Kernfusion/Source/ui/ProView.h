#pragma once

#include "UiState.h"
#include "../Params.h"

class KernfusionProcessor;

namespace kf::ui
{

/** Name + Wert oben, Schieberegler darunter – direkt an einen Parameter gebunden. */
class SliderRow : public juce::Component
{
public:
    SliderRow (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    juce::String name;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

/** Gruppe mit Titel und Reglern (eine Spalte in einem Modul). */
class Group : public juce::Component
{
public:
    explicit Group (juce::String title);
    SliderRow& add (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& label);
    void addExtra (juce::Component&, int height);
    void paint (juce::Graphics&) override;
    void resized() override;
    int idealHeight() const;

    std::function<juce::String()> headerValue;   // optional: Wert rechts im Titel (z. B. Reduktion)
    float meter = -1.0f;                          // optional: Balken unter dem Titel (0…1)

private:
    juce::String title;
    juce::OwnedArray<SliderRow> rows;
    std::vector<std::pair<juce::Component*, int>> extras;
};

/** Ein Modul der Kette (Kopf mit Ein/Aus-Schalter, Inhalt). */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (KernfusionProcessor&, const UiState&, int moduleIndex);
    ~ModulePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    class EqGraph;
    class Goniometer;
    class GrHistory;

    KernfusionProcessor& proc;
    const UiState& state;
    int module;
    juce::ToggleButton power;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAttachment;
    juce::OwnedArray<juce::ToggleButton> toggles;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;   // nach toggles → wird zuerst zerstört
    juce::OwnedArray<Group> groups;
    std::unique_ptr<juce::Component> viz;
    juce::Component* sideViz = nullptr;
    juce::Rectangle<int> headArea, hintArea;
    juce::String hint;
    std::array<juce::TextButton, 3> modeButtons;
    juce::TextButton actionButton;
};

//==============================================================================
class ProView : public juce::Component
{
public:
    ProView (KernfusionProcessor&, const UiState&);
    void resized() override;
    void tick();

private:
    class Tile : public juce::Button
    {
    public:
        Tile (int index, juce::String name);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        juce::String state;
        bool active = true;

    private:
        int index;
    };

    void select (int module);
    juce::String moduleState (int module) const;

    KernfusionProcessor& proc;
    const UiState& uiState;
    juce::OwnedArray<Tile> tiles;
    std::unique_ptr<ModulePanel> panel;
    int selected = 0, frame = 0;
};

} // namespace kf::ui
