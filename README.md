# BtP Oven

A VST3 / Standalone loop-mutation and AI music workstation for Windows, built on JUCE 9.

Three identical loop lanes, one shared control surface, and a set of in-process AI
engines — no Python, no HTTP server, no background app. Everything runs inside the
plugin process.

> **Status:** 1.0.0, source available. Two builds from one tree: a personal
> build with RAVE, and a commercial build without it.
> Read [Licence](#licence) before you build or share it.

---

## What it does

| Page | Purpose |
|---|---|
| `NUKED` | Randomises audio parameters only. Never touches prompts, datasets or generator state. |
| `RACK` | Cook, timing, pitch, filters and FX for the selected lane. |
| `AI` | Text-to-music generation (ACE-Step 1.5) — instrumentals, vocals, hooks. |
| `STEM` | 4-stem separation (drums / bass / other / vocals) via Demucs. |
| `SHOTS` | One-shot library plus RAVE timbre mutation (personal build only). |
| `VOICE` | RVC voice conversion and auto-tune. |

Three loop lanes (**A / B / C**) are identical and fully independent: each goes
through Cook → Timing → Pitch → Filters → FX and is mixed at the output.

Generated audio, stems, voices and variants are ordinary audio buffers once they
exist, so they route through every normal plugin parameter and effect.

### Model lookup order

1. `%APPDATA%\BtP Oven\Models`
2. `Models\` next to the plugin binary
3. A legacy path from `ai_settings.json`

A folder is only used if the required file is actually present, so an empty
installer folder never shadows a working install.

---

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 or newer with the C++ desktop workload (CMake 3.22+)
- [JUCE 9](https://juce.com/get-juce/) — set `-DJUCE_DIR=<path>`
- Optional, each one independent:
  - [ACE-Step](https://github.com/ace-step/ACE-Step) checkout for the AI generator (`-DACESTEP_DIR`)
  - [demucs.cpp](https://github.com/sevagh/demucs.cpp) for stems (`-DDEMUCS_DIR`)
  - [onnxruntime](https://github.com/microsoft/onnxruntime) C++ package for RVC + RAVE (`-DONNX_DIR`)

If a dependency folder is missing, CMake disables that feature with a status
message and the rest of the plugin still builds. You do not need a GPU to
compile; CUDA for ACE-Step is detected automatically.

---

## Build

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
      -DJUCE_DIR="C:/JUCE" `
      -DACESTEP_DIR="D:/Dev/ACE-Step-1.5/acestep.vst3" `
      -DDEMUCS_DIR="D:/Dev/demucs.cpp" `
      -DONNX_DIR="D:/Dev/onnxruntime/onnxruntime-win-x64-1.31.1"

cmake --build build --config Release --parallel 8
```

Artifacts:

```
build/BtPOven_artefacts/Release/VST3/BtP Oven.vst3
build/BtPOven_artefacts/Release/Standalone/BtP Oven.exe
```

Minimal build with no AI engines:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DBTP_ENABLE_AI=OFF -DBTP_ENABLE_STEM=OFF -DBTP_ENABLE_RVC=OFF
```

Smoke tests (each must exit `0`):

```
build/BtPOven_artefacts/Release/btp-stem-smoke.exe
build/BtPOven_artefacts/Release/btp-rvc-smoke.exe
build/BtPOven_artefacts/Release/btp-rave-smoke.exe   # personal build only
```

### Build variants

RAVE and its `voice_multi` weights are CC-BY-NC 4.0, so they cannot ship in a
paid product. The engine is a compile-time switch:

```powershell
# personal / free build - RAVE in, default
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ...

# commercial build - no RAVE code, no RAVE models
cmake -S . -B build-commercial -G "Visual Studio 18 2026" -A x64 `
      -DBTP_ENABLE_RAVE=OFF ...
```

The configure step prints `RAVE variant engine ENABLED` or `DISABLED`. With
`-DBTP_ENABLE_RAVE=OFF` the SHOTS page has no MUTATE / SAVE VAR / TRAIN VAR
row at all. Full walkthrough: [Packaging/BUILD-VARIANTS.txt](Packaging/BUILD-VARIANTS.txt).

---

## Packaging and install

```powershell
# personal / free package (RAVE included)
powershell -ExecutionPolicy Bypass -File Packaging\Package-BtPOven.ps1 -Models Core -Zip

# commercial package (no RAVE code, no RAVE models)
powershell -ExecutionPolicy Bypass -File Packaging\Package-BtPOven.ps1 -Models Core -NoRave -BuildDir build-commercial -Zip
```

Builds `dist\BtP-Oven-<version>\` plus a zip. `-Models Core` bundles Demucs,
ContentVec and RMVPE (plus RAVE unless `-NoRave` is passed); `-Models All` also
bundles the ACE-Step checkpoints; omit it for a small package that downloads
models at install time. The commercial package carries a `NO-RAVE.txt` marker,
which the installer reads to skip RAVE folders and RAVE model checks.

Recipients run `INSTALL.cmd` once. It copies the VST3, the optional standalone,
creates `%APPDATA%\BtP Oven\{Models,Training,Legal}`, installs the example
manifests and the licence documents, and writes an install log.

Documentation that ships with the installer:

| File | Contents |
|---|---|
| `Packaging/README.txt` | Full user manual |
| `Packaging/TRAINING-GUIDE.txt` | GPU/CUDA setup and the RVC training workflow |
| `Packaging/NOTEBOOK-LAYOUT.txt` | Visual notebook layout |
| `Packaging/BUILD-VARIANTS.txt` | Personal vs commercial build, adding RAVE back |
| `Packaging/LICENSING.txt` | What you can sell, voice consent, AI disclosure |
| `Packaging/THIRD-PARTY.txt` | Component inventory and audit notes |
| `Packaging/LICENSES/` | Full licence texts |

---

## Licence

**Source available, not open source.** See [LICENSE](LICENSE).

You may read the source, build it for yourself, and make music with it —
including music you sell. The audio you create is yours. You may not sell the
plugin, redistribute a modified build, or use the source as the basis of
another product.

The two things that shape distribution:

1. **JUCE 9** is used under a paid **Indie** licence, which permits
   closed-source distribution up to USD 300,000/year. Pro is required above
   that.
2. **RAVE is CC-BY-NC 4.0** — code *and* weights. It stays in the personal
   build and is compiled out of the commercial build with
   `-DBTP_ENABLE_RAVE=OFF`.

Music you make with ACE-Step, Demucs, RVC (with the speaker's consent),
auto-tune, FX and mastering is yours to sell. Music containing a RAVE `MUTATE`
variant is also yours to sell: the Intelligent Instruments Lab, who trained the
weights, state that you may do "just about anything with them except sell the
models themselves as part of a product". What you may not do is ship the models
inside a paid plugin.

Full detail: [Packaging/LICENSING.txt](Packaging/LICENSING.txt) and
[Packaging/THIRD-PARTY.txt](Packaging/THIRD-PARTY.txt).

---

## Credits

Built with JUCE, ACE-Step 1.5, ggml, Demucs / demucs.cpp, Eigen, ONNX Runtime
and RVC.

RAVE variant generation uses the [RAVE](https://github.com/acids-ircam/RAVE)
framework by ACIDS/IRCAM and the `voice_multi` weights by the
[Intelligent Instruments Lab](https://iil.is), licensed CC-BY-NC 4.0.

> Intelligent Instruments Lab. *rave-models*. 2023.
> <https://huggingface.co/Intelligent-Instruments-Lab/rave-models>
> doi:10.57967/hf/1235

Voice models are never bundled. Train only on audio you own or that is
explicitly licensed for training, and get consent before cloning a voice.
