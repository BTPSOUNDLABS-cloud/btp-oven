#pragma once

#include <JuceHeader.h>

/**
    In-process RAVE "variant" engine. Encodes audio to a latent via the RAVE
    encoder, perturbs the latent, and decodes a new variant. Uses onnxruntime
    for the encoder/decoder nets and a small C++ PQMF filter bank (analysis +
    synthesis) around them. No Python at runtime.

    Pipeline:
      48k mono -> PQMF analysis -> encoder.onnx -> latent mu
      -> perturb mu (noise * variation) -> decoder.onnx -> multiband
      -> PQMF synthesis -> 48k audio
*/
class RaveEngine : private juce::Thread
{
public:
    enum class State { Unloaded, Loading, Ready, Mutating, Error };

    struct Result
    {
        juce::AudioBuffer<float> audio;
        double seconds = 0.0;
        bool   valid = false;
    };

    static RaveEngine& getInstance();
    static bool isCompiledIn();

    State        getState() const;
    juce::String getStatusText() const;

    /** Generate one variant of the input. variation = latent noise scale. */
    void mutate (const juce::AudioBuffer<float>& input, double sourceSampleRate,
                 float variation, juce::int64 seed);

    bool takeResult (Result& out);

    static juce::File getModelDir();

    void shutdown();

private:
    RaveEngine();
    ~RaveEngine() override;

    void run() override;

    void doLoad();
    void doMutate (const juce::AudioBuffer<float>& input, double sourceSampleRate,
                   float variation, juce::int64 seed);

    void setState (State, const juce::String& status);

    //==========================================================================
    std::atomic<State> state { State::Unloaded };
    mutable juce::CriticalSection statusLock;
    juce::String statusText { "RAVE ENGINE OFF" };

    struct PendingJob
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        float  variation = 0.5f;
        juce::int64 seed = 0;
        bool   valid = false;
    };

    PendingJob pending;
    mutable juce::CriticalSection jobLock;

    juce::CriticalSection resultLock;
    Result pendingResult;

    std::atomic<bool> loadRequested { false };
    std::atomic<bool> quitFlag { false };

    // Opaque onnxruntime handles.
    void* envHandle = nullptr;
    void* encoderSession = nullptr;
    void* decoderSession = nullptr;

    // PQMF filter bank [M, filterLen].
    std::vector<float> hk;
    int pqmfBands = 16;
    int pqmfFilterLen = 0;
    int latentSize = 128;
    int sampleRate = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RaveEngine)
};
