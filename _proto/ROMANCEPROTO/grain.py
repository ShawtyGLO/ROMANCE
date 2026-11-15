"""grain.py - Le grain du rompleur d'epoque.

Coeur DSP du prototype ROMANCE. Ne depend que de numpy + scipy.signal.

Trois briques, chacune a deux etats. Elles viennent toutes de la documentation
technique relevee le 2026-08-18 (voir « ROMANCE - Doc technique des grains ») :

  1. LECTURE          horloge variable (drop-sample) | taux fixe + interpolation
  2. RECONSTRUCTION   filtre asservi au taux, 24 dB/oct | filtre fixe, 18 dB/oct
  3. QUANTIFICATION   mu-law mu-255 (8 bits) | lineaire 12 ou 16 bits

Le point le plus important, et le moins intuitif : sur les machines a HORLOGE
VARIABLE (Emulator II, PPG, Prophet VS, D-50), transposer ne change pas le
rythme de lecture en memoire, ca change la FREQUENCE D'HORLOGE DU CONVERTISSEUR.
C'est litteralement une bande qu'on accelere. Le filtre de reconstruction est
asservi a cette horloge, donc la reponse en frequence ne bouge PAS quand on
transpose.

Les machines japonaises d'apres le D-50 font l'inverse : horloge de sortie fixe,
interpolation lineaire en memoire, filtre de reconstruction fixe.
"""

import numpy as np
from scipy import signal

# ---------------------------------------------------------------- constantes

FS_ROM_EII = 27700.0      # Emulator II (fiabilite moyenne, source secondaire)
FS_MACHINE_JP = 32000.0   # sortie fixe des rompleurs japonais post D-50
CUTOFF_RATIO = 0.40       # coupure du filtre de reconstruction, en fraction de
                          # la frequence d'horloge du DAC.
                          # ATTENTION : valeur SUPPOSEE, aucune source ne la donne.


# ------------------------------------------------------- 3. QUANTIFICATION

_MU_BIAS = 0x84
_MU_CLIP = 32635


def mulaw_encode(x):
    """Encodage G.711 mu-law (mu = 255), entier.

    Structure : 1 bit de signe + 3 bits de segment + 4 bits de pas.
    8 segments par polarite, 16 pas par segment, le pas DOUBLE d'un segment au
    suivant. C'est ce doublement qui fait que le bruit de quantification SUIT le
    signal - fort sur les cretes, discret dans les creux. Signature du companding.
    """
    xi = np.clip(np.rint(np.asarray(x, dtype=np.float64) * 32768.0),
                 -_MU_CLIP, _MU_CLIP)
    sign = np.where(xi < 0, 0x80, 0x00).astype(np.int64)
    mag = np.abs(xi).astype(np.int64) + _MU_BIAS
    seg = np.zeros_like(mag)
    for s in range(1, 8):
        seg = np.where(mag >= (1 << (s + 7)), s, seg)
    mant = (mag >> (seg + 3)) & 0x0F
    return (~(sign | (seg << 4) | mant)) & 0xFF


def mulaw_decode(u):
    """Decodage G.711 mu-law vers flottant [-1, 1]."""
    u = (~np.asarray(u, dtype=np.int64)) & 0xFF
    t = ((u & 0x0F) << 3) + _MU_BIAS
    t = t << ((u & 0x70) >> 4)
    t = t - _MU_BIAS
    val = np.where((u & 0x80) != 0, -t, t).astype(np.float64)
    return val / 32768.0


def quantifie(x, mode):
    """Quantifie une ROM. mode : 'mulaw', 'lin8', 'lin12', 'lin16', 'aucune'."""
    x = np.asarray(x, dtype=np.float64)
    if mode == "aucune":
        return x.copy()
    if mode == "mulaw":
        return mulaw_decode(mulaw_encode(x))
    if mode.startswith("lin"):
        bits = int(mode[3:])
        pas = 2.0 / (2 ** bits)
        return np.clip(np.rint(x / pas) * pas, -1.0, 1.0 - pas)
    raise ValueError("mode de quantification inconnu : " + str(mode))


# --------------------------------------------------------------- 1. LECTURE

def lit_horloge_variable(rom, fs_rom, ratio, n_hi, fs_hi):
    """Machine a HORLOGE VARIABLE (Emulator II, PPG, Prophet VS, D-50).

    L'horloge du convertisseur devient fs_rom * ratio. Le DAC maintient chaque
    valeur pendant une periode d'horloge entiere : c'est un escalier (maintien
    d'ordre zero) represente ici a fs_hi. Aucune interpolation, jamais.
    """
    fs_dac = fs_rom * ratio
    idx = np.floor(np.arange(n_hi, dtype=np.float64) * fs_dac / fs_hi).astype(np.int64)
    idx = np.clip(idx, 0, len(rom) - 1)
    return rom[idx], fs_dac


def lit_interpolation(rom, fs_rom, ratio, n_machine, fs_machine):
    """Machine a TAUX FIXE + INTERPOLATION LINEAIRE (Korg M1 et suite).

    Le rythme de lecture en memoire change, la frequence de sortie non.
    L'interpolation est simple - lineaire - comme sur les machines de l'epoque.
    """
    pas = fs_rom * ratio / fs_machine
    phase = np.arange(n_machine, dtype=np.float64) * pas
    i0 = np.floor(phase).astype(np.int64)
    frac = phase - i0
    i0 = np.clip(i0, 0, len(rom) - 2)
    return rom[i0] * (1.0 - frac) + rom[i0 + 1] * frac


def _zoh(x, fs_src, n_hi, fs_hi):
    """Escalier d'un DAC a taux fixe, porte a fs_hi."""
    idx = np.floor(np.arange(n_hi, dtype=np.float64) * fs_src / fs_hi).astype(np.int64)
    idx = np.clip(idx, 0, len(x) - 1)
    return x[idx]


# -------------------------------------------------------- 2. RECONSTRUCTION

def filtre_reconstruction(x, fs_hi, fc, ordre):
    """Filtre de reconstruction. ordre 4 = 24 dB/oct (SSM2045 de l'Emulator II),
    ordre 3 = 18 dB/oct (passe-bas mobile du S1000)."""
    fc = min(fc, 0.45 * fs_hi)
    sos = signal.butter(ordre, fc, btype="low", fs=fs_hi, output="sos")
    return signal.sosfilt(sos, x)


# ------------------------------------------------------------ chaine complete

class Config:
    """Une combinaison des trois briques."""

    def __init__(self, horloge="variable", filtre="asservi", quant="mulaw",
                 fs_rom=FS_ROM_EII, fs_machine=FS_MACHINE_JP,
                 ordre=4, cutoff_ratio=CUTOFF_RATIO, nom=None):
        self.horloge = horloge        # 'variable' | 'fixe'
        self.filtre = filtre          # 'asservi'  | 'fixe'
        self.quant = quant            # 'mulaw' | 'lin12' | 'lin16' | 'aucune'
        self.fs_rom = fs_rom
        self.fs_machine = fs_machine
        self.ordre = ordre
        self.cutoff_ratio = cutoff_ratio
        self.nom = nom or (horloge + "-" + filtre + "-" + quant)

    def __repr__(self):
        return "<Config " + self.nom + ">"


def joue(rom, cfg, demi_tons=0.0, duree_s=None, fs_sortie=44100.0, sur_ech=8):
    """Joue une ROM a travers la chaine complete et rend l'audio a fs_sortie.

    Le sur-echantillonnage interne (sur_ech x fs_sortie) sert a representer
    l'escalier du DAC et ses images AVANT le filtre de reconstruction. La
    decimation finale coupe au-dessus de la Nyquist de sortie : c'est le
    comportement correct, la vraie machine envoyait ces images au tweeter.
    """
    ratio = 2.0 ** (demi_tons / 12.0)
    rom_q = quantifie(rom, cfg.quant)
    fs_hi = fs_sortie * sur_ech

    if duree_s is None:
        duree_s = len(rom) / (cfg.fs_rom * ratio)
    n_hi = int(duree_s * fs_hi)

    if cfg.horloge == "variable":
        esc, fs_dac = lit_horloge_variable(rom_q, cfg.fs_rom, ratio, n_hi, fs_hi)
        fc = cfg.cutoff_ratio * (fs_dac if cfg.filtre == "asservi" else cfg.fs_rom)
    elif cfg.horloge == "fixe":
        n_m = int(duree_s * cfg.fs_machine) + 2
        y_m = lit_interpolation(rom_q, cfg.fs_rom, ratio, n_m, cfg.fs_machine)
        esc = _zoh(y_m, cfg.fs_machine, n_hi, fs_hi)
        # Une machine a taux fixe NE PEUT PAS avoir de filtre asservi : son
        # horloge ne bouge pas. Le parametre reste pour la symetrie du banc,
        # mais il n'a de sens physique que sur 'variable'.
        fc = cfg.cutoff_ratio * cfg.fs_machine
    else:
        raise ValueError("horloge inconnue : " + str(cfg.horloge))

    y = filtre_reconstruction(esc, fs_hi, fc, cfg.ordre)
    return decime(y, sur_ech), fs_sortie


def decime(y, facteur):
    """Decimation avec anti-repliement."""
    if facteur == 1:
        return y
    return signal.resample_poly(y, 1, facteur, padtype="line")


# ------------------------------------------------------- loudness BS.1770 (K)

def _ponderation_k(fs):
    """Biquads de la ponderation K (ITU-R BS.1770), recalcules pour tout fs."""
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / fs)
    Vh = 10.0 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b1 = np.array([(Vh + Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - Vh) / a0,
                   (Vh - Vb * K / Q + K * K) / a0])
    a1 = np.array([1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0])

    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / fs)
    a0 = 1.0 + K / Q + K * K
    b2 = np.array([1.0, -2.0, 1.0])
    a2 = np.array([1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0])
    return (b1, a1), (b2, a2)


def loudness_k(x, fs):
    """Loudness ponderee K en LKFS (mono)."""
    (b1, a1), (b2, a2) = _ponderation_k(fs)
    y = signal.lfilter(b1, a1, np.asarray(x, dtype=np.float64))
    y = signal.lfilter(b2, a2, y)
    ms = float(np.mean(y * y))
    if ms <= 0:
        return -np.inf
    return -0.691 + 10.0 * np.log10(ms)


def normalise_k(x, fs, cible=-20.0):
    """Aligne un signal sur une loudness K cible. Gain SCALAIRE uniquement.

    Rappel XPERT comp : un gain scalaire ne peut pas modifier un rapport entre
    deux composantes du signal. Normaliser ne masque donc aucune difference de
    timbre - seulement une difference de niveau.
    """
    lk = loudness_k(x, fs)
    if not np.isfinite(lk):
        return x
    return x * (10.0 ** ((cible - lk) / 20.0))


# ============================================================================
#  L'EPOQUE COMME COUCHE SEPARABLE
# ============================================================================
# Decision d'architecture (2026-08-18, proposee par l'utilisateur) : un COEUR
# de synthese propre, et le grain des machines par-dessus comme une couche.
# Melanger les deux embrouille et empeche de tester l'un sans l'autre.
#
# /!\ MAIS « couche » ne veut PAS dire « effet en insert a la fin ».
# La lecture (horloge, drop-sample, filtre asservi) se produit PENDANT le jeu,
# par voix et par note. Implementee en insert de sortie, elle perdrait le
# comportement a la transposition - c'est-a-dire exactement ce que la mesure T1
# a designe comme le coeur du caractere. C'est un ETAGE SEPARABLE du trajet de
# voix, pas un effet terminal.
#
# Et la quantification s'applique a la ROM (c'est une propriete du materiau
# STOCKE), pas au signal de sortie.

def _fir_zoh(fs, n=127):
    """Gabarit en |sinc(f/fs)| : la reponse du maintien d'ordre zero du DAC.

    A ratio = 1 les images sont au-dessus de la Nyquist et le filtre de
    reconstruction les enleve ; la seule trace qui reste dans la bande est ce
    leger affaissement de l'aigu. L'omettre ferait compenser la recherche pour
    quelque chose qui n'existe pas.
    """
    f = np.linspace(0.0, 1.0, 512)
    return signal.firwin2(n, f, np.sinc(f / 2.0))


class Degradation:
    """La couche d'epoque, evaluee a la hauteur d'origine.

    Version RAPIDE, appelee des dizaines de milliers de fois par la recherche ;
    `joue()` reste la version complete pour le rendu final.

    `actif=False` rend le COEUR NU - c'est ce qui permet de developper et de
    juger le moteur sans le grain, puis de rebrancher la couche sans rien
    changer d'autre.
    """

    def __init__(self, cfg, fs_rom=FS_ROM_EII, actif=True):
        self.cfg = cfg
        self.fs = fs_rom
        self.actif = actif
        self.fir = _fir_zoh(fs_rom)
        fc = min(cfg.cutoff_ratio * fs_rom, 0.49 * fs_rom)
        self.sos = signal.butter(cfg.ordre, fc, btype="low", fs=fs_rom,
                                 output="sos")

    def __call__(self, rom):
        if not self.actif:
            return np.asarray(rom, dtype=np.float64)
        q = quantifie(rom, self.cfg.quant)
        q = signal.oaconvolve(q, self.fir, mode="same")
        return signal.sosfilt(self.sos, q)
