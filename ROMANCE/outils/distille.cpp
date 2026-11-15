// =============================================================================
//  distille.cpp - Apprendre sur de VRAIS sons.
//
//  ⭐ POURQUOI. Le jeu synthetique donne des etiquettes exactes mais des sons
//  qui viennent tous de notre generateur. Un Oberheim, une guitare, une 808
//  tombent hors de cette distribution : le reseau excelle sur ce qu'il connait
//  et decroche sur le reste. C'est l'ecart de domaine, et c'est la cause la
//  plus probable du premier resultat NEGATIF (reseau 4,70 dB contre 4,08 pour
//  l'heuristique).
//
//  Ici on renverse le probleme : on lance une recherche LONGUE sur un vrai son,
//  et la recette qu'elle trouve devient l'etiquette. On distille donc la
//  recherche dans le reseau - il apprend a produire d'emblee ce que la
//  recherche mettrait des minutes a trouver.
//
//  /!\ ET ON N'APPRENDRA JAMAIS MIEUX QUE LA RECHERCHE. Le plafond de ce jeu,
//  c'est la qualite de la recherche elle-meme : le reseau imite un professeur
//  imparfait. Ca reste utile - un bon point de depart economise le budget - mais
//  ca ne fabrique pas de la qualite a partir de rien, et il ne faudra pas
//  s'etonner si la perte du reseau converge vers celle de la recherche.
//
//  /!\ LES ETIQUETTES SONT BRUITEES, contrairement au jeu synthetique. Deux
//  recherches sur le meme son peuvent finir a deux endroits differents : le
//  paysage a des minima locaux. On ecrit donc la perte atteinte a cote de
//  chaque exemple, pour pouvoir plus tard ecarter les mauvais professeurs.
//
//  ⭐ IL ACCUMULE. Si le fichier de sortie existe deja, les nouveaux exemples
//  s'AJOUTENT aux anciens, et `decalage` fait piocher ailleurs dans la liste.
//  On peut donc faire quatre passes de 50 sons et obtenir 50, 100, 150, 200
//  exemples - ce qui repond directement a « est-ce le NOMBRE qui manque ? »,
//  au lieu de comparer quatre tirages independants dont on ne saurait pas
//  demeler la taille du hasard.
//
//  Usage : distille.exe <liste.txt> <sortie.bin> <nSons> <budget> [threads] [decalage]
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Perte.h"
#include "Recherche.h"
#include "mesure.h"

using namespace romance;
using namespace outils;

struct Exemple
{
    std::vector<int8_t> entree;
    uint8_t cible[kOctetsProfil] = {};
    float fenetre = 1.5f;
    float perteAtteinte = 0.0f;
    bool ok = false;
};

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage : distille <liste.txt> <sortie.bin> <nSons> <budget> [threads]\n");
        return 1;
    }
    const std::string cheminListe = argv[1], sortie = argv[2];
    const int nSons  = std::atoi (argv[3]);
    const int budget = std::atoi (argv[4]);
    const int nThreads = (argc > 5) ? std::atoi (argv[5])
                                    : (int) std::max (1u, std::thread::hardware_concurrency());
    const int decalage = (argc > 6) ? std::atoi (argv[6]) : 0;

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

    // /!\ Echantillonnage REGULIER : les chemins sont tries par dossier, donc
    // prendre les N premiers ne donnerait qu'une seule banque.
    // /!\ Le pas est calcule sur un decoupage FIXE, pas sur nSons : sinon deux
    // passes de 50 tomberaient exactement sur les memes sons a un decalage pres,
    // et on croirait avoir 100 exemples en ayant deux fois les memes 50.
    const int pas = std::max (1, (int) fichiers.size() / 400);
    std::vector<std::string> choisis;
    for (size_t i = (size_t) (decalage % std::max (1, pas));
         i < fichiers.size() && (int) choisis.size() < nSons; i += (size_t) pas)
        choisis.push_back (fichiers[i]);

    std::printf ("\nROMANCE - distillation\n----------------------\n");
    std::printf ("  %d sons, %d evaluations chacun, %d threads\n",
                 (int) choisis.size(), budget, nThreads);

    // --- reprise : ce qui est deja dans le fichier reste
    std::vector<Exemple> anciens;
    if (FILE* f = std::fopen (sortie.c_str(), "rb"))
    {
        char sig[6]; uint32_t na = 0, te = 0, tc = 0;
        if (std::fread (sig, 1, 6, f) == 6 && std::memcmp (sig, "ROMDS", 6) == 0
            && std::fread (&na, 4, 1, f) == 1 && std::fread (&te, 4, 1, f) == 1
            && std::fread (&tc, 4, 1, f) == 1
            && (int) te == kTailleReduite && (int) tc == kOctetsProfil)
        {
            for (uint32_t k = 0; k < na; ++k)
            {
                Exemple e; e.entree.resize ((size_t) te);
                if (std::fread (e.entree.data(), 1, (size_t) te, f) != te) break;
                if (std::fread (&e.fenetre, 4, 1, f) != 1) break;
                if (std::fread (e.cible, 1, (size_t) tc, f) != tc) break;
                e.ok = true;
                anciens.push_back (e);
            }
        }
        std::fclose (f);
    }
    if (! anciens.empty())
        std::printf ("  %d exemples deja presents, on ajoute\n", (int) anciens.size());

    std::vector<Exemple> exemples (choisis.size());
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
            reg.evaluations = budget;
            // /!\ FENETRE PLAFONNEE A 2 s POUR LA DISTILLATION. Avec 4 s, une
            // evaluation coute 2,7 fois plus cher qu'a 1,5 s et 900 sons
            // demandaient trois heures. On prefere plus de professeurs
            // moyennement patients qu'une poignee de tres patients : le reseau
            // apprend d'abord la DIVERSITE des sons reels.
            reg.fenetreMax = 88200;
            // graine liee au son : deux passages donnent le meme resultat
            reg.graine = (unsigned int) (1000 + i * 37);

            RapportAnalyse rap;
            const auto st = resynthetise (s.x, s.fs, cfg, reg, rap,
                                          [] (const EtatRecherche&) { return true; });

            // l'entree est l'empreinte du VRAI son, sur la fenetre d'ajustement
            const int onset = trouveOnset (s.x);
            std::vector<double> cale (s.x.begin() + onset, s.x.end());
            const int nFen = std::max (reg.fenetreMin,
                                std::min (reg.fenetreMax, (int) cale.size()));
            if ((int) cale.size() > nFen) cale.resize ((size_t) nFen);
            if (cale.size() < 4096) { ++faits; continue; }

            std::vector<float> emp ((size_t) kTailleReduite);
            empreinteReduite (cale, s.fs, emp.data());

            Exemple& e = exemples[(size_t) i];
            e.entree.resize ((size_t) kTailleReduite);
            for (int k = 0; k < kTailleReduite; ++k)
            {
                const double v = std::max ((double) kPlancherPropre,
                                           std::min (0.0, (double) emp[(size_t) k]));
                const double q = (v - kPlancherPropre) / (-kPlancherPropre) * 255.0 - 128.0;
                e.entree[(size_t) k] = (int8_t) std::max (-128.0, std::min (127.0,
                                            std::round (q)));
            }
            // ⭐ LE PROFIL ENTIER, pas la recette : ce que la recherche a
            // trouve inclut son choix de moteur, et c'est precisement ce qu'on
            // veut distiller.
            std::memcpy (e.cible, st.profil, (size_t) kOctetsProfil);
            e.fenetre = (float) ((double) cale.size() / s.fs);
            e.perteAtteinte = (float) st.perteMeilleure;
            e.ok = true;

            const int n = ++faits;
            if ((n % 10) == 0)
            { std::printf ("\r  %d / %d sons...", n, (int) choisis.size());
              std::fflush (stdout); }
        }
    };

    std::vector<std::thread> fils;
    for (int t = 0; t < nThreads; ++t) fils.emplace_back (travail);
    for (auto& f : fils) f.join();

    int n = (int) anciens.size();
    double sommePerte = 0.0;
    int nNeufs = 0;
    for (const auto& e : exemples) if (e.ok) { ++n; ++nNeufs; sommePerte += e.perteAtteinte; }
    std::printf ("\r  %d exemples retenus, perte moyenne du professeur %.3f dB\n",
                 n, sommePerte / std::max (1, n));
    if (n == 0) return 1;

    FILE* f = std::fopen (sortie.c_str(), "wb");
    if (! f) { std::printf ("  !! ecriture impossible\n"); return 1; }
    const uint32_t nn = (uint32_t) n, te = (uint32_t) kTailleReduite,
                   tc = (uint32_t) kOctetsProfil;
    std::fwrite ("ROMDS\2", 1, 6, f);
    std::fwrite (&nn, 4, 1, f);
    std::fwrite (&te, 4, 1, f);
    std::fwrite (&tc, 4, 1, f);
    for (const auto& lot : { &anciens, &exemples })
        for (const auto& e : *lot)
        {
            if (! e.ok) continue;
            std::fwrite (e.entree.data(), 1, (size_t) te, f);
            std::fwrite (&e.fenetre, 4, 1, f);
            std::fwrite (e.cible, 1, (size_t) tc, f);
        }
    std::fclose (f);

    // /!\ Les pertes atteintes sont ecrites a part : elles disent quels
    // professeurs etaient bons, et permettront d'ecarter les mauvais.
    if (FILE* g = std::fopen ((sortie + ".pertes.csv").c_str(), "wb"))
    {
        std::fprintf (g, "chemin;perte;fenetre\n");
        for (size_t i = 0; i < exemples.size(); ++i)
            if (exemples[i].ok)
                std::fprintf (g, "%s;%.4f;%.3f\n", choisis[i].c_str(),
                              exemples[i].perteAtteinte, exemples[i].fenetre);
        std::fclose (g);
    }
    std::printf ("  ecrit : %s\n", sortie.c_str());
    return 0;
}
