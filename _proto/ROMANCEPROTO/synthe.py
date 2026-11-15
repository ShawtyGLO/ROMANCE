"""synthe.py - Le moteur de ROMANCE : couches, unisson, bruit, modulation.

Remplace `moteur.py`, qui etait un jouet additif a 29 parametres ayant servi a
repondre a la question qui decidait du projet. Ici on construit un vrai moteur
avec des briques connues - on ne re-derive pas un synthetiseur par la mesure.

VERSION DU GENERATEUR : voir VERSION. /!\\ Elle doit changer a chaque
modification qui altere le son rendu pour une recette donnee, et les anciennes
versions doivent rester jouables. Sans ca, une mise a jour du plugin change le
son de tous les projets existants - l'etat ne contient qu'une recette.

Structure d'une voix :

    COUCHE A --+
    COUCHE B --+--> melange --> ampli de voix
    BRUIT    --+

Le GRAIN (grain.py) est un etage SEPARE, applique dans le trajet de voix.
Il n'est pas ici : ce module est le COEUR NU.
"""

import numpy as np

VERSION = 1

# --------------------------------------------------------------- constantes

N_HARM = 12
N_VOIX = 3                      # unisson fige (decision utilisateur)
DB_MIN = -60.0
TAU_MIN, TAU_MAX = 0.02, 4.0
ATT_MIN, ATT_MAX = 0.002, 1.5
DEC_MIN, DEC_MAX = 0.05, 6.0
DET_MIN, DET_MAX = 0.5, 60.0    # desaccord d'unisson, en cents
LFO_MIN, LFO_MAX = 0.05, 8.0    # vitesse du LFO de hauteur, en Hz
PROF_MAX = 60.0                 # profondeur du LFO de hauteur, en cents

N_DET = 8                       # /!\\ desaccord QUANTIFIE sur 8 valeurs : c'est
                                # ce qui permet de garder une base en cache.
                                # Contrainte de PROTOTYPE uniquement - dans le
                                # plugin la synthese est par echantillon, avec
                                # des accumulateurs de phase, et le probleme
                                # n'existe pas.

_PHI = np.random.default_rng(1234).uniform(0.0, 2.0 * np.pi, N_HARM * N_VOIX)


# ------------------------------------------------------------------- profil

class Profil:
    """Le sous-ensemble que la RECHERCHE a le droit de bouger.

    Tout le reste du synthe existe pour l'humain. C'est ce qui empeche un moteur
    riche de faire s'effondrer la recherche (elle decroche au-dela de 40-50
    parametres ; Synplant en cherche 48, et c'est tout son moteur).

    Trois ETAGES, parce que les parametres n'ont pas le meme cout d'evaluation :

      etage 1 - CHER a chercher, PAS CHER a evaluer (base harmonique en cache)
      etage 2 - PEU de parametres, mais evaluation cheres (ils invalident le cache)
      etage 3 - affinage global

    /!\\ C'est ce decoupage qui rend l'unisson et le LFO abordables : ils
    coutent 3x plus par evaluation, mais ils ne sont que 3 parametres, donc
    quelques centaines d'evaluations suffisent.
    """

    ETAGE1 = ([("harm_debut_%d" % (i + 1)) for i in range(N_HARM)]
              + [("harm_fin_%d" % (i + 1)) for i in range(N_HARM)]
              + ["morph_tau", "attaque", "decroissance"])
    ETAGE2 = ["desaccord", "lfo_vitesse", "lfo_profondeur", "niveau_bruit"]
    NOMS = ETAGE1 + ETAGE2

    N = len(NOMS)
    I_ETAGE1 = list(range(len(ETAGE1)))
    I_ETAGE2 = list(range(len(ETAGE1), len(NOMS)))

    # ce qui est MESURE, jamais cherche
    MESURE = ["f0", "frontiere attaque/tenue", "enveloppe spectrale du bruit",
              "amorcage des amplitudes", "machine (grain) - choisie par l'utilisateur"]


class Contexte:
    """Ce que l'ANALYSE fournit et que la recherche n'a pas a trouver."""

    def __init__(self, f0, env_bruit=None, centres=None, saut_bruit=None,
                 n_bruit=None):
        self.f0 = float(f0)
        self.env_bruit = env_bruit          # (trames, bandes) ou None
        self.centres = centres
        self.saut_bruit = saut_bruit
        self.n_bruit = n_bruit


# ------------------------------------------------------------------ mapping

def _m01(v, lo, hi):
    return lo + np.clip(v, 0.0, 1.0) * (hi - lo)


def _mlog(v, lo, hi):
    return lo * (hi / lo) ** np.clip(v, 0.0, 1.0)


def decode(vecteur):
    v = np.asarray(vecteur, dtype=np.float64)
    p = {}
    p["a_db"] = _m01(v[:N_HARM], DB_MIN, 0.0)
    p["b_db"] = _m01(v[N_HARM:2 * N_HARM], DB_MIN, 0.0)
    i = 2 * N_HARM
    p["tau"] = _mlog(v[i + 0], TAU_MIN, TAU_MAX)
    p["att"] = _mlog(v[i + 1], ATT_MIN, ATT_MAX)
    p["dec"] = _mlog(v[i + 2], DEC_MIN, DEC_MAX)
    # desaccord QUANTIFIE (voir N_DET)
    cran = int(np.clip(np.round(np.clip(v[i + 3], 0, 1) * (N_DET - 1)), 0, N_DET - 1))
    p["cran_det"] = cran
    p["det"] = DET_MIN * (DET_MAX / DET_MIN) ** (cran / (N_DET - 1.0))
    p["lfo_f"] = _mlog(v[i + 4], LFO_MIN, LFO_MAX)
    p["lfo_p"] = _m01(v[i + 5], 0.0, PROF_MAX)
    p["niv_bruit"] = float(np.clip(v[i + 6], 0.0, 1.0))
    return p


# ------------------------------------------------------------ base en cache

_CACHE = {}


def base(f0, det_cents, duree, fs, avec_unisson=True):
    """Les sinusoides de l'unisson, precalculees.

    Cle : (f0, cran de desaccord, duree, fs). Le desaccord etant quantifie sur
    N_DET valeurs, le nombre de bases possibles reste petit - et elles sont
    construites A LA DEMANDE, donc en pratique la recherche n'en visite que
    deux ou trois.
    """
    cle = (round(f0, 6), round(det_cents, 4), round(duree, 6), round(fs, 3),
           bool(avec_unisson))
    if cle in _CACHE:
        return _CACHE[cle]
    n = int(duree * fs)
    t = np.arange(n) / fs
    ecarts = (np.linspace(-1.0, 1.0, N_VOIX) if (avec_unisson and N_VOIX > 1)
              else np.zeros(1))
    lignes, rangs = [], []
    for iv, e in enumerate(ecarts):
        fv = f0 * 2.0 ** (e * det_cents / 1200.0)
        for k in range(1, N_HARM + 1):
            if fv * k >= 0.45 * fs:
                lignes.append(None)
                rangs.append(k - 1)
                continue
            lignes.append(np.sin(2.0 * np.pi * fv * k * t
                                 + _PHI[iv * N_HARM + (k - 1)]))
            rangs.append(k - 1)
    ondes = np.array([l if l is not None else np.zeros(n) for l in lignes])
    res = (t, np.ascontiguousarray(ondes), np.array(rangs), len(ecarts))
    _CACHE[cle] = res
    return res


# ------------------------------------------------------------------ synthese

def rend(vecteur, ctx, duree, fs, avec_unisson=True, avec_lfo=True,
         avec_bruit=True):
    """Recette + contexte -> audio. Deterministe, aucune source d'aleatoire.

    Les trois interrupteurs servent l'ETAGE 1 de la recherche : sans unisson ni
    LFO, la base reste en cache et une evaluation coute ~7 ms au lieu de ~20.
    """
    p = decode(vecteur)
    f0 = ctx.f0
    det = p["det"] if avec_unisson else 0.0
    t, ondes, rangs, n_voix = base(f0, det, duree, fs, avec_unisson)
    n = len(t)

    a = 10.0 ** (p["a_db"] / 20.0)
    b = 10.0 ** (p["b_db"] / 20.0)
    a[p["a_db"] <= DB_MIN + 1e-9] = 0.0
    b[p["b_db"] <= DB_MIN + 1e-9] = 0.0

    melange = 1.0 - np.exp(-t / p["tau"])
    amp_h = a[:, None] + (b - a)[:, None] * melange[None, :]      # (N_HARM, n)
    amp = amp_h[rangs] / n_voix                                    # par ligne

    if avec_lfo and p["lfo_p"] > 1e-3:
        # LFO vers la HAUTEUR. Le decalage en radians vaut k * m(t), donc on
        # applique l'addition d'angles sur la base en cache :
        #   sin(theta + km) = sin(theta) cos(km) + cos(theta) sin(km)
        # /!\\ On ne dispose que de sin(theta) en cache. Pour de faibles
        # profondeurs (< ~100 cents) on approche cos(theta) par sin decale, ce
        # qui n'est pas exact : on prefere ici la voie honnete et directe -
        # moduler la PHASE - au prix du cache. C'est pourquoi le LFO vit a
        # l'ETAGE 2 de la recherche, ou les evaluations sont peu nombreuses.
        eps = 2.0 ** (p["lfo_p"] / 1200.0) - 1.0
        y = np.zeros(n)
        ecarts = (np.linspace(-1.0, 1.0, N_VOIX) if (avec_unisson and N_VOIX > 1)
                  else np.zeros(1))
        idx = 0
        for iv, e in enumerate(ecarts):
            fv = f0 * 2.0 ** (e * det / 1200.0)
            # /!\ UN LFO PAR VOIX, decorrele. Un LFO global deplace toutes les
            # voix ENSEMBLE : l'analyseur suit un pic unique qui se promene, et
            # la largeur mesuree plafonnait a 43,7 cents pour une cible a 84.
            # Un vrai chorus, lui, module chaque copie independamment - c'est la
            # DERIVE RELATIVE entre voix qui elargit la raie, pas le deplacement
            # commun. Phases et vitesses legerement differentes par voix.
            f_lfo = p["lfo_f"] * (1.0 + 0.17 * iv)
            psi = 2.0 * np.pi * iv / max(len(ecarts), 1)
            m = eps * np.sin(2.0 * np.pi * f_lfo * t + psi)
            dphi = 2.0 * np.pi * np.cumsum(m) / fs
            for k in range(1, N_HARM + 1):
                if fv * k < 0.45 * fs:
                    ph = 2.0 * np.pi * fv * k * t + k * dphi
                    y += amp[idx] * np.sin(ph + _PHI[iv * N_HARM + (k - 1)])
                idx += 1
    else:
        y = np.einsum("kt,kt->t", amp, ondes)

    # enveloppe d'amplitude
    env = np.exp(-t / p["dec"])
    n_att = max(int(p["att"] * fs), 1)
    if n_att < n:
        env[:n_att] *= np.linspace(0.0, 1.0, n_att)
    else:
        env *= np.linspace(0.0, 1.0, n)
    y = y * env

    # couche de BRUIT : son enveloppe spectrale est MESUREE, seul le niveau est
    # cherche. C'est l'invariant « on ne cherche pas ce qu'on peut mesurer ».
    if avec_bruit and ctx.env_bruit is not None and p["niv_bruit"] > 1e-4:
        import decomposition as D
        bruit = D.synthese_bruit(ctx.env_bruit, fs, n, ctx.n_bruit,
                                 ctx.saut_bruit, seed=VERSION)
        r_h = float(np.sqrt(np.mean(y ** 2))) + 1e-12
        r_b = float(np.sqrt(np.mean(bruit ** 2))) + 1e-12
        y = y + bruit * (p["niv_bruit"] * r_h / r_b)

    crete = float(np.max(np.abs(y)))
    return y / crete * 0.9 if crete > 0 else y


def depart_par_analyse(cible, ctx, fs):
    """Point de depart lu dans la cible (invariant : on ne cherche pas ce qu'on
    peut mesurer). Delegue a l'analyse harmonique deja validee."""
    import moteur
    v_ancien = moteur.analyse(cible, ctx.f0, fs)
    v = np.full(Profil.N, 0.5)
    v[:2 * N_HARM + 3] = v_ancien[:2 * N_HARM + 3]
    v[2 * N_HARM + 3] = 0.25      # desaccord modere
    v[2 * N_HARM + 4] = 0.35      # LFO lent
    v[2 * N_HARM + 5] = 0.15      # profondeur faible
    v[2 * N_HARM + 6] = 0.10      # peu de bruit
    return v
