"""measure.py - Banc de mesure du grain.

Trois tests, chacun conçu pour trancher UNE question. Ils ne remplacent pas
l'ecoute : ils disent si le mecanisme fait bien ce que la documentation annonce.

  T1  Le filtre asservi rend-il la reponse invariante a la transposition ?
  T2  Le bruit de quantification mu-law suit-il vraiment le signal ?
  T3  Drop-sample et interpolation lineaire produisent-ils des artefacts
      differents, et de quelle ampleur ?
"""

import numpy as np
from scipy import signal

import grain
import sources
from grain import Config

FS_MES = 96000.0   # Nyquist a 48 kHz : indispensable pour voir une coupure
                   # asservie qui monte quand on transpose vers le haut.


def _psd(y, fs, nperseg=8192):
    f, p = signal.welch(y, fs=fs, nperseg=nperseg, noverlap=nperseg // 2)
    return f, p


def coupure_3db(y, fs, f_ref=(200.0, 1500.0)):
    """Frequence a -3 dB, mesuree par rapport au plateau basse frequence.

    ATTENTION : la DSP d'un bruit est elle-meme bruitee. Sans lissage, un creux
    aleatoire declenche la detection bien avant la vraie coupure - c'est le
    defaut qui rendait cette mesure non monotone a la premiere execution.
    """
    f, p = _psd(y, fs)
    p = signal.medfilt(p, kernel_size=31)      # lissage indispensable
    m = (f >= f_ref[0]) & (f <= f_ref[1])
    if not np.any(m) or np.median(p[m]) <= 0:
        return float("nan")
    plateau = np.median(p[m])
    seuil = plateau / 2.0                      # -3 dB en puissance
    au_dessus = f > f_ref[1]
    idx = np.where(au_dessus & (p < seuil))[0]
    return float(f[idx[0]]) if len(idx) else float("nan")


def bande(y, fs, f1, f2):
    """Energie dans une bande, en dB."""
    f, p = _psd(y, fs)
    m = (f >= f1) & (f < f2)
    e = float(np.sum(p[m]))
    return 10.0 * np.log10(e) if e > 0 else -np.inf


# --------------------------------------------------------------------- T1

def t1_invariance():
    print("\n=== T1  Le filtre asservi rend-il la reponse invariante ? ===")
    print("    ROM = bruit blanc. On lit la coupure a -3 dB de la chaine.")
    print("    Attendu : 'asservi' -> la coupure suit la transposition")
    print("              'fixe'    -> la coupure ne bouge pas\n")
    rng = np.random.default_rng(1)
    rom = 0.3 * rng.standard_normal(int(6.0 * grain.FS_ROM_EII))

    demi_tons = [-24, -12, 0, 7, 12]
    print("    %-10s %10s %10s %12s" % ("demi-tons", "asservi", "fixe", "attendu"))
    res = {}
    for filtre in ("asservi", "fixe"):
        res[filtre] = []
        for dt in demi_tons:
            cfg = Config(horloge="variable", filtre=filtre, quant="aucune", ordre=4)
            y, fs = grain.joue(rom, cfg, demi_tons=dt, duree_s=3.0,
                               fs_sortie=FS_MES, sur_ech=8)
            res[filtre].append(coupure_3db(y, fs))
    for i, dt in enumerate(demi_tons):
        att = grain.CUTOFF_RATIO * grain.FS_ROM_EII * (2.0 ** (dt / 12.0))
        print("    %-10d %10.0f %10.0f %12.0f"
              % (dt, res["asservi"][i], res["fixe"][i], att))

    print("\n    /!\\ Vers le BAS la mesure ne discrimine pas : la ROM elle-meme")
    print("        est ramenee sous la coupure, c'est elle qui limite, pas le filtre.")
    print("        La zone qui tranche est la transposition vers le HAUT.")
    a = np.array(res["asservi"])
    f = np.array(res["fixe"])
    r_a = a[-1] / a[0]
    r_f = f[-1] / f[0]
    ideal = 2.0 ** ((demi_tons[-1] - demi_tons[0]) / 12.0)
    print("\n    Rapport coupure haut/bas : asservi %.2f x  |  fixe %.2f x"
          % (r_a, r_f))
    print("    Ideal si parfaitement asservi : %.2f x   (invariance totale)" % ideal)
    return res


# --------------------------------------------------------------------- T2

def t2_bruit_suit_signal():
    print("\n=== T2  Le bruit de quantification suit-il le signal ? ===")
    print("    Mesure DIRECTE de l'erreur de quantification sur la ROM,")
    print("    sur une salve forte (0,9) puis faible (0,01).\n")
    fs = grain.FS_ROM_EII
    rom = sources.salves(220.0, 4.0, fs=fs, fort=0.9, faible=0.01)
    t = np.arange(len(rom)) / fs
    m_fort = (t % 1.0) < 0.5
    m_faible = ~m_fort

    print("    %-10s %12s %12s %12s" % ("quant.", "bruit fort", "bruit faible", "ecart"))
    for mode in ("mulaw", "lin8", "lin12", "lin16"):
        q = grain.quantifie(rom, mode)
        err = q - rom
        e_fort = 20 * np.log10(np.sqrt(np.mean(err[m_fort] ** 2)) + 1e-30)
        e_faible = 20 * np.log10(np.sqrt(np.mean(err[m_faible] ** 2)) + 1e-30)
        print("    %-10s %12.1f %12.1f %12.1f"
              % (mode, e_fort, e_faible, e_fort - e_faible))
    print("\n    Un ecart NUL = bruit constant (lineaire).")
    print("    Un ecart POSITIF = le bruit suit le signal (companding).")


# --------------------------------------------------------------------- T3

def t3_artefacts():
    print("\n=== T3  Artefacts de lecture : drop-sample contre interpolation ===")
    print("    ROM = sinus pur. Tout ce qui n'est pas la fondamentale attendue")
    print("    est un artefact (images, repliement, erreur d'interpolation).\n")
    f0 = 1000.0
    rom = sources.sinus(f0, 4.0)

    print("    %-28s %8s %12s %12s"
          % ("configuration", "dt", "fond. (Hz)", "artefacts dB"))
    combos = [
        ("TEMOIN (aucune degradation)", None),
        ("horloge variable + asservi", Config("variable", "asservi", "aucune", ordre=4)),
        ("horloge variable + fixe   ", Config("variable", "fixe", "aucune", ordre=4)),
        ("taux fixe + interpolation ", Config("fixe", "fixe", "aucune", ordre=3)),
    ]
    for dt in (0, 7, 12):
        for nom, cfg in combos:
            f_att = f0 * (2.0 ** (dt / 12.0))
            if cfg is None:
                # Temoin : le meme sinus, synthetise directement a f_att et a la
                # frequence de mesure. Tout ce qu'il affiche est un artefact de
                # MON analyse (fuite de fenetre), pas du grain. Sans ce temoin,
                # les chiffres de T3 ne veulent rien dire.
                fs = FS_MES
                t = np.arange(int(1.5 * fs)) / fs
                y = 0.5 * np.sin(2 * np.pi * f_att * t)
            else:
                y, fs = grain.joue(rom, cfg, demi_tons=dt, duree_s=1.5,
                                   fs_sortie=FS_MES, sur_ech=8)
            f, p = _psd(y, fs, nperseg=16384)
            # fenetre autour de la fondamentale
            larg = 40.0
            m_f = np.abs(f - f_att) <= larg
            e_fond = float(np.sum(p[m_f]))
            e_tot = float(np.sum(p[(f > 20.0)]))
            e_art = max(e_tot - e_fond, 1e-30)
            db = 10 * np.log10(e_art / e_fond) if e_fond > 0 else float("nan")
            print("    %-28s %8d %12.0f %12.1f" % (nom, dt, f_att, db))
        print()
    print("    Plus le chiffre est BAS, plus la lecture est propre.")
    print("    Un chiffre proche du TEMOIN = artefact NON mesurable par ce test.")


if __name__ == "__main__":
    t1_invariance()
    t2_bruit_suit_signal()
    t3_artefacts()
