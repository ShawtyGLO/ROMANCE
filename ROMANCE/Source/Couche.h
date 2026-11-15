// =============================================================================
//  Couche.h - Une couche de ROMANCE : source, unisson, grain, filtre, ampli.
//
//  QUATRE couches par patch. Ce n'est pas une convention : trois machines le
//  disent independamment - JV-1080 (4 tones), ZEN-Core (4 partials),
//  Wavestation (4 oscillateurs). Et chez le Wavestation chaque oscillateur est
//  un module COMPLET (filtre, ampli, deux enveloppes, deux LFO), ce qui a
//  corrige ma premiere idee d'un « filtre minimal par couche » : trop timide.
//
//  /!\ Le choix « 1, 2 ou 4 » du Wavestation n'est pas un reglage neutre :
//  prendre 4 oscillateurs COUTE de la polyphonie. La contrainte cree le
//  caractere, et le cout doit etre affiche.
//
//  Ne depend que de <cmath>, <algorithm>, <vector> et Grain.h.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Grain.h"
#include "FM.h"
#include "Table.h"
#include "Oscillateur.h"

namespace romance
{

/** Apports de la matrice de modulation. Defini dans Modulation.h ; declare ici
    pour eviter une dependance circulaire (Modulation.h inclut Couche.h). */
struct ApportMod;

constexpr int kVoixUnisson = 3;   // fige (decision utilisateur). 3 voix
                                  // produisent deja 12 a 41 cents de largeur
                                  // de raie selon le desaccord (mesure).
constexpr int kCransDetune = 8;   // desaccord quantifie - contrainte de
                                  // prototype cote recherche, sans effet ici.

// ------------------------------------------------------------- enveloppe

// ---------------------------------------------------------- filtre musical

/** Filtre a variables d'etat (TPT), stable a la modulation.

    Topologies nommees par MECANISME, jamais par marque - regle heritee de
    XPERT comp. La PENTE est ce qui distingue les machines (mesure : 24 dB/oct
    analogique de l'EII contre 18 dB/oct numerique du S1000).

    /!\ Sur l'Emulator II, filtre musical et filtre de reconstruction sont LE
    MEME composant (le SSM2045 est le VCF/VCA de voie). L'architecture doit donc
    permettre les deux cas - fusionne ou distinct -, d'ou `fusionneAvecGrain`.
*/
enum class Topologie { QuatrePoles, TroisPoles, AnalogiqueBasseReso, ZPlane };

/** ⭐⭐ LE PASSE-HAUT, EN AMONT DU FILTRE MUSICAL.

    C'est l'ordre du Juno-106 : un HPF dedie DEVANT le VCF, pas a sa place.
    Et ce n'est pas un detail d'implementation, c'est ce qui resout le probleme
    reel - quatre couches empilees accumulent du grave, et jusqu'ici la seule
    facon de le corriger etait d'ouvrir un egaliseur dans le mixer. En faire
    une TOPOLOGIE du filtre principal aurait oblige a renoncer au passe-bas
    pour degraisser : or c'est le passe-bas qui porte le caractere.

    Il est disponible sur les QUATRE moteurs et les QUATRE couches, parce qu'il
    vit dans la chaine de couche et non dans un generateur.

    ⭐ Deux machines dans un seul objet, separees par la RESONANCE :
      · resonance nulle -> le HPF non resonant du Juno-106, un degraisseur ;
      · resonance haute -> le passe-haut resonant du MS-20, qui chante.

    /!\ Meme forme TPT que le filtre musical, et pour la meme raison : elle
    reste stable quand on BOUGE la coupure, ce qu'une forme de Chamberlin ne
    fait pas (voir piege 32).
*/
class PasseHaut
{
public:
    void prepare (double fs) noexcept { fs_ = fs; reset(); }
    void reset() noexcept { for (auto& e : s1_) e = 0.0; for (auto& e : s2_) e = 0.0; }

    /** `pente` : 1 = 12 dB/oct, 2 = 24 dB/oct. */
    void regle (double coupureHz, double reso, int pente) noexcept
    {
        // ⭐ 20 Hz = COMPLETEMENT OUVERT. Le reglage neutre doit etre une
        // position du bouton, pas une case a cocher de plus : on veut pouvoir
        // degraisser progressivement sans se demander si l'effet est actif.
        fc_ = std::max (10.0, std::min (coupureHz, 0.45 * fs_));
        actif_ = (fc_ > 21.0);
        nEtages_ = (pente >= 2) ? 2 : 1;
        const double q = std::max (0.0, std::min (0.97, reso));
        for (int i = 0; i < nEtages_; ++i)
        {
            g_[i] = std::tan (kPi * fc_ / fs_);
            // /!\ Seul le PREMIER etage resonne. Deux etages resonants en
            // serie a la meme frequence donnent une bosse de 12 dB qui saute
            // aux oreilles bien avant que le bouton soit a mi-course.
            k_[i] = (i == 0) ? (2.0 - 1.94 * q) : 1.4142;
        }
    }

    inline double traite (double x) noexcept
    {
        if (! actif_) return x;
        double y = x;
        for (int i = 0; i < nEtages_; ++i)
        {
            const double a = 1.0 / (1.0 + g_[i] * (g_[i] + k_[i]));
            const double hp = (y - (g_[i] + k_[i]) * s1_[i] - s2_[i]) * a;
            const double bp = g_[i] * hp + s1_[i];
            s1_[i] = g_[i] * hp + bp;
            const double lp = g_[i] * bp + s2_[i];
            s2_[i] = g_[i] * bp + lp;

            // Le filet, ecrit a l'envers expres : `! (x < limite)` est vrai
            // pour un NaN, `x > limite` ne l'est pas (piege 26).
            if (! (std::abs (s1_[i]) < 1e6) || ! (std::abs (s2_[i]) < 1e6))
            { s1_[i] = s2_[i] = 0.0; return 0.0; }
            y = hp;
        }
        return y;
    }

private:
    double fs_ = 44100.0, fc_ = 20.0;
    double g_[2] = { 0.01, 0.01 }, k_[2] = { 2.0, 1.4142 };
    double s1_[2] = { 0.0, 0.0 }, s2_[2] = { 0.0, 0.0 };
    int nEtages_ = 1;
    bool actif_ = false;
};

class FiltreMusical
{
public:
    void prepare (double fs) noexcept { fs_ = fs; reset(); }
    void reset() noexcept
    {
        for (auto& e : s1_) e = 0.0;
        for (auto& e : s2_) e = 0.0;
        for (auto& e : sortieLp_) e = 0.0;
        p_.reset();
    }

    /** `coupureHz` est deja calculee RELATIVEMENT a la hauteur (Synplant) :
        un reglage juste a 220 Hz reste juste a 880.
        `morph` ne sert qu'a la topologie Z-Plane. */
    void regle (Topologie t, double coupureHz, double reso, double morph = 0.5) noexcept
    {
        topo_ = t;
        // ⭐⭐ LA TOPOLOGIE ANALOGIQUE EST BORNEE PLUS BAS QUE LES AUTRES, et
        // ce n'est pas une precaution : c'est une CONDITION DE STABILITE.
        //
        // La forme TPT resout sa boucle a delai nul avec le coefficient
        // a = 1/(1 + g(g+k)). Ce calcul suppose que le retour est LINEAIRE. La
        // topologie analogique y insere une saturation (`tanh`), ce qui rend la
        // resolution inexacte - tolerable tant que g reste petit, instable
        // quand il grandit.
        //
        // A 0,45 fs on a g = tan(0,45 pi) = 6,3, et le filtre EXPLOSE : mesure,
        // 1,6e308 et 20 486 NaN sur 22 050 echantillons, en jouant simplement
        // un la5 avec une coupure relative de 12,5. A 0,25 fs, g = 1,0 et la
        // boucle reste contractante.
        //
        // /!\ C'est ce defaut qui produisait « le VU-metre a fond sans aucun
        // son » : un NaN sature tous les indicateurs et ne fait aucun bruit.
        // 21,7 % des patchs tires au de tombaient dedans.
        // /!\ 0,45 fs pour TOUTES les topologies depuis que la saturation
        // analogique porte sur l'etat et non sur le retour. Le plafond a
        // 0,25 fs etait un pansement sur une instabilite structurelle : il
        // limitait les degats sans les empecher (9,7e5 de crete au lieu de
        // 1,6e308), et il retirait une octave de coupure a une topologie qui
        // n'avait rien demande.
        fc_ = std::max (20.0, std::min (coupureHz, 0.45 * fs_));
        reso_ = std::max (0.0, std::min (0.99, reso));
        morph_ = std::max (0.0, std::min (1.0, morph));

        switch (topo_)
        {
            case Topologie::TroisPoles:
                nEtages_ = 1;
                regleEtage (0, fc_, reso_);
                p_.regle (fc_, fs_);
                break;

            case Topologie::ZPlane:
            {
                // /!\ Le Morpheus a des filtres a NOMBREUX POLES morphables entre
                // deux formes complexes. On n'en refait pas quatorze : trois
                // etages dont les frequences se RESSERRENT quand on morphe -
                // d'un passe-bas etale a un empilement quasi resonant, ce qui
                // est le geste caracteristique de la machine.
                nEtages_ = 3;
                const double ecart = 2.0 - 1.7 * morph_;   // 2,0 -> 0,3 octave
                regleEtage (0, fc_ * std::pow (2.0, -ecart), reso_ * 0.5);
                regleEtage (1, fc_, 0.35 + 0.6 * reso_);
                regleEtage (2, fc_ * std::pow (2.0, ecart * 0.5), reso_ * 0.8);
                break;
            }

            case Topologie::AnalogiqueBasseReso:
                // Curtis / SSM : quatre poles, et surtout la resonance AMAIGRIT
                // le grave (le chemin resonant se retranche de l'entree) tandis
                // qu'une saturation douce dans la boucle l'empeche de diverger.
                // C'est ca qu'on entend d'un filtre analogique, pas la pente.
                nEtages_ = 2;
                regleEtage (0, fc_, reso_);
                regleEtage (1, fc_, reso_);
                break;

            case Topologie::QuatrePoles:
            default:
                nEtages_ = 2;
                regleEtage (0, fc_, reso_);
                regleEtage (1, fc_, reso_);
                break;
        }
    }

    void reglePoleSimple (double fc) noexcept { p_.regle (fc, fs_); }

    inline double traite (double x) noexcept
    {
        double y = x;
        for (int i = 0; i < nEtages_; ++i) y = etage (y, i);
        if (topo_ == Topologie::TroisPoles) y = p_.traite (y);
        return y;
    }

private:
    void regleEtage (int i, double fc, double q) noexcept
    {
        fc = std::max (20.0, std::min (fc, 0.45 * fs_));
        g_[i] = std::tan (kPi * fc / fs_);
        k_[i] = 2.0 - 1.98 * std::max (0.0, std::min (0.99, q));
    }

    inline double etage (double x, int i) noexcept
    {
        const double a  = 1.0 / (1.0 + g_[i] * (g_[i] + k_[i]));
        const double retour = (g_[i] + k_[i]) * s1_[i];
        if (topo_ == Topologie::AnalogiqueBasseReso)
        {
            // /!\ DEUX mecanismes, et j'avais d'abord cru qu'un seul suffisait.
            // (a) saturation douce dans la boucle : la resonance s'ecrase au
            //     lieu de diverger - c'est le comportement analogique.
            // (a) la saturation est appliquee A L'ETAT, en fin de pas - voir
            //     plus bas. /!\ ELLE ETAIT ICI, sur le terme de retour, et
            //     c'etait l'erreur : le coefficient `a` resout la boucle a
            //     delai nul en SUPPOSANT ce retour lineaire. Le saturer rend la
            //     resolution inexacte, l'amortissement effectif peut devenir
            //     negatif, et le filtre diverge. Mesure : 9,7e5 de crete a
            //     20 kHz de coupure et 0,99 de resonance, et 1,6e308 en jouant
            //     simplement haut. Saturer l'ETAT donne le meme ecrasement de
            //     resonance sans toucher a la resolution.
            // (b) le retour resonant se RETRANCHE DE L'ENTREE, comme dans une
            //     echelle Curtis ou Moog. C'est CA qui amaigrit le grave quand
            //     la resonance monte, et je ne l'avais pas fait : la mesure ne
            //     donnait que 0,7 dB d'ecart avec un 4 poles ordinaire, soit
            //     bien moins que ce que la description promettait.
            // /!\ BORNEE, ET SATUREE. Sans la borne, `dose` depassait 1 a
            // resonance maximale et le gain de cette boucle locale passait
            // au-dessus de 1 : le filtre divergeait des que la coupure BOUGEAIT
            // - ce qui n'arrivait pas tant que la coupure restait fixe, donc le
            // defaut est reste invisible jusqu'a ce que la derive de timbre
            // fasse respirer la coupure de 2,5 %.
            // Un filtre resonant doit rester stable quand on tourne son bouton :
            // c'est le cas d'usage, pas un cas limite.
            const double dose = std::max (0.0, std::min (1.0, (2.0 - k_[i]) / 1.98));
            x -= 0.85 * dose * std::tanh (sortieLp_[i] * 1.3) / 1.3;
        }
        const double hp = (x - retour - s2_[i]) * a;
        const double bp = g_[i] * hp + s1_[i];
        s1_[i] = g_[i] * hp + bp;
        const double lp = g_[i] * bp + s2_[i];
        s2_[i] = g_[i] * bp + lp;
        sortieLp_[i] = lp;

        // (a) L'ECRASEMENT ANALOGIQUE DE LA RESONANCE, applique a l'etat.
        // C'est ce qui fait qu'une resonance poussee s'ecrase au lieu de
        // hurler - le comportement d'une echelle Curtis ou Moog - et ca ne
        // coute pas la stabilite, parce que la boucle a delai nul reste
        // resolue exactement.
        if (topo_ == Topologie::AnalogiqueBasseReso)
            s1_[i] = std::tanh (s1_[i] * 1.3) / 1.3;

        // ⭐ LE FILET. La borne ci-dessus corrige la cause connue ; ce garde-fou
        // couvre celles qu'on ne connait pas encore.
        //
        // /!\ LA COMPARAISON EST ECRITE A L'ENVERS EXPRES. `! (|x| < 1e6)` est
        // vrai pour un NaN, alors que `|x| > 1e6` est FAUX pour un NaN - toute
        // comparaison avec NaN rend faux. Un filet ecrit dans le sens naturel
        // laisserait donc passer exactement ce qu'il doit attraper.
        //
        // Cout : deux comparaisons par echantillon et par etage. Le prix d'un
        // instrument qui ne peut plus rendre un projet inutilisable.
        if (! (std::abs (s1_[i]) < 1e6) || ! (std::abs (s2_[i]) < 1e6))
        {
            s1_[i] = s2_[i] = sortieLp_[i] = 0.0;
            return 0.0;
        }
        return lp;
    }

    double fs_ = 44100.0, fc_ = 1000.0, reso_ = 0.0, morph_ = 0.5;
    double g_[3] = { 0.1, 0.1, 0.1 }, k_[3] = { 2.0, 2.0, 2.0 };
    double s1_[3] = { 0.0, 0.0, 0.0 }, s2_[3] = { 0.0, 0.0, 0.0 };
    double sortieLp_[3] = { 0.0, 0.0, 0.0 };
    int nEtages_ = 2;
    Topologie topo_ = Topologie::QuatrePoles;
    UnPole p_;
};

// ------------------------------------------------------------------ couche

/** Reglages d'une couche. Tout ce qui est expose a l'utilisateur ou au preset. */
/** ⭐ QUEL GENERATEUR. Ce n'est pas une option parmi d'autres : c'est le choix
    qui decide de tout le reste. Le corpus mesure est plein de synthes
    analogiques, et les decrire en 24 partiels additifs demande soixante-douze
    valeurs pour un resultat moins bon que six. */
enum class TypeMoteur { Rom, Analogique, FM, Table };

struct ReglagesCouche
{
    bool   active = true;
    TypeMoteur moteur = TypeMoteur::Rom;
    ReglagesVA va;
    ReglagesFM fm;
    ReglagesTable table;

    // zones - modele ZEN-Core (par couche). Le Wavestation les met au niveau
    // performance ; on a reporte ce niveau, mais l'architecture le permettra.
    int    noteMin = 0,  noteMax = 127;
    int    veloMin = 0,  veloMax = 127;

    // hauteur
    // /!\ Bornes larges (+-48 demi-tons) mais le GRAIN les rend inegales : vers
    // le haut l'horloge du DAC monte et la ROM s'epuise vite ; vers le bas la
    // bande passante s'effondre avec le filtre asservi. Ce n'est pas une plage
    // neutre, c'est une plage ou la machine se degrade differemment aux deux
    // bouts - et c'est voulu.
    int    transpose = 0;        // demi-tons
    double accordFin = 0.0;      // cents

    // unisson
    double desaccordCents = 8.0;
    double largeur = 0.5;

    // grain (par couche - confirme par Pure LoFi, 9 modes par moteur)
    ConfigGrain grain;
    bool   fusionneAvecGrain = false;  // EII : le filtre musical EST le filtre
                                       // de reconstruction

    // filtre musical
    Topologie topo = Topologie::QuatrePoles;
    double coupureRel = 8.0;   // en multiples de la hauteur jouee (Synplant)
    double reso = 0.0;
    double morph = 0.5;        // n'agit que sur la topologie Z-Plane

    /** ⭐ LE PASSE-HAUT DE COUCHE. En Hz absolus et non en multiples de la
        hauteur : degraisser, c'est retirer ce qui s'accumule SOUS une
        frequence donnee, et cette frequence ne doit pas monter avec la note -
        sinon un accord aigu se retrouve amaigri alors que c'est le grave qui
        s'empile. C'est la difference d'intention avec le passe-bas, qui suit
        le clavier. */
    double phCoupure = 20.0;   // 20 Hz = ouvert
    double phReso = 0.0;       // 0 = Juno-106, haut = MS-20
    int    phPente = 1;        // 1 = 12 dB/oct, 2 = 24 dB/oct
    double suiviClavier = 1.0;

    // boucle, en fraction de la ROM
    double boucleDebut = 0.0, boucleFin = 1.0;

    /** ⭐ DEPART DE LECTURE - le geste du rompleur.

        /!\ A ne pas confondre avec le debut de BOUCLE. Le depart dit ou la note
        COMMENCE ; la boucle dit ou elle tourne une fois l'attaque passee. C'est
        le depart, module par la velocite, qui donne le comportement que tout le
        monde reconnait : frappe fort on entend le marteau, frappe doux on entre
        apres. */
    double depart = 0.0;          // fraction de la ROM, 0..0,9

    /** Fondu de boucle, en fraction de la longueur de boucle. */
    double fonduBoucle = 0.0;

    /** ⭐ Retard avant que la note demarre - le « Tone Delay » du JV-1080.
        C'est litteralement comment les nappes de cette machine sont faites :
        une couche part tout de suite, l'autre 300 ms plus tard. */
    double retard = 0.0;          // secondes

    /** ⭐⭐ CE QUE LA VELOCITE FAIT.

        /!\ ELLE NE FAISAIT RIEN. Avant ce jour, `accepte()` etait le SEUL
        endroit du plugin qui lisait la velocite : elle ouvrait ou fermait la
        zone, et c'est tout. Jouer fort ou doux donnait exactement le meme son -
        ce qui explique qu'un instrument par ailleurs riche paraisse fige, et
        que Genopatch ne sache pas resynthetiser un son qui s'ouvre quand on
        appuie.

        /!\ ET LE DEFAUT N'EST PAS ZERO. Un synthetiseur livre sans reponse a la
        velocite n'est pas neutre, il est casse : personne ne pense a aller
        chercher le bouton qui rend son clavier vivant. */
    double veloNiveau  = 0.65;    // 0..1
    double veloCoupure = 0.35;    // -1..1, en multiples de 4 octaves
    double veloAttaque = 0.0;     // 0..1 - raccourcit l'attaque quand on frappe fort
    double veloDepart  = 0.0;     // 0..1 - avance le depart de lecture
    /** Courbe : 0 = molle (il faut appuyer fort), 0,5 = lineaire, 1 = raide. */
    double courbeVelo  = 0.5;
    /** Largeur du fondu aux bords de la zone de velocite, en unites MIDI.
        /!\ Zero = commutation franche, ce qui s'entend comme un interrupteur
        des qu'on joue autour du seuil. */
    double fonduVelo = 0.0;

    // ampli
    double attaque = 0.005, decli = 0.3, tenue = 0.8, relache = 0.4;
    double niveau = 0.7, pan = 0.0;
};

/** Une couche pour UNE voix. La ROM lui est fournie (elle appartient au patch,
    pas a la voix : c'est ce qui permet a l'etat du patch de ne peser que la
    recette). */
class Couche
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        etage_.prepare (fs);
        for (auto& v : vas_) v.prepare (fs * (double) kSurEch);
        for (auto& f : fms_) f.prepare (fs * (double) kSurEch);
        for (auto& t : tables_) t.prepare (fs * (double) kSurEch);
        for (auto& l : lecteurs_) l.prepare (etage_.fsInterne());
        filtre_.prepare (fs);
        phG_.prepare (fs); phD_.prepare (fs);
        filtreD_.prepare (fs);
        env_.prepare (fs);
    }

    /** ⭐ LE GAIN DE ZONE, entre 0 et 1 - et non plus un simple oui/non.

        /!\ Une zone de velocite franche s'entend comme un INTERRUPTEUR des
        qu'on joue autour du seuil : deux notes voisines donnent deux sons sans
        rapport. C'est le defaut classique du multi-couche, et le JV-1080 le
        resout par un fondu. `fonduVelo` a zero rend l'ancien comportement.
    */
    static double gainZone (const ReglagesCouche& r, int note, int velo) noexcept
    {
        if (! r.active) return 0.0;
        if (note < r.noteMin || note > r.noteMax) return 0.0;
        const double f = std::max (0.0, r.fonduVelo);
        if (f < 0.5)
            return (velo >= r.veloMin && velo <= r.veloMax) ? 1.0 : 0.0;

        const double v = (double) velo;
        double g = 1.0;
        if (v < (double) r.veloMin)  g = std::min (g, 1.0 - ((double) r.veloMin - v) / f);
        if (v > (double) r.veloMax)  g = std::min (g, 1.0 - (v - (double) r.veloMax) / f);
        return std::max (0.0, std::min (1.0, g));
    }

    bool accepte (const ReglagesCouche& r, int note, int velo) const noexcept
    {
        return gainZone (r, note, velo) > 0.0;
    }

    /** La velocite mise en forme par la courbe de la couche. */
    static double velNormalisee (const ReglagesCouche& r, int velo) noexcept
    {
        const double v = std::max (0.0, std::min (1.0, (double) velo / 127.0));
        // /!\ Exposant entre 1/3 et 3 : au-dela la courbe devient une marche, et
        // « raide » cesse de vouloir dire jouable.
        const double e = std::pow (3.0, 2.0 * std::max (0.0, std::min (1.0, r.courbeVelo)) - 1.0);
        return std::pow (v, e);
    }

    void declenche (const ReglagesCouche& r, double ratio, double hauteurHz,
                    double vel = 1.0, double gainZ = 1.0) noexcept
    {
        vel_ = std::max (0.0, std::min (1.0, vel));
        gainZone_ = std::max (0.0, std::min (1.0, gainZ));
        // /!\ Le retard se compte en echantillons a la frequence de SORTIE, pas
        // a la frequence interne : c'est un temps musical, pas un nombre de pas
        // de lecteur.
        retard_ = (int) std::round (std::max (0.0, r.retard) * fs_);
        departAPoser_ = true;

        for (int v = 0; v < kVoixUnisson; ++v)
        {
            const double e = (kVoixUnisson > 1)
                               ? (-1.0 + 2.0 * (double) v / (double) (kVoixUnisson - 1))
                               : 0.0;
            const double rv = ratio * std::pow (2.0, e * r.desaccordCents / 1200.0);
            lecteurs_[(size_t) v].majHorloge (r.grain, rv);
            lecteurs_[(size_t) v].reglePhase (0.0);
            // /!\ L'unisson doit marcher pour LES DEUX moteurs. Sur la ROM il
            // vient du taux de lecture ; ici il vient de la hauteur de
            // l'oscillateur. Meme geste, deux mecanismes.
            const double hv = hauteurHz * std::pow (2.0, e * r.desaccordCents / 1200.0);
            // ⭐⭐ ON NE DECLENCHE QUE LE GENERATEUR ACTIF.
            //
            // /!\ LES TROIS ETAIENT DECLENCHES A CHAQUE NOTE, quel que soit le
            // moteur choisi. Anodin pour l'analogique et la FM ; ruineux pour la
            // table d'ondes, dont `declenche` FABRIQUE la table : seize images
            // de 512 points, jusqu'a 90 harmoniques, soit **737 000 sinus** -
            // et fois trois pour l'unisson. Environ 30 ms de calcul SUR LE FIL
            // AUDIO a chaque note posee, alors qu'un bloc de 128 echantillons
            // dure 2,9 ms.
            //
            // C'est le claquement que l'utilisateur entendait « sur tous les
            // modes, meme clean » des qu'il jouait plus de deux notes : un
            // decrochage, pas une discontinuite du signal. Aucune mesure sur le
            // rendu hors-ligne ne pouvait le trouver.
            switch (r.moteur)
            {
                case TypeMoteur::Analogique:
                    vas_[(size_t) v].declenche (r.va, hv, 0.11 * (double) v); break;
                case TypeMoteur::FM:
                    // La vitesse AVANT le declenchement : elle fixe le gain de
                    // chaque operateur pour toute la note, comme sur un DX7.
                    fms_[(size_t) v].poseVelocite (r.fm, vel_);
                    fms_[(size_t) v].declenche (r.fm, hv, 0.11 * (double) v); break;
                case TypeMoteur::Table:
                    tables_[(size_t) v].declenche (r.table, hv, 0.11 * (double) v); break;
                default: break;   // ROM : rien a declencher, la lecture suffit
            }
            // gains de panoramique de la voix d'unisson : c'est la LARGEUR.
            // Sans eux, l'unisson ne s'entend qu'en battement et pas en image.
            const double pos = std::max (-1.0, std::min (1.0, e * r.largeur + r.pan));
            const double a = (pos + 1.0) * 0.25 * kPi;   // loi en puissance
            panG_[v] = std::cos (a);
            panD_[v] = std::sin (a);
        }
        // L'etage de reconstruction est reglé sur la hauteur NOMINALE : les voix
        // d'unisson ne different que de quelques cents, donc une seule horloge
        // de reference suffit. Approximation documentee dans Grain.h.
        etage_.majHorloge (r.grain, ratio);
        // les points de boucle sont poses ici : ils appartiennent au MATERIAU,
        // pas a la note.
        boucleDeb_ = r.boucleDebut;
        boucleFin_ = r.boucleFin;
        bouclePosee_ = false;

        const double fc = r.coupureRel * (hauteurHz * r.suiviClavier
                                          + 261.6 * (1.0 - r.suiviClavier));
        filtre_.regle (r.topo, fc, r.reso, r.morph);
        filtre_.reglePoleSimple (fc);
        filtreD_.regle (r.topo, fc, r.reso, r.morph);
        filtreD_.reglePoleSimple (fc);
        phG_.regle (r.phCoupure, r.phReso, r.phPente);
        phD_.regle (r.phCoupure, r.phReso, r.phPente);
        // ⭐ LA VELOCITE RACCOURCIT L'ATTAQUE. Facteur 8 au maximum : c'est ce
        // qui separe un coup sec d'un souffle, et au-dela l'attaque devient un
        // clic quelle que soit la nuance.
        const double kAtt = std::pow (8.0, -r.veloAttaque * vel_);
        env_.regle (r.attaque * kAtt, r.decli, r.tenue, r.relache);
        env_.declenche();
        niveauLisse_ = r.niveau;   // pas de fondu a l'attaque de la note
    }

    /** Change la hauteur d'une note DEJA EN TRAIN DE SONNER.

        Appelee une fois par bloc, pas par echantillon : recalculer les
        coefficients du filtre de reconstruction (des tan()) a chaque
        echantillon coûterait cher pour un geste de souris.

        ⭐ Sur une machine a HORLOGE VARIABLE, changer la hauteur change
        l'horloge du convertisseur - donc le filtre de reconstruction asservi
        suit, et le TIMBRE se deplace avec la note. Ce n'est pas un effet
        secondaire a corriger, c'est le comportement de la machine.
    */
    void majHauteur (const ReglagesCouche& r, double ratio, double hauteurHz,
                     double dDesaccord = 0.0, double dCoupureOct = 0.0,
                     double dCoupureGrainOct = 0.0, double dMorph = 0.0) noexcept
    {
        if (! env_.active()) return;
        const double desac = std::max (0.0, r.desaccordCents + dDesaccord);
        for (int v = 0; v < kVoixUnisson; ++v)
        {
            const double e = (kVoixUnisson > 1)
                               ? (-1.0 + 2.0 * (double) v / (double) (kVoixUnisson - 1))
                               : 0.0;
            const double dv = std::pow (2.0, e * desac / 1200.0);
            lecteurs_[(size_t) v].majHorloge (r.grain, ratio * dv);
            // meme regle a chaque pas de controle : `EtageTable::majHauteur`
            // refabrique la table des que la hauteur bouge de plus de 3 %, donc
            // un vibrato un peu large la refaisait 2 756 fois par seconde.
            switch (r.moteur)
            {
                case TypeMoteur::Analogique:
                    vas_[(size_t) v].majHauteur (r.va, hauteurHz * dv); break;
                case TypeMoteur::FM:
                    fms_[(size_t) v].majHauteur (r.fm, hauteurHz * dv); break;
                case TypeMoteur::Table:
                    tables_[(size_t) v].majHauteur (r.table, hauteurHz * dv); break;
                default: break;
            }
        }
        // la coupure de RECONSTRUCTION peut etre modulee : c'est l'horloge de la
        // machine qui respire, une destination que personne d'autre n'offre.
        ConfigGrain gc = r.grain;
        if (dCoupureGrainOct != 0.0)
            gc.coupure = std::max (0.02, std::min (0.49,
                            gc.coupure * std::pow (2.0, dCoupureGrainOct)));
        etage_.majHorloge (gc, ratio);

        // la coupure du filtre musical est RELATIVE a la hauteur : elle suit.
        double fc = r.coupureRel * (hauteurHz * r.suiviClavier
                                    + 261.6 * (1.0 - r.suiviClavier));
        // ⭐ ET LA VELOCITE OUVRE LE FILTRE. C'est ici, dans `majHauteur`, et
        // pas seulement au declenchement : cette fonction recalcule la coupure a
        // chaque pas de controle, donc une valeur posee dans `declenche` serait
        // effacee au bout de 16 echantillons. C'est exactement le genre de bug
        // qu'on n'entend pas - un filtre qui s'ouvre puis se referme aussitot.
        fc *= std::pow (2.0, dCoupureOct + r.veloCoupure * 4.0 * (vel_ - 0.5));
        dMorphTable_ = dMorph;
        const double mo = std::max (0.0, std::min (1.0, r.morph + dMorph));
        filtre_.regle (r.topo, fc, r.reso, mo);
        filtre_.reglePoleSimple (fc);
        filtreD_.regle (r.topo, fc, r.reso, mo);
        filtreD_.reglePoleSimple (fc);
        phG_.regle (r.phCoupure, r.phReso, r.phPente);
        phD_.regle (r.phCoupure, r.phReso, r.phPente);
    }

    /** Relance la note SANS couper le son : le lecteur garde sa phase et
        l'enveloppe repart de la valeur ou elle est.

        /!\ En mono, l'ancienne version faisait `coupe()` puis `declenche()` :
        la phase du lecteur repartait de zero et l'enveloppe de zero aussi, donc
        une discontinuite pleine echelle a CHAQUE note. C'est le clic qu'on
        entendait. Les machines monophoniques ne coupent pas leur oscillateur
        quand on change de note - elles changent son horloge.
    */
    void relanceDouce (const ReglagesCouche& r, double ratio, double hauteurHz,
                       double vel = 1.0, double gainZ = 1.0) noexcept
    {
        vel_ = std::max (0.0, std::min (1.0, vel));
        gainZone_ = std::max (0.0, std::min (1.0, gainZ));
        majHauteur (r, ratio, hauteurHz);
        env_.regle (r.attaque, r.decli, r.tenue, r.relache);
        env_.redeclenche();
        // ⭐ ET LES OPERATEURS FM AVEC - meme raison que pour `relache()`.
        // L'enveloppe d'amplitude n'est plus la seule : depuis que chaque
        // operateur a la sienne, relancer l'amplitude seule laisse le timbre
        // (et donc le niveau) la ou la note precedente l'avait laisse.
        if (r.moteur == TypeMoteur::FM)
            for (auto& f : fms_)
            {
                f.poseVelocite (r.fm, vel_);
                f.redeclenche();
            }
    }

    /** ⭐⭐ LEGATO : RELANCER LE TIMBRE, PAS LE VOLUME.

        /!\ Il existait une methode `glisse` ici, et PERSONNE NE L'APPELAIT :
        `Voix::glisse` passe par `majHauteur`. Corriger le legato dedans n'a
        donc rien change - la mesure est restee a 0,08 au dixieme pres, ce qui
        etait le seul indice que je touchais du code mort.

        Le defaut reel : « on ne redeclenche rien du tout » rendait le legato
        injouable des que les operateurs avaient une tenue basse. La seconde
        note heritait d'operateurs eteints - douze fois plus faible que la
        premiere, « la seconde note etouffee ».

        Ce que fait un DX7 : rien ne redeclenche, et c'est pour ca qu'on le
        trouve difficile a jouer lie. Mais nous avons ce qu'il n'a pas - une
        enveloppe d'amplitude PAR COUCHE, en plus de celles des operateurs. On
        separe donc les deux roles :

          · l'enveloppe de COUCHE ne redeclenche pas -> aucune re-attaque de
            volume, c'est exactement ce qui fait le legato ;
          · les enveloppes d'OPERATEURS redeclenchent -> la note « parle » de
            nouveau au lieu d'heriter d'un timbre eteint.

        C'est ce qui distingue notre legato de notre mono : le mono relance les
        deux. */
    void relanceTimbre (const ReglagesCouche& r) noexcept
    {
        if (r.moteur != TypeMoteur::FM) return;
        for (auto& f : fms_) f.redeclenche();
    }

    void relache() noexcept
    {
        env_.relache();
        // /!\ ET LES OPERATEURS FM AVEC. Chacun a desormais sa propre
        // enveloppe : si seule l'amplitude relache, le timbre reste fige a sa
        // valeur de tenue pendant toute la queue.
        for (auto& f : fms_) f.relache();
    }
    /** ⭐ OU EN EST CETTE COUCHE, entre 0 et 1 de ce que l'interface affiche.

        /!\ NORMALISEE, et c'est ce qui la rend utilisable. Le lecteur de ROM
        compte en ECHANTILLONS, les trois autres moteurs en PHASE de cycle :
        deux grandeurs sans rapport. En rendre une seule, ramenee a la fraction
        de ce qui est DESSINE, evite a l'interface de refaire la conversion
        pour chaque moteur - et de se tromper le jour ou l'on en ajoute un.
    */
    double position (const ReglagesCouche& r, int nRom) const noexcept
    {
        switch (r.moteur)
        {
            case TypeMoteur::Analogique: return vas_[0].position();
            case TypeMoteur::FM:         return fms_[0].position();
            case TypeMoteur::Table:      return tables_[0].position();
            default:
                if (nRom < 2) return 0.0;
                return std::max (0.0, std::min (1.0,
                           lecteurs_[0].position() / (double) nRom));
        }
    }

    void extinctionRapide() noexcept { env_.extinctionRapide(); }
    void coupe()   noexcept { env_.coupe(); filtre_.reset(); filtreD_.reset();
                             phG_.reset(); phD_.reset();
                              bouclePosee_ = false; }
    bool active() const noexcept { return env_.active(); }

    /** Un echantillon STEREO. */
    inline void suivant (const ReglagesCouche& r, const double* rom, int nRom,
                         double& g, double& d,
                         double dNiveau = 0.0, double dPan = 0.0) noexcept
    {
        g = d = 0.0;
        // ⭐ TONE DELAY : la couche ne sonne pas encore. /!\ On ne fait PAS
        // avancer l'enveloppe pendant ce temps - sinon le retard ne retarderait
        // pas la note, il en mangerait le debut.
        if (retard_ > 0) { --retard_; return; }
        if (! env_.active()) return;
        const bool synthese = (r.moteur != TypeMoteur::Rom);
        if (! synthese && (rom == nullptr || nRom <= 1)) return;
        // /!\ RECALCULER DES QUE LA ROM CHANGE DE TAILLE. Le verrou d'origine
        // ne posait la boucle qu'UNE fois par voix et ne la revoyait jamais :
        // en passant d'une ROM de recette (~45 000 echantillons, boucle a 94 %)
        // a une forme d'onde (6 882 echantillons), le debut de boucle restait a
        // 42 000, se faisait ramener a `fin - 2`, et la boucle tombait a DEUX
        // echantillons. Le son ne s'arretait pas : il se repliait sur un point.
        // C'est ce que l'utilisateur entendait comme « ca se coupe net ».
        if (! bouclePosee_ || nRomPose_ != nRom)
        {
            const int b0 = (int) (boucleDeb_ * (double) nRom);
            const int b1 = (int) (boucleFin_ * (double) nRom);
            for (auto& l : lecteurs_) { l.regleBoucle (b0, b1); l.regleFondu (r.fonduBoucle); }
            bouclePosee_ = true;
            nRomPose_ = nRom;
        }

        // ⭐ LE DEPART NE PEUT SE POSER QU'ICI : `declenche` ne connait pas la
        // taille de la ROM (elle appartient au patch, pas a la note). On le
        // pose donc au premier echantillon rendu, une seule fois.
        if (departAPoser_)
        {
            const double dep = std::max (0.0, std::min (0.9,
                                   r.depart + r.veloDepart * vel_));
            for (auto& l : lecteurs_) l.reglePhase (dep * (double) nRom);
            departAPoser_ = false;
        }

        double sg = 0.0, sd = 0.0;
        if (synthese)
        {
            // ⭐ AUCUN GRAIN ICI, et c'est un choix documente. Un Juno n'a ni
            // ROM, ni convertisseur, ni filtre de reconstruction ; un DX7 a bien
            // un convertisseur, mais il calcule ses operateurs a pleine
            // resolution et ne relit aucune memoire - le drop-sample et la
            // quantification mu-law n'ont de sens ni pour l'un ni pour l'autre. Faire passer ces oscillateurs par la
            // quantification mu-law serait une decoration, pas une simulation -
            // exactement le genre de raccourci que ce projet refuse.
            // Le sur-echantillonnage reste, lui : il sert aux oscillateurs.
            for (int i = 0; i < kSurEch; ++i)
            {
                double ig = 0.0, id = 0.0;
                for (int v = 0; v < kVoixUnisson; ++v)
                {
                    // ⭐ `dMorph` sert de BALAYAGE de table : c'est la meme
                    // destination de modulation qui pilote le morph du Z-Plane
                    // et la position dans la table. Les deux sont le meme
                    // geste - se deplacer dans un ensemble de formes - donc ils
                    // partagent la meme ligne de matrice plutot que d'en
                    // occuper deux pour la meme idee.
                    const double e =
                        (r.moteur == TypeMoteur::FM)    ? fms_[(size_t) v].suivant (r.fm)
                      : (r.moteur == TypeMoteur::Table) ? tables_[(size_t) v].suivant (r.table, dMorphTable_)
                                                        : vas_[(size_t) v].suivant (r.va);
                    ig += e * panG_[v];
                    id += e * panD_[v];
                }
                // /!\ DECIMATION FILTREE, obligatoirement. Ma premiere version
                // se contentait de garder un echantillon sur quatre : tout ce
                // que les oscillateurs produisent entre 22 et 88 kHz se
                // repliait alors dans la bande audible. On aurait donc mis un
                // PolyBLEP pour supprimer le repliement de l'oscillateur, et
                // reintroduit exactement le meme defaut trois lignes plus bas.
                sg = etage_.decime (0, ig / (double) kVoixUnisson);
                sd = etage_.decime (1, id / (double) kVoixUnisson);
            }
        }
        else if (r.grain.actif)
        {
            // /!\ Le sur-echantillonnage n'est paye QU'ICI, une fois par couche.
            // Le filtre de reconstruction etait a l'origine dans chaque lecteur :
            // 74 millions de biquads par seconde a 16 voix. Injouable.
            for (int i = 0; i < kSurEch; ++i)
            {
                double ig = 0.0, id = 0.0;
                for (int v = 0; v < kVoixUnisson; ++v)
                {
                    const double e = lecteurs_[(size_t) v].pasInterne (rom, nRom, r.grain);
                    ig += e * panG_[v];
                    id += e * panD_[v];
                }
                ig /= (double) kVoixUnisson;
                id /= (double) kVoixUnisson;
                sg = etage_.decime (0, etage_.reconstruit (0, ig));
                sd = etage_.decime (1, etage_.reconstruit (1, id));
            }
        }
        else
        {
            // PROPRE : lecture interpolee, aucun filtre d'epoque - donc aucun
            // sur-echantillonnage non plus. La position artistique la plus
            // neutre est aussi la moins chere.
            for (int i = 0; i < kSurEch; ++i)
            {
                double ig = 0.0, id = 0.0;
                for (int v = 0; v < kVoixUnisson; ++v)
                {
                    const double e = lecteurs_[(size_t) v].pasInterne (rom, nRom, r.grain);
                    ig += e * panG_[v];
                    id += e * panD_[v];
                }
                sg = ig / (double) kVoixUnisson;
                sd = id / (double) kVoixUnisson;
            }
        }

        // ⭐ LE PASSE-HAUT PASSE EN PREMIER, comme sur un Juno-106 : il degraisse
        // ce que le filtre musical va ensuite mettre en forme. Dans l'autre
        // sens, une resonance du passe-bas dans le grave serait retiree apres
        // avoir ete fabriquee - on paierait le calcul pour rien.
        //
        // /!\ Et il est HORS du test `fusionneAvecGrain`. Ce test dit que le
        // filtre musical et le filtre de reconstruction sont le meme composant
        // (SSM2045 de l'Emulator II) ; le passe-haut, lui, n'a jamais ete ce
        // composant, et le desactiver la ou l'on en a le plus besoin - une
        // couche a grain, donc grave et epaisse - serait exactement le
        // contraire du but.
        sg = phG_.traite (sg);
        sd = phD_.traite (sd);

        if (! r.fusionneAvecGrain)
        {
            sg = filtre_.traite (sg);
            sd = filtreD_.traite (sd);
        }
        // /!\ Le NIVEAU multiplie le signal : il doit etre lisse, sinon bouger
        // la tirette craque. Lecon d'XPERT comp - ne lisser QUE ce qui multiplie.
        // ⭐ LA VELOCITE AGIT SUR LE NIVEAU, et le gain de zone avec elle.
        // /!\ En AMPLITUDE et non en decibels : a vel = 0 on veut le silence,
        // pas -60 dB. Une loi en decibels ne rend jamais zero.
        const double gVel = 1.0 - r.veloNiveau * (1.0 - vel_);
        const double niv = std::max (0.0, std::min (1.0, r.niveau + dNiveau))
                         * gVel * gainZone_;
        niveauLisse_ += 0.0008 * (niv - niveauLisse_);
        const double a = env_.suivant() * niveauLisse_;
        if (dPan != 0.0)
        {
            // panoramique module : loi en puissance, comme celui de l'unisson
            const double pos = std::max (-1.0, std::min (1.0, dPan));
            const double ang = (pos + 1.0) * 0.25 * kPi;
            const double gG = std::cos (ang) * 1.41421356, gD = std::sin (ang) * 1.41421356;
            g = sg * a * gG;
            d = sd * a * gD;
            return;
        }
        g = sg * a;
        d = sd * a;
    }

private:
    double fs_ = 44100.0;
    Lecteur lecteurs_[kVoixUnisson];
    EtageVA vas_[kVoixUnisson];
    EtageFM fms_[kVoixUnisson];
    EtageTable tables_[kVoixUnisson];
    double dMorphTable_ = 0.0;
    EtageReconstruction etage_;
    FiltreMusical filtre_, filtreD_;
    PasseHaut phG_, phD_;
    Enveloppe env_;
    double panG_[kVoixUnisson] { 0.707, 0.707, 0.707 };
    double panD_[kVoixUnisson] { 0.707, 0.707, 0.707 };
    double boucleDeb_ = 0.0, boucleFin_ = 1.0;
    double niveauLisse_ = 0.0;
    bool   bouclePosee_ = false;
    int    nRomPose_ = -1;

    /** Velocite mise en forme, et gain de fondu de zone. */
    double vel_ = 1.0, gainZone_ = 1.0;
    int    retard_ = 0;              // Tone Delay, en echantillons restants
    bool   departAPoser_ = false;
};

} // namespace romance
