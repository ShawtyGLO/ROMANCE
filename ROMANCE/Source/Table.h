// =============================================================================
//  Table.h - Le moteur a TABLE D'ONDES (PPG Wave, Prophet VS, Ensoniq VFX).
//
//  ⭐ CE QUI DISTINGUE UNE TABLE D'ONDES D'UNE ROM. Une ROM contient un son
//  entier qu'on relit ; une table contient N formes d'onde d'UN SEUL CYCLE
//  qu'on parcourt. Le geste caracteristique n'est donc pas la lecture, c'est le
//  BALAYAGE : passer de l'image 3 a l'image 47 pendant que la note tient. C'est
//  ce que le PPG appelait « wavetable sweep » et l'Ensoniq « transwave », et
//  aucun des deux autres moteurs ne sait le faire.
//
//  /!\ ET C'EST UN MECANISME DE LECTURE DIFFERENT, pas une ROM deguisee. Le
//  PPG saute d'une image a l'autre SANS interpoler (c'est son grain, ces petits
//  crans dans le balayage) ; le Prophet VS, lui, fond entre quatre formes. On
//  offre les deux, parce que ce sont deux sons et deux epoques.
//
//  /!\ LES IMAGES SONT LIMITEES EN BANDE, chacune selon la note jouee. Une
//  table brute contient des harmoniques jusqu'a la moitie de sa longueur : lue
//  a 2 kHz, elle replie tout. On synthetise donc chaque image en additif au
//  moment ou la note demarre, en s'arretant a Nyquist - c'est ce que faisait
//  deja `remplitRomForme`, applique par image.
//
//  Ne depend que de <cmath>, <algorithm> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include "FM.h"
#include "Dessin.h"

#include "Grain.h"   // kPi

namespace romance
{

constexpr int kImagesTable = 16;      // images par table
constexpr int kLongueurImage = 512;   // echantillons par cycle

enum class LectureTable { Saut, Fondu };   // PPG / Prophet VS

/** Les familles de tables. Ce ne sont pas des « presets » : chacune est une
    facon differente de faire evoluer un spectre, et c'est l'evolution qui fait
    le son d'une table d'ondes. */
enum class FamilleTable { Harmoniques, Formants, Impaires, Bruitee, Dessin };

/** ⭐ Les quatre premieres sont PROCEDURALES : elles ne dependent que de la
    famille et de l'octave, donc une banque partagee par tout le processus
    suffit. La cinquieme est DESSINEE : elle appartient au patch, elle change
    quand l'utilisateur clique, et elle a donc sa propre banque par couche
    (voir BanqueDessin). C'est la meme separation que ROM contre forme. */
constexpr int kFamillesProcedurales = 4;

class BanqueDessin;

struct ReglagesTable
{
    FamilleTable famille = FamilleTable::Harmoniques;
    LectureTable lecture = LectureTable::Fondu;
    double position = 0.0;      // 0..1 - ou l'on se trouve dans la table
    double symetrie = 0.5;      // deforme la phase de lecture (miroir PPG)

    /** ⭐ La banque dessinee de CETTE couche, quand la famille est Dessin.

        /!\ C'est un pointeur vers un membre stable du processeur, jamais vers
        un tampon qu'on realloue : la banque est ecrite en DOUBLE TAMPON (on
        remplit celle que personne ne lit, puis on bascule), exactement comme
        les ROM. Un pointeur ici ne peut donc pas pendre. */
    const BanqueDessin* dessin = nullptr;
};

/** La table elle-meme : kImagesTable cycles, fabriques pour une note donnee. */
class Table
{
public:
    /** ⭐ Fabriquee POUR LA NOTE : on s'arrete a Nyquist, donc une note aigue
        recoit des images plus pauvres qu'une note grave. C'est exactement ce
        que faisait une machine a table d'ondes - ses tables etaient stockees
        en plusieurs versions selon l'octave. */
    void fabrique (FamilleTable f, double hz, double fs)
    {
        // ⭐ ON NE REFABRIQUE QU'AU CHANGEMENT D'OCTAVE.
        //
        // /!\ Le seuil etait de 3 %, soit un demi-ton : un vibrato un peu large
        // refabriquait donc la table a chaque pas de controle - 737 000 sinus,
        // 2 756 fois par seconde. La table ne sert qu'a limiter les harmoniques
        // sous Nyquist ; ce plafond ne change pas de facon audible pour un
        // demi-ton, il change pour une OCTAVE.
        //
        // On fabrique donc pour le HAUT de l'octave (hz arrondi vers le haut a
        // la puissance de deux) : conservateur du bon cote - une note un peu
        // plus sourde que necessaire ne s'entend pas, du repliement si.
        const double hzOct = std::pow (2.0, std::ceil (std::log2 (std::max (20.0, hz))));
        if (f == famille_ && std::abs (hzOct - hz_) < 1e-6 && ! donnees_.empty())
            return;                       // meme famille, meme octave
        famille_ = f; hz_ = hzOct;

        donnees_.assign ((size_t) (kImagesTable * kLongueurImage), 0.0f);
        const int nHarmMax = std::max (1, (int) (0.45 * fs / hz_));

        for (int im = 0; im < kImagesTable; ++im)
        {
            const double u = (double) im / (double) (kImagesTable - 1);   // 0..1
            float* p = donnees_.data() + (size_t) im * kLongueurImage;
            double crete = 1e-9;

            // ⭐⭐ LES POIDS SE CALCULENT UNE FOIS PAR IMAGE, PAS PAR POINT.
            //
            // /!\ `poids (f, k, u)` ne depend PAS de l'echantillon - et elle
            // contient un `std::pow` ou un `std::exp`. Elle etait appelee dans
            // la boucle la plus interne : 512 x 90 fois par image au lieu de
            // 90, soit 737 000 transcendantes pour fabriquer UNE table. C'est
            // exactement la faute du piege 27, au meme endroit du raisonnement -
            // une valeur invariante calculee dans la boucle.
            //
            // Mesure : poser une note en table d'ondes coutait 34,4 ms sur le
            // fil audio, et un accord de cinq notes 147 ms - cinquante fois la
            // duree d'un bloc. Le decrochage etait garanti.
            const int nH = std::min (nHarmMax, kLongueurImage / 2);
            std::vector<double> a ((size_t) nH + 1, 0.0);
            int kMax = 0;
            for (int k = 1; k <= nH; ++k)
            {
                a[(size_t) k] = poids (f, k, u);
                // ⭐ On retient le RANG LE PLUS HAUT qui compte : les familles
                // eteignent leurs harmoniques au-dela de 1 + 40 u, donc a
                // position basse il n'y en a qu'une poignee a sommer.
                if (a[(size_t) k] > 1e-5) kMax = k;
            }

            for (int i = 0; i < kLongueurImage; ++i)
            {
                const double ph = (double) i / (double) kLongueurImage;
                double v = 0.0;
                for (int k = 1; k <= kMax; ++k)
                {
                    if (a[(size_t) k] <= 1e-5) continue;
                    // /!\ La table de sinus (voir FM.h) : cinq fois plus
                    // rapide, erreur -130 dB.
                    v += a[(size_t) k] * sinTour ((double) k * ph);
                }
                p[i] = (float) v;
                crete = std::max (crete, std::abs (v));
            }
            const float g = (float) (0.9 / crete);
            for (int i = 0; i < kLongueurImage; ++i) p[i] *= g;
        }
    }

    /** ⭐⭐ FABRIQUE LES SEIZE IMAGES A PARTIR D'UN CYCLE DESSINE.

        Le balayage doit vouloir dire quelque chose, meme ici. Les seize images
        sont le MEME dessin, coupe de plus en plus bas : image 0 = le dessin
        entier (tout ce qui tient sous Nyquist), image 15 = la fondamentale
        seule.

        /!\ L'AXE EST INVERSE PAR RAPPORT AUX FAMILLES PROCEDURALES, et c'est
        voulu. Ailleurs, position 0 = sinus et position 1 = riche. Ici,
        position 0 doit rendre LE DESSIN : quelqu'un qui vient de dessiner une
        forme et qui entend un sinus croit avoir casse son patch. Le balayage
        devient donc un adoucissement.
    */
    /** ⭐⭐ DEUX SPECTRES : l'image `im` est le fondu de A vers B.

        /!\ ET LE FONDU SE FAIT SUR LES HARMONIQUES, PAS SUR LES ECHANTILLONS.
        Melanger deux formes d'onde point par point donne une troisieme forme
        qui n'est ni l'une ni l'autre - avec des annulations la ou les deux
        dessins sont en opposition de phase. Melanger leurs SPECTRES fait
        entendre chaque partiel passer de son niveau A a son niveau B, ce qui
        est le balayage qu'on veut. */
    void fabriqueDepuisDeuxSpectres (const SpectreDessin& sa, const SpectreDessin& sb,
                                     double hz, double fs)
    {
        const double hzOct = std::pow (2.0, std::ceil (std::log2 (std::max (20.0, hz))));
        famille_ = FamilleTable::Dessin; hz_ = hzOct;

        donnees_.assign ((size_t) (kImagesTable * kLongueurImage), 0.0f);
        const int nyq = std::max (1, (int) (0.45 * fs / hz_));
        const int nH = std::min (std::min (nyq, kLongueurImage / 2),
                                 std::max (1, std::max (sa.kMax, sb.kMax)));

        for (int im = 0; im < kImagesTable; ++im)
        {
            const double u = (double) im / (double) (kImagesTable - 1);
            float* p = donnees_.data() + (size_t) im * kLongueurImage;
            double crete = 1e-9;
            for (int i = 0; i < kLongueurImage; ++i)
            {
                const double ph = (double) i / (double) kLongueurImage;
                double v = 0.0;
                for (int k = 1; k <= nH; ++k)
                {
                    const double as = sa.as[k] * (1.0 - u) + sb.as[k] * u;
                    const double ac = sa.ac[k] * (1.0 - u) + sb.ac[k] * u;
                    v += as * sinTour ((double) k * ph)
                       + ac * sinTour ((double) k * ph + 0.25);
                }
                p[i] = (float) v;
                crete = std::max (crete, std::abs (v));
            }
            const float g = (float) (0.9 / crete);
            for (int i = 0; i < kLongueurImage; ++i) p[i] *= g;
        }
    }

    void fabriqueDepuisSpectre (const SpectreDessin& sp, double hz, double fs)
    {
        const double hzOct = std::pow (2.0, std::ceil (std::log2 (std::max (20.0, hz))));
        famille_ = FamilleTable::Dessin; hz_ = hzOct;

        donnees_.assign ((size_t) (kImagesTable * kLongueurImage), 0.0f);
        const int nyq = std::max (1, (int) (0.45 * fs / hz_));
        const int nH = std::min (std::min (nyq, kLongueurImage / 2),
                                 std::max (1, sp.kMax));

        for (int im = 0; im < kImagesTable; ++im)
        {
            const double u = (double) im / (double) (kImagesTable - 1);   // 0..1
            // image 0 -> nH harmoniques ; image 15 -> une seule
            const int kCoupe = std::max (1, (int) std::lround (
                                   std::pow ((double) nH, 1.0 - u)));
            float* p = donnees_.data() + (size_t) im * kLongueurImage;
            double crete = 1e-9;
            for (int i = 0; i < kLongueurImage; ++i)
            {
                const double ph = (double) i / (double) kLongueurImage;
                double v = 0.0;
                for (int k = 1; k <= kCoupe; ++k)
                    v += sp.as[k] * sinTour ((double) k * ph)
                       + sp.ac[k] * sinTour ((double) k * ph + 0.25);
                p[i] = (float) v;
                crete = std::max (crete, std::abs (v));
            }
            // /!\ On normalise PAR IMAGE, comme les familles procedurales :
            // sans ca, adoucir le dessin ferait aussi baisser le niveau, et on
            // croirait a un reglage de volume cache.
            const float g = (float) (0.9 / crete);
            for (int i = 0; i < kLongueurImage; ++i) p[i] *= g;
        }
    }

    /** Lit la table a la position `u` (0..1) et a la phase `ph` (0..1). */
    inline double lit (double u, double ph, LectureTable mode) const noexcept
    {
        if (donnees_.empty()) return 0.0;
        const double x = std::max (0.0, std::min (1.0, u)) * (double) (kImagesTable - 1);
        const int i0 = (int) x;
        const int i1 = std::min (i0 + 1, kImagesTable - 1);
        const double f = x - (double) i0;

        // /!\ Le PPG SAUTE d'une image a l'autre. Ces petits crans dans le
        // balayage sont sa signature, pas un defaut a lisser.
        const int im = (mode == LectureTable::Saut) ? ((f < 0.5) ? i0 : i1) : i0;

        const double pp = ph - std::floor (ph);
        const double xi = pp * (double) kLongueurImage;
        const int j0 = (int) xi;
        const int j1 = (j0 + 1) % kLongueurImage;
        const double fj = xi - (double) j0;

        auto ech = [&] (int image)
        {
            const float* p = donnees_.data() + (size_t) image * kLongueurImage;
            return (double) p[j0] * (1.0 - fj) + (double) p[j1] * fj;
        };

        if (mode == LectureTable::Saut) return ech (im);
        return ech (i0) * (1.0 - f) + ech (i1) * f;
    }

    bool prete() const noexcept { return ! donnees_.empty(); }

private:
    /** Le poids de l'harmonique k a la position u dans la table. C'est ICI
        que vit le caractere d'une famille : pas dans une forme d'onde, dans la
        FACON dont le spectre se deplace quand on balaye. */
    static double poids (FamilleTable f, int k, double u) noexcept
    {
        const double n = (double) k;
        switch (f)
        {
            case FamilleTable::Harmoniques:
                // de la sinusoide (u=0) a la dent de scie (u=1)
                return (n <= 1.0 + u * 40.0) ? std::pow (n, -1.0) : 0.0;

            case FamilleTable::Impaires:
                // du sinus au carre : seules les impaires survivent
                if ((k % 2) == 0) return 0.0;
                return (n <= 1.0 + u * 40.0) ? std::pow (n, -1.0) : 0.0;

            case FamilleTable::Formants:
            {
                // une bosse qui MONTE dans le spectre quand on balaye : c'est
                // le transwave de l'Ensoniq, et rien d'autre ne fait ca
                const double centre = 1.0 + u * 18.0;
                const double d = (n - centre) / 2.5;
                return std::exp (-d * d) * std::pow (n, -0.35);
            }

            case FamilleTable::Bruitee:
            default:
            {
                // spectre pseudo-aleatoire mais DETERMINISTE : la meme table
                // doit sortir identique a chaque fois, sinon un preset ne
                // rappellerait pas le meme son
                unsigned int e = (unsigned int) (k * 2654435761u
                                   + (unsigned int) (u * 15.999) * 40503u);
                e = e * 1664525u + 1013904223u;
                const double r = (double) (e >> 8) / 16777216.0;
                return (n <= 1.0 + u * 30.0) ? r * std::pow (n, -0.8) : 0.0;
            }
        }
    }

    std::vector<float> donnees_;
    FamilleTable famille_ = FamilleTable::Harmoniques;
    double hz_ = 0.0;
};

/** L'etage table d'ondes d'une voix. */
/** ⭐⭐ LA BANQUE PARTAGEE DE TABLES.

    Une table ne depend que de deux choses : sa FAMILLE et l'OCTAVE de la note
    (qui fixe le nombre d'harmoniques tenant sous Nyquist). Il y en a donc
    quatre fois douze en tout - pas une par voix, pas une par note.

    /!\ C'EST LA MEME LECON QUE POUR LA ROM, ET ELLE A ETE REAPPRISE A LA
    DURE. Le materiau appartient au PATCH, pas a la voix. Chaque `EtageTable`
    fabriquait la sienne au declenchement : seize images de 512 points, jusqu'a
    90 harmoniques. Mesure : 34,4 ms sur le fil audio pour poser UNE note, et
    147 ms pour un accord de cinq - cinquante fois la duree d'un bloc.

    Ici tout est fabrique UNE FOIS, a `prepareToPlay`, sur le fil message. Une
    note posee ne fait plus que lire.

    /!\ `prepare` doit etre appelee depuis le fil message. Les voix ne font que
    LIRE la banque, et elle ne bouge plus une fois prete - c'est ce qui rend le
    partage sans verrou correct.
*/
constexpr int kOctavesTable = 12;

class BanqueTables
{
public:
    /** Fabrique tout. Cout mesure : environ 250 ms, une fois, au chargement. */
    void prepare (double fs)
    {
        if (prete_ && std::abs (fs - fs_) < 1.0) return;
        for (int f = 0; f < 4; ++f)
            for (int o = 0; o < kOctavesTable; ++o)
                t_[f][o].fabrique ((FamilleTable) f, hzOctave (o), fs);
        fs_ = fs;
        prete_ = true;
    }

    /** L'octave d'une hauteur : 0 pour 20 Hz, 11 pour 40 kHz. */
    static int indiceOctave (double hz) noexcept
    {
        const int o = (int) std::ceil (std::log2 (std::max (20.0, hz) / 20.0));
        return std::max (0, std::min (kOctavesTable - 1, o));
    }
    static double hzOctave (int o) noexcept { return 20.0 * std::pow (2.0, (double) o); }

    const Table& lit (FamilleTable f, double hz) const noexcept
    {
        return t_[std::max (0, std::min (3, (int) f))][indiceOctave (hz)];
    }
    bool prete() const noexcept { return prete_; }

private:
    Table t_[4][kOctavesTable];
    double fs_ = 0.0;
    bool prete_ = false;
};

/** L'unique banque du processus.

    /!\ Elle est partagee entre toutes les instances du plugin, et c'est
    voulu : les tables ne dependent d'aucun reglage, seulement de la frequence
    d'echantillonnage. Deux instances a 44,1 kHz n'ont aucune raison d'en
    fabriquer deux jeux.
*/
inline BanqueTables& banqueTables()
{
    static BanqueTables b;
    return b;
}

/** ⭐⭐ LA BANQUE DESSINEE D'UNE COUCHE.

    Meme role que `BanqueTables`, mais elle appartient au PATCH : elle change
    quand l'utilisateur clique. Douze octaves, une seule famille.

    /!\ ELLE SE FABRIQUE SUR LE FIL MESSAGE, jamais sur le fil audio. Douze
    octaves x seize images, c'est le meme ordre de grandeur que le piege 28 -
    34 ms pour poser une note. Le processeur en tient DEUX par couche et
    bascule un indice atomique : on ecrit toujours dans celle que personne ne
    lit.
*/
class BanqueDessin
{
public:
    void fabrique (const Dessin& d, double fs)
    {
        SpectreDessin sp;
        sp.analyse (d);
        for (int o = 0; o < kOctavesTable; ++o)
            t_[o].fabriqueDepuisSpectre (sp, BanqueTables::hzOctave (o), fs);
        seule_ = -1;
        prete_ = true;
    }

    /** ⭐ Les deux dessins : « position » balaye de A vers B. */
    void fabrique (const DessinAB& d, double fs)
    {
        if (! d.bActif) { fabrique (d.a, fs); return; }
        SpectreDessin sa, sb;
        sa.analyse (d.a); sb.analyse (d.b);
        for (int o = 0; o < kOctavesTable; ++o)
            t_[o].fabriqueDepuisDeuxSpectres (sa, sb, BanqueTables::hzOctave (o), fs);
        seule_ = -1;
        prete_ = true;
    }

    void fabriqueOctave (const DessinAB& d, double hz, double fs)
    {
        if (! d.bActif) { fabriqueOctave (d.a, hz, fs); return; }
        const int o = BanqueTables::indiceOctave (hz);
        const int kU = std::max (1, (int) (0.45 * fs / BanqueTables::hzOctave (o)));
        SpectreDessin sa, sb;
        sa.analyse (d.a, kU); sb.analyse (d.b, kU);
        t_[o].fabriqueDepuisDeuxSpectres (sa, sb, BanqueTables::hzOctave (o), fs);
        seule_ = o;
        prete_ = true;
    }

    /** ⭐⭐ UNE SEULE OCTAVE - le chemin de la RECHERCHE.

        Genopatch rend un profil sur UNE note, des milliers de fois. Fabriquer
        les douze octaves a chaque evaluation coute 22 ms ; il en faut 2 pour
        celle dont on se sert. Le reste serait douze fois le meme gaspillage.

        /!\ Le plugin, lui, garde les douze : il ne sait pas quelles notes on
        va jouer, et decouvrir une octave manquante en plein accord voudrait
        dire fabriquer sur le fil audio (piege 28). */
    void fabriqueOctave (const Dessin& d, double hz, double fs)
    {
        const int o = BanqueTables::indiceOctave (hz);
        SpectreDessin sp;
        sp.analyse (d, std::max (1, (int) (0.45 * fs / BanqueTables::hzOctave (o))));
        t_[o].fabriqueDepuisSpectre (sp, BanqueTables::hzOctave (o), fs);
        seule_ = o;
        prete_ = true;
    }

    const Table& lit (double hz) const noexcept
    {
        return t_[(seule_ >= 0) ? seule_ : BanqueTables::indiceOctave (hz)];
    }
    bool prete() const noexcept { return prete_; }

private:
    Table t_[kOctavesTable];
    /** >= 0 : une seule octave a ete fabriquee, et c'est celle-la qu'on rend
        quelle que soit la note demandee. */
    int seule_ = -1;
    bool prete_ = false;
};

class EtageTable
{
public:
    /** /!\ APPELEE DEPUIS LE FIL MESSAGE (via `Couche::prepare`, via
        `prepareToPlay`). C'est la seule fenetre ou l'on a le droit de
        fabriquer les tables. */
    void prepare (double fs) { fs_ = fs; phase_ = 0.0; banqueTables().prepare (fs); }

    void declenche (const ReglagesTable& r, double hz, double phase0) noexcept
    {
        phase_ = phase0 - std::floor (phase0);
        majHauteur (r, hz);
    }

    void majHauteur (const ReglagesTable& r, double hz) noexcept
    {
        hz_ = std::max (0.01, std::min (hz, 0.49 * fs_));
        dt_ = hz_ / fs_;
        // ⭐ PLUS AUCUNE FABRICATION ICI : on choisit la table deja prete.
        if (r.famille == FamilleTable::Dessin)
        {
            // /!\ Si la banque dessinee n'est pas encore prete (premier bloc,
            // ou dessin jamais construit), on retombe sur une famille
            // procedurale plutot que sur le silence : un moteur muet se
            // diagnostique bien plus mal qu'un moteur au mauvais timbre.
            table_ = (r.dessin != nullptr && r.dessin->prete())
                        ? &r.dessin->lit (hz_)
                        : &banqueTables().lit (FamilleTable::Harmoniques, hz_);
        }
        else
            table_ = &banqueTables().lit (r.famille, hz_);
    }

    /** `dPosition` vient de la matrice de modulation : c'est LUI qui fait le
        balayage, et le balayage est tout l'interet du moteur. */
    inline double suivant (const ReglagesTable& r, double dPosition = 0.0) noexcept
    {
        // symetrie : deforme la phase de lecture, comme le miroir du PPG
        const double sy = std::max (0.05, std::min (0.95, r.symetrie));
        double ph = phase_;
        ph = (ph < sy) ? (0.5 * ph / sy) : (0.5 + 0.5 * (ph - sy) / (1.0 - sy));

        if (table_ == nullptr) return 0.0;
        const double v = table_->lit (r.position + dPosition, ph, r.lecture);
        phase_ += dt_;
        if (phase_ >= 1.0) phase_ -= std::floor (phase_);
        return v;
    }

    /** La phase de l'oscillateur, 0..1 - la tete de lecture de l'interface. */
    double position() const noexcept { return phase_ - std::floor (phase_); }

private:
    const Table* table_ = nullptr;
    double fs_ = 44100.0, phase_ = 0.0, dt_ = 0.01, hz_ = 440.0;
};

} // namespace romance
