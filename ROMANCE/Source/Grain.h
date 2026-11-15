// =============================================================================
//  Grain.h - La couche d'epoque de ROMANCE.
//
//  Portage du prototype Python `grain.py`, valide par mesure (T1/T2/T3) et par
//  ecoute. Ne depend que de <cmath> et <algorithm> : testable sans JUCE, sans
//  hote, sans compiler le plugin. Meme regle que XpertComp.h et
//  DetailClipper.h - tout algorithme non trivial sort de l'editeur, sinon il
//  n'est verifiable qu'a l'oeil.
//
//  Les trois briques, chacune mesuree :
//    1. LECTURE          horloge variable (drop-sample) | taux fixe + interpolation
//    2. RECONSTRUCTION   filtre asservi au taux, 24 dB/oct | fixe, 18 dB/oct
//    3. QUANTIFICATION   mu-law mu-255 (8 bits) | lineaire 8/12/16 bits
//
//  /!\ LE POINT LE MOINS INTUITIF, ET LE PLUS IMPORTANT :
//  sur les machines a horloge variable (Emulator II, PPG, Prophet VS, D-50),
//  transposer ne change pas le rythme de lecture en memoire, ca change la
//  FREQUENCE D'HORLOGE DU CONVERTISSEUR. Le filtre de reconstruction est
//  asservi a cette horloge, donc la reponse en frequence NE BOUGE PAS quand on
//  transpose. Mesure sur le prototype : rapport de coupure entre -24 et +12
//  demi-tons = 7,70x en asservi contre 3,24x en fixe (idéal 8,00x).
//  Un filtre de reconstruction FIXE donnerait une machine entierement
//  differente - c'est l'erreur a ne pas commettre.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace romance
{

// /!\ kPi n'est PAS standard en C++ : il n'existe que si <cmath> a ete inclus
// avec _USE_MATH_DEFINES. Les tests le definissaient, pas le plugin - d'ou une
// compilation qui passait d'un cote et echouait de l'autre. On declare la
// constante ici, et ces en-tetes redeviennent vraiment autonomes.
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- constantes

constexpr double kFsRomEII    = 27700.0; // Emulator II (source secondaire)
constexpr double kFsMachineJP = 32000.0; // sortie fixe des rompleurs japonais
constexpr double kCutoffRatio = 0.18;    // coupure du filtre de reconstruction,
                                         // en fraction de l'horloge du DAC.
// /!\ VALEUR TOUJOURS SUPPOSEE - aucune source ne la donne - mais ABAISSEE de
// 0,40 a 0,18 le 2026-08-19, sur DEUX indices concordants :
//   1. L'utilisateur a constate a l'oreille que le filtre asservi ne s'entend
//      qu'a la coupure la plus basse et sur la note la plus haute.
//   2. La mesure lui donne raison : ecart asservi/fixe de 0,1 dB a 0,40 contre
//      2,4 a 3,0 dB a 0,10. A 0,40 le filtre est AU-DESSUS de la bande de la
//      ROM (limitee a 0,45) : il n'a presque rien sur quoi agir.
// ⭐ Le mecanisme etait donc correct et INAUDIBLE - le pire des cas, parce
// qu'on ne peut pas le distinguer d'un bug. C'est l'utilisateur qui l'a
// attrape, pas mes tests, qui verifiaient la COUPURE CALCULEE et non le SON.

// Sur-echantillonnage interne de l'etage de grain.
// /!\ COMPROMIS ASSUME. Le prototype Python utilisait 8x pour representer
// l'escalier du DAC et ses images AVANT le filtre de reconstruction. En temps
// reel on ne peut pas se le permettre : 4x est le compromis. Les images qui
// depassent la Nyquist interne se replient au lieu de partir vers le tweeter
// comme sur la vraie machine.
constexpr int kSurEch = 4;

// ------------------------------------------------------- 3. QUANTIFICATION

/** ⭐ UNE TABLE DE SINUS, comme le DX7 - et pour la meme raison qu'en 1983.

    La modulation de phase demande UN sinus par operateur et par echantillon.
    A quatre operateurs, trois voix d'unisson et un sur-echantillonnage de
    quatre, cela fait 48 sinus pour UN echantillon de sortie, par voix. A seize
    voix et 44,1 kHz : 34 millions de `std::sin` par seconde.

    /!\ 4 096 POINTS AVEC INTERPOLATION LINEAIRE, pas 4 096 points bruts. Sans
    interpolation, l'erreur de troncature vaut 1/4096 de tour, ce qui s'entend
    comme un bruit de fond a -72 dB - juste au-dessus du plancher de
    quantification 8 bits qu'on simule par ailleurs, donc parfaitement audible.
    Avec interpolation, l'erreur tombe sous -100 dB : inaudible, y compris a
    travers le grain.
*/
inline const float* tableSinus() noexcept
{
    static const std::vector<float> t = [] ()
    {
        std::vector<float> v (4097);          // +1 : le point de bouclage
        for (int i = 0; i < 4097; ++i)
            v[(size_t) i] = (float) std::sin (2.0 * kPi * (double) i / 4096.0);
        return v;
    }();
    return t.data();
}

/** Sinus d'un angle exprime en TOURS (1 = 2 pi). */
inline double sinTour (double u) noexcept
{
    u -= std::floor (u);
    const double x = u * 4096.0;
    const int i = (int) x;
    const double f = x - (double) i;
    const float* t = tableSinus();
    return (double) t[i] + f * ((double) t[i + 1] - (double) t[i]);
}

/** ADSR a segments exponentiels.
    /!\ Le taux de rafraichissement grossier des enveloppes fait partie du grain
    d'epoque (mouvement « en escalier »). Il sera ajoute comme propriete de
    machine, pas comme defaut ici.
*/
class Enveloppe
{
public:
    void prepare (double fs) noexcept { fs_ = fs; maj(); }

    void regle (double attaqueS, double decliS, double tenue, double relacheS) noexcept
    {
        a_ = std::max (0.0005, attaqueS);
        d_ = std::max (0.0005, decliS);
        s_ = std::max (0.0, std::min (1.0, tenue));
        r_ = std::max (0.0005, relacheS);
        maj();
    }

    void declenche() noexcept { etat_ = Attaque; v_ = 0.0; }

    /** Redeclenche SANS repartir de zero.

        /!\ La difference s'entend : repartir de zero sur une note deja en
        train de sonner cree un saut d'amplitude, donc un clic. Ici on relance
        la phase d'attaque en gardant le niveau atteint.
    */
    void redeclenche() noexcept { etat_ = Attaque; }
    void relache()   noexcept { if (etat_ != Repos) { etat_ = Relache; cr_ = crLent_; } }
    void coupe()     noexcept { etat_ = Repos; v_ = 0.0; }
    bool active() const noexcept { return etat_ != Repos; }

    /** Extinction rapide, pour le VOL DE VOIX.

        Couper net une voix volee cree une discontinuite pleine echelle : c'est
        la source de craquements la plus courante d'un synthe polyphonique, et
        elle ne coute presque rien en CPU - ce qui explique qu'on entende craquer
        alors que le processeur ne fait rien.
        Les machines d'epoque volaient aussi des voix, mais le convertisseur et
        l'ampli lissaient le saut. Ici il faut le faire explicitement.
    */
    void extinctionRapide (double secondes = 0.006) noexcept
    {
        if (etat_ == Repos) return;
        etat_ = Relache;
        cr_ = 1.0 - std::exp (-1.0 / (std::max (0.0005, secondes) * fs_));
    }

    inline double suivant() noexcept
    {
        switch (etat_)
        {
            case Attaque:
                v_ += ca_ * (1.2 - v_);
                if (v_ >= 1.0) { v_ = 1.0; etat_ = Decli; }
                break;
            case Decli:
                v_ += cd_ * (s_ - 0.2 - v_);
                if (v_ <= s_ + 1e-4) { v_ = s_; etat_ = Tenue; }
                break;
            case Tenue:  v_ = s_; break;
            case Relache:
                v_ += cr_ * (-0.2 - v_);
                if (v_ <= 1e-4) { v_ = 0.0; etat_ = Repos; }
                break;
            case Repos: v_ = 0.0; break;
        }
        return v_;
    }

private:
    enum Etat { Repos, Attaque, Decli, Tenue, Relache };
    void maj() noexcept
    {
        ca_ = 1.0 - std::exp (-1.0 / (a_ * fs_));
        cd_ = 1.0 - std::exp (-1.0 / (d_ * fs_));
        crLent_ = 1.0 - std::exp (-1.0 / (r_ * fs_));
        cr_ = crLent_;
    }
    double fs_ = 44100.0;
    double a_ = 0.01, d_ = 0.2, s_ = 0.7, r_ = 0.3;
    double ca_ = 0.0, cd_ = 0.0, cr_ = 0.0, crLent_ = 0.0, v_ = 0.0;
    Etat etat_ = Repos;
};

enum class Quant { Aucune, MuLaw, Lin8, Lin12, Lin16 };

/** Encodage G.711 mu-law (mu = 255).

    Structure : 1 bit de signe + 3 bits de segment + 4 bits de pas.
    8 segments par polarite, 16 pas par segment, le pas DOUBLE d'un segment au
    suivant. C'est ce doublement qui fait que le bruit de quantification SUIT le
    signal - mesure sur le prototype : erreur a -42,4 dB sur un signal fort et
    -78,2 dB sur un signal faible, soit 35,8 dB d'ecart, contre 1,1 dB pour du
    8 bits lineaire. Le companding ne REDUIT pas le bruit, il le DEPLACE la ou
    le signal le masque.
*/
inline unsigned char muLawEncode (double x) noexcept
{
    constexpr int kBias = 0x84;
    constexpr int kClip = 32635;

    int xi = (int) std::lround (x * 32768.0);
    xi = std::max (-kClip, std::min (kClip, xi));

    const int sign = (xi < 0) ? 0x80 : 0x00;
    int mag = std::abs (xi) + kBias;

    int seg = 0;
    for (int s = 1; s < 8; ++s)
        if (mag >= (1 << (s + 7)))
            seg = s;

    const int mant = (mag >> (seg + 3)) & 0x0F;
    return (unsigned char) (~(sign | (seg << 4) | mant) & 0xFF);
}

inline double muLawDecode (unsigned char u) noexcept
{
    constexpr int kBias = 0x84;
    const int v = (~(int) u) & 0xFF;
    int t = (((v & 0x0F) << 3) + kBias) << ((v & 0x70) >> 4);
    t -= kBias;
    return ((v & 0x80) != 0 ? -t : t) / 32768.0;
}

/** Quantifie un echantillon de ROM.
    /!\ S'applique a la ROM (le materiau STOCKE), jamais au signal de sortie.
*/
inline double quantifie (double x, Quant q) noexcept
{
    switch (q)
    {
        case Quant::Aucune: return x;
        case Quant::MuLaw:  return muLawDecode (muLawEncode (x));
        case Quant::Lin8:
        case Quant::Lin12:
        case Quant::Lin16:
        {
            const int bits = (q == Quant::Lin8) ? 8 : (q == Quant::Lin12 ? 12 : 16);
            const double pas = 2.0 / std::pow (2.0, bits);
            const double y = std::round (x / pas) * pas;
            return std::max (-1.0, std::min (1.0 - pas, y));
        }
    }
    return x;
}

// -------------------------------------------------------- 2. RECONSTRUCTION

/** Un biquad, forme directe transposee II. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline double traite (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Passe-bas du second ordre, transformee bilineaire, Q donne. */
    void passeBas (double fc, double fs, double q) noexcept
    {
        fc = std::max (10.0, std::min (fc, 0.49 * fs));
        const double k  = std::tan (kPi * fc / fs);
        const double kk = k * k;
        const double n  = 1.0 + k / q + kk;
        b0 = kk / n;
        b1 = 2.0 * b0;
        b2 = b0;
        a1 = 2.0 * (kk - 1.0) / n;
        a2 = (1.0 - k / q + kk) / n;
    }
};

/** Un pole, pour completer les ordres impairs.

    /!\ MEME BUG QUE L'EPAULE D'AIR DE XPERT comp, retrouve ici par le test.
    La premiere version ecrivait `z += a * (x - z)` avec a = k/(1+k). Or ce
    coefficient est celui du passe-bas BILINEAIRE, qui possede un ZERO a la
    Nyquist ; la structure `z += a(x-z)` n'en a pas. Resultat : -7,26 dB a la
    coupure au lieu de -3, donc un troisieme ordre qui mordait bien trop tot.
    *Le probleme n'est pas le nombre, c'est la FORME.*
    Forme correcte : y = c(x + x-1) + d*y-1.
*/
struct UnPole
{
    double c = 0.0, d = 0.0, xz = 0.0, yz = 0.0;
    void reset() noexcept { xz = yz = 0.0; }
    void regle (double fc, double fs) noexcept
    {
        fc = std::max (10.0, std::min (fc, 0.49 * fs));
        const double k = std::tan (kPi * fc / fs);
        c = k / (1.0 + k);
        d = (1.0 - k) / (1.0 + k);
    }
    inline double traite (double x) noexcept
    {
        const double y = c * (x + xz) + d * yz;
        xz = x; yz = y;
        return y;
    }
};

/** Filtre de reconstruction Butterworth.

    ordre 4 = 24 dB/oct - le SSM2045 de l'Emulator II, qui est AUSSI son filtre
              musical (meme composant). Deux biquads, Q = 0,5412 et 1,3066.
    ordre 3 = 18 dB/oct - le passe-bas numerique mobile de l'Akai S1000.
              Un biquad Q = 1 et un pole.

    /!\ C'est la PENTE qui distingue les machines, pas la profondeur de bits.
*/
class Reconstruction
{
public:
    void reset() noexcept { b1.reset(); b2.reset(); p.reset(); }

    void regle (double fc, double fs, int ordre) noexcept
    {
        ordre_ = (ordre == 3) ? 3 : 4;
        if (ordre_ == 4)
        {
            b1.passeBas (fc, fs, 0.54119610); // 1 / (2 cos(pi/8))
            b2.passeBas (fc, fs, 1.30656296); // 1 / (2 cos(3pi/8))
        }
        else
        {
            b1.passeBas (fc, fs, 1.0);
            p.regle (fc, fs);
        }
    }

    inline double traite (double x) noexcept
    {
        return (ordre_ == 4) ? b2.traite (b1.traite (x))
                             : p.traite (b1.traite (x));
    }

private:
    Biquad b1, b2;
    UnPole p;
    int ordre_ = 4;
};

// --------------------------------------------------------------- 1. LECTURE

enum class Horloge { Variable, Fixe };
enum class Filtre  { Asservi, Fixe };

struct ConfigGrain
{
    bool    actif   = true;          // /!\ `PROPRE` est une position de plein
                                     // droit, pas un outil de test : on doit
                                     // pouvoir retirer le grain partout.
    Horloge horloge = Horloge::Variable;
    Filtre  filtre  = Filtre::Asservi;
    Quant   quant   = Quant::MuLaw;
    double  fsRom   = kFsRomEII;
    double  fsMachine = kFsMachineJP;
    int     ordre   = 4;
    double  coupure = kCutoffRatio;
};

/** Lecteur d'une ROM, echantillon par echantillon, SANS filtre.

    /!\ LE FILTRE DE RECONSTRUCTION N'EST PLUS ICI. Il etait a l'origine dans
    chaque lecteur, donc dans chaque voix d'unisson : 4 couches x 3 voix x 16
    notes = 192 lecteurs, soit **74 millions d'operations de biquad par seconde**
    a 48 kHz. Injouable. Il est remonte au niveau de la COUCHE (voir Couche.h),
    ce qui divise le cout par trois.

    ⚠️ C'est une APPROXIMATION assumee : sur la vraie machine chaque voix a son
    convertisseur ET son filtre. Partager le filtre entre les voix d'un meme
    unisson est defendable - elles ne different que de quelques cents, donc leurs
    horloges sont quasi identiques - mais ce n'est pas exact, et il faut le
    savoir avant de conclure quoi que ce soit d'une ecoute.

    Deux modes :
      - horloge VARIABLE : lecture drop-sample, aucune interpolation, jamais.
        Le maintien d'ordre zero du DAC est implicite.
      - taux FIXE        : interpolation lineaire, comme les machines japonaises
        d'apres le D-50.
*/
class Lecteur
{
public:
    void prepare (double fsInterne) noexcept
    {
        fsInterne_ = fsInterne;
        phase_ = 0.0;
    }

    void reglePhase (double p) noexcept { phase_ = p; }
    /** Ou en est la lecture, en echantillons. Sert a la TETE DE LECTURE de
        l'interface - et, plus tard, a poser les points de boucle a la souris. */
    double position() const noexcept { return phase_; }

    void majHorloge (const ConfigGrain& cfg, double ratio) noexcept
    {
        // Le pas de lecture est le meme dans les deux modes : c'est ce qui donne
        // la bonne hauteur. Ce qui change, c'est l'INTERPOLATION - deux machines
        // peuvent lire la meme memoire au meme rythme sans sonner pareil.
        pas_ = cfg.fsRom * ratio / fsInterne_;
    }

    /** Points de boucle, en echantillons de ROM.

        /!\ SANS BOUCLE, UN ROMPLEUR N'EN EST PAS UN. La premiere version
        bloquait la phase a la fin de la ROM : une nappe s'arretait au bout de
        deux secondes, et bien plus tot des qu'on transposait vers le haut.
        ⭐ Et les BOUCLES COURTES sont un mecanisme d'epoque a part entiere -
        c'est l'un des cinq facteurs de grain qu'on avait releves.
        ⚠️ Aucun fondu de boucle : une ROM bien faite boucle sur un nombre
        ENTIER de cycles. C'est au materiau d'etre juste, pas au lecteur de
        rattraper - exactement comme sur les machines.
    */
    void regleBoucle (int debut, int fin) noexcept
    {
        bDebut_ = std::max (0, debut);
        bFin_   = std::max (bDebut_ + 2, fin);
    }

    /** Un pas au taux INTERNE. Aucun filtrage : la couche s'en charge. */
    /** ⭐ LE FONDU DE BOUCLE, en fraction de la longueur de boucle.

        /!\ C'ETAIT LE MANQUE LE PLUS VOYANT DU LECTEUR, et le fichier le disait
        lui-meme : « Aucun fondu de boucle ». Sur un Akai S1000 comme sur un
        Emulator II, c'est le travail d'edition principal - une boucle sans
        fondu claque a chaque tour des que le materiau n'y revient pas en phase.
        Une ROM de recette y revient (elle est construite pour), mais une forme
        d'onde arbitraire, non.
    */
    void regleFondu (double fraction) noexcept
    { fondu_ = std::max (0.0, std::min (0.5, fraction)); }

    inline double pasInterne (const double* rom, int nRom,
                              const ConfigGrain& cfg) noexcept
    {
        const double fin = (bFin_ > 0) ? (double) std::min (bFin_, nRom)
                                       : (double) nRom;
        const double deb = (double) std::min (bDebut_, (int) fin - 2);
        const double lg = std::max (1.0, fin - deb);

        auto lis = [&] (double ph) noexcept
        {
            const int i0 = (int) ph;
            if (cfg.actif && cfg.horloge == Horloge::Variable)
                return rom[std::max (0, std::min (i0, nRom - 1))];   // drop-sample
            const double f = ph - (double) i0;
            const int ia = std::max (0, std::min (i0, nRom - 1));
            const int ib = std::max (0, std::min (i0 + 1, nRom - 1));
            return rom[ia] * (1.0 - f) + rom[ib] * f;                // interpole
        };

        double e = lis (phase_);

        // ⭐ LE FONDU : dans les derniers echantillons avant la fin de boucle, on
        // melange avec ce qui sera lu APRES le retour au debut.
        //
        // /!\ FONDU EN PUISSANCE (racine), pas lineaire. Deux copies decorrelees
        // du meme materiau s'additionnent en puissance : un fondu lineaire
        // creuserait le niveau de 3 dB au milieu du croisement, et on entendrait
        // un trou a chaque tour de boucle - en voulant supprimer un clic.
        const int nF = (int) (fondu_ * lg);
        if (nF > 1 && phase_ > fin - (double) nF)
        {
            const double u = (phase_ - (fin - (double) nF)) / (double) nF;
            const double aA = std::sqrt (std::max (0.0, 1.0 - u));
            const double aB = std::sqrt (std::max (0.0, u));
            // /!\ ON LIT LE MATERIAU QUI PRECEDE LE DEBUT DE BOUCLE, pas
            // celui qui le suit. Ma premiere version lisait `deb + x` : au
            // moment du retour elle en etait donc a `deb + nF`, alors que
            // l'echantillon suivant repart de `deb` - le fondu CREAIT un saut
            // de nF echantillons la ou il devait en supprimer un. Mesure : le
            // pas maximal passait de 2,7 a 4,5 fois le pas moyen.
            //
            // Avec `deb - nF + x`, le fondu arrive exactement sur `deb` au
            // moment ou la lecture y revient : les deux copies sont alors en
            // phase, et le raccord est continu par construction.
            const double avant = deb - (double) nF
                               + (phase_ - (fin - (double) nF));
            e = e * aA + lis (std::max (0.0, avant)) * aB;
        }

        phase_ += pas_;
        if (phase_ >= fin)
            phase_ = deb + std::fmod (phase_ - deb, lg);
        return e;
    }

    bool fini (int nRom) const noexcept { return phase_ >= (double) (nRom - 1); }

private:
    double fsInterne_ = 176400.0, phase_ = 0.0, pas_ = 1.0, fondu_ = 0.0;
    int bDebut_ = 0, bFin_ = 0;
};

/** L'etage de reconstruction d'UNE couche : filtre asservi + decimation.

    C'est ici qu'on paie le sur-echantillonnage, une fois par couche et non par
    voix d'unisson.
*/
class EtageReconstruction
{
public:
    void prepare (double fsSortie) noexcept
    {
        fsSortie_  = fsSortie;
        fsInterne_ = fsSortie * (double) kSurEch;
        for (int c = 0; c < 2; ++c)
        {
            recon_[c].reset();
            decim_[c].reset();
            decim_[c].regle (0.45 * fsSortie_, fsInterne_, 4);
        }
    }

    double fsInterne() const noexcept { return fsInterne_; }
    double coupureCourante() const noexcept { return fc_; }

    void majHorloge (const ConfigGrain& cfg, double ratio) noexcept
    {
        const double fsDac = (cfg.horloge == Horloge::Variable)
                               ? cfg.fsRom * ratio
                               : cfg.fsMachine;
        // /!\ Une machine a taux FIXE ne peut pas avoir de filtre asservi : son
        // horloge ne bouge pas. `Asservi` n'a de sens que sur `Variable`.
        const double ref = (cfg.horloge == Horloge::Variable)
                             ? ((cfg.filtre == Filtre::Asservi) ? fsDac : cfg.fsRom)
                             : cfg.fsMachine;
        fc_ = cfg.coupure * ref;
        for (int c = 0; c < 2; ++c) recon_[c].regle (fc_, fsInterne_, cfg.ordre);
    }

    // /!\ DEUX filtres au lieu d'un : c'est le prix du stereo. Panoramiquer
    // APRES le filtre reviendrait a filtrer une somme mono, donc a perdre la
    // largeur d'unisson. Deux filtres par couche restent trois fois moins chers
    // qu'un filtre par voix d'unisson - et c'est PLUS proche de la machine, ou
    // chaque voix a son propre convertisseur et son propre filtre.
    inline double reconstruit (int c, double x) noexcept { return recon_[c].traite (x); }
    inline double decime      (int c, double x) noexcept { return decim_[c].traite (x); }

private:
    double fsSortie_ = 44100.0, fsInterne_ = 176400.0, fc_ = 0.0;
    Reconstruction recon_[2], decim_[2];
};

} // namespace romance
