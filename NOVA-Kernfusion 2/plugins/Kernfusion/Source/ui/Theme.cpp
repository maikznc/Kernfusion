#include "Theme.h"
#include <BinaryData.h>

namespace kf::ui
{

namespace
{
    struct Typefaces
    {
        juce::Typeface::Ptr label[3], mono[2];

        Typefaces()
        {
            using namespace KernfusionFonts;
            label[0] = juce::Typeface::createSystemTypefaceFor (BarlowSemiCondensedMedium_ttf, BarlowSemiCondensedMedium_ttfSize);
            label[1] = juce::Typeface::createSystemTypefaceFor (BarlowSemiCondensedSemiBold_ttf, BarlowSemiCondensedSemiBold_ttfSize);
            label[2] = juce::Typeface::createSystemTypefaceFor (BarlowSemiCondensedBold_ttf, BarlowSemiCondensedBold_ttfSize);
            mono[0] = juce::Typeface::createSystemTypefaceFor (IBMPlexMonoMedium_ttf, IBMPlexMonoMedium_ttfSize);
            mono[1] = juce::Typeface::createSystemTypefaceFor (IBMPlexMonoSemiBold_ttf, IBMPlexMonoSemiBold_ttfSize);
        }
    };

    const Typefaces& typefaces()
    {
        static const Typefaces t;
        return t;
    }

    juce::Font make (const juce::Typeface::Ptr& tf, float size)
    {
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (size));

        return juce::Font (juce::FontOptions (size));
    }
}

juce::Font label (float size, int weight) { return make (typefaces().label[juce::jlimit (0, 2, weight)], size); }
juce::Font mono (float size, int weight)  { return make (typefaces().mono[juce::jlimit (0, 1, weight)], size); }

juce::Font caps (float size, int weight, float tracking)
{
    return label (size, weight).withExtraKerningFactor (tracking);
}

juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::String num (double v, int decimals)
{
    if (! std::isfinite (v)) return de ("\xe2\x80\x93");
    const auto s = juce::String (std::abs (v), decimals).replaceCharacter ('.', ',');
    return (v < 0 && std::abs (v) >= 0.5 * std::pow (10.0, -decimals) ? de ("\xe2\x88\x92") : juce::String()) + s;
}

juce::String signedNum (double v, int decimals)
{
    if (! std::isfinite (v)) return de ("\xe2\x80\x93");
    if (std::abs (v) < 0.5 * std::pow (10.0, -decimals)) return juce::String (0.0, decimals).replaceCharacter ('.', ',');
    return (v > 0 ? "+" : de ("\xe2\x88\x92")) + juce::String (std::abs (v), decimals).replaceCharacter ('.', ',');
}

juce::String lufs (double v)
{
    return std::isfinite (v) && v > -70.0 ? num (v) : de ("\xe2\x80\x93");
}

juce::String hz (double f)
{
    if (f < 1000.0) return juce::String (juce::roundToInt (f)) + " Hz";
    return juce::String (f / 1000.0, f < 10000.0 ? 2 : 1).replaceCharacter ('.', ',') + " kHz";
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (C::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

juce::ColourGradient plasmaGradient (juce::Rectangle<float> r)
{
    juce::ColourGradient grad (C::ember, r.getX(), r.getCentreY(), C::rose, r.getRight(), r.getCentreY(), false);
    grad.addColour (0.6, C::plasma);
    return grad;
}

//==============================================================================
LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { C::bg, C::raised, C::raised, C::line, C::fg,
                                                                C::raised, C::bg, C::plasma, C::fg })
{
    setColour (juce::ResizableWindow::backgroundColourId, C::bg);
    setColour (juce::TextButton::buttonColourId, C::raised);
    setColour (juce::TextButton::buttonOnColourId, C::plasma.withAlpha (0.24f));
    setColour (juce::TextButton::textColourOffId, C::fg);
    setColour (juce::TextButton::textColourOnId, C::fg);
    setColour (juce::ComboBox::backgroundColourId, C::raised);
    setColour (juce::ComboBox::outlineColourId, C::line);
    setColour (juce::ComboBox::textColourId, C::fg);
    setColour (juce::ComboBox::arrowColourId, C::dim);
    setColour (juce::PopupMenu::backgroundColourId, C::raised);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::plasma.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, C::fg);
    setColour (juce::PopupMenu::textColourId, C::fg);
    setColour (juce::PopupMenu::headerTextColourId, C::dim);
    setColour (juce::Label::textColourId, C::fg);
    setColour (juce::Slider::textBoxTextColourId, C::fg);
    setColour (juce::TooltipWindow::backgroundColourId, C::raised);
    setColour (juce::TooltipWindow::textColourId, C::fg);
    setColour (juce::TooltipWindow::outlineColourId, C::line);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)  { return label (juce::jmin (15.0f, (float) h * 0.5f)); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)            { return label (15.0f); }
juce::Font LookAndFeel::getPopupMenuFont()                           { return label (15.0f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l)                { return l.getFont(); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (down) fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.06f);

    const float rad = 6.0f;
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad,
                           ! (b.isConnectedOnLeft() || b.isConnectedOnTop()), ! (b.isConnectedOnRight() || b.isConnectedOnTop()),
                           ! (b.isConnectedOnLeft() || b.isConnectedOnBottom()), ! (b.isConnectedOnRight() || b.isConnectedOnBottom()));
    g.setColour (fill);
    g.fillPath (p);
    g.setColour (on ? C::plasma.withAlpha (0.65f) : (highlighted ? C::line.brighter (0.25f) : C::line));
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // Ein/Aus-Schalter im Stil eines Kippschalters
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromLeft (44.0f).withSizeKeepingCentre (44.0f, 24.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? C::plasma.withAlpha (0.3f).overlaidWith (C::raised.withAlpha (0.4f)) : C::raised);
    g.fillRoundedRectangle (sw, 12.0f);
    g.setColour (on ? C::plasma.withAlpha (0.7f) : (highlighted ? C::dim : C::line));
    g.drawRoundedRectangle (sw.reduced (0.5f), 12.0f, 1.0f);
    const auto knob = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ on ? sw.getRight() - 12.0f : sw.getX() + 12.0f, sw.getCentreY() });
    g.setColour (on ? C::ember : C::faint);
    g.fillEllipse (knob);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (on ? C::fg : C::dim);
        g.setFont (caps (14.0f, 2));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (10.0f), juce::Justification::centredLeft);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? C::plasma.withAlpha (0.6f) : (box.isMouseOver (true) ? C::line.brighter (0.25f) : C::line));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const float cx = (float) w - 15.0f, cy = (float) h * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (C::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (8, 1, box.getWidth() - 32, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (C::raised);
    g.setColour (C::line);
    g.drawRect (0, 0, w, h);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, cy - 2.0f, (float) w, 4.0f);
    g.setColour (C::line);
    g.fillRoundedRectangle (track, 2.0f);

    // Füllung vom Ausgangswert (z. B. 0 dB) zur aktuellen Position
    const double def = s.getDoubleClickReturnValue();
    const float defPos = (float) s.getPositionOfValue (juce::jlimit (s.getMinimum(), s.getMaximum(), def));
    const float a = juce::jmin (defPos, pos), b = juce::jmax (defPos, pos);
    g.setColour (s.isEnabled() ? C::ember.withAlpha (0.55f) : C::faint);
    g.fillRoundedRectangle (juce::Rectangle<float> (a, cy - 2.0f, juce::jmax (0.0f, b - a), 4.0f), 2.0f);

    const auto thumb = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ pos, cy });
    g.setColour (s.isEnabled() ? C::ember : C::faint);
    g.fillEllipse (thumb);
    g.setColour (juce::Colour (0xff1a0b06));
    g.drawEllipse (thumb.reduced (1.0f), 2.0f);

    if (s.hasKeyboardFocus (false))
    {
        g.setColour (C::cool);
        g.drawEllipse (thumb.expanded (3.0f), 1.5f);
    }
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    // Makro-Regler: Bogen mit Plasma-Verlauf vom Ausgangswert aus, Knopf mit Zeiger
    const float size = (float) juce::jmin (w, h);
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre (size, size);
    const float cx = area.getCentreX(), cy = area.getCentreY(), rad = size * 0.42f, thick = size * 0.065f;

    juce::Path bg;
    bg.addCentredArc (cx, cy, rad, rad, 0.0f, a0, a1, true);
    g.setColour (C::line);
    g.strokePath (bg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const double def = s.getDoubleClickReturnValue();
    const bool centred = s.getMinimum() < 0 && s.getMaximum() > 0 && s.getProperties().getWithDefault ("fromMin", false) == juce::var (false);
    const float centrePos = centred ? (float) s.valueToProportionOfLength (def) : 0.0f;
    const float from = a0 + (a1 - a0) * juce::jmin (centrePos, pos), to = a0 + (a1 - a0) * juce::jmax (centrePos, pos);

    if (to - from > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (cx, cy, rad, rad, 0.0f, from, to, true);
        g.setGradientFill (juce::ColourGradient (C::ember, area.getX(), area.getBottom(), C::plasma, area.getRight(), area.getY(), false));
        g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float kr = size * 0.28f;
    juce::ColourGradient body (juce::Colour (0xff2b3040), cx - kr * 0.3f, cy - kr * 0.4f, juce::Colour (0xff161921), cx + kr * 0.6f, cy + kr * 0.8f, true);
    g.setGradientFill (body);
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (s.hasKeyboardFocus (false) ? C::cool : C::line);
    g.drawEllipse (cx - kr, cy - kr, kr * 2, kr * 2, 1.0f);

    const float ang = a0 + (a1 - a0) * pos - juce::MathConstants<float>::halfPi;
    g.setColour (C::fg);
    g.drawLine (cx + std::cos (ang) * kr * 0.35f, cy + std::sin (ang) * kr * 0.35f,
                cx + std::cos (ang) * kr * 0.85f, cy + std::sin (ang) * kr * 0.85f, 2.5f);
}

} // namespace kf::ui
