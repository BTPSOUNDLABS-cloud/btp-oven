================================================================
  BtP OVEN 1.0.0  -  AI MUSIC FACTORY  (VST3 + STANDALONE)
================================================================

Built by BtP.  Free to share with other producers.
Everything the AI does runs INSIDE the plugin process: no Python, no
browser, no server, no URL calls at runtime.  Your prompts and audio
never leave your machine.


----------------------------------------------------------------
1. WHAT IT IS
----------------------------------------------------------------

BtP Oven is a sampler / loop-mangler / FX rack with a full AI music
factory bolted on.  You can:

  * Generate music from a text prompt           (ACE-Step)
  * Generate lyrics + sung vocals               (ACE-Step 4B LM)
  * Generate one-shot samples by category       (ACE-Step)
  * Mutate a sample into new timbres/variants   (RAVE)
  * Split any song into 4 stems                 (Demucs v4)
  * Convert a vocal into another voice          (RVC)
  * Auto-tune a vocal to a key + scale          (native DSP)
  * Run everything through a 3-loop sampler,
    granular Cook, trap timing, pitch/texture,
    filters, 5-band EQ and a rack of FX

Every generated sound, stem or voice can be sent straight into the
sampler loops (A / B / C) and played through the entire FX chain.


----------------------------------------------------------------
2. SYSTEM REQUIREMENTS
----------------------------------------------------------------

  * Windows 10/11 64-bit
  * A DAW that loads VST3 (FL Studio, Ableton, Reaper, Cubase, ...)
  * RAM:       16 GB minimum, 32 GB recommended
  * GPU:       NVIDIA RTX with 8 GB+ VRAM recommended for AI
               (RTX 5070 / 40-series / 30-series all fine)
               CPU-only works but music generation is slow
  * Disk:      ~9 GB for the full model set
  * Internet:  only needed once, to download models


----------------------------------------------------------------
3. INSTALLING
----------------------------------------------------------------

FAST WAY (recommended)
  1. Unzip the release folder anywhere.
  2. Right-click INSTALL.cmd  ->  Run as administrator.
  3. Wait for the "INSTALL COMPLETE" message.
  4. Open your DAW and rescan plugins.

The installer does all of this:
  * copies the VST3 to the VST3 folder
  * copies the standalone app to  %LOCALAPPDATA%\Programs\BtP Oven
  * creates the Models and Training folders
  * copies any bundled models
  * creates a desktop shortcut

If the zip has no models, run the installer again with:
  powershell -ExecutionPolicy Bypass -File Install-BtPOven.ps1 -DownloadModels
(add -NoLM if you only want instrumental generation and want to skip
the 4.5 GB lyric model)

MANUAL WAY
  1. Copy  Payload\BtP Oven.vst3  to:
       C:\Program Files\Common Files\VST3\
     (or, without admin rights:
       %LOCALAPPDATA%\Programs\Common\VST3\ )
  2. Copy  Payload\Standalone\  anywhere you like (optional).
  3. Copy the model folders from  Models\  to:
       %APPDATA%\BtP Oven\Models\
  4. Start your DAW and rescan.

MODEL FOLDER LAYOUT
  %APPDATA%\BtP Oven\Models\
      ACE-Step\   acestep-v15-turbo-Q8_0.gguf
                  Qwen3-Embedding-0.6B-Q8_0.gguf
                  vae-BF16.gguf
                  acestep-5Hz-lm-4B-Q8_0.gguf     (vocals)
      Demucs\     ggml-model-htdemucs-4s-f16.bin
      RVC\hubert\ vec-768-layer-12.onnx
      RVC\rmvpe\  rmvpe.onnx                     (optional)
      RVC\voices\ <your voices>.onnx
      RAVE\       rave_encoder.onnx
                  rave_decoder.onnx
                  pqmf_hk.bin

If you keep models somewhere else, the plugin also looks in
<plugin folder>\Models\ and uses the legacy paths it knows about.
Settings can be changed in:
  %APPDATA%\BtP Oven\ai_settings.json
  %APPDATA%\BtP Oven\stem_settings.json
  %APPDATA%\BtP Oven\rvc_settings.json


----------------------------------------------------------------
4. FIRST RUN
----------------------------------------------------------------

Open the standalone (desktop shortcut) or load the VST3 in your DAW.

  * FIRST generation loads the model into memory.  Expect ~10-30 s
    of "LOADING" the first time.  After that, generation is much
    faster.
  * The AI runs on a worker thread, so the UI stays responsive.
  * If a page says "MODELS NOT FOUND", check the paths in section 3.

The header buttons switch pages:

   NUKED | RACK | AI | STEM | SHOTS | VOICE
   PLAY | PAUSE | STOP
     A  |   B   |   C            (loop selectors)
   SYNC  [140 BPM]               (tempo)
     IN / OUT knobs

PLAY / PAUSE / STOP and the tempo controls live in the header, so they
work on every page.  PAUSE freezes the loop playheads; STOP rewinds them
to the start of the region.  SYNC follows the DAW tempo when the host
provides one; turn SYNC off (or use a host with no tempo) to use the
manual BPM box.


----------------------------------------------------------------
5. THE PAGES
----------------------------------------------------------------

--- AI GEN -----------------------------------------------------

Prompt -> music.

  PROMPT      describe the track (genre, mood, instruments, bpm)
  LYRICS      optional; used with VOCALS
  DURATION    target length in seconds
  STEPS       diffusion steps (higher = slower, more detail)
  GUIDANCE    prompt adherence
  BPM         tempo hint
  SEED        lock a seed to reproduce a take
  VOCALS      toggle the 4B LM: writes lyrics + sung vocals
  PRESET      load a master preset (R&B / dark trap tracks, loops, vocals)

The PRESET box reads  master.json  from
%APPDATA%\BtP Oven\Training\manifests\ .  It ships with 15 starting
points across tracks, loops and vocals; edit the JSON to add your own.
Loading a preset only fills the prompt, lyrics and generator settings -
it never changes your models, datasets or the NUKED state.

Buttons:
  GENERATE    start
  CANCEL      stop
  TRAIN       capture this take (or the selected loop) to the training folder
  TO A/B/C    send the take into a sampler loop

The AI GEN page uses Loop C for preview, with its own Play /
Bypass / Loop / region / Reverse / Half / Double / Stutter control.

--- STEM -------------------------------------------------------

Split audio into Drums / Bass / Other / Vocals.

  USE A/B/C   take the current loop as the source
  LOAD FILE   or pick a file from disk
  SEPARATE    run Demucs
  each row:   waveform, TO A/B/C, EXPORT, TRAIN

EXPORT writes a .wav.  TRAIN copies that stem into the matching
Training\Stems\<type> folder.

--- SHOTS ------------------------------------------------------

One-shot sample factory.

  PROMPT      describe the sound
  CATEGORY    FX / Impacts / Risers / Pads / Vocal Chops / Sirens /
              Phones / Cash / Ambience / Synth One Shots
  DURATION    length of the one-shot
  GENERATE    create the sample
  TO A/B/C    send it to a loop
  SAVE        write it to your one-shot library
  TRAIN       copy it to Training\OneShots\<category>

RAVE VARIANTS (bottom of the SHOTS page):

  NOTE: this row is only present in builds that include RAVE. The
  commercial edition is compiled without it, so MUTATE / SAVE VAR /
  TRAIN VAR simply do not appear. See BUILD-VARIANTS.txt.

  VAR         mutation amount (0 = none, 1 = wild)
  MUTATE      run the last one-shot through RAVE
  SAVE VAR    save the variant into the library
  TRAIN VAR   copy the variant to Training\Variants
  TO C        send the variant to loop C

  The one-shot library lives wherever you chose with CHOOSE FOLDER,
  laid out as  <root>\<Category>\<name>_<timestamp>.wav

--- VOICE ------------------------------------------------------

Voice conversion (RVC).

  USE A/B/C   take the current loop as the source
  LOAD FILE   or pick a vocal file
  IN / OUT    trim the source vocal before conversion
  VOICE       choose a .onnx voice from Models\RVC\voices
  CONVERT     run the conversion
  TO A/B/C    send the converted vocal to a loop
  TRAIN&SAVE  copy the result to Training\Vocals

  The IN / OUT sliders under the source waveform set the exact section
  that gets converted (0-100%).  The source waveform shows the trimmed
  section bright and dims the rest.

  The result keeps the melody and timing of the source and takes the
  timbre of the chosen voice.  Works best on dry, isolated vocals.

--- FX RACK ----------------------------------------------------

Everything the loops go through:

  COOK        granular / stutter / glitch engine
  TIMING      trap-style timing, swing, rolls
  PITCH       pitch / formant / texture
  FILTERS     low/high pass, resonance
  EQ          5-band
  FX          saturation, chorus, delay, reverb, etc.
  AUTOTUNE    key + scale locked, speed, mix, snap
  NUKED       randomises the audio parameters ONLY.
              It never touches prompts, training data or models.

The rack works on live audio AND on AI/stem/voice results once they
are in loops A/B/C.


----------------------------------------------------------------
6. LOOPS A / B / C
----------------------------------------------------------------

Three independent sampler lanes.  Each has:

  PLAY / BYPASS / LOOP
  region start + end
  REVERSE
  HALF / DOUBLE
  STUTTER

All three are mixed, processed by the same Cook/Timing/Pitch/Filter/
EQ/FX rack, and recorded by the FX recorder.  Use them to layer a
generated track, a stem, a one-shot and a converted vocal.


----------------------------------------------------------------
7. TRAINING (short version)
----------------------------------------------------------------

Every AI page has a capture button ("TRAIN", "TRAIN & SAVE",
"TRAIN VAR").  It knows what the audio is and files it automatically:

  VOICE  -> TRAIN&SAVE  ->  Training\Vocals
  AI GEN -> TRAIN       ->  Training\Loops   (or Vocals if sung)
  SHOTS  -> TRAIN       ->  Training\OneShots\<category>
  SHOTS  -> TRAIN VAR   ->  Training\Variants
  STEM   -> TRAIN       ->  Training\Stems\<type>

Everything lands under:
  %APPDATA%\BtP Oven\Training\

Capturing is instant and happens inside the plugin.  TRAIN on the AI
page captures the current AI take, or the selected loop if there is no
take yet, and shows the full saved path in the status line.

LoRAs are discovered in:
  %APPDATA%\BtP Oven\LoRAs\
Drop .safetensors files there (or in the plugin's Models folder) and
press LORA DIR on the AI page to open the folder.  The LoRA picker
rescans when the page opens, after TRAIN and when LORA DIR is pressed.

Actual neural training (RVC, RAVE, LoRA) is a separate one-time
script run, because it takes 30-60+ minutes on a GPU.

See TRAINING-GUIDE.txt for the full workflow, folder map and example
JSON manifests.  Section 0 there has the one-time GPU / CUDA setup
for RVC voice training (not needed to use the plugin).
See NOTEBOOK-LAYOUT.txt for a visual layout you can copy into your notes.


----------------------------------------------------------------
8. TROUBLESHOOTING
----------------------------------------------------------------

"MODELS NOT FOUND"
  Check %APPDATA%\BtP Oven\Models\ against section 3.
  Run the installer with -DownloadModels.

Plugin not showing in the DAW
  Rescan VST3 plugins.  Check the install path.
  In FL Studio: Options -> Manage plugins -> Find installed plugins.

Install fails with "access denied"
  Close FL Studio / your DAW (it locks the .vst3), then run
  INSTALL.cmd as administrator.

AI is very slow
  Music generation needs a CUDA GPU for comfortable speed.
  Lower STEPS, shorter DURATION, and turn VOCALS off for speed.
  The first generation is always slower (model load).

Out of memory / crash on VOCALS
  The 4B LM needs a lot of VRAM.  Close other GPU apps, use a
  shorter duration, or generate instrumentals first and add vocals
  in a second pass.

RAVE variants sound "clean" / less airy
  Known limitation: the ONNX decoder omits the FFT noise module.
  Timbre mutation works; the very top-end noise layer is missing.

Voice conversion sounds robotic
  Use a dry, isolated, mono-ish vocal.  Long reverb or a full mix
  as the source will confuse the pitch/timbre tracking.

Voice training says "torch has no CUDA device"
  That is the one-time setup, not a plugin problem.  Section 0 of
  TRAINING-GUIDE.txt has the exact commands to install the GPU build
  of torch, plus a check you can run.

Nothing records in the FX recorder
  Arm the recorder before playback; it records the post-rack mix.


----------------------------------------------------------------
9. LICENSES / CREDITS
----------------------------------------------------------------

Full details are in LICENSING.txt (plain English), THIRD-PARTY.txt
(the inventory) and LICENSES\ (the actual licence texts). The terms
that cover the plugin itself are in LICENSE-BtPOven.txt.

  JUCE 9               - AGPLv3 OR commercial JUCE licence
  ACE-Step 1.5         - Apache-2.0 code, MIT model weights
  ggml                 - MIT
  yyjson               - MIT
  Demucs / demucs.cpp  - MIT code, MIT weights
  Eigen                - MPL-2.0
  onnxruntime          - MIT
  RVC                  - MIT
  ContentVec / HuBERT  - MIT
  RMVPE                - MIT (optional pitch tracker)
  RAVE (acids-ircam)   - CC-BY-NC 4.0  (code AND weights)
  RAVE voice_multi     - CC-BY-NC 4.0 (Intelligent Instruments Lab)
                         https://huggingface.co/Intelligent-Instruments-Lab/rave-models
                         doi:10.57967/hf/1235

WHAT YOU CAN DO
  * Sell beats, hooks and songs you make with the plugin: yes.
  * Sell a beat that contains a RAVE MUTATE variant: yes. The model
    authors say you can do "just about anything" with the models
    except sell the models themselves. Credit IIL + the DOI.
  * Give the plugin away for free, with RAVE: yes, with attribution.
  * Sell the plugin: yes, but only the build made without RAVE.
    Build with -DBTP_ENABLE_RAVE=OFF and package with -NoRave - see
    BUILD-VARIANTS.txt. Your JUCE licence covers the rest.

TWO BUILDS
  Personal / free   RAVE included. Use it, share it, don't sell it.
  Commercial        No RAVE code, no RAVE models. Sell this one.

VOICE MODELS are not bundled by default.  Only use voices you have
the right to use and redistribute.  Never clone a voice without the
speaker's permission.  If you train a voice, make sure the source
audio is yours or royalty-free.


----------------------------------------------------------------
10. QUICK REFERENCE
----------------------------------------------------------------

  VST3 (machine)  C:\Program Files\Common Files\VST3\BtP Oven.vst3
  VST3 (user)     %LOCALAPPDATA%\Programs\Common\VST3\BtP Oven.vst3
  Standalone      %LOCALAPPDATA%\Programs\BtP Oven\BtP Oven.exe
  Config          %APPDATA%\BtP Oven\
  Models          %APPDATA%\BtP Oven\Models\
  Training        %APPDATA%\BtP Oven\Training\
  Presets         %APPDATA%\BtP Oven\Training\manifests\master.json
  LoRAs           %APPDATA%\BtP Oven\LoRAs\
  Legal           %APPDATA%\BtP Oven\Legal\
  One-shot lib    chosen in the SHOTS page

================================================================
  Make something loud.
================================================================
