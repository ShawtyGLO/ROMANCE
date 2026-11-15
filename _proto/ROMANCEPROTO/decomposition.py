"""decomposition.py - Separer la cible en HARMONIQUE / TRANSITOIRE / BRUIT.

C'est la brique qui debloque les deux problemes restants d'un coup :

  - le CORPS GRAVE d'un son percussif n'est pas une hauteur, c'est du transitoire ;
  - le DESACCORD qui manquait aux nappes se MESURE ici au lieu de se chercher.

Principe directeur du projet, confirme deux fois par la mesure :
**on ne cherche pas ce qu'on peut mesurer.** Une recherche a 27 dimensions
partait de 0,364 (hasard) ; une simple analyse du spectre demarrait a 0,083.
Ici on va plus loin : les amplitudes ET les frequences reelles de chaque
harmonique se lisent directement dans le signal, image par image.

Tout ce module travaille sur le COEUR NU. Aucune degradation d'epoque.
"""

import numpy as np
from scipy import signal

import moteur


# --------------------------------------------------------------- analyse

def taille_fenetre(f0, fs, bins_par_harmonique=12.0):
    """Fenetre assez longue pour SEPARER deux harmoniques voisines.

    /!\\ Une fenetre trop courte melange les harmoniques et l'analyse mesure une
    bouillie. A 36,7 Hz (la basse de l'utilisateur) il faut plus de 3000 points
    la ou 2048 suffisent a 587 Hz. La fenetre doit suivre la hauteur, pas etre
    une constante.
    """
    besoin = bins_par_harmonique * fs / max(f0, 1.0)
    n = 1 << int(np.ceil(np.log2(max(besoin, 512.0))))
    return int(min(n, 16384))


def suit_harmoniques(x, fs, f0, n_harm=32, saut=None):
    """Suit chaque harmonique dans le temps : amplitude ET frequence REELLE.

    La frequence reelle est le point important. Supposer que l'harmonique k est
    exactement a k*f0 revient a postuler un son parfaitement harmonique - or
    c'est precisement ce qui manquait au moteur : PAD 1 montre un paquet de
    raies a 291-296 Hz, pas une raie unique. Le DESACCORD est ici mesure.

    Retourne (amps[k, t], freqs[k, t], saut, n_fenetre).
    """
    x = np.asarray(x, dtype=np.float64)
    n = taille_fenetre(f0, fs)
    if saut is None:
        saut = n // 4
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))

    nb = 1 + (len(x) - n) // saut
    idx = np.arange(n)[None, :] + saut * np.arange(nb)[:, None]
    fen = np.hanning(n)
    trames = x[idx] * fen[None, :]
    spec = np.fft.rfft(trames, axis=1)
    mag = np.abs(spec)
    freqs = np.fft.rfftfreq(n, 1.0 / fs)
    df = freqs[1]
    # correction du gain de la fenetre (Hann : somme = n/2)
    mag = mag * (2.0 / np.sum(fen))

    n_utile = min(n_harm, int(0.45 * fs / f0))
    amps = np.zeros((n_utile, nb))
    fres = np.zeros((n_utile, nb))

    for k in range(n_utile):
        fk = f0 * (k + 1)
        b0 = int(np.floor((fk - 0.5 * f0) / df))
        b1 = int(np.ceil((fk + 0.5 * f0) / df))
        b0, b1 = max(b0, 1), min(b1, mag.shape[1] - 2)
        if b1 <= b0:
            continue
        loc = np.argmax(mag[:, b0:b1 + 1], axis=1) + b0
        a0 = mag[np.arange(nb), loc - 1]
        a1 = mag[np.arange(nb), loc]
        a2 = mag[np.arange(nb), loc + 1]
        # interpolation parabolique sur le LOG des amplitudes : le sommet d'une
        # raie vue par une fenetre de Hann est bien mieux approche par une
        # parabole en dB qu'en lineaire. Sur l'amplitude brute, le biais se
        # voyait dans le temoin.
        l0 = np.log(np.maximum(a0, 1e-20))
        l1 = np.log(np.maximum(a1, 1e-20))
        l2 = np.log(np.maximum(a2, 1e-20))
        den = l0 - 2.0 * l1 + l2
        delta = np.where(np.abs(den) > 1e-20,
                         0.5 * (l0 - l2) / np.where(den == 0, 1e-20, den), 0.0)
        delta = np.clip(delta, -0.5, 0.5)
        fres[k] = (loc + delta) * df
        amps[k] = np.exp(l1 - 0.25 * (l0 - l2) * delta)
    return amps, fres, saut, n


def synthese_harmonique(amps, fres, fs, n_ech, saut):
    """Resynthese par oscillateurs a PHASE CONTINUE.

    /!\\ La phase doit etre integree, pas redemarree a chaque trame : sinon on
    entend un bourdonnement au rythme du saut d'analyse. On interpole
    lineairement amplitude et frequence entre les trames.
    """
    n_h, nb = amps.shape
    y = np.zeros(n_ech)
    t_tr = np.arange(nb) * saut
    t = np.arange(n_ech)
    for k in range(n_h):
        if np.max(amps[k]) <= 0:
            continue
        a = np.interp(t, t_tr, amps[k])
        f = np.interp(t, t_tr, fres[k])
        phase = 2.0 * np.pi * np.cumsum(f) / fs
        y += a * np.sin(phase)
    return y


def residu_spectral(x, fs, fres, f0, n, saut):
    """Residu obtenu en VIDANT les bins des harmoniques, image par image.

    /!\ CORRECTIF DU TEMOIN. Soustraire dans le domaine temporel une resynthese
    dont la PHASE n'est pas celle de l'original ne retire rien : ca ADDITIONNE
    deux signaux decorreles. Mesure : le residu pesait 161 % de l'energie de
    depart sur un son pourtant parfaitement harmonique.

    On peut mesurer la phase et la suivre (c'est ce que fait la modelisation
    sinusoidale classique), mais pour notre besoin le residu n'a pas a etre
    rejouable tel quel - seule son ENVELOPPE nous interesse. Vider les bins est
    donc plus simple ET insensible a la phase.
    """
    x = np.asarray(x, dtype=np.float64)
    n_ech = len(x)
    if n_ech < n:
        x = np.pad(x, (0, n - n_ech))
    nb = 1 + (len(x) - n) // saut
    fen = np.hanning(n)
    df = fs / n
    # /!\ La largeur a vider est celle du LOBE PRINCIPAL de la fenetre (Hann :
    # +-2 bins), PAS la moitie de l'ecart entre harmoniques. Avec f0 = 292 Hz et
    # des bins de 13,5 Hz, ma premiere version vidait +-11 bins autour de chaque
    # rang - soit la quasi-totalite du spectre. Le residu tombait alors a 0,0 %
    # partout, ce qui est aussi faux que les 615 943 % d'avant, juste moins
    # voyant. Une valeur trop belle merite la meme suspicion qu'une valeur absurde.
    largeur = 3

    y = np.zeros(len(x))
    poids = np.zeros(len(x))
    for i in range(nb):
        d = i * saut
        sp = np.fft.rfft(x[d:d + n] * fen)
        for k in range(fres.shape[0]):
            fk = fres[k, min(i, fres.shape[1] - 1)]
            if fk <= 0:
                continue
            c = int(round(fk / df))
            b0, b1 = max(c - largeur, 0), min(c + largeur + 1, len(sp))
            sp[b0:b1] = 0.0
        y[d:d + n] += np.fft.irfft(sp, n) * fen
        poids[d:d + n] += fen ** 2
    # /!\ DEFAUT DE BORD. Aux extremites, la somme des fenetres au carre tend
    # vers zero : diviser par elle amplifie enormement. Sur un fichier court
    # (le keys, 0,86 s) le residu ressortait a 615 943 % de l'energie d'origine.
    # Un chiffre absurde est un bug, pas une mesure. On ne garde que la zone ou
    # le recouvrement est reellement etabli, et on eteint les bords.
    ref = float(np.median(poids[poids > 0])) if np.any(poids > 0) else 1.0
    valide = poids > 0.30 * ref
    out = np.zeros_like(y)
    out[valide] = y[valide] / poids[valide]
    return out[:n_ech]


# --------------------------------------------------------------- residu

def bandes_log(n_fft, fs, n_bandes=24, f_min=40.0, f_max_ratio=0.45):
    """Banc triangulaire log, pour decrire une enveloppe de BRUIT."""
    freqs = np.fft.rfftfreq(n_fft, 1.0 / fs)
    bords = np.geomspace(f_min, f_max_ratio * fs, n_bandes + 2)
    banc = np.zeros((n_bandes, len(freqs)))
    for i in range(n_bandes):
        g, c, d = bords[i], bords[i + 1], bords[i + 2]
        m = (freqs >= g) & (freqs <= c)
        banc[i, m] = (freqs[m] - g) / max(c - g, 1e-9)
        m = (freqs > c) & (freqs <= d)
        banc[i, m] = (d - freqs[m]) / max(d - c, 1e-9)
    return banc, bords[1:-1]


def enveloppe_bruit(res, fs, n=1024, saut=256, n_bandes=24):
    """Enveloppe spectrale du residu, en bandes, dans le temps.

    On ne conserve PAS la forme d'onde du residu - seulement son enveloppe.
    C'est ce qui le rend descriptible par une recette au lieu d'un fichier.
    """
    res = np.asarray(res, dtype=np.float64)
    if len(res) < n:
        res = np.pad(res, (0, n - len(res)))
    nb = 1 + (len(res) - n) // saut
    idx = np.arange(n)[None, :] + saut * np.arange(nb)[:, None]
    trames = res[idx] * np.hanning(n)[None, :]
    p = np.abs(np.fft.rfft(trames, axis=1)) ** 2
    banc, centres = bandes_log(n, fs, n_bandes)
    e = p @ banc.T
    return np.sqrt(np.maximum(e, 0.0)), centres, saut, n


def synthese_bruit(env, fs, n_ech, n, saut, seed=1):
    """Bruit dont l'enveloppe spectrale suit `env`, par recouvrement-addition.

    Deterministe : meme graine, memes octets. La condition qui permet de ne
    stocker que la recette.
    """
    rng = np.random.default_rng(seed)
    nb, n_bandes = env.shape
    banc, _ = bandes_log(n, fs, n_bandes)
    poids = banc.sum(axis=0)
    poids[poids <= 0] = 1.0
    y = np.zeros(n_ech + n)
    fen = np.hanning(n)
    for i in range(nb):
        # gabarit d'amplitude par bin, obtenu en repartissant l'energie de
        # chaque bande sur ses bins (les triangles se recouvrent, d'ou `poids`)
        gabarit = (env[i] @ banc) / poids
        # amplitude imposee, PHASE ALEATOIRE : c'est ce qui fait du bruit et non
        # une impulsion. On ne conserve pas la forme d'onde du residu, seulement
        # son enveloppe - c'est ce qui le rend descriptible par une recette.
        phase = rng.uniform(0.0, 2.0 * np.pi, len(gabarit))
        sp = gabarit * np.exp(1j * phase)
        sp[0] = sp[0].real
        seg = np.fft.irfft(sp, n) * fen
        d = i * saut
        if d + n <= len(y):
            y[d:d + n] += seg
    # compensation du recouvrement des fenetres de Hann
    return y[:n_ech] * (saut / (np.sum(fen ** 2) / n * n))


# --------------------------------------------------------------- ensemble

class Decomposition:
    """Le resultat complet, et de quoi le rejouer."""

    def __init__(self, x, fs, f0=None):
        self.fs = fs
        self.x = np.asarray(x, dtype=np.float64)
        self.f0 = f0 if f0 is not None else moteur.estime_f0(self.x, fs)
        self.i0, self.i_tenue, self.i_fin = moteur.segmente(self.x, fs)

        self.amps, self.fres, self.saut_h, self.n_h = suit_harmoniques(
            self.x, fs, self.f0)
        self.harmonique = synthese_harmonique(
            self.amps, self.fres, fs, len(self.x), self.saut_h)
        self.residu = residu_spectral(self.x, fs, self.fres, self.f0,
                                      self.n_h, self.saut_h)
        self.env, self.centres, self.saut_b, self.n_b = enveloppe_bruit(
            self.residu, fs)

    # -- ce que la decomposition APPREND, et qui n'etait pas mesurable avant

    PLANCHER_HARM_DB = -60.0

    def harmoniques_presents(self):
        """Masque des harmoniques qui portent reellement de l'energie.

        /!\ Sans ce masque, toute statistique globale est FAUSSE. Le temoin
        annoncait -31 cents de desaccord sur un son parfaitement harmonique :
        l'estimateur etait juste a +-0,5 cent sur les 12 harmoniques reels, mais
        on suivait 32 rangs et les 20 vides - dont la frequence mesuree n'est que
        du bruit - dominaient la mediane.
        *L'outil de mesure mentait, pas la chose mesuree.* Troisieme fois sur ce
        projet : toujours verifier qu'un resume porte sur des donnees vivantes.
        """
        pics = np.max(self.amps, axis=1)
        ref = float(np.max(pics)) if len(pics) else 0.0
        if ref <= 0:
            return np.zeros(len(pics), dtype=bool)
        return pics >= ref * 10.0 ** (self.PLANCHER_HARM_DB / 20.0)

    def resume(self, valeurs):
        """Resume pondere par l'energie, sur les seuls harmoniques presents."""
        m = self.harmoniques_presents() & np.isfinite(valeurs)
        if not np.any(m):
            return float("nan")
        poids = np.max(self.amps, axis=1)[m]
        return float(np.sum(valeurs[m] * poids) / np.sum(poids))

    def desaccord_cents(self):
        """Ecart de chaque harmonique a k*f0, en cents, pondere par l'amplitude.

        C'est la mesure qui manquait : un son parfaitement harmonique donne 0
        partout ; un son a plusieurs voix desaccordees donne un ecart non nul et
        qui varie avec le rang.
        """
        out = []
        presents = self.harmoniques_presents()
        for k in range(self.amps.shape[0]):
            a = self.amps[k]
            if not presents[k] or np.sum(a) <= 0:
                out.append(np.nan)
                continue
            f_moy = float(np.sum(self.fres[k] * a) / np.sum(a))
            out.append(1200.0 * np.log2(max(f_moy, 1e-9) / (self.f0 * (k + 1))))
        return np.array(out)

    def largeur_raies_cents(self):
        """Dispersion TEMPORELLE de la frequence de chaque harmonique.

        Une raie unique et stable donne ~0. Plusieurs voix desaccordees qui
        battent font osciller la frequence apparente : c'est la signature du
        battement, et elle se voit ici.
        """
        out = []
        presents = self.harmoniques_presents()
        for k in range(self.amps.shape[0]):
            a = self.amps[k]
            if not presents[k] or np.sum(a) <= 0:
                out.append(np.nan)
                continue
            f_moy = float(np.sum(self.fres[k] * a) / np.sum(a))
            var = float(np.sum(a * (self.fres[k] - f_moy) ** 2) / np.sum(a))
            out.append(1200.0 * np.log2(1.0 + np.sqrt(var) / max(f_moy, 1e-9)))
        return np.array(out)

    def parts_energie(self):
        """Repartition de l'energie entre les trois couches."""
        e = lambda v: float(np.sum(np.asarray(v) ** 2)) + 1e-30
        e_tot = e(self.x)
        att = slice(self.i0, self.i_tenue)
        return {
            "harmonique": e(self.harmonique) / e_tot,
            "residu": e(self.residu) / e_tot,
            "transitoire": e(self.x[att]) / e_tot,
        }

    def reconstruit(self, avec_bruit=True, seed=1):
        y = self.harmonique.copy()
        if avec_bruit:
            y = y + synthese_bruit(self.env, self.fs, len(self.x),
                                   self.n_b, self.saut_b, seed=seed)
        return y


# ------------------------------------------------------------- POLYPHONIE

def multi_f0(x, fs, n_max=4, seuil_db=-24.0, f_min=30.0):
    """Detecte plusieurs hauteurs, par soustraction successive.

    Un son n'est pas forcement une note : PAD 1 s'est revele etre un ACCORD
    (une quinte), ce qui invalidait toute une serie de mesures faites en
    supposant une hauteur unique.

    ⭐ Rien de neuf ici : on applique en boucle ce que le module sait deja
    faire. Estimer la hauteur, suivre ses harmoniques, les retirer du spectre,
    recommencer sur le residu. La polyphonie ne demande pas un autre outil, elle
    demande d'appeler le meme plusieurs fois.

    /!\ COLLISION DE PARTIELS : deux notes en rapport harmonique PARTAGENT des
    partiels (une quinte : 293 x 3 = 880 = 440 x 2). L'energie commune est donc
    attribuee a la premiere note trouvee, et la seconde ressort sous-estimee.
    C'est une limite connue de la methode, pas un bug - mais elle interdit de
    lire les amplitudes relatives entre notes comme une verite.

    Retourne une liste de (f0, energie relative), la plus forte d'abord.
    """
    import moteur
    x = np.asarray(x, dtype=np.float64)
    e_tot = float(np.sum(x ** 2)) + 1e-30
    reste = x.copy()
    trouvees = []

    for _ in range(n_max):
        e_reste = float(np.sum(reste ** 2))
        if 10.0 * np.log10(max(e_reste, 1e-30) / e_tot) < seuil_db:
            break
        f0 = moteur.estime_f0(reste, fs, f_min=f_min)
        if not np.isfinite(f0) or f0 <= 0:
            break
        # /!\ Rejeter les candidats en RAPPORT HARMONIQUE avec une note deja
        # trouvee : ce sont presque toujours des restes de soustraction, pas une
        # seconde note. On teste f0/f et f/f0 pour de petits entiers.
        def parent(f0, f):
            for m in (1, 2, 3, 4, 5, 6):
                for r in (f0 / (f * m), (f0 * m) / f):
                    if abs(1200.0 * np.log2(r)) < 35.0:
                        return True
            return False
        if any(parent(f0, f) for f, _ in trouvees):
            break
        # /!\ COUVERTURE COMPLETE. Avec n_harm=32 par defaut, les rangs 33+ de
        # la premiere note survivaient a la soustraction ; la detection suivante
        # se calait dessus et rendait l'OCTAVE de la note deja trouvee au lieu
        # de la seconde note. Sur un accord construit expres (re4 + la4), le
        # temoin rendait re5. Il faut retirer TOUS les rangs jusqu'a Nyquist.
        n_tous = max(int(0.45 * fs / f0), 1)
        amps, fres, saut, n = suit_harmoniques(reste, fs, f0, n_harm=n_tous)
        harm = synthese_harmonique(amps, fres, fs, len(reste), saut)
        e_h = float(np.sum(harm ** 2))
        trouvees.append((f0, e_h / e_tot))
        reste = residu_spectral(reste, fs, fres, f0, n, saut)

    return trouvees
