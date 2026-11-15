// =============================================================================
//  Recherche.h - La recherche travaille DIRECTEMENT sur les 108 octets.
//
//  ⭐ ELLE NE CHERCHE PAS DANS UN ESPACE CONTINU QU'ON ARRONDIRAIT ENSUITE.
//  Elle mute les octets. Consequence : tout ce qu'elle trouve est exactement
//  representable, et ce qu'on ecoute pendant la recherche est bit pour bit ce
//  qu'on enregistrera. Chercher dans un espace plus fin que l'etat reviendrait
//  a mesurer une precision qu'on ne peut pas garder.
//
//  ⭐ ET ELLE MESURE APRES LA DEGRADATION D'EPOQUE. On ne cherche pas la
//  recette qui imite le sample, mais celle qui - une fois passee dans la
//  machine - lui ressemble.
//
//  Strategie : (1 + lambda) avec pas adaptatif (regle du cinquieme). Simple,
//  robuste, et surtout INTERRUPTIBLE a tout moment : l'utilisateur choisit son
//  temps de generation, la recherche doit donc pouvoir rendre son meilleur
//  resultat quand on l'arrete, pas quand elle a fini.
//
//  /!\ LE HASARD EST SEME PAR L'APPELANT. Deux recherches avec la meme graine
//  donnent le meme resultat : sans ca on ne peut ni comparer deux reglages, ni
//  reproduire un bug.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Analyse.h"
#include <atomic>
#include <thread>
#include "Profil.h"
#include "Reseau.h"
#include "Perte.h"
#include "Recette.h"

namespace romance
{

/** ⭐ CE QUE LA RECHERCHE MUTE : 108 octets de recette PLUS 4 d'enveloppe.

    Decision de l'utilisateur : « les deux, la recherche arbitre ». Une attaque
    lente peut venir du MATERIAU (chaque partiel monte a son rythme, donc le
    timbre s'ouvre) ou de l'ENVELOPPE de couche (tout monte ensemble, timbre
    fige). Ce sont deux sons differents, et c'est a la perte de choisir lequel
    ressemble le plus a la cible.

    /!\ Les 4 octets d'enveloppe ne sont PAS dans la recette : le materiau
    reste a 108 octets et « la recette est l'etat » reste vrai. L'enveloppe
    appartient a la couche, comme sur les machines. */
constexpr int kOctetsCherches = kOctetsProfil;

inline EnvAmpli envDepuisOctets (const uint8_t* o) noexcept
{
    EnvAmpli e;
    e.actif = true;
    auto lg = [] (uint8_t v, double a, double b)
    { return std::exp (std::log (a) + ((double) v / 255.0) * (std::log (b) - std::log (a))); };
    e.attaque = lg (o[0], 0.001, 3.0);
    e.decli   = lg (o[1], 0.005, 8.0);
    e.tenue   = (double) o[2] / 255.0;
    e.relache = lg (o[3], 0.005, 8.0);
    return e;
}

inline void envVersOctets (const EnvAmpli& e, uint8_t* o) noexcept
{
    auto q = [] (double v, double a, double b)
    {
        const double x = (std::log (std::max (a, std::min (b, v))) - std::log (a))
                       / (std::log (b) - std::log (a));
        return (uint8_t) std::max (0.0, std::min (255.0, std::round (x * 255.0)));
    };
    o[0] = q (e.attaque, 0.001, 3.0);
    o[1] = q (e.decli,   0.005, 8.0);
    o[2] = (uint8_t) std::max (0.0, std::min (255.0, std::round (e.tenue * 255.0)));
    o[3] = q (e.relache, 0.005, 8.0);
}

struct ReglagesRecherche
{
    int    lambda = 8;            // enfants par generation
    double sigma0 = 0.06;         // pas initial, en fraction d'octet
    int    octetsMutes = 10;      // combien d'octets bouge un enfant
    int    evaluations = 3000;    // budget - l'utilisateur choisit son temps
    unsigned int graine = 12345u;
    int    nEchComparaison = 24000;  // ~0,54 s a 44,1 kHz

    /** /!\ PAR DEFAUT ON AJUSTE SUR UN SIGNAL PROPRE. Voir le tableau de
        calibration dans Perte.h : a travers la machine, la perte distingue
        vingt fois moins bien, et la recette obtenue pre-compense une machine
        precise - donc devient fausse des qu'on change de saveur. */
    bool   ajusteSurLaMachine = false;

    /** ⭐ FENETRE VARIABLE (choix de l'utilisateur). La fenetre d'ajustement
        s'adapte a la duree du sample, entre `fenetreMin` et `fenetreMax`.

        /!\ CONSEQUENCE ASSUMEE, ET C'EST POUR CA QU'ELLE EST ECRITE ICI : les
        pertes ne sont PLUS comparables d'un son a l'autre. Un son court est
        juge sur 1 s, un long sur 4 : les deux nombres ne mesurent pas la meme
        chose. Pour cette raison l'outil d'evaluation recalcule toujours une
        perte de REFERENCE sur une fenetre fixe - on ajuste sur la fenetre du
        son, on COMPARE sur une fenetre commune. */
    /** ⭐ POUR MESURER, PAS POUR JOUER. Retablit l'ancien tirage - un indice
        au hasard sur TOUT le profil, puis on jette s'il n'est pas mutable.
        C'est le seul moyen de comparer les deux formes de mutation SUR LES
        MEMES CIBLES : sans ca, on compare deux executions qui ne different pas
        que par ce qu'on croit. Laisse a faux en usage normal. */
    /** ⭐⭐ IMPOSE LE MOTEUR au lieu de le faire voter. -1 = vote normal.

        C'est ce qui permet les QUATRE GAGNANTS : on lance la meme recherche
        quatre fois, une par generateur, et l'utilisateur choisit a l'oreille
        entre quatre sons authentiquement differents - au lieu de faire defiler
        quarante-huit generations d'une meme lignee, qui sont toutes les
        ancetres de la derniere.

        /!\ ET ON PEUT SE LE PERMETTRE PARCE QUE LE BUDGET NE COMPTE PRESQUE
        PAS. Mesure de l'oracle : huit fois plus d'evaluations n'achetent que
        0,27 dB, a peine au-dessus du bruit du banc (0,26 dB). Diviser le budget
        par quatre ne coute donc quasiment rien - alors que ca multiplie par
        quatre le choix offert. Sans cette mesure, l'idee aurait paru trop
        chere. */
    int    moteurImpose = -1;

    bool   tirageHistorique = false;

    /** ⭐⭐ RELANCES MULTIPLES : on decoupe le budget en `relances` recherches
        independantes et on garde la meilleure.

        L'oracle a montre que la recherche PLAFONNE - huit fois le budget
        n'achete que 0,27 dB, donc les evaluations tardives d'une execution ne
        valent presque rien. Les depenser ailleurs devrait donc etre gratuit
        au pire, et payant au mieux : le paysage a des minima profonds qui ne
        sont pas la reponse (mesure a l'oracle : depuis une amorce chaude, la
        recherche s'ELOIGNE des vrais parametres, de ~20 a 52 unites par
        octet). Sortir d'un tel minimum demande de repartir, pas d'insister.

        /!\ 1 = comportement d'origine, exactement. */
    int    relances = 1;

    /** ⭐ PAS ADAPTATIF PAR COORDONNEE. Un sigma unique impose le meme pas a la
        coupure du filtre - qui change tout - et au melange de chorus - qui
        change peu. Chaque octet garde donc son propre facteur, monte quand il
        participe a une amelioration et redescend doucement sinon. */
    bool   pasAdaptatif = false;

    /** ⭐⭐ SELECTIONNER A TRAVERS LA MACHINE.

        On AJUSTE sur une cible propre et on ECOUTE a travers le convertisseur.
        Ce ne sont pas le meme classement : correlation de rang +0,42, et
        seulement 5 des 10 meilleurs au propre sont dans les 10 meilleurs a
        l'ecoute. La moitie de ce que la recherche designe comme « le
        meilleur » ne l'est donc pas pour l'oreille - et c'est exactement ce
        que l'utilisateur decrivait : « nos dernieres syntheses etaient parfois
        plus loin que certaines au centre ».

        Quand `cibleMachine` est fourni, la recherche garde les
        `candidatsMachine` meilleurs profils au propre, puis les REJUGE a
        travers la machine et rend celui qui gagne la-bas.

        /!\ ON N'AJUSTE PAS DESSUS, ON SELECTIONNE DESSUS. Chercher
        directement sur l'empreinte degradee ecraserait tout ce qui vit sous
        -40 dB : la recherche perdrait les queues, les detentes et les
        resonances basses, qu'elle sait pourtant reproduire. On garde donc un
        objectif fin pour DESCENDRE et un objectif juste pour CHOISIR.

        /!\ ET CA NE COUTE QU'UN RENDU PAR CANDIDAT RETENU - huit rendus sur
        trois mille evaluations, soit 0,3 % du budget. */
    const Empreinte* cibleMachine = nullptr;
    int    candidatsMachine = 8;

    /** ⭐⭐ MUTATION PONDEREE PAR L'IMPACT MESURE.

        La mutation d'origine tire `octetsMutes` indices UNIFORMEMENT parmi les
        ~150 octets mutables. Or ils sont tres loin de se valoir : la mesure du
        piege 35 donne 17,6 dB pour la coupure du filtre et 0,000 pour le
        relachement (avant correction). Tirer uniformement, c'est donc depenser
        la meme energie a bouger ce qui change tout et ce qui ne change rien.

        Ici on MESURE, une fois au demarrage et SUR CETTE CIBLE, ce que chaque
        octet mutable coute quand on le pousse de +/-40, puis on tire les
        indices proportionnellement. Le budget d'amorcage vaut le nombre
        d'octets mutables (~150 evaluations sur 3 000, soit 5 %).

        /!\ ET LE POIDS A UN PLANCHER. Un octet mesure a 0 garderait une
        probabilite nulle POUR TOUJOURS - or son impact depend du reste du
        profil, qui bouge. Le plancher garantit qu'aucune dimension ne meurt. */
    bool   mutationPonderee = false;

    /** ⭐⭐ DESCENTE PAR COORDONNEE, en alternance avec la couvee.

        Une generation sur `periodeCoord` remplace les enfants aleatoires par
        un BALAYAGE d'un seul octet : on essaie `nEnfants` valeurs reparties sur
        toute sa plage et on garde la meilleure. C'est une minimisation 1-D
        exacte a la resolution du balayage, la ou la mutation gaussienne ne
        fait qu'esperer.

        /!\ CE N'EST PAS LA « RECHERCHE PAR GROUPES » DEJA MESUREE PIRE. Celle-la
        restreignait la mutation ALEATOIRE a un groupe ; celle-ci remplace le
        hasard par un balayage deterministe sur UN octet. Le premier reduisait
        l'exploration sans rien garantir ; le second garantit une descente. */
    int    periodeCoord = 0;        // 0 = jamais

    bool   fenetreVariable = true;
    int    fenetreMin = 33000;      // 0,75 s
    int    fenetreMax = 176400;     // 4 s

    /** Garder la lignee pour l'interface. Couteux en memoire uniquement si on
        le demande : 48 generations x 8 enfants x 112 octets = 43 ko. */
    bool collecteLignee = false;

    /** D'ou vient l'amorce.

        ⭐⭐ LE DEFAUT EST `Reseau`, ET C'EST UNE MESURE QUI L'A IMPOSE - contre
        l'intuition, et contre ce que j'avais ecrit ici pendant des semaines.

        `Meilleur` essaie les deux amorces, garde celle dont la perte est la
        plus basse, puis cherche. Il devrait donc etre au moins aussi bon que le
        meilleur des deux. Mesure sur 37 sons, perte de reference :

            heuristique - reseau     +0,314 dB   t = +2,67   SIGNIFICATIF
            reseau - meilleur        -0,295 dB   t = -3,40   SIGNIFICATIF

        `Meilleur` est SIGNIFICATIVEMENT PIRE que `Reseau`. La raison est
        beaucoup plus interessante qu'un bug :

        /!\ PARTIR PLUS BAS N'EST PAS PARTIR MIEUX. La correlation entre
        « avoir la meilleure amorce » et « obtenir le meilleur resultat » vaut
        **r = -0,30**. Elle est NEGATIVE. Le reseau n'a la perte de depart la
        plus basse que sur 16 sons sur 37, mais il donne le meilleur resultat
        sur 21 - et sur les 21 sons ou il part PLUS HAUT que l'heuristique, il
        arrive quand meme mieux 15 fois, avec 0,45 dB de gain moyen.

        L'heuristique pose vingt et un partiels directement sur le spectre :
        c'est un ajustement local excellent et RIGIDE, que la recherche ne sait
        plus ameliorer. Le reseau propose quelque chose de structurellement plus
        proche de ce que le moteur sait atteindre, meme si c'est plus loin sur
        le moment.

        Choisir sur la perte d'amorce ne revient donc pas a choisir au hasard :
        ca revient a choisir le PERDANT. `Meilleur` reste disponible parce qu'il
        sert a mesurer ce fait, pas parce qu'il faut s'en servir. */
    enum class Amorce { Heuristique, Reseau, Meilleur };
    Amorce amorceDe = Amorce::Reseau;
};

/** ⭐ UN NOEUD DE LA LIGNEE. C'est l'objet que l'interface Genopatch affiche.

    La recherche (1 + lambda) EST deja un arbre : a chaque generation il y a un
    parent et lambda enfants, et on garde le meilleur. Genopatch n'est donc pas
    une couche a plaquer par-dessus, c'est une VUE de cette lignee - et les
    gestes de l'utilisateur y correspondent directement :

      · les vignettes autour d'un noeud   = les lambda enfants
      · « affiner celui-ci »              = continuer depuis ce noeud
      · « quatre versions proches »       = regenerer lambda enfants
      · choisir un noeud QUI N'EST PAS LE MEILLEUR = changer de parent

    /!\ Ce dernier geste est le seul qui demandait un vrai changement : la
    recherche ne remontait jamais en arriere. C'est pourtant lui qui rend
    l'outil jouable plutot qu'automatique - « l'echec sera musical ».
*/
constexpr int kApercu = 28;

struct Noeud
{
    /** ⭐ Un noeud porte le PROFIL ENTIER, pas seulement le materiau : le
        moteur choisi, ses reglages, le filtre, l'unisson, le grain. C'est ce
        qui permet a une branche d'etre un Juno et a sa voisine une cloche FM. */
    uint8_t profil[kOctetsProfil] = {};
    double perte = 0.0;
    bool retenu = false;

    /** ⭐ CE QUE LE NOEUD DONNE A VOIR : l'enveloppe d'amplitude du rendu, en
        `kApercu` points, normalisee sur sa crete.

        /!\ « 4,70 dB » ne veut rien dire pour l'oreille, et l'utilisateur l'a
        dit : « on ne comprend pas les nombres ». Une percussion, une nappe et
        une cloche se reconnaissent d'un coup d'oeil a leur enveloppe ; leurs
        pertes se ressemblent toutes. On garde le nombre pour ordonner, on
        montre la forme pour choisir.

        /!\ Elle est calculee PENDANT la recherche, sur le rendu qu'on a deja
        en main. La recalculer a l'affichage voudrait dire refaire 384 rendus
        sur le thread de l'interface. */
    float apercu[kApercu] = {};

    Recette recette() const noexcept { return depuisOctets (profil); }
    EnvAmpli enveloppe() const noexcept { return envDepuisOctets (profil + kOffEnv); }
    ReglagesCouche couche() const noexcept { return coucheDepuisProfil (profil); }
};

/** Une generation : le parent et ses enfants. */
struct Generation
{
    std::vector<Noeud> enfants;
    double pertePere = 0.0;
    int evaluations = 0;
};

constexpr int kGenerationsGardees = 48;

/** ⭐ CE QUE LA RECHERCHE EST EN TRAIN DE FAIRE.

    /!\ Ce n'est pas de la decoration. Avant le vote de moteur, la recherche
    passait par trois phases dont l'interface ne montrait AUCUNE : l'analyse,
    le vote d'octave, puis le vote de moteur - et l'utilisateur voyait un
    panneau mort pendant tout ce temps. « La resynthese est tres lente a
    demarrer » etait donc, pour moitie, un probleme d'affichage : le travail
    avait commence, rien ne le disait.
*/
enum class PhaseRecherche { Repos, Analyse, Octave, Moteur, Affinage };

struct EtatRecherche
{
    PhaseRecherche phase = PhaseRecherche::Repos;
    /** Pendant le vote de moteur : celui qu'on essaie, et ce que chacun a
        donne (1e9 = pas encore essaye). */
    int moteurEssaye = -1;
    double perteMoteur[4] = { 1e9, 1e9, 1e9, 1e9 };
    /** Pendant le vote d'octave : la hauteur retenue jusqu'ici. */
    double f0Vue = 0.0;

    uint8_t profil[kOctetsProfil] = {};
    Recette meilleure;              // le materiau, extrait du profil
    EnvAmpli enveloppe;             // l'enveloppe de couche, extraite du profil
    std::vector<Generation> lignee;
    double  perteMeilleure = 1e9;
    double  perteDepart = 1e9;
    int     evaluationsFaites = 0;
    double  sigma = 0.0;
};

/** Ce que la recherche a le droit de toucher.

    /!\ PAS TOUT. Les octets de version et de graine n'ont rien a faire dans
    une optimisation : le premier casserait la relecture, le second ferait
    varier la perte sans rien ameliorer - la recherche passerait son temps a
    tirer des des. Le nombre de partiels non plus : le changer d'un cran fait
    apparaitre ou disparaitre un partiel entier, ce qui rend le paysage
    discontinu pour rien (une amplitude a zero suffit a l'eteindre).
*/
inline bool octetMutable (int i) noexcept
{
    if (i == 0) return false;           // version
    if (i == 12 || i == 13) return false;   // graine
    if (i == 14 || i == 15) return false;   // nombre de partiels, reserve
    if (i >= 100 && i < kOctetsRecette) return false;   // reserve
    return true;   // au-dela : les 4 octets d'enveloppe, tous mutables
}

/** Un pas de recherche complet. `rappel` peut interrompre en rendant false. */
/** L'enveloppe d'amplitude d'un rendu, en `kApercu` points.

    /!\ CRETE PAR TRANCHE, pas un echantillon sur N. Sous-echantillonner un
    signal periodique donne un dessin qui depend de la phase : deux rendus
    identiques auraient des vignettes differentes, et l'utilisateur croirait
    voir deux sons.
*/
inline void remplitApercu (const std::vector<double>& y, float* sortie) noexcept
{
    for (int i = 0; i < kApercu; ++i) sortie[i] = 0.0f;
    if (y.empty()) return;
    const size_t n = y.size();
    double crete = 1e-12;
    for (int i = 0; i < kApercu; ++i)
    {
        const size_t a = (size_t) ((double) i * (double) n / (double) kApercu);
        const size_t b = (size_t) ((double) (i + 1) * (double) n / (double) kApercu);
        double m = 0.0;
        for (size_t k = a; k < b && k < n; ++k) m = std::max (m, std::abs (y[k]));
        sortie[i] = (float) m;
        crete = std::max (crete, m);
    }
    for (int i = 0; i < kApercu; ++i) sortie[i] = (float) (sortie[i] / crete);
}

template <typename Rappel>
inline EtatRecherche chercheUne (const Empreinte& cible, double fsSortie,
                                 double f0, const uint8_t* depart,
                                 const ReglagesRecherche& reg, Rappel rappel)
{
    EtatRecherche st;
    st.sigma = reg.sigma0;
    st.phase = PhaseRecherche::Affinage;

    unsigned int e = reg.graine * 2654435761u + 1u;
    auto uni = [&e] () noexcept
    {
        e = e * 1664525u + 1013904223u;
        return (double) (e >> 8) / 16777216.0;             // [0,1)
    };
    auto gauss = [&uni] () noexcept
    {
        // Box-Muller, une seule sortie : on n'a pas besoin de la seconde
        const double u1 = std::max (1e-12, uni()), u2 = uni();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * kPi * u2);
    };

    const double plancher = kPlancherPropre;
    std::vector<double> y;
    auto evalue = [&] (const uint8_t* o)
    {
        rendProfil (o, y, reg.nEchComparaison, fsSortie, f0);
        ++st.evaluationsFaites;
        return perte (cible, empreinte (y, fsSortie, plancher));
    };

    uint8_t parent[kOctetsCherches], enfant[kOctetsCherches],
            meilleurEnfant[kOctetsCherches];
    for (int i = 0; i < kOctetsCherches; ++i) parent[i] = depart[i];
    st.perteMeilleure = st.perteDepart = evalue (parent);
    if (! std::isfinite (st.perteMeilleure))
        st.perteMeilleure = st.perteDepart = 1e9;
    for (int i = 0; i < kOctetsProfil; ++i) st.profil[i] = parent[i];
    st.meilleure = depuisOctets (parent);
    st.enveloppe = envDepuisOctets (parent + kOffEnv);

    // /!\ Le moteur ne change PAS par mutation : il est choisi par un vote au
    // depart. Muter cet octet, ce serait sauter d'un Juno a une cloche FM en
    // gardant les reglages de l'un pour l'autre - un enfant qui ne ressemble a
    // aucun de ses parents n'apprend rien a la recherche.
    const TypeMoteur moteurCourant = coucheDepuisProfil (parent).moteur;

    // ⭐⭐ LES INDICES MUTABLES, PAR GROUPE, CALCULES UNE FOIS.
    //
    // /!\ L'ANCIENNE MUTATION TIRAIT SUR TOUT LE PROFIL PUIS JETAIT ce qui
    // n'etait pas mutable. Pour un moteur FM, 86 octets sur 230 le sont ; pour
    // l'analogique, 54. Dix tirages ne deplacaient donc que deux a quatre
    // octets selon le moteur - et `octetsMutes` ne voulait pas dire la meme
    // chose d'un moteur a l'autre, sans que rien ne le signale.
    //
    // En listant les indices d'abord, un tirage = une mutation, toujours.
    std::vector<int> tousMutables;
    for (int i = 0; i < kOctetsCherches; ++i)
        if (octetProfilMutable (i, moteurCourant)) tousMutables.push_back (i);
    if (tousMutables.empty()) return st;   // rien a chercher : on rend l'amorce

    // ⭐⭐ LE POIDS DE CHAQUE OCTET, MESURE SUR CETTE CIBLE.
    //
    // /!\ ON LE MESURE ICI ET PAS UNE FOIS POUR TOUTES : l'impact d'un octet
    // depend du profil ou il vit. Le rapport de modulation de l'operateur 3 ne
    // change rien si son niveau est a zero, et tout s'il est a fond. Une table
    // d'impacts calculee sur un profil moyen serait fausse sur les extremes,
    // c'est-a-dire precisement la ou la recherche a besoin d'aide.
    std::vector<double> poids (tousMutables.size(), 1.0);
    std::vector<double> cumul (tousMutables.size(), 0.0);
    // /!\ L'AMORCAGE COUTE UN NOMBRE FIXE D'EVALUATIONS - une par octet
    // mutable, soit ~150. C'est 5 % d'un budget de 3 000, mais 25 % du mode
    // « rapide (600) » que le plugin propose, et 37 % de 400. Mesure a 400 :
    // la ponderation fait PERDRE 3,3 dB, uniquement parce qu'elle a mange le
    // budget avant de servir.
    //
    // On ne l'active donc que si l'amorcage reste sous un dixieme du budget.
    // Un mecanisme qui aide sur les longs et sabote les courts serait un
    // mecanisme dont l'effet depend d'un reglage que l'utilisateur ne relie
    // pas a lui.
    const bool ponderer = reg.mutationPonderee
                       && (int) tousMutables.size() * 10 <= reg.evaluations;
    if (ponderer)
    {
        std::vector<uint8_t> t ((size_t) kOctetsCherches);
        double somme = 0.0;
        for (size_t k = 0; k < tousMutables.size(); ++k)
        {
            const int i = tousMutables[k];
            for (int j = 0; j < kOctetsCherches; ++j) t[(size_t) j] = parent[j];
            // On pousse dans la direction qui reste dans la plage.
            const int d = (parent[i] > 128) ? -40 : 40;
            t[(size_t) i] = (uint8_t) std::max (0, std::min (255, (int) parent[i] + d));
            const double p = evalue (t.data());
            // /!\ PLANCHER. Un octet mesure a zero ne doit pas mourir pour
            // toujours : son impact depend du reste du profil, qui bouge.
            poids[k] = std::max (0.05, std::isfinite (p)
                                          ? std::abs (p - st.perteMeilleure) : 0.0);
            somme += poids[k];
        }
        if (somme > 1e-9)
        {
            double c = 0.0;
            for (size_t k = 0; k < poids.size(); ++k) { c += poids[k] / somme; cumul[k] = c; }
        }
    }
    // A partir d'ici, `cumul` vide veut dire « tirage uniforme » - que la
    // ponderation ait ete refusee ou jamais demandee.

    // ⭐ Tire un indice selon les poids mesures. Recherche dichotomique dans le
    // cumul : la table ne bouge plus, donc autant la lire vite.
    auto tireIndicePondere = [&] () -> int
    {
        if (cumul.empty() || cumul.back() <= 0.0)
            return (int) (uni() * (double) tousMutables.size()) % (int) tousMutables.size();
        const double x = uni();
        size_t lo = 0, hi = cumul.size() - 1;
        while (lo < hi) { const size_t m = (lo + hi) / 2;
                          if (cumul[m] < x) lo = m + 1; else hi = m; }
        return (int) lo;
    };

    // ⭐ UN FACTEUR DE PAS PAR OCTET MUTABLE, a 1 au depart : tant que rien
    // n'a ete appris, tout le monde bouge pareil.
    std::vector<double> facteur (tousMutables.size(), 1.0);
    std::vector<int> indiceDe (kOctetsCherches, -1);
    for (int k = 0; k < (int) tousMutables.size(); ++k)
        indiceDe[(size_t) tousMutables[(size_t) k]] = k;

    // ⭐ LES K MEILLEURS AU PROPRE, pour les rejuger a la fin. Trie par perte
    // croissante, taille bornee : on ne garde jamais plus que ce qu'on rejugera.
    struct Candidat { double perte; uint8_t o[kOctetsProfil]; };
    std::vector<Candidat> podium;
    const int nPodium = (reg.cibleMachine != nullptr)
                          ? std::max (1, std::min (32, reg.candidatsMachine)) : 0;
    auto proposePodium = [&] (const uint8_t* o, double p)
    {
        if (nPodium <= 0 || ! std::isfinite (p)) return;
        if ((int) podium.size() >= nPodium && p >= podium.back().perte) return;
        Candidat c; c.perte = p;
        for (int i = 0; i < kOctetsProfil; ++i) c.o[i] = o[i];
        podium.push_back (c);
        std::sort (podium.begin(), podium.end(),
                   [] (const Candidat& a, const Candidat& b) { return a.perte < b.perte; });
        if ((int) podium.size() > nPodium) podium.pop_back();
    };
    proposePodium (parent, st.perteMeilleure);

    int succes = 0, generations = 0;
    while (st.evaluationsFaites < reg.evaluations)
    {
        double perteEnfant = 1e9;
        int iMeilleur = -1;
        bool trouve = false;
        std::vector<Noeud> enfantsGardes;

        // ⭐⭐ LES ENFANTS SONT EVALUES EN PARALLELE.
        //
        // Une generation (1 + lambda) est parfaitement parallele : les huit
        // enfants sont independants, on ne garde que le meilleur a la fin. Et
        // c'est devenu necessaire, pas confortable - depuis que le rendu passe
        // par le moteur entier, une evaluation coute 25 a 70 ms, donc une
        // recherche « rapide » de 600 evaluations demandait 100 SECONDES.
        // L'utilisateur a conclu, a juste titre, que « la resynthese ne marche
        // plus ».
        //
        // /!\ LES ENFANTS SONT TIRES D'ABORD, SEQUENTIELLEMENT, ET SEULEMENT
        // ENSUITE EVALUES. Le generateur aleatoire est une suite : le lire
        // depuis plusieurs fils donnerait un resultat different a chaque
        // execution, et on perdrait la propriete qui permet de comparer deux
        // recherches - deux graines identiques doivent donner le meme son.
        const int nEnfants = std::min (reg.lambda,
                                 std::max (1, reg.evaluations - st.evaluationsFaites));
        std::vector<std::vector<uint8_t>> couvee ((size_t) nEnfants,
                                        std::vector<uint8_t> ((size_t) kOctetsCherches));
        // Qui a bouge dans chaque enfant - seulement si le pas adaptatif sert.
        std::vector<std::vector<int>> mutes ((size_t) nEnfants);
        // /!\ LA RECHERCHE PAR GROUPES A ETE ESSAYEE ICI, ET MESUREE PIRE.
        // L'idee - ne bouger qu'un groupe a la fois, comme une descente par
        // coordonnees - se defend sur un paysage accidente. L'oracle dit non :
        //
        //     mutation d'origine ................ 3,227 dB
        //     par groupes (1 generation sur 4 globale) ... 3,198 dB
        //     tirages corriges, SANS groupes .... 3,033 dB
        //
        // Le groupement coute meme un controle : le melange de reverbe retrouve
        // tombe de 80 a 27 sur 255, parce que l'octet vise n'a plus qu'une
        // generation sur douze pour bouger. Ce qui a vraiment paye, c'est le
        // correctif des tirages ci-dessus. La table `groupesProfil` reste dans
        // Profil.h : elle servira a l'interface, pas a la recherche.
        const std::vector<int>& cible2 = tousMutables;

        // ⭐⭐ UNE GENERATION SUR N EST UN BALAYAGE, PAS UNE COUVEE.
        //
        // On choisit UN octet - pondere par son impact si la mesure existe -
        // et on essaie `nEnfants` valeurs reparties sur toute sa plage. C'est
        // une minimisation 1-D a la resolution du balayage : le meilleur des
        // huit est retenu par le meme code que d'habitude, donc la descente
        // reste monotone.
        //
        // /!\ CE N'EST PAS LA « RECHERCHE PAR GROUPES » DEJA MESUREE PIRE.
        // Celle-la restreignait la mutation ALEATOIRE a un groupe et n'offrait
        // aucune garantie ; ici le hasard ne sert qu'a CHOISIR la coordonnee,
        // le reste est deterministe.
        const bool balaye = (reg.periodeCoord > 0)
                          && (generations % reg.periodeCoord == reg.periodeCoord - 1)
                          && ! cible2.empty();
        const int kBalaye = balaye ? (ponderer ? tireIndicePondere()
                                        : ((int) (uni() * (double) cible2.size())
                                            % (int) cible2.size()))
                                   : 0;

        for (int l = 0; l < nEnfants; ++l)
        {
            uint8_t* e = couvee[(size_t) l].data();
            for (int i = 0; i < kOctetsCherches; ++i) e[i] = parent[i];
            if (balaye)
            {
                // /!\ ON BALAYE TOUTE LA PLAGE, PAS UN VOISINAGE. Un balayage
                // local ne ferait que refaire, plus cher, ce que la mutation
                // gaussienne fait deja. Ce qu'il apporte, c'est de pouvoir
                // SAUTER un mur : essayer 0 quand on est a 255.
                const int i = cible2[(size_t) kBalaye];
                e[i] = (uint8_t) std::min (255.0, std::round (
                           255.0 * (double) l / (double) std::max (1, nEnfants - 1)));
                continue;
            }
            // /!\ Le nombre d'octets deplaces ne depasse pas la taille du
            // groupe : muter dix fois dans un groupe de deux octets, c'est
            // dix tirages pour deux valeurs - du bruit, pas une exploration.
            if (reg.tirageHistorique)
            {
                for (int m = 0; m < reg.octetsMutes; ++m)
                {
                    const int i = (int) (uni() * (double) kOctetsCherches);
                    if (i >= kOctetsCherches
                        || ! octetProfilMutable (i, moteurCourant)) continue;
                    const double v = (double) e[i] + gauss() * st.sigma * 255.0;
                    e[i] = (uint8_t) std::max (0.0, std::min (255.0, std::round (v)));
                }
            }
            else
            {
                const int nMut = std::max (1, std::min (reg.octetsMutes,
                                                        (int) cible2.size()));
                for (int m = 0; m < nMut; ++m)
                {
                    // ⭐ Pondere par l'impact MESURE si on l'a demande, uniforme
                    // sinon. Un seul point d'entree : le reste du tirage est
                    // identique, donc l'A/B ne compare que ce qu'on croit.
                    // /!\ `ponderer`, PAS `reg.mutationPonderee` : si l'amorcage
                    // a ete refuse faute de budget, `cumul` est vide et le
                    // tirage doit redevenir uniforme. Lire la DEMANDE au lieu
                    // de la DECISION ferait tirer sur une table jamais remplie.
                    const int k = ponderer
                                    ? tireIndicePondere()
                                    : ((int) (uni() * (double) cible2.size())
                                        % (int) cible2.size());
                    const int i = cible2[(size_t) k];
                    // ⭐ Le facteur propre a cet octet multiplie le pas commun.
                    const double f = reg.pasAdaptatif ? facteur[(size_t) k] : 1.0;
                    const double v = (double) e[i] + gauss() * st.sigma * f * 255.0;
                    e[i] = (uint8_t) std::max (0.0, std::min (255.0, std::round (v)));
                    if (reg.pasAdaptatif) mutes[(size_t) l].push_back (k);
                }
            }
        }

        std::vector<double> pertes ((size_t) nEnfants, 1e9);
        std::vector<std::vector<float>> apercus ((size_t) nEnfants);
        {
            const int nFils = std::max (1, std::min (nEnfants,
                                  (int) std::thread::hardware_concurrency()));
            std::vector<std::thread> fils;
            std::atomic<int> prochain { 0 };
            auto travail = [&] ()
            {
                // /!\ UN TAMPON DE RENDU PAR FIL. Partager `y` ferait ecrire
                // huit fils dans le meme vecteur : la perte mesuree ne serait
                // meme pas celle du profil qu'on croit evaluer.
                std::vector<double> yl;
                for (;;)
                {
                    const int l = prochain.fetch_add (1);
                    if (l >= nEnfants) return;
                    rendProfil (couvee[(size_t) l].data(), yl,
                                reg.nEchComparaison, fsSortie, f0);
                    const double pl = perte (cible, empreinte (yl, fsSortie, plancher));
                    // /!\ UNE PERTE NON FINIE EST REJETEE, PAS PROPAGEE. Un
                    // profil qui fait diverger le filtre rend un signal plein de
                    // NaN, donc une empreinte de NaN, donc une perte NaN. Or
                    // TOUTE comparaison avec NaN est fausse : `p < meilleure`
                    // aussi bien que `p > meilleure`. Un tel enfant n'etait donc
                    // ni retenu ni ecarte - il restait, et la generation
                    // suivante repartait de lui.
                    pertes[(size_t) l] = std::isfinite (pl) ? pl : 1e9;
                    if (reg.collecteLignee)
                    {
                        apercus[(size_t) l].assign ((size_t) kApercu, 0.0f);
                        remplitApercu (yl, apercus[(size_t) l].data());
                    }
                }
            };
            for (int t = 1; t < nFils; ++t) fils.emplace_back (travail);
            travail();
            for (auto& t : fils) t.join();
        }
        st.evaluationsFaites += nEnfants;

        // /!\ ON INTERROGE LE RAPPEL DES LA FIN DE LA COUVEE, pas seulement en
        // fin de generation. C'est ce qui borne le temps que `arrete()` doit
        // attendre : au-dela, JUCE TUE le fil - et un fil tue pendant que ses
        // std::thread enfants tournent laisse des `std::thread` non joints,
        // donc `std::terminate`, donc l'hote qui tombe. La regle est simple :
        // un fil qu'on peut tuer ne doit jamais posseder d'autres fils.
        if (! rappel (st)) break;

        for (int l = 0; l < nEnfants; ++l)
        {
            const double p = pertes[(size_t) l];
            if (reg.collecteLignee)
            {
                Noeud n;
                for (int k = 0; k < kOctetsProfil; ++k) n.profil[k] = couvee[(size_t) l][(size_t) k];
                n.perte = p;
                if (! apercus[(size_t) l].empty())
                    for (int k = 0; k < kApercu; ++k) n.apercu[k] = apercus[(size_t) l][(size_t) k];
                enfantsGardes.push_back (n);
            }
            proposePodium (couvee[(size_t) l].data(), p);
            if (p < perteEnfant)
            {
                perteEnfant = p;
                for (int i = 0; i < kOctetsCherches; ++i)
                    meilleurEnfant[i] = couvee[(size_t) l][(size_t) i];
                iMeilleur = l;
                trouve = true;
            }
        }

        ++generations;

        // /!\ ON NE GARDE QUE LES GENERATIONS QUI ONT SERVI. Enregistrer les
        // 375 generations d'une recherche de 3 000 evaluations donnerait un
        // arbre que personne ne peut lire, et dont 90 % des branches n'ont mene
        // nulle part. On garde celles ou le parent a change - c'est le chemin
        // reellement parcouru.
        const bool amelioration = trouve && perteEnfant < st.perteMeilleure;
        if (reg.collecteLignee && amelioration
            && (int) st.lignee.size() < kGenerationsGardees)
        {
            Generation g;
            g.pertePere = st.perteMeilleure;
            g.evaluations = st.evaluationsFaites;
            for (int l = 0; l < (int) enfantsGardes.size(); ++l)
                g.enfants.push_back (enfantsGardes[(size_t) l]);
            st.lignee.push_back (g);
        }

        if (trouve && perteEnfant < st.perteMeilleure)
        {
            st.perteMeilleure = perteEnfant;
            for (int i = 0; i < kOctetsCherches; ++i) parent[i] = meilleurEnfant[i];
            // ⭐ CE QUI A SERVI OSE PLUS GRAND. On ne recompense QUE les octets
            // de l'enfant retenu : recompenser tous ceux qui ont bouge dans la
            // couvee reviendrait a recompenser le hasard.
            if (reg.pasAdaptatif && iMeilleur >= 0)
                for (int k : mutes[(size_t) iMeilleur])
                    facteur[(size_t) k] = std::min (4.0, facteur[(size_t) k] * 1.25);
            for (int i = 0; i < kOctetsProfil; ++i) st.profil[i] = parent[i];
            st.meilleure = depuisOctets (parent);
            if (reg.collecteLignee && ! st.lignee.empty())
            {
                auto& g = st.lignee.back();
                for (auto& n : g.enfants)
                    if (n.perte <= perteEnfant + 1e-12) { n.retenu = true; break; }
            }
            ++succes;
        }

        // regle du cinquieme : plus d'un succes sur cinq -> on ose plus grand
        if (generations % 10 == 0)
        {
            const double taux = (double) succes / 10.0;
            st.sigma *= (taux > 0.2) ? 1.5 : 0.82;
            st.sigma = std::max (0.002, std::min (0.35, st.sigma));
            succes = 0;
            // /!\ ET LES FACTEURS RETOMBENT DOUCEMENT. Sans cet oubli, un
            // octet recompense tot garderait un pas enorme pour toujours - la
            // recherche finirait par ne plus explorer que lui.
            if (reg.pasAdaptatif)
                for (auto& f : facteur) f = 1.0 + (f - 1.0) * 0.85;
        }

        if (! rappel (st)) break;
    }

    // ⭐⭐ LE REJUGEMENT. C'est ici qu'on change de juge, et seulement ici.
    if (reg.cibleMachine != nullptr && ! podium.empty())
    {
        double meilleure = 1e9;
        int gagnant = -1;
        for (int k = 0; k < (int) podium.size(); ++k)
        {
            rendProfil (podium[(size_t) k].o, y, reg.nEchComparaison, fsSortie, f0);
            // /!\ ON DEGRADE COMME LA MACHINE, ET ON REMONTE LE PLANCHER. Les
            // deux vont ensemble : juger un signal huit bits avec un plancher a
            // -85 dB reviendrait a comparer du bruit de quantification a du
            // bruit de quantification.
            for (auto& v : y) v = quantifie (v, Quant::Lin8);
            const double p = perte (*reg.cibleMachine,
                                    empreinte (y, fsSortie, kPlancherDegrade));
            if (std::isfinite (p) && p < meilleure) { meilleure = p; gagnant = k; }
        }
        if (gagnant >= 0)
        {
            for (int i = 0; i < kOctetsProfil; ++i) st.profil[i] = podium[(size_t) gagnant].o[i];
            st.meilleure = depuisOctets (st.profil);
            st.enveloppe = envDepuisOctets (st.profil + kOffEnv);
            // /!\ ON N'ECRASE PAS `perteMeilleure` AVEC LA PERTE MACHINE. Les
            // deux ne sont pas dans la meme unite - l'une a un plancher a -85,
            // l'autre a -40 - et tout ce qui lit ce champ (l'interface, les
            // outils d'evaluation, le vote) le compare a des pertes propres.
            // On rend la perte PROPRE du profil retenu.
            st.perteMeilleure = podium[(size_t) gagnant].perte;
        }
    }
    return st;
}

/** ⭐⭐ LA RECHERCHE, AVEC SES RELANCES.

    `relances = 1` rend exactement `chercheUne` : le comportement d'origine est
    donc un cas particulier, pas une variante.

    /!\ CHAQUE RELANCE PART DU MEME ENDROIT AVEC UNE AUTRE GRAINE. Repartir
    d'un point different serait une autre experience - on veut mesurer ce que
    coute d'INSISTER sur une trajectoire, pas ce que vaut une autre amorce.

    /!\ ET LE RAPPEL VOIT UN COMPTEUR CONTINU. Sans le decalage, la barre de
    progression reviendrait en arriere a chaque relance, ce qui se lit comme un
    plantage. */
template <typename Rappel>
inline EtatRecherche cherche (const Empreinte& cible, double fsSortie,
                              double f0, const uint8_t* depart,
                              const ReglagesRecherche& reg, Rappel rappel)
{
    const int R = std::max (1, reg.relances);
    if (R == 1) return chercheUne (cible, fsSortie, f0, depart, reg, rappel);

    ReglagesRecherche r = reg;
    r.evaluations = std::max (32, reg.evaluations / R);
    r.collecteLignee = reg.collecteLignee;

    EtatRecherche meilleur;
    bool premier = true;
    int faites = 0;
    for (int k = 0; k < R; ++k)
    {
        r.graine = reg.graine + (unsigned int) k * 7919u;
        bool continuer = true;
        auto pont = [&] (const EtatRecherche& e)
        {
            EtatRecherche v = e;
            v.evaluationsFaites += faites;
            continuer = rappel (v);
            return continuer;
        };
        auto st = chercheUne (cible, fsSortie, f0, depart, r, pont);
        faites += st.evaluationsFaites;
        if (premier || st.perteMeilleure < meilleur.perteMeilleure)
        {
            meilleur = st;
            premier = false;
        }
        if (! continuer) break;
    }
    meilleur.evaluationsFaites = faites;
    return meilleur;
}

/** ⭐ REPRENDRE DEPUIS UN NOEUD CHOISI A LA MAIN.

    C'est le geste qui distingue un outil d'un bouton : l'utilisateur peut
    partir d'une version qui n'est PAS la meilleure - parce qu'elle lui plait,
    ou parce qu'elle rate d'une facon interessante. La recherche repart de la
    et oublie ce qu'elle croyait savoir.
*/
template <typename Rappel>
inline EtatRecherche affine (const Empreinte& cible, double fsSortie, double f0,
                             const Noeud& depart,
                             const ReglagesRecherche& reg, Rappel rappel)
{
    return cherche (cible, fsSortie, f0, depart.profil, reg, rappel);
}

/** Chaine complete : un son -> une recette. */
template <typename Rappel>
inline EtatRecherche resynthetise (const std::vector<double>& son, double fsSon,
                                   const ConfigGrain& cfg,
                                   const ReglagesRecherche& reg0,
                                   RapportAnalyse& rap, Rappel rappel,
                                   const Reseau* reseau = nullptr)
{
    // /!\ ON ROGNE LE SILENCE DE TETE AVANT TOUT, et la cible ET l'analyse
    // partent du meme endroit. Sinon on compare le debut du rendu au silence de
    // la cible, et la perte mesure un decalage au lieu d'un timbre.
    const int onset = trouveOnset (son);
    const std::vector<double> cale (son.begin() + onset, son.end());

    ReglagesRecherche reg = reg0;
    if (reg.fenetreVariable)
        reg.nEchComparaison = std::max (reg.fenetreMin,
                                  std::min (reg.fenetreMax, (int) cale.size()));
    const double plancher = reg.ajusteSurLaMachine ? kPlancherDegrade
                                                   : kPlancherPropre;

    // La cible est TRONQUEE a la meme longueur que le rendu : comparer une
    // empreinte de 3 s a une de 0,5 s aligne des trames qui n'ont rien a voir.
    const std::vector<double> cible (cale.begin(),
        cale.begin() + std::min ((size_t) reg.nEchComparaison, cale.size()));
    const Empreinte empCible = empreinte (cible, fsSon, plancher);

    // ⭐ L'ENVELOPPE DE DEPART EST MESUREE SUR LA CIBLE, pas devinee. Elle sert
    // d'amorce aux quatre octets d'enveloppe du profil ; la recherche reste
    // libre de la deplacer, ou de rendre l'attaque au materiau.
    EnvAmpli envDepart;
    envDepart.actif = true;
    {
        const double lis = 1.0 - std::exp (-1.0 / (0.005 * fsSon));
        double e = 0.0, pic = 0.0; int iPic = 0;
        std::vector<double> env (cible.size());
        for (size_t i = 0; i < cible.size(); ++i)
        {
            e += lis * (std::abs (cible[i]) - e);
            env[i] = e;
            if (e > pic) { pic = e; iPic = (int) i; }
        }
        envDepart.attaque = std::max (0.001, std::min (3.0, (double) iPic / fsSon));
        double dec = (double) (cible.size() - (size_t) iPic) / fsSon;
        const double seuil = pic * 0.251;
        for (size_t i = (size_t) iPic; i < env.size(); ++i)
            if (env[i] <= seuil) { dec = (double) ((int) i - iPic) / fsSon; break; }
        envDepart.decli = std::max (0.005, std::min (8.0, dec));
        envDepart.tenue = std::max (0.0, std::min (1.0,
                             env.empty() ? 0.5 : env.back() / std::max (pic, 1e-9)));
        envDepart.relache = 0.5;
    }

    // ⭐ L'OCTAVE EST TRANCHEE PAR LA PERTE, pas par une heuristique de plus.
    // Toutes les regles spectrales essayees se faisaient avoir sur un son
    // bruite : PERCUSIVE KEY 1 rendait 36,5 Hz au lieu de ~146. On a deja un
    // rendu et une distance en qui on a confiance - autant les faire voter.
    //
    // /!\ MAIS ON NE PREND PAS SIMPLEMENT LA MEILLEURE PERTE. Une f0 plus basse
    // peut TOUJOURS expliquer davantage : ses partiels contiennent ceux de
    // l'octave au-dessus. La perte n'a aucune preference pour la simplicite,
    // donc laissee seule elle descend indefiniment - PAD 1 est tombe a 77 Hz.
    // Regle : on garde la f0 LA PLUS HAUTE qui arrive a moins de 5 % de la
    // meilleure perte.
    // ⭐ ON ANNONCE CHAQUE ETAPE. L'interface a besoin de savoir qu'il se passe
    // quelque chose avant meme la premiere evaluation.
    EtatRecherche annonce;
    annonce.phase = PhaseRecherche::Analyse;
    rappel (annonce);

    RapportAnalyse r0;
    const Recette brute = amorce (cale, fsSon, r0);
    rap = r0;

    uint8_t prof[kOctetsProfil];
    profilNeutre (prof);
    poseRecetteDansProfil (prof, brute);
    // l'enveloppe mesuree sur la cible
    {
        uint8_t e[4];
        envVersOctets (envDepart, e);
        for (int i = 0; i < 4; ++i) prof[kOffEnv + i] = e[i];
    }

    double f0Retenue = (r0.f0 > 0.0) ? r0.f0 : 220.0;
    std::vector<double> y;

    if (r0.f0 > 0.0)
    {
        annonce.phase = PhaseRecherche::Octave;
        annonce.f0Vue = r0.f0;
        rappel (annonce);

        static const double hyp[] = { 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25 };
        struct Essai { double f; double p; Recette r; RapportAnalyse ra; };
        std::vector<Essai> essais;
        double mieux = 1e9;
        for (double h : hyp)
        {
            const double f = r0.f0 * h;
            if (f < 20.0 || f > 4000.0) continue;
            RapportAnalyse ri;
            const Recette c = amorce (cale, fsSon, ri, f);
            uint8_t t[kOctetsProfil];
            for (int i = 0; i < kOctetsProfil; ++i) t[i] = prof[i];
            poseRecetteDansProfil (t, c);
            rendProfil (t, y, reg.nEchComparaison, fsSon, f);
            const double p = perte (empCible, empreinte (y, fsSon, plancher));
            essais.push_back ({ f, p, c, ri });
            mieux = std::min (mieux, p);
        }
        for (const auto& es : essais)
            if (es.p <= mieux * 1.05)
            {
                poseRecetteDansProfil (prof, es.r);
                rap = es.ra; rap.f0 = es.f; f0Retenue = es.f;
                break;
            }
    }

    // ⭐⭐ LE VOTE DE MOTEUR. Meme principe que le vote d'octave, et pour la
    // meme raison : on a un rendu et une distance en qui on a confiance, alors
    // qu'aucune heuristique ne saurait dire a l'avance si un son est « plutot
    // analogique » ou « plutot FM ».
    //
    // /!\ ET C'EST LE CHANGEMENT QUI COMPTE LE PLUS. Un Juno se decrit avec
    // huit valeurs sur le moteur analogique ; en additif il en faut
    // soixante-douze, pour un resultat moins bon. La recherche cherchait donc
    // depuis toujours avec le mauvais outil sur la moitie du corpus.
    //
    // /!\ ON DONNE UN PETIT BUDGET A CHAQUE MOTEUR AVANT DE COMPARER, et ce
    // n'est pas un raffinement : c'est ce qui rend le vote honnete. La ROM
    // arrive DEJA AJUSTEE - `amorce` lui a pose vingt et un partiels sur le
    // spectre de la cible - alors que les trois autres arrivent avec leurs
    // valeurs par defaut. Compare tel quel, le vote choisissait la ROM sur une
    // cible FM et sur une cible de table d'ondes : il ne mesurait pas le
    // moteur, il mesurait qui avait deja travaille.
    if (reg.moteurImpose >= 0 && reg.moteurImpose < 4)
    {
        // ⭐ MOTEUR IMPOSE : pas de vote, on va droit au generateur demande.
        // C'est le chemin des QUATRE GAGNANTS - chacun est le meilleur que SON
        // generateur sait faire, ce qui donne quatre sons differents au lieu de
        // quatre instants d'une meme lignee.
        prof[kOffMoteur] = (uint8_t) (reg.moteurImpose * 64);
        annonce.phase = PhaseRecherche::Moteur;
        annonce.moteurEssaye = reg.moteurImpose;
        annonce.f0Vue = f0Retenue;
        rappel (annonce);
    }
    else
    {
        uint8_t meilleurProfil[kOctetsProfil];
        for (int i = 0; i < kOctetsProfil; ++i) meilleurProfil[i] = prof[i];
        double meilleure = 1e9;

        const int nVote = std::min ((int) cible.size(), 12000);
        const Empreinte empVote = empreinte (
            std::vector<double> (cible.begin(), cible.begin() + nVote),
            fsSon, plancher);

        // ⭐⭐ LE VOTE EST COURT, ET C'EST UNE MESURE QUI L'A IMPOSE.
        //
        // On lui donnait `evaluations / 16` par moteur sur la fenetre complete.
        // Mesure sur des cibles dont on connait le moteur : le vote rend 3 sur
        // 4 A TOUS LES BUDGETS, de 8 a 150 evaluations par moteur, et de 6 000
        // a 24 000 echantillons de fenetre. Sa justesse est PLATE ; seul son
        // cout variait - de 0,27 s a 5,58 s.
        //
        // C'est logique apres coup : choisir entre « additif », « analogique »,
        // « FM » et « table d'ondes » est une decision GROSSIERE. Elle se prend
        // sur l'allure generale du spectre, pas sur un detail qui demanderait
        // d'affiner. Ce qui demande du budget, c'est ce qui vient APRES.
        //
        // /!\ ET LA FENETRE COMPTAIT PLUS QUE LE BUDGET. Avec une fenetre
        // variable, un sample de 4 s donnait 176 400 echantillons a rendre par
        // evaluation : le vote a lui seul prenait alors une minute, pendant
        // laquelle l'interface ne montrait rien. C'est ce que l'utilisateur a
        // signale comme « la resynthese est tres lente a demarrer ».
        ReglagesRecherche essai = reg;
        essai.evaluations = 32;
        essai.fenetreVariable = false;
        essai.nEchComparaison = std::min (reg.nEchComparaison, 12000);
        essai.collecteLignee = false;
        auto rien = [] (const EtatRecherche&) { return true; };

        for (int m = 0; m < 4; ++m)
        {
            uint8_t t[kOctetsProfil];
            for (int i = 0; i < kOctetsProfil; ++i) t[i] = prof[i];
            t[kOffMoteur] = (uint8_t) (m * 64);
            // /!\ L'EMPREINTE DU VOTE EST CELLE DE LA FENETRE COURTE. Comparer
            // un rendu de 0,27 s a une empreinte de 4 s alignerait des trames
            // qui n'ont rien a voir - c'est la meme faute que celle qu'on avait
            // corrigee en tronquant la cible, et elle se reintroduit des qu'on
            // change une fenetre quelque part.
            annonce.phase = PhaseRecherche::Moteur;
            annonce.moteurEssaye = m;
            annonce.f0Vue = f0Retenue;
            rappel (annonce);

            const auto e = cherche (empVote, fsSon, f0Retenue, t, essai, rien);
            annonce.perteMoteur[m] = e.perteMeilleure;
            rappel (annonce);

            if (e.perteMeilleure < meilleure)
            {
                meilleure = e.perteMeilleure;
                for (int i = 0; i < kOctetsProfil; ++i) meilleurProfil[i] = e.profil[i];
            }
        }
        for (int i = 0; i < kOctetsProfil; ++i) prof[i] = meilleurProfil[i];
        // le budget consomme par le vote est retire de celui de la recherche
        // /!\ Le budget consomme reste retire de celui de la recherche, mais
        // il ne vaut plus que 128 evaluations au lieu du quart du total.
        reg.evaluations = std::max (50, reg.evaluations - 4 * essai.evaluations);
    }

    // ⭐ LE RESEAU EST UN CANDIDAT DE PLUS, jamais un remplacant : c'est la
    // perte qui tranche, donc un reseau qui se trompe ne peut pas faire pire.
    //
    // /!\ IL PROPOSE MAINTENANT UN PROFIL ENTIER - moteur compris. Il n'est
    // donc plus reserve aux cibles que le vote a classees ROM, et c'est tout
    // l'interet : il a ete entraine sur les quatre generateurs.
    //
    // /!\ ET IL PASSE APRES LE VOTE, en concurrence avec lui. S'il proposait
    // avant, son choix de moteur deciderait a la place de la perte - alors que
    // c'est precisement la perte qui a montre qu'elle savait trancher.
    const bool aReseau = (reseau != nullptr) && reseau->pret
                       && reg.amorceDe != ReglagesRecherche::Amorce::Heuristique;
    if (aReseau && rap.f0 > 0.0)
    {
        uint8_t t[kOctetsProfil];
        devineProfil (*reseau, cale, fsSon, f0Retenue, t);

        // /!\ LE CHOIX SE FAIT SUR UNE FENETRE FIXE - correction gardee, mais
        // elle N'A PAS suffi et mon hypothese de depart etait fausse. Je
        // croyais que `Meilleur` echouait parce qu'il comparait sur une fenetre
        // variable ; apres correction il est reste aussi mauvais (-0,295 dB,
        // t = -3,40). Le vrai probleme est que la perte d'amorce ANTI-PREDIT le
        // resultat final (r = -0,30) : aucun critere disponible a ce moment-la
        // ne dit laquelle des deux amorces menera le plus loin. On garde la
        // fenetre fixe parce qu'un selecteur doit juger sur le critere qui le
        // jugera - mais ca ne rachete pas le mode.
        //
        // /!\ Ce mode existe pour etre ROBUSTE : prendre le reseau quand il
        // aide, l'heuristique quand il nuit. Il devrait donc etre au moins
        // aussi bon que le meilleur des deux. Il etait SIGNIFICATIVEMENT PIRE
        // que le reseau seul - 0,257 dB, t = -3,00 sur 37 sons.
        //
        // La raison : il choisissait sur la perte d'AJUSTEMENT, prise sur une
        // fenetre VARIABLE selon la duree du son, alors qu'on le juge sur une
        // fenetre FIXE. Il preferait donc parfois une amorce qui gagne sur sa
        // propre fenetre et perd sur la fenetre commune.
        //
        // Un selecteur « le meilleur des deux » n'est sur QUE s'il choisit sur
        // le critere qui servira a le juger. Sinon il ne choisit pas le
        // meilleur : il choisit le mieux-disant sur une autre question.
        const int nFixe = std::min ((int) cible.size(), 66150);   // 1,5 s
        const Empreinte empFixe = empreinte (
            std::vector<double> (cible.begin(), cible.begin() + nFixe),
            fsSon, plancher);

        rendProfil (t, y, nFixe, fsSon, f0Retenue);
        const double pr = perte (empFixe, empreinte (y, fsSon, plancher));
        rendProfil (prof, y, nFixe, fsSon, f0Retenue);
        const double ph = perte (empFixe, empreinte (y, fsSon, plancher));

        if (reg.amorceDe == ReglagesRecherche::Amorce::Reseau || pr < ph)
            for (int i = 0; i < kOctetsProfil; ++i) prof[i] = t[i];
    }

    auto st = cherche (empCible, fsSon, f0Retenue, prof, reg, rappel);
    // /!\ On RECOPIE le resultat du vote dans l'etat final : sans ca, le
    // panneau perdrait les quatre pertes des moteurs des que l'affinage
    // commence, et l'utilisateur ne saurait plus pourquoi ce moteur-la.
    for (int i = 0; i < 4; ++i) st.perteMoteur[i] = annonce.perteMoteur[i];
    st.f0Vue = f0Retenue;
    return st;
}

} // namespace romance
