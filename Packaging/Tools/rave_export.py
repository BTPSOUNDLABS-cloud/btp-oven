#!/usr/bin/env python3
"""
BtP Oven - RAVE model exporter

Converts a RAVE .ckpt checkpoint into the three files the plugin needs:

    rave_encoder.onnx     multiband [1, M, T] -> latent [1, 128, T']
    rave_decoder.onnx     latent    [1, 128, T'] -> multiband [1, M, T]
    pqmf_hk.bin           the PQMF filter bank (raw float32)

The decoder is exported WITHOUT the FFT noise module because ONNX cannot
represent aten::view_as_complex. Timbre mutation still works; the very
top-end noise layer is omitted.

REQUIREMENTS
  * Python 3.10
  * torch, gin-config, librosa, scipy, einops, resampy
  * the RAVE v3 fork (victor-shepardson/RAVE)
  * the matching cached_conv fork (victor-shepardson/cached_conv)
  * the checkpoint's config.gin

USAGE
  python rave_export.py ^
      --checkpoint D:\\rave\\voice_multi.ckpt ^
      --config     D:\\rave\\config.gin ^
      --out        "%APPDATA%\\BtP Oven\\Models\\RAVE"

  If the fork / cached_conv / deps are not importable, point at them:
      --rave-fork D:\\Dev\\rave-fork
      --cached-conv D:\\Dev\\cached_conv-fork
      --deps D:\\Dev\\rave-pkgs

NOTES
  * The checkpoint must match config.gin. Using the wrong gin produces a
    size mismatch when loading the state dict.
  * Model weights keep their upstream license (voice_multi is CC-BY-NC 4.0).
"""

import argparse
import os
import sys
import traceback
from pathlib import Path


def add_paths(args):
    # order: deps first so torch/gin resolve, then the forks
    for p in (args.deps, args.rave_fork, args.cached_conv):
        if p and Path(p).is_dir():
            sys.path.insert(0, p)


def main():
    ap = argparse.ArgumentParser(description="Export a RAVE checkpoint to ONNX for BtP Oven.")
    ap.add_argument("--checkpoint", required=True, help="path to the RAVE .ckpt")
    ap.add_argument("--config", required=True, help="path to the matching config.gin")
    ap.add_argument("--out", required=True, help="output folder (rave_encoder.onnx, rave_decoder.onnx, pqmf_hk.bin)")
    ap.add_argument("--deps", default=os.environ.get("BTP_RAVE_DEPS", ""),
                    help="folder with torch/gin/librosa packages")
    ap.add_argument("--rave-fork", default=os.environ.get("BTP_RAVE_FORK", ""),
                    help="path to the victor-shepardson/RAVE checkout")
    ap.add_argument("--cached-conv", default=os.environ.get("BTP_CACHED_CONV", ""),
                    help="path to the victor-shepardson/cached_conv checkout")
    ap.add_argument("--latent", type=int, default=0, help="override latent size (0 = from config)")
    args = ap.parse_args()

    add_paths(args)

    import gin
    import numpy as np
    import torch
    import torch.nn as nn

    # importing rave registers the gin configurables
    import rave  # noqa: F401
    from rave.model import RAVE

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"[rave] config    : {args.config}")
    gin.parse_config_file(args.config)

    model = RAVE()
    model.eval()
    for p in model.parameters():
        p.requires_grad_(False)

    ckpt = torch.load(args.checkpoint, map_location="cpu")
    state = ckpt.get("state_dict", ckpt)
    missing, unexpected = model.load_state_dict(state, strict=False)
    print(f"[rave] missing: {len(missing)}   unexpected: {len(unexpected)}")
    if missing:
        print("[rave] WARNING: missing keys mean the config.gin does not match the checkpoint")

    latent = args.latent or model.latent_size
    bands = model.pqmf.n_band
    print(f"[rave] latent_size={latent}  n_bands={bands}  n_channels={model.n_channels}")

    hk = model.pqmf.hk.detach().cpu().numpy().astype(np.float32)
    print(f"[rave] pqmf filters: {hk.shape}")

    # ---- verify with the classic (non-FFT) PQMF --------------------------
    from rave.pqmf import classic_forward, classic_inverse

    x = torch.randn(1, 1, 48000)
    with torch.no_grad():
        mb = classic_forward(x, torch.from_numpy(hk))
        z = model.encoder(mb)
        mu = z[:, :latent]
        mb2 = model.decoder.net(mu)
        mb2 = model.decoder.waveform_module(mb2)
        if model.decoder.amplitude_modulation:
            a, b = mb2.split(mb2.shape[1] // 2, 1)
            mb2 = a * torch.sigmoid(b)
        y = classic_inverse(mb2, torch.from_numpy(hk))
    print(f"[rave] round trip: {tuple(x.shape)} -> {tuple(mu.shape)} -> {tuple(y.shape)}")

    # ---- encoder ---------------------------------------------------------
    class Enc(nn.Module):
        def forward(self, m):
            return model.encoder(m)[:, :latent]

    class Dec(nn.Module):
        def forward(self, z):
            y = model.decoder.net(z)
            y = model.decoder.waveform_module(y)
            if model.decoder.amplitude_modulation:
                a, b = y.split(y.shape[1] // 2, 1)
                y = a * torch.sigmoid(b)
            return y

    enc = Enc()
    dec = Dec()

    dummy_mb = torch.randn(1, bands, 4096)
    dummy_mu = torch.randn(1, latent, 8)

    with torch.no_grad():
        torch.onnx.export(
            enc, (dummy_mb,), str(out_dir / "rave_encoder.onnx"),
            input_names=["multiband"], output_names=["latent"],
            dynamic_axes={"multiband": [2], "latent": [2]},
            opset_version=13, dynamo=False)

        torch.onnx.export(
            dec, (dummy_mu,), str(out_dir / "rave_decoder.onnx"),
            input_names=["latent"], output_names=["multiband"],
            dynamic_axes={"latent": [2], "multiband": [2]},
            opset_version=13, dynamo=False)

    # ---- pqmf filter bank ------------------------------------------------
    with open(out_dir / "pqmf_hk.bin", "wb") as f:
        f.write(np.int32(hk.shape[0]).tobytes())
        f.write(np.int32(hk.shape[1]).tobytes())
        f.write(hk.tobytes())

    print("[rave] EXPORT OK")
    print(f"[rave] -> {out_dir}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception:
        traceback.print_exc()
        sys.exit(1)
