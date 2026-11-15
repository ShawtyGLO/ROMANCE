// =============================================================================
//  mes_oracle.cpp - L'OBJECTIF EST-IL FAUX, OU SEULEMENT MAL ALIGNE ?
//
//  ⭐⭐ LE TEST DE L'ORACLE. On fabrique la cible NOUS-MEMES, depuis un profil
//  connu. La verite existe donc, et elle vaut exactement zero de perte. Trois
//  reponses possibles, et elles menent a trois chantiers opposes :
//
//    1. la recherche descend PRES DE ZERO -> l'objectif est atteignable et la
//       recherche fonctionne. Le probleme est ailleurs.
//    2. elle descend pres de zero MAIS s'eloigne quand on ecoute A TRAVERS LA
//       MACHINE -> l'objectif est MAL ALIGNE.
//    3. elle PLAFONNE loin de zero alors que la verite lui est montrable ->
//       c'est le PAYSAGE qui bloque.
//
//  /!\ AUCUNE OREILLE N'EST REQUISE : la verite est connue par construction.
//
//  ⭐⭐ ET IL REND UN ECART-TYPE, PAS SEULEMENT UNE MOYENNE.
//
//  /!\ SANS CA, IL FABRIQUE DES NOMBRES QUI RESSEMBLENT A DES RESULTATS. J'ai
//  compare trois variantes de recherche sur des ecarts de 0,03 a 0,19 dB sans
//  jamais mesurer ce que donne le MEME code relance avec d'autres graines. La
//  reponse est 0,26 dB - plus que tous les ecarts sur lesquels j'avais conclu.
//  Un banc qui ne dit pas son bruit ne permet de conclure sur rien.
//
//  Il compare donc DEUX VARIANTES SUR LES MEMES CIBLES, en apparie les
//  resultats son par son, et rend un test de Student sur les differences. Deux
//  moyennes cote a cote ne suffisent pas : la variance entre SONS est bien plus
//  grande que l'effet cherche, et elle se simplifie dans un appariement.
//
//  Usage : mes_oracle.exe [nSons] [budget] [nGraines] [chaud 0|1]
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

    Empreinte emp (const uint8_t* o, bool machine)
    {
        std::vector<double> y;
        rendProfil (o, y, kN, kFs, kF0);
        if (machine) for (auto& v : y) v = quantifie (v, Quant::Lin8);
        return empreinte (y, kFs, machine ? kPlancherDegrade : kPlancherPropre);
    }

    const char* nomVariante (int v)
    {
        switch (v)
        {
            case 1:  return "relances x4";
            case 2:  return "pas adaptatif";
            case 3:  return "relances + pas";
            case 4:  return "choix machine";
            case 5:  return "mutation ponderee";
            case 6:  return "balayage 1/4";
            case 7:  return "ponderee+balayage";
            default: return "temoin";
        }
    }

    /** Combien d'octets MUTABLES separent deux profils, et de combien.

        /!\ MESURE FAIBLE, ET C'EST ECRIT ICI POUR QU'ON NE LA SURINTERPRETE
        PAS : deux vecteurs tres differents peuvent sonner pareil si les octets
        qui different sont inertes dans ce patch-la. Elle sert d'indice, jamais
        de preuve. */
    double ecartOctets (const uint8_t* a, const uint8_t* b, TypeMoteur m)
    {
        double s = 0.0; int n = 0;
        for (int i = 0; i < kOctetsProfil; ++i)
            if (octetProfilMutable (i, m))
            { s += std::abs ((double) a[i] - (double) b[i]); ++n; }
        return (n > 0) ? s / (double) n : 0.0;
    }

    struct Stat { double moyenne, ecartType; };

    Stat stat (const std::vector<double>& v)
    {
        if (v.empty()) return { 0.0, 0.0 };
        double m = 0.0; for (double x : v) m += x; m /= (double) v.size();
        if (v.size() < 2) return { m, 0.0 };
        double s = 0.0; for (double x : v) s += (x - m) * (x - m);
        return { m, std::sqrt (s / (double) (v.size() - 1)) };
    }

    /** Un essai : une cible connue, une recherche, ce qu'on en tire. */
    struct Essai { double propre, machine, ecart; };

    Essai unEssai (unsigned int graineCible, unsigned int graineRech,
                   int budget, bool chaud, int variante)
    {
        Des de (graineCible);
        uint8_t vrai[kOctetsProfil];
        tireProfil (de, vrai);
        const TypeMoteur moteur = coucheDepuisProfil (vrai).moteur;

        const auto cibleP = emp (vrai, false);
        const auto cibleM = emp (vrai, true);

        ReglagesRecherche reg;
        reg.evaluations = budget;
        reg.graine = graineRech;
        reg.fenetreVariable = false;
        reg.nEchComparaison = kN;
        reg.collecteLignee = false;
        // ⭐ `variante` choisit CE QU'ON COMPARE. Le banc n'a jamais servi
        // qu'a ca : deux formes de recherche, memes cibles, appariees.
        //   0 = code d'origine        1 = relances multiples
        //   2 = pas adaptatif         3 = les deux
        reg.relances     = (variante == 1 || variante == 3) ? 4 : 1;
        reg.pasAdaptatif = (variante == 2 || variante == 3);
        // ⭐ Variante 4 : on AJUSTE au propre et on CHOISIT a travers la
        // machine. Le banc est le seul endroit ou l'on dispose des deux
        // empreintes de la meme cible, donc le seul ou l'on peut le mesurer.
        if (variante == 4) reg.cibleMachine = &cibleM;
        // ⭐ Les deux pistes qui s'attaquent a la MUTATION elle-meme : 10 octets
        // tires uniformement parmi ~150, avec un seul sigma global.
        reg.mutationPonderee = (variante == 5 || variante == 7);
        reg.periodeCoord     = (variante == 6 || variante == 7) ? 4 : 0;

        uint8_t depart[kOctetsProfil];
        profilNeutre (depart);
        if (chaud)
        {
            // ⭐ AMORCE CHAUDE : la verite DEGRADEE de +/- 40 par octet mutable.
            //
            // /!\ C'EST UN PLAFOND, PAS UNE PREVISION. Cette amorce est bien
            // meilleure que ce qu'une vraie heuristique produit - elle DERIVE de
            // la reponse. Elle mesure donc ce qu'une amorce PARFAITE
            // rapporterait, pas ce qu'on obtiendra.
            for (int i = 0; i < kOctetsProfil; ++i)
                depart[i] = octetProfilMutable (i, moteur)
                    ? (uint8_t) std::max (0.0, std::min (255.0,
                          (double) vrai[i] + de.entre (-40.0, 40.0)))
                    : vrai[i];
        }
        // /!\ ON IMPOSE LE BON MOTEUR : sinon on mesurerait aussi le vote.
        depart[kOffMoteur] = vrai[kOffMoteur];

        const auto st = cherche (cibleP, kFs, kF0, depart, reg,
                                 [] (const EtatRecherche&) { return true; });

        return { perte (cibleP, emp (st.profil, false)),
                 perte (cibleM, emp (st.profil, true)),
                 ecartOctets (vrai, st.profil, moteur) };
    }
}

int main (int argc, char** argv)
{
    const int nSons    = (argc > 1) ? std::atoi (argv[1]) : 12;
    const int budget   = (argc > 2) ? std::atoi (argv[2]) : 3000;
    const int nGraines = (argc > 3) ? std::atoi (argv[3]) : 3;
    const int chaud    = (argc > 4) ? std::atoi (argv[4]) : 0;
    const int variante = (argc > 5) ? std::atoi (argv[5]) : 1;

    std::printf ("\nTEST DE L'ORACLE - cible fabriquee depuis un profil CONNU\n");
    std::printf ("=========================================================\n");
    std::printf ("  %d sons x %d graines, %d evaluations, amorce %s\n",
                 nSons, nGraines, budget, chaud ? "CHAUDE" : "froide");
    std::printf ("  essai : %s\n\n", nomVariante (variante));

    // ⭐ Les differences APPARIEES : meme cible, meme graine de recherche,
    // seule la forme de mutation change.
    std::vector<double> diff, aTout, bTout, moyParGraine;
    std::vector<double> diffM, aM, bM;

    for (int g = 0; g < nGraines; ++g)
    {
        std::vector<double> ag, bg;
        for (int t = 0; t < nSons; ++t)
        {
            const unsigned int gc = (unsigned int) ((t + g * 1000) * 15485863u + 17u);
            const unsigned int gr = (unsigned int) ((t + g * 1000) * 7919u + 3u);
            const auto a = unEssai (gc, gr, budget, chaud != 0, 0);        // temoin
            const auto b = unEssai (gc, gr, budget, chaud != 0, variante); // essai
            ag.push_back (a.propre); bg.push_back (b.propre);
            aTout.push_back (a.propre); bTout.push_back (b.propre);
            diff.push_back (a.propre - b.propre);
            aM.push_back (a.machine); bM.push_back (b.machine);
            diffM.push_back (a.machine - b.machine);
        }
        const auto sa = stat (ag);
        std::printf ("  graine %d : temoin %6.3f dB   %-14s %6.3f dB\n",
                     g, sa.moyenne, nomVariante (variante), stat (bg).moyenne);
        moyParGraine.push_back (sa.moyenne);
    }

    const auto sA = stat (aTout), sB = stat (bTout), sD = stat (diff);
    const auto sG = stat (moyParGraine);
    const int n = (int) diff.size();
    const double t = (sD.ecartType > 1e-12)
                        ? sD.moyenne / (sD.ecartType / std::sqrt ((double) n)) : 0.0;

    std::printf ("\n  ------------------------------------------------------------\n");
    std::printf ("  TEMOIN (code actuel) %6.3f dB  (ecart-type entre sons %.3f)\n",
                 sA.moyenne, sA.ecartType);
    std::printf ("  ESSAI %-14s %6.3f dB  (ecart-type entre sons %.3f)\n",
                 nomVariante (variante), sB.moyenne, sB.ecartType);
    std::printf ("\n  BRUIT DU BANC : ecart-type des moyennes entre graines %.3f dB\n",
                 sG.ecartType);
    std::printf ("  -> tout ecart plus petit que ca ne veut RIEN dire sans appariement.\n");
    std::printf ("\n  DIFFERENCE APPARIEE  %+.3f dB   (t = %+.2f sur %d paires)\n",
                 sD.moyenne, t, n);
    std::printf ("  |t| > 2 : effet reel.  |t| < 2 : rien de demontre.\n");

    // ⭐⭐ ET LE MEME CALCUL A TRAVERS LA MACHINE.
    //
    // /!\ SANS CETTE COLONNE, LE BANC NE PEUT PAS JUGER LA VARIANTE 4. Elle
    // choisit son gagnant sur l'empreinte DEGRADEE : mesuree au propre, elle
    // est perdante PAR CONSTRUCTION, et le banc « demontrerait » qu'elle est
    // mauvaise. Un banc qui ne mesure pas dans l'unite ou l'on DECIDE ne
    // mesure rien - c'est le piege 43 sous une quatrieme forme.
    const auto sAM = stat (aM), sBM = stat (bM), sDM = stat (diffM);
    const double tM = (sDM.ecartType > 1e-12)
                        ? sDM.moyenne / (sDM.ecartType / std::sqrt ((double) n)) : 0.0;
    std::printf ("\n  ---- A TRAVERS LA MACHINE (le juge qui compte a l'oreille) ----\n");
    std::printf ("  TEMOIN               %6.3f dB  (ecart-type entre sons %.3f)\n",
                 sAM.moyenne, sAM.ecartType);
    std::printf ("  ESSAI %-14s %6.3f dB  (ecart-type entre sons %.3f)\n",
                 nomVariante (variante), sBM.moyenne, sBM.ecartType);
    std::printf ("  DIFFERENCE APPARIEE  %+.3f dB   (t = %+.2f)\n", sDM.moyenne, tM);
    std::printf ("  -> positif = l'essai GAGNE a l'ecoute.\n\n");
    return 0;
}
