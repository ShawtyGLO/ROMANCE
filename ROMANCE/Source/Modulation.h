// =============================================================================
//  Modulation.h - Huit emplacements de NATURE LIBRE, et une matrice.
//
//  Idee de l'utilisateur : un emplacement accueille une enveloppe, un LFO ou
//  une source aleatoire, au choix. Besoin d'une enveloppe de plus ? On convertit
//  un LFO. Plus souple que des types figes, et strictement plus capable.
//
//  /!\ LES CABLAGES PAR DEFAUT SONT PRE-ETABLIS. Un synthe qui s'ouvre sans
//  aucune modulation cablee est un synthe sans caractere. Et le defaut
//  `LFO 1 -> hauteur` n'est pas une convention : la mesure a montre que la
//  signature de PAD 1 (84 cents de largeur de raie) est INATTEIGNABLE sans
//  modulation de hauteur - trois voix desaccordees a 80 cents n'en donnent
//  que 41.
//
//  ⭐ TAUX DE CONTROLE GROSSIER : les modulations sont calculees tous les
//  kPasControle echantillons, pas a chaque echantillon. C'est d'abord une
//  necessite (recalculer les coefficients d'un filtre a chaque echantillon
//  coute des tan()), mais c'est AUSSI un mecanisme d'epoque documente - le
//  mouvement « en escalier » des enveloppes est l'un des cinq facteurs de grain
//  qu'on avait releves. La contrainte technique et le caractere coincident.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "Couche.h"

namespace romance
{

constexpr int kNbMods    = 8;    // emplacements de modulation
/** ⭐⭐ SEIZE CABLES, ET C'EST LE GLISSER-DEPOSER QUI L'IMPOSE.

    A huit, une matrice qu'on remplit par menus reste a moitie vide : le geste
    coute assez cher pour qu'on reflechisse avant. Des qu'on peut cabler en
    LACHANT une source sur un bouton, huit s'epuisent en deux minutes - et une
    interface fluide qui bute sur un mur est plus frustrante qu'une liste
    honnete.

    /!\ ET CA NE TOUCHE PAS AU PROFIL. Genopatch n'encode que `kModsProfil`
    cables : la recherche, le reseau et tous les profils enregistres sont
    indifferents a ce nombre. C'est ce qui rend le changement gratuit. */
constexpr int kNbRoutages = 16;  // lignes de matrice
constexpr int kPasControle = 16; // echantillons entre deux calculs de modulation

enum class TypeMod { Aucun, Enveloppe, LFO, Aleatoire };

/** Ce qu'une modulation peut atteindre.

    ⭐ `CoupureGrain` est une destination que personne d'autre n'a : c'est la
    coupure du filtre de RECONSTRUCTION, donc l'horloge de la machine. Moduler
    ca, c'est faire respirer le convertisseur lui-meme.
*/
enum class Dest { Aucune, Hauteur, Coupure, Niveau, Desaccord, Pan,
                  CoupureGrain, Morph, NB };

/** ⭐⭐ LA TABLE DES DESTINATIONS - « pouvons-nous tout moduler ? ».

    Oui, et sans reecrire une ligne de synthese : une destination est un
    POINTEUR VERS UN CHAMP de `ReglagesCouche`, plus l'echelle et les bornes de
    ce champ. La voix garde une copie modulee de ses reglages et n'y reecrit que
    les champs reellement cables - huit au maximum, puisqu'il y a huit lignes de
    matrice.

    /!\ LES BORNES NE SONT PAS UN DETAIL, C'EST LE COEUR DE LA TABLE. On a deja
    paye cette erreur une fois : le filtre analogique divergeait quand la
    coupure bougeait, parce qu'une valeur sortait de sa plage. Ajouter une
    modulation a `reso` sans borne la pousse au-dela de 0,95 et le filtre
    explose - avec un LFO, ca veut dire qu'il explose une fois par cycle.

    /!\ LES CHAMPS DISCRETS N'Y SONT PAS, et c'est une propriete du choix plutot
    qu'un oubli : la table ne liste que des `double`, donc le moteur, la forme
    d'onde et la topologie de filtre en sont exclus d'office. Moduler
    continument un choix qui n'a pas d'entre-deux ne veut rien dire (regle
    Synplant, la meme qui a fait renoncer aux menus dans les LFO).

    /!\ ET LES SEPT PREMIERES RESTENT CABLEES A LA MAIN. `hauteur`, `coupure`,
    `niveau`, `desaccord`, `pan`, `coupureGrain` et `morph` passent par
    `Couche::majHauteur`, qui les recoit en argument au lieu de les lire dans
    les reglages - notamment parce que la hauteur n'EST PAS un champ de la
    couche. Elles gardent donc leurs indices 1 a 7 : un patch enregistre avant
    cette table continue de se relire correctement.
*/
enum class ModeDest
{
    Ajoute,      // champ += x * echelle
    Octaves      // champ *= 2^(x * echelle) - pour tout ce qui se lit en octaves
};

struct Destination
{
    const char* nom;
    /** Nul pour les sept destinations historiques : elles ne passent pas par
        un champ mais par les arguments de `majHauteur`. */
    double* (*champ) (ReglagesCouche&);
    double echelle;
    double mini, maxi;
    ModeDest mode;
};

// Les entrees 0..7 doivent rester dans cet ordre : ce sont celles de `Dest`,
// et l'index est enregistre dans les patchs.
inline const Destination* tableDestinations() noexcept
{
    static const Destination t[] =
    {
        { "-",              nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "hauteur",        nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "coupure",        nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "niveau",         nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "desaccord",      nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "pan",            nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "coupure grain",  nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },
        { "morph",          nullptr, 0.0,  0.0, 0.0, ModeDest::Ajoute },

        // ---- analogique
        { "osc melange",    [] (ReglagesCouche& r) { return &r.va.melange; },   1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "osc 2 cents",    [] (ReglagesCouche& r) { return &r.va.cents2; },   50.0, -50.0, 50.0, ModeDest::Ajoute },
        { "largeur impuls", [] (ReglagesCouche& r) { return &r.va.largeur; },   0.45, 0.05, 0.95, ModeDest::Ajoute },
        { "sous-octave",    [] (ReglagesCouche& r) { return &r.va.sub; },       1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "bruit osc",      [] (ReglagesCouche& r) { return &r.va.bruit; },     1.0, 0.0, 1.0, ModeDest::Ajoute },

        // ---- FM
        { "FM retour",      [] (ReglagesCouche& r) { return &r.fm.retour; },    1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "FM op1 niveau",  [] (ReglagesCouche& r) { return &r.fm.niveau[0]; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "FM op2 niveau",  [] (ReglagesCouche& r) { return &r.fm.niveau[1]; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "FM op3 niveau",  [] (ReglagesCouche& r) { return &r.fm.niveau[2]; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "FM op4 niveau",  [] (ReglagesCouche& r) { return &r.fm.niveau[3]; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },
        // /!\ Les RAPPORTS se modulent en octaves, pas en addition : passer de
        // 1 a 2 et de 8 a 9 ne s'entend pas du tout pareil.
        { "FM op1 rapport", [] (ReglagesCouche& r) { return &r.fm.ratio[0]; },  2.0, 0.25, 16.0, ModeDest::Octaves },
        { "FM op2 rapport", [] (ReglagesCouche& r) { return &r.fm.ratio[1]; },  2.0, 0.25, 16.0, ModeDest::Octaves },
        { "FM op3 rapport", [] (ReglagesCouche& r) { return &r.fm.ratio[2]; },  2.0, 0.25, 16.0, ModeDest::Octaves },
        { "FM op4 rapport", [] (ReglagesCouche& r) { return &r.fm.ratio[3]; },  2.0, 0.25, 16.0, ModeDest::Octaves },
        { "FM op1 chute",   [] (ReglagesCouche& r) { return &r.fm.decli[0]; }, 4.0, 0.005, 8.0, ModeDest::Octaves },
        { "FM op2 chute",   [] (ReglagesCouche& r) { return &r.fm.decli[1]; }, 4.0, 0.005, 8.0, ModeDest::Octaves },
        { "FM op3 chute",   [] (ReglagesCouche& r) { return &r.fm.decli[2]; }, 4.0, 0.005, 8.0, ModeDest::Octaves },
        { "FM op4 chute",   [] (ReglagesCouche& r) { return &r.fm.decli[3]; }, 4.0, 0.005, 8.0, ModeDest::Octaves },

        // ---- table d'ondes
        // ⭐ C'est LA destination de la table : le balayage. Cabler un LFO ici,
        // c'est le geste du PPG - le spectre evolue pendant que la note tient.
        { "table position", [] (ReglagesCouche& r) { return &r.table.position; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "table symetrie", [] (ReglagesCouche& r) { return &r.table.symetrie; }, 0.45, 0.05, 0.95, ModeDest::Ajoute },

        // ---- filtre
        // /!\ 0,95 et pas 1,0 : au-dela le filtre s'auto-oscille et diverge.
        { "resonance",      [] (ReglagesCouche& r) { return &r.reso; },         0.95, 0.0, 0.95, ModeDest::Ajoute },
        { "suivi clavier",  [] (ReglagesCouche& r) { return &r.suiviClavier; }, 1.0, 0.0, 1.0, ModeDest::Ajoute },

        // ---- lecture de ROM
        { "boucle debut",   [] (ReglagesCouche& r) { return &r.boucleDebut; },  1.0, 0.0, 0.98, ModeDest::Ajoute },
        { "boucle fin",     [] (ReglagesCouche& r) { return &r.boucleFin; },    1.0, 0.02, 1.0, ModeDest::Ajoute },

        // ---- unisson et accord
        { "largeur unisson",[] (ReglagesCouche& r) { return &r.largeur; },      1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "accord fin",     [] (ReglagesCouche& r) { return &r.accordFin; },  100.0, -100.0, 100.0, ModeDest::Ajoute },

        // ---- enveloppe d'amplitude
        // /!\ En OCTAVES : une attaque n'a pas d'echelle additive. +0,1 s sur
        // une attaque de 3 ms la rend meconnaissable et ne fait rien a une
        // attaque de 2 s.
        { "attaque",        [] (ReglagesCouche& r) { return &r.attaque; },      3.0, 0.001, 3.0, ModeDest::Octaves },
        { "decroissance",   [] (ReglagesCouche& r) { return &r.decli; },        3.0, 0.005, 8.0, ModeDest::Octaves },
        { "tenue",          [] (ReglagesCouche& r) { return &r.tenue; },        1.0, 0.0, 1.0, ModeDest::Ajoute },
        { "relachement",    [] (ReglagesCouche& r) { return &r.relache; },      3.0, 0.005, 8.0, ModeDest::Octaves },
    };
    return t;
}

constexpr int kNbDestinations = 38;

/** ⭐⭐ LE PONT ENTRE UNE DESTINATION ET SON BOUTON EN FACADE.

    C'est la piece qui manquait pour tout le reste de l'interface. Une
    destination se decrit ici par un POINTEUR VERS UN CHAMP ; un bouton se
    decrit par une CHAINE (`c2_coupure`). Rien ne reliait les deux, donc :
      · un bouton ne pouvait pas savoir s'il etait module, ni par qui ;
      · on ne pouvait pas cabler en LACHANT une source sur un bouton ;
      · survoler une source ne pouvait rien allumer.
    Tout ce qui rend Pigments lisible passe par ce pont-la.

    /!\ ON REND L'IDENTIFIANT COURT, SANS LE PREFIXE DE COUCHE. Le prefixe
    (`c1_`...`c4_`) depend de la couche visee par le cable, que cette table ne
    connait pas. Coller un prefixe ici ferait quatre tables au lieu d'une.

    /!\ ET UNE CHAINE VIDE EST UNE REPONSE VALIDE : `hauteur` n'est le bouton
    de personne (c'est la note jouee), `boucle debut` n'a pas encore de bouton.
    Le controle a ecrire n'est donc pas « toutes les destinations ont un
    bouton » mais « aucune ne pointe vers un bouton qui n'existe pas », et
    celui-la se verifie contre l'APVTS. */
inline const char* idParamDestination (int i) noexcept
{
    static const char* t[kNbDestinations] =
    {
        "",             // -
        "",             // hauteur : la note jouee, pas un reglage
        "coupure",      // coupure du filtre musical
        "niveau",
        "desac",
        "pan",
        "coup_rom",     // coupure de reconstruction
        "morph",

        // ---- analogique
        "va_mix", "va_cents", "va_larg", "va_sub", "va_bruit",

        // ---- FM
        "fm_ret",
        "fm1_niv", "fm2_niv", "fm3_niv", "fm4_niv",
        "fm1_ratio", "fm2_ratio", "fm3_ratio", "fm4_ratio",
        "fm1_dec", "fm2_dec", "fm3_dec", "fm4_dec",

        // ---- table d'ondes
        "tb_pos", "tb_sym",

        // ---- filtre
        "reso", "suivi",

        // ---- lecture de ROM
        "b_deb", "b_fin",

        // ---- unisson et accord
        "largeur", "accord",

        // ---- enveloppe d'amplitude
        "att", "dec", "sus", "rel",
    };
    return (i < 0 || i >= kNbDestinations) ? "" : t[i];
}

/** L'inverse : la destination qui pilote ce bouton, ou 0 si aucune.

    ⭐ Lineaire et sans table de hachage, VOLONTAIREMENT. Trente-huit
    comparaisons de chaines courtes, appelees quand on dessine un bouton ou
    qu'on lache une source dessus - jamais dans le fil audio. Une structure
    plus rapide serait une structure de plus a garder synchronisee. */
inline int destinationDeParam (const char* idCourt) noexcept
{
    if (idCourt == nullptr || idCourt[0] == 0) return 0;
    for (int i = 1; i < kNbDestinations; ++i)
    {
        const char* c = idParamDestination (i);
        if (c[0] == 0) continue;
        const char* a = c; const char* b = idCourt;
        while (*a != 0 && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return 0;
}

/** Les noms, pour les menus et les panneaux. */
inline const char* nomDestination (int i) noexcept
{
    if (i < 0 || i >= kNbDestinations) return "-";
    return tableDestinations()[i].nom;
}

// ===========================================================================
//  ⭐⭐ LES DESTINATIONS GLOBALES - les effets.
//
//  Elles ne peuvent PAS vivre dans la table precedente, et la raison n'est pas
//  technique : il n'y a qu'une reverbe pour tout l'instrument. Moduler son
//  melange depuis une enveloppe DE VOIX n'a pas de sens des que trois notes
//  sonnent - laquelle gagne ? Une modulation d'effet doit etre calculee UNE
//  FOIS PAR BLOC, a partir de sources qui existent une seule fois elles aussi.
//
//  D'ou deux bancs de modulateurs : celui de chaque voix, et un banc GLOBAL qui
//  tourne en continu. Les huit emplacements sont les memes ; c'est leur lecture
//  qui differe.
//
//  /!\ ET LES ENVELOPPES GLOBALES SE REDECLENCHENT SUR LA PREMIERE NOTE D'UNE
//  PHRASE, pas sur chaque note. Redeclencher a chaque note ferait sauter la
//  queue de reverbe a chaque doigt pose, ce qui s'entend comme un defaut.
// ===========================================================================

/** Ce qu'une modulation peut atteindre dans la chaine d'effets. */
enum class DestGlobale
{
    Aucune,
    ChorusMelange, ChorusVitesse, ChorusProfondeur, ChorusCouleur,
    EnsembleMelange, EnsembleVitesse, EnsembleProfondeur,
    DelaiMelange, DelaiTemps, DelaiReinjection,
    ReverbeMelange, ReverbeTaille, ReverbeAmorti,
    // ⭐ Demande explicite : « moduler l'intensite ou la symetrie de la
    // saturation permet de donner plus de possibilites sonores ».
    Saturation, SaturationAsymetrie,
    TonaliteGrave, TonaliteAigu,
    // ---- les quatre effets ajoutes
    PhaserMelange, PhaserVitesse, PhaserProfondeur,
    MultiFiltreMelange, MultiFiltreCoupure, MultiFiltreResonance,
    TransfertMelange,
    CompresseurSeuil,
    NB
};

constexpr int kNbDestGlobales = (int) DestGlobale::NB;   // 26

/** Une destination globale : bornes comprises, comme les autres. */
struct DestinationGlobale
{
    const char* nom;
    double echelle;
    double mini, maxi;
};

inline const DestinationGlobale* tableDestGlobales() noexcept
{
    static const DestinationGlobale t[] =
    {
        { "-",                    0.0,   0.0,   0.0 },
        { "chorus melange",       1.0,   0.0,   1.0 },
        { "chorus vitesse",       6.0,   0.02,  8.0 },
        { "chorus profondeur",    1.0,   0.0,   1.0 },
        { "chorus couleur",       1.0,   0.0,   1.0 },
        { "ensemble melange",     1.0,   0.0,   1.0 },
        { "ensemble vitesse",     3.0,   0.05,  4.0 },
        { "ensemble profondeur",  1.0,   0.0,   1.0 },
        { "delai melange",        1.0,   0.0,   1.0 },
        { "delai temps",          1.0,   0.01,  2.0 },
        // /!\ 0,95 et pas 1,0 : a 1,0 la ligne a retard ne s'eteint plus.
        { "delai reinjection",    0.95,  0.0,   0.95 },
        { "reverbe melange",      1.0,   0.0,   1.0 },
        { "reverbe taille",       1.0,   0.0,   1.0 },
        { "reverbe amortissement",1.0,   0.0,   1.0 },
        { "saturation",           1.0,   0.0,   1.0 },
        { "saturation asymetrie", 1.0,   0.0,   1.0 },
        { "tonalite grave",      12.0, -12.0,  12.0 },
        { "tonalite aigu",       12.0, -12.0,  12.0 },
        { "phaser melange",       1.0,   0.0,   1.0 },
        { "phaser vitesse",       8.0,   0.01, 10.0 },
        { "phaser profondeur",    1.0,   0.0,   1.0 },
        { "multifiltre melange",  1.0,   0.0,   1.0 },
        // /!\ La coupure du multifiltre se module en HERTZ et sur une plage
        // enorme (20 Hz - 18 kHz). Une echelle additive y est absurde : +9000 Hz
        // sur 200 Hz est un saut de six octaves, sur 12 kHz c'est un tiers de
        // ton. On garde l'addition parce que la borne rattrape, mais c'est la
        // destination a moduler avec une petite dose.
        { "multifiltre coupure", 8000.0, 20.0, 18000.0 },
        { "multifiltre resonance",1.0,   0.0,   1.0 },
        { "transfert melange",    1.0,   0.0,   1.0 },
        { "compresseur seuil",   40.0, -40.0,   0.0 },
    };
    return t;
}

/** ⭐ Le bouton d'une destination globale, en identifiant COURT. Le rack
    (master ou tranche) met son propre prefixe : `rev_mix` sur le master,
    `fx2_rev_mix` sur la tranche 2. Une seule table pour les cinq racks. */
inline const char* idParamDestGlobale (int i) noexcept
{
    static const char* t[] =
    {
        "",
        "cho_mix", "cho_vit", "cho_prof", "cho_coul",
        "ens_mix", "ens_vit", "ens_prof",
        "del_mix", "del_tps", "del_rei",
        "rev_mix", "rev_taille", "rev_amorti",
        "sat", "sat_asym",
        "ton_grave", "ton_aigu",
        "pha_mix", "pha_vit", "pha_prof",
        "mf_mix", "mf_coup", "mf_reso",
        "trf_mix",
        "cmp_seuil",
    };
    return (i < 0 || i >= kNbDestGlobales) ? "" : t[i];
}

inline int destGlobaleDeParam (const char* idCourt) noexcept
{
    if (idCourt == nullptr || idCourt[0] == 0) return 0;
    for (int i = 1; i < kNbDestGlobales; ++i)
    {
        const char* a = idParamDestGlobale (i); const char* b = idCourt;
        while (*a != 0 && *a == *b) { ++a; ++b; }
        if (*a == 0 && *b == 0) return i;
    }
    return 0;
}

inline const char* nomDestGlobale (int i) noexcept
{
    if (i < 0 || i >= kNbDestGlobales) return "-";
    return tableDestGlobales()[i].nom;
}

/** ⭐ LES DESTINATIONS GLOBALES SE RANGENT APRES LES AUTRES DANS LA MEME LISTE.

    Un cable est un cable : obliger l'utilisateur a savoir qu'il existe deux
    matrices selon ce qu'il vise serait lui faire porter une contrainte
    d'implementation. Le menu est unique, l'indice dit lequel des deux chemins
    s'applique.
*/
constexpr int kPremiereDestGlobale = kNbDestinations;
constexpr int kNbDestinationsTotal = kNbDestinations + kNbDestGlobales - 1;

/** Le nom d'une destination, globale ou non. */
inline const char* nomDestinationTotale (int i) noexcept
{
    if (i < kNbDestinations) return nomDestination (i);
    return nomDestGlobale (i - kPremiereDestGlobale + 1);
}

/** Applique une destination globale a une valeur d'effet deja lue. */
inline double appliqueDestGlobale (int iGlobal, double x, double valeur) noexcept
{
    if (iGlobal <= 0 || iGlobal >= kNbDestGlobales) return valeur;
    const auto& d = tableDestGlobales()[iGlobal];
    return std::max (d.mini, std::min (d.maxi, valeur + x * d.echelle));
}

/** ⭐ QUATRE MACROS, ASSIGNABLES OU L'ON VEUT.

    Ce sont des SOURCES, pas des destinations : elles s'ajoutent aux huit
    emplacements et se cablent par la meme matrice. C'est ce qui les rend
    utiles - une macro qui aurait sa propre liste de cibles serait un
    quatrieme mecanisme a apprendre.

    /!\ ELLES NE SONT PAS LES TROIS MACROS DE LA PAGE JOUER. Usure, grain et
    mouvement DEPLACENT LE PATCH (voir Macros.h) : elles reecrivent des reglages,
    et la matrice ne les voit pas. Celles-ci ne touchent a rien, elles ne font
    que porter une valeur que des cables vont chercher. Deux mecanismes, deux
    noms - les confondre serait la meilleure facon de rendre les deux illisibles.
*/
constexpr int kNbMacros  = 4;

/** ⭐⭐ LA TOUCHE ET LA VELOCITE SONT DES SOURCES, comme un LFO.

    Elles existaient deja en dur - `suiviClavier` ouvre le filtre vers l'aigu,
    `veloNiveau` et `veloCoupure` repondent a la frappe - mais chacune n'allait
    QU'A UN ENDROIT, choisi par nous. En faire des sources de matrice les envoie
    partout : la velocite sur l'indice FM (le geste du DX7), la touche sur le
    morph A/B, la velocite sur le melange de reverbe, la touche sur la longueur
    de relachement. Aucun de ces cablages n'aurait sa place comme un reglage de
    plus ; tous sont evidents comme un cable.

    /!\ ELLES NE SE COMPORTENT PAS PAREIL, ET C'EST VOULU :
      · la VELOCITE est UNIPOLAIRE (0 a 1). Une frappe faible ne doit pas
        moduler « vers le negatif », elle doit ne rien moduler. Meme raison que
        les macros.
      · la TOUCHE est BIPOLAIRE, centree sur le do du milieu (note 60), +/-1 a
        cinq octaves de part et d'autre. Un suivi de clavier qui n'irait que
        vers le haut ne serait pas un suivi : le grave doit fermer autant que
        l'aigu ouvre.

    /!\ ET ELLES NE SONT PAS DANS LE PROFIL. Un rendu de recherche se fait a
    UNE note et UNE velocite (c'est deja pourquoi `coucheDepuisProfil` neutralise
    `veloNiveau` et compagnie) : un cable partant d'une source constante serait
    une DIMENSION MORTE, exactement le piege 35. Genopatch les recevra le jour
    ou son rendu jouera plusieurs notes - pas avant.
*/
constexpr int kNbSourcesJeu = 5;   // touche, velocite, bend, molette, pression
constexpr int kIdSourceTouche   = kNbMods + kNbMacros + 1;   // 1-base, comme la matrice
constexpr int kIdSourceVelo     = kNbMods + kNbMacros + 2;
constexpr int kIdSourceBend     = kNbMods + kNbMacros + 3;
constexpr int kIdSourceMolette  = kNbMods + kNbMacros + 4;
constexpr int kIdSourcePression = kNbMods + kNbMacros + 5;

constexpr int kNbSources = kNbMods + kNbMacros + kNbSourcesJeu;   // 17

/** Le nom court d'une source, en base 1. Un seul endroit qui le sait : le
    panneau de matrice l'ecrivait a trois endroits avec trois `if` differents. */
inline const char* nomSource (int src) noexcept
{
    static const char* kNoms[] = { "-", "1", "2", "3", "4", "5", "6", "7", "8",
                                   "M1", "M2", "M3", "M4",
                                   "TOUCHE", "VELO", "BEND", "MOLET", "PRESS" };
    return (src < 0 || src > kNbSources) ? "-" : kNoms[src];
}

/** ⭐ La valeur des deux sources de jeu, au meme endroit pour la voix et pour
    le banc global - sinon elles finiraient par ne plus dire la meme chose. */
inline double sourceTouche (int note) noexcept
{
    if (note < 0) return 0.0;
    return std::max (-1.0, std::min (1.0, ((double) note - 60.0) / 60.0));
}

inline double sourceVelo (int velo) noexcept
{
    return std::max (0.0, std::min (1.0, (double) velo / 127.0));
}

/** ⭐⭐ LES TROIS CONTROLEURS DE JEU - la faute la plus visible du projet,
    corrigee. Le plugin n'ecoutait NI la molette de hauteur, NI la molette de
    modulation, NI la pression : `isPitchWheel`, `isController` et
    `isChannelPressure` n'apparaissaient nulle part dans le depot. Un synthe
    qui ignore la molette n'est pas injouable par accident, il l'est par
    omission.

    /!\ LE BEND EST A PART DES DEUX AUTRES : il est CABLE EN DUR sur la
    hauteur, en plus d'etre une source. Une molette de hauteur qui ne fait rien
    tant qu'on n'a pas branche un cable serait cassee pour tout le monde sauf
    pour celui qui a lu la documentation. Les deux autres ne font rien par
    defaut - c'est la convention, et une molette qui bouge le son sans qu'on
    l'ait demande serait tout aussi fausse.

    /!\ BIPOLAIRE / UNIPOLAIRE / UNIPOLAIRE, comme le geste : le bend part du
    centre, la molette et la pression partent de zero. */
inline double sourceBend (double bend) noexcept
{
    return std::max (-1.0, std::min (1.0, bend));
}

inline double sourceUnipolaire (double x) noexcept
{
    return std::max (0.0, std::min (1.0, x));
}



/** Applique une destination generique (index >= 8) a des reglages de couche.

    /!\ ON BORNE APRES avoir applique, pas avant : c'est la valeur finale qui
    doit rester dans la plage, pas l'apport. */
inline void appliqueDestination (int i, double x, ReglagesCouche& r) noexcept
{
    if (i < 8 || i >= kNbDestinations) return;
    const Destination& d = tableDestinations()[i];
    if (d.champ == nullptr) return;
    double* c = d.champ (r);
    double v = (d.mode == ModeDest::Octaves)
                  ? (*c) * std::pow (2.0, x * d.echelle)
                  : (*c) + x * d.echelle;
    *c = std::max (d.mini, std::min (d.maxi, v));
}

/** ⭐⭐ CE QUE VAUDRAIT LE REGLAGE SI LA SOURCE ETAIT A `x`.

    C'est ce qui permet a l'anneau d'un bouton de montrer L'EXCURSION REELLE et
    pas la dose. La difference n'est pas cosmetique : une dose de 0,3 sur la
    coupure vaut 1,2 OCTAVE, la meme dose sur le panoramique vaut 0,3 - les
    dessiner de la meme longueur ferait mentir l'anneau sur ce qui compte.

    /!BS ELLE DOIT RENDRE EXACTEMENT CE QUE LE MOTEUR CALCULE, sinon l'anneau
    dit une chose et l'oreille en entend une autre. Les sept premieres
    destinations sont donc recopiees de `Voix::pasDeControle` - et si l'une
    change la-bas, ce controle-ci doit changer aussi. C'est le test 44 qui
    tient les deux ensemble.

    `base` est la valeur du reglage tel qu'il est pose. */
inline double valeurModulee (int dest, double x, double base) noexcept
{
    switch (dest)
    {
        case 0: return base;
        case 1: return base + x * 24.0;                  // hauteur, demi-tons
        case 2: return base * std::pow (2.0, x * 4.0);   // coupure, octaves
        case 3: return base + x;                         // niveau
        case 4: return base + x * 50.0;                  // desaccord, cents
        case 5: return base + x;                         // pan
        case 6: return base * std::pow (2.0, x * 2.0);   // coupure grain
        case 7: return base + x;                         // morph
        default: break;
    }
    if (dest < 0 || dest >= kNbDestinations) return base;
    const Destination& d = tableDestinations()[dest];
    const double v = (d.mode == ModeDest::Octaves)
                        ? base * std::pow (2.0, x * d.echelle)
                        : base + x * d.echelle;
    return std::max (d.mini, std::min (d.maxi, v));
}

struct ReglagesMod
{
    TypeMod type = TypeMod::Aucun;
    // /!\ QUATRE parametres pour l'enveloppe, pas trois : le relachement etait
    // code en dur a 0,25 s dans ma premiere version, donc ce n'etait pas une
    // ADSR. Or c'est precisement le role de ces emplacements - creer des ADSR
    // differentes et les assigner ou l'on veut, comme chez Pigments et Serum.
    double p1 = 0.3;    // LFO : vitesse (Hz)               · env : ATTAQUE (s)
    double p2 = 0.5;    // LFO : forme (0 sinus -> 1 carre) · env : DECROISSANCE
    double p3 = 0.5;    // LFO : symetrie                   · env : TENUE
    double p4 = 0.3;    // LFO : (inutilise)                · env : RELACHEMENT
    bool   retrig = true;
};

struct Routage
{
    int  source = 0;                 // 0 = aucune, 1..kNbMods
    /** ⭐ UN INDICE DANS `tableDestinations()`, plus un enum a sept valeurs.
        Les indices 0 a 7 sont exactement ceux de `Dest`, donc les patchs
        enregistres avant la table se relisent sans rien changer. */
    int  dest   = 0;
    int  couche = 0;                 // 0 = toutes les couches, 1..4
    double dose = 0.0;               // -1 .. +1
};

/** Un emplacement. Sa NATURE se choisit ; sa sortie est toujours dans [-1, 1]
    pour un LFO ou un aleatoire, et dans [0, 1] pour une enveloppe. */
class Modulateur
{
public:
    void prepare (double fsControle) noexcept
    {
        fs_ = std::max (1.0, fsControle);
        env_.prepare (fs_);
        phase_ = 0.0; v_ = 0.0; etatAlea_ = 12345u;
    }

    void declenche (const ReglagesMod& r) noexcept
    {
        env_.regle (r.p1, r.p2, r.p3, r.p4);
        env_.declenche();
        if (r.retrig) phase_ = 0.0;
    }

    void relache() noexcept { env_.relache(); }
    void coupe()   noexcept { env_.coupe(); v_ = 0.0; }

    inline double suivant (const ReglagesMod& r) noexcept
    {
        switch (r.type)
        {
            case TypeMod::Enveloppe:
                v_ = env_.suivant();
                break;

            case TypeMod::LFO:
            {
                phase_ += r.p1 / fs_;
                if (phase_ >= 1.0) phase_ -= std::floor (phase_);
                // forme continue : sinus -> triangle -> carre, sans menu.
                // Un morphing coute une dimension que l'optimisation gere ;
                // un menu en coute une qu'elle gere mal (regle Synplant).
                const double sinus = std::sin (2.0 * kPi * phase_);
                const double tri   = 4.0 * std::abs (phase_ - 0.5) - 1.0;
                const double carre = (phase_ < 0.5) ? 1.0 : -1.0;
                const double f = std::max (0.0, std::min (1.0, r.p2));
                v_ = (f < 0.5) ? (sinus + (tri - sinus) * (f * 2.0))
                               : (tri + (carre - tri) * ((f - 0.5) * 2.0));
                break;
            }

            case TypeMod::Aleatoire:
            {
                cpt_ += r.p1 / fs_;
                if (cpt_ >= 1.0)
                {
                    cpt_ -= std::floor (cpt_);
                    etatAlea_ = etatAlea_ * 1664525u + 1013904223u;
                    cible_ = ((double) (etatAlea_ >> 8) / 8388608.0) - 1.0;
                }
                // p3 fait glisser d'un echantillonneur-bloqueur (0) vers une
                // derive continue (1) : encore un morphing plutot qu'un mode.
                const double lissage = 0.002 + 0.4 * std::max (0.0, std::min (1.0, r.p3));
                v_ += lissage * (cible_ - v_);
                break;
            }

            case TypeMod::Aucun:
            default:
                v_ = 0.0;
                break;
        }
        return v_;
    }

    double valeur() const noexcept { return v_; }
    bool actif (const ReglagesMod& r) const noexcept
    {
        return r.type != TypeMod::Aucun
            && (r.type != TypeMod::Enveloppe || env_.active());
    }

private:
    double fs_ = 2756.25;   // 44100 / 16
    double phase_ = 0.0, v_ = 0.0, cpt_ = 0.0, cible_ = 0.0;
    unsigned int etatAlea_ = 12345u;
    Enveloppe env_;
};

/** Ce qu'une couche recoit de la matrice, a chaque pas de controle. */
struct ApportMod
{
    double hauteur = 0.0;        // en demi-tons
    double coupure = 0.0;        // en octaves sur la coupure du filtre musical
    double niveau  = 0.0;        // additif, borne ensuite
    double desaccord = 0.0;      // en cents
    double pan = 0.0;
    double coupureGrain = 0.0;   // en octaves sur la coupure de reconstruction
    double morph = 0.0;

    void zero() noexcept { *this = ApportMod{}; }
};

} // namespace romance
