#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "OvenLookAndFeel.h"
#include "OvenWindow.h"
#include "RackPage.h"
#include "AiPage.h"
#include "StemPage.h"
#include "OneShotPage.h"
#include "RvcPage.h"

//==============================================================================
class BtPOvenAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer,
                                    public juce::FileDragAndDropTarget
{
public:
    explicit BtPOvenAudioProcessorEditor (BtPOvenAudioProcessor&);
    ~BtPOvenAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    //==========================================================================
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<float> design;
        bool keepVisible = false;
    };

    struct UiButton
    {
        std::unique_ptr<juce::TextButton> button;
        juce::Rectangle<float> design;
        bool keepVisible = false;
    };

    void timerCallback() override;

    void addKnob (const juce::String& parameterId, const juce::String& name,
                  juce::Rectangle<float> designArea, bool keepVisible = false);
    juce::TextButton* addButton (const juce::String& text, juce::Rectangle<float> designArea,
                                 std::function<void()> onClick, bool isToggle,
                                 bool danger = false, bool keepVisible = false);
    juce::TextButton* addParamButton (const juce::String& parameterId, const juce::String& text,
                                      juce::Rectangle<float> designArea, bool danger = false);

    void toggleRackPage();
    void toggleAiPage();
    void toggleStemPage();
    void toggleOneShotPage();
    void toggleRvcPage();
    void setMainPageVisible (bool visible);

    void toggleRecord (int loopIndex, bool overdub);
    void loadLoopFile (int loopIndex);
    void exportLoopFile (int loopIndex);

    void paintHeader (juce::Graphics&);
    void paintPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title,
                     const juce::String& hint = {}, bool hintOnTitleRow = false);
    void paintLevelMeter (juce::Graphics&);
    void paintLaneStrip (juce::Graphics&);
    void paintStatus (juce::Graphics&);

    juce::Rectangle<int> S (float x, float y, float w, float h) const;

    //==========================================================================
    BtPOvenAudioProcessor& processor;
    OvenLookAndFeel lnf;
    OvenWindow ovenWindow;
    RackPage rackPage;
    AiPage aiPage;
    StemPage stemPage;
    OneShotPage oneShotPage;
    RvcPage rvcPage;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<UiButton> buttons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    std::unique_ptr<juce::ComboBox> filterBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterAttachment;

    std::unique_ptr<juce::ComboBox> chopDivBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chopDivAttachment;

    juce::TextButton* recA = nullptr;
    juce::TextButton* recB = nullptr;
    juce::TextButton* dubA = nullptr;
    juce::TextButton* dubB = nullptr;
    juce::TextButton* loopA = nullptr;
    juce::TextButton* loopB = nullptr;
    juce::TextButton* loopC = nullptr;
    juce::TextButton* fxArm = nullptr;
    juce::TextButton* nukeButton = nullptr;
    juce::TextButton* rackButton = nullptr;
    juce::TextButton* aiButton = nullptr;
    juce::TextButton* stemButton = nullptr;
    juce::TextButton* oneShotButton = nullptr;
    juce::TextButton* rvcButton = nullptr;

    juce::Rectangle<int> panelCook, panelTiming, panelTransport,
                         panelPitch, panelFilters, panelFx;
    juce::Rectangle<int> meterArea, laneStripArea, statusArea;

    std::unique_ptr<juce::FileChooser> fileChooser;
    bool dragOver = false;
    bool rackPageVisible = false;
    bool aiPageVisible = false;
    bool stemPageVisible = false;
    bool oneShotPageVisible = false;
    bool rvcPageVisible = false;
    float sc = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BtPOvenAudioProcessorEditor)
};
