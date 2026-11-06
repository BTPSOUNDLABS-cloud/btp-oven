#pragma once

#include <JuceHeader.h>

/**
    In-process RVC (Retrieval-based Voice Conversion) engine. Uses onnxruntime
    (no Python at runtime) to convert a source vocal into a trained target voice.

    Pipeline (mirrors rvc-python's onnx_inference.py):
      16 kHz mono -> ContentVec [T',768] -> repeat x2 -> [2T',768]
      40 kHz mono -> f0 (Hz) -> mel-quantised "pitch" (1..255) + "pitchf"
      generator(phone, len, pitch, pitchf, ds=0, rnd) -> 40 kHz audio
*/
class RvcEngine : private juce::Thread
{
public:
    enum class State { Unloaded, Loading, Ready, Converting, Error };

    struct Models
    {
        juce::File contentVec;   // vec-768-layer-12.onnx
        juce::File voice;        // <voice>.onnx
        juce::File rmvpe;        // rmvpe.onnx (optional, currently unused)

        bool isComplete() const { return contentVec.existsAsFile() && voice.existsAsFile(); }
    };

    struct Result
    {
        juce::AudioBuffer<float> audio;
        double seconds = 0.0;
        bool   valid = false;
    };

    static RvcEngine& getInstance();
    static bool isCompiledIn();

    Models       getModels() const;
    void         setModels (const Models&);
    void         setVoice (const juce::File& voiceFile);

    static juce::File getDefaultContentVec();
    static juce::File getVoicesDir();
    static juce::File getConfigFile();

    State        getState() const;
    juce::String getStatusText() const;

    void convert (const juce::AudioBuffer<float>& input, double sourceSampleRate);
    bool takeResult (Result& out);

    void shutdown();

private:
    RvcEngine();
    ~RvcEngine() override;

    void run() override;

    void doLoad();
    void doConvert (const juce::AudioBuffer<float>& input, double sourceSampleRate);

    void setState (State, const juce::String& status);
    void loadFromConfig();
    void saveToConfig() const;

    //==========================================================================
    mutable juce::CriticalSection modelLock;
    Models models;

    std::atomic<State> state { State::Unloaded };
    mutable juce::CriticalSection statusLock;
    juce::String statusText { "RVC ENGINE OFF" };

    struct PendingJob
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        bool   valid = false;
    };

    PendingJob pending;
    mutable juce::CriticalSection jobLock;

    juce::CriticalSection resultLock;
    Result pendingResult;

    std::atomic<bool> loadRequested { false };
    std::atomic<bool> quitFlag { false };

    // Opaque onnxruntime handles (kept out of this header).
    void* envHandle = nullptr;
    void* contentVecSession = nullptr;
    void* voiceSession = nullptr;
    juce::String loadedContentVec;
    juce::String loadedVoice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RvcEngine)
};
