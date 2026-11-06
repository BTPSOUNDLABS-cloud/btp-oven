// Vocal smoke test: exercises the full ACE-Step lyrics+vocals flow that the
// plugin's "VOCALS" mode uses — 4B LM writes lyrics + vocal codes, then the
// synth renders audio with vocals.
#include "pipeline-synth.h"
#include "pipeline-lm.h"
#include "request.h"

#include <cstdio>

static const char* kDir = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/";

int main()
{
    fprintf (stderr, "[vocals] loading LM (4B)...\n");

    AceLmParams lp;
    ace_lm_default_params (&lp);
    lp.model_path = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/acestep-5Hz-lm-4B-Q8_0.gguf";
    lp.use_fa = true;

    AceLm* lm = ace_lm_load (&lp);
    if (lm == nullptr) { fprintf (stderr, "[vocals] LM LOAD FAILED\n"); return 1; }
    fprintf (stderr, "[vocals] LM loaded\n");

    AceRequest req;
    request_init (&req);
    req.caption        = "smooth rnb love song, male vocals, trap drums, dreamy keys";
    req.duration       = 8.0f;
    req.bpm            = 140;
    req.keyscale       = "A minor";
    req.vocal_language = "en";
    req.seed           = 42;
    req.batch_size     = 1;

    if (ace_lm_generate (lm, &req, 1, &req, nullptr, nullptr, nullptr, nullptr) != 0)
    {
        fprintf (stderr, "[vocals] LM GENERATE FAILED\n");
        return 2;
    }

    fprintf (stderr, "[vocals] LYRICS:\n%s\n", req.lyrics.c_str());
    fprintf (stderr, "[vocals] codes=%zu bpm=%d key=%s dur=%.1fs\n",
             req.audio_codes.size(), req.bpm, req.keyscale.c_str(), req.duration);

    fprintf (stderr, "[vocals] loading synth (DiT + VAE)...\n");

    AceSynthParams sp;
    ace_synth_default_params (&sp);
    sp.dit_path          = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/acestep-v15-turbo-Q8_0.gguf";
    sp.text_encoder_path = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/Qwen3-Embedding-0.6B-Q8_0.gguf";
    sp.vae_path          = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/vae-BF16.gguf";
    sp.use_fa            = true;

    AceSynth* synth = ace_synth_load (&sp);
    if (synth == nullptr) { fprintf (stderr, "[vocals] SYNTH LOAD FAILED\n"); return 3; }

    AceAudio out {};
    if (ace_synth_generate (synth, &req, nullptr, 0, 1, &out, nullptr, nullptr) != 0
        || out.samples == nullptr || out.n_samples <= 0)
    {
        fprintf (stderr, "[vocals] SYNTH GENERATE FAILED\n");
        return 4;
    }

    fprintf (stderr, "[vocals] audio n=%d rate=%d (%.2fs)\n",
             out.n_samples, out.sample_rate, (double) out.n_samples / out.sample_rate);

    ace_audio_free (&out);
    ace_synth_free (synth);
    ace_lm_free (lm);

    fprintf (stderr, "[vocals] PASS\n");
    return 0;
}
