// RAVE smoke test: mutate a source audio through the in-process RAVE engine.
#include "RaveEngine.h"

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
        fprintf (stderr, "[rave] could not read source\n");
        return 1;
    }

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);

    fprintf (stderr, "[rave] source: %d samples @ %.0f Hz\n",
             (int) reader->lengthInSamples, reader->sampleRate);

    auto& engine = RaveEngine::getInstance();
    engine.mutate (buf, reader->sampleRate, 0.5f, 42);

    for (int i = 0; i < 120; ++i)
    {
        juce::Thread::sleep (500);

        RaveEngine::Result r;

        if (engine.takeResult (r))
        {
            fprintf (stderr, "[rave] MUTATED %d samples (%.1fs)\n",
                     r.audio.getNumSamples(), r.seconds);

            juce::File out ("D:/Dev/rave-models/out.wav");
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

            float peak = 0.0f;
            for (int ch = 0; ch < r.audio.getNumChannels(); ++ch)
                for (int i2 = 0; i2 < r.audio.getNumSamples(); ++i2)
                    peak = juce::jmax (peak, std::abs (r.audio.getSample (ch, i2)));

            fprintf (stderr, "[rave] wrote %s, peak=%.4f\n",
                     out.getFullPathName().toRawUTF8(), peak);
            fprintf (stderr, "[rave] PASS\n");
            engine.shutdown();
            return 0;
        }

        if ((i % 4) == 0)
            fprintf (stderr, "[rave] state=%d  %s\n",
                     (int) engine.getState(), engine.getStatusText().toRawUTF8());
    }

    fprintf (stderr, "[rave] TIMEOUT\n");
    return 2;
}
