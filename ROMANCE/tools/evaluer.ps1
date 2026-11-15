# =============================================================================
#  evaluer.ps1 - Compile et lance `outils/evalue.cpp` sur un reseau donne.
#
#  ⭐ Il existe parce que `apprentissage.ps1` n'evalue qu'a la fin d'une chaine
#  de plusieurs heures. Juger un reseau ne devrait pas demander de tout refaire.
#
#  /!\ IL VERIFIE $LASTEXITCODE APRES LA COMPILATION *ET* APRES LA MESURE.
#  `evalue.exe` rend 2 quand un reseau est DEMANDE et introuvable ou refuse :
#  sans ce controle on lirait des chiffres d'heuristique en croyant juger le
#  reseau (piege 40).
#
#  Usage :
#    powershell -ExecutionPolicy Bypass -File tools\evaluer.ps1
#    powershell -ExecutionPolicy Bypass -File tools\evaluer.ps1 -Reseau corpus\reseau.rnn -Sons 40
# =============================================================================
param(
    [string]$Reseau = 'corpus\reseau.rnn',
    [string]$Liste  = 'corpus\liste.txt',
    [int]$Budget    = 400,
    [int]$Sons      = 40,
    [string]$Csv    = ''
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$inc   = Join-Path $root 'Source'
$src   = Join-Path $root 'outils\evalue.cpp'
$out   = Join-Path $env:TEMP 'romance_evaluer'

function Abs($p) { if ([IO.Path]::IsPathRooted($p)) { return $p } return (Join-Path $root $p) }
$Reseau = Abs $Reseau
$Liste  = Abs $Liste
if ($Csv -eq '') { $Csv = Join-Path $root ('corpus\eval_' + (Get-Date -Format 'yyyyMMdd_HHmm') + '.csv') }
else { $Csv = Abs $Csv }

if (-not (Test-Path $Reseau)) { throw "reseau introuvable : $Reseau" }
if (-not (Test-Path $Liste))  { throw "liste introuvable : $Liste" }
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" "$src" /Fe:"$out\evalue.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii

& cmd /c $bat
if ($LASTEXITCODE -ne 0) { throw "compilation d'evalue.cpp echouee" }

Write-Host ""
Write-Host "reseau : $Reseau"
Write-Host "liste  : $Liste   ($Sons sons max, budget $Budget)"
Write-Host ""

& "$out\evalue.exe" $Liste $Reseau $Budget $Csv $Sons
if ($LASTEXITCODE -ne 0) { throw "evalue.exe a rendu $LASTEXITCODE (2 = reseau demande mais absent ou refuse)" }
Write-Host ""
Write-Host "detail : $Csv" -ForegroundColor Green
