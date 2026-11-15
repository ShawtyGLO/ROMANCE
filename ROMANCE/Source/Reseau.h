// =============================================================================
//  Reseau.h - Le reseau qui devine une recette, et son inference.
//
//  ⭐ IL NE REMPLACE PAS LA RECHERCHE, IL REMPLACE L'AMORCE. Aujourd'hui
//  `amorce()` est une heuristique (produit spectral harmonique, enveloppes
//  ajustees a la main). Le reseau predit les 108 octets directement et la
//  recherche (1 + lambda) affine derriere. Toute la machinerie deja verifiee
//  reste en place ; on change le point de depart, pas le trajet.
//
//  ⭐ PERCEPTRON, PAS AUTRE CHOSE. L'entree est deja une representation
//  travaillee - 48 bandes log, trois echelles, plancher calibre - pas un
//  spectrogramme brut. L'essentiel du travail qu'une convolution ferait a ete
//  fait en amont, et il l'a ete par des mecanismes qu'on a MESURES plutot
//  qu'appris. Ajouter des couches convolutives ici, ce serait redecouvrir a
//  l'aveugle ce qu'on sait deja.
//
//  /!\ L'INFERENCE ET L'ENTRAINEMENT PARTAGENT CE FICHIER. Deux implementations
//  qui divergent d'un detail (l'ordre des poids, la normalisation de l'entree,
//  la fonction d'activation), et le reseau entraine ne voit pas ce qu'on lui
//  donne dans le plugin : il echoue sans qu'aucun test ne le dise.
//
//  Format des poids (binaire, petit-boutiste) :
//    "ROMNN\2" (6 o) | nEntree u32 | nC1 u32 | nC2 u32 | nSortie u32
//    puis W1 (nC1 x nEntree), b1, W2, b2, W3, b3   -- tous en float32
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Perte.h"
#include "Profil.h"
#include "Recette.h"

namespace romance
{

constexpr int kReseauC1 = 384;
constexpr int kReseauC2 = 192;

/** ⭐⭐ LE RESEAU NE PREDIT PAS LA HAUTEUR, ET C'EST UNE DECISION MESUREE.
    L'empreinte compte 48 bandes logarithmiques sur 9,6 octaves : cinq bandes
    par octave, donc une resolution de 2,4 DEMI-TONS. Elle a ete calibree pour
    comparer des TIMBRES, pas pour estimer une hauteur - et on ne peut pas lire
    dans une representation une information qu'elle ne contient pas.
    Mesure : l'erreur de hauteur du reseau plafonnait a 340 cents, soit trois
    demi-tons, exactement l'ordre de grandeur de cette resolution.

    Or l'estimateur heuristique, lui, est verifie a 0 cent sur huit cas
    synthetiques (fondamentale forte ET faible, de 55 a 440 Hz), parce qu'il
    travaille sur une FFT de 32 768 points et pas sur des bandes groupees.

    Division du travail retenue :
      · la HAUTEUR vient de l'analyse (produit spectral harmonique + vote
        d'octave par la perte) ;
      · le TIMBRE vient du reseau - c'est precisement ce que l'empreinte encode.
    Demander a chacun ce qu'il sait faire, plutot que tout au plus recent.

    (Ancien commentaire, conserve parce que le piege reste vrai :)
    ON NE DEMANDE PAS AU RESEAU DE PREDIRE UN ENTIER DECOUPE EN OCTETS.
    f0 est rangee sur DEUX octets (bas puis haut, f0 x 16). L'octet BAS est une
    fonction en dents de scie de la hauteur : il fait un tour complet tous les
    16 Hz. C'est litteralement impossible a regresser, et comme il portait le
    poids le plus fort de la perte, le reseau y passait tout son effort :
    620 cents d'erreur apres cinq epoques, soit cinq demi-tons.

    On lui fait donc predire une valeur CONTINUE - le logarithme de la hauteur,
    normalise - et c'est le code qui compose les deux octets ensuite. Meme
    raisonnement pour les ratios multi-f0 : on ne garde que l'octet haut.

    /!\ Cette convention doit etre la meme a l'entrainement et a l'inference. */
constexpr int kSlotF0 = 1;          // emplacement de sortie, desormais inutilise
constexpr double kF0Min = 28.0, kF0Max = 1200.0;

/** ⭐⭐ LA HAUTEUR EST UNE ENTREE, PAS UNE SORTIE.
    Sans elle, le reseau ne peut pas savoir OU SONT LES HARMONIQUES dans les
    48 bandes : la meme empreinte, lue a 80 Hz ou a 400 Hz, ne decrit pas du
    tout les memes partiels. Il devait donc deviner la structure harmonique
    avant de pouvoir en deduire quoi que ce soit - et l'erreur d'amplitude
    plafonnait a 7 dB.
    On lui donne donc la hauteur (deja estimee, et verifiee a 0 cent) comme
    2017e entree. C'est le meme raisonnement que pour la sortie, dans l'autre
    sens : chacun fait ce qu'il sait faire. */
constexpr int kTailleEntreeReseau = kTailleReduite + 2;

/** ⭐ LA DUREE DE LA FENETRE EST AUSSI UNE ENTREE. Meme lecon que pour la
    hauteur : l'empreinte a un nombre FIXE de tranches, donc la meme empreinte
    peut decrire une attaque de 0,2 s etalee sur 1 s ou une de 0,8 s etalee sur
    4 s. Sans connaitre la duree, le reseau ne peut pas convertir des tranches
    en secondes - et toutes les constantes de temps de la recette sont en
    secondes. */
constexpr double kFenMin = 0.75, kFenMax = 4.0;   // secondes
inline double normeDepuisFenetre (double s) noexcept
{
    const double v = std::max (kFenMin, std::min (kFenMax, s));
    return (std::log (v) - std::log (kFenMin))
         / (std::log (kFenMax) - std::log (kFenMin));
}

inline double f0DepuisNorme (double v) noexcept
{
    const double x = std::max (0.0, std::min (1.0, v));
    return std::exp (std::log (kF0Min) + x * (std::log (kF0Max) - std::log (kF0Min)));
}
inline double normeDepuisF0 (double f0) noexcept
{
    const double f = std::max (kF0Min, std::min (kF0Max, f0));
    return (std::log (f) - std::log (kF0Min))
         / (std::log (kF0Max) - std::log (kF0Min));
}

/** Normalisation de l'entree, partagee.

    /!\ Les dB vont de kPlancherPropre a 0. On les ramene dans [-1, +1] : sans
    ca la premiere couche recoit des valeurs autour de -40 et le reseau passe
    ses premieres epoques a apprendre a soustraire une constante. */
inline float normeEntree (float dB) noexcept
{
    const float v = std::max ((float) kPlancherPropre, std::min (0.0f, dB));
    return (v - (float) kPlancherPropre * 0.5f) / ((float) -kPlancherPropre * 0.5f);
}

/** Un perceptron a deux couches cachees. Les poids tiennent dans ~2,3 Mo en
    float, ~600 ko une fois quantifies - largement sous la barre du megaoctet
    qu'on s'etait fixee pour le plugin. */
struct Reseau
{
    int nEntree = kTailleEntreeReseau, nC1 = kReseauC1, nC2 = kReseauC2,
        nSortie = kOctetsProfil;
    std::vector<float> W1, b1, W2, b2, W3, b3;
    bool pret = false;

    void dimensionne()
    {
        W1.assign ((size_t) nC1 * nEntree, 0.0f);  b1.assign ((size_t) nC1, 0.0f);
        W2.assign ((size_t) nC2 * nC1, 0.0f);      b2.assign ((size_t) nC2, 0.0f);
        W3.assign ((size_t) nSortie * nC2, 0.0f);  b3.assign ((size_t) nSortie, 0.0f);
    }

    /** Propagation avant. `entree` : kTailleReduite dB bruts (non normalises).
        `sortie` : nSortie valeurs dans [0, 1] (sigmoide), a multiplier par 255
        pour obtenir les octets. */
    void avant (const float* entree, float* sortie,
                std::vector<float>& h1, std::vector<float>& h2) const
    {
        h1.assign ((size_t) nC1, 0.0f);
        h2.assign ((size_t) nC2, 0.0f);

        for (int j = 0; j < nC1; ++j)
        {
            const float* w = W1.data() + (size_t) j * nEntree;
            float s = b1[(size_t) j];
            // /!\ La derniere entree est la hauteur, deja dans [-1, 1] : elle ne
            // passe PAS par la normalisation des dB.
            // /!\ Les DEUX dernieres entrees (hauteur, duree) sont deja dans
            // [-1, 1] : elles ne passent PAS par la normalisation des dB.
            for (int i = 0; i < kTailleReduite; ++i) s += w[i] * normeEntree (entree[i]);
            for (int i = kTailleReduite; i < nEntree; ++i) s += w[i] * entree[i];
            h1[(size_t) j] = (s > 0.0f) ? s : 0.01f * s;      // fuite
        }
        for (int j = 0; j < nC2; ++j)
        {
            const float* w = W2.data() + (size_t) j * nC1;
            float s = b2[(size_t) j];
            for (int i = 0; i < nC1; ++i) s += w[i] * h1[(size_t) i];
            h2[(size_t) j] = (s > 0.0f) ? s : 0.01f * s;
        }
        for (int j = 0; j < nSortie; ++j)
        {
            const float* w = W3.data() + (size_t) j * nC2;
            float s = b3[(size_t) j];
            for (int i = 0; i < nC2; ++i) s += w[i] * h2[(size_t) i];
            sortie[j] = 1.0f / (1.0f + std::exp (-s));
        }
    }

    // ------------------------------------------------------------- fichiers

    bool ecrit (const std::string& chemin) const
    {
        FILE* f = std::fopen (chemin.c_str(), "wb");
        if (! f) return false;
        std::fwrite ("ROMNN\2", 1, 6, f);
        const uint32_t a = (uint32_t) nEntree, b = (uint32_t) nC1,
                       c = (uint32_t) nC2, d = (uint32_t) nSortie;
        std::fwrite (&a, 4, 1, f); std::fwrite (&b, 4, 1, f);
        std::fwrite (&c, 4, 1, f); std::fwrite (&d, 4, 1, f);
        auto ec = [f] (const std::vector<float>& v)
                  { std::fwrite (v.data(), 4, v.size(), f); };
        ec (W1); ec (b1); ec (W2); ec (b2); ec (W3); ec (b3);
        std::fclose (f);
        return true;
    }

    /** ⭐⭐ POURQUOI LE CHARGEMENT A ECHOUE - et pas seulement qu'il a echoue.

        /!\ UN BOOLEEN NE SUFFIT PAS ICI, ET LE PROJET EN A DEJA PAYE LE PRIX.
        Le piege 40 dit qu'un repli silencieux sur l'heuristique fait lire des
        chiffres en croyant juger le reseau. Or « le fichier n'existe pas » et
        « le fichier existe mais a ete entraine sur un profil de 230 octets
        alors que le format en fait 298 » demandent deux gestes OPPOSES de la
        part de l'utilisateur : aller le chercher, ou le reentrainer. Un « non »
        commun aux deux le laisse chercher au mauvais endroit.

        C'etait exactement le cas le 3 septembre 2026 : `corpus/reseau.rnn`
        existait, faisait 3,8 Mo, et etait refuse pour un ecart de dimension que
        rien n'affichait nulle part. */
    enum class Etat { Absent, PasUnReseau, FormatIncompatible, Tronque, Charge };

    Etat etat = Etat::Absent;
    /** Ce qu'on a lu dans l'en-tete du fichier refuse. Nul si on n'y est pas
        arrive - sert a ecrire « sortie 230, attendu 298 » plutot que « erreur ». */
    int vuEntree = 0, vuSortie = 0;

    /** Un message court, pretable a l'utilisateur tel quel. */
    std::string diagnostic() const
    {
        switch (etat)
        {
            case Etat::Charge:  return "reseau charge";
            case Etat::Absent:  return "aucun reseau (heuristique seule)";
            case Etat::PasUnReseau: return "fichier illisible : ce n'est pas un reseau";
            case Etat::Tronque: return "fichier tronque : poids incomplets";
            case Etat::FormatIncompatible:
                return "reseau perime : sortie " + std::to_string (vuSortie)
                     + " octets, le format en attend " + std::to_string (kOctetsProfil)
                     + " - a reentrainer";
        }
        return "?";
    }

    bool lit (const std::string& chemin)
    {
        pret = false;
        etat = Etat::Absent;
        vuEntree = vuSortie = 0;
        FILE* f = std::fopen (chemin.c_str(), "rb");
        if (! f) return false;
        char sig[6];
        if (std::fread (sig, 1, 6, f) != 6 || std::memcmp (sig, "ROMNN\2", 6) != 0)
        { std::fclose (f); etat = Etat::PasUnReseau; return false; }
        uint32_t a = 0, b = 0, c = 0, d = 0;
        if (std::fread (&a, 4, 1, f) != 1 || std::fread (&b, 4, 1, f) != 1
            || std::fread (&c, 4, 1, f) != 1 || std::fread (&d, 4, 1, f) != 1)
        { std::fclose (f); etat = Etat::PasUnReseau; return false; }
        vuEntree = (int) a; vuSortie = (int) d;
        // /!\ On REFUSE des poids qui ne correspondent pas a l'entree courante.
        // Charger un reseau entraine sur une autre empreinte donnerait des
        // predictions plausibles et fausses - le pire des deux mondes.
        if ((int) a != kTailleEntreeReseau || (int) d != kOctetsProfil)
        { std::fclose (f); etat = Etat::FormatIncompatible; return false; }
        nEntree = (int) a; nC1 = (int) b; nC2 = (int) c; nSortie = (int) d;
        dimensionne();
        auto li = [f] (std::vector<float>& v)
                  { return std::fread (v.data(), 4, v.size(), f) == v.size(); };
        const bool ok = li (W1) && li (b1) && li (W2) && li (b2) && li (W3) && li (b3);
        std::fclose (f);
        pret = ok;
        etat = ok ? Etat::Charge : Etat::Tronque;
        return ok;
    }
};

/** ⭐ D'UN SON VERS UN PROFIL ENTIER, sans recherche.

    Le reseau devinait une RECETTE : il ne savait donc parler que du moteur ROM,
    et depuis que la recherche vote son generateur, il ne concourait plus que
    sur un son sur trois. Il regardait passer les autres sans avoir le droit de
    repondre.

    /!\ IL DEVINE AUSSI LE MOTEUR. C'est le seul octet du profil qui n'est pas
    continu : une sortie de 0,49 et une de 0,51 donnent deux instruments
    differents. On l'accepte parce que le reseau reste un CANDIDAT - c'est la
    perte qui tranche, donc un moteur mal devine ne peut pas faire pire que
    l'heuristique, il peut seulement ne pas aider.

    `f0Impose` vient de l'analyse : le reseau ne devine pas la hauteur.
*/
inline void devineProfil (const Reseau& r, const std::vector<double>& son, double fs,
                          double f0Impose, uint8_t* profil)
{
    std::vector<float> emp ((size_t) kTailleEntreeReseau), h1, h2;
    empreinteReduite (son, fs, emp.data());
    emp[(size_t) kTailleReduite] =
        (float) (2.0 * normeDepuisF0 (f0Impose) - 1.0);
    emp[(size_t) (kTailleReduite + 1)] =
        (float) (2.0 * normeDepuisFenetre ((double) son.size() / fs) - 1.0);
    std::vector<float> s ((size_t) r.nSortie);
    r.avant (emp.data(), s.data(), h1, h2);

    for (int i = 0; i < kOctetsProfil && i < r.nSortie; ++i)
        profil[i] = (uint8_t) std::max (0.0f, std::min (255.0f,
                        std::round (s[(size_t) i] * 255.0f)));

    // ---- ce qui n'est PAS devine, mais impose
    profil[0] = 2;   // la version de la recette

    // f0 : IMPOSEE par l'analyse, jamais devinee
    const double f0 = std::max (kF0Min, std::min (kF0Max, f0Impose));
    const uint16_t q = (uint16_t) std::max (0.0, std::min (65535.0,
                            std::round (f0 * 16.0)));
    profil[1] = (uint8_t) (q & 0xFF); profil[2] = (uint8_t) (q >> 8);

    // /!\ Les octets bas d'un mot de 16 bits sont INDEVINABLES : ils sont une
    // dent de scie de la valeur qu'ils encodent, donc le reseau ne peut que les
    // deviner au hasard. On les annule au lieu de les laisser faire du bruit.
    profil[88] = 0; profil[91] = 0;
}

/** L'ancienne forme, gardee pour les outils qui ne parlent que recette. */
inline Recette devine (const Reseau& r, const std::vector<double>& son, double fs,
                       double f0Impose)
{
    uint8_t o[kOctetsProfil];
    devineProfil (r, son, fs, f0Impose, o);
    return depuisOctets (o);
}

} // namespace romance
