// =============================================================================
//  Effets3.h - Les quatre effets choisis avec l'utilisateur.
//
//  PHASER, COMPRESSEUR/LIMITEUR, MULTI-FILTRE, TRANSFERT DE CONVERTISSEUR.
//
//  /!\ LE BITCRUSHER A ETE ECARTE, ET C'EST UNE DECISION, PAS UN OUBLI.
//  L'utilisateur : « le grain est deja partout ». Il a raison, et la raison est
//  structurelle : le grain est le MATERIAU de l'epoque, il vit par couche, il
//  est cherche par Genopatch. Un crusher d'effet en serait un doublon plus
//  pauvre, et deux boutons qui font presque la meme chose rendent les deux
//  illisibles.
//
//  ⭐ LE TRANSFERT DE CONVERTISSEUR est le seul effet de ce fichier qui
//  n'existe sur aucune machine, et c'est voulu. Sur un Emulator II le
//  convertisseur est EN AMONT : il ne peut pas voir la reverbe, parce que la
//  reverbe est ailleurs, dans un rack. Ici tout passe par le meme plugin - donc
//  on peut faire passer le mix entier a travers l'horloge et le filtre de
//  reconstruction. C'est de la triche historique assumee : la machine ne
//  pouvait pas, l'instrument peut.
//
//  /!\ Il est un EFFET DE LA CHAINE, pas une norme. Le rendre systematique en
//  ferait un doublon du grain de couche, et surtout il deviendrait impossible
//  de le placer : avant ou apres la reverbe, ce sont deux sons, et c'est
//  precisement le choix qu'on vient de rendre possible avec la chaine
//  ordonnable.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Effets.h"
#include "Effets2.h"
#include "Grain.h"

namespace romance
{

// ------------------------------------------------------------------- phaser

/** Une cellule passe-tout du premier ordre, a coefficient variable.

    /!\ CE N'EST PAS LE `PasseTout` DE LA REVERBE. Celui-la est un passe-tout a
    RETARD (une ligne a retard rebouclee), qui sert a diffuser. Celui-ci est un
    passe-tout a un pole, qui sert a DEPHASER : son retard de groupe depend de
    la frequence, et c'est ce qui creuse les encoches quand on le somme au sec.
    Meme nom en traitement du signal, deux objets sans rapport.
*/
class CelluleAllpass
{
public:
    void reset() noexcept { z_ = 0.0; }

    /** `a` dans (-1, 1) : le coefficient qui place l'encoche. */
    inline double traite (double x, double a) noexcept
    {
        const double y = a * x + z_;
        z_ = x - a * y;
        return y;
    }

private:
    double z_ = 0.0;
};

/** ⭐ LE PHASER - ce que le chorus ne sait pas faire.

    Le chorus et l'ensemble DESACCORDENT : ils empilent des copies legerement
    detonnees. Le phaser CREUSE : il somme le signal a une version dephasee, ce
    qui annule certaines frequences et en laisse passer d'autres. C'est le seul
    mouvement spectral du plugin qui ne vienne pas d'une hauteur.

    Small Stone, Phase 90, le phaser du Prophet-5 : quatre a huit cellules, un
    LFO lent, et une reinjection qui creuse davantage.

    /!\ NOMBRE DE CELLULES PAIR. Chaque cellule passe-tout du premier ordre
    apporte au plus 180 degres ; il en faut deux par encoche. Un nombre impair
    donne une derniere cellule qui ne cree aucune encoche et se contente de
    decaler la phase globale - donc du calcul pour rien.
*/
class Phaser
{
public:
    static constexpr int kCellulesMax = 8;

    void prepare (double fs) noexcept
    {
        fs_ = std::max (8000.0, fs);
        for (int c = 0; c < 2; ++c)
        {
            for (auto& a : cellules_[c]) a.reset();
            retour_[c] = 0.0;
        }
        phase_ = 0.0;
    }

    /** @param vitesse   Hz
        @param profondeur 0..1 - l'amplitude du balayage
        @param melange   0..1
        @param reinjection 0..1 - creuse les encoches
        @param cellules  2..8, arrondi au pair inferieur */
    void regle (double vitesse, double profondeur, double melange,
                double reinjection, int cellules) noexcept
    {
        vitesse_ = std::max (0.01, std::min (10.0, vitesse));
        profondeur_ = std::max (0.0, std::min (1.0, profondeur));
        melange_ = std::max (0.0, std::min (1.0, melange));
        // /!\ Plafonnee a 0,92 : a 1 la boucle de reinjection oscille toute
        // seule. C'est la meme borne, pour la meme raison, que la reinjection
        // du delai - une boucle qui ne s'eteint pas n'est pas un effet.
        retourDose_ = std::max (0.0, std::min (0.92, reinjection));
        nCellules_ = std::max (2, std::min (kCellulesMax, (cellules / 2) * 2));
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (melange_ <= 1e-6) return;

        phase_ += vitesse_ / fs_;
        if (phase_ >= 1.0) phase_ -= std::floor (phase_);

        // ⭐ LE BALAYAGE EST LOGARITHMIQUE EN FREQUENCE. Un balayage lineaire
        // passe l'essentiel de son temps dans l'aigu, ou l'oreille ne suit
        // pas : le phaser sonne alors comme un filtre qui vibre au lieu de
        // respirer.
        const double lfo = 0.5 - 0.5 * std::cos (2.0 * kPi * phase_);
        const double fMin = 200.0, fMax = 200.0 + 1600.0 * (1.0 + 3.0 * profondeur_);
        const double f = fMin * std::pow (fMax / fMin, lfo * profondeur_);

        // coefficient du passe-tout pour une encoche a f
        const double t = std::tan (kPi * std::min (0.45, f / fs_));
        const double a = (t - 1.0) / (t + 1.0);

        double* voies[2] = { &g, &d };
        for (int c = 0; c < 2; ++c)
        {
            const double sec = *voies[c];
            double x = sec + retour_[c] * retourDose_;
            for (int i = 0; i < nCellules_; ++i) x = cellules_[c][(size_t) i].traite (x, a);
            retour_[c] = x;
            *voies[c] = sec * (1.0 - melange_ * 0.5) + x * melange_ * 0.5;
        }
    }

private:
    double fs_ = 44100.0, phase_ = 0.0;
    double vitesse_ = 0.4, profondeur_ = 0.7, melange_ = 0.0, retourDose_ = 0.4;
    int nCellules_ = 4;
    CelluleAllpass cellules_[2][kCellulesMax];
    double retour_[2] = { 0.0, 0.0 };
};

// ------------------------------------------------- compresseur / limiteur

/** ⭐ L'ETAGE DE SORTIE - et sa vraie raison d'etre dans CE plugin.

    Toutes ces machines en avaient un. Mais l'argument decisif est ailleurs :
    Genopatch produit des profils dont les niveaux sont tres inegaux d'une
    branche a l'autre. Sans un etage qui les egalise, comparer deux vignettes a
    l'oreille revient a comparer deux volumes - et l'utilisateur choisirait la
    plus forte en croyant choisir la plus juste.

    /!\ DETECTION SUR LA CRETE STEREO COMMUNE, pas voie par voie. Compresser
    les deux voies separement fait bouger l'image stereo au rythme du signal :
    une note a gauche recentre le mix. C'est l'erreur classique, et elle ne
    s'entend qu'au casque.
*/
class Compresseur
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = std::max (8000.0, fs);
        env_ = 0.0;
        gain_ = 1.0;
    }

    /** @param seuilDb   -40..0
        @param taux      1..20 (20 = limiteur)
        @param attaqueMs 0,1..100
        @param relacheMs 10..1000
        @param compense  0..1 - rattrape le gain perdu */
    void regle (double seuilDb, double taux, double attaqueMs, double relacheMs,
                double compense) noexcept
    {
        seuil_ = std::pow (10.0, std::max (-60.0, std::min (0.0, seuilDb)) / 20.0);
        taux_ = std::max (1.0, std::min (20.0, taux));
        aAtt_ = std::exp (-1.0 / (std::max (0.05, attaqueMs) * 0.001 * fs_));
        aRel_ = std::exp (-1.0 / (std::max (1.0, relacheMs) * 0.001 * fs_));
        actif_ = (seuilDb < -0.05) && (taux_ > 1.0001);

        // /!\ LA COMPENSATION EST CALCULEE, PAS DEVINEE. Le gain qu'on perd sur
        // un signal qui touche 0 dBFS vaut exactement ce que la courbe rend a
        // 0 dBFS. Le mettre a la main, c'est se retrouver avec un compresseur
        // qui monte le volume quand on l'active - et croire qu'il « ouvre le
        // son » alors qu'il est simplement plus fort.
        const double dbSeuil = std::max (-60.0, std::min (0.0, seuilDb));
        const double sortieA0 = dbSeuil + (0.0 - dbSeuil) / taux_;
        rattrape_ = std::pow (10.0, (-sortieA0) / 20.0 * std::max (0.0, std::min (1.0, compense)));
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (! actif_) return;

        const double crete = std::max (std::abs (g), std::abs (d));
        const double a = (crete > env_) ? aAtt_ : aRel_;
        env_ = a * env_ + (1.0 - a) * crete;

        double cible = 1.0;
        if (env_ > seuil_)
        {
            const double sur = env_ / seuil_;
            // compression en puissance : equivalent a la droite en dB, sans log
            cible = std::pow (sur, 1.0 / taux_ - 1.0);
        }
        // le gain suit l'enveloppe, il n'a pas son propre lissage : l'attaque
        // et le relachement sont deja dans `env_`
        gain_ = cible;

        g *= gain_ * rattrape_;
        d *= gain_ * rattrape_;
    }

    /** La reduction de gain instantanee, en dB (positive). Pour l'affichage. */
    double reductionDb() const noexcept
    { return -20.0 * std::log10 (std::max (1e-6, gain_)); }

private:
    double fs_ = 44100.0, env_ = 0.0, gain_ = 1.0, rattrape_ = 1.0;
    double seuil_ = 1.0, taux_ = 4.0, aAtt_ = 0.0, aRel_ = 0.0;
    bool actif_ = false;
};

// -------------------------------------------------------------- multifiltre

enum class TypeMultiFiltre { PasseBas, PasseHaut, PasseBande, Rejecteur };

/** Un filtre d'effet, APRES le mix.

    /!\ CE N'EST PAS LE FILTRE DE COUCHE. Celui de la couche suit la note (sa
    coupure se lit en multiples de la hauteur jouee) : c'est un filtre de
    synthese. Celui-ci a une coupure en HERTZ, fixe, et il voit la reverbe et
    le delai. C'est ce qui en fait un geste de mixage plutot qu'un reglage de
    timbre - et c'est pour ca que le brancher sur une macro transforme une
    nappe statique en quelque chose qui bouge, sans toucher au patch.

    Etat-variable de Chamberlin, deux integrateurs : les quatre sorties sont
    disponibles en meme temps, donc changer de type ne coute rien.
*/
class MultiFiltre
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = std::max (8000.0, fs);
        for (int c = 0; c < 2; ++c) { s1_[c] = s2_[c] = 0.0; }
    }

    void regle (double coupureHz, double resonance, TypeMultiFiltre type,
                double melange) noexcept
    {
        // /!\ CE FILTRE ETAIT UN CHAMBERLIN, ET IL DIVERGEAIT. Sa borne etait
        // posee a 0,45 fs avec, en commentaire, la bonne raison - « au-dela la
        // forme devient instable » - et la mauvaise valeur : la condition de
        // Chamberlin n'est pas fc < 0,45 fs, c'est f + q < 2, soit fc < fs/6.
        // Mesure : au-dessus de 6 kHz a 44,1 kHz, sortie infinie en quelques
        // centaines d'echantillons. C'est ce que pluginval attrapait une fois
        // sur trois en automation, et que le tirage au de ne voyait pas parce
        // qu'il ne DEPLACE aucun reglage.
        //
        // On ne rabaisse pas la borne a fs/6 : un passe-bas plafonne a 7 kHz
        // assombrirait tout, exactement le defaut qu'on cherche a corriger
        // ailleurs. On change de forme. La resolution TPT a delai nul - la meme
        // que le filtre de couche - est stable pour TOUTE coupure.
        const double fc = std::max (20.0, std::min (0.49 * fs_, coupureHz));
        g_ = std::tan (kPi * fc / fs_);
        // k_ est l'amortissement, l'INVERSE du facteur de qualite :
        // 2 = tres amorti, 0,05 = pointu.
        k_ = 2.0 - 1.95 * std::max (0.0, std::min (1.0, resonance));
        // /!\ C'est CE terme qui rend la boucle exacte. Le toucher - le
        // saturer, l'approcher - casse la resolution (voir piege 25).
        a_ = 1.0 / (1.0 + g_ * (g_ + k_));
        type_ = type;
        melange_ = std::max (0.0, std::min (1.0, melange));
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (melange_ <= 1e-6) return;
        double* voies[2] = { &g, &d };
        for (int c = 0; c < 2; ++c)
        {
            const double x = *voies[c];
            const double ph = (x - (g_ + k_) * s1_[c] - s2_[c]) * a_;
            const double pbande = g_ * ph + s1_[c];
            s1_[c] = g_ * ph + pbande;
            const double pbas = g_ * pbande + s2_[c];
            s2_[c] = g_ * pbande + pbas;

            // /!\ Ecrit avec « ! (x < limite) » et non « x > limite » : la
            // comparaison naturelle est FAUSSE pour un NaN, donc le garde-fou
            // le laisserait passer (piege 26).
            if (! (std::abs (s1_[c]) < 1e6) || ! (std::abs (s2_[c]) < 1e6))
            { s1_[c] = s2_[c] = 0.0; *voies[c] = 0.0; continue; }

            double y = pbas;
            switch (type_)
            {
                case TypeMultiFiltre::PasseHaut:  y = ph; break;
                case TypeMultiFiltre::PasseBande: y = pbande; break;
                case TypeMultiFiltre::Rejecteur:  y = ph + pbas; break;
                default: break;
            }
            *voies[c] = x + (y - x) * melange_;
        }
    }

private:
    double fs_ = 44100.0, g_ = 0.1, k_ = 1.0, a_ = 1.0, melange_ = 0.0;
    TypeMultiFiltre type_ = TypeMultiFiltre::PasseBas;
    double s1_[2] = { 0.0, 0.0 }, s2_[2] = { 0.0, 0.0 };
};

// --------------------------------------------- transfert de convertisseur

/** ⭐⭐ FAIRE REPASSER LE MIX PAR LE CONVERTISSEUR DE LA MACHINE.

    Trois choses, dans l'ordre ou un convertisseur les fait :
      1. quantification (mu-law, 8, 12 bits) ;
      2. maintien d'ordre zero a l'horloge de la machine - le signal n'est plus
         echantillonne qu'a fsRom, et ce qui se passe entre deux fronts est
         perdu ;
      3. filtre de reconstruction du bon ordre, a la bonne coupure.

    /!\ L'ORDRE COMPTE ET N'EST PAS NEGOCIABLE. Filtrer avant de decimer
    donnerait un reechantillonnage propre - exactement ce que les machines de
    l'epoque NE faisaient pas. C'est le repliement du maintien d'ordre zero,
    non filtre en amont, qui fait le son. Le corriger serait supprimer l'effet.

    /!\ ET CE N'EST PAS UN BITCRUSHER. Un bitcrusher quantifie et decime, point.
    Ici la decimation est suivie du filtre de reconstruction MESURE de la
    machine, celui-la meme que la couche utilise - donc le resultat a la
    signature d'un convertisseur precis, pas d'un effet numerique generique.
*/
class Transfert
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = std::max (8000.0, fs);
        for (int c = 0; c < 2; ++c)
        {
            maintien_[c] = 0.0;
            recon_[c].reset();
        }
        phase_ = 0.0;
        majFiltre();
    }

    /** @param cfg      la meme configuration de grain que les couches
        @param melange  0..1 */
    void regle (const ConfigGrain& cfg, double melange) noexcept
    {
        cfg_ = cfg;
        melange_ = std::max (0.0, std::min (1.0, melange));
        majFiltre();
    }

    inline void traite (double& g, double& d) noexcept
    {
        if (melange_ <= 1e-6) return;

        // maintien d'ordre zero a l'horloge de la machine
        phase_ += fsDac_ / fs_;
        const bool nouveau = (phase_ >= 1.0);
        if (nouveau) phase_ -= std::floor (phase_);

        double* voies[2] = { &g, &d };
        for (int c = 0; c < 2; ++c)
        {
            const double sec = *voies[c];
            if (nouveau) maintien_[c] = quantifie (sec, cfg_.quant);
            const double y = recon_[c].traite (maintien_[c]);
            *voies[c] = sec + (y - sec) * melange_;
        }
    }

private:
    void majFiltre() noexcept
    {
        // /!\ Le mix n'a pas de hauteur : l'horloge ne peut donc pas etre
        // asservie a une note comme elle l'est dans une couche. On prend
        // l'horloge nominale - c'est le cas « taux fixe », et c'est le seul qui
        // ait un sens ici.
        fsDac_ = std::max (4000.0, (cfg_.horloge == Horloge::Fixe)
                                      ? cfg_.fsMachine : cfg_.fsRom);
        const double fc = std::min (0.45 * fs_, cfg_.coupure * fsDac_);
        for (int c = 0; c < 2; ++c) recon_[c].regle (fc, fs_, cfg_.ordre);
    }

    double fs_ = 44100.0, fsDac_ = kFsRomEII, phase_ = 0.0, melange_ = 0.0;
    double maintien_[2] = { 0.0, 0.0 };
    Reconstruction recon_[2];
    ConfigGrain cfg_;
};

} // namespace romance
