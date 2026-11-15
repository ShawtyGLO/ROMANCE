// =============================================================================
//  Effets.h - Chorus et reverbe.
//
//  /!\ CE NE SONT PAS DES FINITIONS, et c'est mesure : la signature de PAD 1
//  (84 cents de largeur de raie) s'est revelee INATTEIGNABLE sans modulation de
//  hauteur - trois voix desaccordees a 80 cents n'en donnent que 41. Et sur le
//  M1 comme sur le D-50, les effets integres sont indissociables du son.
//
//  Ne depend que de <cmath>, <algorithm> et <vector>. Aucune allocation en
//  dehors de `prepare` : les lignes a retard sont dimensionnees une fois.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Grain.h"   // kPi, Biquad, UnPole

namespace romance
{

/** Ligne a retard a lecture interpolee. */
class LigneRetard
{
public:
    void prepare (int nMax) noexcept
    {
        tampon_.assign ((size_t) std::max (nMax, 4), 0.0);
        ecriture_ = 0;
    }
    void reset() noexcept
    {
        std::fill (tampon_.begin(), tampon_.end(), 0.0);
        ecriture_ = 0;
    }

    inline void ecrit (double x) noexcept
    {
        tampon_[(size_t) ecriture_] = x;
        if (++ecriture_ >= (int) tampon_.size()) ecriture_ = 0;
    }

    inline double lit (double retardEch) const noexcept
    {
        const int n = (int) tampon_.size();
        double pos = (double) ecriture_ - retardEch;
        while (pos < 0.0) pos += (double) n;
        const int i0 = (int) pos;
        const double f = pos - (double) i0;
        const int i1 = (i0 + 1 >= n) ? 0 : i0 + 1;
        return tampon_[(size_t) i0] * (1.0 - f) + tampon_[(size_t) i1] * f;
    }

private:
    std::vector<double> tampon_;
    int ecriture_ = 0;
};

/** Chorus a lignes a retard modulees.

    ⭐ Le parametre `couleur` limite la bande du signal traite : les chorus
    d'epoque etaient a seaux (BBD), donc naturellement sombres et bruites. Un
    chorus parfaitement transparent n'appartient a aucune machine - c'est le
    meme raisonnement que `PROPRE` sur le grain, mais dans l'autre sens.
*/
class Chorus
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        const int nMax = (int) (0.05 * fs) + 4;   // 50 ms
        for (auto& l : lignes_) l.prepare (nMax);
        for (auto& p : phases_) p = 0.0;
        phases_[1] = 0.33; phases_[2] = 0.66;
        for (auto& f : couleurG_) f.reset();
        for (auto& f : couleurD_) f.reset();
    }

    /** ⭐ LA NATURE DE L'EFFET EST UN CHOIX, pas un reglage cache.
        Deux mecanismes REELS et differents :
          · SEAUX (BBD) : bande limitee (16 kHz -> 2,2 kHz) ET souffle. Un
            circuit a seaux bruite, c'est meme sa signature la plus reconnaissable.
          · NUMERIQUE : pleine bande, aucun souffle. C'est le chorus du D-50 ou
            du M1, et il n'a rien a voir avec celui d'un Juno.
        On ne simule pas « un chorus plus ou moins sombre » : ce sont deux
        circuits, et le souffle est ce qui les separe vraiment. */
    void regleNature (bool seaux) noexcept { seaux_ = seaux; }

    void regle (double vitesseHz, double profondeur, double melange,
                double couleur) noexcept
    {
        vitesse_ = std::max (0.01, vitesseHz);
        prof_    = std::max (0.0, std::min (1.0, profondeur));
        melange_ = std::max (0.0, std::min (1.0, melange));
        // couleur 0 = transparent (16 kHz), 1 = seaux d'epoque (2,2 kHz)
        const double c01 = std::max (0.0, std::min (1.0, couleur));
        // en numerique, la couleur ne s'applique pas : le circuit est propre.
        const double fc = seaux_ ? 16000.0 * std::pow (2.2 / 16.0, c01) : 20000.0;
        souffle_ = seaux_ ? 0.00035 * (0.3 + c01) : 0.0;
        for (int i = 0; i < 3; ++i)
        {
            couleurG_[i].regle (fc, fs_);
            couleurD_[i].regle (fc, fs_);
        }
    }

    inline void traite (double& g, double& d) noexcept
    {
        // /!\ SORTIE ANTICIPEE, comme tous les autres effets. Le chorus etait
        // le seul a ne pas en avoir : ses trois lignes a retard modulees
        // tournaient meme a melange nul. Gratuit a corriger, et c'est
        // exactement le genre d'oubli qu'on ne voit pas parce que le defaut du
        // parametre est non nul.
        if (melange_ <= 1e-6) return;

        const double sec = 0.5 * (g + d);
        double wg = 0.0, wd = 0.0;
        const double baseEch = 0.012 * fs_;       // 12 ms
        const double profEch = 0.006 * fs_ * prof_;
        for (int i = 0; i < 3; ++i)
        {
            lignes_[i].ecrit (sec);
            phases_[i] += vitesse_ * (1.0 + 0.17 * (double) i) / fs_;
            if (phases_[i] >= 1.0) phases_[i] -= std::floor (phases_[i]);
            const double m = std::sin (2.0 * kPi * phases_[i]);
            const double r = lignes_[i].lit (baseEch + profEch * m);
            // les trois lignes sont reparties dans l'image
            const double pos = (double) i / 2.0;            // 0, 0.5, 1
            const double ang = pos * 0.5 * kPi;
            wg += couleurG_[i].traite (r) * std::cos (ang);
            wd += couleurD_[i].traite (r) * std::sin (ang);
        }
        wg *= 0.66; wd *= 0.66;
        if (souffle_ > 0.0)
        {
            // le souffle d'un circuit a seaux : present, faible, et constant
            etat_ = etat_ * 1664525u + 1013904223u;
            const double n1 = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
            etat_ = etat_ * 1664525u + 1013904223u;
            const double n2 = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
            wg += souffle_ * n1; wd += souffle_ * n2;
        }
        g = g * (1.0 - melange_) + wg * melange_;
        d = d * (1.0 - melange_) + wd * melange_;
    }

private:
    double fs_ = 44100.0, vitesse_ = 0.4, prof_ = 0.5, melange_ = 0.0;
    LigneRetard lignes_[3];
    double phases_[3] = { 0.0, 0.33, 0.66 };
    UnPole couleurG_[3], couleurD_[3];
    bool seaux_ = true;
    double souffle_ = 0.0;
    unsigned int etat_ = 22695477u;
};

/** Filtre passe-tout de diffusion. Ce qui separe un reseau de retards d'un
    peigne metallique : sans diffusion d'entree, les quatre lignes s'entendent
    UNE PAR UNE.
*/
class PasseTout
{
public:
    void prepare (int n, double gain) noexcept
    {
        n_ = std::max (2, n); g_ = gain;
        ligne_.prepare (n_ + 4);
        ligne_.reset();
    }
    inline double traite (double x) noexcept
    {
        const double d = ligne_.lit ((double) n_);
        const double v = x + g_ * d;
        ligne_.ecrit (v);
        return d - g_ * v;
    }
private:
    LigneRetard ligne_;
    int n_ = 100;
    double g_ = 0.6;
};

/** Reverbe a reseau de retards boucles.

    /!\ TROIS CORRECTIONS APRES ECOUTE - « elle sonne metallique » et « elle
    assombrit enormement, comme si ca diminuait le volume » :

    1. DIFFUSION D'ENTREE. Quatre lignes seules, ce sont quatre echos qu'on
       compte a l'oreille. Quatre passe-tout en cascade avant le reseau les
       etalent en une queue continue. C'est LA difference entre un reseau de
       retards et un peigne.

    2. L'AMORTISSEMENT NE DOIT PAS SORTIR. Il etait dans la boucle ET sur la
       sortie : le son direct de la reverbe arrivait deja filtre a 3,3 kHz. On
       prend maintenant la sortie AVANT l'amortissement - il ne sert plus qu'a
       faire mourir la queue, ce qui est son role.

    3. MELANGE A PUISSANCE CONSTANTE. `sec x (1 - m) + mouille x m` fait perdre
       2,2 dB a m = 0,22 : on entend « moins fort » avant d'entendre « plus de
       reverbe ». En loi de puissance, la meme position ne coute que 0,5 dB.
*/
class Reverbe
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        // longueurs premieres entre elles, pour eviter les resonances communes
        const double ms[4] = { 23.7, 31.1, 41.3, 53.9 };
        for (int i = 0; i < 4; ++i)
        {
            base_[i] = ms[i] * 0.001 * fs;
            lignes_[i].prepare ((int) (base_[i] * 2.2) + 4);
            lignes_[i].reset();
            amorti_[i].reset();
        }
        // diffuseurs : courts, longueurs premieres entre elles
        const double msD[4] = { 4.77, 3.59, 12.73, 9.31 };
        const double gD[4]  = { 0.62, 0.60, 0.58, 0.55 };
        for (int i = 0; i < 4; ++i)
            diff_[i].prepare ((int) (msD[i] * 0.001 * fs), gD[i]);
        pre_.prepare ((int) (0.12 * fs) + 4);
        pre_.reset();
        regle (0.5, 0.4, 0.0, 0.02);
    }

    void regle (double taille, double amortissement, double melange,
                double predelaiS) noexcept
    {
        taille_ = 0.55 + 0.45 * std::max (0.0, std::min (1.0, taille));
        retour_ = 0.60 + 0.38 * std::max (0.0, std::min (1.0, taille));
        const double m = std::max (0.0, std::min (1.0, melange));
        // loi de puissance : a melange nul on garde 100 % du sec, et le passage
        // ne creuse pas le niveau au milieu de la course.
        gSec_    = std::cos (m * 0.5 * kPi);
        gMouille_ = std::sin (m * 0.5 * kPi);
        pred_ = std::max (0.0, predelaiS) * fs_;
        const double fc = 12000.0 * std::pow (0.06, std::max (0.0, std::min (1.0, amortissement)));
        for (auto& a : amorti_) a.regle (fc, fs_);
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (gMouille_ <= 1e-6) return;      // rien a faire, et rien a payer

        const double entree = 0.5 * (g + d);
        pre_.ecrit (entree);
        double x = pre_.lit (std::max (1.0, pred_));
        for (auto& df : diff_) x = df.traite (x);

        double v[4];
        for (int i = 0; i < 4; ++i) v[i] = lignes_[i].lit (base_[i] * taille_);

        // melange de Hadamard : diffuse sans colorer
        const double a = v[0] + v[1], b = v[2] + v[3];
        const double c = v[0] - v[1], e = v[2] - v[3];
        double m[4] = { a + b, c + e, a - b, c - e };
        for (int i = 0; i < 4; ++i)
        {
            // l'amortissement vit DANS la boucle : il fait mourir la queue,
            // il n'a pas a assombrir ce qu'on entend tout de suite.
            const double y = amorti_[i].traite (m[i] * 0.5 * retour_) + x * 0.35;
            lignes_[i].ecrit (y);
        }

        const double wg = 0.6 * (v[0] + v[2]);
        const double wd = 0.6 * (v[1] + v[3]);
        g = g * gSec_ + wg * gMouille_;
        d = d * gSec_ + wd * gMouille_;
    }

private:
    double fs_ = 44100.0, taille_ = 1.0, retour_ = 0.8, pred_ = 0.0;
    double gSec_ = 1.0, gMouille_ = 0.0;
    double base_[4] = { 1000, 1300, 1700, 2300 };
    LigneRetard lignes_[4], pre_;
    PasseTout diff_[4];
    UnPole amorti_[4];
};

} // namespace romance
