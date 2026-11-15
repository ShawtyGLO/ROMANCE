// =============================================================================
//  Spectre.h - UN SPECTRE DESSINE QUI FABRIQUE UNE ROM.
//
//  ⭐⭐ CE QU'IL AJOUTE, ET POURQUOI IL EXISTE.
//
//  L'etalon (tools/mes_plafond.cpp) a mesure ce que chaque contrainte du format
//  coute sur de vrais sons. Le resultat qui a decide de ce fichier :
//
//      21 partiels (notre recette) ................. 4,768 dB
//      21 partiels + residu 64 bandes A->B ......... 2,865 dB   -1,90
//      21 partiels + residu 64, enveloppe/bande .... 0,248 dB   -4,52
//
//  Le poste le plus cher du format n'etait ni le nombre de partiels ni les
//  enveloppes : c'etait TOUT CE QUI N'EST PAS HARMONIQUE - le bruit,
//  l'inharmonicite, les transitoires. Notre recette leur consacrait DEUX
//  bandes a decroissance unique.
//
//  /!\ ET CE N'EST PAS UN CINQUIEME MOTEUR. C'est un second MATERIAU : il
//  fabrique une ROM, donc il herite de tout le chemin de lecture - grain,
//  boucle, quantification, filtre de reconstruction. Un moteur de plus aurait
//  demande une colonne supplementaire dans le vote, dans le profil, dans la
//  matrice et dans l'interface, pour un son qui se LIT exactement comme une ROM.
//
//  /!\ CE QUE L'ETALON N'A PAS MESURE : ses phases venaient de la cible. Ici
//  elles sont TIREES. Pour du bruit c'est sans consequence - une bande large a
//  phases aleatoires EST du bruit. Pour une bande etroite, ca donne un partiel
//  dont la phase ne veut rien dire, ce qui est vrai aussi d'un vrai partiel
//  dont on ne connait pas la phase.
//
//  Ne depend que de <cmath>, <complex>, <cstdint> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "Grain.h"     // kPi

namespace romance
{

/** ⭐ SOIXANTE-QUATRE BANDES, ET C'EST UNE MESURE QUI L'A FIXE.

        32 bandes -> 4,048 dB
        64 bandes -> 2,865 dB     (-1,18)
       128 bandes -> 2,765 dB     (-0,10)

    Doubler de 32 a 64 vaut plus d'un decibel ; doubler encore n'en vaut plus
    qu'un dixieme. C'est aussi la resolution de la grille dessinable existante,
    donc le meme geste et la meme largeur a l'ecran. */
constexpr int kBandesSpectre = 64;

/** Le spectre dessine : ce qu'un preset transporte. */
struct Spectre
{
    /** Niveau de chaque bande, 0..255. Axe LOGARITHMIQUE de 20 Hz a 0,45 fs. */
    uint8_t bande[kBandesSpectre] = { 0 };

    /** ⭐ LA PENTE DE DECROISSANCE, au lieu d'une enveloppe par bande.

        L'etalon dit qu'une enveloppe PAR BANDE ferait tomber la perte a
        0,25 dB. Elle couterait 64 enveloppes dans le profil - plus que tout
        le reste du format reuni, pour un preset qui doit rester compact.

        On garde donc UNE enveloppe, plus une PENTE : les bandes hautes
        decroissent plus vite que les basses. Ce n'est pas un compromis
        arbitraire, c'est ce que fait la matiere - un aigu s'eteint avant un
        grave, dans une corde comme dans une piece. Deux octets au lieu de
        deux cent cinquante.

        0 = toutes les bandes ensemble, 1 = la bande la plus haute s'eteint
        huit fois plus vite que la plus basse. */
    double pente = 0.35;

    double attaque  = 0.005;
    double chute    = 0.60;
    double tenue    = 0.10;

    /** ⭐ SUIT LE CLAVIER. Choix de l'utilisateur : « si on crée un son par
        notre dessin de fréquences il doit suivre les notes ». La ROM est donc
        fabriquee pour une note de reference et TRANSPOSEE a la lecture, comme
        n'importe quelle ROM - c'est le chemin qui existe deja. */
    double noteRef = 60.0;

    Spectre() { plat(); }

    /** ⭐ Le spectre par defaut : une pente douce vers l'aigu, pas du silence
        ni du bruit blanc. Choisir « spectre » ne doit ni couper le son ni
        cracher - sinon on croit avoir casse le patch. */
    void plat() noexcept
    {
        for (int i = 0; i < kBandesSpectre; ++i)
        {
            const double u = (double) i / (double) (kBandesSpectre - 1);
            bande[i] = (uint8_t) std::lround (255.0 * std::pow (1.0 - u, 1.5));
        }
    }

    double lit (int i) const noexcept
    {
        return (double) bande[std::max (0, std::min (kBandesSpectre - 1, i))] / 255.0;
    }

    /** La frequence centrale d'une bande, en hertz. */
    static double frequence (int i, double fs) noexcept
    {
        const double fLo = 20.0, fHi = 0.45 * fs;
        const double u = ((double) i + 0.5) / (double) kBandesSpectre;
        return fLo * std::pow (fHi / fLo, u);
    }

    bool operator== (const Spectre& o) const noexcept
    {
        for (int i = 0; i < kBandesSpectre; ++i) if (bande[i] != o.bande[i]) return false;
        return pente == o.pente && attaque == o.attaque
            && chute == o.chute && tenue == o.tenue;
    }
    bool operator!= (const Spectre& o) const noexcept { return ! (*this == o); }
};

/** ⭐⭐ FABRIQUE LA ROM.

    Une bande = un paquet de sinusoides a phases tirees, sur toute sa largeur.
    Une bande large donne donc du BRUIT, une bande etroite un partiel - c'est
    la meme construction, et c'est ce qui permet de decrire les deux.

    /!\ ON SYNTHETISE DANS LE TEMPS, PAS PAR FFT INVERSE. Une FFT inverse
    donnerait un CYCLE qu'il faudrait boucler : la decroissance propre a chaque
    bande - tout l'interet du modele - n'y tiendrait pas. Ici chaque sinusoide
    porte sa propre exponentielle.

    /!\ ET LE NOMBRE DE SINUSOIDES PAR BANDE SUIT SA LARGEUR. Une bande grave
    couvre quelques hertz, une bande aigue plusieurs milliers : leur donner le
    meme nombre de composantes ferait un grave pur et un aigu clairseme - donc
    un sifflement metallique la ou l'on veut du souffle.
*/
inline void rendSpectre (const Spectre& sp, std::vector<double>& rom,
                         double fsRom, int nEch, unsigned int graine = 1u)
{
    rom.assign ((size_t) nEch, 0.0);
    unsigned int e = graine * 2654435761u + 1u;
    auto uni = [&e] () noexcept
    {
        e = e * 1664525u + 1013904223u;
        return (double) (e >> 8) / 16777216.0;
    };

    const double fLo = 20.0, fHi = 0.45 * fsRom;
    const double pente = std::max (0.0, std::min (1.0, sp.pente));

    for (int b = 0; b < kBandesSpectre; ++b)
    {
        const double niveau = sp.lit (b);
        if (niveau <= 1.0 / 255.0) continue;

        // les bornes de la bande, sur l'axe logarithmique
        const double u0 = (double) b / (double) kBandesSpectre;
        const double u1 = (double) (b + 1) / (double) kBandesSpectre;
        const double f0 = fLo * std::pow (fHi / fLo, u0);
        const double f1 = fLo * std::pow (fHi / fLo, u1);
        if (f1 >= 0.5 * fsRom) break;

        // ⭐ Assez de composantes pour que la bande sonne PLEINE : une par
        // tranche de 12 Hz environ, entre 1 et 24.
        const int nComp = std::max (1, std::min (24, (int) ((f1 - f0) / 12.0)));

        // la decroissance de CETTE bande : plus haute, plus rapide
        const double uc = ((double) b + 0.5) / (double) kBandesSpectre;
        const double tau = std::max (0.005, sp.chute * std::pow (8.0, -pente * uc));
        const double dec = 1.0 / (tau * fsRom);
        const double att = 1.0 / (std::max (0.0005, sp.attaque) * fsRom);
        const double tenue = std::max (0.0, std::min (1.0, sp.tenue));

        // /!\ NORMALISATION PAR RACINE DU NOMBRE DE COMPOSANTES. Des phases
        // independantes s'additionnent en PUISSANCE : diviser par nComp
        // rendrait les bandes larges inaudibles a niveau egal, et « niveau »
        // ne voudrait plus dire la meme chose selon la bande - un parametre
        // dont le sens depend d'un autre n'est pas un parametre.
        const double a = niveau * 0.7 / std::sqrt ((double) nComp);

        for (int c = 0; c < nComp; ++c)
        {
            const double f = f0 + (f1 - f0) * ((double) c + uni()) / (double) nComp;
            const double dt = 2.0 * kPi * f / fsRom;
            double ph = uni() * 2.0 * kPi;
            for (int i = 0; i < nEch; ++i)
            {
                const double t = (double) i;
                const double env = (1.0 - std::exp (-t * att))
                                 * (tenue + (1.0 - tenue) * std::exp (-t * dec));
                rom[(size_t) i] += a * env * std::sin (ph);
                ph += dt;
                if (ph > 2.0 * kPi) ph -= 2.0 * kPi;
            }
        }
    }

    // /!\ Normalisation sur la crete, comme `rendRom` : le niveau d'une ROM est
    // un choix de la couche, pas une consequence du dessin.
    double crete = 1e-12;
    for (double v : rom) crete = std::max (crete, std::abs (v));
    if (crete > 1e-9) { const double g = 0.9 / crete; for (auto& v : rom) v *= g; }
}

} // namespace romance
