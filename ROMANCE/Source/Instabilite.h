// =============================================================================
//  Instabilite.h - Wow, flutter, derive.
//
//  ⭐ LA DISTINCTION EST TOUT LE SUJET :
//
//    · le WOW et le FLUTTER sont PARTAGES par toutes les voix. Une seule
//      horloge maitre : quand elle flanche, tout le clavier flanche ensemble,
//      donc un accord garde son accord.
//
//    · la DERIVE est PROPRE a chaque voix ET a chaque couche. C'est la carte de
//      voix, le condensateur, le trimmer de ce circuit-la.
//
//  ⭐ CORRECTION DE MODELE (retour d'ecoute : « le mouvement est trop repetitif
//  et trop mis en avant ; sur ces machines c'est quelque chose qu'on ressent
//  plus qu'on ne l'entend »). Il avait raison, et pour une raison plus profonde
//  qu'un simple dosage :
//
//    SUR UN ECHANTILLONNEUR, LA DERIVE N'EST PAS DANS LA HAUTEUR.
//    Toutes les voix lisent leur ROM depuis la MEME horloge a quartz - stable a
//    quelques dizaines de ppm, soit moins d'un DIXIEME de cent. Il n'y a
//    physiquement pas de desaccord entre voix a aller chercher.
//    Ce qui differe reellement d'une carte de voix a l'autre, c'est la partie
//    ANALOGIQUE : le filtre et l'ampli. Tolerance des composants 1 % sur une
//    machine neuve, 5 % et plus sur une machine qui a vieilli (condensateurs,
//    trimmers). Cela donne quelques pour cent d'ecart de COUPURE et quelques
//    dixiemes de dB de NIVEAU - jamais des cents de hauteur.
//
//    D'ou : `deriveTimbre` et `deriveNiveau` par defaut, `deriveCents` a une
//    valeur symbolique. Un chorus de desaccord entre voix, c'est un synthe
//    analogique - pas un rompleur.
//
//  /!\ LE WOW ET LE FLUTTER SONT UN TRAIT DE BANDE, PAS DE ROM. Un Emulator II
//  n'a pas de pleurage : son horloge est un quartz. Ils sont donc a ZERO par
//  defaut, et disponibles pour la saveur « bande » (Mellotron, sample repique
//  d'une cassette).
//
//  /!\ ET ILS NE SONT PLUS PERIODIQUES. Un sinus pur a 0,7 Hz se reconnait au
//  bout de trois tours et devient une signature. On somme trois sinus de
//  rapports incommensurables : le motif ne se referme jamais.
//
//  /!\ LA DERIVE NE SE REINITIALISE PAS AU DECLENCHEMENT. C'est un etat de la
//  machine, pas de la note.
//
//  Ne depend que de <cmath> et <algorithm>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Grain.h"   // kPi

namespace romance
{

struct ReglagesInstabilite
{
    bool   actif = true;
    // /!\ `PROPRE` reste une position de plein droit : a `actif = false` la
    // machine est parfaitement stable, et c'est un choix artistique valable.

    // --- trait de BANDE : a zero par defaut (une ROM n'a pas de pleurage)
    double wowProfCents  = 0.0;
    double wowVitesse    = 0.6;    // Hz
    double flutProfCents = 0.0;
    double flutVitesse   = 8.0;    // Hz

    // --- trait de CARTE DE VOIX : ce qui differe reellement d'une voix a
    // l'autre sur un echantillonneur. Valeurs par defaut = machine bien
    // entretenue.
    double deriveCents   = 0.6;    // symbolique : l'horloge est partagee
    double deriveTimbre  = 0.025;  // 2,5 % de coupure - tolerance 1 % cumulee
    double deriveNiveauDb = 0.12;  // un dixieme de dB entre cartes
};

/** L'horloge maitre de la machine. UNE seule instance, partagee. */
class Horlogerie
{
public:
    void prepare (double fsControle) noexcept
    {
        fs_ = std::max (1.0, fsControle);
        for (int i = 0; i < 3; ++i) { phW_[i] = 0.13 * i; phF_[i] = 0.37 * i; }
        bruit_ = 0.0; etat_ = 2463534242u;
    }

    /** Rend l'ecart de hauteur commun, en cents. */
    inline double suivant (const ReglagesInstabilite& r) noexcept
    {
        if (! r.actif) return 0.0;
        if (r.wowProfCents <= 0.0 && r.flutProfCents <= 0.0) return 0.0;

        // ⭐ Trois sinus de rapports IRRATIONNELS : le motif ne se referme
        // jamais, donc l'oreille ne le reconnait pas comme une signature.
        // /!\ AUCUNE COMPOSANTE NE DOIT DOMINER A LA VITESSE NOMINALE. Ma
        // premiere version mettait 0,60 de poids sur le rapport 1,0 : mesuree,
        // l'autocorrelation a la periode nominale valait encore 0,65 - le motif
        // se refermait, donc l'oreille le reconnaissait. Trois poids voisins et
        // une part de derive aleatoire suffisent a le casser.
        static const double rap[3] = { 0.78, 1.4142135, 2.2360679 };  // rien a 1
        static const double poids[3] = { 0.36, 0.31, 0.21 };

        double wow = 0.0, flut = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            phW_[i] += std::max (0.0, r.wowVitesse) * rap[i] / fs_;
            if (phW_[i] >= 1.0) phW_[i] -= std::floor (phW_[i]);
            wow += poids[i] * std::sin (2.0 * kPi * phW_[i]);

            phF_[i] += std::max (0.0, r.flutVitesse) * rap[i] / fs_;
            if (phF_[i] >= 1.0) phF_[i] -= std::floor (phF_[i]);
            flut += poids[i] * std::sin (2.0 * kPi * phF_[i]);
        }

        // le flutter emporte une part de defilement irregulier (« scrape »)
        etat_ = etat_ * 1664525u + 1013904223u;
        const double alea = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
        bruit_ += 0.25 * (alea - bruit_);

        // une part de derive lente : c'est elle qui empeche definitivement le
        // motif de se refermer
        return r.wowProfCents * (wow + 0.12 * bruit_ * 1.6)
             + r.flutProfCents * (0.7 * flut + 0.3 * bruit_ * 1.6);
    }

private:
    double fs_ = 2756.25, phW_[3] = {}, phF_[3] = {}, bruit_ = 0.0;
    unsigned int etat_ = 2463534242u;
};

/** Ce qu'un circuit de voix apporte de personnel. */
struct ApportDerive
{
    double cents = 0.0;      // hauteur (symbolique : l'horloge est partagee)
    double octaves = 0.0;    // coupure du filtre musical
    double dB = 0.0;         // niveau
};

/** La derive d'UN circuit : trois marches aleatoires BORNEES et lentes.
    Bornees, parce qu'une vraie marche aleatoire part a l'infini. */
class Derive
{
public:
    void prepare (double fsControle, unsigned int graine) noexcept
    {
        fs_ = std::max (1.0, fsControle);
        etat_ = graine * 2654435761u + 1u;
        // /!\ NE PAS partir de zero : une machine qu'on allume n'a pas tous ses
        // circuits d'accord en attendant de deriver, ils sont deja chacun un peu
        // a cote. Partir de zero rendait la derive inaudible pendant plusieurs
        // secondes - donc inaudible tout court.
        for (int i = 0; i < 3; ++i)
        {
            etat_ = etat_ * 1664525u + 1013904223u;
            cible_[i] = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
            v_[i] = cible_[i];
            cpt_[i] = 0.25 * (double) i;
        }
    }

    inline ApportDerive suivant (const ReglagesInstabilite& r) noexcept
    {
        ApportDerive a;
        if (! r.actif) { for (auto& v : v_) v = 0.0; return a; }

        // /!\ TRES lent, et chaque axe a sa propre periode : un circuit qui
        // derive ne bat pas la mesure. Des periodes egales redonneraient un
        // motif reconnaissable, ce qu'on vient justement de retirer du wow.
        static const double periode[3] = { 0.055, 0.031, 0.019 };  // par seconde
        for (int i = 0; i < 3; ++i)
        {
            cpt_[i] += periode[i] / fs_;
            if (cpt_[i] >= 1.0)
            {
                cpt_[i] -= std::floor (cpt_[i]);
                etat_ = etat_ * 1664525u + 1013904223u;
                cible_[i] = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
            }
            v_[i] += 0.0015 * (cible_[i] - v_[i]);
        }

        a.cents   = v_[0] * r.deriveCents;
        // 2,5 % de coupure = log2(1,025) octave
        a.octaves = v_[1] * std::log2 (1.0 + std::max (0.0, r.deriveTimbre));
        a.dB      = v_[2] * r.deriveNiveauDb;
        return a;
    }

private:
    double fs_ = 2756.25;
    double v_[3] = {}, cible_[3] = {}, cpt_[3] = {};
    unsigned int etat_ = 1u;
};

} // namespace romance
