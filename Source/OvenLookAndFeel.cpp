#include "OvenLookAndFeel.h"

void OvenLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                        juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .reduced (4.0f);

    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float arcRadius = radius - 3.0f;

    const bool enabled = slider.isEnabled();
    const auto accent = enabled ? lime() : lime().withAlpha (0.35f);

    // Tick marks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = rotaryStartAngle + (float) i / 10.0f * (rotaryEndAngle - rotaryStartAngle);
        const float inner = arcRadius - 2.0f;
        const float outer = arcRadius + (i % 5 == 0 ? 3.0f : 1.5f);

        g.setColour (cream().withAlpha (i % 5 == 0 ? 0.35f : 0.16f));
        g.drawLine (centre.x + std::sin (a) * inner, centre.y - std::cos (a) * inner,
                    centre.x + std::sin (a) * outer, centre.y - std::cos (a) * outer,
                    1.0f);
    }

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (ink());
    g.strokePath (track, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc with glow
    if (angle > rotaryStartAngle + 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, angle, true);

        g.setColour (accent.withAlpha (0.14f));
        g.strokePath (valueArc, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
    }

    // Knob body
    const float bodyRadius = radius * 0.60f;
    const auto body = juce::Rectangle<float> (centre.x - bodyRadius, centre.y - bodyRadius,
                                              bodyRadius * 2.0f, bodyRadius * 2.0f);

    juce::ColourGradient bodyGrad (juce::Colour (0xff4d515a), body.getX(), body.getY(),
                                   juce::Colour (0xff17191d), body.getRight(), body.getBottom(), false);
    bodyGrad.addColour (0.45, juce::Colour (0xff2a2d33));

    g.setGradientFill (bodyGrad);
    g.fillEllipse (body);

    g.setColour (ink());
    g.drawEllipse (body, 1.6f);

    // Top highlight
    juce::Path sheen;
    sheen.addCentredArc (centre.x, centre.y, bodyRadius * 0.72f, bodyRadius * 0.72f, 0.0f,
                         juce::MathConstants<float>::pi * 1.15f,
                         juce::MathConstants<float>::pi * 1.85f, true);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (sheen, juce::PathStrokeType (2.0f));

    // Pointer
    const auto p1 = centre + juce::Point<float> (std::sin (angle) * bodyRadius * 0.30f,
                                                 -std::cos (angle) * bodyRadius * 0.30f);
    const auto p2 = centre + juce::Point<float> (std::sin (angle) * bodyRadius * 0.95f,
                                                 -std::cos (angle) * bodyRadius * 0.95f);

    g.setColour (accent.withAlpha (0.25f));
    g.drawLine ({ p1, p2 }, 4.5f);
    g.setColour (accent);
    g.drawLine ({ p1, p2 }, 2.0f);

    // Centre screw
    g.setColour (juce::Colour (0xff0d0e11));
    g.fillEllipse (juce::Rectangle<float> (centre.x - 2.2f, centre.y - 2.2f, 4.4f, 4.4f));
}

//==============================================================================
void OvenLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted,
                                        bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    auto fill = on ? lime().withAlpha (0.20f) : juce::Colour (0xff14161a);

    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.10f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.05f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (on ? lime().withAlpha (0.85f) : juce::Colour (0xff3a3d45));
    g.drawRoundedRectangle (bounds, 4.0f, 1.2f);

    float textLeft = bounds.getX() + 8.0f;

    if (bounds.getWidth() > 46.0f)
    {
        const float ledR = juce::jmin (3.4f, bounds.getHeight() * 0.18f);
        const auto ledCentre = juce::Point<float> (bounds.getX() + 9.0f, bounds.getCentreY());

        if (on)
        {
            g.setColour (lime().withAlpha (0.28f));
            g.fillEllipse (ledCentre.x - ledR * 2.2f, ledCentre.y - ledR * 2.2f,
                           ledR * 4.4f, ledR * 4.4f);
        }

        g.setColour (on ? lime() : juce::Colour (0xff2b2e34));
        g.fillEllipse (ledCentre.x - ledR, ledCentre.y - ledR, ledR * 2.0f, ledR * 2.0f);

        textLeft = bounds.getX() + 17.0f;
    }

    g.setColour (on ? lime() : cream().withAlpha (button.isEnabled() ? 0.78f : 0.35f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (8.5f, bounds.getHeight() * 0.42f),
                                              juce::Font::bold)));

    g.drawText (button.getButtonText(),
                juce::Rectangle<float> (textLeft, bounds.getY(),
                                        bounds.getRight() - textLeft - 4.0f, bounds.getHeight()),
                juce::Justification::centredLeft, true);
}

//==============================================================================
void OvenLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour&,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const bool danger = (bool) button.getProperties().getWithDefault ("danger", false);
    const auto accent = danger ? blood().brighter (0.2f) : lime();

    auto top = juce::Colour (0xff33373e);
    auto bottom = juce::Colour (0xff191b1f);

    if (on)
    {
        top = accent.withAlpha (0.34f);
        bottom = danger ? juce::Colour (0xff2c0709) : juce::Colour (0xff23300c);
    }

    if (shouldDrawButtonAsDown)
    {
        top = top.darker (0.25f);
        bottom = bottom.darker (0.20f);
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        top = top.brighter (0.12f);
        bottom = bottom.brighter (0.08f);
    }

    juce::ColourGradient grad (top, bounds.getX(), bounds.getY(),
                               bottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.4f);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine (bounds.getX() + 4.0f, bounds.getY() + 1.2f,
                bounds.getRight() - 4.0f, bounds.getY() + 1.2f, 1.0f);

    g.setColour (on ? accent : cream().withAlpha (0.82f));
    g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, bounds.getHeight() * 0.40f),
                                              juce::Font::bold)));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
void OvenLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (juce::Colour (0xff101215));
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (box.hasKeyboardFocus (true) ? lime().withAlpha (0.8f) : juce::Colour (0xff3b3f47));
    g.drawRoundedRectangle (bounds, 4.0f, 1.2f);

    // Arrow
    juce::Path arrow;
    const float cx = (float) width - 14.0f;
    const float cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);

    g.setColour (lime().withAlpha (0.9f));
    g.fillPath (arrow);
}

void OvenLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Label* OvenLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (juce::Font (juce::FontOptions (juce::jlimit (9.0f, 13.0f, slider.getHeight() * 0.22f),
                                                   juce::Font::bold)));
    label->setColour (juce::Label::textColourId, lime().withAlpha (0.85f));
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

    return label;
}
