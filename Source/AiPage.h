#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "AiEngine.h"
#include "OvenLookAndFeel.h"

/**
    Third editor page: the in-process ACE-Step generator ("AI GEN").

    Left = floating prompt + engine status.  Centre = the third waveform lane
    (Loop C) with Play / Bypass / region / reverse / half / double / stutter.
    Bottom-left = generation controls.  Bottom-right = dataset manager + LoRA.

    Everything runs in-process: prompts and audio never leave the plugin.
    The NUKED button on the header never touches this page's prompt, dataset
    or generator state.
*/
class AiPage : public juce::Component,
               private juce::Timer,
               public juce::FileDragAndDropTarget
{
public:
    explicit AiPage (BtPOvenAudioProcessor&);
    ~AiPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Wired to the editor's MAIN PAGE / header button. */
    std::function<void()> onMainPage;

    /** Re-reads working state from the APVTS tree after the host loads a project. */
    void restoreState();

private:
    //==========================================================================
    struct ControlKnob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        juce::Rectangle<float> design;
    };

    struct UiButton
    {
        std::unique_ptr<juce::TextButton> button;
        juce::Rectangle<float> design;
    };

    class DatasetModel : public juce::ListBoxModel
    {
    public:
        explicit DatasetModel (AiPage& o) : owner (o) {}

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;

        AiPage& owner;
    };

    class LoraTrainDialog;

    //==========================================================================
    void timerCallback() override;

    juce::Slider* addControlKnob (const juce::String& name, juce::Rectangle<float> design);
    juce::TextButton* addAction (const juce::String& text, juce::Rectangle<float> design,
                                 std::function<void()> onClick, bool danger = false);
    juce::TextButton* addParamButton (const juce::String& parameterId, const juce::String& text,
                                      juce::Rectangle<float> design, bool danger = false);

    void refreshDatasets();
    void refreshLoras();
    void loadMasterPresets();
    void applyPreset (int index);
    void generate();
    void writeLyrics();          // ask the 4B LM for lyrics from the prompt
    void submitAudio();          // the part that actually renders audio
    juce::String languageCode() const;
    void applyLora();
    void pickModelDir();
    void newDataset();
    void addFilesToDataset();
    void openDatasetFolder();
    void openLoraTrainDialog();
    void startLoraTraining();

    void paintPanel (juce::Graphics&, juce::Rectangle<float> design, const juce::String& title,
                     const juce::String& hint = {});
    void paintWaveform (juce::Graphics&, juce::Rectangle<float> area);
    void paintEngineStatus (juce::Graphics&, juce::Rectangle<float> area);

    juce::Rectangle<int> R (float x, float y, float w, float h) const;

    //==========================================================================
    BtPOvenAudioProcessor& processor;
    AiEngine& engine;

    std::vector<std::unique_ptr<ControlKnob>> knobs;
    std::vector<UiButton> buttons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::Slider>> regionSliders;
    std::vector<juce::Rectangle<float>> regionDesigns;

    std::unique_ptr<juce::TextEditor> promptEditor;
    std::unique_ptr<juce::TextEditor> lyricsEditor;
    std::unique_ptr<juce::ComboBox> presetBox;
    std::unique_ptr<juce::ComboBox> languageBox;
    juce::String modePrefix;

    void applyMode (int mode);

    // Working state survives close/reopen. In a DAW it lives in the APVTS tree,
    // so the host stores it with the project. Standalone has no host to ask, so
    // it also keeps a small JSON file and reloads from that on open.
    // Nothing here is a parameter: it is scratch state, not automation.

    struct SavedUi
    {
        juce::String prompt, lyrics, language, mode;
        double duration = -1.0, steps = -1.0, guidance = -1.0, bpm = -1.0, seed = -2.0;
        bool vocals = false;
        bool autoLyrics = false;
        bool valid = false;

        bool sameAs (const SavedUi& other) const
        {
            return prompt == other.prompt && lyrics == other.lyrics
                && language == other.language && mode == other.mode
                && duration == other.duration && steps == other.steps
                && guidance == other.guidance && bpm == other.bpm
                && seed == other.seed && vocals == other.vocals
                && autoLyrics == other.autoLyrics;
        }
    };

    void saveState();
    void applyUi (const SavedUi&);
    static juce::File uiStateFile();
    bool readStandaloneState (SavedUi& out) const;
    void writeStandaloneState (const SavedUi&) const;

    SavedUi snapshot() const;
    SavedUi lastSaved;

    std::unique_ptr<juce::ListBox> datasetList;
    std::unique_ptr<juce::ComboBox> loraBox;
    std::unique_ptr<juce::Label> loraScaleLabel;

    juce::Slider* durationSlider = nullptr;
    juce::Slider* stepsSlider = nullptr;
    juce::Slider* guidanceSlider = nullptr;
    juce::Slider* bpmSlider = nullptr;
    juce::Slider* seedSlider = nullptr;
    std::unique_ptr<juce::Slider> loraScaleSlider;

    juce::TextButton* generateButton = nullptr;
    juce::TextButton* cancelButton = nullptr;
    juce::TextButton* vocalsButton = nullptr;
    juce::TextButton* loraTrainButton = nullptr;
    juce::TextButton* autoLyricsButton = nullptr;
    juce::TextButton* writeLyricsButton = nullptr;

    bool autoLyrics = false;                 // write lyrics before singing
    bool pendingAudioAfterLyrics = false;    // a lyric job is running for this
    int  lastLyricsSeq = 0;

    std::unique_ptr<LoraTrainDialog> loraDialog;

    DatasetModel datasetModel { *this };

    juce::Array<btpai::DatasetInfo> datasets;
    juce::StringArray loras;
    juce::Array<juce::var> masterPresets;
    AiEngine::Models currentModels;

    juce::File pendingLoraDir;
    juce::String pendingLoraName;
    juce::int64 loraTrainingStartMs = 0;
    bool loraTraining = false;

    juce::ChildProcess loraLauncher;

    juce::Rectangle<float> promptPanel, enginePanel, lanePanel, genPanel, datasetPanel;
    juce::Rectangle<float> waveformArea;

    int selectedDataset = 0;
    float sc = 1.0f;
    bool dragOver = false;
    float shimmerTime = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiPage)
};
