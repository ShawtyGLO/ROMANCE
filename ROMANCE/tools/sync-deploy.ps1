# Compile puis installe le VST3 dans le dossier systeme.
# /!\ Ecrit dans C:\Program Files\Common Files\VST3 : demande une ELEVATION.
# Lancer "SYNC ET DEPLOIE.cmd" en administrateur.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }
$src = Join-Path $root 'Builds\VisualStudio2022\x64\Release\VST3\ROMANCE.vst3'
$dst = 'C:\Program Files\Common Files\VST3'
if (-not (Test-Path $src)) { throw "VST3 introuvable : $src" }
Copy-Item $src $dst -Recurse -Force
Write-Host "ROMANCE.vst3 installe dans $dst" -ForegroundColor Green
