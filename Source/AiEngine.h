#pragma once

#include <JuceHeader.h>

/**
    In-process ACE-Step (ggml) generation engine.

    Everything runs inside the plugin process: no Python, no HTTP, no helper
    executable. The engine is a process-wide singleton so that several plugin
    instances share one set of weights in VRAM.

    Threading: model loading and generation happen on a private worker thread.
    The audio thread never touches this object. Results are polled from the
    message thread (BtPOvenAudioProcessor::timerCallback) with takeResult().
*/
class AiEngine : private juce::Thread
{
public:
    //==========================================================================
    struct Models
    {
        juce::File dit;          // acestep-v15-turbo-Q8_0.gguf
        juce::File textEncoder;  // Qwen3-Embedding-0.6B-Q8_0.gguf
        juce::File vae;          // vae-BF16.gguf
        juce::File lm;           // optional 4B LM (lyrics / CoT), may be empty
        juce::File lora;         // optional LoRA safetensors
        float loraScale = 1.0f;

        bool isComplete() const;
        bool operator== (const Models&) const;
        bool operator!= (const Models& o) const { return ! (*this == o); }
    };

    /** What a generated take is used for. Tracks land in Loop C; one-shots are
        previewed and saved to the library instead. */
    enum class Kind { Track, OneShot };

    struct Job
    {
        juce::String prompt;
        juce::String lyrics;
        juce::String keyscale;     // e.g. "F# minor", empty = free
        int          bpm = 0;      // 0 = let the model decide
        float        duration = 12.0f;
        int          steps = 8;
        float        guidance = 1.0f;
        float        shift = 3.0f;
        juce::int64  seed = -1;    // -1 = random
        Kind         kind = Kind::Track;
        bool         withVocals = false;   // run the 4B LM for lyrics + sung vocals
    };

    struct Result
    {
        juce::AudioBuffer<float> audio;
        juce::int64 seed = 0;
        juce::String prompt;
        juce::String lyrics;
        double seconds = 0.0;
        bool valid = false;
        Kind kind = Kind::Track;
    };

    enum class State { Unloaded, Loading, Ready, Generating, Error };

    //==========================================================================
    static AiEngine& getInstance();

    /** True when the plugin was compiled with the engine. */
    static bool isCompiledIn();

    //==========================================================================
    // Configuration (message thread only)
    Models   getModels() const;
    void     setModels (const Models& newModels);

    /** Marks the loaded weights stale so the next generate reloads them. */
    void unload();

    static juce::File getDefaultModelDir();
    static juce::File getConfigFile();

    //==========================================================================
    State        getState() const;
    juce::String getStatusText() const;
    juce::String getLastError() const;
    double       getElapsedSeconds() const;
    bool         isBusy() const;

    /** Warm the weights up in the background. */
    void startLoad();

    void submit (const Job& job);
    void cancel();

    /** Message-thread poll for a finished generation. */
    bool takeResult (Result& out);

    //==========================================================================
    void shutdown();

private:
    AiEngine();
    ~AiEngine() override;

    void run() override;

    void loadFromConfig();
    void saveToConfig() const;
    Models defaultModels() const;
    void freeModels();

    void doLoad();
    void doGenerate (const Job&);
    void setState (State, const juce::String& status, const juce::String& error = {});
    void post (std::function<void()> fn);

    //==========================================================================
    mutable juce::CriticalSection modelLock;
    Models models;

    std::atomic<State> state { State::Unloaded };
    mutable juce::CriticalSection statusLock;
    juce::String statusText { "ENGINE OFF" };
    juce::String lastError;

    struct PendingJob { Job job; bool valid = false; };
    PendingJob pending;
    mutable juce::CriticalSection jobLock;

    std::atomic<bool> cancelFlag { false };
    std::atomic<double> startedAt { 0.0 };
    std::atomic<bool> loadRequested { false };
    std::atomic<bool> unloadRequested { false };
    std::atomic<bool> quitFlag { false };

    juce::CriticalSection resultLock;
    Result pendingResult;

    // Opaque ACE-Step handles (kept out of the header).
    void* synth = nullptr;
    void* lmCtx = nullptr;
    juce::String loadedSignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiEngine)
};

//==============================================================================
/** Dataset / LoRA helpers used by the AI page (message thread only). */
namespace btpai
{
    struct DatasetInfo
    {
        juce::String name;
        juce::File   folder;
        int          numFiles = 0;
        double       seconds = 0.0;
        juce::int64  bytes = 0;

        juce::String getSummary() const;
    };

    juce::File getConfigDir();
    juce::File getDatasetsDir();
    juce::File getLoraDir();

    /** Portable root for all downloaded AI models. Defaults to
        %APPDATA%\\BtP Oven\\Models, with sub-folders ACE-Step / Demucs / RVC / RAVE. */
    juce::File getModelsRoot();

    juce::File getTrainingDir();

    /** Save a captured AI output into the context-correct training folder.
        kind = top folder (Vocals / Loops / OneShots / Stems / Variants).
        sub  = optional sub-folder (one-shot category, stem name, ...). */
    juce::File saveTrainingCapture (const juce::AudioBuffer<float>& audio, double sampleRate,
                                    const juce::String& kind, const juce::String& sub = {},
                                    const juce::String& baseName = {});

    juce::Array<DatasetInfo> scanDatasets();
    DatasetInfo scanDataset (const juce::File& folder);

    juce::File createDataset (const juce::String& name);

    /** Copies audio files into the dataset folder. Returns how many landed. */
    int importFilesIntoDataset (const juce::File& datasetFolder, const juce::StringArray& files);

    bool isSupportedAudio (const juce::String& path);
    juce::StringArray findLoras();
}
