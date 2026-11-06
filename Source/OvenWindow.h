#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/**
    The oven window: dual lime waveform display with heat shimmer, playheads,
    record state, grain sparks and per-loop start / end / loop controls.
*/
class OvenWindow : public juce::Component,
                   private juce::Timer
{
public:
    explicit OvenWindow (BtPOvenAudioProcessor&);
    ~OvenWindow() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::Rectangle<float> getLaneBounds (int loop) const;
    juce::Rectangle<float> getWaveBounds (juce::Rectangle<float> lane) const;

    static constexpr int peakCount = 260;

    BtPOvenAudioProcessor& processor;
    std::array<std::array<float, peakCount>, 2> peaks {};
    float shimmerTime = 0.0f;
    float sc = 1.0f;

    std::array<std::unique_ptr<juce::Slider>, 2> startSliders;
    std::array<std::unique_ptr<juce::Slider>, 2> endSliders;
    std::array<std::unique_ptr<juce::TextButton>, 2> loopButtons;

    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 2> startAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 2> endAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 2> loopAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvenWindow)
};
