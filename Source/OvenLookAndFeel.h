#pragma once

#include <JuceHeader.h>

/** Custom "trap oven" look: metal knobs, lime LEDs and industrial buttons. */
class OvenLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OvenLookAndFeel() = default;

    static juce::Colour lime()   { return juce::Colour (0xffb6ff2e); }
    static juce::Colour limeHot(){ return juce::Colour (0xffe0ff8a); }
    static juce::Colour blood()  { return juce::Colour (0xffb31219); }
    static juce::Colour cream()  { return juce::Colour (0xffe9e3d2); }
    static juce::Colour ember()  { return juce::Colour (0xffff6b2c); }
    static juce::Colour panel()  { return juce::Colour (0xff1b1d21); }
    static juce::Colour metal()  { return juce::Colour (0xff2e3137); }
    static juce::Colour ink()    { return juce::Colour (0xff0a0b0d); }

    juce::Font labelFont (float height, bool bold = false) const
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmax (9.0f, buttonHeight * 0.40f), juce::Font::bold));
    }

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions (juce::jmax (10.0f, box.getHeight() * 0.42f), juce::Font::bold));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override;
};
