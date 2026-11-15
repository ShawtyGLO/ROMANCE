// =============================================================================
//  mes_objectif.cpp - EST-CE QUE NOTRE OBJECTIF MESURE CE QU'ON CROIT ?
//
//  Trois controles qui ne demandent aucune oreille. Ils ne disent pas si la
//  perte est BONNE ; ils disent si elle est COHERENTE - et une perte
//  incoherente est fausse a coup sur.
//
//    A. MONOTONIE. On part d'un profil, on le deplace de plus en plus loin.
//       La perte doit MONTER a chaque cran. Si un grand ecart score parfois
//       mieux qu'un petit, le paysage a des creux qui ne sont pas la cible.
//
//    B. ACCORD PROPRE / MACHINE. La recherche ajuste sur une cible PROPRE,
//       l'utilisateur ecoute A TRAVERS LA MACHINE. Si les deux mesures ne
//       classent pas les candidats dans le meme ordre, on optimise une chose
//       et on en entend une autre - ce qui expliquerait qu'un resultat plus
//       tardif paraisse plus loin.
//
//    C. IDENTITE. Un son contre lui-meme doit valoir zero.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Profil.h"
#include "Prior.h"
#include "Perte.h"

using namespace romance;

namespace
{
    const double kFs = 22050.0;
    const int    kN  = 22050;

    /** Rend un profil, puis son empreinte - propre ou passee dans la machine. */
    Empreinte emp (const uint8_t* o, bool machine)
    {
        std::vector<double> y;
        rendProfil (o, y, kN, kFs, 220.0);
        if (machine)
        {
            // la degradation d'epoque : quantification 8 bits + bande limitee
            for (auto& v : y) v = quantifie (v, Quant::Lin8);
        }
        return empreinte (y, kFs, machine ? kPlancherDegrade : kPlancherPropre);
    }

    /** Corrélation de rang de Spearman entre deux series. */
    double spearman (std::vector<double> a, std::vector<double> b)
    {
        const int n = (int) a.size();
        auto rangs = [n] (const std::vector<double>& v)
        {
            std::vector<int> idx ((size_t) n);
            for (int i = 0; i < n; ++i) idx[(size_t) i] = i;
            std::sort (idx.begin(), idx.end(),
                       [&v] (int x, int y) { return v[(size_t) x] < v[(size_t) y]; });
            std::vector<double> r ((size_t) n);
            for (int k = 0; k < n; ++k) r[(size_t) idx[(size_t) k]] = k;
            return r;
        };
        const auto ra = rangs (a), rb = rangs (b);
        double s = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double d = ra[(size_t) i] - rb[(size_t) i];
            s += d * d;
        }
        return 1.0 - 6.0 * s / ((double) n * ((double) n * n - 1.0));
    }
}

int main()
{
    std::printf ("\nEST-CE QUE L'OBJECTIF MESURE CE QU'ON CROIT ?\n");
    std::printf ("=============================================\n");

    // ------------------------------------------------------------ C. identite
    {
        Des de (7u);
        uint8_t o[kOctetsProfil]; tireProfil (de, o);
        const auto a = emp (o, false), b = emp (o, false);
        std::printf ("\nC. IDENTITE\n");
        std::printf ("   un profil contre lui-meme : %.6f dB\n", perte (a, b));
    }

    // ----------------------------------------------------------- A. monotonie
    //
    // On part d'un profil, on mute k octets, k croissant. La perte doit monter.
    {
        std::printf ("\nA. MONOTONIE - la perte monte-t-elle avec l'ecart ?\n");
        const int crans[] = { 1, 2, 4, 8, 16, 32, 64 };
        const int nCrans = 7;
        double somme[7] = { 0.0 };
        int inversions = 0, paires = 0;
        const int nTirages = 20;

        for (int t = 0; t < nTirages; ++t)
        {
            Des de ((unsigned int) (t * 6673 + 5));
            uint8_t base[kOctetsProfil]; tireProfil (de, base);
            const auto eb = emp (base, false);
            const TypeMoteur m = coucheDepuisProfil (base).moteur;

            double p[7];
            // /!\ LES ECARTS SONT EMBOITES, PAS INDEPENDANTS. Ma premiere
            // version tirait une mutation NEUVE a chaque cran : un deplacement
            // de 4 octets dans une direction anodine pouvait alors etre plus
            // proche qu'un deplacement de 2 dans une direction nuisible, et
            // l'inversion comptee n'aurait rien prouve. En emboitant, le cran
            // suivant contient toujours le precedent : la distance AUGMENTE
            // vraiment, donc une perte qui baisse est une vraie anomalie.
            uint8_t v[kOctetsProfil]; std::memcpy (v, base, sizeof (base));
            for (int c = 0; c < nCrans; ++c)
            {
                // /!\ On ne mute QUE les octets que la recherche peut bouger :
                // deplacer un octet inerte ne change rien et fabriquerait une
                // fausse non-monotonie.
                const int aPoser = crans[c] - (c > 0 ? crans[c - 1] : 0);
                int poses = 0, essais = 0;
                while (poses < aPoser && essais < 4000)
                {
                    ++essais;
                    const int i = (int) de.entre (0.0, (double) kOctetsProfil - 0.01);
                    if (! octetProfilMutable (i, m)) continue;
                    v[i] = (uint8_t) de.entre (0.0, 255.9);
                    ++poses;
                }
                p[c] = perte (eb, emp (v, false));
                somme[c] += p[c];
            }
            for (int c = 1; c < nCrans; ++c)
            { ++paires; if (p[c] < p[c - 1]) ++inversions; }
        }

        for (int c = 0; c < nCrans; ++c)
            std::printf ("   %2d octets deplaces -> %6.3f dB\n",
                         crans[c], somme[c] / nTirages);
        std::printf ("   INVERSIONS : %d sur %d paires consecutives (%.0f %%)\n",
                     inversions, paires, 100.0 * inversions / paires);
    }

    // ------------------------------------------- B. accord propre / machine
    {
        std::printf ("\nB. ACCORD PROPRE / MACHINE - classe-t-on pareil ?\n");
        const int nCand = 40;
        Des de (1234u);
        uint8_t cible[kOctetsProfil]; tireProfil (de, cible);
        const auto ecP = emp (cible, false), ecM = emp (cible, true);

        std::vector<double> pP, pM;
        for (int i = 0; i < nCand; ++i)
        {
            uint8_t v[kOctetsProfil]; tireProfil (de, v);
            pP.push_back (perte (ecP, emp (v, false)));
            pM.push_back (perte (ecM, emp (v, true)));
        }
        const double rho = spearman (pP, pM);
        std::printf ("   %d candidats, correlation de rang = %+.3f\n", nCand, rho);
        std::printf ("   (1,00 = meme classement ; 0 = aucun rapport)\n");

        // combien des dix MEILLEURS au propre sont dans les dix meilleurs machine ?
        auto dixMeilleurs = [] (const std::vector<double>& v)
        {
            std::vector<int> idx ((size_t) v.size());
            for (size_t i = 0; i < v.size(); ++i) idx[i] = (int) i;
            std::sort (idx.begin(), idx.end(),
                       [&v] (int a, int b) { return v[(size_t) a] < v[(size_t) b]; });
            idx.resize (10);
            return idx;
        };
        const auto dP = dixMeilleurs (pP);
        auto dM = dixMeilleurs (pM);
        int communs = 0;
        for (int i : dP)
            if (std::find (dM.begin(), dM.end(), i) != dM.end()) ++communs;
        std::printf ("   %d des 10 meilleurs au propre sont dans les 10 meilleurs machine\n",
                     communs);
    }

    std::printf ("\n");
    return 0;
}
