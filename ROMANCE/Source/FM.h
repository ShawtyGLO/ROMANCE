// =============================================================================
//  FM.h - Le moteur a MODULATION DE PHASE (quatre operateurs).
//
//  ⭐ POURQUOI CE MOTEUR. Le corpus contient 400 fichiers BELLS et 397 SYNTH.
//  Les cloches, les metalliques, les pianos electriques sont exactement ce que
//  l'additif decrit MAL : leurs partiels sont inharmoniques et nombreux, donc
//  il en faut vingt-quatre et ils sont tous a regler. En FM, un rapport non
//  entier entre deux operateurs produit cette meme foret de partiels avec
//  DEUX valeurs. C'est le meme raisonnement que pour l'analogique : donner au
//  son le generateur qui lui correspond, plutot qu'un generateur universel qui
//  fait tout a moitie.
//
//  /!\ MODULATION DE PHASE, PAS DE FREQUENCE. Yamaha appelait ca de la FM, mais
//  le DX7 module la PHASE. La difference n'est pas cosmetique : en modulation
//  de frequence vraie, l'integrale de la modulante fait deriver la hauteur des
//  que la modulante a une composante continue, et le son se desaccorde tout
//  seul. En modulation de phase, la hauteur reste juste par construction.
//
//  /!\ ET PAS DE POLYBLEP ICI. Un operateur FM est un SINUS : il n'a aucune
//  discontinuite a corriger. Le repliement, en FM, vient de partiels legitimes
//  qui montent au-dessus de Nyquist quand l'indice est fort - on le limite en
//  bornant l'indice a l'aigu, pas en filtrant la forme d'onde.
//
//  Ne depend que de <cmath> et <algorithm>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Grain.h"   // kPi

namespace romance
{

constexpr int kOperateurs = 4;
constexpr int kAlgosFM = 8;

/** Les huit algorithmes.

    `src[i]` = quel operateur module l'operateur i (-1 = aucun).
    `porteur[i]` = l'operateur i sort-il dans le son ?

    /!\ INVARIANT : un modulateur a TOUJOURS un indice superieur a celui qu'il
    module. C'est ce qui permet de calculer les operateurs de 3 vers 0 en une
    seule passe, sans file d'attente ni recursion - et ca interdit les boucles
    autres que le retour explicite de l'operateur 3.

    L'ordre va du plus METALLIQUE (une seule chaine profonde) au plus DOUX
    (quatre sinus en parallele, c'est-a-dire de l'additif a quatre partiels).
*/
struct AlgoFM
{
    int  src[kOperateurs];
    bool porteur[kOperateurs];
};

inline const AlgoFM& algoFM (int i) noexcept
{
    static const AlgoFM t[kAlgosFM] = {
        // 0 : chaine 3->2->1->0. Le plus riche, le plus metallique.
        { { 1, 2, 3, -1 }, { true, false, false, false } },
        // 1 : 3->2->1 et 1->0, mais 2 sort aussi
        { { 1, 3, 3, -1 }, { true, false, true, false } },
        // 2 : deux modulateurs sur une porteuse
        { { 1, -1, 3, -1 }, { true, false, false, false } },
        // 3 : 3->2->0 et 1->0
        { { 1, -1, 3, -1 }, { true, false, true, false } },
        // 4 : deux paires independantes (cloche double)
        { { 1, -1, 3, -1 }, { true, false, true, false } },
        // 5 : un seul modulateur, trois porteuses (orgue module)
        { { 3, 3, 3, -1 }, { true, true, true, false } },
        // 6 : une paire plus deux sinus purs
        { { 1, -1, -1, -1 }, { true, false, true, true } },
        // 7 : quatre sinus en parallele - de l'additif a quatre partiels
        { { -1, -1, -1, -1 }, { true, true, true, true } },
    };
    return t[std::max (0, std::min (kAlgosFM - 1, i))];
}

struct ReglagesFM
{
    int    algo = 0;
    double ratio[kOperateurs]    = { 1.0, 2.0, 1.0, 3.5 };
    double niveau[kOperateurs]   = { 1.0, 0.6, 0.0, 0.4 };
    double retour = 0.0;      // retour de l'operateur 3 sur lui-meme, 0..1

    /** ⭐⭐ UNE ENVELOPPE COMPLETE PAR OPERATEUR.

        Avant, chaque operateur n'avait qu'une DECROISSANCE. C'est deja ce qui
        fait une cloche - le modulateur s'eteint plus vite que la porteuse, donc
        le son commence metallique et finit en sinus - mais ca ne fait qu'une
        seule famille de sons. Une attaque, une chute, une tenue et un
        relachement par operateur, c'est ce qui separe la cloche de
        l'electro-piano et de la basse.

        /!\ ON NE COPIE PAS LES HUIT PARAMETRES DU DX7 (R1-4 / L1-4). Son
        enveloppe vise des NIVEAUX successifs, ce qui autorise des formes qui
        remontent - superbe, et trente-deux nombres par couche que personne ne
        regle. On prend son COMPORTEMENT (une enveloppe par operateur, et le
        scaling ci-dessous), pas sa liste. */
    double attaque[kOperateurs]  = { 0.002, 0.002, 0.002, 0.002 };
    double decli[kOperateurs]    = { 0.60, 0.25, 0.30, 0.20 };
    double tenue[kOperateurs]    = { 0.80, 0.30, 0.40, 0.20 };
    double relache[kOperateurs]  = { 0.30, 0.30, 0.30, 0.30 };

    /** ⭐ LE SUIVI DE CLAVIER EN NIVEAU - le vrai secret du DX7.

        Le niveau d'un operateur baisse quand on monte le clavier. Sans ca, un
        modulateur qui sonne juste sur un do2 rend le do6 STRIDENT : l'indice de
        modulation est le meme, mais toutes ses bandes laterales sont montees
        avec la porteuse. C'est exactement ce qui sonne faux dans une FM naive.

        Signe : positif = plus doux en montant (le cas usuel), negatif =
        l'inverse. L'unite est le decibel par octave, jusqu'a 12. */
    double echelleNiveau[kOperateurs] = { 0.0, 0.35, 0.35, 0.35 };

    /** La note de reference du suivi : au-dessus on attenue, en dessous on
        renforce. Note MIDI, do3 = 60. */
    double pointBrisure = 60.0;

    /** ⭐ LE SUIVI DE CLAVIER EN VITESSE. Les enveloppes accelerent dans
        l'aigu, parce qu'un son aigu s'eteint plus vite dans la nature. 0 =
        aucune, 1 = deux fois plus rapide par octave. */
    double echelleVitesse = 0.0;

    /** ⭐⭐ LE DESACCORD PAR OPERATEUR - l'epaisseur du DX7.

        Sur un DX7 il vaut sept crans, soit quelques cents. C'est minuscule et
        c'est pourtant la moitie du son : deux operateurs au MEME rapport mais
        legerement desaccordes BATTENT, et ce battement lent est ce qui
        distingue un electro-piano vivant d'un electro-piano en carton.

        /!\ EN CENTS, PAS EN HERTZ. Un desaccord fixe en hertz serait enorme
        dans le grave et inaudible dans l'aigu - le battement d'un la0 durerait
        plusieurs secondes et celui d'un la6 serait un bourdonnement. En cents,
        le battement garde la meme allure musicale sur tout le clavier. */
    double desaccord[kOperateurs] = { 0.0, 0.0, 0.0, 0.0 };

    /** ⭐⭐ LA SENSIBILITE A LA VELOCITE, PAR OPERATEUR.

        C'est ce qui rend un electro-piano DX expressif, et ca n'a rien a voir
        avec un reglage de volume. Poser la vitesse sur le niveau d'un
        MODULATEUR fait que jouer plus fort rend plus BRILLANT ; la poser sur
        une porteuse fait seulement plus fort. Le DX7 les regle separement
        parce que ce sont deux gestes musicaux differents.

        0 = l'operateur ignore la vitesse (utile sur une porteuse qu'on veut
        stable), 1 = il en depend entierement. */
    double veloSens[kOperateurs] = { 0.0, 0.0, 0.0, 0.0 };
};

/** L'etage FM d'une voix. */
class EtageFM
{
public:
    void prepare (double fs) noexcept
    {
        fs_ = fs;
        for (auto& e : env_) e.prepare (fs);
        for (int i = 0; i < kOperateurs; ++i)
        { phase_[i] = 0.0; dt_[i] = 0.01; sortie_[i] = 0.0; }
        retour1_ = retour2_ = 0.0;
        n_ = 0;
    }

    /** `vel` est la vitesse normalisee 0..1 (voir Couche::velNormalisee). */
    void poseVelocite (const ReglagesFM& r, double vel) noexcept
    {
        const double v = std::max (0.0, std::min (1.0, vel));
        for (int i = 0; i < kOperateurs; ++i)
        {
            const double sens = std::max (0.0, std::min (1.0, r.veloSens[i]));
            // /!\ INTERPOLATION, PAS MULTIPLICATION. Ecrit `1 - sens (1 - v)`
            // et non `v^sens` : a sensibilite nulle le gain doit valoir
            // exactement 1, quelle que soit la vitesse. Une puissance donnerait
            // bien 1, mais une multiplication directe par `v` aurait fait
            // dependre de la vitesse un operateur regle pour l'ignorer.
            gainVelo_[i] = 1.0 - sens * (1.0 - v);
        }
    }

    void declenche (const ReglagesFM& r, double hz, double phase0) noexcept
    {
        for (int i = 0; i < kOperateurs; ++i)
        {
            phase_[i] = phase0;
            sortie_[i] = 0.0;
        }
        retour1_ = retour2_ = 0.0;
        n_ = 0;
        majHauteur (r, hz);
        for (int i = 0; i < kOperateurs; ++i) env_[i].declenche();
    }

    /** ⭐⭐ RELANCE DOUCE - le mode MONO.

        /!\ SANS ELLE, LA DEUXIEME NOTE EST PLUS FAIBLE QUE LA PREMIERE. Le
        mode mono ne coupe pas la voix : il relance l'enveloppe d'amplitude en
        gardant le son en cours, pour ne pas claquer. Mais depuis que chaque
        operateur a SA PROPRE enveloppe, relancer la seule amplitude ne suffit
        plus - les operateurs, eux, restaient la ou ils etaient. Avec une tenue
        basse ils sont deja a zero quand la deuxieme touche arrive, et elle sort
        beaucoup plus faible.

        Comme pour l'amplitude, on repart en ATTAQUE sans remettre le niveau a
        zero : c'est ce qui evite le clic. */
    void redeclenche() noexcept
    {
        for (auto& e : env_) e.redeclenche();
    }

    /** /!\ LE RELACHEMENT DOIT DESCENDRE JUSQU'AUX OPERATEURS. Sans ca, seule
        l'enveloppe d'amplitude de la couche relache, et le timbre reste fige a
        sa valeur de tenue pendant toute la queue - un son qui s'eteint sans
        jamais s'adoucir. */
    void relache() noexcept
    {
        for (auto& e : env_) e.relache();
    }

    void majHauteur (const ReglagesFM& r, double hz) noexcept
    {
        // ⭐ Combien d'octaves au-dessus du point de brisure ? C'est la seule
        // quantite dont dependent les deux suivis de clavier.
        const double hzBrisure = 440.0 * std::pow (2.0, (r.pointBrisure - 69.0) / 12.0);
        const double octaves = std::log2 (std::max (1e-6, hz) / std::max (1e-6, hzBrisure));

        // /!\ Le suivi en VITESSE multiplie les temps, il ne les remplace pas :
        // un patch dont toutes les enveloppes sont longues doit rester long,
        // simplement un peu moins en haut du clavier.
        const double kVitesse = std::pow (2.0, -r.echelleVitesse * octaves);

        for (int i = 0; i < kOperateurs; ++i)
        {
            // ⭐ Le desaccord, en cents : c'est lui qui fait battre deux
            // operateurs au meme rapport.
            const double cents = std::max (-100.0, std::min (100.0, r.desaccord[i]));
            const double f = std::max (0.01, hz * std::max (0.01, r.ratio[i])
                                                * std::pow (2.0, cents / 1200.0));
            dt_[i] = std::min (f, 0.49 * fs_) / fs_;

            // suivi en NIVEAU : des decibels par octave, bornes a +/- 36 dB
            const double dB = std::max (-36.0, std::min (36.0,
                                  -r.echelleNiveau[i] * 12.0 * octaves));
            gainEchelle_[i] = std::pow (10.0, dB / 20.0);

            env_[i].regle (std::max (0.0005, r.attaque[i] * kVitesse),
                           std::max (0.0005, r.decli[i]   * kVitesse),
                           r.tenue[i],
                           std::max (0.0005, r.relache[i] * kVitesse));

        }
    }

    inline double suivant (const ReglagesFM& r) noexcept
    {
        const AlgoFM& a = algoFM (r.algo);
        ++n_;

        double v = 0.0;
        // /!\ De 3 vers 0 : un modulateur a toujours un indice superieur, donc
        // sa sortie est deja calculee quand on arrive a celui qu'il module.
        for (int i = kOperateurs - 1; i >= 0; --i)
        {
            double mod = 0.0;
            if (a.src[i] >= 0) mod = sortie_[a.src[i]];
            if (i == kOperateurs - 1 && r.retour > 0.0)
            {
                // retour moyenne sur deux echantillons : c'est ce qui le rend
                // stable au lieu de partir en oscillation a la Nyquist.
                mod += r.retour * 0.5 * (retour1_ + retour2_);
            }

            // enveloppe de l'operateur, et son suivi de clavier
            const double e = env_[i].suivant() * gainEchelle_[i] * gainVelo_[i];

            // ⭐ MODULATION DE PHASE : on ajoute la modulante A LA PHASE, on
            // n'integre pas. La hauteur reste donc juste quoi qu'il arrive.
            // /!\ `mod` est en DEMI-TOURS (le kPi d'origine), donc 0,5 * mod
            // une fois converti en tours.
            const double s = sinTour (phase_[i] + 0.5 * mod);
            sortie_[i] = s * r.niveau[i] * e;

            phase_[i] += dt_[i];
            if (phase_[i] >= 1.0) phase_[i] -= std::floor (phase_[i]);

            if (a.porteur[i]) v += sortie_[i];
        }

        retour2_ = retour1_;
        retour1_ = sortie_[kOperateurs - 1];
        return v * 0.5;
    }

    /** La phase de l'oscillateur, 0..1 - la tete de lecture de l'interface. */
    /** La phase de la PORTEUSE (operateur 0) : c'est elle qui donne le cycle
        qu'on voit. Les modulateurs tournent a d'autres rapports. */
    double position() const noexcept { return phase_[0] - std::floor (phase_[0]); }

private:
    double fs_ = 44100.0;
    double phase_[kOperateurs] = {}, dt_[kOperateurs] = {}, sortie_[kOperateurs] = {};
    double retour1_ = 0.0, retour2_ = 0.0;
    Enveloppe env_[kOperateurs];
    double gainEchelle_[kOperateurs] = { 1.0, 1.0, 1.0, 1.0 };
    double gainVelo_[kOperateurs] = { 1.0, 1.0, 1.0, 1.0 };
    long long n_ = 0;
};

} // namespace romance
