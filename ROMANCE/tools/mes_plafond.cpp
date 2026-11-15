// =============================================================================
//  mes_plafond.cpp - QUE COUTE NOTRE FORMAT, INDEPENDAMMENT DE LA RECHERCHE ?
//
//  ⭐⭐ ON TRAVAILLAIT A L'ENVERS (idee de l'utilisateur, 2026-08-28).
//
//  Toutes nos mesures comparaient notre format A LUI-MEME. L'oracle fabrique sa
//  cible depuis un profil ROMANCE : il dit si la recherche retrouve ce que le
//  format sait deja ecrire, jamais CE QUI MANQUE AU FORMAT.
//
//  Ici on mesure l'autre moitie. Un etalon SANS CONTRAINTE, puis le meme etalon
//  contraint UNE DIMENSION A LA FOIS. La ou l'ecart saute, on sait quoi ajouter
//  au VST ; la ou il ne saute pas, on sait qu'une fonctionnalite serait
//  inutile - et ca vaut autant.
//
//  ⭐⭐ L'ETALON SE VERIFIE LUI-MEME, et c'est ce qui manquait a ma premiere
//  version.
//
//  /!\ ELLE RESYNTHETISAIT EN ADDITIF A PHASE NULLE. Trois consequences, toutes
//  invisibles dans le resultat : les transitoires etaient detruits, le bruit
//  n'existait pas, et l'inharmonicite non plus. Le « plafond absolu » sortait a
//  4,42 dB - au-dessus de ce que notre recherche complete atteint - et un
//  modele a HUIT partiels y battait un modele a vingt et un, ce qui est
//  logiquement impossible. Le banc mesurait ses propres manques.
//
//  Ici l'etalon est une ANALYSE / SYNTHESE PAR STFT a recouvrement. Sans
//  contrainte, elle doit reconstruire le signal a l'identique - donc rendre
//  ZERO. Ce zero est le TEMOIN : tant qu'il n'y est pas, aucune autre ligne du
//  tableau ne veut rien dire.
//
//  Usage : mes_plafond.exe <liste.txt> [nSons]
// =============================================================================
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

#include "Analyse.h"
#include "Perte.h"
#include "wav.h"

using namespace romance;

namespace
{
    constexpr int kFen  = 1024;
    constexpr int kSaut = 256;            // 75 % de recouvrement

    void ifft (std::vector<std::complex<double>>& x)
    {
        for (auto& v : x) v = std::conj (v);
        fft (x);
        const double k = 1.0 / (double) x.size();
        for (auto& v : x) v = std::conj (v) * k;
    }

    /** Ce qu'on impose au spectre de chaque trame. */
    struct Contrainte
    {
        int  nHarm      = 0;        // 0 = on garde TOUT (le temoin)
        bool communes   = false;    // une seule forme d'enveloppe pour tous
        bool sansBruit  = false;    // ne garder QUE les raies harmoniques

        /** ⭐⭐ LE SPECTRE DESSINE (idee de l'utilisateur, 2026-08-28).

            N cellules sur un axe de frequences LOGARITHMIQUE et A\OLU. Chaque
            cellule impose un niveau a toute sa bande - c'est exactement ce
            qu'on dessinerait a la souris, et exactement ce qu'un moteur
            construirait par FFT inverse.

            /!\ L'AXE EST A\OLU, PAS HARMONIQUE, et c'est tout l'interet. Une
            grille harmonique ne peut decrire ni un partiel inharmonique ni du
            bruit - or la mesure dit que c'est le poste le plus cher du format
            (2,53 dB). Une cellule etroite et haute donne un partiel a la
            frequence qu'on veut ; une zone large donne une bande de bruit.

            `morph` : 0 = un seul spectre pour toute la duree, 1 = deux
            spectres (debut et fin) interpoles. Sans lui, un spectre dessine
            sonne fige - c'est sa faiblesse connue, et sa correction. */
        int  nCellules  = 0;        // 0 = pas de contrainte de spectre
        bool morph      = false;

        /** ⭐⭐ L'AXE DU DESSIN, et c'est lui qui decide de tout.

            0 = bandes LOGARITHMIQUES absolues (ma premiere version).
            1 = une cellule PAR HARMONIQUE (correction de l'utilisateur).

            /!\ J'AVAIS MESURE LA MAUVAISE CHOSE. Sur un axe log a 64
            cellules, une cellule couvre un tiers d'octave : dans le grave elle
            noie les cinq premiers partiels - les plus importants - dans UNE
            valeur. Le dessin sortait a 6,59 dB contre 4,77 pour notre recette,
            et j'ai failli conclure que l'idee ne valait rien.

            Une cellule PAR HARMONIQUE est une resolution sans rapport, et
            c'est la seule qui permet ce que l'utilisateur decrit : filtrer les
            rangs pairs, les impairs, dessiner un peigne. */
        int  axe        = 0;

        /** ⭐ MODE RESIDU (test B) : les harmoniques passent INTACTES, et seul
            ce qui n'est pas harmonique est contraint en bandes. C'est le
            spectre dessine en COMPLEMENT de la recette, pas en remplacement. */
        bool residu     = false;

        /** ⭐ ENVELOPPE PROPRE A CHAQUE BANDE : le niveau vise suit la trame au
            lieu d'etre interpole entre un debut et une fin. C'est le PLAFOND de
            ce qu'un modele de bruit peut donner - il dit combien il resterait a
            gagner en payant une enveloppe par bande dans le format. */
        bool envParBande = false;

        /** ⭐⭐ LA TABLE D'HARMONIQUES DESSINABLE, telle qu'elle vivrait chez
            nous - pas telle que je l'avais mal testee.

            Une table d'ondes est une FORME FIXE : les rapports entre
            harmoniques ne bougent pas. Le temps vient d'ailleurs - de
            l'enveloppe d'amplitude de la couche, et du FILTRE dont la coupure
            se deplace. C'est ainsi que fonctionne Serum, et c'est ainsi que
            fonctionnerait notre moteur.

            /!\ MON PREMIER TEST L'AVAIT MANQUE. J'avais mesure « un dessin
            harmonique qui remplace TOUTE la recette, evolution comprise,
            reduite a deux points » - 6,90 dB, contre 4,77 pour la recette. Le
            coupable etait le TEMPS, pas la frequence : a resolution
            frequentielle identique (21 rangs), l'ecart valait 2,13 dB.

            Ici : `tableFixe` = les rapports harmoniques sont figes ;
            `filtreVariable` = on ajuste par trame UNE coupure de passe-bas et
            UN gain. Deux nombres par trame, ce qu'un vrai moteur a vraiment. */
        bool tableFixe      = false;
        bool filtreVariable = false;
    };

    /** Analyse / synthese a recouvrement, avec la contrainte demandee.

        /!\ LA PHASE EST CELLE DE LA CIBLE, toujours. C'est ce qui distingue cet
        etalon d'un additif : on ne remplace pas le son, on RETIRE ce que la
        contrainte interdit et on garde le reste tel quel. Une contrainte ne
        peut donc jamais faire MIEUX que l'absence de contrainte - et si le
        tableau le montre, c'est le banc qui a un defaut.
    */
    std::vector<double> etalon (const std::vector<double>& x, double fs,
                                double f0, const Contrainte& c)
    {
        const int n = (int) x.size();
        std::vector<double> y ((size_t) n, 0.0), poids ((size_t) n, 0.0);
        std::vector<double> fen ((size_t) kFen);
        for (int i = 0; i < kFen; ++i)
            fen[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) kFen);

        const int nTr = std::max (1, (n - kFen) / kSaut + 1);
        const int nRaies = kFen / 2 + 1;

        // --- passe 1 : les modules, trame par trame (pour les enveloppes)
        std::vector<std::vector<double>> mod ((size_t) nTr,
                                              std::vector<double> ((size_t) nRaies, 0.0));
        std::vector<std::complex<double>> b ((size_t) kFen);
        for (int t = 0; t < nTr; ++t)
        {
            for (int i = 0; i < kFen; ++i)
            {
                const size_t j = (size_t) (t * kSaut + i);
                b[(size_t) i] = (j < x.size() ? x[j] : 0.0) * fen[(size_t) i];
            }
            fft (b);
            for (int k = 0; k < nRaies; ++k) mod[(size_t) t][(size_t) k] = std::abs (b[(size_t) k]);
        }

        // ⭐ LA FORME COMMUNE : le niveau total de chaque trame, normalise.
        // C'est ce que fait une recette a decroissance unique - toutes les
        // harmoniques suivent la meme courbe, seul leur niveau differe.
        std::vector<double> forme ((size_t) nTr, 0.0);
        std::vector<double> nivRaie ((size_t) nRaies, 0.0);
        if (c.communes)
        {
            for (int t = 0; t < nTr; ++t)
            {
                double s = 0.0;
                for (int k = 1; k < nRaies; ++k) s += mod[(size_t) t][(size_t) k];
                forme[(size_t) t] = s;
            }
            double cr = 1e-30;
            for (double v : forme) cr = std::max (cr, v);
            for (double& v : forme) v /= cr;
            for (int k = 0; k < nRaies; ++k)
                for (int t = 0; t < nTr; ++t)
                    nivRaie[(size_t) k] = std::max (nivRaie[(size_t) k],
                                                    mod[(size_t) t][(size_t) k]);
        }

        // --- quelles raies sont AUTORISEES
        std::vector<bool> garde ((size_t) nRaies, true);
        if (c.nHarm > 0)
        {
            std::fill (garde.begin(), garde.end(), false);
            for (int h = 1; h <= c.nHarm; ++h)
            {
                const double f = (double) h * f0;
                if (f >= 0.5 * fs) break;
                const int r = (int) std::round (f * (double) kFen / fs);
                // /!\ Trois raies par harmonique : une fenetre de Hann etale
                // chaque sinusoide sur trois points. N'en garder qu'une
                // reviendrait a filtrer le partiel, pas a le conserver.
                for (int d = -1; d <= 1; ++d)
                    if (r + d >= 0 && r + d < nRaies) garde[(size_t) (r + d)] = true;
            }
        }
        else if (c.sansBruit)
        {
            // sans contrainte de rang mais sans bruit : on garde les raies
            // dont le module depasse nettement leurs voisines
            std::fill (garde.begin(), garde.end(), false);
            for (int t = 0; t < nTr; ++t)
                for (int k = 2; k < nRaies - 2; ++k)
                    if (mod[(size_t) t][(size_t) k] > mod[(size_t) t][(size_t) (k - 2)] * 2.0
                     && mod[(size_t) t][(size_t) k] > mod[(size_t) t][(size_t) (k + 2)] * 2.0)
                        garde[(size_t) k] = true;
        }

        // ⭐ LE SPECTRE DESSINE : on calcule le niveau VISE de chaque cellule,
        // puis on y ramene toutes les raies de la bande.
        //
        // /!\ ON MOYENNE EN ENERGIE, pas en amplitude. Une bande large qui
        // contient une raie forte et vingt raies faibles a une amplitude
        // moyenne minuscule : en la reposant partout, on remplacerait un
        // partiel par du souffle. L'energie conserve ce qu'on entend.
        std::vector<int> cellDe ((size_t) nRaies, -1);
        std::vector<std::vector<double>> viseCell;
        if (c.nCellules > 0)
        {
            const double fLo = 20.0, fHi = 0.5 * fs;
            for (int k = 1; k < nRaies; ++k)
            {
                const double f = (double) k * fs / (double) kFen;
                if (c.axe == 1)
                {
                    // ⭐ UNE CELLULE PAR HARMONIQUE. La raie appartient au rang
                    // dont elle est la plus proche, et seulement si elle en est
                    // assez proche - sinon on rangerait le bruit dans les
                    // partiels et le dessin ne voudrait plus rien dire.
                    const double rang = f / f0;
                    const int r = (int) std::round (rang);
                    if (r >= 1 && r <= c.nCellules && std::abs (rang - (double) r) < 0.5)
                        cellDe[(size_t) k] = r - 1;
                    // en mode RESIDU c'est l'inverse : on ne contraint QUE ce
                    // qui n'est PAS un partiel
                    if (c.residu)
                        cellDe[(size_t) k] = (cellDe[(size_t) k] >= 0) ? -1
                            : std::min (c.nCellules - 1, std::max (0,
                                  (int) (std::log (std::max (fLo, f) / fLo)
                                         / std::log (fHi / fLo) * c.nCellules)));
                    continue;
                }
                if (f < fLo) continue;
                const double u = std::log (f / fLo) / std::log (fHi / fLo);
                cellDe[(size_t) k] = std::min (c.nCellules - 1,
                                       std::max (0, (int) (u * c.nCellules)));
            }

            // niveau moyen par cellule, sur le DEBUT et sur la FIN
            const int nSeg = c.morph ? 2 : 1;
            viseCell.assign ((size_t) nSeg,
                             std::vector<double> ((size_t) c.nCellules, 0.0));
            std::vector<std::vector<int>> compte ((size_t) nSeg,
                             std::vector<int> ((size_t) c.nCellules, 0));
            for (int t = 0; t < nTr; ++t)
            {
                const int seg = (nSeg == 1) ? 0 : ((t < nTr / 2) ? 0 : 1);
                for (int k = 1; k < nRaies; ++k)
                {
                    const int ce = cellDe[(size_t) k];
                    if (ce < 0) continue;
                    const double m = mod[(size_t) t][(size_t) k];
                    viseCell[(size_t) seg][(size_t) ce] += m * m;
                    ++compte[(size_t) seg][(size_t) ce];
                }
            }
            for (int g = 0; g < nSeg; ++g)
                for (int ce = 0; ce < c.nCellules; ++ce)
                    viseCell[(size_t) g][(size_t) ce] =
                        std::sqrt (viseCell[(size_t) g][(size_t) ce]
                                   / std::max (1, compte[(size_t) g][(size_t) ce]));
        }

        // ⭐ LA TABLE FIXE : le rapport entre harmoniques, moyenne sur toute
        // la duree. C'est la « forme d'onde » que l'utilisateur dessinerait.
        std::vector<double> tableA ((size_t) nRaies, 0.0);
        std::vector<double> gainTr ((size_t) nTr, 1.0), coupeTr ((size_t) nTr, 1e9);
        if (c.tableFixe)
        {
            for (int k = 1; k < nRaies; ++k)
            {
                double e2 = 0.0;
                for (int t = 0; t < nTr; ++t)
                {
                    const double m = mod[(size_t) t][(size_t) k];
                    e2 += m * m;
                }
                tableA[(size_t) k] = std::sqrt (e2 / std::max (1, nTr));
            }

            // ⭐ PAR TRAME : la coupure ET le gain qui collent le mieux.
            //
            // /!\ DEUX NOMBRES PAR TRAME, PAS SOIXANTE-QUATRE. C'est ce que
            // notre moteur possede vraiment - une enveloppe d'amplitude et une
            // coupure de filtre. Donner davantage mesurerait un moteur qu'on
            // n'a pas.
            for (int t = 0; t < nTr; ++t)
            {
                double meilleur = 1e30;
                for (int ic = 0; ic <= 40; ++ic)
                {
                    const double fc = 40.0 * std::pow (0.5 * fs / 40.0,
                                                       (double) ic / 40.0);
                    // gain optimal en moindres carres, pour cette coupure
                    double num = 0.0, den = 1e-30;
                    for (int k = 1; k < nRaies; ++k)
                    {
                        if (garde[(size_t) k] == false) continue;
                        const double f = (double) k * fs / (double) kFen;
                        const double r = f / fc;
                        // 4 poles : -24 dB/oct au-dela de la coupure
                        const double h = 1.0 / std::sqrt (1.0 + std::pow (r, 8.0));
                        const double sk = tableA[(size_t) k] * h;
                        num += mod[(size_t) t][(size_t) k] * sk;
                        den += sk * sk;
                    }
                    const double g = num / den;
                    double err = 0.0;
                    for (int k = 1; k < nRaies; ++k)
                    {
                        if (garde[(size_t) k] == false) continue;
                        const double f = (double) k * fs / (double) kFen;
                        const double r = f / fc;
                        const double h = 1.0 / std::sqrt (1.0 + std::pow (r, 8.0));
                        const double d = mod[(size_t) t][(size_t) k] - g * tableA[(size_t) k] * h;
                        err += d * d;
                    }
                    if (err < meilleur)
                    { meilleur = err; gainTr[(size_t) t] = g;
                      coupeTr[(size_t) t] = c.filtreVariable ? fc : 1e9; }
                }
            }
        }

        // --- passe 2 : synthese
        for (int t = 0; t < nTr; ++t)
        {
            for (int i = 0; i < kFen; ++i)
            {
                const size_t j = (size_t) (t * kSaut + i);
                b[(size_t) i] = (j < x.size() ? x[j] : 0.0) * fen[(size_t) i];
            }
            fft (b);
            for (int k = 0; k < nRaies; ++k)
            {
                std::complex<double> v = b[(size_t) k];
                if (! garde[(size_t) k]) v = 0.0;
                else if (c.nCellules > 0 && cellDe[(size_t) k] >= 0
                         && mod[(size_t) t][(size_t) k] > 1e-30)
                {
                    const int ce = cellDe[(size_t) k];
                    double vise = viseCell[0][(size_t) ce];
                    if (c.envParBande)
                    {
                        // niveau RMS de la bande DANS CETTE TRAME
                        double e2 = 0.0; int nb = 0;
                        for (int q = 1; q < nRaies; ++q)
                            if (cellDe[(size_t) q] == ce)
                            { const double m2 = mod[(size_t) t][(size_t) q];
                              e2 += m2 * m2; ++nb; }
                        vise = std::sqrt (e2 / std::max (1, nb));
                    }
                    else if (c.morph)
                    {
                        const double u = (nTr > 1) ? (double) t / (double) (nTr - 1) : 0.0;
                        vise = viseCell[0][(size_t) ce] * (1.0 - u)
                             + viseCell[1][(size_t) ce] * u;
                    }
                    v *= vise / mod[(size_t) t][(size_t) k];
                }
                else if (c.tableFixe && mod[(size_t) t][(size_t) k] > 1e-30)
                {
                    const double f = (double) k * fs / (double) kFen;
                    const double r = f / coupeTr[(size_t) t];
                    const double h = 1.0 / std::sqrt (1.0 + std::pow (r, 8.0));
                    const double vise = gainTr[(size_t) t] * tableA[(size_t) k] * h;
                    v *= vise / mod[(size_t) t][(size_t) k];
                }
                else if (c.communes && mod[(size_t) t][(size_t) k] > 1e-30)
                {
                    // meme phase, module remplace par « niveau x forme commune »
                    const double vise = nivRaie[(size_t) k] * forme[(size_t) t];
                    v *= vise / mod[(size_t) t][(size_t) k];
                }
                b[(size_t) k] = v;
                if (k > 0 && k < kFen / 2) b[(size_t) (kFen - k)] = std::conj (v);
            }
            ifft (b);
            for (int i = 0; i < kFen; ++i)
            {
                const size_t j = (size_t) (t * kSaut + i);
                if (j >= y.size()) break;
                y[j] += b[(size_t) i].real() * fen[(size_t) i];
                poids[j] += fen[(size_t) i] * fen[(size_t) i];
            }
        }
        for (size_t i = 0; i < y.size(); ++i)
            if (poids[i] > 1e-9) y[i] /= poids[i];
        return y;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage : mes_plafond <liste.txt> [nSons]\n"); return 1; }
    const int nSons = (argc > 2) ? std::atoi (argv[2]) : 20;

    std::vector<std::string> fichiers;
    if (FILE* f = std::fopen (argv[1], "rb"))
    {
        char l[2048];
        // /!\ On saute les « ._xxx.wav » : des forks de ressource macOS, pas
        // des WAV. Ils occupent le debut de la liste, et ma premiere version
        // ne lisait que le debut - d'ou « 0 sons » sans explication.
        while (std::fgets (l, sizeof (l), f))
        {
            std::string s (l);
            while (! s.empty() && (s.back() == 0x0a || s.back() == 0x0d)) s.pop_back();
            if (s.empty()) continue;
            const auto b = s.find_last_of (0x5c);
            const std::string nom = (b == std::string::npos) ? s : s.substr (b + 1);
            if (nom.size() > 2 && nom[0] == 0x2e && nom[1] == 0x5f) continue;
            fichiers.push_back (s);
        }
        std::fclose (f);
    }
    if (fichiers.empty()) { std::printf ("liste vide\n"); return 1; }

    // ⭐⭐ SEULES LES LIGNES EMBOITEES SE COMPARENT.
    //
    // /!\ Ma premiere presentation affichait un ecart entre CHAQUE ligne et la
    // precedente. Or « 8 partiels » n'est pas plus contraint que « 21 a
    // decroissances communes » - ce sont deux axes differents. L'ecart entre
    // eux sortait donc NEGATIF, ce qui n'a aucun sens et faisait douter du banc
    // entier. Ici chaque cas dit de QUI il est le sous-ensemble ; les autres
    // n'affichent pas d'ecart.
    struct Cas { const char* nom; Contrainte c; int parent; };
    const Cas cas[] = {
        { "TEMOIN (sans contrainte)", { 0,   false, false, 0, false, 0, false, false, false, false }, -1 },
        { "128 partiels",             { 128, false, false, 0, false, 0, false, false, false, false },  0 },
        { "64 partiels",              { 64,  false, false, 0, false, 0, false, false, false, false },  1 },
        { "21 partiels",              { 21,  false, false, 0, false, 0, false, false, false, false },  2 },
        { "8 partiels",               { 8,   false, false, 0, false, 0, false, false, false, false },  3 },
        { "21, decr. communes",       { 21,  true,  false, 0, false, 0, false, false, false, false }, 3 },
        // ⭐⭐ LE SPECTRE DESSINE, a comparer a « 21 partiels » - notre format.
        { "spectre log 64 A->B",      { 0,   false, false, 64,  true,  0, false, false, false, false }, 0 },
        // ⭐⭐ UNE CELLULE PAR HARMONIQUE - la version corrigee de l'idee.
        { "dessin 21 harm. A->B",     { 21,  false, false, 21,  true,  1, false, false, false, false }, 3 },
        { "dessin 64 harm. A->B",     { 64,  false, false, 64,  true,  1, false, false, false, false }, 2 },
        { "dessin 128 harm. A->B",    { 128, false, false, 128, true,  1, false, false, false, false }, 1 },
        // ⭐⭐ TEST B : les partiels INTACTS, plus un dessin pour le residu.
        { "21 partiels + residu 32",  { 0,   false, false, 32,  true,  1, true,  false, false, false }, 3 },
        { "21 partiels + residu 64",  { 0,   false, false, 64,  true,  1, true,  false, false, false }, 3 },
        // ⭐ Le PLAFOND du modele de bruit : une enveloppe par bande.
        { "residu 64, env/bande",     { 0,   false, false, 64,  false, 1, true,  true,  false, false }, 3 },
        { "residu 128, env/bande",    { 0,   false, false, 128, false, 1, true,  true,  false, false }, 3 },
        // ⭐⭐ LA TABLE D'HARMONIQUES, telle qu'elle vivrait chez nous.
        { "table 21 h. + env",        { 21,  false, false, 0, false, 0, false, false, true,  false }, 3 },
        { "table 21 h. + env + filtre",{ 21, false, false, 0, false, 0, false, false, true,  true  }, 3 },
        { "table 64 h. + env + filtre",{ 64, false, false, 0, false, 0, false, false, true,  true  }, 2 },
        { "table 128 h. + env + filtre",{128, false, false, 0, false, 0, false, false, true, true  }, 1 },
    };
    const int nCas = (int) (sizeof (cas) / sizeof (cas[0]));

    std::vector<std::vector<double>> pertes ((size_t) nCas);
    int retenus = 0, rejLecture = 0, rejCourt = 0, rejF0 = 0;

    for (const auto& chemin : fichiers)
    {
        if (retenus >= nSons) break;
        const auto son = outils::litWav (chemin.c_str());
        if (! son.ok || son.x.size() < 8000) { ++rejLecture; continue; }
        const double fs = son.fs;
        std::vector<double> x = son.x;
        const int onset = trouveOnset (x);
        x.erase (x.begin(), x.begin() + onset);
        if ((int) x.size() < 16000) { ++rejCourt; continue; }
        x.resize (std::min<size_t> (x.size(), (size_t) (2.0 * fs)));

        RapportAnalyse rap;
        const double f0 = estimeF0 (x, fs, 0, &rap);
        if (! (f0 > 30.0 && f0 < 4000.0)) { ++rejF0; continue; }

        const auto cible = empreinte (x, fs, kPlancherPropre);
        for (int c = 0; c < nCas; ++c)
        {
            const auto y = etalon (x, fs, f0, cas[c].c);
            pertes[(size_t) c].push_back (perte (cible, empreinte (y, fs, kPlancherPropre)));
        }
        ++retenus;
        std::printf ("  %d / %d sons...\r", retenus, nSons);
        std::fflush (stdout);
    }

    std::printf ("\n\nCE QUE CHAQUE CONTRAINTE COUTE\n");
    std::printf ("==============================\n");
    std::printf ("  %d sons reels, etalons AJUSTES (aucune recherche, aucun hasard)\n",
                 retenus);
    std::printf ("  rejets : %d illisibles, %d trop courts, %d sans hauteur\n\n",
                 rejLecture, rejCourt, rejF0);
    std::printf ("  etalon                      perte     cout de la contrainte\n");
    std::printf ("  --------------------------------------------------------------\n");

    double temoin = 0.0;
    std::vector<double> moy ((size_t) nCas, 0.0);
    for (int c = 0; c < nCas; ++c)
    {
        double m = 0.0;
        for (double v : pertes[(size_t) c]) m += v;
        m /= std::max<size_t> (1, pertes[(size_t) c].size());
        moy[c] = m;
        if (c == 0) { temoin = m; std::printf ("  %-24s %7.3f dB\n", cas[c].nom, m); }
        else        std::printf ("  %-24s %7.3f dB      %+6.3f dB   (contre %s)\n",
                                 cas[c].nom, m, m - moy[cas[c].parent],
                                 cas[cas[c].parent].nom);
    }

    // ⭐⭐ LE TEMOIN DECIDE SI LE RESTE EST LISIBLE.
    std::printf ("\n");
    if (temoin > 0.5)
        std::printf ("  /!\\ TEMOIN A %.3f dB, PAS A ZERO : l'analyse/synthese ne\n"
                     "      reconstruit pas le signal. AUCUNE ligne ci-dessus n'est\n"
                     "      exploitable - le banc mesure ses propres manques.\n\n", temoin);
    else
        std::printf ("  TEMOIN a %.3f dB : la chaine reconstruit bien le signal,\n"
                     "  donc les ecarts ci-dessus mesurent BIEN les contraintes.\n\n", temoin);
    return 0;
}
