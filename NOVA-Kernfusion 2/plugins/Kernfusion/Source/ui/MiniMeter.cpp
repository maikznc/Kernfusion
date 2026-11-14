#include "MiniMeter.h"

namespace kf::ui
{

MiniMeter::MiniMeter (const UiState& s, std::atomic<int>& colourSetting, std::function<float()> getCeilingDb)
    : state (s), colour (colourSetting), ceiling (std::move (getCeilingDb))
{
    int i = 0;
    for (auto* b : { &redButton, &blueButton, &spectralButton })
    {
        b->setClickingTogglesState (false);
        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, i] { colour = i; syncColourButtons(); repaint(); };
        addAndMakeVisible (*b);
        ++i;
    }

    redButton.setTooltip (de ("Waveform in Rot"));
    blueButton.setTooltip (de ("Waveform in Blau"));
    spectralButton.setTooltip (de ("Farbe nach Frequenz: Bass rot, Mitten hell, H\xc3\xb6hen blau"));

    resetButton.setButtonText (de ("Messung zur\xc3\xbc" "cksetzen"));
    resetButton.setTooltip (de ("Integrierte Lautheit und True Peak neu messen"));
    resetButton.onClick = [this] { if (onResetMeters) onResetMeters(); };
    addAndMakeVisible (resetButton);
    syncColourButtons();
}

void MiniMeter::setWarning (const juce::String& w)
{
    if (w != warning)
    {
        warning = w;
        repaint();
    }
}

void MiniMeter::syncColourButtons()
{
    const int c = colour.load();
    redButton.setToggleState (c == 0, juce::dontSendNotification);
    blueButton.setToggleState (c == 1, juce::dontSendNotification);
    spectralButton.setToggleState (c == 2, juce::dontSendNotification);
}

void MiniMeter::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    auto controls = r.removeFromRight (196);
    r.removeFromRight (14);
    valuesArea = r.removeFromRight (380);
    r.removeFromRight (14);
    waveArea = r;

    auto top = controls.removeFromTop (30);
    redButton.setBounds (top.removeFromLeft (54));
    blueButton.setBounds (top.removeFromLeft (58));
    spectralButton.setBounds (top);
    controls.removeFromTop (6);
    resetButton.setBounds (controls.removeFromTop (28));
}

juce::Colour MiniMeter::columnColour (const WaveColumn& c) const
{
    switch (colour.load())
    {
        case 0: return C::bad;
        case 1: return C::cool;
        default: break;
    }

    // Spektral: Bass rot, Mitten hell, Höhen blau – gemischt nach Energie
    const float s = c.lo + c.mid + c.hi * 1.6f + 1e-9f;
    const float a = c.lo / s, b = c.mid / s, h = c.hi * 1.6f / s;
    const float r = a * 255 + b * 235 + h * 79, g = a * 70 + b * 225 + h * 141, bl = a * 80 + b * 255 + h * 255;
    return juce::Colour ((juce::uint8) juce::jmin (255.0f, r), (juce::uint8) juce::jmin (255.0f, g), (juce::uint8) juce::jmin (255.0f, bl));
}

void MiniMeter::paintWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff07080c));
    g.fillRoundedRectangle (r, 8.0f);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.reduced (1.0f).toNearestInt());

    const float mid = r.getCentreY(), scale = r.getHeight() * 0.5f - 3.0f;
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (juce::roundToInt (mid), r.getX(), r.getRight());

    // Ceiling-Linien
    const float c = (float) std::pow (10.0, ceiling() / 20.0) * scale;
    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (C::bad.withAlpha (0.28f));
    g.drawDashedLine ({ r.getX(), mid - c, r.getRight(), mid - c }, dashes, 2, 1.0f);
    g.drawDashedLine ({ r.getX(), mid + c, r.getRight(), mid + c }, dashes, 2, 1.0f);

    const int cols = juce::jmin (state.count, (int) r.getWidth());

    for (int i = 0; i < cols; ++i)
    {
        const auto& col = state.column (cols - 1 - i);
        const float x = r.getRight() - (float) cols + (float) i;
        const float fade = 0.45f + 0.55f * ((float) i / (float) juce::jmax (1, cols));

        // Original als Schatten
        g.setColour (juce::Colour (0x22a0aabe));
        g.fillRect (x, mid - col.inMax * scale, 1.0f, juce::jmax (1.0f, (col.inMax - col.inMin) * scale));

        g.setColour (columnColour (col).withAlpha (fade));
        g.fillRect (x, mid - col.outMax * scale, 1.0f, juce::jmax (1.0f, (col.outMax - col.outMin) * scale));
    }

    if (state.count == 0)
    {
        g.setColour (C::faint);
        g.setFont (label (14.0f));
        g.drawText (de ("Play dr\xc3\xbc" "cken \xe2\x80\x93 hier l\xc3\xa4uft die Waveform deines Masters mit"), r, juce::Justification::centred);
    }

    g.setColour (C::faint);
    g.setFont (caps (11.0f, 1, 0.16f));
    g.drawText ("AUSGANG", r.reduced (10.0f, 5.0f), juce::Justification::topLeft);

    if (warning.isNotEmpty())
    {
        g.setFont (label (13.5f, 2));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), warning) + 24.0f;
        const auto pill = juce::Rectangle<float> (r.getRight() - w - 8.0f, r.getY() + 6.0f, w, 22.0f);
        g.setColour (C::warn);
        g.fillRoundedRectangle (pill, 11.0f);
        g.setColour (juce::Colour (0xff1a1206));
        g.drawText (warning, pill, juce::Justification::centred);
    }
}

void MiniMeter::paint (juce::Graphics& g)
{
    g.setColour (C::bg.withAlpha (0.96f));
    g.fillRect (getLocalBounds());
    g.setColour (C::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    paintWave (g, waveArea.toFloat());

    // Werte
    struct V { const char* name; juce::String value; bool hot; };
    const V values[] = {
        { "KURZ", lufs (state.outShort), false },
        { "INTEGRIERT", lufs (state.outIntegrated), false },
        { "TRUE PEAK", state.outTruePeak > -90 ? num (state.outTruePeak) : de ("\xe2\x80\x93"), state.outTruePeak > ceiling() + 0.05f },
        { "LIMITER", state.limiterGr > 0.05f ? de ("\xe2\x88\x92") + num (state.limiterGr) : juce::String ("0,0"), false },
    };

    auto r = valuesArea.toFloat();
    const float w = r.getWidth() / 4.0f;

    for (const auto& v : values)
    {
        auto cell = r.removeFromLeft (w).reduced (4.0f, 4.0f);
        g.setColour (C::faint);
        g.setFont (caps (11.5f, 1, 0.14f));
        g.drawText (de (v.name), cell.removeFromTop (18.0f), juce::Justification::centredRight);
        g.setColour (v.hot ? C::bad : C::fg);
        g.setFont (mono (22.0f, 1));
        g.drawText (v.value, cell.removeFromTop (30.0f), juce::Justification::centredRight);
    }
}

} // namespace kf::ui
