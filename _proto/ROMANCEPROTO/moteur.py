"""moteur.py - Le moteur cible de la recherche : la ROM-RECETTE.

Decision d'architecture prise ici (2026-08-18) : la recherche ne produit PAS un
buffer d'echantillons, elle produit une RECETTE. C'est ce qui rend l'etat du
plugin minuscule (quelques centaines d'octets), morphable entre candidats, et
editable avec le vocabulaire d'un rompleur.

Forme retenue pour la v0 : ADDITIF a deux etats + morphing.
  - un jeu de N amplitudes harmoniques « debut »
  - un jeu de N amplitudes harmoniques « fin »
  - une constante de temps de morphing entre les deux
  - une enveloppe d'amplitude (attaque, decroissance, tenue)

Pourquoi celle-la : c'est exactement le mecanisme du PPG / de la Wavestation
(des formes d'onde qu'on traverse dans le temps), donc la recette sert AUSSI de
matiere a l'axe MOTEUR. Un seul objet couvre les deux besoins.

27 parametres. Au-dela d'une quarantaine, toute recherche s'effondre.
"""

import numpy as np

N_HARM = 12
DB_MIN = -60.0          # amplitude harmonique la plus faible representable
TAU_MIN, TAU_MAX = 0.02, 4.0
ATT_MIN, ATT_MAX = 0.002, 1.5
DEC_MIN, DEC_MAX = 0.05, 6.0

DET_MIN, DET_MAX = 0.3, 60.0      # desaccord entre voix, en cents

N_PARAMS = 2 * N_HARM + 5
NOMS = ([("harm_debut_%d" % (i + 1)) for i in range(N_HARM)]
        + [("harm_fin_%d" % (i + 1)) for i in range(N_HARM)]
        + ["morph_tau", "attaque", "decroissance", "desaccord", "battement"])


def _phases(seed=1234):
    """Phases FIXES. La generation doit etre deterministe : memes parametres,
    memes octets. C'est la condition qui permet de ne stocker que la recette."""
    rng = np.random.default_rng(seed)
    return rng.uniform(0.0, 2.0 * np.pi, N_HARM)


_PHI = _phases()


def _map01(v, lo, hi):
    return lo + np.clip(v, 0.0, 1.0) * (hi - lo)


def _map_log(v, lo, hi):
    return lo * (hi / lo) ** np.clip(v, 0.0, 1.0)


def decode(vecteur):
    """Vecteur dans [0,1]^27 -> parametres physiques."""
    v = np.asarray(vecteur, dtype=np.float64)
    a_db = _map01(v[:N_HARM], DB_MIN, 0.0)
    b_db = _map01(v[N_HARM:2 * N_HARM], DB_MIN, 0.0)
    tau = _map_log(v[2 * N_HARM + 0], TAU_MIN, TAU_MAX)
    att = _map_log(v[2 * N_HARM + 1], ATT_MIN, ATT_MAX)
    dec = _map_log(v[2 * N_HARM + 2], DEC_MIN, DEC_MAX)
    det = _map_log(v[2 * N_HARM + 3], DET_MIN, DET_MAX)
    bat = float(np.clip(v[2 * N_HARM + 4], 0.0, 1.0))
    return a_db, b_db, tau, att, dec, det, bat


_CACHE_BASE = {}


def base_harmonique(f0, duree, fs):
    """Les sinusoides du moteur, precalculees UNE fois.

    C'est l'optimisation qui rend la recherche possible : f0, les phases et le
    nombre d'harmoniques sont FIXES pendant toute une recherche, seules les
    AMPLITUDES changent. Recalculer des dizaines de milliers de fois les memes
    sin() coutait 90 % du temps de synthese.
    """
    cle = (round(f0, 6), round(duree, 6), round(fs, 3))
    if cle not in _CACHE_BASE:
        n = int(duree * fs)
        t = np.arange(n) / fs
        rangs = np.arange(1, N_HARM + 1)
        vivants = (f0 * rangs) < 0.45 * fs
        ondes = np.sin(2.0 * np.pi * f0 * np.outer(rangs[vivants], t)
                       + _PHI[vivants][:, None])
        _CACHE_BASE[cle] = (t, vivants, np.ascontiguousarray(ondes))
    return _CACHE_BASE[cle]


_PSI = np.random.default_rng(99).uniform(0.0, 2.0 * np.pi, N_HARM)
_SOUS_ECH_BAT = 8      # les battements sont lents : inutile de les calculer
                       # a la resolution du signal


def battements(f0, duree, fs, det_cents, profondeur, vivants):
    """Le BATTEMENT de plusieurs voix desaccordees, SANS ajouter de voix.

    ⭐ Deux partiels a f(1+e) et f(1-e) se somment EXACTEMENT en
       2 cos(2*pi*f*e*t) * sin(2*pi*f*t)
    c'est-a-dire une porteuse inchangee multipliee par une enveloppe lente.
    Donc on obtient le battement en MODULANT les sinusoides deja en cache, au
    lieu d'en synthetiser deux ou trois fois plus.

    /!\ C'est ce qui sauve l'optimisation centrale : la base harmonique reste
    valable (les porteuses ne bougent pas), donc le cout d'une evaluation ne
    triple pas. Empiler de vraies voix aurait invalide le cache a chaque
    changement de desaccord - le desaccord etant justement un parametre cherche.

    /!\ Le decalage en Hz croit avec le RANG (le desaccord est constant en
    cents) : le battement est donc plus rapide dans l'aigu. C'est physiquement
    juste, et c'est ce qui fait qu'un empilement desaccorde sonne vivant plutot
    que tremblotant.
    """
    eps = 2.0 ** (det_cents / 1200.0) - 1.0
    n = int(duree * fs)
    m = max(n // _SOUS_ECH_BAT, 2)
    t_c = np.arange(m) * (_SOUS_ECH_BAT / fs)
    rangs = np.arange(1, N_HARM + 1)[vivants]
    taux = f0 * rangs * eps                       # Hz, croissant avec le rang
    env = 1.0 + profondeur * np.cos(
        2.0 * np.pi * np.outer(taux, t_c) + _PSI[vivants][:, None])
    # retour a la resolution du signal
    t_plein = np.arange(n) / _SOUS_ECH_BAT
    i0 = np.clip(t_plein.astype(np.int64), 0, m - 2)
    fr = t_plein - i0
    return env[:, i0] * (1.0 - fr) + env[:, i0 + 1] * fr


def synthetise(vecteur, f0, duree, fs):
    """Recette -> ROM. Deterministe, sans aucune source d'aleatoire."""
    a_db, b_db, tau, att, dec, det, bat = decode(vecteur)
    t, vivants, ondes = base_harmonique(f0, duree, fs)
    n = len(t)
    if ondes.shape[0] == 0:
        return np.zeros(n)

    a = 10.0 ** (a_db / 20.0)
    b = 10.0 ** (b_db / 20.0)
    a[a_db <= DB_MIN + 1e-9] = 0.0
    b[b_db <= DB_MIN + 1e-9] = 0.0
    a, b = a[vivants], b[vivants]

    melange = 1.0 - np.exp(-t / tau)          # 0 -> 1, le morphing
    amps = a[:, None] + (b - a)[:, None] * melange[None, :]
    if bat > 1e-4:
        amps = amps * battements(f0, duree, fs, det, bat, vivants)
    y = np.einsum("kt,kt->t", amps, ondes)

    env = np.exp(-t / dec)
    n_att = max(int(att * fs), 1)
    if n_att < n:
        env[:n_att] *= np.linspace(0.0, 1.0, n_att)
    else:
        env *= np.linspace(0.0, 1.0, n)
    y *= env

    crete = float(np.max(np.abs(y)))
    return y / crete * 0.9 if crete > 0 else y


def distance_recettes(v1, v2):
    """Distance entre deux recettes, en dB moyens sur le spectre harmonique.
    Sert a repondre a la question ouverte : les 4 candidats sont-ils quatre
    reponses DIFFERENTES ou quatre nuances de la meme ?"""
    a1, b1, _, _, _ = decode(v1)
    a2, b2, _, _, _ = decode(v2)
    return float(np.mean(np.abs(np.concatenate([a1 - a2, b1 - b2]))))


def _pics_forts(spec, freqs, f_min, f_max, n_max=6, seuil=0.12):
    """Les raies les plus fortes du spectre, dans la plage utile."""
    m = (freqs >= f_min) & (freqs <= f_max)
    if not np.any(m):
        return np.array([]), np.array([])
    sp, fr = spec[m], freqs[m]
    haut = seuil * float(np.max(sp))
    inte = np.where((sp[1:-1] > sp[:-2]) & (sp[1:-1] >= sp[2:]) & (sp[1:-1] > haut))[0] + 1
    if len(inte) == 0:
        return np.array([]), np.array([])
    ordre = inte[np.argsort(sp[inte])[::-1]][:n_max]
    return fr[ordre], sp[ordre]


def estime_f0(x, fs, f_min=30.0, f_max=None):
    """Estimation de la fondamentale par autocorrelation.

    Elle est faite UNE FOIS, hors de la recherche. Laisser l'optimiseur chercher
    la hauteur lui-meme serait le meilleur moyen de ne jamais converger : une
    erreur d'un demi-ton rend toutes les autres dimensions illisibles.

    /!\\ Premiere version FAUSSE : elle prenait le maximum brut de
    l'autocorrelation entre deux bornes, et retournait donc systematiquement la
    BORNE (1204,3 Hz = fs/lag_min) des que l'epaule du lag 0 dominait. Deux des
    quatre samples de l'utilisateur, dont une basse, etaient estimes a 1204 Hz.
    Un estimateur qui rend sa borne n'estime pas, il echoue silencieusement.

    Version corrigee : produit spectral harmonique (robuste au fondamental
    faible ou absent), puis affinage par autocorrelation NORMALISEE, sur un
    extrait pris dans la TENUE et non dans l'attaque.
    """
    x = np.asarray(x, dtype=np.float64)
    n0 = len(x)
    if n0 < 1024:
        return 220.0
    # /!\ MEME DEFAUT QU'EN BAS, DECOUVERT EN HAUT. Le plafond fixe de 1200 Hz
    # faisait rendre EXACTEMENT une octave trop bas a 1760 Hz (-1199 cents, a
    # toutes les durees) : l'estimateur ne pouvait pas nommer la bonne valeur,
    # alors il rendait la plus proche qu'il savait dire. C'est la meme panne
    # silencieuse que la borne basse, a l'autre bout.
    if f_max is None:
        f_max = min(5000.0, 0.25 * fs)
    # on travaille dans la TENUE, detectee par le flux spectral et non par des
    # pourcentages fixes du fichier (voir segmente()).
    seg = tenue(x, fs)
    if len(seg) < 1024:
        seg = x
    seg = seg - np.mean(seg)
    n = len(seg)

    # /!\ BORNE BASSE IMPOSEE PAR LA DUREE D'ANALYSE.
    # Une frequence n'est estimable que si le segment contient assez de cycles.
    # La tenue d'un son percussif fait 50 ms : en dessous d'environ 80 Hz il n'y
    # a pas deux periodes completes, et toute valeur rendue est du bruit de
    # mesure. C'est exactement ce qui faisait choisir 36,7 Hz sur le keys - une
    # valeur que la fenetre ne pouvait PAS porter.
    # Corollaire que j'avais oublie en lisant les pics : a 50 ms la resolution
    # est de 20 Hz, donc annoncer « 151,5 Hz » etait une fausse precision.
    f_min = max(f_min, 4.0 * fs / n)

    # --- produit spectral harmonique
    nfft = 1 << int(np.ceil(np.log2(n * 2)))
    spec = np.abs(np.fft.rfft(seg * np.hanning(n), nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / fs)
    hps = spec.copy()
    for h in (2, 3, 4, 5):
        dec = spec[::h]
        hps[:len(dec)] *= dec
    m = (freqs >= f_min) & (freqs <= f_max)
    if not np.any(m):
        return 220.0
    idx = np.where(m)[0]
    f_hps = float(freqs[idx[int(np.argmax(hps[idx]))]])

    # --- correction d'octave
    # /!\\ Abaisser f_min a 30 Hz pour rattraper la basse a fait tomber le
    # produit harmonique sur un SOUS-harmonique pour le sample percussif
    # (36,7 Hz au lieu de 147 Hz, soit un quart de la vraie hauteur). Le produit
    # harmonique se trompe toujours vers le bas : f/2 et f/4 ont mecaniquement
    # du support harmonique des que f en a. On tranche par le SUPPORT REEL.
    def support(f):
        if f < f_min or f > f_max:
            return -1.0
        s, n_h = 0.0, 0
        for k in range(1, 7):
            fk = f * k
            if fk >= 0.45 * fs:
                break
            b = np.abs(freqs - fk) <= max(f * 0.06, freqs[1] * 2)
            if np.any(b):
                s += float(np.max(spec[b]))
                n_h += 1
        return s / max(n_h, 1)

    candidats = [f_hps * r for r in (1.0, 2.0, 3.0, 4.0)]
    scores = [support(f) for f in candidats]
    meilleur = float(np.max(scores))
    for f, sc in zip(candidats, scores):
        # on garde la hauteur la PLUS BASSE dont le support reste comparable au
        # meilleur : c'est la fondamentale, pas une de ses harmoniques.
        if sc >= 0.80 * meilleur:
            f_hps = f
            break

    # --- AUTO-VALIDATION par le profil harmonique
    # /!\ Ma premiere tentative de correction d'octave (comparer multiples pairs
    # et impairs) a CASSE un cas qui marchait - PAD 2 passait de 587 Hz juste a
    # 1054 Hz avec un fondamental a -55 dB - sans reparer le cas percussif.
    # Retiree. A la place, l'estimateur se CONTROLE : une hauteur dont le
    # fondamental n'a presque pas d'energie est fausse, quel que soit le
    # raisonnement qui y a mene. Un chiffre invraisemblable doit etre rejete par
    # le code, pas repere a l'oeil dans un tableau.
    def profil_db(f):
        ref = float(np.max(spec))
        out = []
        for k in range(1, 7):
            fk = f * k
            if fk >= 0.45 * fs:
                break
            b = np.abs(freqs - fk) <= max(f * 0.06, freqs[1] * 2)
            v = float(np.max(spec[b])) if np.any(b) else 1e-12
            out.append(20.0 * np.log10(max(v, 1e-12) / max(ref, 1e-12)))
        return out

    # Candidats : les multiples du produit harmonique ET les raies les plus
    # fortes du spectre. Sur le keys percussif, le produit harmonique se calait
    # sur le corps grave (42 Hz) alors que la tenue montrait une serie propre
    # sur 151,5 Hz - une raie forte, mais qu'aucun multiple de 42 n'atteignait.
    pics, _ = _pics_forts(spec, freqs, f_min, f_max)
    candidats = [f_hps * r for r in (1.0, 2.0, 3.0, 4.0)] + list(pics)

    retenus = []
    vus = []
    for f in candidats:
        if any(abs(f / v - 1.0) < 0.03 for v in vus):
            continue
        vus.append(f)
        if f < f_min or f > f_max:
            continue
        pr = profil_db(f)
        if not pr or pr[0] < -30.0:      # fondamental quasi vide -> hauteur fausse
            continue
        retenus.append((f, float(np.mean(pr))))
    if retenus:
        best = max(sc for _, sc in retenus)
        for f, sc in sorted(retenus):    # la plus BASSE encore credible
            if sc >= best - 3.0:
                f_hps = f
                break

    # --- affinage par autocorrelation normalisee autour de f_hps
    ac = np.fft.irfft(np.abs(np.fft.rfft(seg, nfft)) ** 2)[:n]
    if ac[0] <= 0:
        return f_hps
    ac = ac / ac[0]
    lag = int(round(fs / f_hps))
    lo, hi = max(int(lag * 0.8), 2), min(int(lag * 1.25) + 1, n - 1)
    if hi > lo:
        lag = lo + int(np.argmax(ac[lo:hi]))
    # interpolation parabolique sur le pic : sans elle la resolution est d'un
    # echantillon de retard, soit ~2 % d'erreur vers 600 Hz - 35 cents, assez
    # pour decaler tous les harmoniques et brouiller l'analyse.
    if 0 < lag < len(ac) - 1:
        y0, y1, y2 = ac[lag - 1], ac[lag], ac[lag + 1]
        den = y0 - 2.0 * y1 + y2
        if abs(den) > 1e-12:
            lag = lag + 0.5 * (y0 - y2) / den
    f_ac = fs / lag
    # on ne garde l'affinage que s'il reste dans le meme voisinage
    return float(f_ac if 0.8 < f_ac / f_hps < 1.25 else f_hps)


def _inutilise(x, fs, f_min, f_max):
    """conserve pour memoire - ne pas utiliser"""
    return 220.0


def analyse(cible, f0, fs, part_debut=0.25, part_fin=0.45):
    """Lit directement dans la cible les amplitudes harmoniques du moteur.

    ON NE CHERCHE PAS CE QU'ON PEUT MESURER. Pour un moteur additif harmonique,
    les 24 amplitudes ne sont pas des inconnues : elles se lisent au spectre de
    la cible. Les laisser a l'optimiseur, c'est lui donner 27 dimensions quand
    il n'y en a que 3 de reellement cachees (morphing, attaque, decroissance).

    Le resultat n'est pas la reponse finale : la perte est mesuree APRES la
    degradation d'epoque, que cette analyse ignore. C'est un POINT DE DEPART,
    que la recherche affine ensuite pour compenser le companding et le filtre.
    C'est exactement le role que j'avais reserve a un eventuel reseau de
    neurones - une simple analyse harmonique le remplit.
    """
    x = np.asarray(cible, dtype=np.float64)
    n = len(x)

    def amplitudes(seg):
        if len(seg) < 64:
            return np.full(N_HARM, 0.0)
        w = np.hanning(len(seg))
        spec = np.abs(np.fft.rfft(seg * w, len(seg) * 2))
        freqs = np.fft.rfftfreq(len(seg) * 2, 1.0 / fs)
        amps = np.zeros(N_HARM)
        for k in range(N_HARM):
            fk = f0 * (k + 1)
            if fk >= 0.45 * fs:
                break
            # somme sur un voisinage : la fondamentale n'est jamais pile sur un bin
            m = np.abs(freqs - fk) <= max(f0 * 0.25, fs / len(seg))
            if np.any(m):
                amps[k] = float(np.max(spec[m]))
        return amps

    a = amplitudes(x[:int(part_debut * n)])
    b = amplitudes(x[int((1.0 - part_fin) * n):])

    def en_v(amps):
        ref = float(np.max(amps))
        if ref <= 0:
            return np.zeros(N_HARM)
        db = 20.0 * np.log10(np.maximum(amps, 1e-12) / ref)
        return np.clip((db - DB_MIN) / (0.0 - DB_MIN), 0.0, 1.0)

    # enveloppe, pour l'attaque et la decroissance
    env = np.abs(signal_enveloppe(x, fs))
    crete = float(np.max(env)) if len(env) else 0.0
    i_crete = int(np.argmax(env)) if len(env) else 0
    att = max(i_crete / fs, ATT_MIN)
    fin = float(np.mean(env[int(0.85 * n):])) if n > 10 else crete
    if crete > 0 and fin > 0 and fin < crete:
        dec = (n / fs) / np.log(crete / fin)
    else:
        dec = DEC_MAX
    dec = float(np.clip(dec, DEC_MIN, DEC_MAX))

    def inv_log(val, lo, hi):
        return float(np.clip(np.log(val / lo) / np.log(hi / lo), 0.0, 1.0))

    v = np.zeros(N_PARAMS)
    v[:N_HARM] = en_v(a)
    v[N_HARM:2 * N_HARM] = en_v(b)
    v[2 * N_HARM + 0] = inv_log(0.4, TAU_MIN, TAU_MAX)     # morphing : inconnu
    v[2 * N_HARM + 1] = inv_log(np.clip(att, ATT_MIN, ATT_MAX), ATT_MIN, ATT_MAX)
    v[2 * N_HARM + 2] = inv_log(dec, DEC_MIN, DEC_MAX)
    return v


def signal_enveloppe(x, fs, tau=0.02):
    """Suiveur d'enveloppe simple, un pole."""
    a = np.exp(-1.0 / (tau * fs))
    return _lissage(np.abs(x), a)


def _lissage(x, a):
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = a * acc + (1.0 - a) * x[i]
        y[i] = acc
    return y


# ------------------------------------------------- segmentation attaque / tenue

ATT_MAX_S = 0.35        # au-dela, ce n'est plus une attaque mais un son qui evolue


def segmente(x, fs, n=1024, saut=256, seuil_flux=0.25, seuil_db=-45.0):
    """Separe ATTAQUE et TENUE avant toute analyse.

    Formulation de l'utilisateur (2026-08-18), qui est aussi la these de la
    synthese LA du D-50 : un son percussif se lit comme « une attaque, un signal
    qui perd tres vite en timbre, puis un sustain plus facile a comprendre ».

    Traduction mesurable : l'attaque est la region ou le TIMBRE CHANGE VITE.
    On la detecte au FLUX SPECTRAL - la somme des augmentations d'une trame a
    la suivante. La tenue commence quand le flux retombe.

    /!\ C'est ce qui manquait : l'ancienne version analysait un extrait FIXE
    (15 % a 75 % du fichier), ce qui melangeait attaque et tenue sur un keys
    joue percussivement - exactement le cas intermediaire ou l'estimation de
    hauteur echouait.

    Retourne (i_debut, i_tenue, i_fin) en echantillons.
    """
    x = np.asarray(x, dtype=np.float64)
    if len(x) < n * 2:
        return 0, 0, len(x)

    nb = 1 + (len(x) - n) // saut
    idx = np.arange(n)[None, :] + saut * np.arange(nb)[:, None]
    trames = x[idx] * np.hanning(n)[None, :]
    spec = np.abs(np.fft.rfft(trames, axis=1))

    # niveau, pour ecarter le silence de tete et de queue
    niv = np.sqrt(np.mean(trames ** 2, axis=1)) + 1e-12
    niv_db = 20.0 * np.log10(niv / np.max(niv))
    vivant = np.where(niv_db > seuil_db)[0]
    if len(vivant) == 0:
        return 0, 0, len(x)
    t0, t1 = int(vivant[0]), int(vivant[-1])

    # flux spectral : seules les AUGMENTATIONS comptent (une decroissance
    # d'amplitude n'est pas un changement de timbre)
    sn = spec / (np.max(spec, axis=1, keepdims=True) + 1e-12)
    flux = np.zeros(nb)
    flux[1:] = np.sum(np.maximum(sn[1:] - sn[:-1], 0.0), axis=1)
    # lissage court
    k = np.ones(3) / 3.0
    flux = np.convolve(flux, k, mode="same")

    # /!\ Une ATTAQUE est par definition AU DEBUT. Chercher le maximum de flux
    # sur tout le fichier donnait « 7209 ms d'attaque » sur une basse : le pic
    # se trouvait au milieu d'une tenue modulee. On borne la fenetre.
    f_pic = int(0.20 * fs / saut) + 1          # le pic d'attaque est dans 200 ms
    f_max_att = int(ATT_MAX_S * fs / saut) + 1  # au-dela ce n'est plus une attaque
    seg = flux[t0:min(t0 + f_pic, t1 + 1)]
    if len(seg) < 2 or np.max(seg) <= 0:
        return t0 * saut, t0 * saut, min((t1 + 1) * saut + n, len(x))
    i_pic = int(np.argmax(seg))
    seuil = seuil_flux * float(np.max(seg))
    fin_rech = min(t0 + i_pic + f_max_att, t1 + 1)
    apres = np.where(flux[t0 + i_pic:fin_rech] < seuil)[0]
    i_tenue = t0 + i_pic + (int(apres[0]) if len(apres) else f_max_att)
    i_tenue = min(i_tenue, t1)

    return (t0 * saut,
            i_tenue * saut,
            min((t1 + 1) * saut + n, len(x)))


def tenue(x, fs):
    """L'extrait sur lequel toute analyse harmonique doit se faire."""
    _, i_t, i_f = segmente(x, fs)
    seg = x[i_t:i_f]
    # /!\ La tenue d'un son percussif est COURTE (50 ms mesures sur le keys de
    # l'utilisateur). Retomber sur le fichier entier des qu'elle fait moins de
    # 2048 echantillons remettait l'attaque dans l'analyse - donc rendait la
    # segmentation inoperante precisement sur le cas pour lequel elle existe.
    return seg if len(seg) >= 1024 else x
