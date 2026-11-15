# =============================================================================
#  ecoute.ps1 - Fabrique le banc d'ecoute, puis le depouille.
#
#  ⭐⭐ LE SEUL BANC DU PROJET QUI DEMANDE UNE OREILLE. Le piege 60 a montre que
#  la recherche descend BIEN mais AILLEURS. Deux lectures restent - la perte est
#  fausse, ou le probleme est non identifiable - et aucune machine ne les
#  separe. Une heure d'ecoute decide de plusieurs semaines de travail.
#
#  Deux modes :
#    (defaut)     fabrique les fichiers dans corpus\ecoute\
#    -Depouille   lit REPONSES.txt et cle.csv, et rend le verdict
#
#  /!\ NE PAS OUVRIR cle.csv AVANT D'AVOIR REPONDU. Le test n'a de valeur qu'en
#  aveugle - c'est la seule protection contre le biais de celui qui sait deja.
#
#  Usage :
#    powershell -ExecutionPolicy Bypass -File tools\ecoute.ps1
#    powershell -ExecutionPolicy Bypass -File tools\ecoute.ps1 -Depouille
# =============================================================================
param(
    [switch]$Depouille,
    [int]$Essais = 20,
    [int]$Budget = 2000,
    [string]$Liste = 'corpus/liste.txt'
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent
$corp  = Join-Path $root 'corpus'
$dos   = Join-Path $corp 'ecoute'
$cle   = Join-Path $dos 'cle.csv'
$rep   = Join-Path $dos 'REPONSES.txt'

# =============================================================================
#  DEPOUILLEMENT
# =============================================================================
if ($Depouille) {
    if (-not (Test-Path $cle)) { throw "cle.csv introuvable - fabriquer le banc d'abord" }
    if (-not (Test-Path $rep)) { throw "REPONSES.txt introuvable" }

    $verite = @{}
    $type   = @{}
    $ecart  = @{}
    foreach ($l in (Get-Content $cle | Select-Object -Skip 1)) {
        $c = $l -split ';'
        if ($c.Count -lt 4) { continue }
        $verite[[int]$c[0]] = $c[3]
        $type[[int]$c[0]]   = $c[1]
        $ecart[[int]$c[0]]  = [double]$c[2]
    }

    $repAnc = 0; $totAnc = 0; $repTest = 0; $totTest = 0; $egalites = 0; $sans = 0
    foreach ($l in Get-Content $rep) {
        if ($l -notmatch 'essai\s+(\d+)\s*:\s*(\S+)') { continue }
        $n = [int]$Matches[1]
        $r = $Matches[2].ToUpper()
        if (-not $verite.ContainsKey($n)) { continue }
        if ($r -notmatch '^[AB=]$') { $sans++; continue }
        if ($r -eq '=') { $egalites++; continue }
        if ($type[$n] -eq 'ancrage') {
            $totAnc++; if ($r -eq $verite[$n]) { $repAnc++ }
        } else {
            $totTest++; if ($r -eq $verite[$n]) { $repTest++ }
        }
    }

    Write-Host ""
    Write-Host "DEPOUILLEMENT DU BANC D'ECOUTE" -ForegroundColor Cyan
    Write-Host "==============================" -ForegroundColor Cyan
    Write-Host ""
    if ($sans -gt 0) { Write-Host "  $sans essai(s) sans reponse lisible - ignore(s)" -ForegroundColor Yellow }
    if ($egalites -gt 0) { Write-Host "  $egalites essai(s) juge(s) indiscernables" }

    # -------------------------------------------------------------------------
    #  LE TEMOIN D'ABORD (invariant 10). Si les ancrages sont rates, rien de ce
    #  qui suit ne se lit : ce n'est pas la perte qu'on aurait mesuree, c'est le
    #  protocole - fichiers mal ranges, ecoute distraite, monitoring inadapte.
    # -------------------------------------------------------------------------
    if ($totAnc -eq 0) { throw "aucun essai d'ancrage repondu - le temoin manque" }
    $pAnc = 100.0 * $repAnc / $totAnc
    Write-Host ""
    Write-Host ("  TEMOIN - ancrages (ecart > 4 dB, doivent etre evidents)") -ForegroundColor Cyan
    Write-Host ("    {0} / {1} corrects  ({2:N0} %)" -f $repAnc, $totAnc, $pAnc)
    if ($pAnc -lt 80.0) {
        Write-Host ""
        Write-Host "  /!\ TEMOIN ECHOUE. Un ecart de plus de 4 dB devrait s'entendre" -ForegroundColor Red
        Write-Host "      sans hesiter. Soit le protocole est casse, soit la perte est" -ForegroundColor Red
        Write-Host "      fausse JUSQUE DANS LES GRANDS ECARTS - ce qui serait un" -ForegroundColor Red
        Write-Host "      resultat en soi, mais il faut d'abord verifier les fichiers." -ForegroundColor Red
        Write-Host "      Aucun chiffre ci-dessous n'est utilisable en l'etat." -ForegroundColor Red
        exit 1
    }
    Write-Host "    -> le protocole tient, on peut lire la suite." -ForegroundColor Green

    if ($totTest -eq 0) { throw "aucun essai de test repondu" }
    $pTest = 100.0 * $repTest / $totTest

    # Probabilite d'obtenir AU MOINS ce score par hasard (binomiale, p = 0,5).
    # /!\ SANS ELLE, « 9 sur 14 » a l'air d'un resultat. C'est le piege 53 :
    # compter les signes n'est pas un test.
    $p = 0.0
    for ($k = $repTest; $k -le $totTest; $k++) {
        $c = 1.0
        for ($j = 0; $j -lt $k; $j++) { $c = $c * ($totTest - $j) / ($j + 1) }
        $p += $c * [math]::Pow(0.5, $totTest)
    }

    Write-Host ""
    Write-Host ("  TEST - ecarts d'environ 1 dB (la vraie question)") -ForegroundColor Cyan
    Write-Host ("    {0} / {1} d'accord avec la perte  ({2:N0} %)" -f $repTest, $totTest, $pTest)
    Write-Host ("    probabilite d'un tel score par hasard : {0:N3}" -f $p)
    Write-Host ""
    Write-Host "  --- VERDICT ---" -ForegroundColor Cyan
    if ($p -lt 0.05 -and $pTest -gt 50.0) {
        Write-Host "  LA PERTE EST D'ACCORD AVEC L'OREILLE sur les ecarts de ~1 dB." -ForegroundColor Green
        Write-Host "  Lecture (b) : le probleme est NON IDENTIFIABLE, la perte a raison,"
        Write-Host "  et etre loin dans l'espace des parametres n'a pas d'importance."
        Write-Host "  -> ne pas refaire la perte. Chercher ailleurs les 2,8 dB residuels."
    } elseif ($pTest -lt 45.0 -and $p -gt 0.95) {
        Write-Host "  LA PERTE EST A CONTRE-SENS : l'oreille prefere ce qu'elle rejette." -ForegroundColor Red
        Write-Host "  Lecture (a), forme grave. -> refaire la perte, et le resultat"
        Write-Host "  actuel de la recherche est activement trompeur."
    } else {
        Write-Host "  LA PERTE NE DIT RIEN A CETTE ECHELLE : l'accord n'est pas" -ForegroundColor Yellow
        Write-Host "  distinguable du hasard. Un dB de perte ne s'entend pas."
        Write-Host "  Lecture (a) : optimiser la perte au-dela de ce point n'achete rien"
        Write-Host "  d'audible. -> refaire la perte, ou accepter le plafond actuel."
    }
    Write-Host ""
    exit 0
}

# =============================================================================
#  FABRICATION
# =============================================================================
if (-not (Test-Path $dos)) { New-Item -ItemType Directory -Force $dos | Out-Null }
# /!\ ON REPART D'UN DOSSIER PROPRE. Melanger les wav d'une serie precedente
# avec la nouvelle cle donnerait un depouillement faux sans que rien ne le dise.
Get-ChildItem $dos -File | Remove-Item -Force

$out = Join-Path $env:TEMP 'romance_ecoute'
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Force $out | Out-Null }
$vcvars = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
          Select-Object -First 1
if (-not $vcvars) { throw "vcvars64.bat introuvable - Visual Studio requis." }

$bat = Join-Path $out 'build.bat'
@"
@echo off
call "$($vcvars.FullName)" >nul
cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /I"$root\Source" /I"$root\outils" "$root\tools\mes_ecoute.cpp" /Fe:"$out\ecoute.exe" /Fo:"$out\\"
"@ | Out-File -FilePath $bat -Encoding ascii
& cmd /c $bat | Out-Null
if ($LASTEXITCODE -ne 0) { throw "compilation de mes_ecoute.cpp echouee" }

$listeAbs = if ([IO.Path]::IsPathRooted($Liste)) { $Liste } else { Join-Path $root $Liste }
# /!\ PAS DE "$dos\" ICI. Un argument entre guillemets qui FINIT par un
# antislash fait echapper le guillemet fermant : Windows recoit
# `D:\...\ecoute" 8 800` comme un seul argument. C'est la troisieme panne
# d'antislash de la semaine. Le separateur est ajoute cote C++.
& "$out\ecoute.exe" $listeAbs $dos $Essais $Budget
if ($LASTEXITCODE -ne 0) { throw "mes_ecoute.exe a rendu $LASTEXITCODE" }

Write-Host ""
Write-Host "  Repondre dans : $rep" -ForegroundColor Green
Write-Host "  Puis : powershell -ExecutionPolicy Bypass -File tools\ecoute.ps1 -Depouille"
Write-Host ""
