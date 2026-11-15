param(
    [string]$Dossier = "D:\1 Plugin Vibecodding\ROMANCEPROTO VST",
    [int]$Budget = 3000
)
# =============================================================================
#  resynth.ps1 - Compile et lance l'outil de resynthese sur un dossier de .wav.
#
#  Il utilise EXACTEMENT les en-tetes du plugin (Recette.h, Grain.h, Perte.h) :
#  ce qu'on optimise ici est ce qu'on entendra la-bas.
#
#  Usage :
#    powershell -ExecutionPolicy Bypass -File tools\resynth.ps1
#    powershell -ExecutionPolicy Bypass -File tools\resynth.ps1 -Budget 12000
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$src   = Join-Path $root 'outils\resynth.cpp'
$inc   = Join-Path $root 'Source'
$out   = Join-Path $env:TEMP 'romance_resynth'

if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

Write-Host ""
Write-Host "ROMANCE - resynthese" -ForegroundColor Cyan
Write-Host "--------------------" -ForegroundColor Cyan

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" "$src" /Fe:"$out\resynth.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }

$wavs = Get-ChildItem $Dossier -Filter *.wav -File |
        Where-Object { $_.Name -notlike '* - resynth *' }
if (-not $wavs) { throw "Aucun .wav dans $Dossier" }

foreach ($w in $wavs) {
    & "$out\resynth.exe" $w.FullName $Budget
}
Write-Host ""
