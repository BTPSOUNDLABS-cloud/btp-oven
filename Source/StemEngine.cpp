#include "StemEngine.h"
#include "ModelPaths.h"   // btppaths::getModelsRoot()

#if BTP_HAS_STEM
 #include "model.hpp"
#endif

namespace
{
    constexpr int kStemRate = 44100;

    // Simple linear resampler: source (numChannels, n) at inRate -> (2, outN) at outRate.
    void resampleToStereo44 (const juce::AudioBuffer<float>& src, double inRate,
                             Eigen::MatrixXf& out)
    {
        const int srcN = src.getNumSamples();
        const int srcCh = src.getNumChannels();
        const double ratio = (double) kStemRate / inRate;
        const int outN = juce::jmax (1, (int) std::llround ((double) srcN * ratio));

        out.resize (2, outN);

        const float* in0 = srcCh > 0 ? src.getReadPointer (0) : nullptr;
        const float* in1 = srcCh > 1 ? src.getReadPointer (1) : in0;

        for (int i = 0; i < outN; ++i)
        {
            const double pos = (double) i / ratio;
            const int i0 = juce::jmin (srcN - 1, (int) pos);
            const int i1 = juce::jmin (srcN - 1, i0 + 1);
            const float frac = (float) (pos - (double) i0);

            out (0, i) = (in0 != nullptr) ? in0[i0] + (in0[i1] - in0[i0]) * frac : 0.0f;
            out (1, i) = (in1 != nullptr) ? in1[i0] + (in1[i1] - in1[i0]) * frac : out (0, i);
        }
    }
}

//==============================================================================
StemEngine& StemEngine::getInstance()
{
    static StemEngine engine;
    return engine;
}

bool StemEngine::isCompiledIn()
{
   #if BTP_HAS_STEM
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
StemEngine::StemEngine()
    : juce::Thread ("BtP Stem Engine")
{
    loadFromConfig();
    startThread (juce::Thread::Priority::low);
}

StemEngine::~StemEngine()
{
    shutdown();
}

void StemEngine::shutdown()
{
    quitFlag.store (true);

    if (isThreadRunning())
        stopThread (2000);
}

//==============================================================================
juce::File StemEngine::getConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("BtP Oven")
             .getChildFile ("stem_settings.json");
}

juce::File StemEngine::getDefaultModelFile()
{
    juce::Array<juce::File> candidates;

    const auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                            .getParentDirectory();

    // Portable layouts first, legacy dev paths last.
    candidates.add (btppaths::getModelsRoot().getChildFile ("Demucs")
                        .getChildFile ("ggml-model-htdemucs-4s-f16.bin"));
    candidates.add (btppaths::getModelsRoot().getChildFile ("ggml-model-htdemucs-4s-f16.bin"));
    candidates.add (appDir.getChildFile ("Models").getChildFile ("Demucs")
                        .getChildFile ("ggml-model-htdemucs-4s-f16.bin"));
    candidates.add (juce::File ("D:/Dev/demucs-models/ggml-model-htdemucs-4s-f16.bin"));
    candidates.add (juce::File ("D:/Dev/demucs.cpp/ggml-demucs/ggml-model-htdemucs-4s-f16.bin"));

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;

    return candidates.getFirst();
}

void StemEngine::loadFromConfig()
{
    juce::File f = getDefaultModelFile();

    const juce::File cfg = getConfigFile();

    if (cfg.existsAsFile())
    {
        const auto json = juce::JSON::parse (cfg);

        if (auto* obj = json.getDynamicObject())
        {
            const auto path = obj->getProperty ("model").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                f = juce::File (path);
        }
    }

    modelFile = f;
    setState (State::Unloaded, f.existsAsFile() ? "STEM ENGINE IDLE" : "STEM MODEL NOT FOUND");
}

void StemEngine::saveToConfig() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("model", modelFile.getFullPathName());
    getConfigFile().getParentDirectory().createDirectory();
    getConfigFile().replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

juce::File StemEngine::getModelFile() const
{
    const juce::ScopedLock sl (modelLock);
    return modelFile;
}

void StemEngine::setModelFile (const juce::File& file)
{
    {
        const juce::ScopedLock sl (modelLock);
        modelFile = file;
    }

    saveToConfig();
    loadRequested.store (true);
    notify();
}

//==============================================================================
StemEngine::State StemEngine::getState() const { return state.load(); }

juce::String StemEngine::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

float StemEngine::getProgress() const { return progress.load (std::memory_order_relaxed); }

void StemEngine::setState (State s, const juce::String& text)
{
    {
        const juce::ScopedLock sl (statusLock);
        statusText = text;
    }
    state.store (s);
}

//==============================================================================
void StemEngine::startLoad()
{
    loadRequested.store (true);
    setState (State::Loading, "LOADING DEMUCS MODEL");
    notify();
}

void StemEngine::run (const juce::AudioBuffer<float>& input, double sourceSampleRate)
{
    if (input.getNumSamples() < 1024)
    {
        setState (State::Error, "INPUT TOO SHORT TO SEPARATE");
        return;
    }

    {
        const juce::ScopedLock sl (jobLock);
        pending.audio = input;
        pending.sampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
        pending.valid = true;
    }

    notify();
}

bool StemEngine::takeResult (Result& out)
{
    const juce::ScopedLock sl (resultLock);

    if (! pendingResult.valid)
        return false;

    out = pendingResult;
    pendingResult = Result();
    return true;
}

//==============================================================================
void StemEngine::run()
{
   #if BTP_HAS_STEM
    while (! threadShouldExit())
    {
        if (quitFlag.load())
            break;

        if (loadRequested.exchange (false))
        {
            doLoad();
            continue;
        }

        PendingJob job;
        {
            const juce::ScopedLock sl (jobLock);
            job = pending;
            pending = PendingJob();
        }

        if (job.valid)
        {
            if (modelHandle == nullptr)
                doLoad();

            if (modelHandle != nullptr)
                doSeparate (job.audio, job.sampleRate);
            else
                setState (State::Error, "STEM MODEL NOT LOADED");

            continue;
        }

        wait (100);
    }

    if (modelHandle != nullptr)
    {
       #if BTP_HAS_STEM
        delete static_cast<demucscpp::demucs_model*> (modelHandle);
       #endif
        modelHandle = nullptr;
    }
   #else
    while (! threadShouldExit())
        wait (200);
   #endif
}

//==============================================================================
#if BTP_HAS_STEM

void StemEngine::doLoad()
{
    juce::File f;
    {
        const juce::ScopedLock sl (modelLock);
        f = modelFile;
    }

    if (! f.existsAsFile())
    {
        setState (State::Error, "STEM MODEL FILE MISSING");
        return;
    }

    if (modelHandle != nullptr && loadedPath == f.getFullPathName())
    {
        setState (State::Ready, "STEM ENGINE READY");
        return;
    }

    if (modelHandle != nullptr)
    {
        delete static_cast<demucscpp::demucs_model*> (modelHandle);
        modelHandle = nullptr;
    }

    setState (State::Loading, "LOADING DEMUCS WEIGHTS");

    auto* model = new demucscpp::demucs_model();

    if (! demucscpp::load_demucs_model (f.getFullPathName().toStdString(), model))
    {
        delete model;
        setState (State::Error, "FAILED TO LOAD STEM MODEL");
        return;
    }

    modelHandle = model;
    loadedPath = f.getFullPathName();

    setState (State::Ready, "STEM ENGINE READY");
}

void StemEngine::doSeparate (const juce::AudioBuffer<float>& input, double sourceSampleRate)
{
    auto* model = static_cast<demucscpp::demucs_model*> (modelHandle);

    progress.store (0.0f, std::memory_order_relaxed);
    setState (State::Separating, "SEPARATING STEMS");

    Eigen::MatrixXf audio;
    resampleToStereo44 (input, sourceSampleRate, audio);

    demucscpp::ProgressCallback cb = [this] (float p, const std::string& msg)
    {
        progress.store (p, std::memory_order_relaxed);

        if (! msg.empty())
        {
            const juce::ScopedLock sl (statusLock);
            statusText = "SEPARATING: " + juce::String (msg.c_str());
        }
    };

    Eigen::Tensor3dXf targets = demucscpp::demucs_inference (*model, audio, cb);

    const int nbSources = model->is_4sources ? 4 : 6;
    const int length = (int) audio.cols();

    Result result;
    result.seconds = (double) length / (double) kStemRate;

    for (int s = 0; s < juce::jmin (4, nbSources); ++s)
    {
        auto& stem = result.stems[(size_t) s];
        stem.setSize (2, length, false, true, false);

        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = stem.getWritePointer (ch);
            for (int i = 0; i < length; ++i)
                dst[i] = targets (s, ch, i);
        }
    }

    result.valid = true;

    {
        const juce::ScopedLock sl (resultLock);
        pendingResult = std::move (result);
    }

    progress.store (1.0f, std::memory_order_relaxed);
    setState (State::Ready, "SEPARATION DONE - " + juce::String (result.seconds, 1) + " s");
}

#else

void StemEngine::doLoad()
{
    setState (State::Error, "STEM DISABLED");
}

void StemEngine::doSeparate (const juce::AudioBuffer<float>&, double)
{
    setState (State::Error, "STEM DISABLED");
}

#endif
