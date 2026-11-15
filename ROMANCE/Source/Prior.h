// =============================================================================
//  Prior.h - D'ou viennent les recettes d'entrainement.
//
//  ⭐ LE GENERATEUR EST LA VERITE. On tire une recette, on la rend, et on a une
//  paire (audio, 108 octets) dont l'etiquette est EXACTE. C'est la seule raison
//  pour laquelle ce projet peut avoir un reseau : un sample depose n'a pas
//  d'etiquette - personne ne sait quelle recette le produirait - mais dans
//  l'autre sens, c'est gratuit et infini.
//
//  /!\ ET C'EST AUSSI LE PIEGE. Tirer uniformement dans les 108 octets, c'est
//  entrainer sur des sons que PERSONNE NE FAIT : des empilements de partiels
//  sans queue ni tete. Le reseau apprendrait parfaitement une region ou aucun
//  sample reel ne se trouve.
//
//  D'ou ces chiffres, qui ne sont pas inventes : ce sont les quantiles MESURES
//  sur 1 665 sons du corpus (Oberheim Matrix 1000, OB-8, Prophet T8, Jupiter 8,
//  Juno 60, Rhodes Chroma, guitares, pads et basses de MUSIC 2000).
//
//                        p5      p25     p50     p75     p95
//    f0 (Hz)             40      66      131     262     698
//    duree (s)           0,47    1,81    3,41    6,62    8,78
//    part de bruit       0,000   0,007   0,090   0,557   0,931
//    centroide (Hz)      409     823     1789    3102    5883
//    attaque (s)         0,018   0,051   0,284   1,082   3,562
//    decroissance -12dB  0,03    0,19    0,53    1,48    4,94
//
//  /!\ ET LE TAUX D'ACCEPTATION NE SUFFIT PAS A JUGER LE PRIOR. Il valait 93 %
//  au premier essai : les bornes ne filtraient presque rien, et pourtant le jeu
//  genere ne ressemblait pas du tout au corpus. Ce qui compte est la
//  DISTRIBUTION, et elle ne se voit qu'en mettant les deux cote a cote
//  (`dataset.exe` ecrit ses propres mesures pour ca).
//
//  Etat de la comparaison apres trois corrections (medianes) :
//
//                     corpus   genere   ecart
//    f0 (Hz)            131      124     ok
//    centroide (Hz)    1789     2194     un peu brillant
//    part de bruit     0,090    0,075    ok
//    transitoire       0,53     0,59     ok
//    attaque (s)       0,284    0,160    encore deux fois trop rapide
//
//  Les trois corrections, chacune declenchee par la mesure :
//    1. f0 log-NORMALE calee sur la mediane du corpus (une log-uniforme mettait
//       la mediane un tiers trop haut) ;
//    2. une CLASSE D'ATTAQUE COMMUNE a tous les partiels - la recette n'a pas
//       d'enveloppe d'ensemble, donc tirer la montee partiel par partiel ne
//       produisait que des attaques rapides (29 ms de mediane contre 284) ;
//    3. compensation du gain du filtre de bruit, sans laquelle `bruitNiveau` ne
//       voulait pas dire la meme chose a 300 Hz et a 10 kHz.
//
//  /!\ CE QUI RESTE HORS D'ATTEINTE, et il faut le dire : la DECROISSANCE ne se
//  compare pas au-dela de la fenetre d'analyse (1,5 s). Le corpus a un p75 a
//  1,48 s et un p90 a 3,87 s : ces sons-la, ni la mesure ni le reseau ne les
//  voient decroitre. Ce n'est pas un defaut du prior, c'est la limite de ce
//  qu'on regarde.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Profil.h"
#include "Recette.h"

namespace romance
{

/** Generateur pseudo-aleatoire deterministe : deux jeux de donnees avec la
    meme graine sont identiques. Sans ca on ne peut pas comparer deux
    entrainements. */
class Des
{
public:
    explicit Des (unsigned int graine = 1u) : e_ (graine * 2654435761u + 1u) {}

    inline double uni() noexcept
    {
        e_ = e_ * 1664525u + 1013904223u;
        return (double) (e_ >> 8) / 16777216.0;
    }
    inline double entre (double a, double b) noexcept { return a + (b - a) * uni(); }
    inline double gauss() noexcept
    {
        const double u1 = std::max (1e-12, uni()), u2 = uni();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * kPi * u2);
    }
    /** Tirage log-uniforme : pour tout ce qui se lit en octaves ou en decades
        (frequences, temps). Une loi uniforme sur 40-700 Hz mettrait 80 % des
        tirages au-dessus de 170 Hz. */
    inline double logEntre (double a, double b) noexcept
    {
        return std::exp (entre (std::log (a), std::log (b)));
    }
    inline unsigned int brut() noexcept
    {
        e_ = e_ * 1664525u + 1013904223u;
        return e_;
    }

private:
    unsigned int e_;
};

/** Une recette tiree dans la region du corpus. */
inline Recette tireRecette (Des& d)
{
    Recette r;

    // --- hauteur : log-normale calee sur le corpus (mediane 131 Hz, p10 56,
    // p90 523). /!\ Une log-uniforme sur 33-900 Hz donnait une mediane a
    // 170 Hz - un tiers trop haut. Mesure, pas intuition.
    r.f0 = std::max (28.0, std::min (1200.0,
                std::exp (std::log (131.0) + 0.87 * d.gauss())));

    // --- duree d'attaque : c'est la partie qui evolue.
    r.dureeAttaque = std::min (2.04, d.logEntre (0.15, 2.0));
    r.cyclesBoucle = std::max (2, std::min (64,
                        (int) std::round (r.f0 * d.entre (0.06, 0.20))));

    // --- combien de partiels, et quelle pente. La pente pilote le centroide :
    // -0,4 donne un son brillant, -2,2 un son sourd.
    r.nPartiels = (int) d.entre (3.0, (double) kPartielsMax + 0.999);

    // ⭐ UNE CLASSE D'ATTAQUE GLOBALE, et c'est la correction la plus
    // importante du prior. La recette n'a pas d'enveloppe d'amplitude
    // d'ensemble : la seule facon d'obtenir une attaque lente est que TOUS les
    // partiels montent ensemble. En tirant la montee partiel par partiel, on
    // ne produisait que des attaques rapides - mediane 29 ms contre 284 ms
    // dans le corpus, dix fois trop court. Les nappes n'existaient pas.
    const double uAtt = d.uni();
    // /!\ Repartition calee sur le corpus, pas sur l'intuition : sa mediane
    // d'attaque est a 284 ms, donc la MOITIE des sons montent plus lentement
    // que 0,28 s. Une repartition 45/27/18/10 laissait 72 % des tirages sous
    // 40 ms et la mediane generee a 109 ms.
    const int monteeCommune = (uAtt < 0.30) ? 0        // percussif, immediat
                            : (uAtt < 0.50) ? 1        // 40 ms
                            : (uAtt < 0.78) ? 2        // 300 ms
                                            : 3;       // 1,6 s - la nappe
    const double pente = d.entre (0.35, 2.4);
    const bool impairs = d.uni() < 0.15;     // orgues, clarinettes
    const double formant = d.uni() < 0.35 ? d.entre (2.0, 12.0) : 0.0;
    const double largFormant = d.entre (1.5, 5.0);

    for (int k = 0; k < r.nPartiels; ++k)
    {
        const double n = (double) (k + 1);
        double a = std::pow (n, -pente);
        if (impairs && (k % 2) == 1) a *= 0.06;
        if (formant > 0.0)
        {
            // une bosse de formant : ce qui separe une voyelle d'une dent de scie
            const double dd = (n - formant) / largFormant;
            a *= 1.0 + 1.8 * std::exp (-dd * dd);
        }
        a *= std::exp (0.35 * d.gauss());     // du desordre, sinon tout est lisse
        r.partiels[k].amplitude = std::max (0.0, std::min (1.0, a));

        // ⭐ l'aigu part AVANT le grave, sur a peu pres tout ce qui vibre.
        // Le desordre autour de cette regle est ce qui distingue deux timbres.
        const double base = d.entre (0.10, 0.75);
        const double pentDec = d.entre (0.0, 0.045);
        r.partiels[k].decroiss = std::max (0.0, std::min (1.0,
                                    base + pentDec * (double) k + 0.05 * d.gauss()));
        r.partiels[k].desaccord = (d.uni() < 0.3) ? d.entre (-25.0, 25.0) : 0.0;
        // montee : la classe commune, avec un peu de dispersion autour. C'est
        // ce leger decalage entre partiels qui fait qu'une nappe s'ouvre au
        // lieu d'apparaitre d'un bloc.
        int m = monteeCommune;
        const double u = d.uni();
        if (u < 0.18 && m > 0) --m;
        else if (u > 0.88 && m < 3) ++m;
        r.partiels[k].montee = m;
    }

    r.inharmonicite = (d.uni() < 0.25) ? d.entre (0.0, 1.0) : 0.0;

    // --- bruit : le corpus est franchement bimodal (p25 = 0,007, p75 = 0,557).
    // On respecte cette bimodalite au lieu de tirer au milieu, ou personne
    // n'est.
    // /!\ Le niveau de bruit de la recette est RELATIF a la somme des
    // partiels, pas au rendu final. Avec 0,05-0,85 le rendu ne montait qu'a
    // 7,6 % de part de bruit au p75, la ou le corpus est a 55,7 % : bien trop
    // propre. On monte donc franchement la branche bruitee.
    if (d.uni() < 0.45)
    {
        r.bruit[0].niveau = d.entre (0.0, 0.06);
        r.bruit[1].niveau = d.entre (0.0, 0.04);
    }
    else
    {
        r.bruit[0].niveau = d.entre (0.30, 1.0);
        r.bruit[1].niveau = d.entre (0.15, 1.0);
    }
    r.bruit[0].coupure = d.entre (0.0, 0.8);
    r.bruit[0].decroiss = d.entre (0.0, 1.0);
    r.bruit[1].coupure = d.entre (0.5, 1.0);
    r.bruit[1].decroiss = d.entre (0.0, 1.0);

    // --- transitoire
    if (d.uni() < 0.5)
    {
        r.transNiveau = d.entre (0.05, 0.9);
        r.transDuree = d.logEntre (0.002, 0.06);
        r.transCouleur = d.entre (0.0, 1.0);
    }

    // --- enveloppe de hauteur : rare, mais c'est le 808 et le tom
    if (d.uni() < 0.12)
    {
        r.pitchProf = d.entre (-24.0, 24.0);
        r.pitchDuree = d.logEntre (0.01, 0.4);
        r.pitchCourbe = d.entre (0.0, 1.0);
    }

    // --- multi-f0 : rare aussi. Un son est PARFOIS plusieurs notes.
    if (d.uni() < 0.15)
    {
        static const double intervalles[] = { 1.5, 2.0, 1.25, 1.3348, 0.5, 3.0 };
        r.ratio[1] = intervalles[(int) d.entre (0.0, 5.999)];
        r.niveau[1] = d.entre (0.2, 0.9);
    }
    if (d.uni() < 0.05)
    {
        r.ratio[2] = d.entre (0.5, 4.0);
        r.niveau[2] = d.entre (0.15, 0.7);
    }

    r.graine = (d.brut() & 0xFFFFu);
    return normalise (r);
}


// ===========================================================================
//  ⭐ LE PRIOR SUR LES PROFILS - c'est ce qui permet d'entrainer le reseau sur
//  LES QUATRE MOTEURS, sur les modulations et sur les effets.
//
//  Jusqu'ici le generateur ne savait tirer qu'un MATERIAU additif : le reseau
//  ne pouvait donc apprendre que la ROM, et il ne concourait que lorsque le
//  vote de moteur retenait la ROM. Il regardait la moitie du corpus sans avoir
//  le droit de repondre.
//
//  /!\ MEME PIEGE QU'AVANT, DEUX ETAGES PLUS HAUT. Tirer uniformement dans les
//  184 octets donnerait des patchs que personne ne fait : quatre operateurs FM
//  a plein niveau, une reverbe systematique, deux LFO cables au hasard. On tire
//  donc SPARSE - la plupart des sons n'ont ni modulation ni effet, et c'est
//  cette rarete-la qu'il faut reproduire, pas la moyenne.
//
//  /!\ ET LES PROPORTIONS DE MOTEUR NE SONT PAS UN QUART CHACUN. Le corpus
//  vise est melodique et d'epoque : l'analogique et la ROM y dominent, la FM y
//  est nettement moins frequente, la table d'ondes est rare. Un tirage uniforme
//  entrainerait le reseau a proposer de la FM une fois sur quatre alors qu'elle
//  explique un son sur dix.
// ===========================================================================

/** Tire un profil complet dans la region ou vivent les sons reels. */
inline void tireProfil (Des& d, uint8_t* o)
{
    using namespace profil;
    profilNeutre (o);

    // ---- le moteur : 35 % ROM, 40 % analogique, 15 % FM, 10 % table
    const double u = d.uni();
    const int moteur = (u < 0.35) ? 0 : (u < 0.75 ? 1 : (u < 0.90 ? 2 : 3));
    o[kOffMoteur] = (uint8_t) (moteur * 64);

    // ---- le materiau : seulement s'il sert
    if (moteur == 0) poseRecetteDansProfil (o, tireRecette (d));

    // BUG CORRIGE : la hauteur se tire pour TOUS les moteurs.
    // `rendProfil` lit la hauteur dans les octets de recette, y compris quand
    // le moteur n'est pas la ROM. Comme `tireRecette` n'etait appelee que pour
    // la ROM, les deux tiers du jeu se rendaient a la hauteur NEUTRE - 261,6 Hz
    // pour tout le monde. Mesure : mediane a 234 Hz contre 131 pour le corpus,
    // et un jeu ou le reseau voyait trois fois le meme do.
    {
        Recette r = depuisOctets (o);
        r.f0 = std::max (28.0, std::min (1200.0,
                    std::exp (std::log (131.0) + 0.87 * d.gauss())));
        poseRecetteDansProfil (o, r);
    }

    // ---- l'enveloppe d'amplitude, calee sur les quantiles du corpus
    // (attaque p25 = 0,051 s, p50 = 0,284, p75 = 1,082)
    o[kOffEnv + 0] = qlg (d.logEntre (0.002, 1.5), 0.001, 3.0);
    // /!\ Decroissance elargie pour la meme raison : mesure a 0,186 s de
    // mediane contre 0,506 pour le corpus. L'enveloppe de couche et la
    // decroissance du materiau se MULTIPLIENT - deux decroissances moyennes
    // font une decroissance courte.
    o[kOffEnv + 1] = qlg (d.logEntre (0.15, 12.0), 0.005, 8.0);
    o[kOffEnv + 2] = (uint8_t) (d.uni() < 0.45 ? d.entre (0.0, 60.0)
                                               : d.entre (120.0, 255.0));
    o[kOffEnv + 3] = qlg (d.logEntre (0.02, 4.0), 0.005, 8.0);

    // ---- les reglages du moteur actif
    if (moteur == 1)
    {
        o[kOffVA + 0] = (uint8_t) d.entre (0.0, 255.9);
        o[kOffVA + 1] = (uint8_t) d.entre (0.0, 255.9);
        o[kOffVA + 2] = (uint8_t) d.entre (0.0, 255.9);
        // /!\ L'accord du second oscillateur n'est PAS uniforme : les
        // intervalles qui existent sur les machines sont l'unisson, l'octave
        // et la quinte. Tirer uniformement entre -24 et +24 demi-tons donne
        // des secondes mineures que personne ne programme.
        static const double demiTons[] = { 0.0, 0.0, 0.0, 12.0, -12.0, 7.0, 5.0, 24.0 };
        o[kOffVA + 3] = qli (demiTons[(int) d.entre (0.0, 7.999)], -24.0, 24.0);
        o[kOffVA + 4] = qli (d.gauss() * 8.0, -50.0, 50.0);
        o[kOffVA + 5] = qli (d.entre (0.1, 0.9), 0.05, 0.95);
        o[kOffVA + 6] = (uint8_t) (d.uni() < 0.5 ? 0.0 : d.entre (0.0, 200.0));
        o[kOffVA + 7] = (uint8_t) (d.uni() < 0.7 ? 0.0 : d.entre (0.0, 120.0));
    }
    else if (moteur == 2)
    {
        o[kOffFM + 0] = (uint8_t) d.entre (0.0, 255.9);
        o[kOffFM + 1] = (uint8_t) (d.uni() < 0.6 ? 0.0 : d.entre (0.0, 180.0));
        o[kOffFM + 2] = qli (d.entre (36.0, 84.0), 24.0, 96.0);
        // /!\ Le suivi de clavier est tire GENEREUSEMENT : c'est ce qui rend
        // une FM jouable sur tout le clavier, et un jeu ou il serait toujours
        // nul apprendrait au reseau a ne jamais s'en servir.
        o[kOffFM + 3] = (uint8_t) (d.uni() < 0.4 ? 0.0 : d.entre (40.0, 200.0));
        for (int i = 0; i < kOperateurs; ++i)
        {
            uint8_t* p = o + kOffFM + 4 + i * 9;
            p[2] = qlg (d.logEntre (0.0008, 0.6), 0.0005, 3.0);
            p[3] = qlg (d.logEntre (0.02, 6.0), 0.005, 8.0);
            p[4] = (uint8_t) (d.uni() < 0.5 ? d.entre (0.0, 70.0) : d.entre (120.0, 255.0));
            p[5] = qlg (d.logEntre (0.05, 4.0), 0.005, 8.0);
            p[6] = qli (d.uni() < 0.35 ? 0.0 : d.entre (0.0, 0.8), -1.0, 1.0);
            // /!\ RAPPORTS ENTIERS EN MAJORITE. Un rapport non entier donne un
            // spectre inharmonique - une cloche. C'est un son, pas la moitie
            // des sons : le tirer une fois sur deux ferait un jeu de donnees
            // ou tout sonne metallique.
            static const double rapports[] = { 1.0, 1.0, 2.0, 3.0, 4.0, 0.5, 7.0, 1.41 };
            p[0] = qlg (rapports[(int) d.entre (0.0, 7.999)], 0.25, 16.0);
            p[1] = (uint8_t) (i == 0 ? d.entre (200.0, 255.0) : d.entre (0.0, 220.0));
            // ⭐ DESACCORD : nul la plupart du temps, faible quand il existe.
            // Un jeu ou il serait souvent grand apprendrait au reseau a
            // proposer des sons faux ; nul en permanence lui apprendrait a
            // ne jamais s'en servir. Il faut les deux.
            p[7] = qli (d.uni() < 0.6 ? 0.0 : d.entre (-12.0, 12.0), -25.0, 25.0);
            // ⭐ VITESSE : plus souvent sur les MODULATEURS que sur
            // l'operateur 1, parce que c'est la qu'elle rend expressif.
            p[8] = (uint8_t) (d.uni() < (i == 0 ? 0.75 : 0.45)
                                 ? 0.0 : d.entre (60.0, 255.0));
        }
    }
    else if (moteur == 3)
    {
        o[kOffTable + 0] = (uint8_t) d.entre (0.0, 255.9);
        o[kOffTable + 1] = (uint8_t) (d.uni() < 0.7 ? 0.0 : 200.0);
        o[kOffTable + 2] = (uint8_t) d.entre (0.0, 255.9);
        o[kOffTable + 3] = qli (d.entre (0.2, 0.8), 0.05, 0.95);

        // ⭐⭐ LA GRILLE DESSINEE, TIREE LISSE - jamais en bruit blanc.
        //
        // /!\ Seize octets independants tires au hasard font un BRUIT, pas
        // une forme d'onde : le reseau apprendrait que « dessine » veut dire
        // « sale », et la recherche n'aurait jamais de point de depart
        // musical. On tire donc une SOMME D'HARMONIQUES - c'est ce qu'un
        // humain dessine quand il dessine une onde - puis on laisse la
        // mutation deformer cellule par cellule.
        {
            const int nH = (int) d.entre (1.0, 6.99);
            double a[7] = { 0.0 }, ph[7] = { 0.0 };
            for (int k = 1; k <= nH; ++k)
            {
                a[k] = d.entre (0.0, 1.0) / (double) k;
                ph[k] = d.uni();
            }
            double crete = 1e-9, v[kCellulesProfil];
            for (int i = 0; i < kCellulesProfil; ++i)
            {
                const double u = ((double) i + 0.5) / (double) kCellulesProfil;
                double y = 0.0;
                for (int k = 1; k <= nH; ++k)
                    y += a[k] * std::sin (2.0 * kPi * ((double) k * u + ph[k]));
                v[i] = y;
                crete = std::max (crete, std::abs (y));
            }
            for (int i = 0; i < kCellulesProfil; ++i)
                o[kOffGrille + i] = (uint8_t) std::lround (
                                        (v[i] / crete * 0.5 + 0.5) * 255.0);
        }
        // finesse et profondeur : la finesse penche vers le fin, la
        // profondeur vers le propre - les valeurs extremes restent
        // atteignables par mutation.
        o[kOffGrille + kCellulesProfil] =
            (uint8_t) (((int) d.entre (1.0, 3.99) << 6)
                     | ((int) d.entre (0.0, 5.99) << 3));


    // ⭐⭐ LE SECOND MATERIAU, DANS LE JEU D'ENTRAINEMENT.
    //
    // /!\ UN TIRAGE SUR CINQ, pas un sur deux. Le spectre ne decrit bien que
    // ce qui n'est pas harmonique - l'etalon lui donne 6,59 dB seul contre
    // 4,77 pour la recette. Un jeu ou il apparaitrait la moitie du temps
    // apprendrait au reseau a le proposer pour des sons harmoniques, ou il
    // est moins bon. Un sur cinq suffit a ce qu'il sache l'utiliser quand il
    // faut, sans qu'il devienne son reflexe.
    o[kOffMateriau] = (uint8_t) (d.uni() < 0.20 ? 200 : 0);
    {
        // ⭐ Le spectre est tire LISSE - jamais bande par bande au hasard.
        // Soixante-quatre valeurs independantes font du bruit blanc bosselé,
        // pas un timbre : le reseau apprendrait que « spectre » veut dire
        // « souffle », et la recherche n'aurait aucun point de depart musical.
        const int nBosses = (int) d.entre (1.0, 4.99);
        double c[5] = { 0.0 }, l[5] = { 0.0 }, h[5] = { 0.0 };
        for (int k = 0; k < nBosses; ++k)
        {
            c[k] = d.entre (0.0, (double) kBandesSpectre);
            l[k] = d.entre (1.5, 20.0);
            h[k] = d.entre (0.3, 1.0);
        }
        for (int i2 = 0; i2 < kBandesSpectre; ++i2)
        {
            double v = 0.0;
            for (int k = 0; k < nBosses; ++k)
            {
                const double u = ((double) i2 - c[k]) / l[k];
                v += h[k] * std::exp (-u * u);
            }
            o[kOffSpectre + i2] = (uint8_t) std::lround (
                                      255.0 * std::min (1.0, v));
        }
        o[kOffSpParam + 0] = (uint8_t) d.entre (0.0, 255.9);          // pente
        o[kOffSpParam + 1] = qlg (d.logEntre (0.001, 1.0), 0.0005, 3.0);
        o[kOffSpParam + 2] = qlg (d.logEntre (0.05, 6.0), 0.01, 8.0);
        o[kOffSpParam + 3] = (uint8_t) (d.uni() < 0.5 ? d.entre (0.0, 60.0)
                                                      : d.entre (120.0, 255.0));
    }    }

    // ---- le filtre
    // /!\ MESURE, PAS DEVINEE - et la premiere version etait FAUSSE. Avec une
    // coupure tiree entre 0,8 et 48 fois la hauteur, le jeu genere sortait a un
    // centroide median de 695 Hz contre 1 767 Hz pour le corpus : deux fois et
    // demie trop sourd. La raison est structurelle et vaut d'etre notee : le
    // rendu passe desormais par la COUCHE ENTIERE, donc par le filtre musical,
    // alors que l'ancien `rendDegrade` ne l'appliquait pas du tout. Le meme
    // prior ne veut plus dire la meme chose parce que le rendu a change.
    o[kOffFiltre + 0] = (uint8_t) d.entre (0.0, 255.9);
    // /!\ La plage monte a 250 comme le parametre du plugin. Tiree entre 12 et
    // 64, la coupure ne pouvait proposer que des sons deja filtres - la
    // recherche partait sombre quel que soit le sample.
    o[kOffFiltre + 1] = qlg (d.logEntre (10.0, 250.0), 0.25, 250.0);
    o[kOffFiltre + 2] = (uint8_t) (d.uni() < 0.5 ? d.entre (0.0, 60.0)
                                                 : d.entre (60.0, 230.0));
    o[kOffFiltre + 3] = (uint8_t) d.entre (0.0, 255.9);
    o[kOffFiltre + 4] = (uint8_t) d.entre (150.0, 255.0);
    // ⭐ LE PASSE-HAUT : ouvert deux fois sur trois. Un jeu ou il serait
    // toujours actif apprendrait au reseau que tout sample est degraisse ;
    // toujours ouvert, il ne saurait jamais s'en servir.
    o[kOffFiltre + 5] = (uint8_t) (d.uni() < 0.66 ? 0.0 : d.entre (40.0, 200.0));
    o[kOffFiltre + 6] = (uint8_t) (d.uni() < 0.8 ? d.entre (0.0, 60.0)
                                                 : d.entre (60.0, 240.0));
    o[kOffFiltre + 7] = (uint8_t) (d.uni() < 0.7 ? 0.0 : 255.0);

    // ---- unisson
    o[kOffUni + 0] = qli (d.uni() < 0.5 ? d.entre (0.0, 6.0) : d.entre (6.0, 30.0),
                          0.0, 40.0);
    o[kOffUni + 1] = (uint8_t) d.entre (0.0, 255.9);

    // ---- grain : la moitie des sons d'epoque en portent
    if (d.uni() < 0.5)
    {
        o[kOffGrain + 0] = (uint8_t) d.entre (64.0, 255.9);
        o[kOffGrain + 1] = qli (d.entre (0.10, 0.45), 0.05, 0.45);
        o[kOffGrain + 2] = (uint8_t) (d.uni() < 0.5 ? 0.0 : 200.0);
    }

    // ---- modulations : SPARSE. Un son sur deux n'en a aucune.
    for (int i = 0; i < kModsProfil; ++i)
    {
        if (d.uni() > (i == 0 ? 0.5 : 0.25)) continue;
        uint8_t* m = o + kOffMod + i * 5;
        const bool env = d.uni() < 0.45;
        m[0] = (uint8_t) (env ? 128 : 64);           // enveloppe ou LFO
        m[1] = env ? qlg (d.logEntre (0.002, 1.0), 0.001, 3.0)
                   : qlg (d.logEntre (0.2, 9.0), 0.02, 12.0);
        m[2] = (uint8_t) d.entre (0.0, 255.9);
        m[3] = (uint8_t) d.entre (0.0, 255.9);
        m[4] = env ? qlg (d.logEntre (0.05, 4.0), 0.005, 8.0)
                   : (uint8_t) d.entre (0.0, 255.9);

        uint8_t* c = o + kOffCable + i * 2;
        // /!\ Les destinations ne sont pas equiprobables : la coupure et la
        // hauteur portent l'immense majorite des modulations qu'on entend sur
        // un sample. Le morph n'existe que sur une topologie de filtre.
        // /!\ L'octet encode un INDICE dans la table des 38 destinations, pas
        // un des huit anciens `Dest` : le facteur 32 d'avant donnait desormais
        // n'importe quoi (destination 2 -> indice 11).
        static const int dests[] = { 1, 2, 2, 2, 1, 3, 4, 26 };  // 26 = table position
        const int di = dests[(int) d.entre (0.0, 7.999)];
        c[0] = (uint8_t) std::min (255.0,
                   ((double) di + 0.5) * 256.0 / (double) kNbDestinations);
        // et une dose FAIBLE en general : la destination hauteur porte deux
        // octaves a pleine dose, ce qui est une sirene, pas un vibrato
        const double dose = d.gauss() * 0.12;
        c[1] = qli (std::max (-1.0, std::min (1.0, dose)), -1.0, 1.0);
    }

    // ---- effets : SPARSE aussi, et c'est le point le plus important du lot.
    // /!\ Un jeu de donnees ou tout baigne dans la reverbe apprendrait au
    // reseau a en mettre partout - y compris sur les samples secs, ou elle
    // couterait ensuite du budget de recherche uniquement pour l'enlever.
    if (d.uni() < 0.30) o[kOffFx + 0] = (uint8_t) d.entre (20.0, 140.0);   // chorus
    if (d.uni() < 0.15) o[kOffFx + 6] = (uint8_t) d.entre (10.0, 110.0);   // delai
    if (d.uni() < 0.35) o[kOffFx + 7] = (uint8_t) d.entre (15.0, 150.0);   // reverbe
    if (d.uni() < 0.25) o[kOffFx + 10] = (uint8_t) d.entre (20.0, 180.0);  // saturation
    if (d.uni() < 0.20)
    {
        o[kOffFx + 12] = qli (d.gauss() * 3.0, -12.0, 12.0);
        o[kOffFx + 13] = qli (d.gauss() * 3.0, -12.0, 12.0);
    }
    if (d.uni() < 0.10) o[kOffFx + 14] = (uint8_t) d.entre (20.0, 140.0);  // ensemble
}

} // namespace romance
