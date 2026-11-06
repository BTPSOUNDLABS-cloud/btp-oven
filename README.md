# BtP Oven

A VST3 / Standalone loop-mutation and AI music workstation for Windows, built on JUCE 9.

Three identical loop lanes, one shared control surface, and a set of in-process AI
engines — no Python, no HTTP server, no background app. Everything runs inside the
plugin process.

> **Status:** 1.0.0, source available, free for non-commercial distribution.
> Read [Licence](#licence) before you build or share it.

---

## What it does

| Page | Purpose |
|---|---|
| `NUKED` | Randomises audio parameters only. Never touches prompts, datasets or generator state. |
| `RACK` | Cook, timing, pitch, filters and FX for the selected lane. |
| `AI` | Text-to-music generation (ACE-Step 1.5) — instrumentals, vocals, hooks. |
| `STEM` | 4-stem separation (drums / bass / other / vocals) via Demucs. |
| `SHOTS` | One-shot library plus RAVE timbre mutation. |
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
build/BtPOven_artefacts/Release/btp-rave-smoke.exe
```

---

## Packaging and install

```powershell
powershell -ExecutionPolicy Bypass -File Packaging\Package-BtPOven.ps1 -Models Core -Zip
```

Builds `dist\BtP-Oven-<version>\` plus a zip. `-Models Core` bundles Demucs,
ContentVec, RMVPE and RAVE; `-Models All` also bundles the ACE-Step checkpoints;
omit it for a small package that downloads models at install time.

Recipients run `INSTALL.cmd` once. It copies the VST3, the optional standalone,
creates `%APPDATA%\BtP Oven\{Models,Training,Legal}`, installs the example
manifests and the licence documents, and writes an install log.

Documentation that ships with the installer:

| File | Contents |
|---|---|
| `Packaging/README.txt` | Full user manual |
| `Packaging/TRAINING-GUIDE.txt` | GPU/CUDA setup and the RVC training workflow |
| `Packaging/NOTEBOOK-LAYOUT.txt` | Visual notebook layout |
| `Packaging/LICENSING.txt` | What you can sell, voice consent, AI disclosure |
| `Packaging/THIRD-PARTY.txt` | Component inventory and audit notes |
| `Packaging/LICENSES/` | Full licence texts |

---

## Licence

**Source available, not open source.** You may read, build and use it for your
own music — including music you sell. You may not sell the plugin or ship a
modified build. See [LICENSE](LICENSE).

Two things you should know before distributing:

1. **JUCE 9** is used under the free **Starter** tier (revenue under
   USD 20,000/year). Above that, a commercial tier is required.
2. **RAVE is CC-BY-NC 4.0** — code *and* weights. That makes this release
   non-commercial. A paid build must remove or replace the RAVE engine.

Music you make with ACE-Step, Demucs, RVC (with the speaker's consent),
auto-tune, FX and mastering is yours to sell. Music containing a RAVE
`MUTATE` variant is not.

Full detail: [Packaging/LICENSING.txt](Packaging/LICENSING.txt) and
[Packaging/THIRD-PARTY.txt](Packaging/THIRD-PARTY.txt).

---

## Credits

Built with JUCE, ACE-Step 1.5, ggml, Demucs / demucs.cpp, Eigen, ONNX Runtime
and RVC. RAVE variant generation uses the RAVE framework and voice_multi weights
by Intelligent Instruments Lab, licensed CC-BY-NC 4.0.

Voice models are never bundled. Train only on audio you own or that is
explicitly licensed for training, and get consent before cloning a voice.
