// =============================================================================
//  Perte.h - La distance entre deux sons, et le rendu qui la rend comparable.
//
//  ⭐ LA PERTE EST MESUREE APRES LA DEGRADATION D'EPOQUE, jamais avant. C'est
//  la moitie de la these du projet : on ne cherche pas la recette qui imite le
//  sample, on cherche celle qui, UNE FOIS PASSEE DANS LA MACHINE, ressemble au
//  sample. Ce sont deux problemes differents et ils n'ont pas la meme solution.
//
//  /!\ REGROUPEMENT EN BANDES LOGARITHMIQUES, OBLIGATOIRE. Le prototype Python
//  l'a paye cher : compare echantillon par echantillon (ou raie par raie), la
//  perte devient une AIGUILLE - plate sur 90 % du chemin puis effondrement.
//  On comparait alors deux realisations d'un bruit de quantification
//  deterministe, pas deux timbres. Un plancher a -80 dB ne suffisait pas non
//  plus : le bruit mu-law se tient a -42 dB. Le regroupement en 48 bandes log
//  rend le paysage lisse, donc parcourable.
//
//  /!\ MULTI-ECHELLE. Une seule fenetre ne peut pas voir a la fois l'attaque
//  (courte) et la tenue (longue). Trois tailles, c'est le minimum honnete.
//
//  Ne depend que de <cmath>, <complex>, <algorithm> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "Couche.h"   // Enveloppe
#include "Grain.h"
#include "Recette.h"

namespace romance
{

// ------------------------------------------------------------------- FFT

/** FFT radix-2 sur place. Taille en puissance de deux. */
inline void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }
    for (size_t lg = 2; lg <= n; lg <<= 1)
    {
        const double ang = -2.0 * kPi / (double) lg;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += lg)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < lg / 2; ++k)
            {
                const auto u = x[i + k];
                const auto v = x[i + k + lg / 2] * w;
                x[i + k] = u + v;
                x[i + k + lg / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// ------------------------------------------------- empreinte multi-echelle

constexpr int kNbBandes = 48;

/** ⭐⭐ LE CANAL D'ENVELOPPE - combien de fois le niveau d'une trame compte.

    L'empreinte normalise chaque trame par SA PROPRE crete. C'est ce qui la rend
    bonne pour comparer des TIMBRES, et c'est ce qui a ete calibre (voir le
    tableau des planchers) : une queue de reverbe 40 dB plus bas pese autant
    qu'une attaque, donc on la compare vraiment.

    Mais ca jetait AUSSI toute l'enveloppe d'amplitude. Un son qui s'eteint en
    50 ms et un son qui tient deux secondes donnaient la meme empreinte tant que
    leur timbre se ressemblait - donc bouger l'ADSR ne changeait pas la perte,
    donc la recherche l'ignorait. Les octets d'enveloppe etaient bien MUTABLES ;
    ils n'avaient simplement aucun effet mesurable.

    /!\ J'AI D'ABORD ESSAYE DE NORMALISER GLOBALEMENT, et la mesure a dit non :
    l'enveloppe devenait visible mais les queues s'ecrasaient sur le plancher.
    Le melange de reverbe retrouve tombait de 40+ a 31 sur 255, et la recherche
    partait a l'octave. On ne choisit donc pas entre les deux : on garde la
    normalisation par trame ET on ajoute le niveau de la trame comme canal
    supplementaire.

    Le nombre de copies EST le poids. A quatre copies sur 52 canaux, un ecart
    d'enveloppe de 60 dB coute 4,6 dB de perte - decisif face a des pertes
    typiques de 3 a 6 dB, sans noyer le timbre. */
constexpr int kBandesEnveloppe = 4;
constexpr int kCanauxTrame = kNbBandes + kBandesEnveloppe;

/** ⭐ LE PLANCHER DECIDE DE TOUT, et il se calibre - il ne se devine pas.
    Mesure : cout d'un changement MUSICALEMENT NUL (un gain de +2 %, alors que
    l'empreinte est deja normalisee par trame) contre celui d'un vrai changement
    de timbre. Plus le rapport est grand, plus la recherche voit ou aller.

      plancher   objectif PROPRE   a travers la MACHINE
       -100 dB        33,8                 1,7
        -60 dB        33,2                 2,2
        -40 dB        31,3                14,4
        -30 dB        30,6                21,1

    ⭐ CE TABLEAU A RENVERSE UNE DECISION. J'avais ecrit deux fois que mesurer
    l'erreur APRES la degradation etait « la moitie de la these ». La mesure dit
    le contraire, et par un facteur 20 : a travers la machine, le bruit mu-law
    (-41,6 dB) noie l'objectif, et il fallait remonter le plancher a -40 dB pour
    s'en proteger - donc jeter 45 dB de dynamique. Sur un objectif propre, le
    plancher redescend a -85 dB, la perte occupe 65 % de plancher au lieu de
    82 %, et elle distingue vingt fois mieux.

    Ce qui survit de l'idee de depart : c'est bien a travers la machine qu'on
    JUGE un resultat, puisque c'est ce qu'on entendra. Mais ce n'est pas a
    travers elle qu'on AJUSTE - sinon la recette pre-compense une machine
    precise et devient fausse des qu'on change d'epoque ou qu'on passe en
    `PROPRE`. Le grain est un choix de l'utilisateur, applique a la lecture ;
    il n'a pas a etre cuit dans le materiau. */
constexpr double kPlancherDegrade = -40.0;   // si l'on compare A TRAVERS la machine
constexpr double kPlancherPropre  = -85.0;   // sur un objectif propre

/** Ce qu'on compare : pour chaque echelle, une suite de trames de 48 bandes. */
struct Empreinte
{
    std::vector<double> v;    // tout a plat, en dB
    int taille() const noexcept { return (int) v.size(); }
};

namespace detail
{
    /** Table de correspondance raie -> bande, calculee une fois par echelle. */
    inline std::vector<int> bandes (int n, double fs)
    {
        std::vector<int> b ((size_t) (n / 2 + 1), -1);
        // /!\ 25 Hz et pas 40 : a 40 Hz, la fondamentale d'une basse sub
        // tombait HORS de l'analyse. Sur BASS 1 (36,9 Hz) presque toutes les
        // bandes se retrouvaient au plancher dans les DEUX signaux, et la perte
        // rendait 0,30 dB - un score magnifique qui ne voulait rien dire.
        const double fLo = 25.0, fHi = 0.45 * fs;
        for (int k = 1; k <= n / 2; ++k)
        {
            const double f = (double) k * fs / (double) n;
            if (f < fLo || f > fHi) continue;
            const double x = std::log (f / fLo) / std::log (fHi / fLo);
            b[(size_t) k] = std::max (0, std::min (kNbBandes - 1,
                                (int) (x * (double) kNbBandes)));
        }
        return b;
    }
}

/** Empreinte multi-echelle : trois tailles de fenetre, 48 bandes log chacune.

    ⭐⭐ LA NORMALISATION EST GLOBALE A L'ECHELLE, PLUS PAR TRAME - et ce n'est
    pas un reglage, c'est la correction d'un defaut qui rendait la moitie du
    plugin invisible a la recherche.

    Chaque trame etait divisee par SA PROPRE crete. Consequence : l'empreinte ne
    contenait que des FORMES de spectre, jamais leur niveau relatif. Un son qui
    s'eteint en 50 ms et un son qui tient deux secondes donnaient donc la meme
    empreinte tant que leur timbre se ressemblait. L'enveloppe d'amplitude
    n'existait pas pour la perte.

    C'est ce que l'utilisateur entendait : « fais que Genopatch bouge les
    options type ADSR et filtres pour que ce soit plus proche de la source ».
    Les octets d'enveloppe etaient bien MUTABLES (voir `octetProfilMutable`) -
    ils n'avaient simplement aucun effet mesurable, donc la recherche les
    ignorait, et elle avait raison de le faire.

    /!\ CE QU'IL FALLAIT GARDER : l'invariance au niveau du sample. Elle est
    intacte - diviser toutes les trames par une meme constante laisse leurs
    ecarts inchanges, donc un sample deux fois plus fort donne exactement la
    meme empreinte. Ce qu'on cesse de jeter, c'est le rapport ENTRE trames.

    /!\ Et le plancher devient une vraie dynamique : a -85 dB sous la crete du
    son, une queue de reverbe 60 dB plus bas garde encore du detail.
*/
inline Empreinte empreinte (const std::vector<double>& x, double fs,
                            double plancherDb = kPlancherPropre)
{
    static const int tailles[3] = { 2048, 512, 128 };
    Empreinte e;
    std::vector<std::complex<double>> buf;
    std::vector<double> fen, bande ((size_t) kNbBandes);
    std::vector<double> brut;

    for (int t = 0; t < 3; ++t)
    {
        const int n = tailles[t];
        const int saut = n / 4;
        const auto tab = detail::bandes (n, fs);
        fen.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            fen[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);

        // Passe 1 : les energies de bande, et la crete GLOBALE de l'echelle.
        brut.clear();
        std::vector<double> creteTrame;
        double creteGlob = 1e-30;
        for (int deb = 0; deb + n <= (int) x.size(); deb += saut)
        {
            buf.assign ((size_t) n, {});
            for (int i = 0; i < n; ++i)
                buf[(size_t) i] = x[(size_t) (deb + i)] * fen[(size_t) i];
            fft (buf);
            std::fill (bande.begin(), bande.end(), 0.0);
            for (int k = 1; k <= n / 2; ++k)
                if (tab[(size_t) k] >= 0)
                    bande[(size_t) tab[(size_t) k]] += std::norm (buf[(size_t) k]);

            double c = 1e-30;
            for (double b : bande) { brut.push_back (b); c = std::max (c, b); }
            creteTrame.push_back (c);
            creteGlob = std::max (creteGlob, c);
        }

        // Passe 2 : le timbre, normalise PAR TRAME - puis le niveau de la
        // trame, normalise sur la crete GLOBALE. Les deux, pas l'un ou l'autre.
        for (size_t tr = 0; tr < creteTrame.size(); ++tr)
        {
            const double c = creteTrame[tr];
            for (int b = 0; b < kNbBandes; ++b)
                e.v.push_back (std::max (
                    10.0 * std::log10 (brut[tr * (size_t) kNbBandes + (size_t) b] / c),
                    plancherDb));
            const double niv = std::max (10.0 * std::log10 (c / creteGlob), plancherDb);
            for (int k = 0; k < kBandesEnveloppe; ++k) e.v.push_back (niv);
        }
    }
    return e;
}

/** Part des bandes assises sur le plancher.

    /!\ A LIRE AVANT LE SCORE. Si presque tout est au plancher, la perte ne
    compare plus grand-chose et son chiffre n'est pas comparable a celui d'un
    autre son. Ca ne se devine pas en regardant la perte : il faut le mesurer.
*/
inline double partAuPlancher (const Empreinte& e,
                              double plancherDb = kPlancherPropre) noexcept
{
    if (e.taille() == 0) return 1.0;
    int n = 0;
    for (double v : e.v) if (v <= plancherDb + 1e-9) ++n;
    return (double) n / (double) e.taille();
}

/** Distance moyenne en dB entre deux empreintes. 0 = identique. */
inline double perte (const Empreinte& a, const Empreinte& b) noexcept
{
    const int n = std::min (a.taille(), b.taille());
    if (n == 0) return 1e9;
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += std::abs (a.v[(size_t) i] - b.v[(size_t) i]);
    return s / (double) n;
}

// ------------------------------------------- empreinte REDUITE (entree reseau)

// ⭐ CE QUE LE RESEAU VOIT. Trois echelles, 48 bandes log, mais un nombre FIXE
// de tranches de temps par echelle - sinon l'entree change de taille avec la
// duree du son et aucun reseau ne peut la lire.
//
// /!\ LA REPARTITION DES TRANCHES N'EST PAS UNIFORME, et c'est deliberé :
//   · la fenetre LONGUE voit bien les frequences et mal le temps -> 24 tranches,
//     parce que c'est elle qui porte l'evolution des partiels, le point central
//     de la recette ;
//   · la fenetre COURTE voit bien le temps et mal les frequences -> 6 tranches,
//     elle sert a l'attaque et au transitoire, pas au timbre.
// Donner autant de tranches aux trois reviendrait a payer trois fois pour la
// meme information temporelle, et a affamer celle qui compte.
//
// /!\ CETTE FONCTION DOIT ETRE PARTAGEE PAR L'ENTRAINEMENT ET LE PLUGIN. Deux
// implementations qui divergent d'un detail, et le reseau entraine ne voit pas
// ce qu'on lui donne a l'inference : il echoue sans qu'aucun test ne le dise.
constexpr int kTranches[3] = { 24, 12, 6 };
constexpr int kTaillesEmpreinte[3] = { 2048, 512, 128 };
constexpr int kTailleReduite = (24 + 12 + 6) * kCanauxTrame;   // 2184

/** Remplit `sortie` (kTailleReduite flottants) avec l'empreinte reduite, en dB
    relatifs a la trame, plancher kPlancherPropre. */
inline void empreinteReduite (const std::vector<double>& x, double fs,
                              float* sortie)
{
    for (int i = 0; i < kTailleReduite; ++i) sortie[i] = (float) kPlancherPropre;

    std::vector<std::complex<double>> buf;
    std::vector<double> fen, bande ((size_t) kNbBandes);
    int decalage = 0;

    for (int t = 0; t < 3; ++t)
    {
        const int n = kTaillesEmpreinte[t];
        const int saut = n / 4;
        const int nTr = kTranches[t];
        const auto tab = detail::bandes (n, fs);
        fen.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            fen[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);

        // combien de trames au total, pour repartir en nTr tranches egales
        int nTrames = 0;
        for (int deb = 0; deb + n <= (int) x.size(); deb += saut) ++nTrames;
        if (nTrames <= 0) { decalage += nTr * kCanauxTrame; continue; }

        std::vector<double> acc ((size_t) (nTr * kCanauxTrame), 0.0);
        std::vector<int> compte ((size_t) nTr, 0);

        // ⭐⭐ DEUX PASSES, exactement comme `empreinte()`.
        //
        // /!\ ELLE DOIT LA SUIVRE A LA LETTRE. Ce que le reseau voit et ce que
        // la perte mesure doivent etre la meme grandeur : deux normalisations
        // differentes, et le reseau apprend a viser une cible que la recherche
        // ne juge pas.
        std::vector<double> brut, creteTrame;
        double creteGlob = 1e-30;
        for (int deb = 0; deb + n <= (int) x.size(); deb += saut)
        {
            buf.assign ((size_t) n, {});
            for (int i = 0; i < n; ++i)
                buf[(size_t) i] = x[(size_t) (deb + i)] * fen[(size_t) i];
            fft (buf);
            std::fill (bande.begin(), bande.end(), 0.0);
            for (int k = 1; k <= n / 2; ++k)
                if (tab[(size_t) k] >= 0)
                    bande[(size_t) tab[(size_t) k]] += std::norm (buf[(size_t) k]);
            double c = 1e-30;
            for (double b : bande) { brut.push_back (b); c = std::max (c, b); }
            creteTrame.push_back (c);
            creteGlob = std::max (creteGlob, c);
        }

        for (int iTrame = 0; iTrame < nTrames; ++iTrame)
        {
            const int tranche = std::min (nTr - 1, iTrame * nTr / nTrames);
            const double c = creteTrame[(size_t) iTrame];
            for (int b = 0; b < kNbBandes; ++b)
                acc[(size_t) (tranche * kCanauxTrame + b)] +=
                    std::max (10.0 * std::log10 (
                        brut[(size_t) (iTrame * kNbBandes + b)] / c), kPlancherPropre);
            const double niv = std::max (10.0 * std::log10 (c / creteGlob), kPlancherPropre);
            for (int k = 0; k < kBandesEnveloppe; ++k)
                acc[(size_t) (tranche * kCanauxTrame + kNbBandes + k)] += niv;
            ++compte[(size_t) tranche];
        }

        for (int tr = 0; tr < nTr; ++tr)
        {
            const double d = (compte[(size_t) tr] > 0) ? 1.0 / compte[(size_t) tr] : 0.0;
            for (int b = 0; b < kCanauxTrame; ++b)
            {
                const int i = tr * kCanauxTrame + b;
                sortie[decalage + i] = (compte[(size_t) tr] > 0)
                    ? (float) (acc[(size_t) i] * d) : (float) kPlancherPropre;
            }
        }
        decalage += nTr * kCanauxTrame;
    }
}

// --------------------------------------------- rendu d'une recette, degrade

/** L'enveloppe d'amplitude proposee par la resynthese.

    ⭐ ELLE N'EST PAS DANS LES 108 OCTETS, ET C'EST VOULU. Une enveloppe globale
    cuite dans la ROM serait tronquee par la boucle : le materiau est periodique
    apres l'attaque, une decroissance d'ensemble n'y a pas sa place. L'enveloppe
    appartient a la COUCHE, comme sur les machines - et la resynthese la propose
    en meme temps que le materiau au lieu de faire semblant qu'elle n'existe pas.

    Ce sont donc 4 octets EN PLUS des 108, que la recherche mute comme les
    autres, et que le plugin ecrit dans les parametres de la couche.
*/
struct EnvAmpli
{
    double attaque = 0.005, decli = 0.4, tenue = 0.8, relache = 0.5;
    bool actif = false;      // faux = pas d'enveloppe (comportement d'origine)
};

/** Joue une recette COMME LE PLUGIN LA JOUERAIT : ROM quantifiee, lecture a
    horloge variable, filtre de reconstruction asservi, sur-echantillonnage.

    ⭐ C'est ce rendu-la qu'on compare a la cible, pas la ROM brute. Chercher
    sur la ROM brute reviendrait a chercher la recette d'une machine qu'on n'a
    pas.
*/
inline void rendDegrade (const Recette& r, std::vector<double>& sortie,
                         int nEch, double fsSortie, const ConfigGrain& cfg,
                         const EnvAmpli* env = nullptr)
{
    std::vector<double> rom;
    const auto info = rendRom (r, rom, cfg.fsRom);

    // quantification : c'est une propriete du MATERIAU
    for (auto& v : rom) v = quantifie (v, cfg.quant);

    EtageReconstruction etage;
    etage.prepare (fsSortie);
    etage.majHorloge (cfg, 1.0);

    Lecteur lect;
    lect.prepare (etage.fsInterne());
    lect.majHorloge (cfg, 1.0);
    lect.regleBoucle (info.nAttaque, info.nTotal);
    lect.reglePhase (0.0);

    sortie.assign ((size_t) nEch, 0.0);
    for (int i = 0; i < nEch; ++i)
    {
        double acc = 0.0;
        for (int s = 0; s < kSurEch; ++s)
        {
            const double e = lect.pasInterne (rom.data(), info.nTotal, cfg);
            acc = etage.decime (0, etage.reconstruit (0, e));
        }
        sortie[(size_t) i] = acc;
    }

    if (env != nullptr && env->actif)
    {
        // /!\ La note est TENUE pendant toute la fenetre de comparaison : on
        // n'applique donc que attaque-decroissance-tenue. Le relachement est
        // propose mais jamais entendu ici - il n'a pas de sens sur un
        // echantillon qu'on ne relache pas.
        Enveloppe e;
        e.prepare (fsSortie);
        e.regle (env->attaque, env->decli, env->tenue, env->relache);
        e.declenche();
        for (int i = 0; i < nEch; ++i) sortie[(size_t) i] *= e.suivant();
    }
}

/** ⭐ LA CONFIGURATION PAR DEFAUT DE LA RECHERCHE : TRANSPARENTE.

    On cherche la recette du SON, pas celle du son deja passe dans une machine.
    Deux raisons, et la seconde est mesuree :

    1. Une recette ajustee a travers la machine est ajustee A CETTE machine :
       elle la pre-compense. Changez d'epoque, ou passez en `PROPRE`, et elle
       devient fausse. Or le grain est un CHOIX de l'utilisateur, applique a la
       lecture - il ne doit pas etre cuit dans le materiau.

    2. La degradation POLLUE l'objectif. Le bruit mu-law se tient a -41,6 dB
       (mesure) : pour l'empecher de dominer la perte, il avait fallu remonter
       le plancher a -40 dB, donc jeter tout ce qui vit en dessous. Sur un
       objectif propre, ce plancher peut redescendre et la perte voit beaucoup
       plus de timbre.

    /!\ On garde en revanche fsRom : le taux de la ROM n'est pas une saveur,
    c'est la place disponible dans le materiau. Chercher a 44,1 kHz puis jouer
    a 27,7 kHz ferait disparaitre les partiels du haut apres coup.
*/
inline ConfigGrain grainTransparent (double fsSortie = 44100.0)
{
    ConfigGrain c;
    c.actif = false;                  // interpolation lineaire, pas d'escalier
    c.horloge = Horloge::Fixe;
    c.filtre  = Filtre::Fixe;
    c.quant   = Quant::Aucune;
    c.fsMachine = fsSortie;
    c.coupure = 0.45;                 // ouvert : on ne veut pas de bande d'epoque
    return c;
}

/** L'autre mode, sur demande : ajuster A TRAVERS la machine. Utile quand on
    veut que le resultat soit fidele UNE FOIS degrade - au prix de ne plus etre
    transposable a une autre machine. */
inline ConfigGrain grainRecherche (bool actif = true)
{
    ConfigGrain c;
    c.actif = actif;
    c.horloge = Horloge::Variable;
    c.filtre  = Filtre::Asservi;
    c.quant   = actif ? Quant::MuLaw : Quant::Aucune;
    return c;
}

} // namespace romance
