#requires -version 5.1
<#
    BtP Oven - installer
    ------------------------------------------------------------------
    Installs the VST3 (and optionally the standalone app), creates the
    Training / Models folders, and optionally copies or downloads the AI
    models.

    USAGE (normal):
        Right-click INSTALL.cmd -> Run as administrator
        (or)  powershell -ExecutionPolicy Bypass -File Install-BtPOven.ps1

    OPTIONS:
        -NoModels          Do not copy or download any models
        -DownloadModels    Download missing models from the manifest
        -NoLM              Skip the optional 4B lyric/vocal LM (~4.5 GB)
        -NoStandalone      Do not install the standalone app
        -NoShortcut        Do not create a desktop shortcut
        -SystemVST3        Force the machine-wide VST3 folder (needs admin)
        -UserVST3          Force the per-user VST3 folder
        -NoPause           Do not wait for a key at the end
#>

param(
    [switch]$NoModels,
    [switch]$DownloadModels,
    [switch]$NoLM,
    [switch]$NoStandalone,
    [switch]$NoShortcut,
    [switch]$SystemVST3,
    [switch]$UserVST3,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$script:hadError = $false

function Say  ($m) { Write-Host $m }
function Ok   ($m) { Write-Host "  [ok]   $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "  [warn] $m" -ForegroundColor Yellow }
function Fail ($m) { Write-Host "  [fail] $m" -ForegroundColor Red; $script:hadError = $true }

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$appData = Join-Path $env:APPDATA 'BtP Oven'
$modelsDir = Join-Path $appData 'Models'
$trainingDir = Join-Path $appData 'Training'

Say ''
Say '============================================================'
Say '  BtP Oven - installer'
Say '============================================================'
Say "  source : $here"
Say "  config : $appData"
Say ''

# ---------------------------------------------------------------- helpers
function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Download-File ($url, $dest, [int64]$expected) {
    $dir = Split-Path -Parent $dest
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    if ((Test-Path $dest) -and $expected -gt 0) {
        $len = (Get-Item $dest).Length
        if ($len -eq $expected) { Ok "already present: $(Split-Path -Leaf $dest)"; return }
        if ($len -gt 0) { Warn "partial file, resuming: $(Split-Path -Leaf $dest)" }
    }

    Say "  ... downloading $(Split-Path -Leaf $dest)"
    & curl.exe -L --fail --retry 3 -C - -o "$dest" "$url"
    if ($LASTEXITCODE -ne 0) { throw "download failed ($LASTEXITCODE): $url" }

    if ($expected -gt 0) {
        $len = (Get-Item $dest).Length
        if ($len -ne $expected) { Warn "size mismatch for $(Split-Path -Leaf $dest): got $len, expected $expected" }
    }
    Ok "downloaded: $(Split-Path -Leaf $dest)"
}

# ---------------------------------------------------------------- VST3
Say '[1/5] Installing the VST3 plugin'
$vst3Src = Join-Path $here 'Payload\BtP Oven.vst3'
if (-not (Test-Path $vst3Src)) {
    # dev-tree fallback
    $alt = Join-Path $here '..\build\BtPOven_artefacts\Release\VST3\BtP Oven.vst3'
    if (Test-Path $alt) { $vst3Src = (Resolve-Path $alt).Path }
}
if (-not (Test-Path $vst3Src)) { Fail 'could not find BtP Oven.vst3 in Payload\'; }
else {
    $isAdmin = Test-Admin
    $destParent = $null

    if ($UserVST3) {
        $destParent = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
    } elseif ($SystemVST3 -or $isAdmin) {
        $destParent = Join-Path $env:CommonProgramFiles 'VST3'
    } else {
        $destParent = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
    }

    New-Item -ItemType Directory -Force -Path $destParent | Out-Null

    try {
        $target = Join-Path $destParent 'BtP Oven.vst3'
        if (Test-Path $target) { Remove-Item $target -Recurse -Force }
        Copy-Item $vst3Src $destParent -Recurse -Force
        Ok "VST3 -> $target"
        if (-not $isAdmin -and -not $UserVST3) {
            Warn 'installed for the current user. Run as administrator to install machine-wide.'
        }
    } catch {
        Fail "could not write to $destParent : $($_.Exception.Message)"
        Warn 'Close FL Studio / your DAW and run INSTALL.cmd as administrator, then retry.'
    }
}

# ---------------------------------------------------------------- standalone
Say ''
Say '[2/5] Installing the standalone app'
if ($NoStandalone) { Warn 'skipped (-NoStandalone)' }
else {
    $standaloneSrc = Join-Path $here 'Payload\Standalone'
    if (-not (Test-Path $standaloneSrc)) {
        $alt = Join-Path $here '..\build\BtPOven_artefacts\Release\Standalone'
        if (Test-Path $alt) { $standaloneSrc = (Resolve-Path $alt).Path }
    }

    if (-not (Test-Path $standaloneSrc)) { Warn 'standalone payload not found, skipping' }
    else {
        $standaloneDest = Join-Path $env:LOCALAPPDATA 'Programs\BtP Oven'
        New-Item -ItemType Directory -Force -Path $standaloneDest | Out-Null
        Copy-Item (Join-Path $standaloneSrc '*') $standaloneDest -Recurse -Force
        Ok "Standalone -> $standaloneDest"
    }
}

# ---------------------------------------------------------------- folders
Say ''
Say '[3/5] Creating Training / Models folders'
$commercialEdition = Test-Path (Join-Path $here 'NO-RAVE.txt')
$dirs = @(
    $modelsDir,
    (Join-Path $modelsDir 'ACE-Step'),
    (Join-Path $modelsDir 'Demucs'),
    (Join-Path $modelsDir 'RVC\hubert'),
    (Join-Path $modelsDir 'RVC\voices'),
    (Join-Path $modelsDir 'RVC\rmvpe'),
    (Join-Path $trainingDir 'Vocals'),
    (Join-Path $trainingDir 'Loops'),
    (Join-Path $trainingDir 'Variants'),
    (Join-Path $trainingDir 'Stems\drums'),
    (Join-Path $trainingDir 'Stems\bass'),
    (Join-Path $trainingDir 'Stems\other'),
    (Join-Path $trainingDir 'Stems\vocals'),
    (Join-Path $trainingDir 'runs'),
    (Join-Path $trainingDir 'manifests'),
    (Join-Path $appData 'LoRAs'),
    (Join-Path $appData 'Tools')
)
foreach ($cat in @('FX','Impacts','Risers','Pads','Vocal Chops','Sirens','Phones','Cash','Ambience','Synth One Shots')) {
    $dirs += (Join-Path $trainingDir "OneShots\$cat")
}
if (-not $commercialEdition) { $dirs += (Join-Path $modelsDir 'RAVE') }
foreach ($d in $dirs) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
Ok 'folders ready'
if ($commercialEdition) { Ok 'commercial edition: RAVE variant engine not installed' }

# copy the example manifests next to the training data
$manifestSrc = Join-Path $here 'manifests'
if (Test-Path $manifestSrc) {
    Copy-Item (Join-Path $manifestSrc '*.json') (Join-Path $trainingDir 'manifests') -Force
    Ok 'example training manifests copied'
}

# copy the LoRA trainer wrapper so the AI page can launch it
$toolsSrc = Join-Path $here 'Tools'
$toolsDest = Join-Path $appData 'Tools'
if (Test-Path $toolsSrc) {
    New-Item -ItemType Directory -Force -Path $toolsDest | Out-Null
    Get-ChildItem $toolsSrc -File | ForEach-Object {
        Copy-Item $_.FullName $toolsDest -Force
    }
    Ok "training tools -> $toolsDest"
}

# legal notices stay with the install
$legalDir = Join-Path $appData 'Legal'
New-Item -ItemType Directory -Force -Path $legalDir | Out-Null
foreach ($legal in @('LICENSE-BtPOven.txt','LICENSING.txt','THIRD-PARTY.txt','BUILD-VARIANTS.txt')) {
    $src = Join-Path $here $legal
    if (Test-Path $src) { Copy-Item $src $legalDir -Force }
}
$licSrc = Join-Path $here 'LICENSES'
if (Test-Path $licSrc) {
    Copy-Item $licSrc (Join-Path $legalDir 'LICENSES') -Recurse -Force
    Ok 'licence texts copied'
}

# ---------------------------------------------------------------- models
Say ''
Say '[4/5] Installing models'
if ($NoModels) {
    Warn 'skipped (-NoModels)'
} else {
    $bundled = Join-Path $here 'Models'
    if (Test-Path $bundled) {
        Say '  copying bundled models (this can take a few minutes)...'
        Get-ChildItem $bundled -Directory | ForEach-Object {
            $dst = Join-Path $modelsDir $_.Name
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            Copy-Item (Join-Path $_.FullName '*') $dst -Recurse -Force
        }
        Ok 'bundled models copied'
    }

    if ($DownloadModels) {
        $manifestPath = Join-Path $here 'models_manifest.json'
        if (-not (Test-Path $manifestPath)) { Warn 'models_manifest.json missing, cannot download' }
        else {
            $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
            foreach ($m in $manifest.models) {
                if ($m.bundle_only -eq $true -or [string]::IsNullOrEmpty($m.url)) {
                    $dest = Join-Path $modelsDir $m.dest
                    if (Test-Path $dest) { Ok "already present: $($m.file)" }
                    else { Warn "not downloadable, ship this file in Models\$($m.dest)" }
                    continue
                }
                if ($NoLM -and $m.file -like '*5Hz-lm*') { Warn "skipped LM: $($m.file)"; continue }
                if ($m.required -eq $false -and $m.file -like '*rmvpe*') { continue }
                Download-File $m.url (Join-Path $modelsDir $m.dest) ([int64]$m.size)
            }
        }
    }

    # report what is still missing
    $need = @{
        'ACE-Step\acestep-v15-turbo-Q8_0.gguf' = 'AI GEN / SHOTS (instrumental)'
        'ACE-Step\Qwen3-Embedding-0.6B-Q8_0.gguf' = 'AI GEN / SHOTS'
        'ACE-Step\vae-BF16.gguf' = 'AI GEN / SHOTS'
        'Demucs\ggml-model-htdemucs-4s-f16.bin' = 'STEM'
        'RVC\hubert\vec-768-layer-12.onnx' = 'VOICE'
    }
    if (-not $commercialEdition) {
        $need['RAVE\rave_encoder.onnx'] = 'SHOTS -> MUTATE'
        $need['RAVE\rave_decoder.onnx'] = 'SHOTS -> MUTATE'
        $need['RAVE\pqmf_hk.bin'] = 'SHOTS -> MUTATE'
    }
    $missing = @()
    foreach ($k in $need.Keys) {
        if (-not (Test-Path (Join-Path $modelsDir $k))) { $missing += "$k  ($($need[$k]))" }
    }
    if ($missing.Count -gt 0) {
        Say ''
        Warn 'These model files are still missing:'
        $missing | ForEach-Object { Say "         - $_" }
        Say '       Re-run with -DownloadModels, or copy them into:'
        Say "         $modelsDir"
    } else {
        Ok 'all core models present'
    }
}

# ---------------------------------------------------------------- shortcut
Say ''
Say '[5/5] Finishing up'
$standaloneExe = Join-Path $env:LOCALAPPDATA 'Programs\BtP Oven\BtP Oven.exe'
if (-not $NoShortcut -and (Test-Path $standaloneExe)) {
    try {
        $desktop = [Environment]::GetFolderPath('Desktop')
        $ws = New-Object -ComObject WScript.Shell
        $lnk = $ws.CreateShortcut((Join-Path $desktop 'BtP Oven.lnk'))
        $lnk.TargetPath = $standaloneExe
        $lnk.WorkingDirectory = Split-Path $standaloneExe
        $lnk.Description = 'BtP Oven - AI music factory'
        $lnk.Save()
        Ok 'desktop shortcut created'
    } catch { Warn "could not create shortcut: $($_.Exception.Message)" }
}

# install log
$log = @()
$log += "BtP Oven install log"
$log += "date: $(Get-Date -Format o)"
$log += "source: $here"
$log += "models: $modelsDir"
$log += "training: $trainingDir"
$log += "legal: $(Join-Path $appData 'Legal')"
$log += "vst3 candidates:"
$log += "  $(Join-Path $env:CommonProgramFiles 'VST3\BtP Oven.vst3')"
$log += "  $(Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\BtP Oven.vst3')"
$log | Set-Content (Join-Path $appData 'install_log.txt') -Encoding UTF8

Say ''
Say '============================================================'
if ($script:hadError) { Say '  INSTALL FINISHED WITH ERRORS - see messages above' -ForegroundColor Yellow }
else { Say '  INSTALL COMPLETE' -ForegroundColor Green }
Say '============================================================'
Say ''
Say 'NEXT:'
Say '  1. Open your DAW and rescan plugins, or launch "BtP Oven" from the desktop.'
Say '  2. Read README.txt and TRAINING-GUIDE.txt.'
Say "  3. Training audio goes in: $trainingDir"
Say ''

if (-not $NoPause) {
    Write-Host 'Press any key to close...'
    try { $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') } catch { Start-Sleep -Seconds 5 }
}
