#include "AiEngine.h"
#include "ModelPaths.h"

#if BTP_HAS_AI
 #include "pipeline-synth.h"
 #include "pipeline-lm.h"
 #include "request.h"
#endif

//==============================================================================
namespace
{
    juce::String modelSignature (const AiEngine::Models& m)
    {
        juce::String s;
        auto add = [&s] (const juce::File& f)
        {
            s << (f.existsAsFile() ? f.getFullPathName() : juce::String ("-"))
              << "|" << (f.existsAsFile() ? f.getSize() : 0) << "|";
        };

        add (m.dit);
        add (m.textEncoder);
        add (m.vae);
        add (m.lm);
        add (m.lora);
        s << m.loraScale;

        return s;
    }

    juce::String stateName (AiEngine::State s)
    {
        switch (s)
        {
            case AiEngine::State::Unloaded:   return "UNLOADED";
            case AiEngine::State::Loading:    return "LOADING";
            case AiEngine::State::Ready:      return "READY";
            case AiEngine::State::Generating: return "COOKING";
            case AiEngine::State::Error:      return "ERROR";
        }

        return "UNKNOWN";
    }
}

//==============================================================================
bool AiEngine::Models::isComplete() const
{
    return dit.existsAsFile() && textEncoder.existsAsFile() && vae.existsAsFile();
}

bool AiEngine::Models::operator== (const Models& o) const
{
    return dit == o.dit
        && textEncoder == o.textEncoder
        && vae == o.vae
        && lm == o.lm
        && lora == o.lora
        && std::abs (loraScale - o.loraScale) < 1.0e-4f;
}

//==============================================================================
AiEngine& AiEngine::getInstance()
{
    static AiEngine engine;
    return engine;
}

bool AiEngine::isCompiledIn()
{
   #if BTP_HAS_AI
    return true;
   #else
    return false;
   #endif
}

//==============================================================================
AiEngine::AiEngine()
    : juce::Thread ("BtP ACE Engine")
{
    loadFromConfig();
    startThread (juce::Thread::Priority::low);
}

AiEngine::~AiEngine()
{
    shutdown();
}

void AiEngine::shutdown()
{
    quitFlag.store (true);
    cancelFlag.store (true);

    if (isThreadRunning())
        stopThread (3000);

    // The worker frees the models on the way out.
    setState (State::Unloaded, "ENGINE OFF");
}

//==============================================================================
// Config
//==============================================================================
juce::File AiEngine::getConfigFile()
{
    return btpai::getConfigDir().getChildFile ("ai_settings.json");
}

void AiEngine::loadFromConfig()
{
    Models m;

    const juce::File cfg = getConfigFile();

    if (cfg.existsAsFile())
    {
        const auto json = juce::JSON::parse (cfg);

        if (auto* obj = json.getDynamicObject())
        {
            auto readFile = [obj] (const juce::Identifier& key) -> juce::File
            {
                const auto v = obj->getProperty (key).toString();
                return v.isNotEmpty() ? juce::File (v) : juce::File();
            };

            m.dit         = readFile ("dit");
            m.textEncoder = readFile ("text_encoder");
            m.vae         = readFile ("vae");
            m.lm          = readFile ("lm");
            m.lora        = readFile ("lora");
            m.loraScale   = (float) (double) obj->getProperty ("lora_scale");
        }
    }

    if (! m.isComplete())
        m = defaultModels();

    models = m;

    setState (State::Unloaded, m.isComplete() ? "ENGINE IDLE" : "MODELS NOT FOUND");
}

void AiEngine::saveToConfig() const
{
    auto* obj = new juce::DynamicObject();

    obj->setProperty ("dit", models.dit.getFullPathName());
    obj->setProperty ("text_encoder", models.textEncoder.getFullPathName());
    obj->setProperty ("vae", models.vae.getFullPathName());
    obj->setProperty ("lm", models.lm.getFullPathName());
    obj->setProperty ("lora", models.lora.getFullPathName());
    obj->setProperty ("lora_scale", models.loraScale);

    const juce::var json (obj);

    getConfigFile().getParentDirectory().createDirectory();
    getConfigFile().replaceWithText (juce::JSON::toString (json, true));
}

juce::File AiEngine::getDefaultModelDir()
{
    juce::Array<juce::File> candidates;

    const auto appDir = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                            .getParentDirectory();

    // Portable layouts first (what a release package uses), legacy dev paths last.
    candidates.add (btpai::getModelsRoot().getChildFile ("ACE-Step"));
    candidates.add (appDir.getChildFile ("Models").getChildFile ("ACE-Step"));
    candidates.add (appDir.getChildFile ("models"));
    candidates.add (btpai::getModelsRoot());
    candidates.add (juce::File ("D:/Dev/ACE-Step-1.5/acestep.vst3/models"));

    auto hasDit = [] (const juce::File& dir)
    {
        return dir.isDirectory()
            && ! dir.findChildFiles (juce::File::findFiles, false, "acestep-v15-turbo*.gguf").isEmpty();
    };

    // Prefer a folder that actually holds the diffusion model. The installer
    // creates empty placeholder folders, which must not shadow a real install.
    for (const auto& dir : candidates)
        if (hasDit (dir))
            return dir;

    for (const auto& dir : candidates)
        if (dir.isDirectory()
            && ! dir.findChildFiles (juce::File::findFiles, false, "*.gguf").isEmpty())
            return dir;

    for (const auto& dir : candidates)
        if (dir.isDirectory())
            return dir;

    return candidates.getFirst();
}

AiEngine::Models AiEngine::defaultModels() const
{
    Models m;
    const auto dir = getDefaultModelDir();

    if (! dir.isDirectory())
        return m;

    auto pick = [&dir] (const juce::String& pattern) -> juce::File
    {
        const auto files = dir.findChildFiles (juce::File::findFiles, false, pattern);
        return files.isEmpty() ? juce::File() : files.getFirst();
    };

    m.dit         = pick ("acestep-v15-turbo*.gguf");
    m.textEncoder = pick ("Qwen3-Embedding*.gguf");
    m.vae         = pick ("vae-*.gguf");
    m.lm          = pick ("acestep-5Hz-lm*.gguf");

    return m;
}

AiEngine::Models AiEngine::getModels() const
{
    const juce::ScopedLock sl (modelLock);
    return models;
}

void AiEngine::setModels (const Models& newModels)
{
    {
        const juce::ScopedLock sl (modelLock);
        models = newModels;
    }

    saveToConfig();
    unload();
}

void AiEngine::unload()
{
    unloadRequested.store (true);
    setState (State::Unloaded, "ENGINE IDLE - RELOADS ON NEXT RUN");
}

//==============================================================================
AiEngine::State AiEngine::getState() const { return state.load(); }

juce::String AiEngine::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

juce::String AiEngine::getLastError() const
{
    const juce::ScopedLock sl (statusLock);
    return lastError;
}

double AiEngine::getElapsedSeconds() const
{
    if (state.load() != State::Generating)
        return 0.0;

    const double t0 = startedAt.load();
    return t0 > 0.0 ? juce::Time::getMillisecondCounterHiRes() * 0.001 - t0 : 0.0;
}

bool AiEngine::isBusy() const
{
    const auto s = state.load();
    return s == State::Loading || s == State::Generating;
}

void AiEngine::setState (State newState, const juce::String& text, const juce::String& error)
{
    {
        const juce::ScopedLock sl (statusLock);
        statusText = text;
        lastError = error;
    }

    state.store (newState);
}

//==============================================================================
void AiEngine::post (std::function<void()> fn)
{
    juce::MessageManager::callAsync (std::move (fn));
}

void AiEngine::startLoad()
{
    if (isBusy())
        return;

    loadRequested.store (true);
    setState (State::Loading, "LOADING WEIGHTS");
    notify();
}

void AiEngine::submit (const Job& job)
{
    if (isBusy())
        return;

    {
        const juce::ScopedLock sl (jobLock);
        pending.job = job;
        pending.valid = true;
    }

    cancelFlag.store (false);
    notify();
}

void AiEngine::cancel()
{
    cancelFlag.store (true);
}

//==============================================================================
bool AiEngine::takeResult (Result& out)
{
    const juce::ScopedLock sl (resultLock);

    if (! pendingResult.valid)
        return false;

    out = pendingResult;
    pendingResult = Result();
    return true;
}

//==============================================================================
void AiEngine::run()
{
   #if BTP_HAS_AI
    bool unloaded = true;

    while (! threadShouldExit())
    {
        if (quitFlag.load())
            break;

        if (unloadRequested.exchange (false))
        {
            freeModels();
            unloaded = true;
        }

        if (loadRequested.exchange (false))
        {
            doLoad();
            unloaded = false;
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
            if (unloaded || synth == nullptr)
            {
                doLoad();
                unloaded = false;
            }

            if (synth != nullptr)
                doGenerate (job.job);
            else
                setState (State::Error, "ENGINE NOT LOADED", lastError);

            continue;
        }

        wait (60);
    }

    freeModels();
   #else
    while (! threadShouldExit())
        wait (200);
   #endif
}

//==============================================================================
#if BTP_HAS_AI

void AiEngine::freeModels()
{
    if (synth != nullptr)
    {
        ace_synth_free (static_cast<AceSynth*> (synth));
        synth = nullptr;
    }

    if (lmCtx != nullptr)
    {
        ace_lm_free (static_cast<AceLm*> (lmCtx));
        lmCtx = nullptr;
    }

    loadedSignature.clear();
}

void AiEngine::doLoad()
{
    Models m;
    {
        const juce::ScopedLock sl (modelLock);
        m = models;
    }

    if (! m.isComplete())
    {
        setState (State::Error, "MODEL FILES MISSING",
                  "Point AI GEN at a folder holding acestep-v15-turbo, Qwen3-Embedding and vae GGUFs.");
        return;
    }

    const auto signature = modelSignature (m);

    if (synth != nullptr && signature == loadedSignature)
    {
        setState (State::Ready, "ENGINE READY");
        return;
    }

    freeModels();
    setState (State::Loading, "LOADING DiT + TEXT ENCODER + VAE");

    const auto started = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Keep the strings alive for the duration of ace_synth_load().
    const juce::String ditPath  = m.dit.getFullPathName();
    const juce::String encPath  = m.textEncoder.getFullPathName();
    const juce::String vaePath  = m.vae.getFullPathName();
    const juce::String loraPath = m.lora.getFullPathName();

    AceSynthParams params;
    ace_synth_default_params (&params);

    params.dit_path          = ditPath.toRawUTF8();
    params.text_encoder_path = encPath.toRawUTF8();
    params.vae_path          = vaePath.toRawUTF8();
    params.lora_path         = m.lora.existsAsFile() ? loraPath.toRawUTF8() : nullptr;
    params.lora_scale        = juce::jlimit (0.0f, 2.0f, m.loraScale);
    params.use_fa            = true;
    params.clamp_fp16        = false;

    synth = ace_synth_load (&params);

    if (synth == nullptr)
    {
        setState (State::Error, "ENGINE LOAD FAILED",
                  "ace_synth_load() returned null - check the GGUF files and the GPU backend.");
        return;
    }

    loadedSignature = signature;

    const auto secs = juce::Time::getMillisecondCounterHiRes() * 0.001 - started;

    setState (State::Ready, "ENGINE READY  /  LOADED IN " + juce::String (secs, 1) + " s");
}

void AiEngine::doGenerate (const Job& job)
{
    auto* ctx = static_cast<AceSynth*> (synth);

    setState (State::Generating, "COOKING: " + job.prompt.substring (0, 40));
    startedAt.store (juce::Time::getMillisecondCounterHiRes() * 0.001);

    AceRequest req;
    request_init (&req);

    const auto caption = job.prompt.toStdString();
    const auto lyrics  = job.lyrics.toStdString();
    const auto key     = job.keyscale.toStdString();

    req.caption        = caption;
    req.lyrics         = lyrics;
    req.bpm            = job.bpm;
    req.duration       = juce::jmax (3.0f, job.duration);
    req.keyscale       = key;
    req.timesignature  = "4/4";
    req.batch_size     = 1;
    req.seed           = job.seed >= 0 ? job.seed : -1;
    req.inference_steps = juce::jmax (1, job.steps);
    req.guidance_scale  = job.guidance;
    req.shift           = job.shift;
    req.use_cot_caption = false;

    // Vocals: enrich the request (lyrics + vocal audio codes) via the 4B LM.
    if (job.withVocals && ! caption.empty())
    {
        juce::File lmPath;
        {
            const juce::ScopedLock sl (modelLock);
            lmPath = models.lm;
        }

        if (lmCtx == nullptr && lmPath.existsAsFile())
        {
            setState (State::Loading, "LOADING VOCALS LM (4B)");

            AceLmParams lp;
            ace_lm_default_params (&lp);

            const juce::String lpStr = lmPath.getFullPathName();
            lp.model_path = lpStr.toRawUTF8();
            lp.use_fa = true;

            lmCtx = ace_lm_load (&lp);
        }

        if (lmCtx != nullptr)
        {
            req.vocal_language = "en";
            setState (State::Generating, "WRITING LYRICS + VOCALS: " + job.prompt.substring (0, 40));

            const int lmRc = ace_lm_generate (static_cast<AceLm*> (lmCtx), &req, 1, &req,
                                              nullptr, nullptr,
                                              [] (void* data) -> bool
                                              {
                                                  return static_cast<AiEngine*> (data)->cancelFlag.load();
                                              },
                                              this);

            if (lmRc != 0)
            {
                if (cancelFlag.load())
                    setState (State::Ready, "GENERATION CANCELLED");
                else
                    setState (State::Error, "LYRICS GENERATION FAILED", "ace_lm_generate() returned an error.");

                return;
            }
        }
        else
        {
            setState (State::Generating, "VOCALS LM MISSING - INSTRUMENTAL");
        }
    }

    AceAudio audio {};
    audio.samples = nullptr;
    audio.n_samples = 0;
    audio.sample_rate = 48000;

    const int rc = ace_synth_generate (ctx, &req, nullptr, 0, 1, &audio,
                                       [] (void* data) -> bool
                                       {
                                           return static_cast<AiEngine*> (data)->cancelFlag.load();
                                       },
                                       this);

    if (rc != 0 || audio.samples == nullptr || audio.n_samples <= 0)
    {
        if (audio.samples != nullptr)
            ace_audio_free (&audio);

        if (cancelFlag.load())
            setState (State::Ready, "GENERATION CANCELLED");
        else
            setState (State::Error, "GENERATION FAILED", "ace_synth_generate() returned an error.");

        return;
    }

    Result result;
    result.prompt  = job.prompt;
    result.lyrics  = juce::String (req.lyrics.c_str());
    result.seed    = req.seed;
    result.kind    = job.kind;
    result.seconds = (double) audio.n_samples / (double) audio.sample_rate;
    result.audio.setSize (2, audio.n_samples);
    result.audio.copyFrom (0, 0, audio.samples, audio.n_samples);
    result.audio.copyFrom (1, 0, audio.samples + audio.n_samples, audio.n_samples);
    result.valid = true;

    ace_audio_free (&audio);

    {
        const juce::ScopedLock sl (resultLock);
        pendingResult = std::move (result);
    }

    setState (State::Ready, "READY  /  LAST TAKE " + juce::String (pendingResult.seconds, 1) + " s");
}

#else

void AiEngine::freeModels() {}
void AiEngine::doLoad()
{
    setState (State::Error, "AI DISABLED", "This build was compiled without the ACE-Step engine.");
}
void AiEngine::doGenerate (const Job&)
{
    setState (State::Error, "AI DISABLED", "This build was compiled without the ACE-Step engine.");
}

#endif

//==============================================================================
namespace btpai
{

juce::File getConfigDir()
{
    return btppaths::getConfigDir();
}

juce::File getDatasetsDir() { return getConfigDir().getChildFile ("Datasets"); }
juce::File getLoraDir()     { return getConfigDir().getChildFile ("LoRAs"); }
juce::File getModelsRoot()  { return btppaths::getModelsRoot(); }

bool isSupportedAudio (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff") || path.endsWithIgnoreCase (".flac")
        || path.endsWithIgnoreCase (".mp3") || path.endsWithIgnoreCase (".ogg");
}

juce::String DatasetInfo::getSummary() const
{
    if (numFiles <= 0)
        return "EMPTY";

    return juce::String (numFiles) + " FILES  /  "
         + juce::String (seconds / 60.0, 1) + " MIN  /  "
         + juce::String (bytes / (1024.0 * 1024.0), 1) + " MB";
}

DatasetInfo scanDataset (const juce::File& folder)
{
    DatasetInfo info;
    info.name = folder.getFileName();
    info.folder = folder;

    if (! folder.isDirectory())
        return info;

    static juce::AudioFormatManager formats;
    static bool registered = false;

    if (! registered)
    {
        formats.registerBasicFormats();
        registered = true;
    }

    const auto files = folder.findChildFiles (juce::File::findFiles, true, "*");

    for (const auto& f : files)
    {
        if (! isSupportedAudio (f.getFullPathName()))
            continue;

        info.numFiles++;
        info.bytes += f.getSize();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

        if (reader != nullptr && reader->sampleRate > 0.0)
            info.seconds += (double) reader->lengthInSamples / reader->sampleRate;
    }

    return info;
}

juce::Array<DatasetInfo> scanDatasets()
{
    juce::Array<DatasetInfo> out;
    const auto root = getDatasetsDir();

    if (! root.isDirectory())
        return out;

    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false))
        out.add (scanDataset (dir));

    return out;
}

juce::File createDataset (const juce::String& name)
{
    auto safe = name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");

    if (safe.trim().isEmpty())
        safe = "DATASET " + juce::String (juce::Time::getCurrentTime().toMilliseconds());

    auto folder = getDatasetsDir().getChildFile (safe.trim());
    folder.createDirectory();
    return folder;
}

int importFilesIntoDataset (const juce::File& datasetFolder, const juce::StringArray& files)
{
    if (! datasetFolder.isDirectory())
        datasetFolder.createDirectory();

    int copied = 0;

    for (const auto& path : files)
    {
        const juce::File src (path);

        if (! src.existsAsFile() || ! isSupportedAudio (path))
            continue;

        auto dest = datasetFolder.getChildFile (src.getFileName());

        if (dest.existsAsFile())
            dest = datasetFolder.getChildFile (src.getFileNameWithoutExtension()
                                               + "_" + juce::String (juce::Random::getSystemRandom().nextInt (9999))
                                               + src.getFileExtension());

        if (src.copyFileTo (dest))
            copied++;
    }

    return copied;
}

juce::StringArray findLoras()
{
    juce::StringArray out;
    juce::Array<juce::File> roots { getLoraDir(), AiEngine::getDefaultModelDir() };

    for (const auto& root : roots)
    {
        if (! root.isDirectory())
            continue;

        for (const auto& f : root.findChildFiles (juce::File::findFiles, false, "*.safetensors"))
            if (! out.contains (f.getFullPathName()))
                out.add (f.getFullPathName());
    }

    out.sort (true);
    return out;
}

juce::File getTrainingDir()
{
    return btppaths::getTrainingDir();
}

juce::File saveTrainingCapture (const juce::AudioBuffer<float>& audio, double sampleRate,
                                const juce::String& kind, const juce::String& sub,
                                const juce::String& baseName)
{
    juce::File dir = getTrainingDir().getChildFile (kind);

    if (sub.isNotEmpty())
        dir = dir.getChildFile (sub);

    dir.createDirectory();

    auto slug = [] (const juce::String& t)
    {
        auto s = t.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ")
                    .trim().replace (" ", "_");
        if (s.length() > 30) s = s.substring (0, 30);
        return s.isEmpty() ? juce::String ("capture") : s;
    };

    const juce::String stamp = juce::String (juce::Time::getCurrentTime().toMilliseconds());
    const juce::String name = (baseName.isNotEmpty() ? slug (baseName) + "_" : juce::String()) + stamp + ".wav";

    juce::File file = dir.getChildFile (name);

    std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());

    if (out == nullptr)
        return juce::File();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), sampleRate > 0.0 ? sampleRate : 48000.0,
                             (unsigned int) juce::jmax (1, audio.getNumChannels()), 16, {}, 0));

    if (writer != nullptr)
    {
        out.release();   // the writer owns the stream now
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        writer->flush();
    }

    return file;
}

} // namespace btpai
