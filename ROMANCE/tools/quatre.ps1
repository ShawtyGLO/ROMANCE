param(
    [int]$Lot      = 50,      # sons ajoutes a chaque passe
    [int]$Budget   = 400,     # evaluations par son distille
    [int]$Epoques  = 6,
    [int]$Eval     = 200,
    [int]$EvalSons = 16,
    [int]$Passes   = 4
)
# =============================================================================
#  quatre.ps1 - Quatre distillations qui S'ACCUMULENT.
#
#  ⭐ LE JEU S'ACCUMULE (50, 100, 150, 200 exemples) MAIS L'AFFINAGE REPART
#  TOUJOURS DE LA MEME BASE. C'est ce qui rend l'experience lisible : la seule
#  chose qui change d'une passe a l'autre est la QUANTITE de sons reels.
#
#  Si on enchainait les affinages les uns sur les autres, une amelioration
#  pourrait venir de la quantite, du nombre d'epoques cumulees, ou de l'ordre
#  des lots - et on ne saurait pas laquelle.
#
#  Chaque passe ecrit son evaluation ; la courbe des quatre repond a la
#  question « est-ce le nombre de sons qui manque ? ».
#
#  Usage : powershell -ExecutionPolicy Bypass -File tools\quatre.ps1
# =============================================================================
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$corp = Join-Path $root 'corpus'
$bin  = Join-Path $env:TEMP 'romance_appr'
$base  = Join-Path $corp 'reseau_melodique.rnn'
$jeu   = Join-Path $corp 'distill.bin'
$liste = Join-Path $corp 'liste_large.txt'

# /!\ On repart d'un jeu VIDE : sinon une execution precedente fausserait la
# courbe en donnant deja 200 exemples a la premiere passe.
if (Test-Path $jeu) { Remove-Item $jeu -Force }
if (Test-Path "$jeu.pertes.csv") { Remove-Item "$jeu.pertes.csv" -Force }

Write-Host ""
Write-Host "ROMANCE - quatre distillations cumulatives" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  base : $base"
Write-Host ""

for ($k = 1; $k -le $Passes; $k++) {
    $n = $Lot * $k
    Write-Host ""
    Write-Host "===== passe $k / $Passes  ->  $n exemples au total" -ForegroundColor Cyan

    & "$bin\distille.exe" $liste $jeu $Lot $Budget 12 ($k - 1)
    if ($LASTEXITCODE -ne 0) { throw "distillation $k echouee" }

    $res = Join-Path $corp "reseau_d$k.rnn"
    & "$bin\train.exe" $jeu $res $Epoques 12 $base
    if ($LASTEXITCODE -ne 0) { throw "affinage $k echoue" }

    & "$bin\evalue.exe" $liste $res $Eval (Join-Path $corp "eval_d$k.csv") $EvalSons
}

Write-Host ""
Write-Host "termine - comparer les quatre evaluations" -ForegroundColor Green
