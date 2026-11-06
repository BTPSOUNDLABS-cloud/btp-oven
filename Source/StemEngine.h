#pragma once

#include <JuceHeader.h>

/**
    In-process Demucs v4 (htdemucs) stem separator. Runs entirely inside the
    plugin: no Python, no server, no network. The model is loaded from a
    Demucs ggml-format .bin file (converted weights, see demucs.cpp).

    Threading mirrors AiEngine: loading and separation run on a private worker
    thread, results are polled from the message thread.
*/
class StemEngine : private juce::Thread
{
public:
    enum class State { Unloaded, Loading, Ready, Separating, Error };

    struct Result
    {
        std::array<juce::AudioBuffer<float>, 4> stems;   // 0 drums, 1 bass, 2 other, 3 vocals
        double seconds = 0.0;
        bool   valid = false;
    };

    static StemEngine& getInstance();
    static bool isCompiledIn();

    //==========================================================================
    juce::File getModelFile() const;
    void       setModelFile (const juce::File& file);

    static juce::File getDefaultModelFile();
    static juce::File getConfigFile();

    State        getState() const;
    juce::String getStatusText() const;
    float        getProgress() const;

    void startLoad();

    /** Queue a separation job. Input is resampled to 44.1 kHz internally. */
    void run (const juce::AudioBuffer<float>& input, double sourceSampleRate);

    bool takeResult (Result& out);

    void shutdown();

private:
    StemEngine();
    ~StemEngine() override;

    void run() override;

    void doLoad();
    void doSeparate (const juce::AudioBuffer<float>& input, double sourceSampleRate);

    void setState (State, const juce::String& status);
    void loadFromConfig();
    void saveToConfig() const;

    //==========================================================================
    mutable juce::CriticalSection modelLock;
    juce::File modelFile;

    std::atomic<State> state { State::Unloaded };
    mutable juce::CriticalSection statusLock;
    juce::String statusText { "STEM ENGINE OFF" };
    std::atomic<float> progress { 0.0f };

    struct PendingJob
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        bool   valid = false;
    };

    PendingJob pending;
    mutable juce::CriticalSection jobLock;

    std::atomic<bool> loadRequested { false };
    std::atomic<bool> quitFlag { false };

    juce::CriticalSection resultLock;
    Result pendingResult;

    // Opaque demucs model handle (Eigen types stay out of this header).
    void* modelHandle = nullptr;
    juce::String loadedPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemEngine)
};
