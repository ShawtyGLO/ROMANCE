param(
    [int]$Sons     = 50,      # sons reels distilles a CETTE passe (elles s accumulent)
    [int]$Budget   = 400,     # evaluations par son distille
    [int]$Epoques  = 6,       # affinage : PEU d'epoques (voir plus bas)
    [int]$Eval     = 200,     # evaluations par son a l'evaluation
    [int]$EvalSons = 16,
    [string]$Base  = "",      # reseau de depart
    [string]$Nom   = "",      # suffixe de cette iteration
    [int]$Decalage = 0        # pour piocher AILLEURS dans la liste
)
# =============================================================================
#  iteration.ps1 - Un tour de distillation, en ~20 minutes.
#
#  ⭐ POURQUOI DES PETITS LOTS. 200 sons a 400 evaluations tiennent en ~11 min ;
#  900 sons a 700 en tenaient trois heures. Cinq iterations courtes apprennent
#  plus qu'une longue : chacune est MESUREE, donc chacune dit si la precedente
#  a servi. Une seule passe de trois heures ne dit rien d'autre que son score
#  final.
#
#  /!\ AFFINAGE COURT, ET C'EST DELIBERE. Le jeu distille compte quelques
#  centaines d'exemples contre des centaines de milliers pour le synthetique.
#  Trop d'epoques dessus et le reseau oublie tout ce qu'il sait au profit de
#  200 sons - l'oubli catastrophique est trivial a provoquer dans cette
#  configuration.
#
#  Usage :
#    powershell -ExecutionPolicy Bypass -File tools\iteration.ps1 -Nom 1
#    powershell -ExecutionPolicy Bypass -File tools\iteration.ps1 -Nom 2 -Base corpus\reseau_d1.rnn
# =============================================================================
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$corp = Join-Path $root 'corpus'
$bin  = Join-Path $env:TEMP 'romance_appr'
if (-not $Nom)  { $Nom = Get-Date -Format 'HHmm' }
if (-not $Base) { $Base = Join-Path $corp 'reseau_melodique.rnn' }

$jeu = Join-Path $corp 'distill.bin'   # UN seul jeu, il s accumule
$res = Join-Path $corp "reseau_d$Nom.rnn"
$csv = Join-Path $corp "eval_d$Nom.csv"
$liste = Join-Path $corp 'liste_large.txt'

Write-Host ""
Write-Host "ROMANCE - iteration $Nom" -ForegroundColor Cyan
Write-Host "------------------------" -ForegroundColor Cyan
Write-Host "  depart : $Base"

Write-Host ""
Write-Host "1. distillation ($Sons sons x $Budget evaluations)" -ForegroundColor Cyan
& "$bin\distille.exe" $liste $jeu $Sons $Budget 12 $Decalage

Write-Host ""
Write-Host "2. affinage ($Epoques epoques)" -ForegroundColor Cyan
& "$bin\train.exe" $jeu $res $Epoques 12 $Base

Write-Host ""
Write-Host "3. evaluation" -ForegroundColor Cyan
& "$bin\evalue.exe" $liste $res $Eval $csv $EvalSons

Write-Host ""
Write-Host "iteration $Nom terminee : $res" -ForegroundColor Green
