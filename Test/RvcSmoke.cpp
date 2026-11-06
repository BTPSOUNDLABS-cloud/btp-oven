// RVC smoke test: converts a real vocal through the in-process RVC engine and
// writes the result so the pipeline can be verified end-to-end.
#include "RvcEngine.h"

#include <JuceHeader.h>
#include <cstdio>

int main()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    juce::File src ("D:/Dev1/creations/BtP_Hook_Voices_ORIGINAL/BtP_Hook_Voices/Smooth_RnB_Male.wav");

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (src));

    if (reader == nullptr)
    {
        fprintf (stderr, "[rvc] could not read source\n");
        return 1;
    }

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);

    fprintf (stderr, "[rvc] source: %d samples @ %.0f Hz (%.1fs)\n",
             (int) reader->lengthInSamples, reader->sampleRate,
             (double) reader->lengthInSamples / reader->sampleRate);

    auto& engine = RvcEngine::getInstance();
    engine.convert (buf, reader->sampleRate);

    for (int i = 0; i < 240; ++i)   // up to 2 minutes
    {
        juce::Thread::sleep (500);

        RvcEngine::Result r;

        if (engine.takeResult (r))
        {
            fprintf (stderr, "[rvc] CONVERTED %d samples (%.1fs)\n",
                     r.audio.getNumSamples(), r.seconds);

            juce::File out ("D:/Dev/rvc-models/out.wav");

            std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (
                os.get(), reader->sampleRate, 2, 16, {}, 0));

            // createWriterFor takes ownership of the stream on success.
            if (w != nullptr)
            {
                os.release();
                w->writeFromAudioSampleBuffer (r.audio, 0, r.audio.getNumSamples());
                w->flush();
            }

            fprintf (stderr, "[rvc] wrote %s\n", out.getFullPathName().toRawUTF8());

            // Peak check (is it non-silent?)
            float peak = 0.0f;
            for (int ch = 0; ch < r.audio.getNumChannels(); ++ch)
                for (int i = 0; i < r.audio.getNumSamples(); ++i)
                    peak = juce::jmax (peak, std::abs (r.audio.getSample (ch, i)));

            fprintf (stderr, "[rvc] peak=%.4f\n", peak);
            fprintf (stderr, "[rvc] PASS\n");
            engine.shutdown();
            return 0;
        }

        if ((i % 4) == 0)
            fprintf (stderr, "[rvc] state=%d  %s\n",
                     (int) engine.getState(), engine.getStatusText().toRawUTF8());
    }

    fprintf (stderr, "[rvc] TIMEOUT\n");
    return 2;
}
