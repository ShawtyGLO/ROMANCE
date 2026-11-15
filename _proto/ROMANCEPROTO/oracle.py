"""oracle.py - Le temoin qui manquait a l'etape 2.

La premiere execution de recherche.py plafonnait a une perte de ~4,5 et huit
fois plus d'evaluations n'y changeaient rien. Deux explications possibles, et
le test tel qu'il etait ecrit NE POUVAIT PAS les separer :

  (a) la RECHERCHE echoue - mauvais optimiseur, mauvaise perte, paysage rugueux
  (b) le MOTEUR ne peut pas representer la cible - le plancher est structurel

La cible par defaut a des partiels desaccordes et des battements, qu'un additif
harmonique pur ne peut pas produire. Elle est donc hors de portee PAR
CONSTRUCTION : (b) est probable, mais indemontrable en l'etat.

Ce script tranche : on synthetise une cible AVEC LE MOTEUR LUI-MEME, a partir
d'une recette connue. Elle est donc atteignable exactement, perte zero possible.

  - si la recherche la retrouve  -> la recherche marche, 4,5 est le plancher du MOTEUR
  - si elle ne la retrouve pas   -> c'est la RECHERCHE qu'il faut changer

C'est le seul resultat qui dit quoi corriger.
"""

import os
import time

import numpy as np

import grain
import moteur
import recherche
from recherche import Degradation, Perte, FS_ROM, DUREE, CFG

BUDGET_S = float(os.environ.get("BUDGET", 45.0))
N_CANDIDATS = int(os.environ.get("CANDIDATS", 4))


def main():
    rng = np.random.default_rng(2024)
    verite = rng.uniform(0.1, 0.9, moteur.N_PARAMS)
    f0 = 220.0

    degrade = Degradation(CFG)
    cible = degrade(moteur.synthetise(verite, f0, DUREE, FS_ROM))
    perte = Perte(cible)
    moteur.base_harmonique(f0, DUREE, FS_ROM)

    print("=== TEMOIN : cible produite PAR LE MOTEUR (donc atteignable) ===")
    print("moteur   : %d parametres" % moteur.N_PARAMS)
    print("budget   : %.0f s par candidat, %d candidats" % (BUDGET_S, N_CANDIDATS))
    print("verifie  : perte de la verite contre elle-meme = %.6f"
          % perte(cible))
    v_analyse = moteur.analyse(cible, f0, FS_ROM)
    p_analyse = perte(degrade(moteur.synthetise(v_analyse, f0, DUREE, FS_ROM)))
    print("depart par ANALYSE du spectre de la cible : perte %.4f\n" % p_analyse)

    journal = []
    res = []
    for i in range(N_CANDIDATS):
        # candidat 1 : le point de depart par ANALYSE, resserre.
        # candidats suivants : le meme point, de plus en plus perturbe.
        # C'est ce qui produit a la fois « le plus proche en premier » et la
        # diversite, sans avoir a l'imposer artificiellement.
        rng_i = np.random.default_rng(500 + 37 * i)
        x0 = v_analyse if i == 0 else np.clip(
            v_analyse + rng_i.normal(0.0, 0.10 * i, moteur.N_PARAMS), 0.0, 1.0)
        v, p, courbe = recherche.cherche_un(perte, f0, degrade,
                                            seed=500 + 37 * i,
                                            budget_s=BUDGET_S, journal=journal,
                                            x0=x0, sigma=0.12 + 0.06 * i)
        d = moteur.distance_recettes(v, verite)
        res.append((i, v, p, d, courbe))
        print("candidat %d : perte %.4f | ecart a la verite %5.1f dB | %d eval."
              % (i + 1, p, d, journal[-1][1]))
        print("             (%d redemarrages)" % journal[-1][3])

    print("\n--- Convergence ---")
    print("%8s" % "t (s)", end="")
    for i, _, _, _, _ in res:
        print("%12s" % ("cand. %d" % (i + 1)), end="")
    print()
    for tc in [j for j in (2, 5, 10, 20, 30, 45, 60, 90, 120, 180) if j <= BUDGET_S]:
        print("%8d" % tc, end="")
        for _, _, _, _, c in res:
            val = [p for (t, p) in c if t <= tc]
            print("%12s" % ("%.4f" % val[-1] if val else "-"), end="")
        print()

    rng2 = np.random.default_rng(0)
    hasard = [perte(degrade(moteur.synthetise(
                  rng2.uniform(0, 1, moteur.N_PARAMS), f0, DUREE, FS_ROM)))
              for _ in range(200)]
    meilleur = min(res, key=lambda r: r[2])

    print("\n--- Verdict ---")
    print("    perte atteignable (verite)      : 0.0000")
    print("    recette au hasard (mediane)     : %.4f" % float(np.median(hasard)))
    print("    meilleure au hasard sur 200     : %.4f" % float(np.min(hasard)))
    print("    depart par analyse              : %.4f" % p_analyse)
    print("    MEILLEUR CANDIDAT CHERCHE       : %.4f" % meilleur[2])
    print("    ecart de recette a la verite    : %.1f dB par harmonique"
          % meilleur[3])
    reduction = 1.0 - meilleur[2] / float(np.median(hasard))
    print("\n    Chemin parcouru vers la cible   : %.1f %%" % (100 * reduction))
    if meilleur[2] < 0.20 * float(np.median(hasard)):
        print("    -> La RECHERCHE fonctionne. Le plancher de recherche.py est")
        print("       celui du MOTEUR, pas de l'algorithme.")
    else:
        print("    -> La recherche NE retrouve PAS une cible pourtant atteignable.")
        print("       C'est l'ALGORITHME ou la PERTE qu'il faut changer, pas le moteur.")


if __name__ == "__main__":
    main()
