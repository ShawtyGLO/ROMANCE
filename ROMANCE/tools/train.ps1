param(
    [string]$Jeu = "",
    [string]$Sortie = "",
    [int]$Epoques = 30,
    [string]$Reprise = "",
    [int]$Fils = 0
)
# =============================================================================
#  train.ps1 - Compile et lance l.entrainement du reseau.
#
#  Le generateur est la verite : chaque exemple est une paire (empreinte,
#  108 octets) dont l'etiquette est exacte. Le prior vient du corpus mesure.
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
if (-not $Jeu)    { $Jeu    = Join-Path (Join-Path $root 'corpus') 'train.bin' }
if (-not $Sortie) { $Sortie = Join-Path (Join-Path $root 'corpus') 'reseau.rnn' }

$src = Join-Path $root 'outils\train.cpp'
$inc = Join-Path $root 'Source'
$inc2 = Join-Path $root 'outils'
$out = Join-Path $env:TEMP 'romance_train'
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
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" /I"$inc2" "$src" /Fe:"$out\train.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation echouee." }

# /!\ CETTE LIGNE A ECHOUE EN SILENCE DEUX FOIS DE SUITE.
#
#   1. Elle appelait `train.exe $Sortie $N $Graine` - des variables qui
#      n'existent meme pas ici, la ligne avait ete copiee de dataset.ps1.
#      train.exe affichait son mode d'emploi et rendait 0.
#   2. Corrigee, elle passait `0` pour le nombre de fils en croyant dire
#      « automatique ». train.exe l'a pris au mot : aucun fil, donc aucun
#      gradient, perte 0,00000 a chaque epoque, reseau reste aleatoire - et
#      succes annonce. Le seul indice etait la duree : 30 epoques sur 200 000
#      exemples en deux minutes.
#
# `$Fils` a 0 signifie desormais « ne passe pas l'argument », et train.exe
# refuse de toute facon un nombre de fils nul.
if ($Fils -gt 0) { & "$out\train.exe" $Jeu $Sortie $Epoques $Fils $Reprise }
else             { & "$out\train.exe" $Jeu $Sortie $Epoques }

