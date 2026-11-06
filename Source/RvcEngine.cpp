#include "RvcEngine.h"
#include "ModelPaths.h"   // btppaths::getModelsRoot()

#if BTP_HAS_RVC
 #include "onnxruntime_cxx_api.h"
#endif

#include <random>

namespace
{
    constexpr int   kContentVecRate = 16000;
    constexpr int   kGenRate        = 40000;
    constexpr int   kGenHop         = 512;

    // --- mono linear resample ------------------------------------------------
    void resampleMono (const juce::AudioBuffer<float>& src, double inRate, double outRate,
                       std::vector<float>& out)
    {
        const float* l = src.getReadPointer (0);
        const float* r = src.getNumChannels() > 1 ? src.getReadPointer (1) : l;
        const int srcN = src.getNumSamples();
        const int outN = juce::jmax (1, (int) std::llround ((double) srcN * outRate / inRate));

        out.resize ((size_t) outN);

        const double ratio = inRate / outRate;

        for (int i = 0; i < outN; ++i)
        {
            const double pos = (double) i * ratio;
            const int i0 = juce::jmin (srcN - 1, (int) pos);
            const int i1 = juce::jmin (srcN - 1, i0 + 1);
            const float frac = (float) (pos - (double) i0);
            out[(size_t) i] = ((l[i0] + (l[i1] - l[i0]) * frac) + (r[i0] + (r[i1] - r[i0]) * frac)) * 0.5f;
        }
    }

    // --- f0 via per-frame autocorrelation ------------------------------------
    void computeF0 (const std::vector<float>& wav, int hop, std::vector<float>& f0)
    {
        const int N = (int) wav.size();
        const int nFrames = juce::jmax (1, N / hop);
        const int win = 2048;
        const int minLag = 36;    // ~1111 Hz @ 40k
        const int maxLag = 800;   // ~50 Hz   @ 40k

        f0.assign ((size_t) nFrames, 0.0f);

        for (int f = 0; f < nFrames; ++f)
        {
            const int center = f * hop;
            int start = juce::jmax (0, center - win / 2);
            start = juce::jmin (start, N - win);

            float maxCorr = 0.0f;
            int   bestLag = 0;

            for (int lag = minLag; lag < maxLag; ++lag)
            {
                double sum = 0.0, e0 = 0.0, e1 = 0.0;

                for (int i = 0; i < win - lag; ++i)
                {
                    const float a = wav[(size_t) (start + i)];
                    const float b = wav[(size_t) (start + i + lag)];
                    sum += (double) a * b;
                    e0  += (double) a * a;
                    e1  += (double) b * b;
                }

                const float norm = (float) (sum / std::sqrt (e0 * e1 + 1.0e-12));

                if (norm > maxCorr)
                {
                    maxCorr = norm;
                    bestLag = lag;
                }
            }

            f0[(size_t) f] = (maxCorr > 0.5f) ? (float) kGenRate / (float) bestLag : 0.0f;
        }
    }

    void interpolateToLength (const std::vector<float>& in, int targetLen, std::vector<float>& out)
    {
        out.resize ((size_t) targetLen);

        if (in.empty())
        {
            std::fill (out.begin(), out.end(), 0.0f);
            return;
        }

        for (int i = 0; i < targetLen; ++i)
        {
            const double pos = (double) i * (double) (in.size() - 1) / (double) juce::jmax (1, targetLen - 1);
            const int i0 = (int) pos;
            const int i1 = juce::jmin ((int) in.size() - 1, i0 + 1);
            const float frac = (float) (pos - (double) i0);
            out[(size_t) i] = in[(size_t) i0] + (in[(size_t) i1] - in[(size_t) i0]) * frac;
        }
    }

    // --- mel-quantised pitch (mirrors onnx_inference.py) ---------------------
    void quantizePitch (const std::vector<float>& pitchf, std::vector<int64_t>& pitch)
    {
        const float f0Min = 50.0f, f0Max = 1100.0f;
        const float melMin = 1127.0f * std::log (1.0f + f0Min / 700.0f);
        const float melMax = 1127.0f * std::log (1.0f + f0Max / 700.0f);

        pitch.resize (pitchf.size());

        for (size_t i = 0; i < pitchf.size(); ++i)
        {
            const float p = pitchf[i];
            const float mel = 1127.0f * std::log (1.0f + p / 700.0f);

            float q = (mel > 0.0f) ? (mel - melMin) * 254.0f / (melMax - melMin) + 1.0f : 1.0f;
            q = juce::jlimit (1.0f, 255.0f, q);

            pitch[i] = (int64_t) std::llround (q);
        }
    }
}

//==============================================================================
RvcEngine& RvcEngine::getInstance()
{
    static RvcEngine engine;
    return engine;
}

bool RvcEngine::isCompiledIn()
{
   #if BTP_HAS_RVC
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
RvcEngine::RvcEngine()
    : juce::Thread ("BtP RVC Engine")
{
    loadFromConfig();
    startThread (juce::Thread::Priority::low);
}

RvcEngine::~RvcEngine()
{
    shutdown();
}

void RvcEngine::shutdown()
{
    quitFlag.store (true);

    if (isThreadRunning())
        stopThread (2000);
}

//==============================================================================
juce::File RvcEngine::getConfigFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("BtP Oven")
             .getChildFile ("rvc_settings.json");
}

juce::File RvcEngine::getDefaultContentVec()
{
    const auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                            .getParentDirectory();

    juce::Array<juce::File> candidates;

    candidates.add (btppaths::getModelsRoot().getChildFile ("RVC")
                        .getChildFile ("hubert").getChildFile ("vec-768-layer-12.onnx"));
    candidates.add (appDir.getChildFile ("Models").getChildFile ("RVC")
                        .getChildFile ("hubert").getChildFile ("vec-768-layer-12.onnx"));
    candidates.add (juce::File ("D:/Dev/rvc-models/hubert/vec-768-layer-12.onnx"));

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;

    return candidates.getFirst();
}

juce::File RvcEngine::getVoicesDir()
{
    const auto portable = btppaths::getModelsRoot().getChildFile ("RVC").getChildFile ("voices");
    const auto legacy   = juce::File ("D:/Dev/rvc-models/voices");

    auto hasVoices = [] (const juce::File& dir)
    {
        return dir.isDirectory()
            && ! dir.findChildFiles (juce::File::findFiles, false, "*.onnx").isEmpty();
    };

    if (hasVoices (portable)) return portable;
    if (hasVoices (legacy))   return legacy;

    return portable;
}

void RvcEngine::loadFromConfig()
{
    Models m;
    m.contentVec = getDefaultContentVec();

    const juce::File cfg = getConfigFile();

    if (cfg.existsAsFile())
    {
        const auto json = juce::JSON::parse (cfg);

        if (auto* obj = json.getDynamicObject())
        {
            const auto cv = obj->getProperty ("content_vec").toString();
            const auto vc = obj->getProperty ("voice").toString();

            if (cv.isNotEmpty() && juce::File (cv).existsAsFile())
                m.contentVec = juce::File (cv);

            if (vc.isNotEmpty() && juce::File (vc).existsAsFile())
                m.voice = juce::File (vc);
        }
    }

    // Fall back to the first available voice.
    if (! m.voice.existsAsFile())
    {
        const auto dir = getVoicesDir();

        if (dir.isDirectory())
        {
            const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.onnx");
            if (! files.isEmpty())
                m.voice = files.getFirst();
        }
    }

    models = m;

    setState (State::Unloaded, m.isComplete() ? "RVC ENGINE IDLE" : "RVC MODELS MISSING");
}

void RvcEngine::saveToConfig() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("content_vec", models.contentVec.getFullPathName());
    obj->setProperty ("voice", models.voice.getFullPathName());
    getConfigFile().getParentDirectory().createDirectory();
    getConfigFile().replaceWithText (juce::JSON::toString (juce::var (obj), true));
}

RvcEngine::Models RvcEngine::getModels() const
{
    const juce::ScopedLock sl (modelLock);
    return models;
}

void RvcEngine::setModels (const Models& newModels)
{
    {
        const juce::ScopedLock sl (modelLock);
        models = newModels;
    }

    saveToConfig();
    loadRequested.store (true);
    notify();
}

void RvcEngine::setVoice (const juce::File& voiceFile)
{
    auto m = getModels();
    m.voice = voiceFile;
    setModels (m);
}

//==============================================================================
RvcEngine::State RvcEngine::getState() const { return state.load(); }

juce::String RvcEngine::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

void RvcEngine::setState (State s, const juce::String& text)
{
    {
        const juce::ScopedLock sl (statusLock);
        statusText = text;
    }
    state.store (s);
}

//==============================================================================
void RvcEngine::convert (const juce::AudioBuffer<float>& input, double sourceSampleRate)
{
    if (input.getNumSamples() < 1024)
    {
        setState (State::Error, "INPUT TOO SHORT TO CONVERT");
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

bool RvcEngine::takeResult (Result& out)
{
    const juce::ScopedLock sl (resultLock);

    if (! pendingResult.valid)
        return false;

    out = pendingResult;
    pendingResult = Result();
    return true;
}

//==============================================================================
void RvcEngine::run()
{
   #if BTP_HAS_RVC
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
            if (voiceSession == nullptr || contentVecSession == nullptr)
                doLoad();

            if (voiceSession != nullptr && contentVecSession != nullptr)
                doConvert (job.audio, job.sampleRate);
            else
                setState (State::Error, "RVC MODELS NOT LOADED");

            continue;
        }

        wait (100);
    }

    // free onnx sessions
    if (voiceSession != nullptr)      { delete (Ort::Session*) voiceSession; voiceSession = nullptr; }
    if (contentVecSession != nullptr) { delete (Ort::Session*) contentVecSession; contentVecSession = nullptr; }
    if (envHandle != nullptr)         { delete (Ort::Env*) envHandle; envHandle = nullptr; }
   #else
    while (! threadShouldExit())
        wait (200);
   #endif
}

//==============================================================================
#if BTP_HAS_RVC

static std::wstring tow (const juce::String& s)
{
    std::wstring w;
    for (int i = 0; i < s.length(); ++i)
        w += (wchar_t) (unsigned short) s[i];
    return w;
}

void RvcEngine::doLoad()
{
    Models m;
    {
        const juce::ScopedLock sl (modelLock);
        m = models;
    }

    if (! m.isComplete())
    {
        setState (State::Error, "RVC MODELS MISSING");
        return;
    }

    setState (State::Loading, "LOADING RVC MODELS");

    try
    {
        if (envHandle == nullptr)
            envHandle = new Ort::Env (ORT_LOGGING_LEVEL_WARNING, "rvc");

        auto* env = (Ort::Env*) envHandle;

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (4);
        opts.SetGraphOptimizationLevel (ORT_DISABLE_ALL);

        const bool needCV = (contentVecSession == nullptr || loadedContentVec != m.contentVec.getFullPathName());
        const bool needV  = (voiceSession == nullptr || loadedVoice != m.voice.getFullPathName());

        if (needCV)
        {
            if (contentVecSession != nullptr) delete (Ort::Session*) contentVecSession;
            contentVecSession = new Ort::Session (*env, tow (m.contentVec.getFullPathName()).c_str(), opts);
            loadedContentVec = m.contentVec.getFullPathName();
        }

        if (needV)
        {
            if (voiceSession != nullptr) delete (Ort::Session*) voiceSession;
            voiceSession = new Ort::Session (*env, tow (m.voice.getFullPathName()).c_str(), opts);
            loadedVoice = m.voice.getFullPathName();
        }
    }
    catch (const Ort::Exception& e)
    {
        setState (State::Error, juce::String ("RVC LOAD FAILED: ") + e.what());
        return;
    }

    setState (State::Ready, "RVC ENGINE READY");
}

void RvcEngine::doConvert (const juce::AudioBuffer<float>& input, double sourceSampleRate)
{
    auto* env = (Ort::Env*) envHandle;
    auto* cv  = (Ort::Session*) contentVecSession;
    auto* gen = (Ort::Session*) voiceSession;

    setState (State::Converting, "CONVERTING VOICE");

    auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    try
    {
        // 1. ContentVec features (16 kHz mono -> [1, T', 768])
        std::vector<float> wav16k;
        resampleMono (input, sourceSampleRate, kContentVecRate, wav16k);

        std::vector<float> features;
        int Tprime = 0;
        {
            const int64_t shape[3] = { 1, 1, (int64_t) wav16k.size() };
            auto inVal = Ort::Value::CreateTensor<float> (mem, wav16k.data(), wav16k.size(), shape, 3);

            const char* inNames[1] = { "source" };
            const char* outNames[1] = { "embed" };

            Ort::Value inputArr[1] = { std::move (inVal) };
            auto outputs = cv->Run (Ort::RunOptions { nullptr }, inNames, inputArr, 1, outNames, 1);

            auto outInfo = outputs[0].GetTensorTypeAndShapeInfo();
            auto outShape = outInfo.GetShape();           // [1, T', 768]
            const float* data = outputs[0].GetTensorData<float>();

            for (auto d : outShape) fprintf (stderr, " %lld", (long long) d);
            fprintf (stderr, "\n");

            Tprime = (int) outShape[1];
            const int dim = 768;
            features.assign ((size_t) 2 * Tprime * dim, 0.0f);

            // outputs[0] is [1, T', 768]; copy + repeat each frame x2.
            for (int t = 0; t < Tprime; ++t)
            {
                for (int d = 0; d < dim; ++d)
                {
                    const float v = data[(size_t) t * dim + d];
                    features[(size_t) (2 * t)     * dim + d] = v;
                    features[(size_t) (2 * t + 1) * dim + d] = v;
                }
            }
        }

        const int len = 2 * Tprime;   // hubert_length

        // 2. f0 on 40 kHz, interpolated to len
        std::vector<float> wav40k;
        resampleMono (input, sourceSampleRate, kGenRate, wav40k);

        std::vector<float> f0Frames;
        computeF0 (wav40k, kGenHop, f0Frames);

        std::vector<float> pitchf;
        interpolateToLength (f0Frames, len, pitchf);

        std::vector<int64_t> pitch;
        quantizePitch (pitchf, pitch);

        // 3. rnd noise [1, 192, len]
        std::vector<float> rnd ((size_t) 192 * len);
        {
            std::mt19937 gen (std::random_device {} ());
            std::normal_distribution<float> dist (0.0f, 1.0f);
            for (auto& v : rnd) v = dist (gen);
        }

        // 4. generator
        const int64_t phoneShape[3]  = { 1, (int64_t) len, 768 };
        const int64_t lenShape[1]    = { 1 };
        const int64_t pitchShape[2]  = { 1, (int64_t) len };
        const int64_t dsShape[1]     = { 1 };
        const int64_t rndShape[3]    = { 1, 192, (int64_t) len };

        int64_t dsVal[1] = { 0 };
        int64_t lenVal[1] = { (int64_t) len };

        Ort::Value phone  = Ort::Value::CreateTensor<float> (mem, features.data(), features.size(), phoneShape, 3);
        Ort::Value plen   = Ort::Value::CreateTensor<int64_t> (mem, lenVal, 1, lenShape, 1);
        Ort::Value pitchv = Ort::Value::CreateTensor<int64_t> (mem, pitch.data(), pitch.size(), pitchShape, 2);
        Ort::Value pitchfv= Ort::Value::CreateTensor<float> (mem, pitchf.data(), pitchf.size(), pitchShape, 2);
        Ort::Value dsv    = Ort::Value::CreateTensor<int64_t> (mem, dsVal, 1, dsShape, 1);
        Ort::Value rndv   = Ort::Value::CreateTensor<float> (mem, rnd.data(), rnd.size(), rndShape, 3);

        Ort::Value inputs[6] = { std::move (phone), std::move (plen), std::move (pitchv),
                                 std::move (pitchfv), std::move (dsv), std::move (rndv) };
        const char* inNames[6] = { "phone", "phone_lengths", "pitch", "pitchf", "ds", "rnd" };
        const char* outNames[1] = { "audio" };

        auto outputs = gen->Run (Ort::RunOptions { nullptr }, inNames, inputs, 6, outNames, 1);

        auto outInfo = outputs[0].GetTensorTypeAndShapeInfo();
        auto outShape = outInfo.GetShape();              // [1, 1, N]
        const float* outData = outputs[0].GetTensorData<float>();
        const int N = (int) outShape[2];

        // 5. resample 40k -> host, mono -> stereo, trim to original length
        const int orgLen = input.getNumSamples();

        Result result;
        result.audio.setSize (2, orgLen, false, true, false);

        const double ratio = (double) kGenRate / sourceSampleRate;

        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = result.audio.getWritePointer (ch);

            for (int i = 0; i < orgLen; ++i)
            {
                const double pos = (double) i * ratio;
                const int i0 = juce::jmin (N - 1, (int) pos);
                const int i1 = juce::jmin (N - 1, i0 + 1);
                const float frac = (float) (pos - (double) i0);
                dst[i] = outData[i0] + (outData[i1] - outData[i0]) * frac;
            }
        }

        result.seconds = (double) orgLen / sourceSampleRate;
        result.valid = true;

        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
        }

        setState (State::Ready, "VOICE CONVERTED - " + juce::String (result.seconds, 1) + " s");
    }
    catch (const Ort::Exception& e)
    {
        setState (State::Error, juce::String ("RVC CONVERT FAILED: ") + e.what());
    }
}

#else

void RvcEngine::doLoad() { setState (State::Error, "RVC DISABLED"); }
void RvcEngine::doConvert (const juce::AudioBuffer<float>&, double) { setState (State::Error, "RVC DISABLED"); }

#endif
