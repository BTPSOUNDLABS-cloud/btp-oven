#include "RaveEngine.h"
#include "ModelPaths.h"   // btppaths::getModelsRoot()

#if BTP_HAS_RAVE
 #include "onnxruntime_cxx_api.h"
#endif

#include <random>

namespace
{
    constexpr int kRate = 48000;

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
}

//==============================================================================
RaveEngine& RaveEngine::getInstance()
{
    static RaveEngine engine;
    return engine;
}

bool RaveEngine::isCompiledIn()
{
   #if BTP_HAS_RAVE
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
RaveEngine::RaveEngine()
    : juce::Thread ("BtP RAVE Engine")
{
    startThread (juce::Thread::Priority::low);
}

RaveEngine::~RaveEngine()
{
    shutdown();
}

void RaveEngine::shutdown()
{
    quitFlag.store (true);

    if (isThreadRunning())
        stopThread (2000);
}

juce::File RaveEngine::getModelDir()
{
    const auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                            .getParentDirectory();

    juce::Array<juce::File> candidates;
    candidates.add (btppaths::getModelsRoot().getChildFile ("RAVE"));
    candidates.add (appDir.getChildFile ("Models").getChildFile ("RAVE"));
    candidates.add (juce::File ("D:/Dev/rave-models"));

    auto hasModels = [] (const juce::File& dir)
    {
        return dir.isDirectory() && dir.getChildFile ("rave_encoder.onnx").existsAsFile();
    };

    for (const auto& dir : candidates)
        if (hasModels (dir))
            return dir;

    return candidates.getFirst();
}

//==============================================================================
RaveEngine::State RaveEngine::getState() const { return state.load(); }

juce::String RaveEngine::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

void RaveEngine::setState (State s, const juce::String& text)
{
    {
        const juce::ScopedLock sl (statusLock);
        statusText = text;
    }
    state.store (s);
}

//==============================================================================
void RaveEngine::mutate (const juce::AudioBuffer<float>& input, double sourceSampleRate,
                         float variation, juce::int64 seed)
{
    if (input.getNumSamples() < 1024)
    {
        setState (State::Error, "INPUT TOO SHORT TO MUTATE");
        return;
    }

    {
        const juce::ScopedLock sl (jobLock);
        pending.audio = input;
        pending.sampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
        pending.variation = juce::jlimit (0.0f, 2.0f, variation);
        pending.seed = seed;
        pending.valid = true;
    }

    notify();
}

bool RaveEngine::takeResult (Result& out)
{
    const juce::ScopedLock sl (resultLock);

    if (! pendingResult.valid)
        return false;

    out = pendingResult;
    pendingResult = Result();
    return true;
}

//==============================================================================
void RaveEngine::run()
{
   #if BTP_HAS_RAVE
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
            if (encoderSession == nullptr || decoderSession == nullptr)
                doLoad();

            if (encoderSession != nullptr && decoderSession != nullptr)
                doMutate (job.audio, job.sampleRate, job.variation, job.seed);
            else
                setState (State::Error, "RAVE MODELS NOT LOADED");

            continue;
        }

        wait (100);
    }

    if (encoderSession != nullptr) { delete (Ort::Session*) encoderSession; encoderSession = nullptr; }
    if (decoderSession != nullptr) { delete (Ort::Session*) decoderSession; decoderSession = nullptr; }
    if (envHandle != nullptr)      { delete (Ort::Env*) envHandle; envHandle = nullptr; }
   #else
    while (! threadShouldExit())
        wait (200);
   #endif
}

//==============================================================================
#if BTP_HAS_RAVE

static std::wstring tow (const juce::String& s)
{
    std::wstring w;
    for (int i = 0; i < s.length(); ++i)
        w += (wchar_t) (unsigned short) s[i];
    return w;
}

void RaveEngine::doLoad()
{
    const juce::File dir = getModelDir();

    const juce::File encFile = dir.getChildFile ("rave_encoder.onnx");
    const juce::File decFile = dir.getChildFile ("rave_decoder.onnx");
    const juce::File hkFile  = dir.getChildFile ("pqmf_hk.bin");

    if (! encFile.existsAsFile() || ! decFile.existsAsFile() || ! hkFile.existsAsFile())
    {
        setState (State::Error, "RAVE MODELS MISSING (rave_encoder.onnx / rave_decoder.onnx / pqmf_hk.bin)");
        return;
    }

    setState (State::Loading, "LOADING RAVE MODELS");

    // Load PQMF filter bank.
    {
        juce::FileInputStream in (hkFile);

        if (in.openedOk())
        {
            juce::int32 m = 0, len = 0;
            in.read (&m, sizeof (m));
            in.read (&len, sizeof (len));

            pqmfBands = m;
            pqmfFilterLen = len;

            hk.resize ((size_t) m * len);
            in.read (hk.data(), (int) hk.size() * (int) sizeof (float));
        }
    }

    if (pqmfBands <= 0 || pqmfFilterLen <= 0)
    {
        setState (State::Error, "RAVE PQMF FILTER MISSING");
        return;
    }

    try
    {
        if (envHandle == nullptr)
            envHandle = new Ort::Env (ORT_LOGGING_LEVEL_WARNING, "rave");

        auto* env = (Ort::Env*) envHandle;

        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads (4);
        opts.SetGraphOptimizationLevel (ORT_DISABLE_ALL);

        if (encoderSession != nullptr) delete (Ort::Session*) encoderSession;
        if (decoderSession != nullptr) delete (Ort::Session*) decoderSession;

        encoderSession = new Ort::Session (*env, tow (encFile.getFullPathName()).c_str(), opts);
        decoderSession = new Ort::Session (*env, tow (decFile.getFullPathName()).c_str(), opts);
    }
    catch (const Ort::Exception& e)
    {
        setState (State::Error, juce::String ("RAVE LOAD FAILED: ") + e.what());
        return;
    }

    setState (State::Ready, "RAVE ENGINE READY");
}

// PQMF analysis: mono [T] -> multiband [M, T/M]
static void pqmfAnalyze (const std::vector<float>& x, int M, int filterLen,
                         const std::vector<float>& hk, std::vector<float>& mb)
{
    const int T = (int) x.size();
    const int Tb = T / M;
    const int pad = filterLen / 2;

    mb.assign ((size_t) M * Tb, 0.0f);

    for (int m = 0; m < M; ++m)
    {
        const float* h = hk.data() + (size_t) m * filterLen;

        for (int t = 0; t < Tb; ++t)
        {
            const int base = t * M - pad;
            float acc = 0.0f;

            for (int k = 0; k < filterLen; ++k)
            {
                const int xi = base + k;
                if (xi >= 0 && xi < T)
                    acc += h[k] * x[(size_t) xi];
            }

            mb[(size_t) m * Tb + t] = acc;
        }
    }
}

// PQMF synthesis: multiband [M, Tb] -> mono [M*Tb]
static void pqmfSynthesize (const std::vector<float>& mb, int M, int filterLen,
                            const std::vector<float>& hk, std::vector<float>& out)
{
    const int Tb = (int) mb.size() / M;
    const int L = M * Tb;
    const int pad = filterLen / 2;

    out.assign ((size_t) L, 0.0f);

    // Build the upsampled per-band signals y[m][i] = mb[m][i/M]*M when i%M==0.
    // We convolve directly without materialising the full [M, L] buffer.
    for (int i = 0; i < L; ++i)
    {
        float acc = 0.0f;

        for (int m = 0; m < M; ++m)
        {
            const float* h = hk.data() + (size_t) m * filterLen;  // hk already flipped? no, flip here

            for (int k = 0; k < filterLen; ++k)
            {
                const int yi = i + k - pad + 1;   // +1 accounts for the [..., 1:] crop

                if (yi >= 0 && yi < L && (yi % M) == 0)
                {
                    const int mbIdx = yi / M;
                    const float sample = mb[(size_t) m * Tb + mbIdx] * M;

                    // hk_flip[m][k] = hk[m][filterLen-1-k]
                    acc += h[filterLen - 1 - k] * sample;
                }
            }
        }

        out[(size_t) i] = acc;
    }
}

void RaveEngine::doMutate (const juce::AudioBuffer<float>& input, double sourceSampleRate,
                           float variation, juce::int64 seed)
{
    auto* env = (Ort::Env*) envHandle;
    auto* enc = (Ort::Session*) encoderSession;
    auto* dec = (Ort::Session*) decoderSession;

    setState (State::Mutating, "MUTATING");

    auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    try
    {
        // 1. resample to 48k mono
        std::vector<float> wav48;
        resampleMono (input, sourceSampleRate, kRate, wav48);

        // 2. PQMF analysis
        std::vector<float> mb;
        pqmfAnalyze (wav48, pqmfBands, pqmfFilterLen, hk, mb);
        const int Tb = (int) mb.size() / pqmfBands;

        // 3. encoder -> latent mu
        std::vector<float> latent;
        int latentFrames = 0;
        {
            const int64_t shape[3] = { 1, pqmfBands, Tb };
            auto inVal = Ort::Value::CreateTensor<float> (mem, mb.data(), mb.size(), shape, 3);

            const char* inNames[1] = { "multiband" };
            const char* outNames[1] = { "latent" };
            Ort::Value inputs[1] = { std::move (inVal) };
            auto outputs = enc->Run (Ort::RunOptions { nullptr }, inNames, inputs, 1, outNames, 1);

            auto info = outputs[0].GetTensorTypeAndShapeInfo();
            auto s = info.GetShape();          // [1, 128, T']
            const float* d = outputs[0].GetTensorData<float>();

            latentFrames = (int) s[2];
            latent.assign (d, d + outputs[0].GetTensorTypeAndShapeInfo().GetElementCount());
        }

        // 4. perturb latent with gaussian noise * variation
        {
            std::mt19937 gen ((uint32_t) (seed ^ 0x5eed5eed));
            std::normal_distribution<float> dist (0.0f, 1.0f);

            // estimate latent scale (robust std) for proportional noise
            float mean = 0.0f;
            for (float v : latent) mean += v;
            mean /= (float) latent.size();

            float var = 0.0f;
            for (float v : latent) var += (v - mean) * (v - mean);
            const float stdv = std::sqrt (var / (float) latent.size());

            const float noiseScale = variation * juce::jmax (0.05f, stdv);

            for (float& v : latent)
                v += dist (gen) * noiseScale;
        }

        // 5. decoder -> multiband
        std::vector<float> mb2;
        int Tb2 = 0;
        {
            const int64_t shape[3] = { 1, latentSize, latentFrames };
            auto inVal = Ort::Value::CreateTensor<float> (mem, latent.data(), latent.size(), shape, 3);

            const char* inNames[1] = { "latent" };
            const char* outNames[1] = { "multiband" };
            Ort::Value inputs[1] = { std::move (inVal) };
            auto outputs = dec->Run (Ort::RunOptions { nullptr }, inNames, inputs, 1, outNames, 1);

            auto info = outputs[0].GetTensorTypeAndShapeInfo();
            auto s = info.GetShape();          // [1, M, Tb2]
            const float* d = outputs[0].GetTensorData<float>();

            Tb2 = (int) s[2];
            mb2.assign (d, d + outputs[0].GetTensorTypeAndShapeInfo().GetElementCount());
        }

        // 6. PQMF synthesis
        std::vector<float> out48;
        pqmfSynthesize (mb2, pqmfBands, pqmfFilterLen, hk, out48);

        // Peak-normalise to avoid clipping (RAVE reconstruction can exceed 1.0).
        {
            float peak = 0.0f;
            for (float v : out48) peak = juce::jmax (peak, std::abs (v));

            if (peak > 0.99f)
            {
                const float g = 0.99f / peak;
                for (float& v : out48) v *= g;
            }
        }

        // 7. resample back to host rate + trim to original length
        const int orgLen = input.getNumSamples();
        Result result;
        result.audio.setSize (2, orgLen, false, true, false);

        const double ratio = (double) kRate / sourceSampleRate;

        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = result.audio.getWritePointer (ch);

            for (int i = 0; i < orgLen; ++i)
            {
                const double pos = (double) i * ratio;
                const int i0 = juce::jmin ((int) out48.size() - 1, (int) pos);
                const int i1 = juce::jmin ((int) out48.size() - 1, i0 + 1);
                const float frac = (float) (pos - (double) i0);
                dst[i] = out48[(size_t) i0] + (out48[(size_t) i1] - out48[(size_t) i0]) * frac;
            }
        }

        result.seconds = (double) orgLen / sourceSampleRate;
        result.valid = true;

        {
            const juce::ScopedLock sl (resultLock);
            pendingResult = std::move (result);
        }

        setState (State::Ready, "VARIANT READY - " + juce::String (result.seconds, 1) + " s");
    }
    catch (const Ort::Exception& e)
    {
        setState (State::Error, juce::String ("RAVE MUTATE FAILED: ") + e.what());
    }
}

#else

void RaveEngine::doLoad() { setState (State::Error, "RAVE DISABLED"); }
void RaveEngine::doMutate (const juce::AudioBuffer<float>&, double, float, juce::int64)
{
    setState (State::Error, "RAVE DISABLED");
}

#endif
