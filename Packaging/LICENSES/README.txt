================================================================
  BtP OVEN  -  LICENCE TEXTS
================================================================

Full licence texts for every third-party component used by, or
bundled with, BtP Oven. The plain-English summary is in
..\LICENSING.txt and the inventory is in ..\THIRD-PARTY.txt.

  FILE                              COMPONENT
  ----------------------------------------------------------------
  AGPL-3.0.txt                      JUCE 9 (open-source option)
  JUCE-LICENSE.md                   JUCE 9 (dual-licence notice)
  GPL-3.0.txt                       reference only (not used by
                                    any shipped component)
  Apache-2.0.txt                    ACE-Step framework
  ACE-Step-LICENSE.txt              ACE-Step upstream notice
  MIT-ggml.txt                      ggml
  MIT-yyjson.txt                    yyjson
  MIT-demucs.txt                    Demucs (facebookresearch)
  MIT-demucs-cpp.txt                demucs.cpp (sevagh)
  MPL-2.0.txt                       Eigen
  MIT-onnxruntime.txt               ONNX Runtime
  MIT-RVC.txt                       RVC
  MIT-ContentVec.txt                ContentVec / HuBERT
  RAVE-LICENSE-CC-BY-NC-4.0.txt     RAVE upstream notice
  CC-BY-NC-4.0.txt                  RAVE code + voice_multi weights

RAVE note: the RAVE files only apply to builds that include the RAVE
variant engine. A commercial build (compiled with
-DBTP_ENABLE_RAVE=OFF and packaged with -NoRave) contains no RAVE
code or weights, so those entries simply do not apply to it. The
files are kept here so the folder is identical in both packages.
See ..\BUILD-VARIANTS.txt.

JUCE note: JUCE itself vendors further third-party code. The full
SPDX inventory ships in the JUCE repository as JUCE.spdx.json.

Keep this folder intact when you redistribute the plugin.
================================================================
