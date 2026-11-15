// =============================================================================
//  dataset.cpp - Fabriquer le jeu d'entrainement.
//
//  ⭐ LE GENERATEUR EST LA VERITE : on tire une recette, on la rend, et on a une
//  paire (empreinte, 108 octets) dont l'etiquette est EXACTE. Pas d'annotation,
//  pas d'ambiguite, autant de donnees qu'on veut.
//
//  /!\ AVEC REJET. Un tirage dont le RENDU sort de la region du corpus est
//  jete. Le taux d'acceptation est imprime : c'est lui qui dit si le prior est
//  honnete. Un taux tres eleve voudrait dire que les bornes ne filtrent rien ;
//  un taux tres bas, qu'on jette l'essentiel du temps de calcul et que le prior
//  vise a cote.
//
//  Format de sortie (binaire, petit-boutiste) :
//    "ROMDS\1"  (6 o) | nbExemples (u32) | tailleEntree (u32) | tailleCible (u32)
//    puis, par exemple : tailleEntree x int8  puis  tailleCible x uint8
//
//  L'entree est quantifiee en int8 : les dB vont de kPlancherPropre a 0, donc
//  un octet donne un pas de 0,33 dB. Bien plus fin que ce que la perte
//  distingue, et quatre fois plus compact qu'un flottant.
//
//  Usage :  dataset.exe <sortie.bin> <nbExemples> [graine] [threads]
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Perte.h"
#include "Prior.h"
#include <cstring>
#include "Recette.h"
#include "mesure.h"

using namespace romance;
using namespace outils;

// --- La region du corpus, mesuree sur 1 665 sons (voir Prior.h).
//     Bornes larges : p2 / p98, pas p25 / p75. On veut ecarter l'absurde, pas
//     retrecir le monde a la mediane - un reseau qui n'a vu que des medianes
//     ne sait rien faire des extremes, et c'est justement la que l'utilisateur
//     depose ses sons les plus interessants.
struct Region
{
    double bruitMin = 0.0,   bruitMax = 0.98;
    double centMin  = 200.0, centMax  = 9000.0;
    double decMin   = 0.01,  decMax   = 8.0;
    double attMin   = 0.001, attMax   = 4.0;
};

static bool dansLaRegion (const Mesure& m, const Region& r)
{
    return m.ok
        && m.partBruit   >= r.bruitMin && m.partBruit   <= r.bruitMax
        && m.centroideHz >= r.centMin  && m.centroideHz <= r.centMax
        && m.decroissS   >= r.decMin   && m.decroissS   <= r.decMax
        && m.attaqueS    >= r.attMin   && m.attaqueS    <= r.attMax;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage : dataset <sortie.bin> <nbExemples> [graine] [threads]\n");
        return 1;
    }
    const std::string sortie = argv[1];
    const int nVoulu = std::atoi (argv[2]);
    const unsigned int graine0 = (argc > 3) ? (unsigned int) std::atoi (argv[3]) : 1u;
    const int nThreads = (argc > 4) ? std::atoi (argv[4])
                                    : (int) std::max (1u, std::thread::hardware_concurrency());

    const double fs = 44100.0;
    // ⭐ 1,5 s ET PAS 0,54. L'empreinte reduite a un nombre FIXE de tranches :
    // allonger la fenetre ne change pas la taille de l'entree du reseau, ca
    // change seulement ce que chaque tranche recouvre. Or a 0,54 s la mesure
    // de decroissance SATURAIT - elle ne pouvait pas depasser 0,52 s, ce qui
    // ecrasait toute la variance du jeu genere (corpus : 0,06 a 3,87 s).
    // On mesurait la fenetre, pas le son.
    // ⭐ FENETRE VARIABLE, comme a l'inference. Le corpus dure 3,4 s en
    // mediane ; on tire entre 0,75 et 4 s. /!\ Si le jeu etait genere a
    // fenetre fixe et l'inference faite a fenetre variable, le reseau verrait
    // a l'usage des entrees d'un genre qu'il n'a jamais vu - et echouerait
    // sans qu'aucun test ne le dise.
    const int nEchMax = 176400;           // 4 s
    const auto cfg = grainTransparent (fs);
    const Region region;

    std::printf ("\nROMANCE - jeu d'entrainement\n---------------------------\n");
    std::printf ("  %d exemples, %d threads, entree %d, cible %d\n",
                 nVoulu, nThreads, kTailleReduite, kOctetsProfil);

    std::vector<int8_t>  entrees ((size_t) nVoulu * kTailleReduite);
    std::vector<float>   fenetres ((size_t) nVoulu, 1.5f);
    std::vector<uint8_t> cibles  ((size_t) nVoulu * kOctetsProfil);

    std::vector<Mesure> mesures ((size_t) nVoulu);
    std::atomic<int> produits { 0 };
    std::atomic<long long> essais { 0 };

    auto travail = [&] (int idThread)
    {
        Des d (graine0 + 7919u * (unsigned int) idThread);
        std::vector<double> y;
        std::vector<float> emp ((size_t) kTailleReduite);

        for (;;)
        {
            if (produits.load (std::memory_order_relaxed) >= nVoulu) return;

            // ⭐ ON TIRE UN PROFIL, PAS UNE RECETTE. C'est ce qui fait que le
            // reseau apprend les quatre moteurs, les modulations et les
            // effets : la verite d'entrainement est desormais un patch entier.
            uint8_t prof[kOctetsProfil];
            tireProfil (d, prof);
            essais.fetch_add (1, std::memory_order_relaxed);

            // /!\ La duree de fenetre est une ENTREE du reseau : sa
            // distribution doit ressembler a celle des sons reels. Mesure :
            // 1,76 s de mediane contre 3,37 pour le corpus.
            const double fen = d.logEntre (1.2, 4.0);
            const int nEch = std::min (nEchMax, (int) std::round (fen * fs));
            // /!\ ET ON REND PAR LE MEME CHEMIN QUE LA RECHERCHE. Entrainer sur
            // `rendDegrade` pendant que la recherche evalue avec `rendProfil`,
            // ce serait apprendre a viser une cible que personne ne mesure.
            const double f0Tire = depuisOctets (prof).f0;
            rendProfil (prof, y, nEch, fs, f0Tire);

            // /!\ On juge le RENDU, pas la recette. Deux recettes voisines
            // peuvent sonner tres differemment : c'est le son qui doit tomber
            // dans la region du corpus.
            Son s; s.x = y; s.fs = fs; s.ok = true;
            const Mesure mm = mesure (s);
            if (! dansLaRegion (mm, region)) continue;

            const int i = produits.fetch_add (1, std::memory_order_relaxed);
            if (i >= nVoulu) return;
            mesures[(size_t) i] = mm;
            fenetres[(size_t) i] = (float) fen;

            empreinteReduite (y, fs, emp.data());
            int8_t* dst = entrees.data() + (size_t) i * kTailleReduite;
            for (int k = 0; k < kTailleReduite; ++k)
            {
                // dB dans [kPlancherPropre, 0] -> [-128, 127]
                const double v = std::max ((double) kPlancherPropre,
                                           std::min (0.0, (double) emp[(size_t) k]));
                const double q = (v - kPlancherPropre) / (-kPlancherPropre) * 255.0 - 128.0;
                dst[k] = (int8_t) std::max (-128.0, std::min (127.0, std::round (q)));
            }
            std::memcpy (cibles.data() + (size_t) i * kOctetsProfil, prof,
                         (size_t) kOctetsProfil);

            if ((i % 500) == 0)
            {
                std::printf ("\r  %d / %d   (acceptation %.0f %%)   ",
                             i, nVoulu,
                             100.0 * (double) i
                                 / (double) std::max (1LL, essais.load()));
                std::fflush (stdout);
            }
        }
    };

    std::vector<std::thread> fils;
    for (int t = 0; t < nThreads; ++t) fils.emplace_back (travail, t);
    for (auto& f : fils) f.join();

    const int n = std::min (nVoulu, produits.load());
    const double taux = 100.0 * (double) n / (double) std::max (1LL, essais.load());
    std::printf ("\r  %d exemples, acceptation %.1f %%                    \n", n, taux);

    FILE* f = std::fopen (sortie.c_str(), "wb");
    if (! f) { std::printf ("  !! ecriture impossible\n"); return 1; }
    const uint32_t nn = (uint32_t) n, te = (uint32_t) kTailleReduite,
                   tc = (uint32_t) kOctetsProfil;
    std::fwrite ("ROMDS\1", 1, 6, f);
    std::fwrite (&nn, 4, 1, f);
    std::fwrite (&te, 4, 1, f);
    std::fwrite (&tc, 4, 1, f);
    for (int i = 0; i < n; ++i)
    {
        std::fwrite (entrees.data() + (size_t) i * kTailleReduite, 1, (size_t) te, f);
        std::fwrite (&fenetres[(size_t) i], 4, 1, f);
        std::fwrite (cibles.data()  + (size_t) i * kOctetsProfil, 1, (size_t) tc, f);
    }
    std::fclose (f);

    const double mo = (double) n * (double) (te + tc) / 1048576.0;
    std::printf ("  ecrit : %s  (%.1f Mo)\n", sortie.c_str(), mo);

    // /!\ On ecrit AUSSI les mesures du jeu genere. Le taux d'acceptation ne
    // dit pas si le prior vise juste : il dit seulement qu'on ne tire pas
    // n'importe quoi (93 % au premier essai, donc il ne filtrait presque rien).
    // Ce qui compte est que la DISTRIBUTION du jeu ressemble a celle du corpus,
    // et ca ne se verifie qu'en mettant les deux cote a cote.
    if (FILE* g = std::fopen ((sortie + ".mesures.csv").c_str(), "wb"))
    {
        std::fprintf (g, "duree;f0;f0_suspecte;bruit;centroide;attaque;"
                         "decroissance;transitoire\n");
        for (int i = 0; i < n; ++i)
        {
            const auto& m = mesures[(size_t) i];
            std::fprintf (g, "%.3f;%.2f;%d;%.4f;%.1f;%.4f;%.4f;%.3f\n",
                          m.dureeS, m.f0, m.f0Suspecte ? 1 : 0, m.partBruit,
                          m.centroideHz, m.attaqueS, m.decroissS, m.transitoire);
        }
        std::fclose (g);
        std::printf ("  mesures du jeu : %s.mesures.csv\n", sortie.c_str());
    }
    return 0;
}
