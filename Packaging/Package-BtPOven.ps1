#requires -version 5.1
<#
    BtP Oven - release packager
    ------------------------------------------------------------------
    Builds a send-ready folder (and optionally a .zip) containing:

        BtP-Oven-<version>\
            INSTALL.cmd
            Install-BtPOven.ps1
            README.txt
            TRAINING-GUIDE.txt
            NOTEBOOK-LAYOUT.txt
            models_manifest.json
            manifests\*.json
            Tools\...
            Payload\BtP Oven.vst3\...
            Payload\Standalone\...
            Models\...            (optional)

    USAGE:
        powershell -ExecutionPolicy Bypass -File Package-BtPOven.ps1
        powershell ... -Models All -Zip
        powershell ... -Models None -Version "1.0.1"

        Commercial build (no RAVE, no non-commercial models):
        powershell ... -Models Core -NoRave -Zip -BuildDir build-commercial

    MODELS:
        None   no model files in the package (installer can download them)
        Core   Demucs + RVC hubert + RAVE exports   (~0.9 GB)
        All    Core + ACE-Step GGUF files           (~8.6 GB)

    -NoRave
        Omits the RAVE encoder/decoder/PQMF models and marks the package
        as a commercial edition. Pair it with a plugin built using
        -DBTP_ENABLE_RAVE=OFF. The installed copy then contains no
        CC-BY-NC code or weights.

    -BuildDir
        Which build tree to package from. Default 'build'. Use
        -BuildDir build-commercial for the no-RAVE build.

    NOTE: voices are never bundled by default (licensing). Use -IncludeVoices
          if you have redistribution rights for the .onnx files in
          %APPDATA%\BtP Oven\Models\RVC\voices.
#>

param(
    [ValidateSet('None','Core','All')]
    [string]$Models = 'Core',
    [string]$Version = '1.0.0',
    [string]$OutDir = '',
    [string]$BuildDir = 'build',
    [switch]$Zip,
    [switch]$IncludeVoices,
    [switch]$NoRave
)

$ErrorActionPreference = 'Stop'

function Say  ($m) { Write-Host $m }
function Ok   ($m) { Write-Host "  [ok]   $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "  [warn] $m" -ForegroundColor Yellow }

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# The script may live in the project root or in a Packaging\ sub-folder.
# $proj is the folder that owns CMakeLists.txt and build\.
$proj = $root
if (-not (Test-Path (Join-Path $proj 'CMakeLists.txt'))) {
    $parent = Split-Path -Parent $root
    if (Test-Path (Join-Path $parent 'CMakeLists.txt')) { $proj = $parent }
}

if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $proj 'dist' }

$releaseName = "BtP-Oven-$Version"
$stage = Join-Path $OutDir $releaseName
$payload = Join-Path $stage 'Payload'
$modelsStage = Join-Path $stage 'Models'

Say ''
Say '============================================================'
Say "  Packaging BtP Oven $Version"
Say "  models: $Models"
Say '============================================================'
Say ''

# ---------------------------------------------------------------- clean
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $payload | Out-Null

# ---------------------------------------------------------------- payload
Say '[1/4] Copying build artifacts'

$vst3 = Join-Path $proj "$BuildDir\BtPOven_artefacts\Release\VST3\BtP Oven.vst3"
$standalone = Join-Path $proj "$BuildDir\BtPOven_artefacts\Release\Standalone"

if (-not (Test-Path $vst3)) { throw "VST3 not found: $vst3  (build Release first, or pass -BuildDir)" }
Copy-Item $vst3 $payload -Recurse -Force
Ok 'VST3 bundle copied'

if (Test-Path $standalone) {
    Copy-Item $standalone (Join-Path $payload 'Standalone') -Recurse -Force
    Ok 'Standalone copied'
} else {
    Warn 'standalone build not found (VST3-only package)'
}

# ---------------------------------------------------------------- docs
Say ''
Say '[2/4] Copying installer, docs and manifests'

$files = @(
    'INSTALL.cmd',
    'Install-BtPOven.ps1',
    'README.txt',
    'TRAINING-GUIDE.txt',
    'NOTEBOOK-LAYOUT.txt',
    'BUILD-VARIANTS.txt',
    'LICENSING.txt',
    'THIRD-PARTY.txt',
    'models_manifest.json'
)
foreach ($f in $files) {
    $src = Join-Path $root $f
    if (Test-Path $src) { Copy-Item $src $stage -Force; Ok $f }
    else { Warn "missing: $f" }
}

$licSrc = Join-Path $root 'LICENSES'
if (Test-Path $licSrc) {
    Copy-Item $licSrc (Join-Path $stage 'LICENSES') -Recurse -Force
    Ok 'LICENSES'
}

# the plugin's own EULA
$eula = Join-Path $proj 'LICENSE'
if (Test-Path $eula) {
    Copy-Item $eula (Join-Path $stage 'LICENSE-BtPOven.txt') -Force
    Ok 'LICENSE-BtPOven.txt'
} else {
    Warn 'missing: LICENSE'
}

$manifestSrc = Join-Path $root 'manifests'
if (Test-Path $manifestSrc) {
    Copy-Item $manifestSrc (Join-Path $stage 'manifests') -Recurse -Force
    Ok 'training manifests'
}

$toolsSrc = Join-Path $proj 'Tools'
if (-not (Test-Path $toolsSrc)) { $toolsSrc = Join-Path $root 'Tools' }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Tools') | Out-Null
if (Test-Path $toolsSrc) {
    Get-ChildItem $toolsSrc -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $stage 'Tools') -Force
    }
    Ok 'tools'
}
$trainScript = Join-Path $proj 'train_voice.py'
if (-not (Test-Path $trainScript)) { $trainScript = Join-Path $root '..\train_voice.py' }
if (Test-Path $trainScript) {
    Copy-Item $trainScript (Join-Path $stage 'Tools\train_voice.py') -Force
    Ok 'train_voice.py'
}

# empty voice + training folders so the layout is obvious
$skeleton = @(
    'Models\ACE-Step', 'Models\Demucs', 'Models\RVC\hubert', 'Models\RVC\voices',
    'Models\RVC\rmvpe', 'Models\RAVE',
    'Training\Vocals', 'Training\Loops', 'Training\Variants',
    'Training\Stems\drums', 'Training\Stems\bass', 'Training\Stems\other', 'Training\Stems\vocals',
    'Training\runs', 'Training\manifests'
)
foreach ($cat in @('FX','Impacts','Risers','Pads','Vocal Chops','Sirens','Phones','Cash','Ambience','Synth One Shots')) {
    $skeleton += (Join-Path 'Training\OneShots' $cat)
}
if ($NoRave) { $skeleton = $skeleton | Where-Object { $_ -ne 'Models\RAVE' } }
foreach ($d in $skeleton) { New-Item -ItemType Directory -Force -Path (Join-Path $stage $d) | Out-Null }

# ---------------------------------------------------------------- models
Say ''
Say '[3/4] Adding models'

$modelRoots = @(
    (Join-Path $env:APPDATA 'BtP Oven\Models'),
    (Join-Path $proj 'Models'),
    (Join-Path $root 'Models')
) | Where-Object { Test-Path $_ }

function Find-Model ($relative, $legacy) {
    foreach ($r in $modelRoots) {
        $p = Join-Path $r $relative
        if (Test-Path $p) { return $p }
    }
    if ($legacy -and (Test-Path $legacy)) { return $legacy }
    return $null
}

function Add-Model ($relative, $legacy, $required) {
    $src = Find-Model $relative $legacy
    if (-not $src) {
        if ($required) { Warn "MISSING model: $relative" }
        return
    }
    $dst = Join-Path $modelsStage $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item $src $dst -Force
    $mb = [math]::Round((Get-Item $dst).Length / 1MB, 1)
    Ok "$relative  ($mb MB)"
}

if ($Models -ne 'None') {
    # Demucs
    Add-Model 'Demucs\ggml-model-htdemucs-4s-f16.bin' 'D:\Dev\demucs-models\ggml-model-htdemucs-4s-f16.bin' $true

    # RVC (ContentVec is required; RMVPE optional)
    Add-Model 'RVC\hubert\vec-768-layer-12.onnx' 'D:\Dev\rvc-models\hubert\vec-768-layer-12.onnx' $true
    Add-Model 'RVC\rmvpe\rmvpe.onnx' 'D:\Dev\rvc-models\rmvpe\rmvpe.onnx' $false

    if ($IncludeVoices) {
        $voices = Join-Path $env:APPDATA 'BtP Oven\Models\RVC\voices'
        if (-not (Test-Path $voices)) { $voices = 'D:\Dev\rvc-models\voices' }
        if (Test-Path $voices) {
            Get-ChildItem $voices -Filter '*.onnx' | ForEach-Object {
                Copy-Item $_.FullName (Join-Path $modelsStage 'RVC\voices') -Force
                Ok "RVC voice: $($_.Name)"
            }
        }
    }

    # RAVE exports (CC-BY-NC 4.0 - skipped in commercial builds)
    if (-not $NoRave) {
        Add-Model 'RAVE\rave_encoder.onnx' 'D:\Dev\rave-models\rave_encoder.onnx' $true
        Add-Model 'RAVE\rave_decoder.onnx' 'D:\Dev\rave-models\rave_decoder.onnx' $true
        Add-Model 'RAVE\pqmf_hk.bin' 'D:\Dev\rave-models\pqmf_hk.bin' $true
    }
}

if ($NoRave) {
    $marker = @"
BtP Oven - commercial edition
------------------------------

This package was built WITHOUT the RAVE variant engine.

  * SHOTS -> MUTATE / SAVE VAR / TRAIN VAR are not present.
  * No CC-BY-NC code or model weights are included.
  * Everything else (ACE-Step, Demucs, RVC, FX, auto-tune) is unaffected.

The matching plugin binary must be compiled with -DBTP_ENABLE_RAVE=OFF.
See BUILD-VARIANTS.txt for how to add RAVE back to a personal build.
"@
    Set-Content -Path (Join-Path $stage 'NO-RAVE.txt') -Value $marker -Encoding UTF8
    Ok 'commercial edition marker (NO-RAVE.txt)'
}

if ($Models -eq 'All') {
    Add-Model 'ACE-Step\acestep-v15-turbo-Q8_0.gguf' 'D:\Dev\ACE-Step-1.5\acestep.vst3\models\acestep-v15-turbo-Q8_0.gguf' $true
    Add-Model 'ACE-Step\Qwen3-Embedding-0.6B-Q8_0.gguf' 'D:\Dev\ACE-Step-1.5\acestep.vst3\models\Qwen3-Embedding-0.6B-Q8_0.gguf' $true
    Add-Model 'ACE-Step\vae-BF16.gguf' 'D:\Dev\ACE-Step-1.5\acestep.vst3\models\vae-BF16.gguf' $true
    Add-Model 'ACE-Step\acestep-5Hz-lm-4B-Q8_0.gguf' 'D:\Dev\ACE-Step-1.5\acestep.vst3\models\acestep-5Hz-lm-4B-Q8_0.gguf' $false
}

if ($Models -eq 'None') {
    Warn 'no models included; the installer will need -DownloadModels or manual files'
}

# ---------------------------------------------------------------- summary
Say ''
Say '[4/4] Summary'
$size = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
Say ("  folder : {0}" -f $stage)
Say ("  size   : {0:N2} GB" -f ($size / 1GB))

if ($Zip) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $zipPath = Join-Path $OutDir "$releaseName.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    Say "  zipping -> $zipPath"
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        & tar.exe -a -c -f "$zipPath" -C "$OutDir" "$releaseName"
        if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }
    } else {
        Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal
    }
    $mb = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
    Ok "zip: $zipPath  ($mb MB)"
}

Say ''
Say 'DONE. Send the folder (or zip) to your testers.'
Say ''
Say 'They run INSTALL.cmd (right-click -> Run as administrator),'
Say 'or run Install-BtPOven.ps1 -DownloadModels if the zip has no models.'
Say ''
