param(
    [int]$Melodique = 120000,      # exemples pour la premiere phase
    [int]$Large     = 160000,      # exemples pour la phase elargie
    [int]$Epoques   = 30,
    [int]$Budget    = 400,         # evaluations par son a l'evaluation
    [int]$SonsDistilles = 900,     # vrais sons passes a la recherche longue
    [int]$BudgetDistill = 700,     # evaluations par son distille
    [switch]$Reprendre             # ne refait pas ce qui existe deja
)
# =============================================================================
#  apprentissage.ps1 - Toute la chaine, en une commande.
#
#  ⭐ LE GENERATEUR EST LA VERITE : chaque exemple est une paire (empreinte,
#  108 octets) dont l'etiquette est exacte. Le corpus ne sert pas de donnees
#  d'entrainement, il sert a savoir OU tirer - et a juger le resultat.
#
#  /!\ CHAQUE ETAPE ECRIT SON RESULTAT SUR DISQUE ET PEUT ETRE REPRISE. Une
#  chaine de deux heures qui repart de zero a la moindre interruption ne sert
#  a rien.
#
#  Etapes :
#    1. mesurer le corpus melodique       -> corpus\mesures.csv
#    2. jeu d'entrainement melodique      -> corpus\train_melodique.bin
#    3. entrainement                      -> corpus\reseau_melodique.rnn
#    4. evaluation sur le corpus melodique
#    5. mesurer le corpus elargi          -> corpus\mesures_large.csv
#    6. jeu elargi                        -> corpus\train_large.bin
#    7. affinage a partir du reseau melodique -> corpus\reseau.rnn
#    8. evaluation finale, melodique ET elargi
#
#  Usage :
#    powershell -ExecutionPolicy Bypass -File tools\apprentissage.ps1
#    powershell -ExecutionPolicy Bypass -File tools\apprentissage.ps1 -Reprendre
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$corp  = Join-Path $root 'corpus'
$inc   = Join-Path $root 'Source'
$inc2  = Join-Path $root 'outils'
$bin   = Join-Path $env:TEMP 'romance_appr'
if (-not (Test-Path $corp)) { New-Item -ItemType Directory -Force $corp | Out-Null }
if (-not (Test-Path $bin))  { New-Item -ItemType Directory -Force $bin  | Out-Null }

$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

function Compile($nom) {
    $src = Join-Path $root "outils\$nom.cpp"
    $exe = Join-Path $bin "$nom.exe"
    $bat = Join-Path $bin "b_$nom.bat"
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$inc" /I"$inc2" "$src" /Fe:"$exe" /Fo:"$bin\${nom}_"
"@ | Out-File -FilePath $bat -Encoding ascii
    & cmd /c $bat | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "compilation de $nom echouee" }
    return $exe
}

# /!\ VERIFIER LE CODE DE RETOUR DE CHAQUE ETAPE, pas seulement des
# compilations. L'etape 9 a echoue en silence pendant deux executions
# completes : `train.exe` rendait 1 sur « jeu illisible », personne ne le
# lisait, et la chaine se terminait en vert avec un reseau manquant.
function Verifie($n) {
    if ($LASTEXITCODE -ne 0) { throw "etape $n echouee (code $LASTEXITCODE)" }
}

function Etape($n, $titre) {
    Write-Host ""
    Write-Host "=== $n. $titre" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "ROMANCE - apprentissage complet" -ForegroundColor Cyan
Write-Host "===============================" -ForegroundColor Cyan

Etape 0 "compilation des outils"
$exeCorpus  = Compile 'corpus'
$exeDataset = Compile 'dataset'
$exeTrain   = Compile 'train'
$exeEvalue  = Compile 'evalue'
$exeDistill = Compile 'distille'
Write-Host "  ok"

$listeMel   = Join-Path $corp 'liste.txt'
$listeLarge = Join-Path $corp 'liste_large.txt'
$mesMel     = Join-Path $corp 'mesures.csv'
$mesLarge   = Join-Path $corp 'mesures_large.csv'
$jeuMel     = Join-Path $corp 'train_melodique.bin'
$jeuLarge   = Join-Path $corp 'train_large.bin'
$resMel     = Join-Path $corp 'reseau_melodique.rnn'
$resFinal   = Join-Path $corp 'reseau.rnn'
$jeuDist    = Join-Path $corp 'distill.bin'
$resDist    = Join-Path $corp 'reseau_distille.rnn'

function Existe($p) { return $Reprendre -and (Test-Path $p) -and ((Get-Item $p).Length -gt 1024) }

# =============================================================================
#  /!\ UN JEU PERIME NE DOIT PAS ETRE REPRIS.
#
#  L'en-tete d'un .bin porte la taille des cibles. Le jour ou `kOctetsProfil`
#  passe de 230 a 298, les jeux deja sur disque restent LA, avec la bonne
#  taille de fichier et la bonne signature - `Existe` les declare « deja fait »,
#  et l'entrainement echoue trois etapes plus loin sur « jeu illisible ».
#
#  Ce n'est pas theorique : c'est exactement l'etat du disque au 4 septembre
#  2026. Quelqu'un qui lance la chaine avec -Reprendre avant d'aller dormir se
#  reveille devant un echec a l'etape 3, apres une minute de calcul et huit
#  heures d'attente.
#
#  On lit donc les douze octets qui suivent la signature et on compare.
# =============================================================================
function JeuCompatible($p) {
    if (-not (Test-Path $p)) { return $false }
    try {
        $f = [System.IO.File]::OpenRead($p)
        try {
            $b = New-Object byte[] 18
            if ($f.Read($b, 0, 18) -ne 18) { return $false }
            if ([Text.Encoding]::ASCII.GetString($b, 0, 5) -ne 'ROMDS') { return $false }
            $tailleCible = [BitConverter]::ToUInt32($b, 14)
            return $tailleCible -eq $OctetsProfil
        } finally { $f.Dispose() }
    } catch { return $false }
}

# La taille du profil, lue dans le CODE et jamais recopiee a la main (piege 13).
$OctetsProfil = [int]([regex]::Match(
    (Get-Content (Join-Path $root 'Source\Profil.h') -Raw),
    'kOctetsProfil\s*=\s*(\d+)').Groups[1].Value)
if ($OctetsProfil -lt 8) { throw "kOctetsProfil introuvable dans Profil.h" }
Write-Host "  format de profil courant : $OctetsProfil octets"

function JeuUtilisable($p) {
    if (-not (Existe $p)) { return $false }
    if (JeuCompatible $p) { return $true }
    Write-Host "  /!\ $([IO.Path]::GetFileName($p)) est PERIME (autre taille de profil) - on le refait" -ForegroundColor Yellow
    return $false
}

Etape 1 "mesure du corpus melodique"
if (Existe $mesMel) { Write-Host "  deja fait" } else { & $exeCorpus $listeMel $mesMel }
Verifie 1

Etape 2 "jeu d'entrainement melodique ($Melodique exemples)"
if (JeuUtilisable $jeuMel) { Write-Host "  deja fait" } else { & $exeDataset $jeuMel $Melodique 1 12 }
Verifie 2

Etape 3 "entrainement melodique ($Epoques epoques)"
if (Existe $resMel) { Write-Host "  deja fait" } else { & $exeTrain $jeuMel $resMel $Epoques 12 }
Verifie 3

Etape 4 "evaluation - corpus melodique"
& $exeEvalue $listeMel $resMel $Budget (Join-Path $corp 'eval_melodique.csv') 40

Etape 5 "mesure du corpus elargi"
if (Existe $mesLarge) { Write-Host "  deja fait" } else { & $exeCorpus $listeLarge $mesLarge }
Verifie 5

Etape 6 "jeu elargi ($Large exemples)"
if (JeuUtilisable $jeuLarge) { Write-Host "  deja fait" } else { & $exeDataset $jeuLarge $Large 7 12 }
Verifie 6

Etape 7 "affinage a partir du reseau melodique"
# /!\ On REPART du reseau melodique au lieu de reinitialiser : c'est ce qui
# distingue un elargissement d'un remplacement. Sinon on perdrait ce qui a ete
# appris sur les nappes et les claviers en apprenant les basses.
& $exeTrain $jeuLarge $resFinal $Epoques 12 $resMel
Verifie 7

# =============================================================================
#  ⭐⭐ ON INSTALLE LE RESEAU LA OU LE PLUGIN LE CHERCHE.
#
#  /!\ SANS CETTE LIGNE, LA CHAINE SE TERMINE EN VERT ET LE PLUGIN CONTINUE DE
#  TOURNER A L'HEURISTIQUE. Il lit `Documents\ROMANCE/reseau.rnn` ; la chaine,
#  elle, ecrit dans `corpus\`. Deux chemins, aucun lien - exactement la forme
#  du piege 40 : un resultat produit que personne ne consomme.
#
#  ⭐ ET C'EST LE RESEAU DE L'ETAPE 7 QU'ON INSTALLE, PAS LE DISTILLE. C'est
#  celui dont le gain est MESURE (+0,314 dB sur l'heuristique). La distillation
#  n'a jamais pu tourner jusqu'ici, donc son apport est inconnu : l'installer
#  d'office reviendrait a livrer un reseau que personne n'a juge. L'etape 10
#  donnera les chiffres, et on choisira demain.
# =============================================================================
$dest = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'ROMANCE'
if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Force $dest | Out-Null }
Copy-Item $resFinal (Join-Path $dest 'reseau.rnn') -Force
Write-Host "  installe pour le plugin : $(Join-Path $dest 'reseau.rnn')" -ForegroundColor Green

Etape 8 "distillation - de VRAIS sons vers des recettes"
# ⭐ C'est ici qu'on referme l'ecart de domaine. Le reseau n'avait vu que des
# rendus de notre generateur ; il apprend maintenant sur des Oberheim, des
# guitares, des 808. L'etiquette n'est plus exacte - c'est ce que la recherche
# a trouve, donc un professeur imparfait - mais elle est du BON MONDE.
if (Existe $jeuDist) { Write-Host "  deja fait" }
else { & $exeDistill $listeLarge $jeuDist $SonsDistilles $BudgetDistill 12 }
Verifie 8

Etape 9 "affinage sur les sons reels"
# /!\ On repart du reseau elargi, avec peu d'epoques : le jeu distille est
# petit (quelques centaines d'exemples contre des centaines de milliers). Trop
# d'epoques dessus et le reseau oublierait tout ce qu'il sait au profit de 900
# sons - c'est l'oubli catastrophique, et il est facile a provoquer ici.
& $exeTrain $jeuDist $resDist 8 12 $resFinal
Verifie 9

Etape 10 "evaluation finale"
Write-Host "--- corpus melodique"
& $exeEvalue $listeMel   $resDist $Budget (Join-Path $corp 'eval_final_melodique.csv') 40
Write-Host "--- corpus elargi"
& $exeEvalue $listeLarge $resDist $Budget (Join-Path $corp 'eval_final_large.csv') 60

Write-Host ""
Write-Host "termine." -ForegroundColor Green
Write-Host "  installe pour le plugin : $(Join-Path $dest 'reseau.rnn')   (etape 7)"
Write-Host "  distille, NON installe  : $resDist   (a juger sur eval_final_*.csv)"
Write-Host ""
Write-Host "  /!\ Comparer eval_final_melodique.csv (reseau distille) a" -ForegroundColor Yellow
Write-Host "      eval_etape7.csv AVANT de remplacer l'un par l'autre." -ForegroundColor Yellow
