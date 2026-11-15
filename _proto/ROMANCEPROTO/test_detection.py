"""test_detection.py - Verites de terrain sur la detection.

Ces valeurs viennent de l'UTILISATEUR (la note jouee dans son DAW) ou d'une
verification spectrale independante. Elles protegent contre les regressions -
et il y en a deja eu une : une correction d'octave « pair/impair » avait fait
passer PAD 2 de 587 Hz (juste) a 1054 Hz sans que rien ne le signale.

Lancer :  ./.venv/Scripts/python.exe test_detection.py
"""

import os
import sys

import numpy as np
import soundfile as sf
from scipy import signal as sg

import grain
import moteur

DOSSIER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FS = grain.FS_ROM_EII

# fichier -> (f0 attendue en Hz, tolerance en cents, origine de la verite)
CAS = {
    "PERCUSIVE KEY 1.wav": (146.83, 60, "D3 releve dans le DAW par l'utilisateur"),
    "PAD 1.wav":           (293.55, 60, "raies mesurees 291-296 Hz"),
    "PAD 2.wav":           (587.41, 60, "raies mesurees 573-581 Hz"),
    "BASS 1.wav":          (36.72, 60, "raies mesurees 36,8 et 73,5 Hz"),
}


def charge(nom):
    x, fs = sf.read(os.path.join(DOSSIER, nom), always_2d=True)
    m = np.mean(x, axis=1)
    return sg.resample(m, int(round(len(m) * FS / fs)))


def main():
    echecs = 0
    print("%-22s %10s %10s %9s  %s" % ("fichier", "attendu", "mesure", "ecart", "origine"))
    for nom, (attendu, tol, origine) in CAS.items():
        chemin = os.path.join(DOSSIER, nom)
        if not os.path.exists(chemin):
            print("%-22s   ABSENT" % nom)
            continue
        f0 = moteur.estime_f0(charge(nom), FS)
        cents = 1200.0 * np.log2(f0 / attendu)
        ok = abs(cents) <= tol
        echecs += 0 if ok else 1
        print("%-22s %9.2f %9.2f %+8.0f c  %s %s"
              % (nom, attendu, f0, cents, "OK " if ok else "ECHEC", origine))

    # la segmentation doit trouver une attaque BREVE (elle est au debut)
    print()
    for nom in CAS:
        chemin = os.path.join(DOSSIER, nom)
        if not os.path.exists(chemin):
            continue
        x = charge(nom)
        i0, it, i1 = moteur.segmente(x, FS)
        att_ms = (it - i0) / FS * 1000.0
        ok = att_ms <= moteur.ATT_MAX_S * 1000.0 + 200.0
        echecs += 0 if ok else 1
        print("%-22s attaque %6.0f ms  %s" % (nom, att_ms, "OK" if ok else "ECHEC"))

    print("\n%d echec(s)" % echecs)
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(main())
