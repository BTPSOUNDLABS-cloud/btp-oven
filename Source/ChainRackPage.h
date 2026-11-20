#pragma once

#include <JuceHeader.h>
#include "AudioCapture.h"
#include "EngineClient.h"
#include "EngineService.h"

/**
    Chain rack: the built-in chain as an editable stack.

    Left   - every stage; click to select, BYPASS toggles the selected one.
    Right  - that stage's parameters, editable.
    Middle - the live waveform from the insert.
    Bottom - prompt box, build, drag out.

    Rendering lives in the desktop app; this page is the control surface plus the
    export. That boundary is deliberate: the DSP port is the container product,
    not a page.
*/
class ChainRackPage : public juce::Component,
                      public juce::ListBoxModel,
                      private juce::Timer
{
public:
    ChainRackPage (AudioCapture& audioCapture, EngineService& engineService);
    ~ChainRackPage() override;

    std::function<void()> onMainPage;

    void setPalette (juce::Colour background, juce::Colour panel, juce::Colour textColour,
                     juce::Colour accentColour);

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;

    /** Called with a build response so the rack can adopt the chain. */
    void adoptSpec (const juce::var& specJson);

private:
    void timerCallback() override;
    void refreshStages();
    void rebuildControls();
    void applyPrompt();
    void toggleBypass();
    void buildFromRack();
    void onBuildReply (const EngineReply& reply);

    AudioCapture& capture;
    EngineService& service;

    juce::Colour background { 0xff0a0b0d }, panel { 0xff1b1d21 }, text { 0xffe9e3d2 },
                 accent { 0xffb6ff2e };

    juce::var spec;
    juce::StringArray stageLabels;
    int selectedStage = -1;

    juce::ListBox stageList { "stages", this };
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton backButton { "MAIN" };
    juce::TextEditor promptBox;
    juce::TextButton applyButton { "APPLY" };
    juce::TextButton buildButton { "BUILD + DRAG OUT" };
    juce::Label statusLabel;

    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Label>> sliderLabels;
    juce::StringArray sliderKeys;

    juce::String lastFstPath;
    std::vector<float> waveform;
    juce::Rectangle<int> dragZone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainRackPage)
};
