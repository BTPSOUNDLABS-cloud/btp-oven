#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "StemEngine.h"
#include "OvenLookAndFeel.h"

/**
    Fourth editor page: in-process stem separation (Demucs v4 htdemucs).
    Pick a source (Loop A / B / C or a file), separate into Vocals / Drums /
    Bass / Other, then route each stem into any loop lane or export it.
*/
class StemPage : public juce::Component,
                 private juce::Timer
{
public:
    explicit StemPage (BtPOvenAudioProcessor&);
    ~StemPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onMainPage;

private:
    struct UiButton
    {
        std::unique_ptr<juce::TextButton> button;
        juce::Rectangle<float> design;
    };

    void timerCallback() override;

    juce::TextButton* addAction (const juce::String& text, juce::Rectangle<float> design,
                                 std::function<void()> onClick, bool danger = false);

    void useLoopAsSource (int loopIndex);
    void loadSourceFile();
    void separate();
    void exportStem (int stemIndex);

    void paintPanel (juce::Graphics&, juce::Rectangle<float> design, const juce::String& title,
                     const juce::String& hint = {});
    void paintWave (juce::Graphics&, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, const juce::Colour& colour);
    void paintStemRow (juce::Graphics&, int stemIndex);

    juce::Rectangle<int> R (float x, float y, float w, float h) const;

    BtPOvenAudioProcessor& processor;
    StemEngine& engine;

    std::vector<UiButton> buttons;

    juce::AudioBuffer<float> sourceBuffer;
    double sourceRate = 44100.0;

    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<float> sourcePanel, enginePanel, stemsPanel;
    juce::Rectangle<float> sourceWaveArea;

    float sc = 1.0f;
    float shimmerTime = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemPage)
};
