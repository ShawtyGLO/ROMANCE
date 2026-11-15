// =============================================================================
//  corpus.cpp - Mesurer la banque, pour que le PRIOR vienne d'elle.
//
//  ⭐ POURQUOI CET OUTIL EXISTE. Le reseau sera entraine sur des recettes
//  tirees au hasard. Si on les tire uniformement dans les 108 octets, on
//  l'entraine sur des sons que PERSONNE NE FAIT - des empilements de partiels
//  sans queue ni tete. Il faut donc d'abord savoir a quoi ressemblent les sons
//  qu'on veut vraiment resynthetiser, et tirer dans cette region-la.
//
//  /!\ ON MESURE AVEC NOTRE PROPRE ANALYSE, pas avec un outil tiers. Ce qui
//  compte n'est pas la verite absolue sur ces sons, c'est ce que NOTRE chaine
//  en percoit - puisque c'est elle qui devra les reproduire.
//
//  Usage :  corpus.exe <liste.txt> <sortie.csv>
//           (liste.txt : un chemin de .wav par ligne)
// =============================================================================
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "mesure.h"

using namespace romance;
using namespace outils;

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage : corpus <liste.txt> <sortie.csv>\n");
        return 1;
    }
    FILE* liste = std::fopen (argv[1], "rb");
    if (! liste) { std::printf ("liste introuvable\n"); return 1; }
    FILE* csv = std::fopen (argv[2], "wb");
    if (! csv) { std::fclose (liste); std::printf ("sortie impossible\n"); return 1; }

    std::fprintf (csv, "chemin;duree;f0;f0_suspecte;bruit;centroide;attaque;"
                       "decroissance;transitoire\n");

    char ligne[4096];
    int n = 0, ok = 0, rejet = 0;
    while (std::fgets (ligne, sizeof (ligne), liste))
    {
        std::string p (ligne);
        while (! p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
        if (p.empty()) continue;
        ++n;

        const Son s = litWav (p.c_str());
        const Mesure m = mesure (s);
        if (! m.ok) { ++rejet; continue; }
        ++ok;

        std::fprintf (csv, "%s;%.3f;%.2f;%d;%.4f;%.1f;%.4f;%.4f;%.3f\n",
                      p.c_str(), m.dureeS, m.f0, m.f0Suspecte ? 1 : 0,
                      m.partBruit, m.centroideHz, m.attaqueS, m.decroissS,
                      m.transitoire);

        if ((n % 200) == 0)
        { std::printf ("\r  %d fichiers...", n); std::fflush (stdout); }
    }
    std::fclose (liste);
    std::fclose (csv);
    std::printf ("\r  %d fichiers lus, %d mesures, %d rejets\n", n, ok, rejet);
    return 0;
}
