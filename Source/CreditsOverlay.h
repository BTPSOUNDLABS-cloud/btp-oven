#pragma once

#include <JuceHeader.h>
#include "OvenLookAndFeel.h"

/**
    First-run credit wall.

    Shown once per build, not every launch. Two reasons: nobody reads the same
    splash twice, and respect that becomes wallpaper stops being respect.
*/
class CreditsOverlay : public juce::Component
{
public:
    std::function<void()> onDismiss;

    CreditsOverlay()
    {
        dismiss.setButtonText ("CONTINUE");
        dismiss.setColour (juce::TextButton::buttonColourId, OvenLookAndFeel::lime());
        dismiss.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        dismiss.onClick = [this]
        {
            if (onDismiss) onDismiss();
        };
        addAndMakeVisible (dismiss);
        setAlwaysOnTop (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xf20a0b0d));

        auto area = getLocalBounds().reduced (juce::roundToInt (getWidth() * 0.08f));

        g.setColour (OvenLookAndFeel::lime());
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (18.0f, getWidth() * 0.026f),
                                                  juce::Font::bold)));
        g.drawText ("MADE WITH", area.removeFromTop (juce::roundToInt (getHeight() * 0.09f)),
                    juce::Justification::centredLeft);

        g.setColour (OvenLookAndFeel::cream().withAlpha (0.62f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (10.0f, getWidth() * 0.0115f))));
        g.drawFittedText (
            "Every tool below was built by people who chose to share their work. "
            "This project stands on that generosity.",
            area.removeFromTop (juce::roundToInt (getHeight() * 0.07f)),
            juce::Justification::topLeft, 2);

        area.removeFromTop (juce::roundToInt (getHeight() * 0.02f));

        struct Entry { const char* name; const char* role; };
        static const Entry entries[] = {
            { "JUCE",              "plugin framework, UI, drag and drop" },
            { "Steinberg VST3 SDK","plugin format" },
            { "ONNX Runtime",      "local model inference" },
            { "ACE-Step",          "loop and music generation" },
            { "RVC / So-VITS-SVC", "voice conversion" },
            { "Demucs",            "stem separation" },
            { "FFmpeg",            "audio and video encoding" },
            { "Python",            "engine and tooling" },
            { "CMake",             "build system" },
            { "Image-Line",        "preset format, via independent interoperability" },
        };

        const int rowHeight = juce::jmax (14, juce::roundToInt (getHeight() * 0.052f));
        for (const auto& entry : entries)
        {
            auto row = area.removeFromTop (rowHeight);
            g.setColour (OvenLookAndFeel::cream());
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (10.0f, getWidth() * 0.0115f),
                                                      juce::Font::bold)));
            g.drawText (entry.name, row.removeFromLeft (juce::roundToInt (getWidth() * 0.22f)),
                        juce::Justification::centredLeft);
            g.setColour (OvenLookAndFeel::cream().withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, getWidth() * 0.0105f))));
            g.drawText (entry.role, row, juce::Justification::centredLeft);
        }

        auto footer = getLocalBounds().removeFromBottom (juce::roundToInt (getHeight() * 0.12f));
        g.setColour (OvenLookAndFeel::cream().withAlpha (0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (9.0f, getWidth() * 0.0105f))));
        g.drawText ("BtP SoundLabs", footer.removeFromLeft (getWidth() / 2),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int w = juce::roundToInt (getWidth() * 0.14f);
        const int h = juce::roundToInt (getHeight() * 0.06f);
        dismiss.setBounds (getWidth() - w - juce::roundToInt (getWidth() * 0.06f),
                           getHeight() - h - juce::roundToInt (getHeight() * 0.05f), w, h);
    }

private:
    juce::TextButton dismiss;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CreditsOverlay)
};
