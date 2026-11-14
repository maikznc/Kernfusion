#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*  Gestaltung von Kernfusion: dunkles Studio-Werkzeug, Akzent „Fusionsplasma“ (rot/orange) gegen kühles Blau.
    Schriften: Barlow Semi Condensed (Beschriftung) und IBM Plex Mono (Zahlen), beide eingebettet (SIL OFL). */
namespace kf::ui
{

namespace C
{
    inline const juce::Colour bg      { 0xff0b0c11 };
    inline const juce::Colour panel   { 0xff12141c };
    inline const juce::Colour raised  { 0xff1a1d27 };
    inline const juce::Colour line    { 0xff272b38 };
    inline const juce::Colour grid    { 0xff1e222d };
    inline const juce::Colour fg      { 0xffe3e5ed };
    inline const juce::Colour dim     { 0xff8e94a6 };
    inline const juce::Colour faint   { 0xff555c6e };
    inline const juce::Colour plasma  { 0xffff5a3c };
    inline const juce::Colour ember   { 0xffffad4a };
    inline const juce::Colour rose    { 0xffff3d7a };
    inline const juce::Colour cool    { 0xff4f8dff };
    inline const juce::Colour ok      { 0xff45d19f };
    inline const juce::Colour warn    { 0xffffb020 };
    inline const juce::Colour bad     { 0xffff4d5e };
    inline const juce::Colour vizBg   { 0xff0d0f15 };
}

/** Schriften. weight: 0 Medium, 1 SemiBold, 2 Bold (Beschriftung) bzw. 0/1 (Zahlen). */
juce::Font label (float size, int weight = 0);
juce::Font mono (float size, int weight = 0);

/** Großbuchstaben mit Laufweite (für Überschriften). */
juce::Font caps (float size, int weight = 1, float tracking = 0.12f);

// Text-Helfer (deutsche Formatierung)
juce::String de (const char* utf8);
juce::String num (double v, int decimals = 1);
juce::String signedNum (double v, int decimals = 1);
juce::String lufs (double v);
juce::String hz (double f);

/** Gefüllter Hintergrund mit abgerundeten Ecken und Kontur – für Panels. */
void drawPanel (juce::Graphics&, juce::Rectangle<float>, float radius = 12.0f, juce::Colour fill = C::panel);

/** Plasma-Verlauf (für den Hauptknopf, den Markennamen, die Regler). */
juce::ColourGradient plasmaGradient (juce::Rectangle<float>);

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

} // namespace kf::ui
