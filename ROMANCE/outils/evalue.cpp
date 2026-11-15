// =============================================================================
//  evalue.cpp - Le reseau aide-t-il VRAIMENT ?
//
//  ⭐ « Ca ne fait jamais pire » n'est pas « ca aide ». Comme le reseau est mis
//  en concurrence avec l'analyse heuristique et que la perte tranche, l'ensemble
//  ne peut pas regresser - ce qui rend la question « est-ce que ca marche »
//  impossible a poser si l'on ne mesure que l'ensemble. D'ou trois colonnes,
//  toutes A BUDGET EGAL :
//
//    · amorce heuristique seule, puis recherche
//    · amorce reseau seule, puis recherche
//    · le meilleur des deux (le mode d'usage)
//
//  /!\ ET ON REGARDE AUSSI L'AMORCE AVANT RECHERCHE. Si le reseau part mieux
//  mais arrive au meme endroit, il fait gagner du TEMPS, pas de la qualite -
//  c'est une conclusion differente et elle change ce qu'on doit ameliorer.
//
//  Usage : evalue.exe <liste.txt> <reseau.rnn> <budget> <sortie.csv> [max]
// =============================================================================
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Perte.h"
#include "Recherche.h"
#include "Reseau.h"
#include "mesure.h"

using namespace romance;
using namespace outils;

struct Resultat
{
    double amorce = 0.0, finale = 0.0, reference = 0.0;
};

/** ⭐ LA PERTE DE REFERENCE, sur une fenetre FIXE de 1,5 s.

    /!\ L'utilisateur a choisi une fenetre d'ajustement VARIABLE. C'est
    defendable - chaque son est juge sur sa propre duree - mais ca rend les
    pertes incomparables entre sons : un son juge sur 1 s et un autre sur 4 s
    ne mesurent pas la meme chose. On ajuste donc sur la fenetre du son, et on
    COMPARE sur une fenetre commune. Sans ca, toutes les moyennes de ce tableau
    seraient des additions de pommes et de poires. */
static double perteReference (const uint8_t* profil, double f0,
                              const std::vector<double>& cible, double fs)
{
    // /!\ ON REND LE PROFIL, PAS LA RECETTE. Depuis que la recherche choisit
    // son moteur, `st.meilleure` n'est que la tranche ROM du resultat : rendre
    // cette recette quand la recherche a retenu un patch FM mesurerait un son
    // que personne n'entendra jamais - et le tableau afficherait des chiffres
    // faux sans que rien ne le signale.
    const int nRef = 66150;   // 1,5 s, toujours
    std::vector<double> c (cible.begin(),
        cible.begin() + std::min ((size_t) nRef, cible.size()));
    if (c.size() < 4096) return 0.0;
    std::vector<double> y;
    rendProfil (profil, y, (int) c.size(), fs, f0);
    return perte (empreinte (c, fs), empreinte (y, fs));
}

static Resultat lance (const std::vector<double>& x, double fs,
                       const ConfigGrain& cfg, ReglagesRecherche reg,
                       const Reseau* reseau)
{
    RapportAnalyse rap;
    const auto st = resynthetise (x, fs, cfg, reg, rap,
                                  [] (const EtatRecherche&) { return true; }, reseau);
    Resultat r;
    r.amorce = st.perteDepart;
    r.finale = st.perteMeilleure;
    const std::vector<double> cale (x.begin() + trouveOnset (x), x.end());
    r.reference = perteReference (st.profil, rap.f0 > 0.0 ? rap.f0 : 220.0,
                                  cale, fs);
    return r;
}

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage : evalue <liste.txt> <reseau.rnn> <budget> <sortie.csv> [max]\n");
        return 1;
    }
    const std::string cheminListe = argv[1], cheminRes = argv[2], cheminCsv = argv[4];
    const int budget = std::atoi (argv[3]);
    const int maxFic = (argc > 5) ? std::atoi (argv[5]) : 60;

    Reseau reseau;
    const bool aReseau = reseau.lit (cheminRes);
    std::printf ("\nROMANCE - evaluation\n--------------------\n");
    std::printf ("  reseau : %s\n", aReseau ? cheminRes.c_str() : "ABSENT (heuristique seule)");
    // /!\ UN RESEAU DEMANDE ET INTROUVABLE EST UNE ERREUR, pas un mode de
    // repli. Sans ca, une evaluation finale peut tourner trente minutes sur
    // l'heuristique seule, imprimer « ABSENT » au milieu de son rapport, et
    // rendre 0 - on lit alors les chiffres en croyant juger le reseau.
    // Comparer AVEC et SANS reste possible : on passe une chaine vide.
    if (! aReseau && ! cheminRes.empty())
    {
        std::printf ("  !! reseau introuvable ou incompatible : %s\n", cheminRes.c_str());
        return 2;
    }
    std::printf ("  budget : %d evaluations par son, %d sons max\n", budget, maxFic);

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

    // /!\ Echantillonnage REGULIER dans la liste, pas les N premiers : les
    // dossiers sont tries, donc prendre le debut reviendrait a n'evaluer qu'une
    // seule banque.
    const int pas = std::max (1, (int) fichiers.size() / maxFic);

    FILE* csv = std::fopen (cheminCsv.c_str(), "wb");
    if (csv) std::fprintf (csv, "chemin;h_amorce;h_finale;r_amorce;r_finale;"
                                "m_amorce;m_finale;plancher\n");

    const double fsCible = 44100.0;
    double sH = 0.0, sR = 0.0, sM = 0.0, sHa = 0.0, sRa = 0.0;
    double rH = 0.0, rR = 0.0, rM = 0.0;
    int n = 0, gagnesReseau = 0;

    for (size_t i = 0; i < fichiers.size() && n < maxFic; i += (size_t) pas)
    {
        const Son s = litWav (fichiers[i].c_str());
        if (! s.ok || s.x.size() < 8192) continue;

        const auto cfg = grainTransparent (s.fs);
        ReglagesRecherche reg;
        reg.evaluations = budget;
        reg.graine = 4242u;
        (void) fsCible;

        reg.amorceDe = ReglagesRecherche::Amorce::Heuristique;
        const Resultat h = lance (s.x, s.fs, cfg, reg, nullptr);

        Resultat r = h, m = h;
        if (aReseau)
        {
            reg.amorceDe = ReglagesRecherche::Amorce::Reseau;
            r = lance (s.x, s.fs, cfg, reg, &reseau);
            reg.amorceDe = ReglagesRecherche::Amorce::Meilleur;
            m = lance (s.x, s.fs, cfg, reg, &reseau);
        }

        // part au plancher : sans elle un joli score peut ne rien vouloir dire
        std::vector<double> c (s.x.begin() + trouveOnset (s.x), s.x.end());
        if ((int) c.size() > reg.nEchComparaison) c.resize ((size_t) reg.nEchComparaison);
        const double pl = partAuPlancher (empreinte (c, s.fs));

        // /!\ LA REFERENCE MANQUAIT DANS LE FICHIER, et c'est la seule mesure
        // comparable d'un son a l'autre. La perte d'ajustement se prend sur une
        // fenetre VARIABLE : on ne peut pas en faire une statistique appariee.
        // Le tableau resume affichait bien la reference, mais sans le detail par
        // son il etait impossible de dire si l'ecart depassait le bruit - on ne
        // pouvait qu'y croire, ce qui n'est pas une mesure.
        if (csv) std::fprintf (csv, "%s;%.4f;%.4f;%.4f;%.4f;%.4f;%.4f;%.3f;"
                               "%.4f;%.4f;%.4f\n",
                               fichiers[i].c_str(), h.amorce, h.finale,
                               r.amorce, r.finale, m.amorce, m.finale, pl,
                               h.reference, r.reference, m.reference);

        sHa += h.amorce; sRa += r.amorce;
        sH += h.finale;  sR += r.finale;  sM += m.finale;
        rH += h.reference; rR += r.reference; rM += m.reference;
        // /!\ On compte les victoires sur la REFERENCE, pas sur la perte
        // d'ajustement : c'est la seule des deux qui soit comparable.
        if (r.reference < h.reference) ++gagnesReseau;
        ++n;
        std::printf ("\r  %d / %d sons...", n, maxFic);
        std::fflush (stdout);
    }
    if (csv) std::fclose (csv);
    if (n == 0) { std::printf ("\n  !! aucun son exploitable\n"); return 1; }

    std::printf ("\r  %d sons evalues                    \n\n", n);
    std::printf ("                      amorce    ajustee   REFERENCE (1,5 s)\n");
    std::printf ("  heuristique      %8.3f   %8.3f   %8.3f dB\n",
                 sHa / n, sH / n, rH / n);
    if (aReseau)
    {
        std::printf ("  reseau           %8.3f   %8.3f   %8.3f dB\n",
                     sRa / n, sR / n, rR / n);
        std::printf ("  meilleur des 2              %8.3f   %8.3f dB\n",
                     sM / n, rM / n);
        std::printf ("\n  le reseau bat l'heuristique sur %d / %d sons (%.0f %%)\n",
                     gagnesReseau, n, 100.0 * gagnesReseau / n);
        // =====================================================================
        //  /!\ CETTE LIGNE MENTAIT DE DEUX FACONS A LA FOIS - piege 29 sous une
        //  quatrieme forme : un resume qu'on ne peut pas verifier contre son
        //  propre tableau.
        //
        //    1. elle s'appelait « gain » alors qu'elle rend `essai - temoin`
        //       sur une PERTE : un + y veut dire PIRE. On lisait « le reseau
        //       gagne 0,165 dB » pour un reseau qui perdait ;
        //    2. elle comparait `meilleur des 2` a l'heuristique, alors que la
        //       phrase juste au-dessus parle du RESEAU. Deux comparaisons
        //       differentes dans deux lignes consecutives, sans le dire.
        //
        //  Mesure du 4 septembre 2026 : reseau 5,088 contre heuristique 4,638,
        //  soit +0,450 dB de PLUS - et la ligne affichait « +0,165 ».
        //
        //  On ecrit donc les deux ecarts, nommes, avec le sens du signe.
        // =====================================================================
        const double dAmorce = (sRa - sHa) / n;
        const double dRes    = (rR - rH) / n;
        const double dMeil   = (rM - rH) / n;
        auto verdict = [] (double d)
        {
            return d < -0.001 ? "le reseau GAGNE"
                 : d >  0.001 ? "le reseau PERD" : "egalite";
        };
        std::printf ("\n  ecarts sur la REFERENCE (une PERTE : negatif = meilleur)\n");
        std::printf ("    reseau - heuristique          %+.3f dB   %s\n",
                     dRes, verdict (dRes));
        std::printf ("    meilleur des 2 - heuristique  %+.3f dB   %s\n",
                     dMeil, verdict (dMeil));
        std::printf ("    amorce : reseau - heuristique %+.3f dB   "
                     "(positif = part de plus haut, voir piege 31)\n", dAmorce);
    }
    std::printf ("\n  detail : %s\n", cheminCsv.c_str());
    return 0;
}
