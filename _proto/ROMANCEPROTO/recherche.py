"""recherche.py - ETAPE 2 : la question qui decide du projet.

  « Une recherche de parametres converge-t-elle vers quelque chose de
    musicalement reconnaissable en un temps acceptable ? »

Si oui, le projet existe. Si non, ce n'est pas rattrapable par plus
d'ingenierie - et il vaut mieux le savoir maintenant.

Principe, repris de Genopatch (Synplant 2) : la recherche converge vers les
PARAMETRES DU MOTEUR, pas vers un fichier audio. Notre sauce tient en un point :

  ** l'erreur est mesuree APRES la degradation d'epoque **

La recherche cherche donc la ROM qui, une fois massacree par le companding et le
filtre de reconstruction, ressemble a la cible. Le grain n'est pas un filtre
pose a la fin, c'est une contrainte que l'algorithme habite.

Pas de reseau de neurones : CMA-ES sur une perte spectrale multi-echelle.
Rien a embarquer, rien a entrainer, aucune question de provenance de donnees.
"""

import os
import sys
import time

import numpy as np
import soundfile as sf
from scipy import signal

import cma

import grain
import moteur
import sources
from grain import Config

FS_ROM = grain.FS_ROM_EII
DUREE = 1.2        # une nappe est quasi stationnaire : 1,2 s suffit
                   # a la comparaison, et chaque seconde coute des evaluations
N_CANDIDATS = 4
BUDGET_S = float(os.environ.get('BUDGET', 40.0))            # budget mur par candidat (ils tourneraient en
                           # parallele dans le plugin -> 40 s au total)
DOSSIER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rendus")

# La machine visee pendant la recherche. C'est un CHOIX de l'utilisateur, pas
# une variable de la recherche (categories discretes = mal gerees par
# l'optimisation, et « fais-moi la version Emulator II de ce son » est une
# bien meilleure promesse que « l'IA a choisi pour toi »).
CFG = Config("variable", "asservi", "mulaw", ordre=4)


# --------------------------------------------------- la couche d'epoque
# /!\ Elle vit desormais dans grain.py, et NULLE PART AILLEURS.
# Il en existait deux implementations - la complete dans grain.joue() et une
# copie rapide ici. Meme schema que la divergence process / process_joint de
# XPERT comp : deux chemins qui derivent en silence, et un rendu qui peut
# ignorer un parametre sans que rien ne le signale.
from grain import Degradation, _fir_zoh          # noqa: F401  (re-export)


# ------------------------------------------------------ perte spectrale multi-echelle

def _spectro(x, n):
    saut = n // 2
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    nb = 1 + (len(x) - n) // saut
    idx = np.arange(n)[None, :] + saut * np.arange(nb)[:, None]
    trames = x[idx] * np.hanning(n)[None, :]
    return np.abs(np.fft.rfft(trames, axis=1))


TAILLES = (1024, 512, 256)
EPS = 1e-7
PLANCHER_DB = -80.0     # tout ce qui est sous ce niveau est traite comme vide


def _rms_norm(x):
    r = float(np.sqrt(np.mean(x * x)))
    return x / r if r > 0 else x


def _log_spec(s):
    """Log d'un spectrogramme, NORMALISE et PLANCHONNE.

    /!\\ C'est le correctif du defaut le plus grave rencontre sur ce projet.
    Sans plancher, le terme logarithmique compare les bins VIDES - entre les
    harmoniques et au-dessus du filtre de reconstruction. Le moteur y met des
    zeros exacts, la cible y a un plancher de bruit de companding : l'ecart de
    log y est enorme, constant, et ne diminue que si les parametres sont
    EXACTEMENT justes. Mesure : le terme log valait 0,65 contre 0,0004 pour le
    lineaire, et la perte restait plate sur 90 % du chemin vers la solution
    avant de tomber d'un coup. Une aiguille - introuvable par construction.

    Normalisation par le maximum GLOBAL du spectrogramme (pas par trame) :
    normaliser trame par trame effacerait l'enveloppe temporelle.
    """
    ref = float(np.max(s))
    if ref <= 0:
        ref = 1.0
    return np.log10(np.maximum(s / ref, 10.0 ** (PLANCHER_DB / 20.0)))


def banc_bandes(n_fft, fs, n_bandes=48, f_min=50.0, f_max_ratio=0.45):
    """Banc de filtres triangulaires a espacement logarithmique.

    /!\\ CORRECTIF DECISIF. La perte compare des signaux QUANTIFIES : le bruit
    de companding est deterministe, et deux jeux de parametres meme tres proches
    produisent des REALISATIONS de bruit totalement decorrelees. Comparer les
    bins bruts revient donc a exiger que la recherche retrouve le bruit
    echantillon par echantillon - c'est impossible autrement qu'en tombant
    exactement sur la solution.

    Mesure du defaut : une perturbation de 0,001 sur les parametres faisait
    passer la perte de 0,000 a 0,077 sur une echelle de 0,24. Une aiguille.

    Le regroupement en bandes moyenne la realisation du bruit tout en gardant
    son ENVELOPPE spectrale - qui, elle, est bien une propriete du son. C'est
    la consequence directe de notre propre choix de mesurer l'erreur APRES la
    degradation : la perte doit etre aveugle au grain de bruit, pas au grain.
    """
    freqs = np.fft.rfftfreq(n_fft, 1.0 / fs)
    f_max = f_max_ratio * fs
    bords = np.geomspace(f_min, f_max, n_bandes + 2)
    banc = np.zeros((n_bandes, len(freqs)))
    for i in range(n_bandes):
        g, c, d = bords[i], bords[i + 1], bords[i + 2]
        m = (freqs >= g) & (freqs <= c)
        banc[i, m] = (freqs[m] - g) / max(c - g, 1e-9)
        m = (freqs > c) & (freqs <= d)
        banc[i, m] = (d - freqs[m]) / max(d - c, 1e-9)
        s = banc[i].sum()
        if s > 0:
            banc[i] /= s
    return banc


class Perte:
    """Perte spectrale multi-echelle contre une cible FIXE.

    La cible ne change jamais pendant une recherche : ses spectrogrammes et
    leurs logarithmes sont donc calcules UNE fois. C'etait 40 % du cout d'une
    evaluation, paye des dizaines de milliers de fois pour rien.

    Les deux signaux sont normalises en RMS avant comparaison : sans ca,
    l'optimiseur depense des dimensions a regler un gain global, qui n'est pas
    ce qu'on lui demande de trouver.
    """

    def __init__(self, cible, fs=FS_ROM):
        self.n = len(cible)
        self.fs = fs
        c = _rms_norm(np.asarray(cible, dtype=np.float64))
        self.banc = {t: banc_bandes(t, fs) for t in TAILLES}
        self.sc = {}
        for t in TAILLES:
            b = self._bandes(c, t)
            self.sc[t] = (b / max(float(np.max(b)), 1e-30), _log_spec(b))

    def _bandes(self, x, t):
        return _spectro(x, t) @ self.banc[t].T

    def __call__(self, y):
        y = _rms_norm(np.asarray(y, dtype=np.float64)[:self.n])
        total = 0.0
        for t in TAILLES:
            sc, lc = self.sc[t]
            by = self._bandes(y, t)
            m = min(sc.shape[0], by.shape[0])
            by_n = by / max(float(np.max(by)), 1e-30)
            # les deux termes sont du meme ordre de grandeur : le lineaire suit
            # les fortes composantes, le log le detail faible.
            total += float(np.mean(np.abs(sc[:m] - by_n[:m])))
            total += 0.5 * float(np.mean(np.abs(lc[:m] - _log_spec(by)[:m])))
        return total / len(TAILLES)


def perte_mss(x, y):
    """Version sans cache - pour les reperes ponctuels uniquement."""
    return Perte(x)(y)


# ------------------------------------------------------------------ recherche

def cherche_un(perte, f0, degrade, seed, budget_s, journal, x0=None, sigma=0.25):
    """Un candidat. Retourne (vecteur, perte, courbe [(t, perte)]).

    Deux mecanismes, tous deux ajoutes apres le diagnostic du temoin
    (oracle.py) qui a montre une CONVERGENCE PREMATUREE dans des minima locaux :

    1. `x0` - point de depart. Fourni par moteur.analyse(), il vaut deja mieux
       que le meilleur de 200 tirages au hasard.
    2. REDEMARRAGES a population croissante (IPOP). Quand CMA-ES declare l'arret
       - c'est-a-dire quand il a converge - on le relance plus large depuis le
       meilleur point connu, au lieu de laisser le budget se perdre. Sans ca,
       11 000 evaluations donnaient exactement le meme resultat que 5 700.
    """
    rng = np.random.default_rng(seed)
    if x0 is None:
        x0 = rng.uniform(0.15, 0.85, moteur.N_PARAMS)
    x0 = np.clip(np.asarray(x0, dtype=np.float64), 0.0, 1.0)

    t0 = time.time()
    courbe = []
    meilleur, perte_min = np.array(x0), float("inf")
    n_eval = 0
    popsize = 12
    n_redem = 0

    while time.time() - t0 < budget_s:
        es = cma.CMAEvolutionStrategy(
            list(meilleur if n_redem else x0),
            sigma if n_redem == 0 else min(0.5, sigma * (1.6 ** n_redem)),
            {"bounds": [0, 1], "seed": int(seed) + 1 + 97 * n_redem,
             "verbose": -9, "popsize": popsize})
        while time.time() - t0 < budget_s and not es.stop():
            pop = es.ask()
            pertes = []
            for v in pop:
                p = perte(degrade(moteur.synthetise(v, f0, DUREE, FS_ROM)))
                pertes.append(p)
                n_eval += 1
                if p < perte_min:
                    perte_min, meilleur = p, np.array(v)
            es.tell(pop, pertes)
            courbe.append((time.time() - t0, perte_min))
        n_redem += 1
        popsize = min(popsize * 2, 256)

    journal.append((seed, n_eval, time.time() - t0, n_redem))
    return meilleur, perte_min, courbe


def main():
    os.makedirs(DOSSIER, exist_ok=True)

    if len(sys.argv) > 1:
        cible_brute = sources.charge_wav(sys.argv[1], FS_ROM)[:int(DUREE * FS_ROM)]
        etiquette = os.path.splitext(os.path.basename(sys.argv[1]))[0]
        print("CIBLE = " + sys.argv[1])
    else:
        # Cible volontairement HORS de portee du moteur : partiels desaccordes
        # et battements, que l'additif harmonique pur ne peut pas reproduire.
        # Une cible atteignable exactement ne prouverait rien.
        cible_brute = sources.nappe(f0=220.0, duree=DUREE, fs=FS_ROM, seed=11)
        etiquette = "nappe_cible"
        print("CIBLE = nappe de synthese desaccordee (hors de portee exacte du moteur)")

    f0 = moteur.estime_f0(cible_brute, FS_ROM)
    print("f0 estimee          : %.1f Hz  (mesuree UNE fois, hors recherche)" % f0)
    print("moteur              : %d parametres" % moteur.N_PARAMS)
    print("machine visee       : %s" % CFG.nom)
    print("budget              : %.0f s par candidat, %d candidats\n"
          % (BUDGET_S, N_CANDIDATS))

    degrade = Degradation(CFG)
    cible = cible_brute[:int(DUREE * FS_ROM)]
    perte = Perte(cible)
    moteur.base_harmonique(f0, DUREE, FS_ROM)      # prechauffe le cache

    # cout d'une evaluation
    v = np.full(moteur.N_PARAMS, 0.5)
    t0 = time.time()
    for _ in range(50):
        perte(degrade(moteur.synthetise(v, f0, DUREE, FS_ROM)))
    cout = (time.time() - t0) / 50
    print("cout d'une evaluation : %.1f ms  -> ~%.0f evaluations en %.0f s\n"
          % (cout * 1000, BUDGET_S / cout, BUDGET_S))

    journal = []
    resultats = []
    for i in range(N_CANDIDATS):
        vect, p_fin, courbe = cherche_un(perte, f0, degrade,
                                         seed=100 + 37 * i,
                                         budget_s=BUDGET_S, journal=journal)
        resultats.append((i, vect, p_fin, courbe))
        print("candidat %d : perte finale %.4f  (%d evaluations)"
              % (i + 1, p_fin, journal[-1][1]))

    # --------- « le plus proche en premier » : convergence dans le temps
    print("\n--- Convergence : perte du meilleur candidat au fil du temps ---")
    print("%8s" % "t (s)", end="")
    for i, _, _, _ in resultats:
        print("%12s" % ("cand. %d" % (i + 1)), end="")
    print()
    jalons = [j for j in (2, 5, 10, 20, 30, 40, 60, 90, 120, 180, 240)
              if j <= BUDGET_S]
    for t_cible in jalons:
        print("%8d" % t_cible, end="")
        for _, _, _, courbe in resultats:
            val = [p for (t, p) in courbe if t <= t_cible]
            print("%12s" % ("%.4f" % val[-1] if val else "-"), end="")
        print()

    ordre = sorted(resultats, key=lambda r: r[2])
    print("\nOrdre d'arrivee (le plus proche en premier) : "
          + " > ".join("cand. %d" % (r[0] + 1) for r in ordre))

    # --------- diversite : 4 reponses differentes, ou 4 nuances d'une seule ?
    print("\n--- Diversite des 4 candidats (ecart moyen en dB par harmonique) ---")
    n = len(resultats)
    for i in range(n):
        for j in range(i + 1, n):
            d = moteur.distance_recettes(resultats[i][1], resultats[j][1])
            print("    cand. %d <-> cand. %d : %6.1f dB" % (i + 1, j + 1, d))

    # --------- reference : ou en est-on par rapport a une recette au hasard ?
    rng = np.random.default_rng(0)
    pertes_hasard = [perte(degrade(moteur.synthetise(
                         rng.uniform(0, 1, moteur.N_PARAMS), f0, DUREE, FS_ROM)))
                     for _ in range(200)]
    print("\n--- Reperes ---")
    print("    recette au hasard (200 tirages) : %.4f  (mediane)"
          % float(np.median(pertes_hasard)))
    print("    meilleure au hasard             : %.4f" % float(np.min(pertes_hasard)))
    print("    meilleur candidat cherche       : %.4f" % ordre[0][2])
    print("    cible contre elle-meme          : %.4f" % perte(cible))
    print("    cible contre silence            : %.4f"
          % perte(np.zeros_like(cible)))
    print("    (echelle : 0 = identique, %.2f = rien du tout)"
          % perte(np.zeros_like(cible)))

    # --------- rendus, par la chaine COMPLETE cette fois
    print("\n--- Rendus ---")
    sf.write(os.path.join(DOSSIER, "10_cible_%s.wav" % etiquette),
             (0.9 * cible / max(np.max(np.abs(cible)), 1e-9)
              ).astype(np.float32), int(FS_ROM), subtype="FLOAT")
    print("    10_cible_%s.wav" % etiquette)

    for rang, (i, vect, perte, _) in enumerate(ordre, start=1):
        rom = moteur.synthetise(vect, f0, DUREE, FS_ROM)
        y, fs = grain.joue(rom, CFG, demi_tons=0.0, duree_s=DUREE,
                           fs_sortie=44100.0, sur_ech=8)
        y = grain.normalise_k(y, fs, cible=-20.0)
        nom = "1%d_candidat%d_perte%.3f_%s.wav" % (rang, i + 1, perte, etiquette)
        sf.write(os.path.join(DOSSIER, nom), y.astype(np.float32), int(fs),
                 subtype="FLOAT")
        print("    " + nom)

    # taille de l'etat
    print("\n--- Taille de l'etat ---")
    print("    une recette = %d flottants = %d octets"
          % (moteur.N_PARAMS, moteur.N_PARAMS * 4))
    print("    la ROM correspondante        = %d octets (jamais stockee)"
          % (len(rom) * 4))


if __name__ == "__main__":
    main()
