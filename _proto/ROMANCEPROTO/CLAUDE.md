# ROMANCEPROTO — prototype du grain

Prototype Python du grain de lecture d'un rompleur d'epoque. Etape 1 du projet
ROMANCE (voir le vault Obsidian, dossier `ROMANCE/`). **Aucune IA ici** : cette
etape ne repond qu'a une question, « ou est le grain ».

## Lancer

```
./.venv/Scripts/python.exe measure.py          # banc de mesure (T1, T2, T3)
./.venv/Scripts/python.exe render.py           # 8 rendus A/B dans rendus/
./.venv/Scripts/python.exe render.py mon.wav   # meme chose sur un WAV a soi
```

## Fichiers

- `grain.py` — coeur DSP. Trois briques : lecture, reconstruction, quantification.
- `sources.py` — materiel de test (synthetique pour mesurer, nappe pour ecouter).
- `measure.py` — banc de mesure.
- `render.py` — rendus A/B normalises a -20 LKFS.

## Pieges deja rencontres — ne pas les redecouvrir

1. **La DSP d'un bruit est elle-meme bruitee.** La detection de coupure a -3 dB
   sans lissage donnait des resultats NON MONOTONES (une coupure qui descend
   quand on transpose vers le haut). Un `medfilt` de 31 points a suffi. Toujours
   verifier la monotonie d'une mesure censee etre monotone.

2. **Un test d'artefacts sans TEMOIN ne mesure rien.** La premiere version de T3
   affichait -50 dB partout et j'ai failli en conclure quelque chose. Le temoin
   (sinus pur synthetise directement) est a -59 dB : c'est le plancher de MA
   fenetre d'analyse, pas celui de la machine. Sans lui, 9 dB de marge auraient
   ete lus comme 50.

3. **Vers le BAS, T1 ne discrimine pas.** Transposer vers le grave ramene le
   contenu de la ROM sous la coupure : c'est la ROM qui limite, pas le filtre.
   Seule la transposition vers le HAUT teste le filtre asservi.

4. **Une machine a taux fixe ne peut pas avoir de filtre asservi.** Son horloge
   ne bouge pas. Le parametre existe dans `Config` pour la symetrie du banc mais
   n'a de sens physique que sur `horloge='variable'` — deux des huit cases du
   produit cartesien sont vides.

5. **Ne pas renormaliser les rendus.** Ils sortent alignes a 0,01 dB pres en
   loudness K. Une passe supplementaire ajoute de l'erreur. (Lecon XPERT comp.)

## Valeurs a fiabilite incertaine

- `CUTOFF_RATIO = 0.40` — coupure du filtre de reconstruction en fraction de
  l'horloge du DAC. **Aucune source ne la donne.** C'est le parametre le plus
  influent du modele : a verifier sur les schemas de l'Emulator II.
- `FS_ROM_EII = 27700` — source secondaire, pas le manuel de service.
- `FS_MACHINE_JP = 32000` — source secondaire.
