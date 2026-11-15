"""sources.py - Materiel de test pour le banc de grain.

Deux familles, comme sur XPERTPROTO :
  - synthetique  -> pour MESURER (sinus, balayage, deux tons, salves)
  - musical      -> pour ECOUTER (nappe harmonique, cloche)

Les ROM sont generees a fs_rom (27,7 kHz par defaut, l'Emulator II), parce que
c'est la frequence a laquelle la machine les stocke.
"""

import numpy as np
import soundfile as sf

from grain import FS_ROM_EII


def sinus(f, duree, fs=FS_ROM_EII, amp=0.5):
    t = np.arange(int(duree * fs)) / fs
    return amp * np.sin(2 * np.pi * f * t)


def deux_tons(f1, f2, duree, fs=FS_ROM_EII, amp=0.35):
    t = np.arange(int(duree * fs)) / fs
    return amp * (np.sin(2 * np.pi * f1 * t) + np.sin(2 * np.pi * f2 * t))


def balayage(f1, f2, duree, fs=FS_ROM_EII, amp=0.5):
    """Balayage logarithmique."""
    t = np.arange(int(duree * fs)) / fs
    k = np.log(f2 / f1) / duree
    phase = 2 * np.pi * f1 * (np.exp(k * t) - 1.0) / k
    return amp * np.sin(phase)


def salves(f, duree, fs=FS_ROM_EII, fort=0.9, faible=0.01):
    """Alternance fort / faible - pour voir si le bruit de quantification SUIT
    le signal (mu-law) ou reste constant (lineaire)."""
    t = np.arange(int(duree * fs)) / fs
    s = np.sin(2 * np.pi * f * t)
    env = np.where((t % 1.0) < 0.5, fort, faible)
    return s * env


def nappe(f0=220.0, duree=3.0, fs=FS_ROM_EII, n_partiels=24, seed=7):
    """Nappe harmonique tenue : le cas d'usage vise par ROMANCE.

    Partiels en 1/n, legerement desaccordes et dephases pour que ca respire.
    Contient volontairement de l'energie jusqu'en haut du spectre - c'est ce
    contenu-la que les images de repliement vont abimer, donc c'est lui qui
    revele le grain.
    """
    rng = np.random.default_rng(seed)
    t = np.arange(int(duree * fs)) / fs
    y = np.zeros_like(t)
    for n in range(1, n_partiels + 1):
        fn = f0 * n * (1.0 + rng.normal(0.0, 0.0008))
        if fn > 0.45 * fs:
            break
        amp = 1.0 / (n ** 1.15)
        lfo = 1.0 + 0.06 * np.sin(2 * np.pi * rng.uniform(0.3, 1.7) * t
                                  + rng.uniform(0, 6.28))
        y += amp * lfo * np.sin(2 * np.pi * fn * t + rng.uniform(0, 6.28))
    # enveloppe douce d'attaque et de fin, pour eviter les clics de boucle
    n_att = int(0.05 * fs)
    env = np.ones_like(y)
    env[:n_att] = np.linspace(0, 1, n_att)
    env[-n_att:] = np.linspace(1, 0, n_att)
    y *= env
    return 0.7 * y / np.max(np.abs(y))


def cloche(f0=440.0, duree=3.0, fs=FS_ROM_EII, seed=3):
    """Partiels inharmoniques a decroissances differentes - revele les artefacts
    de transposition mieux qu'un son harmonique."""
    rng = np.random.default_rng(seed)
    t = np.arange(int(duree * fs)) / fs
    rapports = [0.56, 0.92, 1.19, 1.71, 2.00, 2.74, 3.00, 3.76, 4.07]
    y = np.zeros_like(t)
    for i, r in enumerate(rapports):
        fn = f0 * r
        if fn > 0.45 * fs:
            continue
        tau = duree / (1.5 + 0.9 * i)
        y += (1.0 / (1 + i)) * np.exp(-t / tau) * np.sin(2 * np.pi * fn * t
                                                         + rng.uniform(0, 6.28))
    return 0.7 * y / np.max(np.abs(y))


def charge_wav(chemin, fs_rom=FS_ROM_EII):
    """Charge un WAV utilisateur et le reechantillonne au taux de la ROM.

    C'est physiquement ce que fait la machine : elle numerise a SON taux.
    """
    from scipy import signal as sg
    x, fs = sf.read(chemin, always_2d=True)
    x = np.mean(x, axis=1)
    if fs != fs_rom:
        n = int(round(len(x) * fs_rom / fs))
        x = sg.resample(x, n)
    crete = np.max(np.abs(x))
    return 0.9 * x / crete if crete > 0 else x
