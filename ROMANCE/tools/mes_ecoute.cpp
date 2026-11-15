// =============================================================================
//  mes_ecoute.cpp - LA PERTE EST-ELLE D'ACCORD AVEC L'OREILLE ?
//
//  ⭐⭐ LA SEULE QUESTION QU'AUCUN BANC NE PEUT TRANCHER SEUL. Le piege 60 a
//  montre que la recherche descend BIEN mais AILLEURS : elle finit a 2,8 dB en
//  etant a 74 unites de la verite, alors que la verite degradee de 5 unites
//  coute 3,9 dB. Deux lectures restent, et elles menent a des chantiers
//  opposes :
//
//    (a) LA PERTE EST FAUSSE - elle ne mesure pas ce qu'on entend.
//        -> refaire la perte.
//    (b) LE PROBLEME EST NON IDENTIFIABLE - beaucoup de jeux de parametres
//        sonnent reellement pareil, donc la distance en parametres n'a aucune
//        importance et la perte a raison.
//        -> ne rien refaire, et expliquer les 2,8 dB residuels autrement.
//
//  Aucune machine ne les separe. Il faut une oreille.
//
//  ---------------------------------------------------------------------------
//  CE QUE CE BANC FABRIQUE
//
//  Des essais en AVEUGLE. Pour chaque essai : la cible, et deux candidats que
//  LA PERTE classe - l'un est meilleur, l'autre moins bon, d'un ecart connu.
//  L'auditeur dit lequel ressemble le plus. Si la perte a raison, il choisit le
//  candidat que la perte prefere.
//
//  /!\ TROIS PRECAUTIONS, ET CHACUNE ANNULERAIT LE TEST SI ON L'OUBLIAIT :
//
//   1. L'ORDRE EST TIRE AU SORT PAR ESSAI. Sans ca, « A est toujours le
//      meilleur » se devine en trois essais et le reste ne mesure plus rien.
//
//   2. TOUT EST MIS AU MEME NIVEAU PERCU (RMS) - la cible comme les deux
//      candidats. Une difference de niveau s'entend AVANT une difference de
//      timbre. Et normaliser la CRETE ne suffit pas : deux sons de meme crete
//      peuvent avoir des RMS dans un rapport de trois. Surtout, la perte est
//      DELIBEREMENT AVEUGLE au niveau (l'empreinte normalise chaque trame),
//      donc laisser le niveau peser reviendrait a poser une autre question que
//      celle qu'on croit.
//
//   3. IL Y A DES ESSAIS D'ANCRAGE, ou l'ecart de perte est ENORME (> 4 dB).
//      Ils ne servent pas a repondre a la question, ils servent a valider le
//      TEST : si l'auditeur les rate, c'est le protocole qui est casse - fichiers
//      mal ranges, ecoute distraite, monitoring inadapte - et les essais fins
//      ne veulent rien dire. C'est l'invariant 10 applique a une oreille.
//
//  ⭐ LES DEUX CANDIDATS VIENNENT DE LA MEME LIGNEE. Ce ne sont pas deux
//  patchs quelconques : c'est la recherche a deux moments d'elle-meme. La
//  question posee est donc exactement celle qui compte pour le projet - quand
//  la perte descend de 1 dB, est-ce que ca s'entend comme un progres ?
//
//  Usage : mes_ecoute.exe <liste.txt> <dossier_sortie> [nEssais] [budget]
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Perte.h"
#include "Profil.h"
#include "Recherche.h"
#include "mesure.h"

using namespace romance;
using namespace outils;

namespace
{
    const int    kNRendu = 88200;      // 2 s a 44,1 kHz
    const double kEcartAncre = 4.0;    // dB : doit s'entendre sans hesiter
    const double kEcartTest  = 1.0;    // dB : l'ecart dont on veut savoir s'il s'entend

    /** ⭐⭐ MEME NIVEAU PERCU - EN RMS, PAS EN CRETE.
    
        /!\ MA PREMIERE VERSION NORMALISAIT LA CRETE, et c'etait une faute qui
        aurait fausse la conclusion. Deux sons de meme crete peuvent avoir des
        RMS dans un rapport de trois (mesure : cible 0,166, candidat A 0,045,
        candidat B 0,075 sur le meme essai) - donc l'un s'entend nettement plus
        fort que l'autre.
    
        Et le niveau n'est PAS neutre ici : la perte est DELIBEREMENT AVEUGLE au
        niveau, puisque l'empreinte normalise la crete de chaque trame - c'est
        pourquoi la destination `niveau` mesure 0,000 dB (piege 35). Laisser le
        niveau influencer l'oreille reviendrait donc a comparer sur une dimension
        que la perte ignore, et a conclure qu'elle est en desaccord avec
        l'oreille alors qu'on lui aurait pose une autre question.
    
        Le gain commun est le plus fort qui n'ecrete AUCUN des trois signaux. */
    void memeNiveau (std::vector<double>& cible, std::vector<double>& a,
                     std::vector<double>& b)
    {
        auto rms = [] (const std::vector<double>& x)
        {
            if (x.empty()) return 0.0;
            double s = 0.0; for (double v : x) s += v * v;
            return std::sqrt (s / (double) x.size());
        };
        auto crete = [] (const std::vector<double>& x)
        {
            double c = 1e-9; for (double v : x) c = std::max (c, std::abs (v));
            return c;
        };
        std::vector<double>* t[3] = { &cible, &a, &b };
        double vise = 1e30;
        for (int k = 0; k < 3; ++k)
        {
            const double r = rms (*t[k]);
            if (r < 1e-9) return;                       // signal vide : on ne touche a rien
            vise = std::min (vise, 0.89 * r / crete (*t[k]));
        }
        for (int k = 0; k < 3; ++k)
        {
            const double g = vise / std::max (1e-9, rms (*t[k]));
            for (double& v : *t[k]) v *= g;
        }
    }

    struct Candidat { double perte; uint8_t profil[kOctetsProfil]; };
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage : mes_ecoute <liste.txt> <dossier> [nEssais] [budget]\n");
        return 1;
    }
    const std::string cheminListe = argv[1];
    std::string dossier = argv[2];
    if (! dossier.empty() && dossier.back() != '\\' && dossier.back() != '/')
        dossier += '/';
    const int nEssais = (argc > 3) ? std::atoi (argv[3]) : 20;
    const int budget  = (argc > 4) ? std::atoi (argv[4]) : 2000;

    std::printf ("\nBANC D'ECOUTE - la perte est-elle d'accord avec l'oreille ?\n");
    std::printf ("===========================================================\n");
    std::printf ("  %d essais, %d evaluations par son\n\n", nEssais, budget);

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
    if (fichiers.empty()) { std::printf ("  !! liste vide\n"); return 1; }

    const int pas = std::max (1, (int) fichiers.size() / (nEssais * 2));

    FILE* cle = std::fopen ((dossier + "cle.csv").c_str(), "wb");
    if (! cle) { std::printf ("  !! ecriture impossible dans %s\n", dossier.c_str()); return 1; }
    std::fprintf (cle, "essai;type;ecart_dB;meilleur_selon_la_perte;perte_A;perte_B;source\n");

    FILE* feuille = std::fopen ((dossier + "REPONSES.txt").c_str(), "wb");
    std::fprintf (feuille,
        "BANC D'ECOUTE ROMANCE\n"
        "=====================\n\n"
        "Pour chaque essai : ecouter cible_NN.wav, puis NN_A.wav et NN_B.wav.\n"
        "Question : LEQUEL DES DEUX RESSEMBLE LE PLUS A LA CIBLE ?\n\n"
        "Ecrire A ou B apres le numero. Si vraiment aucune difference, ecrire =.\n"
        "NE PAS OUVRIR cle.csv AVANT D'AVOIR FINI - le test n'a de valeur qu'en aveugle.\n\n");

    unsigned int graine = 20260905u;
    auto uni = [&graine] () { graine = graine * 1664525u + 1013904223u;
                              return (double) (graine >> 8) / 16777216.0; };

    int n = 0, ancres = 0;
    for (size_t i = 0; i < fichiers.size() && n < nEssais; i += (size_t) pas)
    {
        const Son s = litWav (fichiers[i].c_str());
        if (! s.ok || s.x.size() < 16384) continue;

        // /!\ UN ESSAI SUR QUATRE EST UN ANCRAGE. Trop peu et on ne saurait pas
        // si le protocole tient ; trop et on gaspille l'oreille sur des
        // evidences.
        const bool ancre = (n % 4 == 3);
        const double ecartVoulu = ancre ? kEcartAncre : kEcartTest;

        const auto cfg = grainTransparent (s.fs);
        ReglagesRecherche reg;
        reg.evaluations = budget;
        reg.graine = 4242u + (unsigned int) n;
        reg.collecteLignee = true;     // c'est elle qui fournit les deux candidats
        reg.amorceDe = ReglagesRecherche::Amorce::Heuristique;

        RapportAnalyse rap;
        const auto st = resynthetise (s.x, s.fs, cfg, reg, rap,
                                      [] (const EtatRecherche&) { return true; }, nullptr);

        // ---- tous les noeuds de la lignee, tries par perte
        std::vector<Candidat> cands;
        for (const auto& g : st.lignee)
            for (const auto& nd : g.enfants)
            {
                if (! std::isfinite (nd.perte)) continue;
                Candidat c; c.perte = nd.perte;
                for (int k = 0; k < kOctetsProfil; ++k) c.profil[k] = nd.profil[k];
                cands.push_back (c);
            }
        if (cands.size() < 4) continue;
        std::sort (cands.begin(), cands.end(),
                   [] (const Candidat& a, const Candidat& b) { return a.perte < b.perte; });

        // le meilleur, et le partenaire dont l'ecart approche le mieux la consigne
        const Candidat& bon = cands.front();
        int iPire = -1; double meilleurEcart = 1e9;
        for (size_t k = 1; k < cands.size(); ++k)
        {
            const double d = std::abs ((cands[k].perte - bon.perte) - ecartVoulu);
            if (d < meilleurEcart) { meilleurEcart = d; iPire = (int) k; }
        }
        if (iPire < 0) continue;
        const double ecart = cands[(size_t) iPire].perte - bon.perte;
        // /!\ ON REFUSE UN ESSAI DONT L'ECART N'EST PAS CELUI QU'ON VOULAIT.
        // Melanger des ecarts de 0,2 et de 4 dB dans la meme colonne rendrait
        // le resultat illisible : on ne saurait plus quelle question on a posee.
        if (ecart < ecartVoulu * 0.5) continue;

        const double f0 = (rap.f0 > 0.0) ? rap.f0 : 220.0;

        std::vector<double> yBon, yPire;
        rendProfil (bon.profil, yBon, kNRendu, s.fs, f0);
        rendProfil (cands[(size_t) iPire].profil, yPire, kNRendu, s.fs, f0);

        std::vector<double> cible (s.x.begin() + trouveOnset (s.x), s.x.end());
        if ((int) cible.size() > kNRendu) cible.resize ((size_t) kNRendu);

        // ⭐ L'ORDRE EST TIRE AU SORT. Sans ca le test se devine en trois essais.
        const bool bonEnA = (uni() < 0.5);

        // /!\ LES TROIS ENSEMBLE, pas chacun de son cote : c'est le niveau
        // RELATIF entre eux qui doit disparaitre.
        memeNiveau (cible, bonEnA ? yBon : yPire, bonEnA ? yPire : yBon);

        char nom[512];
        std::snprintf (nom, sizeof (nom), "%scible_%02d.wav", dossier.c_str(), n + 1);
        ecritWav (nom, cible, s.fs);
        std::snprintf (nom, sizeof (nom), "%s%02d_A.wav", dossier.c_str(), n + 1);
        ecritWav (nom, bonEnA ? yBon : yPire, s.fs);
        std::snprintf (nom, sizeof (nom), "%s%02d_B.wav", dossier.c_str(), n + 1);
        ecritWav (nom, bonEnA ? yPire : yBon, s.fs);

        std::fprintf (cle, "%d;%s;%.3f;%c;%.4f;%.4f;%s\n", n + 1,
                      ancre ? "ancrage" : "test", ecart, bonEnA ? 'A' : 'B',
                      bonEnA ? bon.perte : cands[(size_t) iPire].perte,
                      bonEnA ? cands[(size_t) iPire].perte : bon.perte,
                      fichiers[i].c_str());
        std::fprintf (feuille, "  essai %02d : ____\n", n + 1);

        if (ancre) ++ancres;
        ++n;
        std::printf ("\r  %d / %d essais...", n, nEssais);
        std::fflush (stdout);
    }
    std::fprintf (feuille,
        "\n\nQuand c'est fini, relancer :\n"
        "  powershell -File tools\\ecoute.ps1 -Depouille\n");
    std::fclose (cle);
    std::fclose (feuille);

    std::printf ("\r  %d essais ecrits (%d ancrages, %d tests)          \n",
                 n, ancres, n - ancres);
    if (n < 8)
    {
        std::printf ("  !! trop peu d'essais exploitables pour conclure\n");
        return 1;
    }
    std::printf ("\n  dossier : %s\n", dossier.c_str());
    std::printf ("  A ECOUTER : cible_NN.wav, puis NN_A.wav et NN_B.wav\n");
    std::printf ("  /!\\ NE PAS OUVRIR cle.csv avant d'avoir repondu.\n\n");
    return 0;
}
