# =============================================================================
#  mesure.ps1 - Compile et lance un banc de mesure de tools\.
#
#  /!\ IL VERIFIE $LASTEXITCODE APRES CHAQUE ETAPE. C'est le piege 40 : un
#  script d'orchestration qui ne verifie que ce qu'il compile ne verifie rien,
#  et une chaine qui se termine en vert en designant un binaire qui n'existe
#  pas coute des heures avant qu'on s'en apercoive.
#
#  Usage :  powershell -ExecutionPolicy Bypass -File tools\mesure.ps1 oracle 12 3000 3 0
# =============================================================================
param(
    [Parameter(Mandatory=$true)][string]$Banc,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$Args
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$src   = Join-Path $tools "mes_$Banc.cpp"
$inc   = Join-Path $root 'Source'
# ⭐ `outils/` aussi : c'est la que vivent wav.h et mesure.h, dont tout banc qui
# lit ou ecrit des fichiers son a besoin (mes_ecoute).
$inc2  = Join-Path $root 'outils'
$out   = Join-Path $env:TEMP "romance_mes_$Banc"

if (-not (Test-Path $src)) { throw "banc introuvable : $src" }
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

# /!\ ${nom}_ ET PAS $nom_ : PowerShell lirait `$nom_` comme une variable
# inexistante, donc vide (piege 37).
$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" /I"$inc2" "$src" /Fe:"$out\$Banc.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "Compilation de mes_$Banc.cpp echouee." }

& "$out\$Banc.exe" @Args
if ($LASTEXITCODE -ne 0) { throw "mes_$Banc.exe a rendu $LASTEXITCODE." }
