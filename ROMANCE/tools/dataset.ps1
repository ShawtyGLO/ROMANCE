param(
    [int]$N = 200000,
    [string]$Sortie = "",
    [int]$Graine = 1
)
# =============================================================================
#  dataset.ps1 - Compile et lance la fabrication du jeu d'entrainement.
#
#  Le generateur est la verite : chaque exemple est une paire (empreinte,
#  108 octets) dont l'etiquette est exacte. Le prior vient du corpus mesure.
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
if (-not $Sortie) { $Sortie = Join-Path (Join-Path $root 'corpus') 'train.bin' }

$src = Join-Path $root 'outils\dataset.cpp'
$inc = Join-Path $root 'Source'
$inc2 = Join-Path $root 'outils'
$out = Join-Path $env:TEMP 'romance_dataset'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

Write-Host ""
Write-Host "ROMANCE - jeu d.entrainement" -ForegroundColor Cyan
Write-Host "-------------------------" -ForegroundColor Cyan

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" /I"$inc2" "$src" /Fe:"$out\dataset.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }

& "$out\dataset.exe" $Sortie $N $Graine

