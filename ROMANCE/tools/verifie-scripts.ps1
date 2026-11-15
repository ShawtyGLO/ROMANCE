# =============================================================================
#  verifie-scripts.ps1 - Le lint des scripts d'orchestration.
#
#  ⭐⭐ POURQUOI IL EXISTE. Les 4 et 5 septembre 2026, trois pannes de suite
#  n'etaient pas des fautes de DSP mais des fautes de SCRIPT - et chacune a
#  coute des heures parce qu'elle s'est manifestee tard :
#
#    · un `\r` ecrit dans un commentaire PowerShell (retour chariot REEL) a
#      coupe la ligne en deux ; la chaine d'apprentissage est morte trois
#      lignes avant la copie du reseau, apres 4 h de calcul ;
#    · un `| tee` a masque le code de retour : la chaine a signale un SUCCES
#      en mourant ;
#    · une fonction rendait `[sortie de cl + chemin]` au lieu du chemin, faute
#      d'un `| Out-Null` - et `& $exe` a tente d'executer « distille.cpp ».
#
#  Aucune n'etait detectable a la lecture. Toutes le sont par une machine.
#
#  /!\ CE N'EST PAS UN LINT DE STYLE. Chaque regle ici correspond a une panne
#  REELLE, datee. On n'en ajoute pas par gout ; on en ajoute quand quelque
#  chose casse.
#
#  Usage : powershell -ExecutionPolicy Bypass -File tools\verifie-scripts.ps1
#  Rend 1 si quelque chose ne va pas - donc utilisable dans une chaine.
# =============================================================================
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent

$fautes = 0
$avertis = 0

function Faute($f, $l, $m) {
    $script:fautes++
    Write-Host ("  FAUTE  {0}:{1}  {2}" -f (Split-Path $f -Leaf), $l, $m) -ForegroundColor Red
}
function Avertit($f, $l, $m) {
    $script:avertis++
    Write-Host ("  ~      {0}:{1}  {2}" -f (Split-Path $f -Leaf), $l, $m) -ForegroundColor Yellow
}

Write-Host ""
Write-Host "ROMANCE - verification des scripts" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan

# /!\ IL S'EXCLUT LUI-MEME, et ce n'est pas une commodite : ce fichier
# CONTIENT les motifs qu'il cherche (`| tee`, `$nom_`) dans ses propres chaines
# de message. Un lint qui s'accuse lui-meme rend un rapport que plus personne ne
# lit - exactement le defaut qu'on corrige.
$moi = $MyInvocation.MyCommand.Path
$fichiers = Get-ChildItem (Join-Path $root 'tools') -Filter *.ps1 -File |
            Where-Object { $_.FullName -ne $moi }
foreach ($f in $fichiers) {
    $chemin = $f.FullName

    # --- 1. le parseur PowerShell lui-meme. Rien ne remplace le vrai parseur :
    #        une heuristique sur du texte laisserait passer ce qu'il attrape.
    $err = $null
    [System.Management.Automation.Language.Parser]::ParseFile($chemin, [ref]$null, [ref]$err) | Out-Null
    if ($err) { foreach ($e in $err) { Faute $chemin $e.Extent.StartLineNumber $e.Message } }

    # --- 2. RETOUR CHARIOT ISOLE.
    # /!\ C'est la panne du 4 septembre. Un CR qui n'est pas suivi d'un LF
    # coupe la ligne pour PowerShell : la fin d'un commentaire devient une
    # COMMANDE. Invisible dans un editeur, invisible a la relecture.
    $octets = [IO.File]::ReadAllBytes($chemin)
    for ($i = 0; $i -lt $octets.Length; $i++) {
        if ($octets[$i] -eq 13 -and ($i + 1 -ge $octets.Length -or $octets[$i+1] -ne 10)) {
            $avant = [Text.Encoding]::UTF8.GetString($octets, 0, $i)
            $ligne = ($avant -split "`n").Count
            Faute $chemin $ligne "retour chariot ISOLE (un ``\r`` ecrit par erreur ?)"
            break
        }
    }

    $lignes = Get-Content $chemin
    for ($n = 0; $n -lt $lignes.Count; $n++) {
        $l = $lignes[$n]
        $num = $n + 1
        $code = ($l -split '#')[0]        # on ignore ce qui est en commentaire

        # --- 3. `| tee` MASQUE LE CODE DE RETOUR.
        # C'est ce qui a fait passer pour un succes une chaine morte a l'etape 7.
        if ($code -match '\|\s*tee\b') {
            Faute $chemin $num "``| tee`` masque `$LASTEXITCODE - utiliser Tee-Object, ou rediriger"
        }

        # --- 4. UNE VARIABLE QUI AVALE LE CARACTERE SUIVANT (piege 37).
        # `$nom_` est lu comme la variable `nom_`, inexistante donc vide.
        if ($code -match '\$[A-Za-z][A-Za-z0-9]*_[^A-Za-z0-9{]') {
            Avertit $chemin $num "``\$nom_`` : PowerShell lit ``nom_`` - ecrire ``\${nom}_`` (piege 37)"
        }

        # --- 4 bis. UN ARGUMENT ENTRE GUILLEMETS QUI FINIT PAR UN ANTISLASH.
        # /!\ `"$dos\"` fait echapper le guillemet fermant : le processus
        # appele recoit le chemin ET les arguments suivants COLLES en un seul.
        # Panne du 5 septembre 2026 sur ecoute.ps1 - le message d'erreur
        # affichait `...\ecoute" 8 800/`, ce qui ne ressemble a rien de connu.
        # /!\ MA PREMIERE VERSION ETAIT EXACTEMENT A L'ENVERS : elle signalait
        # `"$out\\"` - l'idiome CORRECT, ou l'antislash double se protege - et
        # laissait passer `"$dos\"`, le seul dangereux. Un lint inverse est pire
        # qu'aucun lint : il apprend a ignorer ses propres alertes.
        # On cherche donc UN SEUL antislash avant le guillemet fermant.
        if ($code -match '"[^"]*[^"\\]\\"') {
            Faute $chemin $num "argument entre guillemets finissant par UN antislash - il echappe le guillemet (doubler)"
        }

        # --- 5. UN ANTISLASH DANS UN CHEMIN PASSE EN ARGUMENT.
        # Sans consequence en PowerShell pur, mais ces scripts sont souvent
        # lances depuis bash, qui mange l'antislash (`corpus\reseau.rnn` ->
        # `corpusreseau.rnn`). On prefere les barres obliques.
        if ($code -match "-(Reseau|Liste|Csv)\s+[A-Za-z0-9_]+\\") {
            Avertit $chemin $num "chemin a antislash en argument - preferer / (bash le mange)"
        }
    }

    # --- 6. UNE FONCTION QUI APPELLE UNE COMMANDE PUIS FAIT `return`.
    # /!\ En PowerShell, une fonction rend TOUT ce qui n'a pas ete consomme,
    # pas seulement ce qui suit `return`. Une commande dont la sortie n'est ni
    # redirigee ni absorbee contamine la valeur de retour.
    $texte = Get-Content $chemin -Raw
    $regexFn = [regex]'(?ms)^function\s+(\w+).*?\n\}'
    foreach ($m in $regexFn.Matches($texte)) {
        $corps = $m.Value
        if ($corps -match '(?m)^\s*&\s' -and $corps -match '(?m)^\s*return\s') {
            $sansOut = [regex]::Matches($corps, '(?m)^\s*&\s.*$') |
                       Where-Object { $_.Value -notmatch 'Out-Null|Out-Host|>\s*\$null' }
            if ($sansOut.Count -gt 0) {
                $ligne = ($texte.Substring(0, $m.Index) -split "`n").Count
                Faute $chemin $ligne ("fonction ``{0}`` : un ``&`` sans Out-Null avant un ``return`` - la sortie contamine la valeur rendue" -f $m.Groups[1].Value)
            }
        }
    }
}

Write-Host ""
Write-Host ("  {0} fichier(s) verifie(s) - {1} faute(s), {2} avertissement(s)" -f $fichiers.Count, $fautes, $avertis)
if ($fautes -gt 0) {
    Write-Host "  /!\ Une faute ici est une panne qui arrivera TARD." -ForegroundColor Red
    exit 1
}
Write-Host "  OK" -ForegroundColor Green
exit 0
