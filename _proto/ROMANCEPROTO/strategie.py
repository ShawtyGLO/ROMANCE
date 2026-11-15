"""strategie.py - Recherche par BLOCS avec feuille de route mesuree.

Idee de l'utilisateur (2026-08-18) : plutot qu'une seule recherche sur tous les
parametres, en traiter des paquets a la suite, du plus structurel au plus fin,
puis revenir verifier ce qui etait reste en suspens, et cycler jusqu'a
convergence.

C'est de la DESCENTE PAR BLOCS avec un ordre de curriculum. Les etages 1 et 2
ecrits a la main n'en etaient qu'un cas particulier a deux blocs.

Trois principes, dont deux ajoutes a la proposition initiale :

  1. L'ORDRE SE MESURE, il ne se devine pas. Une passe de sensibilite suffit.
     Mesure sur PAD 2 : l'ecart d'influence entre parametres va de 1 a plus de
     400. Les traiter a egalite depense l'essentiel du budget pour rien.

  2. L'ordre est RECALCULE entre les cycles. La sensibilite vaut en UN point ;
     quand la recherche s'est deplacee, les priorites changent. Un ordre fige
     optimiserait le deuxieme cycle avec les priorites du premier.

  3. /!\\ La descente par blocs BUTE SUR LES CRETES : quand deux parametres
     interagissent, bouger l'un seul degrade, bouger l'autre seul degrade, mais
     les bouger ensemble ameliore - et aucun bloc isole ne peut le voir.
     C'est pourquoi les CYCLES et la PASSE CONJOINTE finale ne sont pas
     facultatifs. Un seul passage bloc par bloc s'arrete sur la premiere crete.

/!\\ CONDITION TECHNIQUE : la perte est TOUJOURS calculee sur le signal complet,
jamais sur le residu d'un bloc. Sinon chaque bloc optimise un objectif different
et la somme ne converge vers rien.
"""

import time

import numpy as np
import cma


def sensibilite(perte, v, indices=None, pas=0.15):
    """Effet de chaque parametre sur la perte, au point courant."""
    v = np.asarray(v, dtype=np.float64)
    if indices is None:
        indices = range(len(v))
    base = perte(v)
    out = np.zeros(len(v))
    for k in indices:
        e = 0.0
        for d in (-pas, pas):
            w = v.copy()
            w[k] = float(np.clip(w[k] + d, 0.0, 1.0))
            e = max(e, abs(perte(w) - base))
        out[k] = e
    return out


def blocs(sens, taille=8, seuil_rel=1e-3):
    """Regroupe les parametres par influence decroissante.

    Les parametres dont l'effet est negligeable devant le plus fort sont
    ecartes du cycle : leur chercher une valeur, c'est depenser du budget a
    regler ce qui ne s'entend pas.
    """
    ordre = np.argsort(sens)[::-1]
    vivants = [int(k) for k in ordre if sens[k] > seuil_rel * max(sens.max(), 1e-30)]
    return [vivants[i:i + taille] for i in range(0, len(vivants), taille)]


def _cma_sur(perte, v, idx, budget_s, sigma=0.20, popsize=8, seed=0):
    """CMA-ES sur un SOUS-ENSEMBLE d'indices, le reste fige.

    La perte reste evaluee sur le vecteur COMPLET - c'est la condition qui fait
    que les blocs optimisent tous le meme objectif.
    """
    if not idx:
        return v, perte(v), 0
    v = np.asarray(v, dtype=np.float64).copy()
    x0 = [float(v[k]) for k in idx]
    es = cma.CMAEvolutionStrategy(
        x0, sigma, {"bounds": [0, 1], "seed": int(seed) + 1,
                    "verbose": -9, "popsize": popsize})
    meilleur, p_min, n = v.copy(), perte(v), 0
    t0 = time.time()
    while time.time() - t0 < budget_s and not es.stop():
        pop = es.ask()
        pertes = []
        for c in pop:
            w = v.copy()
            w[idx] = np.clip(c, 0.0, 1.0)
            p = perte(w)
            pertes.append(p)
            n += 1
            if p < p_min:
                p_min, meilleur = p, w
        es.tell(pop, pertes)
    return meilleur, p_min, n


def cherche(perte, v0, budget_s, taille_bloc=8, n_cycles=3, part_conjointe=0.25,
            n_conjoint=40, seed=0, journal=None):
    """Recherche par blocs, feuille de route recalculee, passe conjointe finale.

    Retourne (vecteur, perte, courbe [(t, perte)]).
    """
    v = np.asarray(v0, dtype=np.float64).copy()
    t0 = time.time()
    courbe = [(0.0, perte(v))]
    n_eval = 0

    b_conjoint = budget_s * part_conjointe
    b_cycles = budget_s - b_conjoint

    for cyc in range(n_cycles):
        reste = b_cycles / n_cycles
        # 1. l'ordre est REMESURE a chaque cycle
        d0 = time.time()
        s = sensibilite(perte, v)
        cout_sens = time.time() - d0
        reste = max(reste - cout_sens, 1.0)
        bl = blocs(s, taille_bloc)
        if not bl:
            break
        # 2. le budget suit l'influence : un bloc qui pese plus recoit plus
        poids = np.array([max(float(np.sum(s[b])), 1e-12) for b in bl])
        poids = poids / poids.sum()
        for i, b in enumerate(bl):
            v, p, n = _cma_sur(perte, v, b, reste * poids[i],
                               seed=seed + 100 * cyc + i)
            n_eval += n
            courbe.append((time.time() - t0, p))
        if journal is not None:
            journal.append(("cycle %d" % (cyc + 1), len(bl), courbe[-1][1]))

    # 3. passe CONJOINTE sur les parametres les plus influents - c'est elle qui
    #    permet de sortir des cretes que la descente par blocs ne voit pas
    s = sensibilite(perte, v)
    idx = [int(k) for k in np.argsort(s)[::-1][:n_conjoint]]
    v, p, n = _cma_sur(perte, v, idx, b_conjoint, sigma=0.08,
                       popsize=12, seed=seed + 7777)
    n_eval += n
    courbe.append((time.time() - t0, p))
    if journal is not None:
        journal.append(("conjointe", len(idx), p))
    return v, p, courbe, n_eval
