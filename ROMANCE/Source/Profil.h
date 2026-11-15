// =============================================================================
//  Profil.h - CE QUE LA RECHERCHE A LE DROIT DE BOUGER.
//
//  ⭐ LE CHANGEMENT LE PLUS IMPORTANT DU PROJET COTE RESYNTHESE. Jusqu'ici la
//  recherche ne touchait que le MATERIAU : 108 octets de partiels additifs. Elle
//  jouait avec une main dans le dos - le plugin possede quatre generateurs,
//  quatre topologies de filtre, un unisson, huit modulations et une chaine
//  d'effets, et elle n'en utilisait aucun.
//
//  Un Juno se decrit avec HUIT valeurs sur le moteur analogique ; en additif il
//  en faut soixante-douze, pour un resultat moins bon. Ce n'est pas une question
//  de capacite, c'est une question d'OUTIL.
//
//  ⭐ ET LE RENDU PASSE PAR `Couche`, la vraie. Pas une reimplementation : la
//  meme classe que le plugin joue. C'est le meme raisonnement que pour
//  l'entrainement en C++ - ce qu'on cherche est ce qu'on entendra, par
//  construction et pas par discipline.
//
//  /!\ ON NE MUTE QUE LES OCTETS DU MOTEUR ACTIF. Laisser la recherche brasser
//  les quatre operateurs FM alors que la couche est en analogique, c'est
//  depenser les trois quarts du budget a deplacer des choses inaudibles - et
//  l'oracle a montre que le budget est precisement ce qui manque.
//
//  /!\ ET CE QUI N'EST PAS DANS CETTE LISTE RESTE A LA MAIN DE L'UTILISATEUR.
//  Une resynthese qui ecraserait tous les reglages ne serait pas un outil, ce
//  serait un remplacement.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Couche.h"
#include "Effets.h"
#include "Effets2.h"
#include "Grain.h"
#include "Moteur.h"
#include "Recette.h"
#include "Spectre.h"

namespace romance
{

/*  Plan du profil
    ------------------------------------------------------------------
      0..107   la recette (materiau du moteur ROM)
    108..111   enveloppe d'amplitude : attaque, decroissance, tenue, relachement
        112    moteur (0 ROM, 1 analogique, 2 FM, 3 table)
    113..120   analogique : forme1, forme2, melange, demi-tons, cents,
               largeur, sous-octave, bruit
    121..160   FM : algo, retour, point de brisure, suivi de vitesse, puis
               4 x (rapport, niveau, attaque, chute, tenue, relachement,
               suivi de niveau, desaccord, sensibilite a la vitesse)
    161..164   table : famille, lecture, position, symetrie
    165..172   filtre : topologie, coupure, resonance, morph, suivi clavier,
               passe-haut coupure, passe-haut resonance, passe-haut pente
    173..174   unisson : desaccord, largeur
    175..177   grain : quantification, bande, horloge
    178..187   modulation : 2 emplacements x (nature, p1, p2, p3, p4)
    188..191   cablage : 2 x (destination, dose)
    192..206   effets : chorus 4, delai 3, reverbe 3, saturation 2, tonalite 2,
               ensemble 1
    207..223   grille dessinee : 16 cellules + 1 octet (finesse, bits)
    224        materiau : recette ou spectre dessine
    225..288   spectre dessine : 64 bandes
    289..292   spectre : pente, attaque, chute, tenue
    293..297   reserve
*/
constexpr int kOctetsProfil = 298;
constexpr int kOffEnv    = 108;
constexpr int kOffMoteur = 112;
constexpr int kOffVA     = 113;
constexpr int kOffFM     = 121;
constexpr int kOffTable  = 161;
constexpr int kOffFiltre = 165;
constexpr int kOffUni    = 173;
constexpr int kOffGrain  = 175;
constexpr int kOffMod    = 178;
constexpr int kOffCable  = 188;
constexpr int kOffFx     = 192;

/** ⭐ LA GRILLE DESSINEE, VUE PAR LA RECHERCHE : SEIZE CELLULES, PAS SOIXANTE.

    Le plugin en stocke soixante-quatre. En donner soixante-quatre a la
    recherche serait suivre la lettre et rater l'intention : soixante-quatre
    octets tires au hasard font un BRUIT, pas une forme d'onde, et une mutation
    d'un octet sur soixante-quatre ne s'entend pas. Seize cellules, c'est une
    forme qu'on reconnait et qu'un pas de mutation deplace vraiment.

    /!\ Elles s'ecrivent par `Dessin::pose (i, a, 16)`, qui remplit le pave de
    quatre cellules que couvre chacune - donc le dessin reste plein quand
    l'utilisateur repasse en 64 cellules pour le retoucher. */
constexpr int kOffGrille   = 207;
constexpr int kCellulesProfil = 16;

/** ⭐⭐ LE SECOND MATERIAU, DANS LE PROFIL.

    L'etalon (tools/mes_plafond.cpp) a mesure ce que chaque contrainte du
    format coute sur de vrais sons :

        21 partiels (la recette seule) ............. 4,768 dB
        21 partiels + residu 64 bandes ............ 2,865 dB   -1,90

    Le poste le plus cher n'etait ni le nombre de partiels ni les enveloppes,
    c'etait TOUT CE QUI N'EST PAS HARMONIQUE. La recherche ne pouvait pas
    l'atteindre : la recette n'a que deux bandes de bruit.

    /!\ UN OCTET DECIDE LEQUEL DES DEUX MATERIAUX LA COUCHE UTILISE. Sans lui,
    la recherche devrait deviner - et un profil ou les deux coexisteraient
    ferait payer soixante-huit octets de spectre a un patch qui n'en veut pas.
*/
constexpr int kOffMateriau = 224;
constexpr int kOffSpectre  = 225;      // 64 bandes
constexpr int kOffSpParam  = 289;      // pente, attaque, chute, tenue

/** Le materiau que la couche fabrique. */
enum class Materiau { Recette, SpectreDessine };

/** ⭐ DEUX EMPLACEMENTS DE MODULATION, PAS HUIT.

    Le plugin en a huit, et la recherche pourrait tous les bouger. Mais chaque
    emplacement coute cinq octets plus un cablage, et l'oracle a montre que le
    budget d'evaluations est le goulot du projet : ouvrir huit emplacements,
    c'est quadrupler l'espace pour des sources dont la plupart resteraient a
    zero. Deux suffisent a expliquer ce qui s'entend sur un sample glisse - un
    vibrato et une enveloppe de filtre. Les six autres restent a la main de
    l'utilisateur, ce qui est aussi une reponse : la resynthese propose, elle
    ne remplit pas le patch.
*/
constexpr int kModsProfil = 2;

namespace profil
{
    inline double lg (uint8_t v, double a, double b) noexcept
    {
        return std::exp (std::log (a) + ((double) v / 255.0) * (std::log (b) - std::log (a)));
    }
    inline uint8_t qlg (double v, double a, double b) noexcept
    {
        const double x = (std::log (std::max (a, std::min (b, v))) - std::log (a))
                       / (std::log (b) - std::log (a));
        return (uint8_t) std::max (0.0, std::min (255.0, std::round (x * 255.0)));
    }
    inline double li (uint8_t v, double a, double b) noexcept
    { return a + ((double) v / 255.0) * (b - a); }
    inline uint8_t qli (double v, double a, double b) noexcept
    {
        const double x = (std::max (a, std::min (b, v)) - a) / (b - a);
        return (uint8_t) std::max (0.0, std::min (255.0, std::round (x * 255.0)));
    }
}

/** Traduit les octets en reglages de couche jouables. */
inline ReglagesCouche coucheDepuisProfil (const uint8_t* o)
{
    using namespace profil;
    ReglagesCouche r;
    r.active = true;

    r.attaque = lg (o[kOffEnv + 0], 0.001, 3.0);
    r.decli   = lg (o[kOffEnv + 1], 0.005, 8.0);
    r.tenue   = (double) o[kOffEnv + 2] / 255.0;
    r.relache = lg (o[kOffEnv + 3], 0.005, 8.0);

    switch (o[kOffMoteur] >> 6)          // deux bits de poids fort
    {
        case 1:  r.moteur = TypeMoteur::Analogique; break;
        case 2:  r.moteur = TypeMoteur::FM;         break;
        case 3:  r.moteur = TypeMoteur::Table;      break;
        default: r.moteur = TypeMoteur::Rom;        break;
    }

    auto forme = [] (uint8_t v)
    {
        switch (v >> 6)
        {
            case 1:  return FormeOsc::Impulsion;
            case 2:  return FormeOsc::Triangle;
            case 3:  return FormeOsc::Sinus;
            default: return FormeOsc::Scie;
        }
    };
    r.va.forme1    = forme (o[kOffVA + 0]);
    r.va.forme2    = forme (o[kOffVA + 1]);
    r.va.melange   = (double) o[kOffVA + 2] / 255.0;
    r.va.demiTons2 = (int) std::round (li (o[kOffVA + 3], -24.0, 24.0));
    r.va.cents2    = li (o[kOffVA + 4], -50.0, 50.0);
    r.va.largeur   = li (o[kOffVA + 5], 0.05, 0.95);
    r.va.sub       = (double) o[kOffVA + 6] / 255.0;
    r.va.bruit     = (double) o[kOffVA + 7] / 255.0;

    r.fm.algo   = (int) ((double) o[kOffFM + 0] / 256.0 * (double) kAlgosFM);
    r.fm.algo   = std::max (0, std::min (kAlgosFM - 1, r.fm.algo));
    r.fm.retour = (double) o[kOffFM + 1] / 255.0;
    // SEPT OCTETS PAR OPERATEUR au lieu de trois : rapport, niveau, une
    // enveloppe complete, et sa profondeur de suivi de clavier. C'est ce qui
    // separe la cloche de l'electro-piano - et Genopatch peut desormais les
    // chercher au lieu de subir un timbre fige.
    r.fm.pointBrisure   = li (o[kOffFM + 2], 24.0, 96.0);
    r.fm.echelleVitesse = (double) o[kOffFM + 3] / 255.0;
    for (int i = 0; i < kOperateurs; ++i)
    {
        const uint8_t* p = o + kOffFM + 4 + i * 9;
        r.fm.ratio[i]         = lg (p[0], 0.25, 16.0);
        r.fm.niveau[i]        = (double) p[1] / 255.0;
        r.fm.attaque[i]       = lg (p[2], 0.0005, 3.0);
        r.fm.decli[i]         = lg (p[3], 0.005, 8.0);
        r.fm.tenue[i]         = (double) p[4] / 255.0;
        r.fm.relache[i]       = lg (p[5], 0.005, 8.0);
        r.fm.echelleNiveau[i] = li (p[6], -1.0, 1.0);
        // ⭐ Le desaccord tient dans +/- 25 cents : au-dela ce n'est plus un
        // battement, c'est un deuxieme son faux.
        r.fm.desaccord[i]     = li (p[7], -25.0, 25.0);
        r.fm.veloSens[i]      = (double) p[8] / 255.0;
    }

    // ⭐ CINQ FAMILLES, DESSIN COMPRIS. Trois bits de poids fort au lieu de
    // deux : la grille est une famille comme les autres pour la recherche.
    switch (o[kOffTable + 0] >> 5)
    {
        case 1:  r.table.famille = FamilleTable::Formants; break;
        case 2:  r.table.famille = FamilleTable::Impaires; break;
        case 3:  r.table.famille = FamilleTable::Bruitee;  break;
        case 4:
        case 5:
        case 6:
        case 7:  r.table.famille = FamilleTable::Dessin;   break;
        default: r.table.famille = FamilleTable::Harmoniques; break;
    }
    r.table.lecture  = (o[kOffTable + 1] >= 128) ? LectureTable::Saut
                                                 : LectureTable::Fondu;
    r.table.position = (double) o[kOffTable + 2] / 255.0;
    r.table.symetrie = li (o[kOffTable + 3], 0.05, 0.95);
    // /!\ Le POINTEUR vers la banque dessinee n'est pas pose ici : il depend
    // de la note qu'on va rendre, pas du profil. C'est `rendProfil` qui le
    // pose, et le plugin qui pose le sien (voir RomanceProcessor).

    switch (o[kOffFiltre + 0] >> 6)
    {
        case 1:  r.topo = Topologie::TroisPoles; break;
        case 2:  r.topo = Topologie::AnalogiqueBasseReso; break;
        case 3:  r.topo = Topologie::ZPlane; break;
        default: r.topo = Topologie::QuatrePoles; break;
    }
    // /!\ Le plafond passe de 64 a 250 fois la hauteur, comme le parametre du
    // plugin : a 64 fois, un la1 plafonnait a 3,5 kHz et la recherche ne
    // pouvait PAS proposer un son ouvert, quel que soit le sample.
    r.coupureRel   = lg (o[kOffFiltre + 1], 0.25, 250.0);
    r.reso         = (double) o[kOffFiltre + 2] / 255.0 * 0.95;
    r.morph        = (double) o[kOffFiltre + 3] / 255.0;
    r.suiviClavier = (double) o[kOffFiltre + 4] / 255.0;
    // ⭐ Le passe-haut : 20 Hz = ouvert, donc un profil qui ne s'en sert pas
    // met simplement l'octet a zero.
    r.phCoupure    = lg (o[kOffFiltre + 5], 20.0, 8000.0);
    r.phReso       = (double) o[kOffFiltre + 6] / 255.0 * 0.97;
    r.phPente      = (o[kOffFiltre + 7] >= 128) ? 2 : 1;

    r.desaccordCents = li (o[kOffUni + 0], 0.0, 40.0);
    r.largeur        = (double) o[kOffUni + 1] / 255.0;

    // ⭐ Le grain fait partie du profil : si l'utilisateur depose un sample
    // deja LoFi, la recherche doit pouvoir aller le chercher la.
    switch (o[kOffGrain + 0] >> 6)
    {
        case 1:  r.grain.quant = Quant::MuLaw; break;
        case 2:  r.grain.quant = Quant::Lin8;  break;
        case 3:  r.grain.quant = Quant::Lin12; break;
        default: r.grain.quant = Quant::Aucune; break;
    }
    r.grain.actif   = (o[kOffGrain + 0] >= 64);
    r.grain.coupure = li (o[kOffGrain + 1], 0.05, 0.45);
    r.grain.horloge = (o[kOffGrain + 2] >= 128) ? Horloge::Fixe : Horloge::Variable;
    r.grain.filtre  = Filtre::Asservi;
    r.boucleDebut = 0.0; r.boucleFin = 1.0;

    // ⭐ LA VELOCITE EST NEUTRALISEE DANS LA RECHERCHE, et c'est necessaire.
    // Un rendu se fait a UNE velocite : la reponse au clavier n'y est donc pas
    // observable, et la laisser active reviendrait a faire dependre la perte
    // d'un reglage que la perte ne peut pas juger. Le jour ou les defauts de
    // velocite sont passes de 0 a 0,65 et 0,35, le vote de moteur est tombe de
    // 2/3 a 1/3 - la recherche comparait des rendus assombris au hasard de
    // leur reponse au clavier.
    r.veloNiveau = 0.0; r.veloCoupure = 0.0;
    r.veloAttaque = 0.0; r.veloDepart = 0.0;
    r.fonduVelo = 0.0;  r.retard = 0.0;
    r.niveau = 1.0; r.pan = 0.0;
    return r;
}

/** ⭐ LE PATCH COMPLET : la couche, ses modulations, ses cablages.

    /!\ Les emplacements de modulation du profil sont les DEUX PREMIERS du
    plugin. Un cablage qui pointe ailleurs n'aurait aucune source, et la
    recherche passerait son temps a deplacer une dose qui ne module rien.
*/
inline ReglagesPatch patchDepuisProfil (const uint8_t* o)
{
    using namespace profil;
    ReglagesPatch p;
    for (auto& c : p.couche) c.active = false;
    p.couche[0] = coucheDepuisProfil (o);
    p.instab.actif = false;          // l'instabilite n'est pas cherchee
    p.mode = ModeVoix::Poly;
    p.polyphonie = 1;

    for (int i = 0; i < kModsProfil; ++i)
    {
        const uint8_t* m = o + kOffMod + i * 5;
        switch (m[0] >> 6)
        {
            case 1:  p.mods[i].type = TypeMod::LFO;       break;
            case 2:  p.mods[i].type = TypeMod::Enveloppe; break;
            case 3:  p.mods[i].type = TypeMod::Aleatoire; break;
            default: p.mods[i].type = TypeMod::Aucun;     break;
        }
        // /!\ p1 n'a PAS la meme plage selon la nature : c'est une vitesse en
        // Hz pour un LFO, une attaque en secondes pour une enveloppe. Les lire
        // sur la meme echelle donnerait des LFO a 0,001 Hz ou des attaques de
        // 20 s selon le sens du vent.
        p.mods[i].p1 = (p.mods[i].type == TypeMod::Enveloppe)
                          ? lg (m[1], 0.001, 3.0) : lg (m[1], 0.02, 12.0);
        p.mods[i].p2 = (double) m[2] / 255.0;
        p.mods[i].p3 = (double) m[3] / 255.0;
        p.mods[i].p4 = (p.mods[i].type == TypeMod::Enveloppe)
                          ? lg (m[4], 0.005, 8.0) : (double) m[4] / 255.0;
        p.mods[i].retrig = true;
    }

    for (int i = 0; i < kModsProfil; ++i)
    {
        const uint8_t* c = o + kOffCable + i * 2;
        // ⭐ La recherche a maintenant les 38 destinations, pas sept.
        const int d = (int) ((double) c[0] / 256.0 * (double) kNbDestinations);
        p.routages[i].source = i + 1;
        p.routages[i].dest   = std::max (0, std::min (kNbDestinations - 1, d));
        p.routages[i].couche = 0;
        p.routages[i].dose   = li (c[1], -1.0, 1.0);
    }
    return p;
}

/** ⭐ Le dessin d'un profil, tel que le plugin le stockera. */
inline Dessin dessinDepuisProfil (const uint8_t* o)
{
    using namespace profil;
    Dessin d;
    const uint8_t f = o[kOffGrille + kCellulesProfil];
    // finesse : 8, 16, 32 ou 64 cellules dans le PLUGIN ; la recherche n'en
    // pose que seize, mais elle choisit la finesse que l'utilisateur verra.
    static const int kCell[4] = { 8, 16, 32, 64 };
    d.nCellules = kCell[(f >> 6) & 3];
    d.bits = kBitsMin + (int) ((f >> 3) & 7) % (kBitsMax - kBitsMin + 1);
    for (int i = 0; i < kCellulesProfil; ++i)
        d.pose (i, (double) o[kOffGrille + i] / 255.0 * 2.0 - 1.0, kCellulesProfil);
    return d;
}

/** Quel materiau ce profil fabrique-t-il ? */
inline Materiau materiauDeProfil (const uint8_t* o) noexcept
{
    return (o[kOffMateriau] >= 128) ? Materiau::SpectreDessine : Materiau::Recette;
}

/** ⭐ Le spectre dessine d'un profil. */
inline Spectre spectreDepuisProfil (const uint8_t* o)
{
    using namespace profil;
    Spectre sp;
    for (int i = 0; i < kBandesSpectre; ++i) sp.bande[i] = o[kOffSpectre + i];
    sp.pente   = (double) o[kOffSpParam + 0] / 255.0;
    sp.attaque = lg (o[kOffSpParam + 1], 0.0005, 3.0);
    sp.chute   = lg (o[kOffSpParam + 2], 0.01, 8.0);
    sp.tenue   = (double) o[kOffSpParam + 3] / 255.0;
    return sp;
}

/** Les effets d'un profil, poses sur des instances deja preparees.

    /!\ L'ORDRE DE LA CHAINE N'EST PAS DANS LE PROFIL. Six emplacements
    discrets, c'est un paysage en marches d'escalier ou une mutation d'un cran
    change tout le son : exactement le genre de dimension que la regle Synplant
    dit de ne pas donner a une recherche continue. L'ordre reste un geste
    d'utilisateur ; la recherche regle les DOSES.
*/
struct FxProfil
{
    Chorus chorus;
    Ensemble ensemble;
    Delai delai;
    Reverbe reverbe;
    Saturation saturation;
    Tonalite tonalite;

    void prepare (double fs)
    {
        chorus.prepare (fs); ensemble.prepare (fs); delai.prepare (fs);
        reverbe.prepare (fs); saturation.prepare (fs); tonalite.prepare (fs);
    }

    void regleDepuisProfil (const uint8_t* o)
    {
        using namespace profil;
        const uint8_t* f = o + kOffFx;
        chorus.regleNature (f[0] < 128);
        chorus.regle (lg (f[1], 0.02, 8.0), (double) f[2] / 255.0,
                      (double) f[0] / 255.0, (double) f[3] / 255.0);
        delai.regle (lg (f[4], 0.01, 2.0), (double) f[5] / 255.0 * 0.95,
                     (double) f[6] / 255.0, 0.45, 0.0);
        reverbe.regle ((double) f[8] / 255.0, (double) f[9] / 255.0,
                       (double) f[7] / 255.0, 0.018);
        saturation.regle ((double) f[10] / 255.0, (double) f[11] / 255.0);
        tonalite.regle (li (f[12], -12.0, 12.0), li (f[13], -12.0, 12.0));
        ensemble.regle (0.6, 0.6, (double) f[14] / 255.0);

        actifChorus   = f[0] > 0;
        actifDelai    = f[6] > 0;
        actifReverbe  = f[7] > 0;
        actifSat      = f[10] > 0;
        actifTon      = (f[12] != 128) || (f[13] != 128);
        actifEnsemble = f[14] > 0;
    }

    /** /!\ ON SAUTE LES EFFETS A MELANGE NUL, et ce n'est pas une optimisation
        de confort : la recherche appelle ce rendu des milliers de fois, et un
        reseau de reverbe a quatre lignes plus une ligne a retard de deux
        secondes tournaient a chaque echantillon meme quand leur melange etait a
        zero - c'est-a-dire la plupart du temps. Mesure : 31 ms par evaluation
        avant, 18 apres.

        C'est EXACT, pas approche : a melange nul, ces effets rendent leur
        entree telle quelle, et leur etat interne ne reapparait jamais dans le
        chemin sec. */
    inline void traite (double& g, double& d) noexcept
    {
        if (actifChorus)   chorus.traite (g, d);
        if (actifEnsemble) ensemble.traite (g, d);
        if (actifDelai)    delai.traite (g, d);
        if (actifReverbe)  reverbe.traite (g, d);
        if (actifSat)      saturation.traite (g, d);
        if (actifTon)      tonalite.traite (g, d);
    }

    bool actifChorus = false, actifEnsemble = false, actifDelai = false,
         actifReverbe = false, actifSat = false, actifTon = false;
};

/** Un profil neutre : moteur ROM, filtre ouvert, pas de grain. */
inline void profilNeutre (uint8_t* o)
{
    using namespace profil;
    for (int i = 0; i < kOctetsProfil; ++i) o[i] = 0;
    // /!\ Une recette toute a zero n'a AUCUN partiel : le moteur ROM d'un
    // profil neutre serait muet, et le vote de moteur l'ecarterait pour une
    // raison qui n'a rien a voir avec le son de la cible.
    {
        Recette r;
        r.nPartiels = 12;
        for (int i = 0; i < r.nPartiels; ++i)
        {
            r.partiels[i].amplitude = 1.0 / (double) (i + 1);
            r.partiels[i].decroiss  = 0.5;
            r.partiels[i].desaccord = 0.0;
            r.partiels[i].montee    = 0;
        }
        versOctets (r, o);
    }
    o[kOffEnv + 0] = qlg (0.005, 0.001, 3.0);
    o[kOffEnv + 1] = qlg (0.4, 0.005, 8.0);
    o[kOffEnv + 2] = 230;
    o[kOffEnv + 3] = qlg (0.5, 0.005, 8.0);
    o[kOffMoteur]  = 0;
    o[kOffVA + 2] = 128; o[kOffVA + 3] = 128; o[kOffVA + 4] = 146;
    o[kOffVA + 5] = 128;
    o[kOffFM + 2] = qli (60.0, 24.0, 96.0);       // point de brisure : do3
    o[kOffFM + 3] = 0;                            // pas de suivi de vitesse
    {
        static const double rap[4] = { 1.0, 2.0, 1.0, 3.5 };
        static const double niv[4] = { 1.0, 0.6, 0.0, 0.4 };
        static const double ech[4] = { 0.0, 0.35, 0.35, 0.35 };
        for (int i = 0; i < kOperateurs; ++i)
        {
            uint8_t* q = o + kOffFM + 4 + i * 9;
            q[0] = qlg (rap[i], 0.25, 16.0);
            q[1] = (uint8_t) std::round (niv[i] * 255.0);
            q[2] = qlg (0.002, 0.0005, 3.0);
            q[3] = qlg (0.30, 0.005, 8.0);
            q[4] = 190;
            q[5] = qlg (0.30, 0.005, 8.0);
            q[6] = qli (ech[i], -1.0, 1.0);
            q[7] = qli (0.0, -25.0, 25.0);   // pas de desaccord
            q[8] = 0;                        // pas de sensibilite a la vitesse
        }
    }
    o[kOffTable + 3] = 128;
    o[kOffFiltre + 1] = qlg (200.0, 0.25, 250.0);  // filtre grand ouvert
    o[kOffFiltre + 4] = 255;                       // suivi clavier complet
    o[kOffFiltre + 5] = 0;                         // passe-haut ouvert (20 Hz)
    o[kOffFiltre + 6] = 0;
    o[kOffFiltre + 7] = 0;
    // ⭐ La grille neutre est un SINUS, comme dans le plugin : basculer sur la
    // famille dessinee ne doit pas changer le son avant qu'on ait dessine.
    for (int i = 0; i < kCellulesProfil; ++i)
    {
        const double u = ((double) i + 0.5) / (double) kCellulesProfil;
        o[kOffGrille + i] = (uint8_t) std::lround (
                                (std::sin (2.0 * kPi * u) * 0.5 + 0.5) * 255.0);
    }
    o[kOffGrille + kCellulesProfil] = 255;   // finesse maximale, huit bits

    // ⭐ Le materiau NEUTRE est la RECETTE : c'est elle qui decrit le mieux un
    // son harmonique, et l'etalon le confirme (4,77 dB contre 6,59 pour un
    // spectre seul). Le spectre est un COMPLEMENT, pas un remplacant.
    o[kOffMateriau] = 0;
    {
        Spectre sp;                          // la pente douce par defaut
        for (int i = 0; i < kBandesSpectre; ++i) o[kOffSpectre + i] = sp.bande[i];
        o[kOffSpParam + 0] = (uint8_t) std::lround (sp.pente * 255.0);
        o[kOffSpParam + 1] = qlg (sp.attaque, 0.0005, 3.0);
        o[kOffSpParam + 2] = qlg (sp.chute, 0.01, 8.0);
        o[kOffSpParam + 3] = (uint8_t) std::lround (sp.tenue * 255.0);
    }

    o[kOffUni + 0] = qli (6.0, 0.0, 40.0);
    o[kOffUni + 1] = 120;
    o[kOffGrain + 1] = qli (0.30, 0.05, 0.45);

    // modulations eteintes, doses au centre (0)
    for (int i = 0; i < kModsProfil; ++i)
    {
        o[kOffMod + i * 5 + 0] = 0;              // nature = aucune
        o[kOffMod + i * 5 + 1] = 128;
        o[kOffMod + i * 5 + 2] = 128;
        o[kOffMod + i * 5 + 3] = 128;
        o[kOffMod + i * 5 + 4] = 128;
        o[kOffCable + i * 2 + 0] = 0;            // destination = aucune
        o[kOffCable + i * 2 + 1] = 128;          // dose = 0
    }

    // /!\ TOUS LES EFFETS A MELANGE NUL. Un profil neutre qui arriverait avec
    // de la reverbe ferait porter a la recherche le poids d'une queue qu'elle
    // n'a pas demandee - et sur une cible seche, elle devrait depenser du
    // budget uniquement pour l'eteindre.
    for (int i = 0; i < 15; ++i) o[kOffFx + i] = 0;
    o[kOffFx + 1]  = qlg (0.4, 0.02, 8.0);       // vitesse de chorus
    o[kOffFx + 3]  = 115;                        // couleur de chorus
    o[kOffFx + 4]  = qlg (0.375, 0.01, 2.0);     // temps de delai
    o[kOffFx + 5]  = 94;                         // reinjection
    o[kOffFx + 8]  = 115;                        // taille de reverbe
    o[kOffFx + 9]  = 115;                        // amortissement
    o[kOffFx + 11] = 128;                        // asymetrie de saturation
    o[kOffFx + 12] = 128;                        // tonalite grave = 0 dB
    o[kOffFx + 13] = 128;                        // tonalite aigu  = 0 dB
}

/** Y ecrit une recette (le materiau du moteur ROM). */
inline void poseRecetteDansProfil (uint8_t* o, const Recette& r)
{
    versOctets (r, o);
}

/** ⭐ QUELS OCTETS LA RECHERCHE PEUT BOUGER, SELON LE MOTEUR ACTIF.

    /!\ C'est ce qui empeche l'explosion de l'espace. Un profil fait 152 octets,
    mais un moteur donne n'en utilise qu'une soixantaine : brasser les quatorze
    octets de FM pendant qu'on cherche un Juno, c'est jeter les trois quarts du
    budget - et l'oracle a montre que le budget est exactement ce qui manque.
*/
/** ⭐⭐ LES GROUPES DE PARAMETRES - ce que la recherche deplace ENSEMBLE.

    /!\ L'ORACLE A MONTRE QUE LA RECHERCHE EST BLOQUEE, pas lente : sur des
    cibles fabriquees depuis un profil connu - donc de perte nulle atteignable -
    elle plafonne a 3,0 dB avec 3 000 evaluations et 2,7 dB avec 24 000. Huit
    fois le budget pour 0,27 dB. Elle ne retrouve pas la reponse meme quand on
    la lui montre.

    La cause tient a la forme des mutations : dix octets tires au hasard parmi
    cent cinquante, tous groupes confondus. Chaque enfant deplace donc un peu de
    filtre, un peu d'enveloppe et un peu de FM a la fois - et sur un paysage qui
    a 22 % d'inversions locales, un pas dans une direction quelconque a peu de
    chances d'ameliorer quoi que ce soit.

    Une descente par COORDONNEES fait l'inverse : on ne bouge qu'un groupe a la
    fois. Chaque generation pose alors une question a laquelle on peut repondre
    - « le filtre peut-il aller mieux, tout le reste etant fixe ? » - au lieu
    d'une question a douze inconnues.

    /!\ ON GARDE DES GENERATIONS « TOUS GROUPES ». Une descente par coordonnees
    pure se bloque des que deux groupes sont couples - la coupure du filtre et
    le niveau d'un operateur FM, par exemple, ou bouger l'un seul empire
    toujours. Une generation sur quatre reste globale : c'est ce qui permet de
    sortir de ces creux-la.
*/
struct GroupeProfil
{
    const char* nom;
    int debut, fin;      // [debut, fin)
};

inline const GroupeProfil* groupesProfil (int& n) noexcept
{
    static const GroupeProfil g[] = {
        { "materiau",   0,            kOctetsRecette },
        { "enveloppe",  kOffEnv,      kOffEnv + 4 },
        { "analogique", kOffVA,       kOffVA + 8 },
        { "FM",         kOffFM,       kOffFM + 40 },
        { "table",      kOffTable,    kOffTable + 4 },
        { "filtre",     kOffFiltre,   kOffFiltre + 8 },
        { "unisson",    kOffUni,      kOffUni + 2 },
        { "grain",      kOffGrain,    kOffGrain + 3 },
        { "modulation", kOffMod,      kOffMod + kModsProfil * 5 },
        { "cablage",    kOffCable,    kOffCable + kModsProfil * 2 },
        { "effets",     kOffFx,       kOffFx + 15 },
        { "grille",     kOffGrille,   kOffGrille + kCellulesProfil + 1 },
    };
    n = (int) (sizeof (g) / sizeof (g[0]));
    return g;
}

inline bool octetProfilMutable (int i, TypeMoteur m) noexcept
{
    if (i >= kOctetsProfil) return false;
    // ⭐ LE SPECTRE ET LA RECETTE SONT DEUX MATERIAUX DE ROM : ils ne servent
    // qu'au moteur ROM, et la recherche choisit entre eux par `kOffMateriau`.
    if (i == kOffMateriau) return m == TypeMoteur::Rom;
    if (i >= kOffSpectre && i < kOffSpParam + 4) return m == TypeMoteur::Rom;

    if (i < kOctetsRecette)
    {
        if (m != TypeMoteur::Rom) return false;      // materiau inutilise
        if (i == 0) return false;                    // version
        if (i == 12 || i == 13) return false;        // graine
        if (i == 14 || i == 15) return false;        // nb de partiels, reserve
        if (i >= 100) return false;                  // reserve
        return true;
    }
    if (i >= kOffEnv && i < kOffEnv + 4) return true;
    if (i == kOffMoteur) return false;               // change par vote, pas par mutation
    if (i >= kOffVA    && i < kOffVA + 8)    return m == TypeMoteur::Analogique;
    if (i >= kOffFM    && i < kOffFM + 40)   return m == TypeMoteur::FM;
    if (i >= kOffTable && i < kOffTable + 4) return m == TypeMoteur::Table;
    // ⭐ La grille n'a de sens que sur le moteur a table d'ondes. La rendre
    // mutable partout ferait payer dix-sept dimensions mortes aux trois autres.
    if (i >= kOffGrille && i < kOffGrille + kCellulesProfil + 1)
        return m == TypeMoteur::Table;
    if (i >= kOffFiltre && i < kOffFiltre + 8) return true;
    if (i >= kOffUni   && i < kOffUni + 2)   return true;
    if (i >= kOffGrain && i < kOffGrain + 3) return true;

    // ⭐ LES MODULATIONS ET LES EFFETS SONT CHERCHES POUR TOUS LES MOTEURS.
    // Un sample glisse porte presque toujours un vibrato, une enveloppe de
    // filtre ou une queue de reverbe ; sans ces octets, la recherche devait les
    // imiter avec le materiau, ce qui les fige dans la ROM au lieu de les
    // rendre reglables.
    if (i >= kOffMod   && i < kOffMod + kModsProfil * 5) return true;
    if (i >= kOffCable && i < kOffCable + kModsProfil * 2) return true;
    if (i >= kOffFx    && i < kOffFx + 15) return true;
    return false;
}

/** ⭐ LE RENDU PASSE PAR LA VRAIE `Couche`. Pas une reimplementation : la meme
    classe que le plugin joue, avec son filtre, son enveloppe, son unisson et
    son etage de reconstruction. Ce qu'on cherche est donc ce qu'on entendra. */
inline void rendProfil (const uint8_t* o, std::vector<double>& sortie,
                        int nEch, double fsSortie, double f0)
{
    // ⭐⭐ ON REND PAR LE MOTEUR ENTIER, plus seulement par une couche : les
    // modulations et les effets font partie de ce qu'on cherche. C'est la
    // demande, et c'est aussi ce qui rend la recherche honnete - un sample
    // glisse porte presque toujours un vibrato et une queue, et jusqu'ici la
    // recherche devait les imiter avec le materiau.
    const ReglagesPatch p = patchDepuisProfil (o);

    std::vector<double> rom;
    int nRom = 0;
    if (p.couche[0].moteur == TypeMoteur::Rom)
    {
        // ⭐⭐ DEUX MATERIAUX, UN SEUL CHEMIN DE LECTURE. Le spectre dessine
        // fabrique une ROM exactement comme la recette ; tout ce qui suit -
        // grain, boucle, quantification, filtre de reconstruction - ne sait
        // pas d'ou elle vient, et n'a pas a le savoir.
        if (materiauDeProfil (o) == Materiau::SpectreDessine)
        {
            const double fsR = p.couche[0].grain.fsRom;
            rendSpectre (spectreDepuisProfil (o), rom, fsR, (int) (2.0 * fsR), 1u);
            nRom = (int) rom.size();
        }
        else
        {
            const Recette rec = depuisOctets (o);
            const auto info = rendRom (rec, rom, p.couche[0].grain.fsRom);
            nRom = info.nTotal;
        }
        for (auto& v : rom) v = quantifie (v, p.couche[0].grain.quant);
    }

    // /!\ LA NOTE EST ENTIERE, L'ACCORDAGE RATTRAPE LE RESTE. `noteOn` prend un
    // numero MIDI : arrondir la hauteur cherchee a la note la plus proche
    // introduirait jusqu'a un demi-ton d'erreur, soit bien plus que ce que la
    // perte sait distinguer.
    const double hz = std::max (20.0, f0);
    const double noteExacte = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int note = std::max (0, std::min (127, (int) std::round (noteExacte)));
    ReglagesPatch pp = p;
    pp.accordage = 440.0 * std::pow (2.0, (noteExacte - (double) note) / 12.0);

    const double* roms[kNbCouches] = { rom.empty() ? nullptr : rom.data(),
                                       nullptr, nullptr, nullptr };
    int tailles[kNbCouches] = { nRom, 0, 0, 0 };

    // ⭐ La grille dessinee, pour la SEULE octave qu'on va jouer.
    BanqueDessin banque;
    if (pp.couche[0].table.famille == FamilleTable::Dessin)
    {
        banque.fabriqueOctave (dessinDepuisProfil (o), hz, fsSortie);
        pp.couche[0].table.dessin = &banque;
    }

    Moteur m;
    m.prepare (fsSortie, 1);
    m.noteOn (pp, note, 100, roms, tailles);

    FxProfil fx;
    fx.prepare (fsSortie);
    fx.regleDepuisProfil (o);

    // ⭐⭐ ON RELACHE LA NOTE. Mesure avant : pousser l'octet de RELACHEMENT
    // d'un bout a l'autre de sa plage coutait exactement 0,000 dB de perte -
    // il etait mutable, la recherche pouvait le bouger, et ca ne changeait
    // rien parce que le rendu ne le declenchait jamais. Une dimension de
    // recherche morte, qui diluait le budget d'evaluations sans rien apporter.
    //
    // /!\ TARD, PAS A LA MOITIE. Un sample tenu doit pouvoir etre imite par un
    // patch tenu : relacher tot rendrait la tenue longue INATTEIGNABLE, donc
    // remplacerait un defaut par un autre. A 80 %, une nappe garde sa tenue et
    // un son percussif est deja eteint depuis longtemps - seule la queue
    // change, et c'est precisement ce qu'on cherchait a rendre visible.
    const int iRelache = (nEch * 4) / 5;

    sortie.assign ((size_t) nEch, 0.0);
    for (int i = 0; i < nEch; ++i)
    {
        if (i == iRelache) m.noteOff (pp, note);
        double g = 0.0, d = 0.0;
        m.suivant (pp, g, d);
        fx.traite (g, d);
        sortie[(size_t) i] = 0.5 * (g + d);
    }

    // /!\ Normalisation sur la crete : l'empreinte est deja normalisee par
    // trame, mais un profil qui sature ou qui murmure fausserait la mesure
    // AVANT elle. On compare des timbres, pas des volumes.
    double crete = 1e-12;
    for (double v : sortie) crete = std::max (crete, std::abs (v));
    if (crete > 1e-9) { const double g = 0.9 / crete; for (auto& v : sortie) v *= g; }
}

} // namespace romance
