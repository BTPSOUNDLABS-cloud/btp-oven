#pragma once

#include <JuceHeader.h>
#include "AudioCapture.h"
#include "EngineClient.h"

/** Seven band bars plus the fit score, drawn in the host's colours. */
class BandMeter : public juce::Component
{
public:
    BandMeter() { for (auto& v : levels) v = -60.0f; }

    void setColours (juce::Colour waveIn, juce::Colour backgroundIn,
                     juce::Colour textIn, juce::Colour trackIn)
    {
        wave = waveIn; background = backgroundIn; text = textIn; track = trackIn;
        repaint();
    }

    void setFromValues (const juce::var& values, float newScore)
    {
        static const char* keys[] = { "sub_db", "mud_db", "box_db", "body_db",
                                      "presence_db", "sibilance_db", "air_db" };
        for (int i = 0; i < 7; ++i)
            if (values.hasProperty (keys[i]))
                levels[(size_t) i] = (float) (double) values[keys[i]];
        score = newScore;
        hasData = true;
        repaint();
    }

    void setUnavailable() { hasData = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (background);
        g.fillRoundedRectangle (area, 6.0f);

        auto header = area.removeFromTop (22.0f).reduced (8.0f, 3.0f);
        g.setColour (text.withAlpha (0.75f));
        g.setFont (11.0f);
        g.drawText (hasData ? ("fit " + juce::String ((int) score) + "/100")
                            : "no measurement yet",
                    header, juce::Justification::centredLeft);
        if (! hasData)
            return;

        static const char* labels[] = { "sub", "mud", "box", "body", "pres", "sib", "air" };
        auto bars = area.reduced (8.0f, 4.0f);
        const float w = bars.getWidth() / 7.0f;

        for (int i = 0; i < 7; ++i)
        {
            auto slot = juce::Rectangle<float> (bars.getX() + w * (float) i, bars.getY(),
                                                w - 3.0f, bars.getHeight() - 14.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, (levels[(size_t) i] + 60.0f) / 60.0f);
            auto filled = slot.withTrimmedTop (slot.getHeight() * (1.0f - norm));

            g.setColour (track);
            g.fillRoundedRectangle (slot, 3.0f);
            g.setColour (wave);
            g.fillRoundedRectangle (filled, 3.0f);

            g.setColour (text.withAlpha (0.5f));
            g.setFont (9.0f);
            g.drawText (labels[i],
                        juce::Rectangle<float> (slot.getX(), slot.getBottom() + 1.0f,
                                                slot.getWidth(), 12.0f),
                        juce::Justification::centred);
        }
    }

private:
    std::array<float, 7> levels {};
    float score = 0.0f;
    bool hasData = false;
    juce::Colour wave { 0xffb6ff2e }, background { 0xff1b1d21 },
                 text { 0xffe9e3d2 }, track { 0xff2e3137 };
};
