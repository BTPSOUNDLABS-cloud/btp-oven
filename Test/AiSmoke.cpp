// AI smoke test: loads the ACE-Step models through the same statically-linked
// ggml + acestep-core used by the plugin, and generates a short take.
#include "pipeline-synth.h"
#include "request.h"

#include <cstdio>
#include <cstdlib>

int main (int argc, char** argv)
{
    AceSynthParams p;
    ace_synth_default_params (&p);

    p.dit_path          = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/acestep-v15-turbo-Q8_0.gguf";
    p.text_encoder_path = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/Qwen3-Embedding-0.6B-Q8_0.gguf";
    p.vae_path          = "D:/Dev/ACE-Step-1.5/acestep.vst3/models/vae-BF16.gguf";
    p.use_fa            = true;
    p.clamp_fp16        = false;

    // Optional: path to a LoRA .safetensors file or an adapter folder.
    if (argc > 1)
        p.lora_path = argv[1];

    if (argc > 2)
        p.lora_scale = (float) std::atof (argv[2]);

    fprintf (stderr, "[smoke] loading...\n");

    AceSynth* s = ace_synth_load (&p);

    if (s == nullptr)
    {
        fprintf (stderr, "[smoke] LOAD FAILED\n");
        return 1;
    }

    AceRequest req;
    request_init (&req);

    req.caption          = "dark trap beat with rolling 808 bass and eerie bells";
    req.duration         = 6.0f;
    req.bpm              = 140;
    req.keyscale         = "F# minor";
    req.inference_steps  = 8;
    req.guidance_scale   = 1.0f;
    req.shift            = 3.0f;
    req.batch_size       = 1;
    req.seed             = 42;

    AceAudio out {};
    const int rc = ace_synth_generate (s, &req, nullptr, 0, 1, &out, nullptr, nullptr);

    if (rc != 0 || out.samples == nullptr || out.n_samples <= 0)
    {
        fprintf (stderr, "[smoke] GENERATE FAILED rc=%d\n", rc);
        return 2;
    }

    fprintf (stderr, "[smoke] GENERATE OK n_samples=%d rate=%d (%.2fs)\n",
             out.n_samples, out.sample_rate, (double) out.n_samples / out.sample_rate);

    ace_audio_free (&out);
    ace_synth_free (s);

    fprintf (stderr, "[smoke] PASS\n");
    return 0;
}
