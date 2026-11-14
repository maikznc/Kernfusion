#pragma once

#include "UiState.h"

class KernfusionProcessor;

namespace kf::ui
{

/** „Industrielautstärke“: Ist der Master laut genug – und was machen Spotify & Co. damit? */
class LoudnessView : public juce::Component
{
public:
    LoudnessView (KernfusionProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    struct Platform { const char* name; float target; bool turnsUp, limiter, official; };
    static const std::array<Platform, 7>& platforms();

    /** Pegeländerung durch die Plattform und Beschreibung (Integrated, True Peak des Masters). */
    static std::pair<float, juce::String> platformChange (const Platform&, float integrated, float truePeak);

private:
    void paintLadder (juce::Graphics&, juce::Rectangle<float>);
    void paintVerdict (juce::Graphics&, juce::Rectangle<float>);
    void paintTable (juce::Graphics&, juce::Rectangle<float>);

    KernfusionProcessor& proc;
    const UiState& state;
    juce::Rectangle<int> ladderArea, verdictArea, tableArea, tipArea, titleArea, textArea, statsArea;
    std::array<juce::TextButton, 7> listen;
    juce::TextButton resetButton, previewOffButton, ceilingButton;
    std::array<juce::Rectangle<int>, 7> rowAreas;
    bool showTip = false;
};

} // namespace kf::ui
