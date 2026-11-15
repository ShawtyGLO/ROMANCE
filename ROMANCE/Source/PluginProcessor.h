#pragma once

#include <JuceHeader.h>
#include <vector>

#include "Moteur.h"
#include "Effets.h"
#include "Effets2.h"
#include "Effets3.h"
#include "Rack.h"
#include "Machines.h"
#include "Recette.h"
#include "Spectre.h"
#include "Genopatch.h"
#include "Macros.h"

/** ROMANCE - synthe / rompleur a ROM generee.

    Version BRUTE assumee : editeur generique, une tirette par parametre, aucune
    decoration. Meme methode que XPERT comp, ou elle avait paye - trois decisions
    que les fichiers ne pouvaient pas trancher l'ont ete en manipulant, bien
    avant qu'il y ait une facade.
*/
class RomanceProcessor : public juce::AudioProcessor,
                         private juce::AsyncUpdater,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    RomanceProcessor();
    ~RomanceProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ROMANCE"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    /** Pose une recette sur une couche et refabrique sa ROM.

        ⭐ C'est le seul point d'entree de la resynthese : elle ne produit pas
        un son, elle produit une RECETTE. Le reste du plugin ne sait pas d'ou
        elle vient - deposee, cherchee, ou ecrite a la main.
    */
    void poseRecette (int couche, const romance::Recette& r);

    /** ⭐ Pose un PROFIL DE RESYNTHESE entier sur une couche : le materiau,
        mais aussi le moteur retenu par le vote, ses reglages, le filtre,
        l'unisson, l'enveloppe et le grain.

        /!\ Ce que le profil ne couvre pas n'est PAS touche - panoramique,
        zones de clavier, boucle, modulations, effets restent a la main de
        l'utilisateur. Une resynthese qui ecraserait le patch entier ne serait
        pas un outil, ce serait un remplacement. */
    void poseProfil (int couche, const uint8_t* profil);

    /** ⭐ Pose la saveur de convertisseur sur les QUATRE couches.

        /!\ C'est une POSE, pas un mode. Les reglages de grain restent
        modifiables apres coup, exactement comme apres une resynthese : le menu
        de machine est un point de depart, pas une prison. Et « Propre » en est
        une position de plein droit - l'utilisateur doit pouvoir entendre
        l'instrument sans aucun caractere de rompleur. */
    void poseMachine (int machine);

    /** ⭐ TIRE UN PATCH PLAUSIBLE sur une couche.

        /!\ Ce n'est PAS un tirage uniforme dans les parametres - ce serait un
        generateur de bruit. C'est `tireProfil`, le prior calibre sur les
        quantiles MESURES de 1 665 sons du corpus : hauteur log-normale de
        mediane 131 Hz, attaques et decroissances aux bonnes proportions,
        modulations et effets rares. On avait construit cet objet pour
        entrainer le reseau, et on ne s'en servait pas pour jouer. */
    void tireAuSort (int couche);

    /** Copier / coller une couche entiere, parametres compris. */
    void copieCouche (int couche);
    void colleCouche (int couche);
    bool aUneCoucheEnMemoire() const noexcept { return presseOccupe_; }

    /** Le materiau tel qu'il sonne, pour l'affichage.

        /!\ Rend le tampon DEJA QUANTIFIE, celui que le lecteur parcourt : ce
        qu'on montre doit etre ce qu'on entend, pas une version idealisee.
        `boucle` est la position de debut de boucle en fraction, ou 0.
    */
    const double* materiau (int couche, int& n, double& boucle) const noexcept;

    /** ⭐ LES TETES DE LECTURE D'UNE COUCHE, pour l'affichage. Positions entre
        0 et 1 de ce que l'editeur dessine. Rend combien il y en a. */
    static constexpr int kTetesMax = 16;
    int tetes (int couche, float* sortie, int maxi) const noexcept;

    /** Les reglages d'une couche, tels que le moteur les joue en ce moment.

        /!\ POUR L'AFFICHAGE SEULEMENT. Ils sont ecrits par le fil message a
        chaque bloc ; les lire depuis l'editeur peut attraper un etat a
        mi-chemin. Pour dessiner une forme d'onde c'est sans consequence, pour
        rendre de l'audio ca ne le serait pas. */
    const romance::ReglagesCouche& reglagesCouche (int c) const noexcept
    { return patch_.couche[juce::jlimit (0, romance::kNbCouches - 1, c)]; }

    /** Le moteur de resynthese. Il vit sur son propre fil ; l'interface le
        pilote, le thread audio ne le voit jamais. */
    romance::Genopatch geno;

    /** ⭐⭐ LE RESEAU DE GENOPATCH, charge depuis `Documents/ROMANCE/reseau.rnn`.

        /!\ PAS EMBARQUE DANS LE BINAIRE, ET C'EST UN CHOIX. Trois virgule huit
        megaoctets de poids dans le VST3 pour un fichier qu'il faudra remplacer
        a chaque reentrainement - donc recompiler et redeployer a chaque fois.
        Depuis Documents, on le remplace et on recharge le plugin.

        /!\ ET SON A\ENCE N'EST PAS UNE PANNE. L'heuristique reste un mode
        valide : sans reseau, la resynthese fonctionne, elle part juste d'un
        autre endroit. Ce qui SERAIT une panne, c'est de ne pas le DIRE - voir
        `diagnosticReseau()`. */
    romance::Reseau reseau;

    /** Le chemin attendu, expose pour que la facade puisse le montrer. */
    static juce::File fichierReseau();

    /** Un message court, affichable tel quel sur la page RESYNTHESE. */
    juce::String diagnosticReseau() const
    { return juce::String (reseau.diagnostic()); }

    /** La configuration d'epoque a utiliser pour la recherche : celle de la
        couche visee, pour qu'on cherche sur la machine ou l'on jouera.
        /!\ Mais avec le grain NEUTRALISE : on cherche la recette du SON, pas
        celle du son deja passe dans une machine (voir Perte.h). */
    romance::ConfigGrain configRecherche() const noexcept;
    const romance::Recette& recette (int couche) const noexcept
    { return recettes_[couche]; }

    /** ⭐ LE DESSIN D'UNE COUCHE. L'editeur le lit pour l'afficher et l'ecrit
        quand on clique ; le moteur ne voit, lui, que la banque limitee en bande
        qui en decoule. */
    const romance::DessinAB& dessin (int couche) const noexcept
    { return dessins_[couche]; }

    /** ⭐ LE SPECTRE DESSINE D'UNE COUCHE - le second materiau.

        Il existe parce que l'etalon a mesure que le poste le plus cher du
        format etait tout ce qui n'est PAS harmonique. Voir Spectre.h. */
    const romance::Spectre& spectre (int couche) const noexcept
    { return spectres_[couche]; }
    void poseSpectre (int couche, const romance::Spectre& sp);
    int versionSpectre (int couche) const noexcept { return spectreVersion_[couche]; }
    void poseDessin (int couche, const romance::DessinAB& d);
    /** Le numero de version : l'editeur s'en sert pour savoir qu'un dessin a
        change sans avoir a comparer soixante-quatre octets douze fois par
        seconde. */
    int versionDessin (int couche) const noexcept { return dessinVersion_[couche]; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout disposition();

    void relitParams() noexcept;
    void construitRoms();
    void construitRom (int couche, bool force);
    void construitDessin (int couche, bool force);
    void handleAsyncUpdate() override;
    /** Un echantillon complet : voix, racks de tranche, somme, rack master. */
    void rendUnEchantillon (double& g, double& d) noexcept;
    void traiteEffets (double& g, double& d) noexcept;
    /** Le rack d'insert d'une couche, applique a son bus avant la somme. */
    void traiteRackCouche (int couche, double& g, double& d) noexcept;

    romance::Moteur moteur_;
    romance::ReglagesPatch patch_;

    /** ⭐ LE BANC DE MODULATEURS GLOBAL. Les memes huit emplacements que dans
        chaque voix, mais lus UNE FOIS PAR BLOC : c'est ce qui permet de moduler
        les effets, dont il n'existe qu'un exemplaire.

        /!\ Ses enveloppes se redeclenchent sur la PREMIERE note d'une phrase,
        pas sur chaque note - sinon la queue de reverbe sauterait a chaque doigt
        pose. */
    romance::Modulateur modsGlobaux_[romance::kNbMods];
    /** La derniere note frappee, pour les sources de jeu du banc GLOBAL.
        /!\ -1 tant que rien n'a ete joue : `sourceTouche` rend alors 0, donc
        un cable TOUCHE -> effet ne fait rien avant la premiere note plutot que
        de partir de cinq octaves sous le do du milieu. */
    int derniereNote_ = -1, derniereVelo_ = 0;

    /** ⭐⭐ LES TROIS CONTROLEURS DE JEU, ET POURQUOI CE NE SONT PAS DES
        PARAMETRES D'HOTE.

        Un parametre automatisable aurait deux ecrivains qui se battent : la
        molette du clavier et la ligne d'automation de l'hote. Et le bend
        arrive a une centaine de messages par seconde - autant de
        `setValueNotifyingHost` depuis le fil audio, ce que pluginval n'aime
        pas et que l'hote enregistrerait dans le projet.

        Ce sont donc des valeurs de JEU : elles ne s'enregistrent pas, elles ne
        s'automatisent pas, elles vivent le temps qu'on joue. La bandelette de
        la page JOUER ecrit dedans exactement comme le MIDI - dernier arrive,
        dernier servi - ce qui les rend testables A LA SOURIS, sans clavier a
        aftertouch.

        /!\ `std::atomic` parce que l'editeur ecrit depuis le fil message
        pendant que le fil audio lit. */
    std::atomic<float> jeuBend_ { 0.0f };      // -1 .. +1
    std::atomic<float> jeuMolette_ { 0.0f };   // 0 .. 1
    std::atomic<float> jeuPression_ { 0.0f };  // 0 .. 1

public:
    /** Ce que la bandelette de jeu ecrit, et ce qu'elle relit pour SUIVRE le
        MIDI : un seul jeu de valeurs, deux ecrivains. */
    void poseJeu (int quoi, float v) noexcept
    {
        if (quoi == 0) jeuBend_.store (v);
        else if (quoi == 1) jeuMolette_.store (v);
        else jeuPression_.store (v);
    }
    float litJeu (int quoi) const noexcept
    {
        return quoi == 0 ? jeuBend_.load()
             : quoi == 1 ? jeuMolette_.load() : jeuPression_.load();
    }
private:
    bool uneNoteTenue_ = false;

    /** ⭐⭐ CINQ RACKS : un par couche, plus le master.

        /!\ Les anciens membres individuels ont disparu au profit de
        `racks_[kMaster]`. Les garder « au cas ou » aurait laisse deux chemins
        pour le meme effet, et le jour ou l'un des deux recoit un reglage que
        l'autre ignore, le son depend de quel chemin a ete pris. */
    static constexpr int kMaster = romance::kNbCouches;   // 4
    romance::RackFx racks_[romance::kNbCouches + 1];
    void parameterChanged (const juce::String& id, float valeur) override;
    std::atomic<int> machineAPoser_ { -1 };

    /** ⭐⭐ LE CACHE DE POINTEURS DE PARAMETRES.

        /!\ `relitParams` tourne A CHAQUE BLOC AUDIO et faisait, pour chacun de
        ses ~310 parametres, DEUX choses interdites sur le fil audio :
          · construire une `juce::String` (« c2_ » + « fm3_ » + « ratio ») -
            donc une ALLOCATION SUR LE TAS, qui peut prendre un verrou ;
          · une recherche par hachage dans la table de l'APVTS.
        A 344 blocs par seconde, cela faisait plus de cent mille allocations
        par seconde sur le fil qui n'a pas le droit d'attendre.

        Ici on retient les pointeurs au PREMIER passage, dans l'ordre exact des
        appels, puis on ne fait plus qu'indexer un tableau.

        /!\ CET ORDRE DOIT RESTER FIXE. `relitParams` n'a aucun branchement qui
        change sa suite d'appels - c'est ce qui rend l'indexation legitime. En
        debug, chaque lecture verifie que l'identifiant attendu est bien celui
        qu'on avait retenu, donc toute reorganisation future se signalera
        immediatement au lieu de lire le mauvais parametre en silence. */
    std::vector<std::atomic<float>*> cacheParam_;
   #if JUCE_DEBUG
    std::vector<juce::String> cacheNom_;
   #endif
    size_t iCache_ = 0;

    /** Chronometre du fil audio, en pourcentage du temps reel disponible.
        /!\ Sans lui on optimise a l'aveugle : mes mesures hors ligne ne
        voyaient QUE le DSP, jamais la lecture des parametres ni l'hote. */
    std::atomic<float> chargeTotale_ { 0.0f }, chargeParams_ { 0.0f },
                       chargeVoix_ { 0.0f }, chargeEffets_ { 0.0f };
    std::atomic<int> nNotes_ { 0 }, nCouches_ { 0 }, moteurVu_ { 0 };

public:
    struct Charge { float total, params, voix, effets; int notes, couches, moteur; };
    /** /!\ On publie AUSSI le nombre de voix, de couches actives et le moteur.
        Sans eux, « 73,7 % » ne se compare a rien : la meme charge est saine
        pour vingt-quatre voix-couches de FM et catastrophique pour six de ROM.
        Un chiffre sans son diviseur n'est pas une mesure. */
    Charge charge() const noexcept
    {
        return { chargeTotale_.load(), chargeParams_.load(),
                 chargeVoix_.load(), chargeEffets_.load(),
                 nNotes_.load(), nCouches_.load(), moteurVu_.load() };
    }
private:
    /** Le presse-papier de couche : les valeurs BRUTES des parametres, plus la
        recette. /!\ La recette DOIT en faire partie : coller une couche sans
        son materiau donnerait un patch qui ne ressemble a rien de ce qu'on a
        copie, et l'utilisateur en conclurait que le collage ne marche pas. */
    std::vector<std::pair<juce::String, float>> presse_;
    romance::Recette presseRecette_;
    romance::DessinAB presseDessin_;
    romance::Spectre presseSpectre_;
    bool presseOccupe_ = false;
    unsigned int graineDe_ = 1u;

    /** ⭐ BLOCAGE DE CONTINUE EN SORTIE.

        /!\ CE N'EST PAS UNE EXPLICATION, C'EST UN FILET. Sur 3 000 patchs
        tires au de, 30 % sortent avec plus de 0,05 de composante continue - le
        materiau additif n'est pas symetrique quand ses bandes de bruit et son
        transitoire dominent. Une continue ne s'entend pas, mais elle mange de
        la reserve et elle CHARGE LE VU-METRE, ce qui ressemble a s'y meprendre
        a un son trop fort qu'on n'entend pas.

        Un pole a 5 Hz : assez bas pour ne rien retirer d'une sub-basse a 25 Hz,
        assez haut pour evacuer la continue en une fraction de seconde. */
    double dcG_ = 0.0, dcD_ = 0.0, dcXg_ = 0.0, dcXd_ = 0.0, dcA_ = 0.9993;



    // /!\ LA ROM EST STOCKEE QUANTIFIEE - c'est une propriete du materiau, pas
    // du signal de sortie. On garde donc une version BRUTE et une version
    // QUANTIFIEE.
    //
    // ⭐ DEUX BANQUES PAR COUCHE, et c'est ce qui rend le changement de source
    // silencieux. On fabrique toujours dans la banque INACTIVE, puis on publie
    // l'indice. Les voix qui sonnent ont copie leur pointeur au declenchement :
    // elles finissent sur l'ancienne banque, qui reste valide.
    //
    // /!\ TROIS DANGERS, tous reels :
    //   1. fabriquer une ROM de recette coute quelques millions de sin() : ca ne
    //      peut pas se faire sur le thread audio (d'ou l'AsyncUpdater) ;
    //   2. les tampons sont reserves a kRomMax une fois pour toutes et ne
    //      changent plus jamais d'adresse ;
    //   3. on n'ECRIT jamais dans une banque qu'une voix est en train de lire.
    struct Banque
    {
        std::vector<double> brute, quant;
        const double* ptr = nullptr;
        int taille = 0;
        romance::Quant quantPosee = romance::Quant::Aucune;
    };
    Banque banques_[romance::kNbCouches][2];
    std::atomic<int> banqueActive_[romance::kNbCouches] { 0, 0, 0, 0 };
    int sourcePosee_[romance::kNbCouches] { -1, -1, -1, -1 };

    // ⭐ LES BANQUES DESSINEES, meme discipline exactement : douze octaves de
    // seize images par couche, fabriquees sur le fil message, ecrites dans
    // celle que personne ne lit, publiees par un indice atomique.
    romance::DessinAB dessins_[romance::kNbCouches];
    romance::BanqueDessin banquesDessin_[romance::kNbCouches][2];
    std::atomic<int> dessinActif_[romance::kNbCouches] { 0, 0, 0, 0 };
    romance::DessinAB dessinPose_[romance::kNbCouches];
    int dessinVersion_[romance::kNbCouches] { 0, 0, 0, 0 };
    bool dessinJamaisPose_[romance::kNbCouches] { true, true, true, true };

    // ⭐ Le spectre dessine, meme discipline que la recette et le dessin : ce
    // n'est pas un parametre APVTS mais du MATERIAU, range a part dans l'etat.
    romance::Spectre spectres_[romance::kNbCouches];
    int spectreVersion_[romance::kNbCouches] { 0, 0, 0, 0 };
    int spectrePose_[romance::kNbCouches]    { -1, -1, -1, -1 };

    // /!\ La recette N'EST PAS un parametre APVTS. 108 octets par couche, c'est
    // l'etat du materiau, pas un reglage : l'exposer en 108 tirettes serait
    // illisible. Elle est rangee a part dans l'etat, et `recetteVersion_` sert
    // a savoir qu'il faut refabriquer la ROM.
    romance::Recette recettes_[romance::kNbCouches];
    int recetteVersion_[romance::kNbCouches] { 0, 0, 0, 0 };
    int recettePosee_[romance::kNbCouches]   { -1, -1, -1, -1 };
    // debut de boucle propose par la recette (fraction de la ROM)
    double boucleRecette_[romance::kNbCouches] { 0.0, 0.0, 0.0, 0.0 };

    // le thread audio ne fabrique rien : il constate et il demande.
    std::atomic<bool> refaireRoms_ { false };

    /** L'instantane des tetes de lecture : ecrit une fois par bloc par le fil
        audio, relu par l'editeur. Des flottants nus - au pire une tete qui
        saute d'un pixel, jamais un verrou sur le fil audio. */
    std::atomic<float> tetes_[romance::kNbCouches][kTetesMax];
    std::atomic<int>   nTetes_[romance::kNbCouches] { 0, 0, 0, 0 };

    double fs_ = 44100.0;
    float  sortieLissee_ = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RomanceProcessor)
};
