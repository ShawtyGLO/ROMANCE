// =============================================================================
//  mes_trace.cpp - L'OPTIMISEUR EST-IL MAUVAIS, OU L'OBJECTIF EST-IL FAUX ?
//
//  ⭐⭐ LA QUESTION QUE TOUT LE RESTE ATTEND. L'oracle a montre que la recherche
//  plafonne loin de zero alors que la verite lui est montrable. Deux
//  explications s'excluent, et elles menent a des chantiers OPPOSES :
//
//    · l'OPTIMISEUR descend mal        -> changer d'algorithme
//    · l'OBJECTIF descend bien, mais VERS LE MAUVAIS ENDROIT
//                                      -> refaire la PERTE, changer d'algorithme
//                                         ne servirait a rien
//
//  Se tromper de moitie coute des semaines. Ce banc tranche en une soiree.
//
//  ---------------------------------------------------------------------------
//  DEUX MESURES, ET LA SECONDE EST UN TEMOIN
//
//  1. LE TRACE. On fabrique la cible depuis un profil CONNU, on cherche, et a
//     chaque amelioration on note DEUX choses : la perte, et la distance aux
//     VRAIS octets. Si la perte descend pendant que la distance monte, la
//     recherche s'eloigne de la reponse en se felicitant - l'objectif est mal
//     aligne. Si les deux descendent ensemble mais s'arretent tot, c'est
//     l'optimiseur.
//
//  2. LA MONOTONIE LOCALE (le temoin). On part de la VERITE et on la degrade
//     par paliers croissants. La perte DOIT monter avec la degradation. Si
//     elle ne le fait pas, l'objectif est faux jusque dans le voisinage
//     immediat de la reponse - et aucun optimiseur au monde n'y peut rien.
//
//     /!\ SANS CE TEMOIN, LE TRACE NE SE LIT PAS. Une correlation faible peut
//     venir d'un paysage accidente (optimiseur) ou d'une perte qui ne mesure
//     pas la ressemblance (objectif). Seule la monotonie locale les separe :
//     elle ne fait intervenir AUCUNE recherche.
//
//  ---------------------------------------------------------------------------
//  /!\ `ecartOctets` EST UNE MESURE FAIBLE, et c'est ecrit ici pour qu'on ne la
//  surinterprete pas : deux profils tres differents peuvent sonner pareil si
//  les octets qui different sont inertes dans ce patch-la. On la borne donc aux
//  octets MUTABLES du bon moteur, et on ne conclut jamais sur elle seule -
//  c'est la CONJONCTION avec la perte qui informe.
//
//  Usage : mes_trace.exe [nSons] [budget] [graine]
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Perte.h"
#include "Profil.h"
#include "Prior.h"
#include "Recherche.h"

using namespace romance;

namespace
{
    const double kFs = 22050.0;
    const int    kN  = 22050;
    const double kF0 = 220.0;

    Empreinte emp (const uint8_t* o)
    {
        std::vector<double> y;
        rendProfil (o, y, kN, kFs, kF0);
        return empreinte (y, kFs, kPlancherPropre);
    }

    double ecartOctets (const uint8_t* a, const uint8_t* b, TypeMoteur m)
    {
        double s = 0.0; int n = 0;
        for (int i = 0; i < kOctetsProfil; ++i)
            if (octetProfilMutable (i, m))
            { s += std::abs ((double) a[i] - (double) b[i]); ++n; }
        return (n > 0) ? s / (double) n : 0.0;
    }

    /** Correlation de Pearson. */
    double correlation (const std::vector<double>& x, const std::vector<double>& y)
    {
        const int n = (int) std::min (x.size(), y.size());
        if (n < 3) return 0.0;
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < n; ++i) { mx += x[(size_t) i]; my += y[(size_t) i]; }
        mx /= n; my /= n;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = x[(size_t) i] - mx, b = y[(size_t) i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return (sxx > 1e-12 && syy > 1e-12) ? sxy / std::sqrt (sxx * syy) : 0.0;
    }
}

int main (int argc, char** argv)
{
    const int nSons  = (argc > 1) ? std::atoi (argv[1]) : 8;
    const int budget = (argc > 2) ? std::atoi (argv[2]) : 3000;
    const unsigned int graine0 = (argc > 3) ? (unsigned int) std::atoi (argv[3]) : 1u;

    std::printf ("\nOPTIMISEUR OU OBJECTIF ?\n");
    std::printf ("========================\n");
    std::printf ("  %d cibles connues, %d evaluations\n\n", nSons, budget);

    // =========================================================================
    //  TEMOIN D'ABORD : la perte monte-t-elle quand on s'eloigne de la verite ?
    //
    //  /!\ AVANT LE RESULTAT, TOUJOURS (invariant 10). Si ce temoin echoue,
    //  rien de ce qui suit ne se lit.
    // =========================================================================
    std::printf ("  --- TEMOIN : monotonie locale autour de la verite ---\n");
    std::printf ("  degradation par octet  ->  perte moyenne\n");

    static const int kPaliers[] = { 0, 5, 10, 20, 40, 80 };
    const int nPaliers = (int) (sizeof (kPaliers) / sizeof (kPaliers[0]));
    std::vector<double> moyPalier ((size_t) nPaliers, 0.0);

    for (int t = 0; t < nSons; ++t)
    {
        Des de (graine0 + (unsigned int) t * 15485863u);
        uint8_t vrai[kOctetsProfil];
        tireProfil (de, vrai);
        const TypeMoteur moteur = coucheDepuisProfil (vrai).moteur;
        const auto cible = emp (vrai);

        for (int p = 0; p < nPaliers; ++p)
        {
            uint8_t o[kOctetsProfil];
            for (int i = 0; i < kOctetsProfil; ++i) o[i] = vrai[i];
            if (kPaliers[p] > 0)
                for (int i = 0; i < kOctetsProfil; ++i)
                    if (octetProfilMutable (i, moteur))
                        o[i] = (uint8_t) std::max (0.0, std::min (255.0,
                                   (double) vrai[i] + de.entre (-(double) kPaliers[p],
                                                                 (double) kPaliers[p])));
            const double pp = perte (cible, emp (o));
            moyPalier[(size_t) p] += std::isfinite (pp) ? pp : 0.0;
        }
    }
    bool monotone = true;
    for (int p = 0; p < nPaliers; ++p)
    {
        moyPalier[(size_t) p] /= (double) nSons;
        std::printf ("    +/- %-3d               %8.3f dB\n", kPaliers[p], moyPalier[(size_t) p]);
        if (p > 0 && moyPalier[(size_t) p] < moyPalier[(size_t) (p - 1)]) monotone = false;
    }
    std::printf ("  -> %s\n\n", monotone
        ? "MONOTONE : localement, la perte SAIT ou est la reponse."
        : "PAS MONOTONE : la perte ne sait pas ou est la reponse, meme de pres.");

    // =========================================================================
    //  LE TRACE : perte et distance a la verite, le long de la descente.
    // =========================================================================
    std::printf ("  --- TRACE : la descente s'approche-t-elle de la reponse ? ---\n");

    std::vector<double> correlations;
    int pasTotal = 0, pasContraires = 0;
    double distDepart = 0.0, distFin = 0.0, perteFin = 0.0;

    for (int t = 0; t < nSons; ++t)
    {
        Des de (graine0 + (unsigned int) t * 15485863u);
        uint8_t vrai[kOctetsProfil];
        tireProfil (de, vrai);
        const TypeMoteur moteur = coucheDepuisProfil (vrai).moteur;
        const auto cible = emp (vrai);

        uint8_t depart[kOctetsProfil];
        profilNeutre (depart);
        depart[kOffMoteur] = vrai[kOffMoteur];   // on n'evalue pas le vote

        ReglagesRecherche reg;
        reg.evaluations = budget;
        reg.graine = 7919u + (unsigned int) t;
        reg.fenetreVariable = false;
        reg.nEchComparaison = kN;
        reg.collecteLignee = false;

        std::vector<double> pertes, distances;
        double dernierePerte = 1e30;
        auto rappel = [&] (const EtatRecherche& st)
        {
            // /!\ ON N'ENREGISTRE QUE LES AMELIORATIONS REELLES. Le rappel est
            // appele deux fois par generation ; noter chaque appel gonflerait
            // l'echantillon de doublons et ferait mentir la correlation.
            if (st.perteMeilleure < dernierePerte - 1e-9)
            {
                dernierePerte = st.perteMeilleure;
                pertes.push_back (st.perteMeilleure);
                distances.push_back (ecartOctets (vrai, st.profil, moteur));
            }
            return true;
        };
        const auto st = cherche (cible, kFs, kF0, depart, reg, rappel);

        if (pertes.size() >= 3)
        {
            correlations.push_back (correlation (pertes, distances));
            for (size_t i = 1; i < pertes.size(); ++i)
            {
                ++pasTotal;
                // un pas CONTRAIRE : la perte descend, la distance monte
                if (distances[i] > distances[i - 1]) ++pasContraires;
            }
            distDepart += distances.front();
            distFin    += distances.back();
            perteFin   += st.perteMeilleure;
        }
        std::printf ("\r    %d / %d cibles...", t + 1, nSons);
        std::fflush (stdout);
    }
    std::printf ("\r                          \r");

    const int n = (int) correlations.size();
    if (n == 0) { std::printf ("  !! aucune trajectoire exploitable\n"); return 1; }
    double mc = 0.0; for (double c : correlations) mc += c; mc /= n;

    std::printf ("  correlation perte <-> distance a la verite : %+.3f\n", mc);
    std::printf ("    (+1 = la perte descend QUAND on s'approche ; 0 = elle n'en sait rien)\n");
    std::printf ("  pas ou la perte DESCEND et la distance MONTE : %d / %d  (%.0f %%)\n",
                 pasContraires, pasTotal, 100.0 * pasContraires / std::max (1, pasTotal));
    std::printf ("  distance moyenne par octet : depart %.1f -> arrivee %.1f\n",
                 distDepart / n, distFin / n);
    std::printf ("  perte finale moyenne : %.3f dB   (la verite vaut 0)\n\n", perteFin / n);

    // =========================================================================
    //  LE VERDICT, ECRIT PAR LE BANC ET PAS PAR CELUI QUI LE LIT.
    // =========================================================================
    std::printf ("  --- VERDICT ---\n");
    if (! monotone)
        std::printf ("  L'OBJECTIF. La perte n'est pas monotone AUTOUR DE LA REPONSE :\n"
                     "  aucun optimiseur ne peut trouver ce que la perte ne recompense pas.\n"
                     "  Chantier : refaire la PERTE.\n");
    else if (mc < 0.3 || pasContraires > pasTotal / 3)
        std::printf ("  L'OBJECTIF, mais globalement. La perte sait ou est la reponse DE PRES\n"
                     "  et l'ignore de loin : la descente s'eloigne souvent tout en gagnant.\n"
                     "  Changer d'optimiseur descendra mieux vers le MAUVAIS endroit.\n"
                     "  Chantier : la perte, ou un objectif intermediaire mieux conditionne.\n");
    else if (distFin / n < distDepart / n * 0.5)
        std::printf ("  L'OPTIMISEUR. La descente s'approche vraiment de la reponse mais\n"
                     "  s'arrete tot : le paysage est bon, l'algorithme est faible.\n"
                     "  Chantier : changer de famille d'optimiseur.\n");
    else
        std::printf ("  MIXTE : la perte guide, mais faiblement, et la descente n'arrive pas.\n"
                     "  A creuser avant d'engager quoi que ce soit.\n");
    std::printf ("\n");
    return 0;
}
