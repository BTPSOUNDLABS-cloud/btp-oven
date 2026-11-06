// Stem smoke test: loads the Demucs 4-source model through the same static
// btp-demucs library the plugin uses, and separates a short clip.
#include "model.hpp"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>

int main()
{
    const int rate = 44100;
    const int N = rate * 2;   // 2 seconds

    Eigen::MatrixXf audio (2, N);

    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / (float) rate;
        audio (0, i) = 0.25f * std::sin (2.0f * 3.14159f * 220.0f * t);
        audio (1, i) = audio (0, i);
    }

    demucscpp::demucs_model model;

    // Same resolution rule as the plugin: portable %APPDATA% folder first,
    // then the legacy dev path. BTP_DEMUCS_MODEL overrides both.
    namespace fs = std::filesystem;

    fs::path modelFile;

    if (auto* env = std::getenv ("BTP_DEMUCS_MODEL"))
        modelFile = fs::path (env);

    if (modelFile.empty() || ! fs::exists (modelFile))
    {
        if (auto* appData = std::getenv ("APPDATA"))
            modelFile = fs::path (appData) / "BtP Oven" / "Models" / "Demucs"
                          / "ggml-model-htdemucs-4s-f16.bin";
    }

    if (modelFile.empty() || ! fs::exists (modelFile))
        modelFile = fs::path ("D:/Dev/demucs-models/ggml-model-htdemucs-4s-f16.bin");

    fprintf (stderr, "[stem] model=%s\n", modelFile.string().c_str());

    if (! demucscpp::load_demucs_model (modelFile.string().c_str(), &model))
    {
        fprintf (stderr, "[stem] LOAD FAILED\n");
        return 1;
    }

    fprintf (stderr, "[stem] loaded, is_4sources=%d\n", model.is_4sources ? 1 : 0);

    demucscpp::ProgressCallback cb = [] (float p, const std::string& msg)
    {
        if (msg.find ("chunk") != std::string::npos || p > 0.999f)
            fprintf (stderr, "[stem] %.0f%% %s\n", p * 100.0f, msg.c_str());
    };

    auto out = demucscpp::demucs_inference (model, audio, cb);

    fprintf (stderr, "[stem] output dims: %ld sources x %ld ch x %ld samples\n",
             out.dimension (0), out.dimension (1), out.dimension (2));
    fprintf (stderr, "[stem] PASS\n");
    return 0;
}
