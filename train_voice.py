#!/usr/bin/env python3
"""
BtP Oven - RVC voice trainer (one command)

WHAT THIS DOES
  Turns a folder of one consistent voice into an RVC .onnx model that the
  plugin's VOICE page can load.

  The plugin's TRAIN & SAVE button writes captures to:
      %APPDATA%\\BtP Oven\\Training\\Vocals

  This script runs the full RVC pipeline:
      1. preprocess          (resample + slice)
      2. extract f0          (rmvpe / harvest / pm)
      3. extract features    (HuBERT / ContentVec 768)
      4. train               (RVC v2)
      5. export .onnx        (drop-in for the plugin)

USAGE
  Train from the plugin's Vocals folder:
      python train_voice.py --name "my_rnb_male"

  Train from any folder:
      python train_voice.py --name "my_voice" --input "D:\\voices\\my_voice"

  Check everything without training:
      python train_voice.py --name "test" --dry-run

  CPU-only torch (very slow, hours):
      python train_voice.py --name "my_voice" --allow-cpu

OUTPUT
  %APPDATA%\\BtP Oven\\Models\\RVC\\voices\\<name>.onnx
  (also copied to D:\\Dev\\rvc-models\\voices if that folder exists)

REQUIREMENTS
  * The RVC-WebUI checkout (default below, override with BTP_RVC_WEBUI)
  * Python 3.10 with torch. A CUDA build is strongly recommended.
  * ~10+ minutes of ONE consistent, dry, isolated voice.
  * Royalty-free audio, or audio you own / have permission to use.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------
RVCWEB = Path(os.environ.get("BTP_RVC_WEBUI",
                             r"D:\Dev\ai-gen-app\Retrieval-based-Voice-Conversion-WebUI"))

APPDATA = Path(os.environ.get("APPDATA", Path.home()))
BTP_DIR = APPDATA / "BtP Oven"
TRAINING = BTP_DIR / "Training"
VOICES_OUT = BTP_DIR / "Models" / "RVC" / "voices"
LEGACY_VOICES = Path(r"D:\Dev\rvc-models\voices")
DEPS = BTP_DIR / "Training" / "pydeps"

# site-package folders we can borrow from (first one with torch wins)
CANDIDATE_SITES = [
    RVCWEB / ".venv" / "Lib" / "site-packages",
    Path(r"D:\Dev\BtP_vst_Projects\BtP_TrapForge_v3\rvc_env\Lib\site-packages"),
]

HF = "https://huggingface.co/lj1995/VoiceConversionWebUI/resolve/main/"

ASSETS = [
    ("assets/hubert/hubert_base.pt", HF + "hubert_base.pt"),
    ("assets/rmvpe/rmvpe.pt", HF + "rmvpe.pt"),
    ("assets/pretrained_v2/f0G40k.pth", HF + "pretrained_v2/f0G40k.pth"),
    ("assets/pretrained_v2/f0D40k.pth", HF + "pretrained_v2/f0D40k.pth"),
]

AUDIO_EXTS = (".wav", ".mp3", ".ogg", ".flac", ".m4a", ".aif", ".aiff")


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def log(msg=""):
    print(msg, flush=True)


def die(msg, code=1):
    log("")
    log("[train] ERROR: " + msg)
    sys.exit(code)


def pick_site():
    for p in CANDIDATE_SITES:
        if (p / "torch").is_dir():
            return p
    return None


def setup_paths(site):
    # RVCWEB first (infer.* imports), then its packages, then local deps last
    for p in (str(RVCWEB), str(site) if site else None, str(DEPS)):
        if p and p not in sys.path:
            sys.path.insert(0, p) if p != str(DEPS) else sys.path.append(p)


def subprocess_env(site):
    parts = [str(RVCWEB)]
    if site:
        parts.append(str(site))
    parts.append(str(DEPS))
    old = os.environ.get("PYTHONPATH", "")
    if old:
        parts.append(old)
    env = os.environ.copy()
    env["PYTHONPATH"] = os.pathsep.join(parts)
    env["PYTHONIOENCODING"] = "utf-8"
    return env


def ensure_module(module, pip_name=None, site=None):
    try:
        __import__(module)
        return True
    except Exception:
        pass

    name = pip_name or module
    log(f"[train] installing missing python package: {name}")
    DEPS.mkdir(parents=True, exist_ok=True)
    cmd = [sys.executable, "-m", "pip", "install", "--target", str(DEPS),
           "--upgrade", "--no-warn-script-location", name]
    r = subprocess.run(cmd, env=subprocess_env(site))
    if r.returncode != 0:
        return False

    if str(DEPS) not in sys.path:
        sys.path.append(str(DEPS))
    # invalidate caches so the new folder is seen
    import importlib
    importlib.invalidate_caches()
    try:
        __import__(module)
        return True
    except Exception:
        return False


def download(url, dest, expected_hint=""):
    import urllib.request

    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)

    log(f"[train] downloading {dest.name} {expected_hint} ...")
    tmp = dest.with_suffix(dest.suffix + ".part")

    def hook(blocks, block_size, total):
        if total > 0:
            done = blocks * block_size
            pct = min(100.0, done * 100.0 / total)
            mb = done / (1024 * 1024)
            total_mb = total / (1024 * 1024)
            print(f"\r        {mb:7.1f} / {total_mb:7.1f} MB  ({pct:5.1f}%)",
                  end="", flush=True)

    try:
        urllib.request.urlretrieve(url, tmp, reporthook=hook)
        print()
        if tmp.stat().st_size < 1024:
            raise RuntimeError("downloaded file is empty")
        tmp.replace(dest)
        log(f"[train] saved {dest}")
    except Exception as e:
        print()
        if tmp.exists():
            try:
                tmp.unlink()
            except Exception:
                pass
        die(f"could not download {url}\n       {e}")


def ensure_assets(no_download, dry_run=False):
    missing = []
    for rel, url in ASSETS:
        if not (RVCWEB / rel).is_file():
            missing.append((rel, url))

    if not missing:
        log("[train] RVC base assets present")
        return

    if dry_run:
        log("[train] missing base assets (would download):")
        for rel, _ in missing:
            log(f"         {rel}")
        return

    if no_download:
        die("missing RVC assets (re-run without --no-download):\n       " +
            "\n       ".join(rel for rel, _ in missing))

    for rel, url in missing:
        # rough size hint for the progress line
        hint = {
            "hubert_base.pt": "(~189 MB)",
            "rmvpe.pt": "(~181 MB)",
            "f0G40k.pth": "(~73 MB)",
            "f0D40k.pth": "(~143 MB)",
        }
        download(url, RVCWEB / rel, hint.get(Path(rel).name, ""))


def audio_files(folder):
    folder = Path(folder)
    return [p for p in folder.rglob("*")
            if p.is_file() and p.suffix.lower() in AUDIO_EXTS]


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Train an RVC voice from captured audio.")
    ap.add_argument("--name", required=True, help="voice name (output .onnx)")
    ap.add_argument("--input", default=None, help="folder of voice audio (default: Training/Vocals)")
    ap.add_argument("--sr", default="40k", choices=["32k", "40k", "48k"], help="RVC sample rate")
    ap.add_argument("--epochs", type=int, default=200, help="total epochs")
    ap.add_argument("--batch-size", type=int, default=8, help="batch size")
    ap.add_argument("--save-every", type=int, default=40, help="checkpoint interval (epochs)")
    ap.add_argument("--f0", default="rmvpe", choices=["rmvpe", "harvest", "pm", "dio"], help="pitch method")
    ap.add_argument("--n-proc", type=int, default=4, help="preprocess worker processes")
    ap.add_argument("--per", type=float, default=2.0, help="max seconds per training slice")
    ap.add_argument("--allow-cpu", action="store_true", help="allow CPU-only training (very slow)")
    ap.add_argument("--no-download", action="store_true", help="never download missing base assets")
    ap.add_argument("--dry-run", action="store_true", help="check env + data, do not train")
    ap.add_argument("--keep-run", action="store_true", help="keep the work folder if it exists")
    args = ap.parse_args()

    log("")
    log("============================================================")
    log("  BtP Oven - RVC voice trainer")
    log("============================================================")

    # ---- sanity -----------------------------------------------------------
    if not RVCWEB.is_dir():
        die(f"RVC-WebUI not found: {RVCWEB}\n"
            f"       Set BTP_RVC_WEBUI to the folder that contains infer/ and assets/.")

    name = args.name.strip().replace(" ", "_")
    if not name:
        die("--name cannot be empty")

    inp = Path(args.input) if args.input else (TRAINING / "Vocals")
    if not inp.is_dir():
        die(f"input folder not found: {inp}\n"
            f"       Capture audio with the plugin's TRAIN & SAVE button, or pass --input.")

    files = audio_files(inp)
    if not files:
        die(f"no audio files in: {inp}\n"
            f"       Accepted: {', '.join(AUDIO_EXTS)}")

    log(f"[train] voice      : {name}")
    log(f"[train] input      : {inp}")
    log(f"[train] files      : {len(files)}")
    log(f"[train] sample rate: {args.sr}")
    log(f"[train] epochs     : {args.epochs}")

    if len(files) < 3:
        log("[train] WARNING: very few files. RVC wants ~10+ minutes of one voice.")

    # ---- python env -------------------------------------------------------
    site = pick_site()
    if site is None:
        die("could not find a site-packages folder with torch.\n"
            f"       Checked:\n       " + "\n       ".join(str(p) for p in CANDIDATE_SITES) +
            "\n       Install torch for this Python, or set BTP_RVC_WEBUI.")

    setup_paths(site)
    log(f"[train] packages   : {site}")

    try:
        import torch
    except Exception:
        traceback.print_exc()
        die("could not import torch")

    cuda = bool(torch.cuda.is_available())
    log(f"[train] torch      : {torch.__version__}   cuda: {cuda}")

    if not cuda and not args.allow_cpu:
        die("torch has no CUDA device, so training would run on CPU (hours/days).\n"
            "       Install a CUDA torch build, for example:\n"
            f"         \"{sys.executable}\" -m pip install --upgrade --index-url https://download.pytorch.org/whl/cu128 torch torchaudio\n"
            "       Then re-run this command.\n"
            "       (Or pass --allow-cpu if you really want CPU training.)")

    # ---- optional imports / bootstrap ------------------------------------
    for mod, pipname in [
        ("parselmouth", "praat-parselmouth"),
        ("soundfile", "soundfile"),
        ("onnx", "onnx"),
        ("onnxsim", "onnxsim"),
    ]:
        if args.dry_run and not _can_import(mod):
            log(f"[train] missing (would install): {pipname}")
            continue
        if not ensure_module(mod, pipname, site):
            die(f"could not install/import '{mod}'. Install it manually with:\n"
                f"         pip install {pipname}")

    ensure_assets(args.no_download, args.dry_run)

    # ---- work dir ---------------------------------------------------------
    exp_rel = f"logs/{name}"
    exp_abs = RVCWEB / exp_rel
    if exp_abs.exists() and not args.keep_run:
        log(f"[train] clearing previous run: {exp_abs}")
        shutil.rmtree(exp_abs, ignore_errors=True)

    if args.dry_run:
        log("")
        log("[train] DRY RUN OK - environment and data look usable.")
        log(f"[train] would write: {exp_abs}")
        return 0

    # ---- 1. preprocess ----------------------------------------------------
    log("")
    log("[train] 1/5 preprocessing ...")
    os.chdir(RVCWEB)
    from infer.modules.train.preprocess import preprocess_trainset
    preprocess_trainset(str(inp), int(args.sr[:-1]) * 1000, args.n_proc, str(exp_abs), args.per)

    gt = exp_abs / "0_gt_wavs"
    n_slices = len(list(gt.glob("*.wav"))) if gt.is_dir() else 0
    log(f"[train] slices: {n_slices}")
    if n_slices == 0:
        die("preprocessing produced no slices. The audio may be silent, too short, or corrupt.")

    env = subprocess_env(site)

    # ---- 2. f0 ------------------------------------------------------------
    log("")
    log(f"[train] 2/5 extracting pitch ({args.f0}) ...")
    f0_script = RVCWEB / "infer/modules/train/extract/extract_f0_print.py"
    r = subprocess.run([sys.executable, str(f0_script), str(exp_abs),
                        str(args.n_proc), args.f0], cwd=str(RVCWEB), env=env)
    if r.returncode != 0:
        die("pitch extraction failed (see output above)")

    # ---- 3. features ------------------------------------------------------
    log("")
    log("[train] 3/5 extracting features ...")
    feat_script = RVCWEB / "infer/modules/train/extract_feature_print.py"
    device = "cuda:0" if cuda else "cpu"
    is_half = "True" if cuda else "False"
    r = subprocess.run([sys.executable, str(feat_script), device, "1", "0",
                        str(exp_abs), "v2", is_half], cwd=str(RVCWEB), env=env)
    if r.returncode != 0:
        die("feature extraction failed (see output above)")

    # ---- build filelist + config -----------------------------------------
    log("[train] building file list ...")
    feature = exp_abs / "3_feature768"
    f0d = exp_abs / "2a_f0"
    f0n = exp_abs / "2b-f0nsf"

    def stems(d):
        return {p.stem.replace(".wav", "") for p in d.glob("*.npy")} if d.is_dir() else set()

    names = (stems(gt) & stems(feature) & stems(f0d) & stems(f0n))
    if not names:
        die("no matching slices across wav / feature / f0 folders")

    opt = []
    for nm in sorted(names):
        opt.append("%s/%s.wav|%s/%s.npy|%s/%s.wav.npy|%s/%s.wav.npy|0" % (
            str(gt).replace("\\", "\\\\"), nm,
            str(feature).replace("\\", "\\\\"), nm,
            str(f0d).replace("\\", "\\\\"), nm,
            str(f0n).replace("\\", "\\\\"), nm))

    mute = RVCWEB / "logs/mute"
    for _ in range(2):
        opt.append("%s/0_gt_wavs/mute%s.wav|%s/3_feature768/mute.npy|%s/2a_f0/mute.wav.npy|%s/2b-f0nsf/mute.wav.npy|0" % (
            str(mute), args.sr, str(mute), str(mute), str(mute)))

    (exp_abs / "filelist.txt").write_text("\n".join(opt), encoding="utf-8")

    import json
    import config as rvc_config
    cfg_key = ("v1/" if False else "v2/") + args.sr + ".json"
    with open(exp_abs / "config.json", "w", encoding="utf-8") as f:
        json.dump(rvc_config.json_config[cfg_key], f, ensure_ascii=False, indent=4)

    # ---- 4. train ---------------------------------------------------------
    log("")
    log(f"[train] 4/5 training for {args.epochs} epochs ...")
    pg = RVCWEB / "assets/pretrained_v2" / f"f0G{args.sr}.pth"
    pd = RVCWEB / "assets/pretrained_v2" / f"f0D{args.sr}.pth"

    cmd = [sys.executable, "infer/modules/train/train.py",
           "-e", name, "-sr", args.sr, "-f0", "1",
           "-bs", str(args.batch_size), "-g", "0",
           "-te", str(args.epochs), "-se", str(args.save_every),
           "-l", "0", "-c", "0", "-sw", "1", "-v", "v2"]
    if pg.is_file():
        cmd += ["-pg", str(pg)]
    if pd.is_file():
        cmd += ["-pd", str(pd)]

    r = subprocess.run(cmd, cwd=str(RVCWEB), env=env)
    if r.returncode != 0:
        die("training failed (see output above)")

    # ---- 5. export --------------------------------------------------------
    log("")
    log("[train] 5/5 exporting .onnx ...")
    weights = sorted(glob.glob(str(RVCWEB / "assets/weights" / f"{name}_e*.pth")),
                     key=os.path.getmtime)
    if not weights:
        # fall back to a plain name
        weights = sorted(glob.glob(str(RVCWEB / "assets/weights" / f"{name}*.pth")),
                         key=os.path.getmtime)
    if not weights:
        die(f"training finished but no weights found in {RVCWEB / 'assets/weights'}")

    pth = weights[-1]
    log(f"[train] weights: {pth}")

    VOICES_OUT.mkdir(parents=True, exist_ok=True)
    out_onnx = VOICES_OUT / f"{name}.onnx"

    from infer.modules.onnx.export import export_onnx
    export_onnx(str(pth), str(out_onnx))

    if not out_onnx.is_file():
        die("ONNX export did not produce a file")

    # mirror to the legacy dev folder if present
    if LEGACY_VOICES.is_dir():
        try:
            shutil.copy2(out_onnx, LEGACY_VOICES / out_onnx.name)
        except Exception:
            pass

    mb = out_onnx.stat().st_size / (1024 * 1024)
    log("")
    log("============================================================")
    log(f"  DONE: {out_onnx}  ({mb:.1f} MB)")
    log("  Reopen the VOICE page (or restart the plugin) and pick it.")
    log("============================================================")
    return 0


def _can_import(mod):
    try:
        __import__(mod)
        return True
    except Exception:
        return False


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        log("\n[train] cancelled")
        sys.exit(130)
    except SystemExit:
        raise
    except Exception:
        traceback.print_exc()
        sys.exit(1)
