// =============================================================================
//  Dessin.h - LA GRILLE DESSINABLE.
//
//  ⭐⭐ CE QU'ELLE EST, ET CE QU'ELLE N'EST PAS.
//
//  Elle vient du Fors Junior : on clique des carres, et le cycle qui en sort
//  est l'oscillateur. Trois resolutions se melangent dans ce geste, et les
//  confondre donne soit un mensonge, soit une porte qui claque :
//
//    - LA CELLULE n'a AUCUNE quantification. C'est un carre a cliquer, une
//      aide de visee. Poser une cellule a mi-hauteur ecrit le milieu exact,
//      pas « le huitieme niveau sur seize ».
//
//    - LA VALEUR STOCKEE est TOUJOURS sur huit bits. C'est ce qui rend la
//      profondeur non destructive : descendre a quatre bits puis remonter rend
//      le dessin intact. Une profondeur qui detruirait le dessin serait un
//      reglage qu'on n'ose plus toucher.
//
//    - LA QUANTIFICATION EST REELLE, appliquee AU RENDU. Si le « 4 bits » ne
//      faisait que dessiner un escalier sans quantifier le signal, le grain
//      qu'on entend serait un mensonge - et tout le reste du plugin est bati
//      sur le contraire.
//
//  /!\ ET ELLE EST LIMITEE EN BANDE. Un escalier contient des harmoniques
//  jusqu'a l'infini : joue tel quel, il replie salement. On analyse donc le
//  cycle UNE FOIS en harmoniques, puis on le resynthetise par octave en
//  s'arretant a Nyquist - exactement le traitement des tables procedurales.
//  Les harmoniques de l'escalier sont VRAIES (elles viennent du signal
//  quantifie), elles sont seulement coupees la ou elles replieraient.
//
//  Ne depend que de <cmath>, <algorithm>, <cstdint> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Grain.h"   // kPi, sinTour

namespace romance
{

constexpr int kCellulesMax = 64;
constexpr int kCellulesMin = 8;
constexpr int kBitsMin = 3;
constexpr int kBitsMax = 8;

/** Le dessin lui-meme : ce qu'un preset transporte. 64 octets et deux entiers. */
struct Dessin
{
    uint8_t cellule[kCellulesMax] = { 0 };
    int nCellules = 32;    // resolution HORIZONTALE - 8 a 64
    int bits = 8;          // resolution VERTICALE au rendu - 3 a 8

    Dessin() { sinus(); }

    /** ⭐ Le dessin par defaut est un SINUS, pas une dent de scie ni du
        silence. Basculer sur la grille ne doit pas changer le son avant qu'on
        ait dessine quoi que ce soit - sinon on croit avoir casse le patch. */
    void sinus() noexcept
    {
        for (int i = 0; i < kCellulesMax; ++i)
        {
            const double u = ((double) i + 0.5) / (double) kCellulesMax;
            pose (i, std::sin (2.0 * kPi * u), kCellulesMax);
        }
    }

    /** Ecrit la cellule `i` (grille de `n` cellules) a l'amplitude `a` (-1..1).

        /!\ La grille affichee a `n` cellules mais le stockage en a toujours 64 :
        on ecrit donc le PAVE de cellules que le clic recouvre. Sans ca, passer
        de 16 a 64 cellules montrerait un dessin troue. */
    void pose (int i, double a, int n) noexcept
    {
        n = std::max (kCellulesMin, std::min (kCellulesMax, n));
        i = std::max (0, std::min (n - 1, i));
        const double v = std::max (-1.0, std::min (1.0, a));
        const uint8_t o = (uint8_t) std::lround ((v * 0.5 + 0.5) * 255.0);
        const int pas = kCellulesMax / n;
        for (int k = 0; k < pas; ++k) cellule[i * pas + k] = o;
    }

    /** L'amplitude STOCKEE de la cellule `i` d'une grille de `n` cellules. */
    double lit (int i, int n) const noexcept
    {
        n = std::max (kCellulesMin, std::min (kCellulesMax, n));
        i = std::max (0, std::min (n - 1, i));
        const int pas = kCellulesMax / n;
        return (double) cellule[i * pas] / 255.0 * 2.0 - 1.0;
    }

    /** ⭐⭐ LA QUANTIFICATION, LA VRAIE.

        /!\ SA GRILLE DOIT ETRE CELLE DU STOCKAGE, decalage compris. Ecrite
        « naturellement » - `round (a / pas) * pas`, des multiples de pas
        autour de zero - elle rate la grille du stockage d'une demi-marche :
        celle-ci part de -1 et monte par pas de 2/255, et -1 n'est pas un
        multiple de 2/255. Resultat mesure : a HUIT bits, la quantification
        deplacait encore chaque valeur de 1/255. Le controle « huit bits ne
        quantifie rien » l'a attrape.

        On quantifie donc en partant de -1, comme le stockage. A huit bits
        c'est exactement l'identite - c'est ce qui rend la profondeur
        reversible.

        Consequence assumee : la grille ne contient pas le zero exact (256
        niveaux ne peuvent pas etre symetriques ET inclure les deux bouts).
        C'est le comportement d'un vrai convertisseur, et la composante
        continue qui en decoule ne survit pas : l'analyse harmonique part du
        rang 1, donc le rang 0 n'est jamais resynthetise. */
    static double quantifie (double a, int bits) noexcept
    {
        bits = std::max (kBitsMin, std::min (kBitsMax, bits));
        const int niveaux = 1 << bits;
        const double pas = 2.0 / (double) (niveaux - 1);
        const double q = std::round ((a + 1.0) / pas) * pas - 1.0;
        return std::max (-1.0, std::min (1.0, q));
    }

    /** La valeur telle qu'on l'ENTEND : stockee en huit bits, quantifiee au
        rendu. A huit bits c'est l'identite - le pas vaut 2/255, exactement le
        pas du stockage. */
    double rendu (int i, int n) const noexcept { return quantifie (lit (i, n), bits); }

    /** ⭐⭐ LES FORMES DE DEPART - dessiner n'est pas toujours le bon geste.

        Demande de l'utilisateur : « pouvoir créer nos waveshapes sans passer
        par le dessin, en choisissant la forme de départ et celle d'arrivée ».

        C'est ce qui donne enfin son sens au morph A vers B : dessiner DEUX
        formes cohérentes a la souris est un travail ; dessiner l'une et poser
        l'autre depuis une forme connue est un geste. On peut alors faire
        passer un dessin vers une dent de scie, ou l'inverse.

        /!\ ELLES SONT ECRITES DANS LES CELLULES, pas gardees comme un
        « type ». Une forme posee redevient donc un dessin qu'on peut
        retoucher - sinon choisir « carre » figerait la case et il faudrait
        tout effacer pour y toucher. */
    enum class Forme { Sinus, Triangle, Scie, Carre, Impulsion,
                       Impaires, DeuxTiers, Cloche };

    static const char* nomForme (Forme f) noexcept
    {
        switch (f)
        {
            case Forme::Sinus:     return "Sinus";
            case Forme::Triangle:  return "Triangle";
            case Forme::Scie:      return "Dent de scie";
            case Forme::Carre:     return "Carre";
            case Forme::Impulsion: return "Impulsion";
            case Forme::Impaires:  return "Harmoniques impaires";
            case Forme::DeuxTiers: return "Formant (2/3)";
            default:               return "Cloche";
        }
    }

    void pose (Forme f) noexcept
    {
        for (int i = 0; i < kCellulesMax; ++i)
        {
            const double u = ((double) i + 0.5) / (double) kCellulesMax;
            double v = 0.0;
            switch (f)
            {
                case Forme::Sinus:     v = std::sin (2.0 * kPi * u); break;
                case Forme::Triangle:  v = 4.0 * std::abs (u - 0.5) - 1.0; break;
                case Forme::Scie:      v = 2.0 * u - 1.0; break;
                case Forme::Carre:     v = (u < 0.5) ? 1.0 : -1.0; break;
                case Forme::Impulsion: v = (u < 0.12) ? 1.0 : -0.15; break;
                case Forme::Impaires:
                    // sinus + tiers + cinquieme : le carre adouci d'un Juno
                    v = std::sin (2.0 * kPi * u)
                      + std::sin (6.0 * kPi * u) / 3.0
                      + std::sin (10.0 * kPi * u) / 5.0;
                    break;
                case Forme::DeuxTiers:
                    // une bosse de formant : deux harmoniques voisines fortes
                    v = std::sin (2.0 * kPi * u) * 0.5
                      + std::sin (8.0 * kPi * u) * 0.8
                      + std::sin (10.0 * kPi * u) * 0.6;
                    break;
                default:
                    // cloche : des rangs inharmoniques, comme un metal frappe
                    v = std::sin (2.0 * kPi * u)
                      + std::sin (2.0 * kPi * u * 2.76) * 0.6
                      + std::sin (2.0 * kPi * u * 5.40) * 0.35;
                    break;
            }
            v = std::max (-1.0, std::min (1.0, v));
            cellule[i] = (uint8_t) std::lround ((v * 0.5 + 0.5) * 255.0);
        }
        // /!\ On normalise sur la crete : les formes composees depassent 1 et
        // seraient ecretees, ce qui les rendrait toutes carrees.
        int mini = 255, maxi = 0;
        for (int i = 0; i < kCellulesMax; ++i)
        { mini = std::min (mini, (int) cellule[i]); maxi = std::max (maxi, (int) cellule[i]); }
        if (maxi > mini)
            for (int i = 0; i < kCellulesMax; ++i)
                cellule[i] = (uint8_t) std::lround (
                    255.0 * (double) (cellule[i] - mini) / (double) (maxi - mini));
    }

    bool operator== (const Dessin& o) const noexcept
    {
        if (nCellules != o.nCellules || bits != o.bits) return false;
        for (int i = 0; i < kCellulesMax; ++i)
            if (cellule[i] != o.cellule[i]) return false;
        return true;
    }
    bool operator!= (const Dessin& o) const noexcept { return ! (*this == o); }
};

/** ⭐⭐ DEUX DESSINS, A ET B - et « position » morphe de l'un vers l'autre.

    /!\ SANS B, LE BOUTON « POSITION » NE SERT A RIEN, et l'utilisateur l'a vu
    avant moi : « notre WT custom a une position evolutive mais on ne decide pas
    de la shape de destination, ce qui rend le bouton position un peu inutile ».

    Il avait raison, et le defaut etait dans ma conception : les seize images
    d'une table dessinee etaient le MEME dessin filtre de plus en plus bas.
    « Position » etait donc un adoucisseur deguise, pas un balayage - or c'est
    le balayage qui fait une table d'ondes.

    Avec deux dessins, position 0 rend A, position 1 rend B, et entre les deux
    on entend passer l'un dans l'autre. C'est ce que fait un PPG.
*/
struct DessinAB
{
    Dessin a, b;
    /** Vrai si B a ete dessine. Sinon on retombe sur l'ancien comportement -
        A filtre progressivement - pour ne pas casser les patchs existants. */
    bool bActif = false;

    bool operator== (const DessinAB& o) const noexcept
    { return a == o.a && b == o.b && bActif == o.bActif; }
    bool operator!= (const DessinAB& o) const noexcept { return ! (*this == o); }
};

// ------------------------------------------------------------- l'analyse

constexpr int kHarmDessin = 256;      // kLongueurImage / 2

/** Le spectre d'un cycle dessine.

    /!\ ON ANALYSE UNE FOIS, ON RESYNTHETISE DOUZE FOIS. Les amplitudes des
    harmoniques ne dependent PAS de la note ; seul le rang ou l'on s'arrete en
    depend. Refaire l'analyse par octave serait douze fois le travail pour douze
    fois le meme resultat.
*/
struct SpectreDessin
{
    double ac[kHarmDessin + 1] = { 0.0 };   // cosinus
    double as[kHarmDessin + 1] = { 0.0 };   // sinus
    int kMax = 0;                           // dernier rang non negligeable

    /** `kUtile` : rang le plus haut qu'on prendra la peine de calculer.

        ⭐ La recherche appelle ceci des milliers de fois, et pour UNE note.
        A 220 Hz sur 22 050 Hz d'echantillonnage, il n'y a que 45 harmoniques
        sous Nyquist : en calculer 256 revient a jeter 82 % du travail.

        /!\ Ce n'est PAS une approximation qui ferait diverger la recherche du
        plugin. Les rangs qu'on ne calcule pas sont exactement ceux que la
        resynthese jetterait ensuite - le resultat est identique au bit pres,
        il arrive seulement plus vite. */
    void analyse (const Dessin& d, int kUtile = kHarmDessin) noexcept
    {
        constexpr int N = 1024;   // points du cycle analyse
        const int n = std::max (kCellulesMin, std::min (kCellulesMax, d.nCellules));

        // le cycle, en marches d'escalier : c'est le maintien d'ordre zero
        // d'un convertisseur, et ses harmoniques sont celles qu'on veut
        double x[N];
        for (int i = 0; i < N; ++i)
            x[i] = d.rendu ((i * n) / N, n);

        for (auto& v : ac) v = 0.0;
        for (auto& v : as) v = 0.0;
        kMax = 0;
        const int kFin = std::max (1, std::min (kHarmDessin, kUtile));
        for (int k = 1; k <= kFin; ++k)
        {
            double c = 0.0, s = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const double ph = (double) (k * i % N) / (double) N;
                c += x[i] * sinTour (ph + 0.25);
                s += x[i] * sinTour (ph);
            }
            ac[k] = 2.0 * c / (double) N;
            as[k] = 2.0 * s / (double) N;
            if (std::abs (ac[k]) + std::abs (as[k]) > 1e-4) kMax = k;
        }
    }
};

} // namespace romance
