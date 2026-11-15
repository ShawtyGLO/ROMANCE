# =============================================================================
#  distiller.ps1 - Distillation REDUITE, affinage, et evaluation du resultat.
#
#  ⭐⭐ POURQUOI REDUITE - ET POURQUOI LA RAISON D'ORIGINE ETAIT FAUSSE.
#
#  /!\ J'AVAIS ANNONCE 21,7 HEURES POUR LA NOMINALE (900 sons x 700
#  evaluations), et c'est pour ca qu'on l'a reduite. MESURE REELLE : 200 x 400
#  en HUIT MINUTES, soit ~167 evaluations/s - la nominale tient en une heure.
#  Facteur d'erreur 20.
#
#  La faute : j'avais tire le debit de `evalue.exe`, qui traite les sons
#  SEQUENTIELLEMENT, et je l'ai applique a `distille.exe`, qui en traite DOUZE
#  EN PARALLELE. Un debit ne se transpose pas sans verifier le modele
#  d'execution (piege 58).
#
#  La reduction reste utile pour une AUTRE raison : l'apport de la distillation
#  n'avait jamais ete demontre, et une mesure courte suffisait a repondre.
#  Reponse obtenue le 5 septembre 2026 : elle EMPIRE le resultat (+2,381 dB
#  contre l'heuristique, contre +0,771 sans elle). Voir piege 57.
#
#  /!\ ET ON EVALUE LE RESULTAT CONTRE LE MEME CORPUS que le reseau de
#  l'etape 7, avec le MEME budget. Deux mesures qu'on veut comparer doivent
#  differer par une seule chose.
#
#  /!\ LE RESEAU DISTILLE N'EST PAS INSTALLE POUR LE PLUGIN. On le juge
#  d'abord ; installer un reseau que personne n'a mesure serait exactement ce
#  qu'on reproche a la chaine d'origine.
#
#  Usage : powershell -ExecutionPolicy Bypass -File tools\distiller.ps1
# =============================================================================
param(
    [int]$Sons    = 200,     # sons reels passes a la recherche longue
    [int]$Budget  = 400,     # evaluations par son
    [int]$Epoques = 8,       # peu : le jeu distille est petit (oubli catastrophique)
    [int]$Threads = 12,
    [int]$SonsEval = 40
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$inc   = Join-Path $root 'Source'
$corp  = Join-Path $root 'corpus'
$out   = Join-Path $env:TEMP 'romance_distiller'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

function Compile($nom) {
    $bat = Join-Path $out "b_$nom.bat"
    @"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" "$root\outils\$nom.cpp" /Fe:"$out\$nom.exe" /Fo:"$out\${nom}_"
"@ | Out-File -FilePath $bat -Encoding ascii
    # /!\ `| Out-Null` N'EST PAS DECORATIF. Sans lui, la sortie de `cl` part
    # dans le PIPELINE de la fonction : `return` rend alors un TABLEAU
    # [lignes de compilation + chemin], et `& $exe` tente d'executer
    # « distille.cpp » comme une commande. C'est le piege PowerShell classique -
    # une fonction rend tout ce qui n'a pas ete consomme, pas seulement ce
    # qu'on ecrit apres `return`.
    & cmd /c $bat | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "compilation de $nom.cpp echouee" }
    return (Join-Path $out "$nom.exe")
}

# /!\ VERIFIER LE CODE DE RETOUR DE CHAQUE ETAPE, pas seulement des
# compilations (piege 40). Et surtout : NE PAS TUYAUTER la sortie de ce script
# dans `tee` sans relire $LASTEXITCODE - c'est ce qui a masque l'echec de
# `apprentissage.ps1` le 4 septembre, qui est mort a l'etape 7 en rendant 0.
function Verifie($n) { if ($LASTEXITCODE -ne 0) { throw "etape $n echouee (code $LASTEXITCODE)" } }

Write-Host ""
Write-Host "ROMANCE - distillation reduite" -ForegroundColor Cyan
Write-Host "==============================" -ForegroundColor Cyan
# 167 evaluations/s : debit MESURE le 5 septembre 2026 sur distille.exe lui-meme,
# pas transpose depuis un autre outil (piege 58).
Write-Host "  $Sons sons x $Budget evaluations  (~$([math]::Round($Sons*$Budget/167/60,0)) min au debit mesure)"

$exeDistill = Compile 'distille'
$exeTrain   = Compile 'train'
$exeEvalue  = Compile 'evalue'

$listeLarge = Join-Path $corp 'liste_large.txt'
$listeMel   = Join-Path $corp 'liste.txt'
$resEtape7  = Join-Path $corp 'reseau.rnn'
$jeuDist    = Join-Path $corp 'distill_reduit.bin'
$resDist    = Join-Path $corp 'reseau_distille.rnn'

if (-not (Test-Path $resEtape7)) { throw "reseau de l'etape 7 introuvable : $resEtape7" }
# /!\ ON REPART D'UN JEU NEUF. `distille.exe` AJOUTE aux exemples deja presents
# ("N exemples deja presents, on ajoute") : reutiliser le fichier d'aout
# melangerait des cibles de 230 octets a des cibles de 298.
if (Test-Path $jeuDist) { Remove-Item $jeuDist -Force }

Write-Host ""
Write-Host "=== 1. distillation ($Sons sons, budget $Budget)" -ForegroundColor Cyan
& $exeDistill $listeLarge $jeuDist $Sons $Budget $Threads
Verifie 1

Write-Host ""
Write-Host "=== 2. affinage sur les sons reels ($Epoques epoques)" -ForegroundColor Cyan
& $exeTrain $jeuDist $resDist $Epoques $Threads $resEtape7
Verifie 2

Write-Host ""
Write-Host "=== 3. evaluation du reseau DISTILLE" -ForegroundColor Cyan
$csv = Join-Path $corp ('eval_distille_' + (Get-Date -Format 'yyyyMMdd_HHmm') + '.csv')
& $exeEvalue $listeMel $resDist $Budget $csv $SonsEval
Verifie 3

Write-Host ""
Write-Host "termine." -ForegroundColor Green
Write-Host "  reseau distille : $resDist   (NON installe - a comparer avant)"
Write-Host "  detail          : $csv"
Write-Host ""
Write-Host "  /!\ Comparer la ligne 'reseau - heuristique' de cette mesure a celle" -ForegroundColor Yellow
Write-Host "      du reseau de l'etape 7. Negatif = meilleur." -ForegroundColor Yellow
