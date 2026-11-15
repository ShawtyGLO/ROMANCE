// =============================================================================
//  Recette.h - 108 octets qui FABRIQUENT une ROM.  (format v2)
//
//  ⭐ C'EST LA THESE DU PROJET. Le sample depose n'est jamais conserve : il est
//  lu, analyse, resynthetise, puis jete. Ce qui reste - ce qui EST l'etat du
//  plugin - c'est cette recette. D'ou trois proprietes qui ne s'obtiennent pas
//  autrement :
//    · aucun sample n'est livre (donc aucune question de droits) ;
//    · un preset pese 108 octets, pas 4 Mo ;
//    · la recherche peut explorer un espace continu, ce qu'un sample interdit.
//
//  ⭐ CHAQUE PARTIEL A SA PROPRE DECROISSANCE. Sans ca le spectre est fige et
//  l'on ne fabrique qu'une forme d'onde de plus - c'est precisement ce qui rend
//  l'additif bon marche si mort.
//
//  ⭐ v2 - trois manques MESURES sur la v1, et combles :
//    1. AUCUNE CHUTE DE HAUTEUR. Sonde : 0,0000 a 150 Hz au debut d'un 808.
//       Or la chute de hauteur EST la signature d'un 808, d'un tom, d'un kick.
//       -> `pitchProf` / `pitchDuree` / `pitchCourbe`.
//    2. UN PARTIEL NE POUVAIT QUE DECROITRE. Sonde : -0,21 dB entre le debut
//       et 0,72 s, decroissance minimale = PLAT, jamais croissant. Une nappe
//       qui s'ouvre etait donc hors d'atteinte.
//       -> deux bits de MONTEE par partiel, pris sur le desaccord (qui n'a pas
//          besoin de 8 bits : +-50 cents sur 6 bits = pas de 1,6 cent). Zero
//          octet depense, 24 partiels conserves.
//    3. UNE SEULE BANDE DE BRUIT. Un souffle PLUS un sifflement metallique,
//       ce sont deux bandes. -> une deuxieme, prise sur la reserve.
//
//  /!\ UNE ROM EST BOUCLEE. Trois consequences qu'on ne peut pas ignorer :
//    1. dans la boucle, les partiels sont RECALES sur des multiples exacts de
//       la longueur de boucle. Boucler force la periodicite, autant la calculer
//       juste plutot que d'entendre un clac a chaque tour. On n'a volontairement
//       pas de fondu de boucle dans le lecteur - c'est au MATERIAU d'etre juste.
//    2. le bruit de la boucle est PERIODIQUE : le meme motif revient a chaque
//       tour et prend une hauteur. Artefact d'epoque assume.
//    3. la chute de hauteur vit dans l'ATTAQUE uniquement. Une boucle dont la
//       hauteur bouge n'est pas une boucle.
//
//  Ne depend que de <cstdint>, <cmath>, <algorithm> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Grain.h"   // kPi, kFsRomEII, UnPole

namespace romance
{

constexpr int kOctetsRecette = 108;   // la taille de l'etat, et elle est tenue
constexpr int kPartielsMax   = 24;
constexpr int kSourcesMax    = 3;     // multi-f0 : un son a parfois plusieurs
                                      // oscillateurs, et parfois plusieurs notes
constexpr int kBandesBruit   = 2;

/** /!\ PLAFOND DUR sur la taille d'une ROM. Il ne sert pas a economiser la
    memoire : il sert a ce que les tampons soient alloues UNE FOIS et ne se
    reallouent JAMAIS. Une voix qui sonne tient un pointeur sur la ROM ; si le
    vecteur se reallouait sous elle, on lirait de la memoire liberee. */
constexpr int kRomMax = 180000;       // ~6,5 s a 27,7 kHz

/** Les quatre temps de montee possibles, en secondes. Deux bits suffisent :
    ce qui compte est l'ordre de grandeur, pas la valeur exacte.

    /!\ ELARGIS D'APRES LE CORPUS. Les valeurs d'origine s'arretaient a 600 ms,
    or un quart des 1 665 sons mesures ont une attaque plus longue que 1,08 s
    (p90 : 2,5 s). Ces sons-la etaient tout simplement HORS D'ATTEINTE du
    format - aucune quantite de recherche ni d'apprentissage n'y aurait change
    quoi que ce soit. Le jeu genere plafonnait a 100 ms de mediane contre 284
    dans le corpus, et c'est cette mesure-la qui l'a revele. */
constexpr double kMontees[4] = { 0.0, 0.040, 0.300, 1.600 };

/** Un partiel : son poids, son extinction, sa montee, son ecart a l'harmonique. */
struct Partiel
{
    double amplitude = 0.0;    // 0..1 (lineaire)
    double decroiss  = 0.5;    // 0 = tient, 1 = s'eteint tres vite
    double desaccord = 0.0;    // -50..+50 cents
    int    montee    = 0;      // indice dans kMontees : 0 = attaque immediate
};

struct BandeBruit
{
    double niveau  = 0.0;      // 0..1
    double coupure = 0.35;     // 0..1 (log, 200 Hz -> 12 kHz)
    double decroiss = 0.5;
};

struct Recette
{
    double f0 = 261.6255653;          // Hz
    double dureeAttaque = 0.35;       // s - la partie qui evolue
    int    cyclesBoucle = 8;          // longueur de boucle, en cycles de f0
    double inharmonicite = 0.0;       // 0..1 - etire les partiels vers l'aigu

    BandeBruit bruit[kBandesBruit];

    double transNiveau = 0.0;         // 0..1 - le choc initial
    double transDuree  = 0.02;        // s
    double transCouleur = 0.5;        // 0 = sourd, 1 = clair

    // enveloppe de hauteur - c'est le 808, le tom, le kick.
    double pitchProf   = 0.0;         // demi-tons, -48..+48
    double pitchDuree  = 0.05;        // s, 0..1
    double pitchCourbe = 0.5;         // 0 = molle, 1 = raide

    unsigned int graine = 1u;         // les phases et le bruit : deterministes

    int nPartiels = 12;
    Partiel partiels[kPartielsMax];

    // multi-f0 : deux sources supplementaires. ratio 0 = source eteinte.
    double ratio[kSourcesMax]  = { 1.0, 0.0, 0.0 };
    double niveau[kSourcesMax] = { 1.0, 0.0, 0.0 };
};

// ------------------------------------------------------- forme compacte

namespace detail
{
    inline uint8_t vers8 (double v, double lo, double hi) noexcept
    {
        const double x = (v - lo) / (hi - lo);
        return (uint8_t) std::max (0.0, std::min (255.0, std::round (x * 255.0)));
    }
    inline double depuis8 (uint8_t o, double lo, double hi) noexcept
    {
        return lo + ((double) o / 255.0) * (hi - lo);
    }
    inline uint8_t vers6 (double v, double lo, double hi) noexcept
    {
        const double x = (v - lo) / (hi - lo);
        return (uint8_t) std::max (0.0, std::min (63.0, std::round (x * 63.0)));
    }
    inline double depuis6 (uint8_t o, double lo, double hi) noexcept
    {
        return lo + ((double) (o & 63u) / 63.0) * (hi - lo);
    }
}

/*  Plan des 108 octets (v2)
    ------------------------------------------------------------------
     0        version (2)
     1..2     f0 (Hz x 16)
     3        duree d'attaque
     4        longueur de boucle, en cycles
     5        inharmonicite
     6..8     bruit 1 : niveau, coupure, decroissance
     9..11    transitoire : niveau, duree, couleur
    12..13    graine
    14        nombre de partiels
    15        reserve
    16..87    24 x { amplitude(8) , decroissance(8) , desaccord(6)|montee(2) }
    88..93    2 x { ratio(16) , niveau(8) }          - multi-f0
    94..96    enveloppe de hauteur : profondeur, duree, courbe
    97..99    bruit 2 : niveau, coupure, decroissance
   100..107   reserve (8 octets)
*/

/** Serialise en EXACTEMENT kOctetsRecette octets.

    /!\ La quantification est faite EXPRES et elle est visible : ecrire puis
    relire une recette doit rendre la MEME ROM, sinon « la recette est l'etat »
    est un slogan et pas un fait. Tout le rendu part donc de la forme compacte.
*/
inline void versOctets (const Recette& r, uint8_t* o) noexcept
{
    using namespace detail;
    for (int i = 0; i < kOctetsRecette; ++i) o[i] = 0;

    o[0] = 2;                                    // version
    const uint16_t f0q = (uint16_t) std::max (0.0, std::min (65535.0,
                              std::round (r.f0 * 16.0)));
    o[1] = (uint8_t) (f0q & 0xFF); o[2] = (uint8_t) (f0q >> 8);
    o[3] = vers8 (r.dureeAttaque, 0.0, 2.04);
    o[4] = (uint8_t) std::max (1, std::min (255, r.cyclesBoucle));
    o[5] = vers8 (r.inharmonicite, 0.0, 1.0);
    o[6] = vers8 (r.bruit[0].niveau,  0.0, 1.0);
    o[7] = vers8 (r.bruit[0].coupure, 0.0, 1.0);
    o[8] = vers8 (r.bruit[0].decroiss, 0.0, 1.0);
    o[9] = vers8 (r.transNiveau,  0.0, 1.0);
    o[10] = vers8 (r.transDuree,  0.0, 0.20);
    o[11] = vers8 (r.transCouleur, 0.0, 1.0);
    o[12] = (uint8_t) (r.graine & 0xFF); o[13] = (uint8_t) ((r.graine >> 8) & 0xFF);
    o[14] = (uint8_t) std::max (1, std::min (kPartielsMax, r.nPartiels));
    o[15] = 0;
    for (int k = 0; k < kPartielsMax; ++k)
    {
        uint8_t* p = o + 16 + k * 3;
        // amplitude en dB : 0 = muet, 255 = 0 dB, sur 66 dB.
        // Une echelle lineaire gacherait ses pas dans le fort et n'aurait
        // presque rien pour le faible - or c'est le faible qui fait le timbre.
        const double a = r.partiels[k].amplitude;
        p[0] = (a <= 1e-4) ? (uint8_t) 0
             : (uint8_t) std::max (1.0, std::min (255.0,
                   std::round (255.0 + 20.0 * std::log10 (a) * 255.0 / 66.0)));
        p[1] = vers8 (r.partiels[k].decroiss, 0.0, 1.0);
        // 6 bits de desaccord + 2 bits de montee, dans le meme octet
        const uint8_t d6 = vers6 (r.partiels[k].desaccord, -50.0, 50.0);
        const uint8_t m2 = (uint8_t) std::max (0, std::min (3, r.partiels[k].montee));
        p[2] = (uint8_t) ((d6 << 2) | m2);
    }
    for (int s = 1; s < kSourcesMax; ++s)
    {
        uint8_t* p = o + 88 + (s - 1) * 3;
        const uint16_t rq = (uint16_t) std::max (0.0, std::min (65535.0,
                                 std::round (r.ratio[s] * 4096.0)));
        p[0] = (uint8_t) (rq & 0xFF); p[1] = (uint8_t) (rq >> 8);
        p[2] = vers8 (r.niveau[s], 0.0, 1.0);
    }
    o[94] = vers8 (r.pitchProf,  -48.0, 48.0);
    o[95] = vers8 (r.pitchDuree,   0.0, 1.0);
    o[96] = vers8 (r.pitchCourbe,  0.0, 1.0);
    o[97] = vers8 (r.bruit[1].niveau,  0.0, 1.0);
    o[98] = vers8 (r.bruit[1].coupure, 0.0, 1.0);
    o[99] = vers8 (r.bruit[1].decroiss, 0.0, 1.0);
    // 100..107 : reserve - de la place pour la suite sans casser les presets
}

inline Recette depuisOctets (const uint8_t* o) noexcept
{
    using namespace detail;
    Recette r;
    r.f0 = ((double) ((uint16_t) o[1] | ((uint16_t) o[2] << 8))) / 16.0;
    r.dureeAttaque = depuis8 (o[3], 0.0, 2.04);
    r.cyclesBoucle = std::max (1, (int) o[4]);
    r.inharmonicite = depuis8 (o[5], 0.0, 1.0);
    r.bruit[0].niveau   = depuis8 (o[6], 0.0, 1.0);
    r.bruit[0].coupure  = depuis8 (o[7], 0.0, 1.0);
    r.bruit[0].decroiss = depuis8 (o[8], 0.0, 1.0);
    r.transNiveau   = depuis8 (o[9], 0.0, 1.0);
    r.transDuree    = depuis8 (o[10], 0.0, 0.20);
    r.transCouleur  = depuis8 (o[11], 0.0, 1.0);
    r.graine = (unsigned int) o[12] | ((unsigned int) o[13] << 8);
    r.nPartiels = std::max (1, std::min (kPartielsMax, (int) o[14]));
    for (int k = 0; k < kPartielsMax; ++k)
    {
        const uint8_t* p = o + 16 + k * 3;
        r.partiels[k].amplitude = (p[0] == 0) ? 0.0
            : std::pow (10.0, (((double) p[0] - 255.0) * 66.0 / 255.0) / 20.0);
        r.partiels[k].decroiss  = depuis8 (p[1], 0.0, 1.0);
        r.partiels[k].desaccord = depuis6 ((uint8_t) (p[2] >> 2), -50.0, 50.0);
        r.partiels[k].montee    = (int) (p[2] & 3u);
    }
    r.ratio[0] = 1.0; r.niveau[0] = 1.0;
    for (int s = 1; s < kSourcesMax; ++s)
    {
        const uint8_t* p = o + 88 + (s - 1) * 3;
        r.ratio[s]  = ((double) ((uint16_t) p[0] | ((uint16_t) p[1] << 8))) / 4096.0;
        r.niveau[s] = depuis8 (p[2], 0.0, 1.0);
    }
    r.pitchProf   = depuis8 (o[94], -48.0, 48.0);
    r.pitchDuree  = depuis8 (o[95],   0.0, 1.0);
    r.pitchCourbe = depuis8 (o[96],   0.0, 1.0);
    r.bruit[1].niveau   = depuis8 (o[97], 0.0, 1.0);
    r.bruit[1].coupure  = depuis8 (o[98], 0.0, 1.0);
    r.bruit[1].decroiss = depuis8 (o[99], 0.0, 1.0);
    return r;
}

/** Passe une recette par sa forme compacte. Tout le rendu part de la, pour que
    ce qu'on entend soit exactement ce qu'on enregistre. */
inline Recette normalise (const Recette& r) noexcept
{
    uint8_t o[kOctetsRecette];
    versOctets (r, o);
    return depuisOctets (o);
}

// ------------------------------------------------------- rendu

struct RomRendue
{
    int nAttaque = 0;      // debut de boucle, en echantillons
    int nTotal   = 0;
    double boucleDebut() const noexcept
    { return (nTotal > 0) ? (double) nAttaque / (double) nTotal : 0.0; }
};

/** Fabrique la ROM. Deterministe : meme recette, meme ROM, toujours.

    /!\ NE PAS APPELER SUR LE THREAD AUDIO. C'est quelques millions de sin() et
    un redimensionnement. Le plugin la fabrique sur le thread message et echange
    ensuite un indice de banque.
*/
inline RomRendue rendRom (const Recette& brute, std::vector<double>& rom,
                          double fsRom = kFsRomEII)
{
    const Recette r = normalise (brute);
    const double f0 = std::max (10.0, r.f0);

    // longueur de boucle : un nombre ENTIER d'echantillons, donc f0 effective
    // legerement recalee. C'est la contrainte de la machine, pas un compromis.
    int nBoucle = std::max (16, (int) std::round ((double) r.cyclesBoucle
                                                  * fsRom / f0));
    int nAtt = std::max (0, (int) std::round (r.dureeAttaque * fsRom));
    // plafond dur : les tampons ne doivent jamais se reallouer (cf. kRomMax)
    nBoucle = std::min (nBoucle, kRomMax / 2);
    nAtt    = std::min (nAtt, kRomMax - nBoucle);
    const int nTot = nAtt + nBoucle;
    rom.assign ((size_t) nTot, 0.0);

    // --- generateur pseudo-aleatoire deterministe, seme par la recette
    unsigned int e = r.graine * 2654435761u + 1u;
    auto alea = [&e] () noexcept
    {
        e = e * 1664525u + 1013904223u;
        return ((double) (e >> 8) / 8388608.0) - 1.0;
    };

    const double nyq = 0.45 * fsRom;

    // --- enveloppe de hauteur, precalculee une fois pour tous les partiels.
    // ⭐ C'est le 808 : une sinusoide dont la HAUTEUR chute. Sans ca, le corps
    // et le clic y etaient mais la signature manquait.
    // /!\ Elle vit dans l'ATTAQUE seulement : une boucle dont la hauteur bouge
    // n'est pas une boucle.
    std::vector<double> facteur;
    const bool pitchActif = (std::abs (r.pitchProf) > 0.05) && nAtt > 0;
    if (pitchActif)
    {
        facteur.resize ((size_t) nAtt);
        const double tp = std::max (0.002, r.pitchDuree);
        const double g  = 0.3 + 2.7 * r.pitchCourbe;   // courbe : molle -> raide
        for (int i = 0; i < nAtt; ++i)
        {
            const double t = (double) i / fsRom;
            const double env = std::pow (std::exp (-t / tp), g);
            facteur[(size_t) i] = std::pow (2.0, r.pitchProf * env / 12.0);
        }
    }

    // --- partiels
    for (int s = 0; s < kSourcesMax; ++s)
    {
        if (r.ratio[s] <= 1e-6 || r.niveau[s] <= 1e-4) continue;
        const double fs0 = f0 * r.ratio[s];

        for (int k = 0; k < r.nPartiels; ++k)
        {
            const auto& pa = r.partiels[k];
            if (pa.amplitude <= 1e-5) continue;

            const double n = (double) (k + 1);
            // inharmonicite a la corde : f_k = k f0 sqrt(1 + B k^2)
            const double B = r.inharmonicite * 0.0008;
            const double fBase = fs0 * n * std::sqrt (1.0 + B * n * n)
                                 * std::pow (2.0, pa.desaccord / 1200.0);
            if (fBase >= nyq) continue;

            // constante de temps : 30 s (tient) a 30 ms (percussif)
            const double tau = 30.0 * std::pow (0.001, pa.decroiss);
            const double tMon = kMontees[std::max (0, std::min (3, pa.montee))];
            const double a0 = pa.amplitude * r.niveau[s];
            const double ph0 = kPi * alea();

            // enveloppe du partiel : montee PUIS decroissance.
            // A montee nulle on retombe exactement sur l'exponentielle pure.
            auto envPartiel = [tau, tMon, fsRom] (int i) noexcept
            {
                const double t = (double) i / fsRom;
                const double d = std::exp (-t / tau);
                return (tMon <= 0.0) ? d : d * (1.0 - std::exp (-t / tMon));
            };

            // --- partie attaque. La phase est INTEGREE, pas calculee : avec une
            // chute de hauteur, w n'est plus constant.
            double ph = ph0;
            const double w = 2.0 * kPi * fBase / fsRom;
            for (int i = 0; i < nAtt; ++i)
            {
                rom[(size_t) i] += a0 * envPartiel (i) * std::sin (ph);
                ph += pitchActif ? (w * facteur[(size_t) i]) : w;
            }

            // --- partie boucle : frequence RECALEE sur la longueur de boucle.
            // /!\ Sans ce recalage, chaque tour de boucle claque. Boucler FORCE
            // la periodicite ; on la calcule juste au lieu de la subir.
            const int h = std::max (1, (int) std::round (fBase * (double) nBoucle / fsRom));
            const double fCal = (double) h * fsRom / (double) nBoucle;
            if (fCal >= nyq) continue;
            const double wc = 2.0 * kPi * fCal / fsRom;
            // on reprend la phase et l'amplitude atteintes a la fin de l'attaque
            const double aFin = a0 * envPartiel (nAtt);
            for (int i = 0; i < nBoucle; ++i)
                rom[(size_t) (nAtt + i)] += aFin * std::sin (wc * (double) i + ph);
        }
    }

    // --- bruit, sur DEUX bandes.
    // ⭐ Un souffle PLUS un sifflement metallique, ce sont deux bandes. La v1
    // n'en avait qu'une, et la sonde montrait une seule pente sur tout le
    // spectre.
    for (int b = 0; b < kBandesBruit; ++b)
    {
        const auto& bb = r.bruit[b];
        if (bb.niveau <= 1e-4) continue;

        const double fc = 200.0 * std::pow (12000.0 / 200.0, bb.coupure);
        UnPole lp; lp.regle (fc, fsRom);
        const double tau = 8.0 * std::pow (0.002, bb.decroiss);

        // /!\ COMPENSATION DU GAIN DU FILTRE. Un passe-bas a 300 Hz retire
        // l'essentiel de l'energie d'un bruit blanc : sans compensation,
        // `niveau` ne voulait pas dire la meme chose a 300 Hz et a 10 kHz, et
        // le rendu plafonnait a 11 % de part de bruit la ou le corpus est a
        // 56 % au p75. Un parametre dont le sens depend d'un autre parametre
        // n'est pas un parametre, c'est un piege - pour l'utilisateur comme
        // pour la recherche.
        double gainLp = 0.0;
        {
            UnPole t; t.regle (fc, fsRom);
            unsigned int e2 = 12345u;
            double acc = 0.0;
            for (int i = 0; i < 2048; ++i)
            {
                e2 = e2 * 1664525u + 1013904223u;
                const double v = t.traite (((double) (e2 >> 8) / 8388608.0) - 1.0);
                if (i > 256) acc += v * v;
            }
            gainLp = std::sqrt (acc / 1792.0);
        }
        const double comp = (gainLp > 1e-6) ? (0.577 / gainLp) : 1.0;   // 0,577 = rms d'un bruit uniforme

        for (int i = 0; i < nAtt; ++i)
            rom[(size_t) i] += bb.niveau * 0.5 * comp
                             * std::exp (-(double) i / (tau * fsRom))
                             * lp.traite (alea());

        // /!\ Dans la boucle, le bruit est PERIODIQUE : un motif de nBoucle
        // echantillons qui revient a chaque tour, et qui prend donc une hauteur.
        // Toutes les machines a ROM bouclee font ca. Artefact d'epoque assume.
        const double aFin = bb.niveau * 0.5 * comp
                          * std::exp (-(double) nAtt / (tau * fsRom));
        if (aFin > 1e-6)
        {
            std::vector<double> motif ((size_t) nBoucle);
            UnPole lb; lb.regle (fc, fsRom);
            for (int i = 0; i < nBoucle; ++i) motif[(size_t) i] = lb.traite (alea());
            // deuxieme passe : le filtre lui-meme devient periodique
            for (int i = 0; i < nBoucle; ++i) motif[(size_t) i] = lb.traite (motif[(size_t) i]);
            for (int i = 0; i < nBoucle; ++i)
                rom[(size_t) (nAtt + i)] += aFin * motif[(size_t) i];
        }
    }

    // --- transitoire : le choc, seulement au tout debut
    if (r.transNiveau > 1e-4 && nAtt > 0)
    {
        const double fc = 300.0 * std::pow (14000.0 / 300.0, r.transCouleur);
        UnPole lp; lp.regle (fc, fsRom);
        const int nT = std::min (nAtt, (int) std::round (
                            std::max (0.0005, r.transDuree) * fsRom));
        for (int i = 0; i < nT; ++i)
        {
            const double env = 1.0 - (double) i / (double) nT;
            rom[(size_t) i] += r.transNiveau * env * env * lp.traite (alea());
        }
    }

    // --- normalisation SUR LA CRETE, pas sur l'energie : la ROM alimente une
    // quantification 8 bits, ou tout ce qu'on laisse sous la crete est du
    // rapport signal/bruit perdu pour rien.
    double crete = 1e-12;
    for (double v : rom) crete = std::max (crete, std::abs (v));
    const double g = 0.9 / crete;
    for (auto& v : rom) v *= g;

    RomRendue out;
    out.nAttaque = nAtt;
    out.nTotal = nTot;
    return out;
}

} // namespace romance
