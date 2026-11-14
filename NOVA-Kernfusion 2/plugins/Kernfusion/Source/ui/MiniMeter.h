#pragma once

#include "UiState.h"

namespace kf::ui
{

/** Immer sichtbares Fenster oben: scrollende Waveform des Masters (Rot, Blau oder Spektral),
    dahinter grau das Original, rechts Lautheit, True Peak und Limiter. */
class MiniMeter : public juce::Component
{
public:
    MiniMeter (const UiState&, std::atomic<int>& colourSetting, std::function<float()> getCeilingDb);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onResetMeters;
    /** Warnhinweis (z. B. „Vorschau aktiv“), leer = keiner. */
    void setWarning (const juce::String&);

private:
    void paintWave (juce::Graphics&, juce::Rectangle<float>);
    juce::Colour columnColour (const WaveColumn&) const;
    void syncColourButtons();

    const UiState& state;
    std::atomic<int>& colour;
    std::function<float()> ceiling;
    juce::TextButton redButton { "Rot" }, blueButton { "Blau" }, spectralButton { "Spektral" }, resetButton;
    juce::Rectangle<int> waveArea, valuesArea;
    juce::String warning;
};

} // namespace kf::ui
