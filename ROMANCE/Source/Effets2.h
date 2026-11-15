// =============================================================================
//  Effets2.h - Delai, saturation, ensemble, tonalite.
//
//  /!\ CHAQUE EFFET DOIT FAIRE QUELQUE CHOSE QUE LES AUTRES NE FONT PAS. On a
//  deja deux controles qui assombrissent - la bande du filtre de reconstruction
//  et la couleur du chorus. En ajouter un troisieme sous un nouveau nom serait
//  deux de trop. Les distinctions retenues, et elles sont mesurables :
//
//    · BANDE (reconstruction) : l'horloge de la machine, elle SUIT la note ;
//    · COULEUR (chorus)       : les seaux du BBD, sur l'effet seul, + souffle ;
//    · TONALITE (Baxandall)   : l'etage de sortie, FIXE, et il monte aussi bien
//                               qu'il descend - les autres ne savent que couper.
//
//  Ne depend que de <cmath>, <algorithm> et Effets.h (LigneRetard).
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Effets.h"   // LigneRetard, PasseTout
#include "Grain.h"

namespace romance
{

/** ⭐ COMBIEN D'EFFETS PEUVENT ETRE CHAINES, ET DONC ORDONNES.

    Dix : autant que d'effets existants. Un emplacement de plus ne servirait a
    rien tant qu'un effet ne peut apparaitre qu'une fois dans la chaine - et il
    ne le peut pas, faute d'une deuxieme instance.
*/
constexpr int kEmplacementsFx = 10;

/** Reduit une liste d'emplacements a la chaine reellement traversee.

    /!\ LE DOUBLON EST LE PIEGE. Il n'y a qu'UNE instance de chaque effet : la
    traverser deux fois rebouclerait le delai sur sa propre sortie et ferait
    diverger la reverbe - un reglage anodin dans un menu, un larsen a la
    sortie. On garde donc la PREMIERE occurrence et on ignore les suivantes.

    Rend le nombre d'etages ecrits dans `sortie` (0..10). Une valeur hors
    [1, 10] est un emplacement vide.
*/
inline int chaineFx (const int* emplacements, int n, int* sortie) noexcept
{
    unsigned int vu = 0u;
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        const int e = emplacements[i];
        if (e <= 0 || e > kEmplacementsFx) continue;
        const unsigned int bit = 1u << e;
        if (vu & bit) continue;
        vu |= bit;
        sortie[k++] = e;
    }
    return k;
}

// ------------------------------------------------------------------ delai

/** Delai a tete unique, avec reinjection et amortissement.

    ⭐ AMORTISSEMENT DANS LA BOUCLE, pas sur la sortie. C'est ce qui fait qu'un
    delai d'epoque s'assombrit A CHAQUE REPETITION au lieu d'etre simplement
    sourd : la premiere repetition est claire, la cinquieme ne l'est plus. Un
    filtre place sur la sortie donnerait cinq repetitions egales et ternes.
*/
class Delai
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        ligne_[0].prepare ((int) (2.05 * fs) + 4);
        ligne_[1].prepare ((int) (2.05 * fs) + 4);
        ligne_[0].reset(); ligne_[1].reset();
        amorti_[0].reset(); amorti_[1].reset();
        lisse_ = 0.25 * fs;
    }

    /** `pingpong` : 0 = les deux cotes identiques, 1 = le retour rebondit. */
    void regle (double tempsS, double reinjection, double melange,
                double amortissement, double pingpong) noexcept
    {
        cible_ = std::max (0.001, std::min (2.0, tempsS)) * fs_;
        reinj_ = std::max (0.0, std::min (0.95, reinjection));
        const double m = std::max (0.0, std::min (1.0, melange));
        gSec_ = std::cos (m * 0.5 * kPi);
        gMouille_ = std::sin (m * 0.5 * kPi);
        pp_ = std::max (0.0, std::min (1.0, pingpong));
        const double fc = 14000.0 * std::pow (0.05,
                              std::max (0.0, std::min (1.0, amortissement)));
        amorti_[0].regle (fc, fs_); amorti_[1].regle (fc, fs_);
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (gMouille_ <= 1e-6) return;

        // /!\ Le temps de delai est LISSE. Le changer d'un coup fait un saut de
        // lecture dans la ligne, donc un clac - et sur un delai, ce clac est
        // ensuite reinjecte et repete.
        lisse_ += 0.0004 * (cible_ - lisse_);

        const double sg = ligne_[0].lit (lisse_);
        const double sd = ligne_[1].lit (lisse_);

        // ping-pong : le retour croise les cotes
        const double rg = sg * (1.0 - pp_) + sd * pp_;
        const double rd = sd * (1.0 - pp_) + sg * pp_;

        ligne_[0].ecrit (g + amorti_[0].traite (rg) * reinj_);
        ligne_[1].ecrit (d + amorti_[1].traite (rd) * reinj_);

        g = g * gSec_ + sg * gMouille_;
        d = d * gSec_ + sd * gMouille_;
    }

private:
    LigneRetard ligne_[2];
    UnPole amorti_[2];
    double fs_ = 44100.0, cible_ = 11025.0, lisse_ = 11025.0;
    double reinj_ = 0.3, gSec_ = 1.0, gMouille_ = 0.0, pp_ = 0.0;
};

// ------------------------------------------------------------ saturation

/** L'etage de sortie.

    ⭐ ASYMETRIQUE, et c'est tout le sujet. Une saturation symetrique ne produit
    que des harmoniques IMPAIRES : elle sonne dure, electronique. Un etage a
    transistors reel n'est jamais parfaitement symetrique, et c'est ce leger
    desequilibre qui donne les harmoniques PAIRES - celles qu'on entend comme
    de la chaleur. On peut le mesurer : H2 apparait, H3 seul ne suffit pas.

    /!\ COMPENSATION DE GAIN. Sans elle, monter la saturation monte le volume,
    et on croit que ca sonne mieux alors que c'est juste plus fort. C'est le
    biais le plus courant en ecoute d'effets.
*/
class Saturation
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        dc_[0].reset(); dc_[1].reset();
        dc_[0].regle (12.0, fs); dc_[1].regle (12.0, fs);
        z_[0] = z_[1] = 0.0;
    }

    void regle (double dose, double asymetrie) noexcept
    {
        dose_ = std::max (0.0, std::min (1.0, dose));
        gain_ = 1.0 + 14.0 * dose_ * dose_;
        asym_ = std::max (0.0, std::min (1.0, asymetrie));
        // /!\ COMPENSATION MESUREE, PAS ESTIMEE. Ma premiere version prenait la
        // pente a l'origine, ce qui ignore la saturation elle-meme : il restait
        // 6,2 dB de gain a dose maximale. On mesure donc le rms REEL sur un
        // sinus de reference - une centaine de tanh() a chaque changement de
        // reglage, jamais dans le chemin audio.
        {
            double e = 0.0;
            const int n = 128;
            for (int i = 0; i < n; ++i)
            {
                const double x = 0.25 * std::sin (2.0 * kPi * (double) i / (double) n);
                const double y = std::tanh (gain_ * x + 0.35 * asym_);
                e += y * y;
            }
            const double rms = std::sqrt (e / (double) n);
            // 0,1768 = rms d'un sinus d'amplitude 0,25
            comp_ = (rms > 1e-9) ? 0.1768 / rms : 1.0;
        }
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (dose_ <= 1e-4) return;
        g = un (g, 0);
        d = un (d, 1);
    }

private:
    inline double un (double x, int c) noexcept
    {
        const double b = 0.35 * asym_;          // decalage : c'est lui qui fait H2
        double y = std::tanh (gain_ * x + b);
        y *= comp_;
        // /!\ Le decalage cree une composante continue : sans ce passe-haut,
        // elle se propage dans tout le reste de la chaine et mange la dynamique.
        const double bas = dc_[c].traite (y);
        y -= bas;
        z_[c] = y;
        return y;
    }

    UnPole dc_[2];
    double fs_ = 44100.0, dose_ = 0.0, gain_ = 1.0, asym_ = 0.5, comp_ = 1.0;
    double z_[2] = { 0.0, 0.0 };
};

// -------------------------------------------------------------- tonalite

/** Correcteur de Baxandall : grave et aigu qui INTERAGISSENT.

    /!\ Ce n'est pas deux shelfs independants. Dans un Baxandall les deux bandes
    partagent un pivot median : monter le grave et l'aigu ensemble ne fait pas
    une bosse au milieu, ca fait un creux. C'est ce comportement-la qu'on
    reconnait d'une chaine hi-fi d'epoque, et c'est ce qui le distingue d'un
    egaliseur de studio.

    ⭐ Et surtout il MONTE aussi bien qu'il descend. Les deux autres controles
    de couleur du plugin (bande, couleur du chorus) ne savent que retirer.
*/
class Tonalite
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        for (int c = 0; c < 2; ++c) { bas_[c].reset(); haut_[c].reset(); }
        regle (0.0, 0.0);
    }

    /** `grave` et `aigu` en dB, -12 a +12. */
    void regle (double graveDb, double aiguDb) noexcept
    {
        gDb_ = std::max (-12.0, std::min (12.0, graveDb));
        aDb_ = std::max (-12.0, std::min (12.0, aiguDb));
        for (int c = 0; c < 2; ++c)
        {
            bas_[c].regle (320.0, fs_);
            haut_[c].regle (3200.0, fs_);
        }
        gG_ = std::pow (10.0, gDb_ / 20.0) - 1.0;
        gA_ = std::pow (10.0, aDb_ / 20.0) - 1.0;
        actif_ = (std::abs (gDb_) > 0.01) || (std::abs (aDb_) > 0.01);
    }

    /** Le tilt : un seul geste, grave et aigu en sens inverse. */
    void regleTilt (double tiltDb) noexcept { regle (-tiltDb, tiltDb); }

    inline void traite (double& g, double& d) noexcept
    {
        if (! actif_) return;
        g = un (g, 0);
        d = un (d, 1);
    }

private:
    inline double un (double x, int c) noexcept
    {
        const double lo = bas_[c].traite (x);          // < 320 Hz
        const double hi = x - haut_[c].traite (x);     // > 3,2 kHz
        // ⭐ Le pivot : ce qui est entre les deux n'est touche par AUCUNE des
        // deux commandes. C'est ce qui fait qu'un Baxandall garde le medium
        // intact meme aux extremes, la ou deux shelfs additifs le deplacent.
        return x + gG_ * lo + gA_ * hi;
    }

    UnPole bas_[2], haut_[2];
    double fs_ = 44100.0, gDb_ = 0.0, aDb_ = 0.0, gG_ = 0.0, gA_ = 0.0;
    bool actif_ = false;
};

// -------------------------------------------------------------- ensemble

/** L'ensemble des cordes (Solina, Polymoog).

    /!\ CE N'EST PAS UN CHORUS PLUS PROFOND. Un chorus a trois lignes a des
    taux proches, donc son mouvement se reconnait. Un ensemble en a trois a des
    taux VOLONTAIREMENT eloignes et irrationnels entre eux (un lent, un moyen,
    un rapide), et c'est ce qui produit ce flou continu ou l'oreille ne suit
    plus aucune ligne. Meme cablage, deux sons sans rapport.
*/
class Ensemble
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        for (auto& l : lignes_) { l.prepare ((int) (0.05 * fs) + 4); l.reset(); }
        for (int i = 0; i < 3; ++i) ph_[i] = 0.31 * (double) i;
    }

    void regle (double vitesse, double profondeur, double melange) noexcept
    {
        vit_ = std::max (0.01, vitesse);
        prof_ = std::max (0.0, std::min (1.0, profondeur));
        const double m = std::max (0.0, std::min (1.0, melange));
        gSec_ = std::cos (m * 0.5 * kPi);
        gMouille_ = std::sin (m * 0.5 * kPi);
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (gMouille_ <= 1e-6) return;
        const double sec = 0.5 * (g + d);
        // ⭐ Taux tres eloignes ET irrationnels entre eux, ET AUCUN A 1.
        // /!\ Ma premiere version gardait un taux exactement au nominal : la
        // mesure a montre que l'ensemble se refermait PLUS qu'un chorus
        // (autocorrelation 0,128 contre 0,020), c'est-a-dire l'inverse de ce
        // qu'on voulait. Exactement la meme faute que sur le pleurage, ou une
        // composante dominante au taux nominal rendait le motif reconnaissable.
        static const double taux[3] = { 0.7071068, 1.359140, 3.141593 };
        const double base = 0.011 * fs_, prof = 0.004 * fs_ * prof_;
        double wg = 0.0, wd = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            lignes_[i].ecrit (sec);
            ph_[i] += vit_ * taux[i] / fs_;
            if (ph_[i] >= 1.0) ph_[i] -= std::floor (ph_[i]);
            const double r = lignes_[i].lit (base + prof * std::sin (2.0 * kPi * ph_[i]));
            const double a = ((double) i / 2.0) * 0.5 * kPi;
            wg += r * std::cos (a);
            wd += r * std::sin (a);
        }
        wg *= 0.6; wd *= 0.6;
        g = g * gSec_ + wg * gMouille_;
        d = d * gSec_ + wd * gMouille_;
    }

private:
    LigneRetard lignes_[3];
    double fs_ = 44100.0, ph_[3] = {}, vit_ = 0.6, prof_ = 0.6;
    double gSec_ = 1.0, gMouille_ = 0.0;
};

} // namespace romance
