param(
    [string]$Liste = "",
    [string]$Sortie = ""
)
# =============================================================================
#  corpus.ps1 - Compile et lance la mesure du corpus.
#
#  Le prior doit venir de la banque, pas d'une intuition : cet outil mesure les
#  sons qu'on veut vraiment resynthetiser, avec NOTRE analyse.
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
if (-not $Liste)  { $Liste  = Join-Path $root 'corpus\liste.txt' }
if (-not $Sortie) { $Sortie = Join-Path $root 'corpus\mesures.csv' }

$src = Join-Path $root 'outils\corpus.cpp'
$inc = Join-Path $root 'Source'
$inc2 = Join-Path $root 'outils'
$out = Join-Path $env:TEMP 'romance_corpus'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

Write-Host ""
Write-Host "ROMANCE - mesure du corpus" -ForegroundColor Cyan
Write-Host "--------------------------" -ForegroundColor Cyan

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" /I"$inc2" "$src" /Fe:"$out\corpus.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }

& "$out\corpus.exe" $Liste $Sortie
Write-Host "  -> $Sortie"
