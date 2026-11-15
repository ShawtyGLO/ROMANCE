// =============================================================================
//  Oscillateur.h - Le moteur ANALOGIQUE VIRTUEL.
//
//  ⭐ POURQUOI CE MOTEUR EN PREMIER. Le corpus de l'utilisateur est plein
//  d'Oberheim Matrix 1000, OB-8, Jupiter 8, Juno 60, Prophet T8, Rhodes Chroma.
//  Ce sont des synthes ANALOGIQUES, et on essayait de les reproduire avec
//  24 partiels additifs. Un Juno, c'est une dent de scie, un filtre et une
//  enveloppe : SIX valeurs. En additif il en faut soixante-douze, et le resultat
//  est moins bon.
//  Le format ne saturait pas par manque d'octets - il saturait parce que ce
//  n'etait pas le bon generateur pour ces sons-la.
//
//  /!\ BANDE LIMITEE PAR POLYBLEP, obligatoirement. Une dent de scie naive
//  replie tout son spectre : a 440 Hz elle sonne deja sale, et a 2 kHz elle est
//  inutilisable. Le repliement qu'on veut entendre dans ce plugin vient de la
//  LECTURE d'epoque (drop-sample), pas d'un oscillateur mal fabrique - les deux
//  ne sonnent pas pareil, et confondre les deux serait exactement le genre de
//  raccourci que ce projet refuse.
//
//  /!\ ET LE GRAIN NE S'APPLIQUE PAS ICI. Un Juno n'a ni ROM, ni convertisseur,
//  ni filtre de reconstruction : sa chaine est analogique de bout en bout. Faire
//  passer cet oscillateur par la quantification mu-law serait une decoration,
//  pas une simulation. La couche le documente et l'ignore.
//
//  Ne depend que de <cmath> et <algorithm>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Grain.h"   // kPi

namespace romance
{

enum class FormeOsc { Scie, Impulsion, Triangle, Sinus };

/** Correction PolyBLEP : elle arrondit la discontinuite sur les deux
    echantillons qui l'entourent. C'est le meilleur rapport qualite/cout connu
    pour un oscillateur a bande limitee - et surtout il reste EXACT en phase,
    ce qui compte pour la synchronisation et pour l'unisson. */
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)             { const double x = t / dt - 1.0;        return -x * x; }
    if (t > 1.0 - dt)       { const double x = (t - 1.0) / dt + 1.0; return  x * x; }
    return 0.0;
}

/** Un oscillateur analogique virtuel. */
class Oscillateur
{
public:
    void prepare (double fs) noexcept { fs_ = fs; phase_ = 0.0; }
    void reglePhase (double p) noexcept { phase_ = p - std::floor (p); }

    void majHauteur (double hz) noexcept
    {
        hz_ = std::max (0.01, std::min (hz, 0.49 * fs_));
        dt_ = hz_ / fs_;
    }

    /** `largeur` : rapport cyclique de l'impulsion, 0,05 a 0,95. */
    inline double suivant (FormeOsc f, double largeur) noexcept
    {
        const double t = phase_;
        double v = 0.0;

        switch (f)
        {
            case FormeOsc::Scie:
                v = 2.0 * t - 1.0;
                v -= polyBlep (t, dt_);
                break;

            case FormeOsc::Impulsion:
            {
                const double l = std::max (0.05, std::min (0.95, largeur));
                v = (t < l) ? 1.0 : -1.0;
                v += polyBlep (t, dt_);
                double t2 = t - l;
                if (t2 < 0.0) t2 += 1.0;
                v -= polyBlep (t2, dt_);
                break;
            }

            case FormeOsc::Triangle:
            {
                // ⭐ Le triangle est l'INTEGRALE d'un carre a bande limitee :
                // on l'obtient donc proprement en integrant, pas en dessinant
                // des rampes (qui replieraient exactement comme une scie naive).
                double c = (t < 0.5) ? 1.0 : -1.0;
                c += polyBlep (t, dt_);
                double t2 = t - 0.5;
                if (t2 < 0.0) t2 += 1.0;
                c -= polyBlep (t2, dt_);
                integ_ += 4.0 * dt_ * c;
                // fuite tres lente : sans elle l'integrateur derive vers une
                // composante continue au bout de quelques secondes
                integ_ *= 0.9995;
                v = integ_;
                break;
            }

            case FormeOsc::Sinus:
            default:
                // /!\ La table de sinus (Grain.h) plutot que `std::sin` :
                // cinq fois plus rapide, erreur -130 dB. Sur seize voix de
                // trois oscillateurs d'unisson avec sur-echantillonnage par
                // quatre, ca fait 8,5 millions d'appels par seconde.
                v = sinTour (t);
                break;
        }

        phase_ += dt_;
        if (phase_ >= 1.0) phase_ -= std::floor (phase_);
        return v;
    }

    double phase() const noexcept { return phase_; }

private:
    double fs_ = 44100.0, phase_ = 0.0, dt_ = 0.01, hz_ = 440.0, integ_ = 0.0;
};

/** Ce qu'un etage analogique complet contient : deux oscillateurs, une
    sous-octave et du bruit. C'est la configuration commune au Juno 60 (un DCO
    plus sub plus bruit), a l'OB-8 et au Jupiter 8 (deux VCO). */
struct ReglagesVA
{
    FormeOsc forme1 = FormeOsc::Scie;
    FormeOsc forme2 = FormeOsc::Impulsion;
    double   melange = 0.5;        // 0 = osc1 seul, 1 = osc2 seul
    int      demiTons2 = 0;        // accord de l'osc2, en demi-tons
    double   cents2 = 7.0;         // desaccord fin - c'est lui qui fait l'epaisseur
    double   largeur = 0.5;        // rapport cyclique de l'impulsion
    double   sub = 0.0;            // sous-octave (carre), 0..1
    double   bruit = 0.0;          // 0..1
};

/** L'etage analogique d'une voix. */
class EtageVA
{
public:
    void prepare (double fs) noexcept
    {
        o1_.prepare (fs); o2_.prepare (fs); oSub_.prepare (fs);
        etat_ = 22222u;
    }

    void declenche (const ReglagesVA& r, double hz, double phase0) noexcept
    {
        // /!\ Phase de depart COMMUNE mais decalee : deux oscillateurs qui
        // partent exactement ensemble s'annulent ou se doublent selon la forme,
        // et la note claque differemment a chaque fois selon le desaccord. Les
        // machines a DCO redemarraient leurs oscillateurs ensemble ; celles a
        // VCO, jamais. On prend le milieu : depart cale, leger decalage fixe.
        o1_.reglePhase (phase0);
        o2_.reglePhase (phase0 + 0.25);
        oSub_.reglePhase (phase0);
        majHauteur (r, hz);
    }

    void majHauteur (const ReglagesVA& r, double hz) noexcept
    {
        o1_.majHauteur (hz);
        o2_.majHauteur (hz * std::pow (2.0, (double) r.demiTons2 / 12.0
                                            + r.cents2 / 1200.0));
        oSub_.majHauteur (hz * 0.5);
    }

    inline double suivant (const ReglagesVA& r) noexcept
    {
        const double m = std::max (0.0, std::min (1.0, r.melange));
        double v = o1_.suivant (r.forme1, r.largeur) * (1.0 - m)
                 + o2_.suivant (r.forme2, r.largeur) * m;
        if (r.sub > 0.0)
            v += r.sub * oSub_.suivant (FormeOsc::Impulsion, 0.5) * 0.7;
        if (r.bruit > 0.0)
        {
            etat_ = etat_ * 1664525u + 1013904223u;
            v += r.bruit * (((double) (etat_ >> 8) / 8388608.0) - 1.0) * 0.5;
        }
        // /!\ On borne : sub et bruit peuvent faire depasser 1, et ce qui suit
        // est un filtre resonant qu'il vaut mieux ne pas nourrir n'importe quoi.
        return std::max (-2.0, std::min (2.0, v * 0.7));
    }

    /** La phase de l'oscillateur 1, 0..1 - la tete de lecture de l'interface.
        L'etage n'a pas de phase a lui : il en a trois (osc 1, osc 2, sous-octave)
        et c'est la premiere qui donne le cycle qu'on dessine. */
    double position() const noexcept
    { const double p = o1_.phase(); return p - std::floor (p); }

private:
    Oscillateur o1_, o2_, oSub_;
    unsigned int etat_ = 22222u;
};

} // namespace romance
