// =============================================================================
//  oracle.cpp - Ou est le PLAFOND ?
//
//  ⭐ LA QUESTION QU'ON N'AVAIT PAS POSEE. On passe du temps a ameliorer le
//  POINT DE DEPART de la recherche sans savoir ou est l'ARRIVEE. Or les deux
//  reponses possibles menent a des chantiers opposes :
//
//    · si 20 000 evaluations descendent bien plus bas que 400, c'est la
//      RECHERCHE qui limite - alors un bon amorcage vaut de l'or et le reseau
//      a un vrai role ;
//    · si 20 000 s'arretent au meme endroit que 400, ce n'est pas la recherche
//      qui limite mais la CAPACITE DU FORMAT a representer ces sons. Et la, ni
//      plus de donnees, ni plus d'epoques, ni un meilleur reseau ne changeront
//      rien : il faut elargir ce que la recette sait decrire.
//
//  C'est la meme discipline que sur le reste du projet, montee d'un cran : un
//  test qui ne peut pas echouer ne prouve rien, et optimiser un trajet dont on
//  ignore la destination n'en est pas un.
//
//  /!\ UNE SEULE RECHERCHE PAR SON, avec des RELEVES en cours de route. Lancer
//  quatre recherches a quatre budgets couterait quatre fois plus cher ET
//  melangerait deux effets (le budget, et le hasard du tirage). Ici la courbe
//  vient d'une seule trajectoire : ce qu'on lit est bien l'effet du budget.
//
//  Usage : oracle.exe <liste.txt> <nSons> <budgetMax> <sortie.csv> [threads]
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Perte.h"
#include "Recherche.h"
#include "mesure.h"

using namespace romance;
using namespace outils;

// les paliers ou l'on releve la perte
static const int kPaliers[] = { 400, 1000, 2500, 6000, 12000, 20000 };
static const int kNbPaliers = 6;

struct Trace
{
    std::string nom;
    double perte[kNbPaliers] = {};
    double amorce = 0.0;
    double plancher = 0.0;
    bool ok = false;
};

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage : oracle <liste.txt> <nSons> <budgetMax> <sortie.csv> [threads]\n");
        return 1;
    }
    const std::string cheminListe = argv[1], cheminCsv = argv[4];
    const int nSons = std::atoi (argv[2]);
    const int budgetMax = std::atoi (argv[3]);
    const int nThreads = (argc > 5) ? std::atoi (argv[5])
                                    : (int) std::max (1u, std::thread::hardware_concurrency());

    std::vector<std::string> fichiers;
    if (FILE* f = std::fopen (cheminListe.c_str(), "rb"))
    {
        char ligne[4096];
        while (std::fgets (ligne, sizeof (ligne), f))
        {
            std::string p (ligne);
            while (! p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
            if (! p.empty()) fichiers.push_back (p);
        }
        std::fclose (f);
    }
    if (fichiers.empty()) { std::printf ("liste vide\n"); return 1; }

    const int pas = std::max (1, (int) fichiers.size() / std::max (1, nSons));
    std::vector<std::string> choisis;
    for (size_t i = 0; i < fichiers.size() && (int) choisis.size() < nSons; i += (size_t) pas)
        choisis.push_back (fichiers[i]);

    std::printf ("\nROMANCE - oracle : ou est le plafond ?\n");
    std::printf ("-------------------------------------\n");
    std::printf ("  %d sons, jusqu'a %d evaluations, %d threads\n",
                 (int) choisis.size(), budgetMax, nThreads);
    std::printf ("  (une seule recherche par son, relevee en cours de route)\n\n");

    std::vector<Trace> traces (choisis.size());
    std::atomic<int> suivant { 0 }, faits { 0 };

    auto travail = [&] ()
    {
        for (;;)
        {
            const int i = suivant.fetch_add (1);
            if (i >= (int) choisis.size()) return;

            const Son s = litWav (choisis[(size_t) i].c_str());
            if (! s.ok || s.x.size() < 8192) { ++faits; continue; }

            const auto cfg = grainTransparent (s.fs);
            ReglagesRecherche reg;
            reg.evaluations = budgetMax;
            reg.fenetreMax = 88200;          // 2 s, comme la distillation
            reg.graine = (unsigned int) (2000 + i * 61);

            Trace& t = traces[(size_t) i];
            const auto pos = choisis[(size_t) i].find_last_of ("\\/");
            t.nom = (pos == std::string::npos) ? choisis[(size_t) i]
                                               : choisis[(size_t) i].substr (pos + 1);

            int palier = 0;
            RapportAnalyse rap;
            const auto st = resynthetise (s.x, s.fs, cfg, reg, rap,
                [&] (const EtatRecherche& e)
                {
                    while (palier < kNbPaliers && e.evaluationsFaites >= kPaliers[palier])
                        t.perte[palier++] = e.perteMeilleure;
                    return true;
                });
            // les paliers non atteints prennent la valeur finale
            while (palier < kNbPaliers) t.perte[palier++] = st.perteMeilleure;
            t.amorce = st.perteDepart;

            std::vector<double> c (s.x.begin() + trouveOnset (s.x), s.x.end());
            if ((int) c.size() > reg.fenetreMax) c.resize ((size_t) reg.fenetreMax);
            t.plancher = partAuPlancher (empreinte (c, s.fs));
            t.ok = true;

            const int n = ++faits;
            std::printf ("  %d / %d termine\n", n, (int) choisis.size());
            std::fflush (stdout);
        }
    };

    std::vector<std::thread> fils;
    for (int t = 0; t < nThreads; ++t) fils.emplace_back (travail);
    for (auto& f : fils) f.join();

    // --- rapport
    double moy[kNbPaliers] = {};
    double moyAmorce = 0.0;
    int n = 0;
    for (const auto& t : traces)
        if (t.ok)
        {
            ++n; moyAmorce += t.amorce;
            for (int k = 0; k < kNbPaliers; ++k) moy[k] += t.perte[k];
        }
    if (n == 0) { std::printf ("aucun son exploitable\n"); return 1; }

    std::printf ("\n  %d sons\n\n", n);
    std::printf ("  budget      perte moyenne    gain depuis 400 eval.\n");
    std::printf ("  amorce    %9.3f dB\n", moyAmorce / n);
    for (int k = 0; k < kNbPaliers; ++k)
        std::printf ("  %6d    %9.3f dB       %+7.3f dB\n",
                     kPaliers[k], moy[k] / n, (moy[k] - moy[0]) / n);

    const double gain = (moy[0] - moy[kNbPaliers - 1]) / n;
    std::printf ("\n  ⭐ VERDICT : de 400 a %d evaluations, on gagne %.3f dB.\n",
                 kPaliers[kNbPaliers - 1], gain);
    if (gain < 0.35)
        std::printf ("     C'est PEU. Ce n'est donc pas la recherche qui limite,\n"
                     "     c'est ce que le format sait representer.\n");
    else if (gain < 1.0)
        std::printf ("     C'est MODESTE. La recherche a encore un peu de marge,\n"
                     "     mais le format n'est probablement pas loin de sa limite.\n");
    else
        std::printf ("     C'est BEAUCOUP. La recherche est le goulot : un bon\n"
                     "     amorcage vaut donc vraiment quelque chose.\n");

    if (FILE* g = std::fopen (cheminCsv.c_str(), "wb"))
    {
        std::fprintf (g, "son;amorce");
        for (int k = 0; k < kNbPaliers; ++k) std::fprintf (g, ";e%d", kPaliers[k]);
        std::fprintf (g, ";plancher\n");
        for (const auto& t : traces)
        {
            if (! t.ok) continue;
            std::fprintf (g, "%s;%.4f", t.nom.c_str(), t.amorce);
            for (int k = 0; k < kNbPaliers; ++k) std::fprintf (g, ";%.4f", t.perte[k]);
            std::fprintf (g, ";%.3f\n", t.plancher);
        }
        std::fclose (g);
        std::printf ("\n  detail : %s\n", cheminCsv.c_str());
    }
    return 0;
}
