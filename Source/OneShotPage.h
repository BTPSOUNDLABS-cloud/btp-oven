#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "AiEngine.h"
#include "OvenLookAndFeel.h"

/**
    One-Shot Studio: generate short one-shot sounds with the in-process ACE-Step
    engine, preview them, then save them into a category-organised library the
    user picks. Library files can be dropped straight back into any loop lane.
*/
class OneShotPage : public juce::Component,
                    private juce::Timer
{
public:
    explicit OneShotPage (BtPOvenAudioProcessor&);
    ~OneShotPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onMainPage;

    static const juce::StringArray& categories();

private:
    struct UiButton
    {
        std::unique_ptr<juce::TextButton> button;
        juce::Rectangle<float> design;
    };

    struct ControlKnob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        juce::Rectangle<float> design;
    };

    class LibraryModel : public juce::ListBoxModel
    {
    public:
        explicit LibraryModel (OneShotPage& o) : owner (o) {}

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;

        OneShotPage& owner;
    };

    void timerCallback() override;

    juce::TextButton* addAction (const juce::String& text, juce::Rectangle<float> design,
                                 std::function<void()> onClick, bool danger = false);
    juce::Slider* addControlKnob (const juce::String& name, juce::Rectangle<float> design);

    void generate();
    void saveLastOneShot();
    void saveRaveVariant();
    void chooseLibraryRoot();
    void refreshLibrary();
    void openLibraryFolder();
    void loadLibraryFile (const juce::File& file);

    void paintPanel (juce::Graphics&, juce::Rectangle<float> design, const juce::String& title,
                     const juce::String& hint = {});
    void paintWave (juce::Graphics&, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, const juce::Colour& colour);

    juce::Rectangle<int> R (float x, float y, float w, float h) const;

    juce::File libraryRoot;
    juce::Array<juce::File> libraryFiles;

    BtPOvenAudioProcessor& processor;
    AiEngine& engine;

    std::vector<UiButton> buttons;
    std::vector<std::unique_ptr<ControlKnob>> knobs;

    std::unique_ptr<juce::TextEditor> promptEditor;
    std::unique_ptr<juce::ComboBox> categoryBox;
    std::unique_ptr<juce::ListBox> libraryList;

    juce::Slider* durationSlider = nullptr;
    juce::Slider* variationSlider = nullptr;
    juce::TextButton* generateButton = nullptr;

    LibraryModel libraryModel { *this };

    juce::Rectangle<float> generatePanel, previewPanel, libraryPanel;
    juce::Rectangle<float> previewWaveArea, variantWaveArea;

    float sc = 1.0f;
    float shimmerTime = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OneShotPage)
};
