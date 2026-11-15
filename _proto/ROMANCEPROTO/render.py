"""render.py - Rendus A/B pour l'ecoute.

Huit fichiers. Chacun isole UNE chose par rapport a un voisin, pour qu'un A/B
reponde a une question et pas a trois.

Chaque rendu joue la MEME ROM a cinq hauteurs (-24, -12, 0, +7, +12 demi-tons),
parce que c'est la transposition qui revele le grain : une machine a horloge
variable et une machine a interpolation sonnent presque pareil sur la note
d'origine, et se separent des qu'on s'en eloigne.

Tous les fichiers sortent alignes a -20 LKFS. NE PAS LES RENORMALISER.
"""

import os
import sys

import numpy as np
import soundfile as sf

import grain
import sources
from grain import Config

DOSSIER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rendus")
HAUTEURS = [-24, -12, 0, 7, 12]
DUREE_NOTE = 1.4
SILENCE = 0.25
FS = 44100.0

# nom de fichier -> (config, ce que l'A/B isole)
RENDUS = [
    ("00_temoin",
     Config("variable", "asservi", "aucune", ordre=4, cutoff_ratio=0.49),
     "reference : aucune quantification, filtre quasi transparent"),

    ("01_emulatorII",
     Config("variable", "asservi", "mulaw", ordre=4),
     "Emulator II complet : horloge variable + filtre asservi + mu-law"),

    ("02_EII_sans_mulaw",
     Config("variable", "asservi", "lin16", ordre=4),
     "vs 01 : isole la QUANTIFICATION mu-law"),

    ("03_EII_filtre_fixe",
     Config("variable", "fixe", "mulaw", ordre=4),
     "vs 01 : isole le FILTRE ASSERVI (le coeur de la trouvaille)"),

    ("04_EII_lin8",
     Config("variable", "asservi", "lin8", ordre=4),
     "vs 01 : mu-law contre 8 bits LINEAIRE, a nombre de bits egal"),

    ("05_EII_lin12",
     Config("variable", "asservi", "lin12", ordre=4),
     "vs 01 : le grain 12 bits (SP-1200 / S950)"),

    ("06_korgM1",
     Config("fixe", "fixe", "lin16", ordre=3),
     "Korg M1 : taux fixe + interpolation lineaire + 16 bits"),

    ("07_M1_mulaw",
     Config("fixe", "fixe", "mulaw", ordre=3),
     "vs 06 : machine qui n'a jamais existe, interpolation + companding"),
]


def sequence(rom, cfg):
    """Joue la ROM aux cinq hauteurs, bout a bout."""
    morceaux = []
    blanc = np.zeros(int(SILENCE * FS))
    for dt in HAUTEURS:
        y, _ = grain.joue(rom, cfg, demi_tons=dt, duree_s=DUREE_NOTE,
                          fs_sortie=FS, sur_ech=8)
        morceaux.append(y)
        morceaux.append(blanc)
    return np.concatenate(morceaux)


def main():
    os.makedirs(DOSSIER, exist_ok=True)

    if len(sys.argv) > 1:
        rom = sources.charge_wav(sys.argv[1])
        etiquette = os.path.splitext(os.path.basename(sys.argv[1]))[0]
        print("ROM = " + sys.argv[1])
    else:
        rom = sources.nappe(f0=220.0, duree=2.2)
        etiquette = "nappe"
        print("ROM = nappe de synthese (passer un WAV en argument pour la remplacer)")
    print("      %d echantillons a %.0f Hz  (%.2f s de ROM)\n"
          % (len(rom), grain.FS_ROM_EII, len(rom) / grain.FS_ROM_EII))

    print("%-22s %10s   %s" % ("fichier", "LKFS", "ce que l'A/B isole"))
    for nom, cfg, note in RENDUS:
        y = sequence(rom, cfg)
        y = grain.normalise_k(y, FS, cible=-20.0)
        crete = float(np.max(np.abs(y)))
        if crete > 0.99:                      # securite, jamais atteint en pratique
            y = y * (0.99 / crete)
        lk = grain.loudness_k(y, FS)
        chemin = os.path.join(DOSSIER, "%s_%s.wav" % (nom, etiquette))
        sf.write(chemin, y.astype(np.float32), int(FS), subtype="FLOAT")
        print("%-22s %10.2f   %s" % (nom, lk, note))

    print("\nEcrit dans : " + DOSSIER)
    print("Ordre d'ecoute conseille :")
    print("  1) 00 contre 01  - le grain complet contre la reference")
    print("  2) 01 contre 03  - LE test du filtre asservi. Ecouter la note aigue :")
    print("     avec 'asservi' elle garde le meme timbre, avec 'fixe' elle s'ouvre.")
    print("  3) 01 contre 04  - mu-law contre 8 bits lineaire (meme nombre de bits)")
    print("  4) 01 contre 06  - Emulator II contre Korg M1")


if __name__ == "__main__":
    main()
