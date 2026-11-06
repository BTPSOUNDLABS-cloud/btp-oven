#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "RvcEngine.h"
#include "OvenLookAndFeel.h"

/**
    Voice page: convert a source vocal into a trained target voice using the
    in-process RVC engine (onnxruntime, no Python at runtime).
*/
class RvcPage : public juce::Component,
                private juce::Timer
{
public:
    explicit RvcPage (BtPOvenAudioProcessor&);
    ~RvcPage() override;

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
    void convert();
    void refreshVoices();

    void paintPanel (juce::Graphics&, juce::Rectangle<float> design, const juce::String& title,
                     const juce::String& hint = {});
    void paintWave (juce::Graphics&, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, const juce::Colour& colour);

    juce::Rectangle<int> R (float x, float y, float w, float h) const;

    BtPOvenAudioProcessor& processor;
    RvcEngine& engine;

    std::vector<UiButton> buttons;

    std::unique_ptr<juce::ComboBox> voiceBox;
    std::unique_ptr<juce::ListBox> voiceList;

    juce::AudioBuffer<float> sourceBuffer;
    double sourceRate = 44100.0;

    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::StringArray voiceNames;
    juce::Array<juce::File> voiceFiles;

    juce::Rectangle<float> sourcePanel, voicePanel, resultPanel;
    juce::Rectangle<float> sourceWaveArea, resultWaveArea;

    float sc = 1.0f;
    float shimmerTime = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RvcPage)
};
