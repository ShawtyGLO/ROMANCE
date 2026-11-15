# ROMANCE — synthé / rompleur à ROM générée

Nom **provisoire**. Conception complète dans le vault Obsidian, dossier `ROMANCE/` —
notamment `ROMANCE - Topologie complète.md` et `ROMANCE - Architecture du synthé.md`
(les dix invariants du projet).

Prototype d'analyse et de recherche en Python : `D:\1 Plugin Vibecodding\ROMANCEPROTO VST\`.
**Les bornes des tests d'ici sont les valeurs mesurées là-bas.**

## Lancer

```
powershell -ExecutionPolicy Bypass -File tools\run-tests.ps1
```
ou `LANCER LES TESTS.cmd`.

## Fichiers

| Fichier | Rôle |
|---|---|
| `Source/Grain.h` | la couche d'époque : μ-law, horloge, reconstruction, `PROPRE` |
| `Source/Couche.h` | une couche : enveloppe, filtre musical, zones, unisson |
| `Source/Moteur.h` | la voix (4 couches + bruit) et l'allocation avec vol |
| `Source/Modulation.h` | sources, destinations, **le pont vers les boutons de façade** |
| `Source/PluginProcessor.*` | APVTS (~240 paramètres), MIDI (notes **et contrôleurs**), 5 racks FX |
| `Source/Recherche.h` | la (1+λ), les relances, le pas adaptatif, le choix à travers la machine |
| `tools/mesure.ps1` | compile et lance un banc `tools/mes_*.cpp`, en vérifiant `$LASTEXITCODE` |
| `tests/test_romance.cpp` | 377 contrôles, bornes issues du prototype Python |

Aucun de ces en-têtes ne dépend de JUCE : ils se testent en quelques secondes,
sans hôte et sans compiler le plugin. Même règle que `XpertComp.h` et
`DetailClipper.h` — tout algorithme non trivial sort de l'éditeur, sinon il
n'est vérifiable qu'à l'œil.

## ⚠️ Pièges déjà rencontrés — ne pas les redécouvrir

1. ⭐ **Le pôle simple : c'est la FORME, pas le nombre.** `z += a·(x−z)` avec
   `a = k/(1+k)` est faux — ce coefficient est celui du passe-bas *bilinéaire*,
   qui a un zéro à la Nyquist ; cette structure n'en a pas. Mesuré : −7,26 dB à
   la coupure au lieu de −3. **C'est la deuxième fois** (même bug sur l'épaule
   d'air de XPERT comp). Forme correcte : `y = c(x + x₋₁) + d·y₋₁`.
2. **Mesurer une pente près de la Nyquist ment.** La transformée bilinéaire
   comprime l'axe : entre 20 et 40 kHz à fs = 176,4 kHz, une octave en vaut 2,33,
   et un 4 pôles y mesure 29,3 dB au lieu de 24. Mesurer bien en dessous.
3. **`LANCER LES TESTS.cmd`** : ne jamais l'écrire avec `printf` — le `\r` de
   `tools\run-tests.ps1` est interprété comme un retour chariot et le chemin
   devient `toolsun-tests.ps1`. C'est l'origine du même défaut sur XPERT comp.
4. **Une machine à taux fixe ne peut pas avoir de filtre asservi** : son horloge
   ne bouge pas. Le mode `Asservi` n'a de sens que sur `Variable`.
5. ⭐ **`M_PI` n'est pas standard en C++.** Les tests le définissaient (via
   `_USE_MATH_DEFINES`), pas le plugin — d'où une compilation qui passait d'un
   côté et échouait de l'autre. `kPi` est déclaré dans `Grain.h`.
6. **Sans boucle, un rompleur n'en est pas un.** La première version bloquait la
   phase en fin de ROM : une nappe s'arrêtait au bout de deux secondes, et bien
   plus tôt transposée vers le haut. ⚠️ Pas de fondu de boucle : c'est au
   **matériau** de boucler sur un nombre entier de cycles, comme sur les machines.
7. ⚠️ **Le filtre de reconstruction coûte cher.** Un par voix d'unisson donnait
   **74 millions de biquads par seconde** à 16 voix. Il est au niveau de la
   COUCHE, en stéréo (2 filtres). Approximation assumée : sur la vraie machine
   chaque voix a son convertisseur et son filtre.
8. **En PowerShell, `-match` traite le motif comme une regex** : un chemin
   Windows y casse sur les antislashs. Utiliser `-like`.

## ⚠️ Valeurs à fiabilité incertaine

- `kCutoffRatio = 0.40` — coupure du filtre de reconstruction en fraction de
  l'horloge du DAC. **Aucune source ne la donne**, et c'est le paramètre le plus
  influent de toute la couche de grain. À chercher dans les schémas de
  l'Emulator II (autour du S3528).
- `kFsRomEII = 27700`, `kFsMachineJP = 32000` — sources secondaires.
- `kSurEch = 4` — compromis temps réel assumé, contre 8× dans le prototype
  Python. Les images au-delà de la Nyquist interne se replient au lieu de partir
  vers le tweeter comme sur la vraie machine.

## Invariants à ne pas casser

1. L'erreur de resynthèse se mesure **après** la dégradation.
2. Génération **déterministe et versionnée** (l'état d'un patch est une recette).
3. Le grain est un **étage du trajet de voix**, jamais un insert de sortie.
4. La recherche ne bouge que le **profil** (~31 paramètres).
5. On ne cherche pas ce qu'on peut **mesurer**.
6. Un macro doit **déplacer visiblement** les paramètres.
7. **Aucun échantillon livré.**
8. Accessibilité Serum / Pigments, pas menus Roland.
9. Presets machine = portes d'entrée, resynthèse = rampe d'accès.
10. **Toute mesure doit avoir un témoin.**


## Pièges 9 à 66 — index (détail dans le vault)

Le détail de chaque piège (symptôme, occurrences, correctif) est dans
`ROMANCE - Pièges 9 à 66.md`, dossier `ROMANCE/` du vault. **Le lire avant de toucher à
la zone concernée** : un banc, un test, la recherche, le filtre, le rack FX, le MIDI.

- **9** — NE JAMAIS mesurer une part d'aigu par `x - passeBas(x)`
- **10** — Le comptage de passages par zero n'est PAS une mesure de hauteur
- **11** — Un vote entre candidats dont un seul est deja ajuste ne mesure pas ce qu'on croit
- **12** — Une mesure de reference doit rendre le MEME objet que la recherche
- **13** — Un nombre magique qui depend d'une constante ailleurs est un test qui ment
- **14** — Un script d'outil qui echoue en silence coute plus cher qu'un crash
- **15** — Changer le rendu invalide le prior, meme si on n'a pas touche au prior
- **16** — « sonne encore » n'est pas « est tenue »
- **17** — Une dose de modulation se lit dans l'unite de sa destination
- **18** — Un limiteur mal regle AMPLIFIE
- **19** — Un piege documente peut survivre dans un test qu'on n'a pas relu
- **20** — Un test qui echoue sur du silence surveille la mauvaise chose
- **21** — Un fondu de boucle doit lire ce qui PRECEDE le point de retour
- **22** — Une vignette ne doit rien POINTER dans l'instantane
- **23** — Un fil qu'on peut tuer ne doit jamais posseder d'autres fils
- **24** — Un rapport « pas max / pas moyen » ne veut rien dire sur du silence
- **25** — Ne jamais saturer DANS une boucle a delai nul
- **26** — Un garde-fou contre NaN s'ecrit a l'envers
- **27** — Une transcendante qui ne depend pas du temps n'a rien a faire dans la boucle
- **28** — Le cout d'une note POSEE ne se voit dans aucune mesure de rendu
- **29** — Une mesure resumee sans son detail ne se verifie pas
- **30** — Un selecteur « le meilleur des deux » doit choisir sur le critere qui le jugera
- **31** — Partir plus bas n'est pas partir mieux
- **32** — Un test qui ne bouge rien ne voit pas un filtre qui diverge
- **33** — JUCE ne rafraichit pas la valeur brute d'un parametre booleen
- **34** — Un quantificateur doit tomber sur la grille du stockage
- **35** — Un octet mutable qui ne change rien est une dimension MORTE
- **36** — Un defaut de filtre qui rend tout sombre
- **37** — `$nom_` n'est pas `$nom` en PowerShell
- **38** — Corriger du code mort, et ne rien voir bouger
- **39** — Deux modes qui donnent le meme chiffre ne sont plus deux modes
- **40** — Deux numeros de version pour le meme format, et une etape morte
- **41** — On optimisait le premier pas d'un voyage qui n'arrive jamais
- **42** — Un banc qui ne peut pas voir l'effet qu'on lui demande de juger
- **43** — Un banc qui ne dit pas son bruit fabrique de faux resultats
- **44** — Quatre messages MIDI recus et jetes, pendant des mois
- **45** — Une bandelette de facade repond a « comment tester sans le materiel »
- **46** — Rien ne reliait une destination de modulation a son bouton
- **47** — La dose d'un cable cree par un geste doit se CALCULER
- **48** — Un banc qui ne peut pas s'ecraser lui-meme
- **49** — Une matrice qui s'applique APRES le declenchement rate tout ce qui se lit AU declenchement
- **50** — Le banc n'a pas pu voir sa propre correction (piege 42, 3e fois)
- **51** — Selectionner a travers la machine ne paie que sur des candidats DIVERSES
- **52** — Le reseau n'a JAMAIS ete branche dans le plugin
- **53** — Trois idees d'optimiseur, trois fois « rien de demontre »
- **54** — Le controle du cout d'un note-on n'existait pas
- **55** — Brancher le reseau, et surtout rendre son ABSENCE lisible
- **56** — Un marqueur d'echappement qui existe dans la langue
- **57** — Trois etages d'apprentissage, et chacun EMPIRE le resultat
- **58** — Un debit ne se transpose pas d'un outil sequentiel a un outil parallele
- **59** — Trois pannes de SCRIPT en deux jours, et un lint qui les attrape
- **60** — LA RECHERCHE NE RATE PAS. C'est l'objectif qui designe autre chose.
- **61** — Six idees d'optimiseur, et la seule significative est NEGATIVE
- **62** — Le rack master se reconstruisait a chaque battement, et on ne pouvait plus rien toucher
- **63** — Un libelle rogne se cherche dans la police, et se trouve dans une soustraction
- **64** — Deux selecteurs pour une seule notion
- **65** — Un refus silencieux se lit comme une panne
- **66** — Un test qui echoue au hasard est pire qu'un test absent
