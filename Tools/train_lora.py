#!/usr/bin/env python3
"""
BtP Oven - ACE-Step LoRA trainer wrapper
========================================

Turns a folder of captured loops / songs into an ACE-Step LoRA that the
plugin can load from its LORA picker.

This is a one-time, offline GPU job.  The plugin itself still runs entirely
in-process; nothing here is used during audio playback.

It drives the official ACE-Step 1.5 training code:

    <acestep-root>\\train.py fixed  (Side-Step, Apache-2.0)

and writes the result as:

    %APPDATA%\\BtP Oven\\LoRAs\\<name>\\
        adapter_model.safetensors
        adapter_config.json
        lora_info.json

The C++ inference engine merges PEFT lora_A / lora_B tensors and reads
lora_alpha from adapter_config.json, so no conversion step is needed.

USAGE
    python train_lora.py --name "dark_trap_140" --audio-dir "%APPDATA%\\BtP Oven\\Training\\Loops"

    # explicit paths
    python train_lora.py --name test --acestep-root D:\\Dev\\ACE-Step-1.5 ^
        --python D:\\Dev\\ACE-Step-1.5\\.venv\\Scripts\\python.exe ^
        --audio-dir D:\\loops --epochs 40 --rank 16

    # what would run, without running it
    python train_lora.py --name test --audio-dir D:\\loops --dry-run

NOTES
    * Work files must live under the ACE-Step root because the official
      trainer sandboxes paths to its working directory.
    * Audio sidecars written by BtP Oven (<name>.json) supply caption,
      lyrics and BPM automatically.  Otherwise the filename is the caption.
    * 12 GB cards are fine for loop-length clips; peak VRAM in testing was
      ~4.6 GiB at rank 8.  Use rank 16-32 for larger datasets.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

AUDIO_EXTENSIONS = {".wav", ".mp3", ".flac", ".ogg", ".opus", ".m4a"}
DEFAULT_EPOCHS = 40
DEFAULT_RANK = 16
DEFAULT_MAX_DURATION = 30.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(message: str) -> None:
    print(message, flush=True)


def fail(message: str, code: int = 1) -> int:
    print(f"[FAIL] {message}", file=sys.stderr, flush=True)
    print(f"BTP_LORA_FAILED {message}", flush=True)
    return code


def slugify(name: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9 _-]+", "", name).strip().replace(" ", "_")
    slug = re.sub(r"_+", "_", slug)
    return slug[:60] or f"lora_{int(time.time())}"


def config_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "BtP Oven"
    return Path.home() / "AppData" / "Roaming" / "BtP Oven"


def discover_acestep_root(explicit: str | None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env = os.environ.get("BTP_ACESTEP_ROOT")
    if env:
        candidates.append(Path(env))
    here = Path(__file__).resolve().parent
    candidates += [
        here.parent / "ACE-Step-1.5",
        here.parent.parent / "ACE-Step-1.5",
        Path(r"D:\Dev\ACE-Step-1.5"),
        config_dir() / "ACE-Step-1.5",
    ]

    settings = config_dir() / "lora_settings.json"
    if settings.is_file():
        try:
            data = json.loads(settings.read_text(encoding="utf-8"))
            if data.get("acestep_root"):
                candidates.insert(0, Path(data["acestep_root"]))
        except Exception:
            pass

    for candidate in candidates:
        if (candidate / "train.py").is_file() and (candidate / "checkpoints").is_dir():
            return candidate.resolve()
    return None


def discover_python(root: Path, explicit: str | None) -> str:
    if explicit:
        return explicit

    env = os.environ.get("BTP_LORA_PYTHON")
    if env:
        return env

    settings = config_dir() / "lora_settings.json"
    if settings.is_file():
        try:
            data = json.loads(settings.read_text(encoding="utf-8"))
            if data.get("python"):
                return data["python"]
        except Exception:
            pass

    for candidate in (
        root / ".venv" / "Scripts" / "python.exe",
        root / "venv" / "Scripts" / "python.exe",
        root / ".venv" / "bin" / "python",
    ):
        if candidate.is_file():
            return str(candidate)

    return sys.executable


def default_audio_dir() -> Path:
    return config_dir() / "Training" / "Loops"


def default_lora_dir() -> Path:
    return config_dir() / "LoRAs"


def find_audio_files(folder: Path) -> list[Path]:
    if not folder.is_dir():
        return []
    return sorted(
        p for p in folder.rglob("*")
        if p.is_file() and p.suffix.lower() in AUDIO_EXTENSIONS
    )


def read_sidecar(audio: Path) -> dict:
    sidecar = audio.with_suffix(".json")
    if not sidecar.is_file():
        return {}
    try:
        data = json.loads(sidecar.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def build_dataset(audio_files: list[Path], work_audio: Path, caption_override: str,
                  bpm_override: float | None, genre: str) -> dict:
    work_audio.mkdir(parents=True, exist_ok=True)
    samples = []

    for source in audio_files:
        dest = work_audio / source.name
        stem = dest.stem
        n = 1
        while dest.exists():
            n += 1
            dest = work_audio / f"{stem}_{n}{source.suffix.lower()}"

        shutil.copy2(source, dest)
        meta = read_sidecar(source)

        caption = (caption_override
                   or meta.get("caption")
                   or dest.stem.replace("_", " ").replace("-", " "))
        lyrics = meta.get("lyrics") or "[Instrumental]"
        bpm = bpm_override if bpm_override is not None else meta.get("bpm") or 0

        samples.append({
            "filename": dest.name,
            "audio_path": str(dest),
            "caption": str(caption),
            "lyrics": str(lyrics),
            "genre": str(meta.get("genre") or genre or ""),
            "bpm": int(bpm) if bpm else 0,
            "keyscale": str(meta.get("keyscale") or ""),
            "timesignature": str(meta.get("timesignature") or "4"),
            "duration": float(meta.get("duration") or 0),
            "is_instrumental": bool(meta.get("is_instrumental", lyrics == "[Instrumental]")),
        })

    return {
        "metadata": {"tag_position": "prepend", "genre_ratio": 0, "custom_tag": ""},
        "samples": samples,
    }


def run(command: list[str], cwd: Path, dry_run: bool) -> int:
    printable = " ".join(f'"{c}"' if " " in c else c for c in command)
    log(f"[cmd] {printable}")
    if dry_run:
        return 0
    env = dict(os.environ)
    env["PYTHONUNBUFFERED"] = "1"
    return subprocess.call(command, cwd=str(cwd), env=env)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Train an ACE-Step LoRA from BtP Oven captures.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--name", required=True, help="LoRA name shown in the plugin")
    parser.add_argument("--audio-dir", default=None,
                        help="Folder of wav/mp3/flac/ogg files (default: Training\\Loops)")
    parser.add_argument("--acestep-root", default=None,
                        help="ACE-Step 1.5 checkout (auto-detected)")
    parser.add_argument("--python", default=None,
                        help="Python with ACE-Step dependencies (auto-detected)")
    parser.add_argument("--output-dir", default=None,
                        help="Where the LoRA folder is written (default: %APPDATA%\\BtP Oven\\LoRAs)")
    parser.add_argument("--epochs", type=int, default=DEFAULT_EPOCHS)
    parser.add_argument("--rank", type=int, default=DEFAULT_RANK)
    parser.add_argument("--alpha", type=int, default=None,
                        help="LoRA alpha (default: 2 x rank)")
    parser.add_argument("--batch-size", type=int, default=1)
    parser.add_argument("--grad-accum", type=int, default=4)
    parser.add_argument("--lr", type=float, default=1e-4)
    parser.add_argument("--max-duration", type=float, default=DEFAULT_MAX_DURATION)
    parser.add_argument("--caption", default="",
                        help="Override every sample caption with this text")
    parser.add_argument("--bpm", type=float, default=None,
                        help="Override BPM for every sample")
    parser.add_argument("--genre", default="", help="Optional genre tag")
    parser.add_argument("--device", default="cuda", choices=["cuda", "cpu"])
    parser.add_argument("--precision", default="bf16", choices=["bf16", "fp16", "fp32", "auto"])
    parser.add_argument("--clean", action="store_true",
                        help="Delete the work folder after a successful export")
    parser.add_argument("--dry-run", action="store_true")

    args = parser.parse_args()

    root = discover_acestep_root(args.acestep_root)
    if root is None:
        return fail("ACE-Step 1.5 not found. Pass --acestep-root or set BTP_ACESTEP_ROOT.")

    audio_dir = Path(args.audio_dir) if args.audio_dir else default_audio_dir()
    audio_files = find_audio_files(audio_dir)
    if not audio_files:
        return fail(f"no supported audio found in {audio_dir}")
    if len(audio_files) < 3:
        log(f"[warn] only {len(audio_files)} files; 30-120 loops give much better results")

    python = discover_python(root, args.python)
    if not Path(python).is_file() and python != sys.executable:
        return fail(f"Python not found: {python}")

    name = slugify(args.name)
    output_dir = Path(args.output_dir) if args.output_dir else default_lora_dir()
    output_dir = output_dir / name
    output_dir.mkdir(parents=True, exist_ok=True)

    work = root / "_btp_lora" / name
    work_audio = work / "audio"
    tensors = work / "tensors"
    run_dir = work / "run"
    dataset_json = work / "dataset.json"

    if not args.dry_run and work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True, exist_ok=True)

    log("=" * 62)
    log("  BtP Oven - ACE-Step LoRA training")
    log("=" * 62)
    log(f"  name        : {name}")
    log(f"  audio       : {audio_dir}  ({len(audio_files)} files)")
    log(f"  ACE-Step    : {root}")
    log(f"  python      : {python}")
    log(f"  work        : {work}")
    log(f"  output      : {output_dir}")
    log(f"  epochs      : {args.epochs}")
    log(f"  rank/alpha  : {args.rank} / {args.alpha or args.rank * 2}")
    log(f"  device      : {args.device} {args.precision}")
    log("=" * 62)

    dataset = build_dataset(audio_files, work_audio, args.caption, args.bpm, args.genre)
    dataset_json.write_text(json.dumps(dataset, indent=2), encoding="utf-8")
    log(f"[ok] dataset.json: {len(dataset['samples'])} samples")

    alpha = args.alpha or args.rank * 2
    trainer = root / "train.py"
    base = [python, str(trainer)]
    common = [
        "--checkpoint-dir", str(root / "checkpoints"),
        "--model-variant", "turbo",
        "--device", args.device,
        "--precision", "auto" if args.precision == "auto" else args.precision,
    ]

    log("")
    log("BTP_LORA_STAGE preprocess")
    rc = run(base + ["fixed", *common,
                     "--dataset-dir", str(tensors),
                     "--output-dir", str(run_dir),
                     "--preprocess",
                     "--dataset-json", str(dataset_json),
                     "--tensor-output", str(tensors),
                     "--max-duration", str(args.max_duration)],
             root, args.dry_run)

    if rc != 0:
        return fail("preprocessing failed - see the console above", rc)

    log("")
    log("BTP_LORA_STAGE train")
    rc = run(base + ["-y", "--plain", "fixed", *common,
                     "--dataset-dir", str(tensors),
                     "--output-dir", str(run_dir),
                     "--epochs", str(args.epochs),
                     "--rank", str(args.rank),
                     "--alpha", str(alpha),
                     "--batch-size", str(args.batch_size),
                     "--gradient-accumulation", str(args.grad_accum),
                     "--lr", str(args.lr),
                     "--save-every", str(max(1, args.epochs)),
                     "--num-workers", "0",
                     "--prefetch-factor", "0",
                     "--no-pin-memory",
                     "--no-persistent-workers",
                     "--optimizer-type", "adamw",
                     "--scheduler-type", "cosine",
                     "--shift", "3",
                     "--num-inference-steps", "8",
                     "--sample-every-n-epochs", "0"],
             root, args.dry_run)

    if rc != 0:
        return fail("training failed - see the console above", rc)

    final_dir = run_dir / "final"
    adapter = final_dir / "adapter_model.safetensors"
    config = final_dir / "adapter_config.json"

    if not args.dry_run and (not adapter.is_file() or not config.is_file()):
        return fail(f"trainer finished but {final_dir} has no adapter files")

    if args.dry_run:
        log("[dry-run] would copy the adapter into the LoRA folder")
        return 0

    shutil.copy2(adapter, output_dir / "adapter_model.safetensors")
    shutil.copy2(config, output_dir / "adapter_config.json")

    info = {
        "name": name,
        "created": time.strftime("%Y-%m-%d %H:%M:%S"),
        "samples": len(dataset["samples"]),
        "epochs": args.epochs,
        "rank": args.rank,
        "alpha": alpha,
        "base_model": "acestep-v15-turbo",
        "acestep_root": str(root),
        "audio_dir": str(audio_dir),
        "caption": args.caption or "(from sidecars / filenames)",
        "bpm": args.bpm,
        "files": [s["filename"] for s in dataset["samples"]],
    }
    (output_dir / "lora_info.json").write_text(json.dumps(info, indent=2), encoding="utf-8")

    log("")
    log("=" * 62)
    log("  LORA READY")
    log("=" * 62)
    log(f"  {output_dir}")
    log("  Open the AI page in BtP Oven and pick it in the LORA list.")
    log(f"BTP_LORA_DONE {output_dir}")

    if args.clean:
        shutil.rmtree(work, ignore_errors=True)
        log("[ok] work folder removed")

    return 0


if __name__ == "__main__":
    sys.exit(main())
