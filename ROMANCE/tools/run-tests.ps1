# =============================================================================
#  run-tests.ps1 - Tests DSP autonomes de ROMANCE.
#
#  Grain.h ne depend que de <cmath> et <algorithm> : on le teste sans JUCE,
#  sans hote, sans compiler le plugin. Quelques secondes au lieu de quelques
#  minutes. Meme regle que XpertComp.h et DetailClipper.h.
#
#  Les bornes sont les valeurs MESUREES sur le prototype Python
#  (D:\1 Plugin Vibecodding\ROMANCEPROTO VST).
#
#  Usage :  powershell -ExecutionPolicy Bypass -File tools\run-tests.ps1
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$src   = Join-Path $root 'tests\test_romance.cpp'
$inc   = Join-Path $root 'Source'
$out   = Join-Path $env:TEMP 'romance_tests'

if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

# ⭐⭐ LE LINT DES SCRIPTS D'ABORD, ET AVANT LA COMPILATION.
#
# /!\ Trois pannes des 4-5 septembre 2026 n'etaient pas des fautes de DSP mais
# des fautes de SCRIPT - un retour chariot isole, un `| tee` qui masque un code
# de retour, une fonction qui rend sa sortie de compilation. Aucune n'etait
# visible a la lecture ; toutes coutent des heures parce qu'elles se
# manifestent tard. Le lint est ici pour qu'on les paie en deux secondes.
& (Join-Path $PSScriptRoot 'verifie-scripts.ps1')
if ($LASTEXITCODE -ne 0) { throw "verification des scripts echouee." }

Write-Host ""
Write-Host "ROMANCE - tests DSP" -ForegroundColor Cyan
Write-Host "-------------------" -ForegroundColor Cyan

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" "$src" /Fe:"$out\tests.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }

& "$out\tests.exe"
exit $LASTEXITCODE
