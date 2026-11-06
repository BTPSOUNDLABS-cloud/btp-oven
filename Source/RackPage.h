#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

/**
    Second editor page: the full FX rack, five-band EQ, routing and key / scale
    selection. Laid out in the same 1148 x 692 design space as the main page.
*/
class RackPage : public juce::Component,
                 private juce::Timer
{
public:
    explicit RackPage (BtPOvenAudioProcessor&);
    ~RackPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called by the MAIN PAGE button (wired up by the editor). */
    std::function<void()> onMainPage;

private:
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<float> design;
    };

    struct UiButton
    {
        std::unique_ptr<juce::TextButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
        juce::Rectangle<float> design;
    };

    void timerCallback() override;

    void addKnob (const juce::String& parameterId, const juce::String& name,
                  juce::Rectangle<float> designArea);
    juce::TextButton* addToggle (const juce::String& parameterId, const juce::String& text,
                                 juce::Rectangle<float> designArea);
    juce::TextButton* addAction (const juce::String& text, juce::Rectangle<float> designArea,
                                 std::function<void()> onClick, bool danger = false);

    void paintPanel (juce::Graphics&, juce::Rectangle<float> area,
                     const juce::String& title, const juce::String& hint = {});
    void paintSignalFlow (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintDelayEchoes (juce::Graphics&);

    juce::Rectangle<int> R (float x, float y, float w, float h) const;

    BtPOvenAudioProcessor& processor;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<UiButton> buttons;

    std::unique_ptr<juce::ComboBox> routeBox;
    std::unique_ptr<juce::ComboBox> keyBox;
    std::unique_ptr<juce::ComboBox> scaleBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAttachment;

    juce::Rectangle<float> flowArea;
    juce::Rectangle<float> keyboardArea;
    juce::Rectangle<float> echoArea;

    int lastKey = -1;
    int lastScale = -1;
    float sc = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackPage)
};
