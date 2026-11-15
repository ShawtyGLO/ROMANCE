// =============================================================================
//  PluginEditor.h - Deux pages : JOUER et ATELIER.
//
//  ⭐ DEUX PAGES, PAS TROIS. La page RESYNTHESE n'existe pas encore parce que
//  la recherche n'est pas encore DANS le plugin (elle vit dans outils/resynth).
//  Un onglet qui ne fait rien est pire qu'un onglet manquant : il promet.
//
//  /!\ DES NOMS, PAS DES VALEURS. Retour de l'utilisateur sur la maquette :
//  « c'est tres charge avec beaucoup d'info parfois superflus ». Les valeurs
//  n'apparaissent qu'au survol et pendant qu'on tourne.
//
//  Organisation validee :
//    A - materiau / lecture      B - bandeau LIER (les couches)
//    C - une ADSR par couche     D - panneau machine
// =============================================================================
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace facade
{
    // Une seule palette, sombre, sans decoration. La couleur sert a distinguer
    // les groupes, pas a faire joli.
    const juce::Colour kFond      { 0xff14151a };
    const juce::Colour kPanneau   { 0xff1d1f27 };
    const juce::Colour kTrait     { 0xff2c2f3a };
    const juce::Colour kTexte     { 0xffb9becd };
    const juce::Colour kTexteFort { 0xffe8ecf5 };
    const juce::Colour kAccent    { 0xffd08a4a };   // le materiau
    const juce::Colour kAccent2   { 0xff6fa8c7 };   // la machine
    const juce::Colour kAccent3   { 0xff8bbf7a };   // le mouvement
}

/** Un bouton rond, avec son nom dessous. La valeur ne s'affiche que pendant
    qu'on tourne - c'est la demande explicite de l'utilisateur. */
class Bouton : public juce::Component,
               public juce::DragAndDropTarget,
               private juce::Timer
{
public:
    Bouton (juce::AudioProcessorValueTreeState& s, const juce::String& id,
            const juce::String& nom, juce::Colour teinte);
    void resized() override;
    void paint (juce::Graphics&) override;
    /** /!\ La bulle de valeur doit s'afficher DANS LE PLUS HAUT PARENT, sinon
        elle est rognee par le bouton lui-meme - qui fait 40 pixels de haut. */
    void parentHierarchyChanged() override;

    /** ⭐⭐ ON LACHE UNE SOURCE SUR UN BOUTON, ET LE CABLE EXISTE.

        C'est le geste central de Pigments, et c'est ce qui remplace « ouvrir
        la page matrice, choisir une ligne, derouler trente-huit destinations,
        revenir voir si c'etait la bonne ». La destination n'a pas a etre
        nommee : elle est SOUS LE CURSEUR. */
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { survole_ = true;  repaint(); }
    void itemDragExit  (const SourceDetails&) override { survole_ = false; repaint(); }
    void itemDropped   (const SourceDetails&) override;

private:
    void timerCallback() override;
    /** Les cables qui arrivent sur ce bouton, relus depuis les parametres. */
    void relitCables();
    juce::Slider curseur_;
    juce::Label  nom_;
    juce::Colour teinte_;
    juce::String id_;
    /** /!\ NON POSSEDANT, et trouve en remontant la hierarchie plutot que
        passe au constructeur : il y a plus de cent appels de `new Bouton` dans
        ce fichier, et un pointeur statique casserait des qu'un projet
        contiendrait deux instances de ROMANCE. */
    class RomanceEditor* edit_ = nullptr;
    /** Resout l'editeur a la demande : un bouton peut naitre avant d'etre
        rattache a la fenetre (rack FX). */
    class RomanceEditor* editeur();
    /** Vrai quand un glisser est en cours ET que ce bouton peut le recevoir :
        c'est ce qui rend le geste devinable. */
    bool cibleDrag_ = false;
    struct Arrivee { int source; float a, b; };   // a,b = bornes normalisees
    juce::Array<Arrivee> cables_;
    /** ⭐ On ne redessine QUE si le cablage a change. Sans cette signature, un
        minuteur a 8 Hz repeindrait cent boutons en permanence. */
    juce::int64 signature_ = -1;
    bool survole_ = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lien_;
};

/** ⭐⭐ LA BANDE DES SOURCES, EN BAS ET SUR TOUTES LES PAGES.

    Elle est le pendant du bouton : on l'attrape ici, on la lache la-bas. La
    poser dans la page MATRICE seulement aurait garde le defaut qu'on corrige -
    il faudrait changer de page pour cabler, donc quitter des yeux ce qu'on
    veut moduler.

    /!\ SURVOLER UNE PASTILLE ALLUME SES DESTINATIONS. C'est la question
    inverse - « qu'est-ce que cette source touche ? » - et c'est celle qu'on se
    pose quand un patch bouge sans qu'on sache pourquoi. */
class BandeSources : public juce::Component, private juce::Timer
{
public:
    explicit BandeSources (class RomanceEditor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> rect (int i) const;
    int sous (juce::Point<int>) const;
    class RomanceEditor& edit_;
};

/** Un interrupteur, meme logique. */
class Bascule : public juce::Component
{
public:
    Bascule (juce::AudioProcessorValueTreeState& s, const juce::String& id,
             const juce::String& nom);
    void resized() override;

private:
    juce::ToggleButton bouton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lien_;
};

/** Un menu deroulant. */
class Menu : public juce::Component
{
public:
    Menu (juce::AudioProcessorValueTreeState& s, const juce::String& id,
          const juce::String& nom);
    void resized() override;

private:
    juce::ComboBox boite_;
    juce::Label    nom_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lien_;
};

/** Un groupe encadre et titre. C'est lui qui porte l'organisation : sans
    regroupement, 220 tirettes ne sont pas un instrument. */
class Groupe : public juce::Component, private juce::Button::Listener
{
public:
    Groupe (const juce::String& titre, juce::Colour teinte);
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Ajoute une commande TOUJOURS visible. */
    void ajoute (juce::Component* c) { ajoute (c, -1); }

    /** ⭐⭐ AJOUTE UNE COMMANDE SUR UNE PAGE. C'est la demande : « les 4
        operateurs ranges en onglet ». Vingt-huit boutons FM cote a cote, c'est
        une grille ou l'on cherche ; sept boutons TOUJOURS AUX MEMES SEPT
        PLACES, c'est une grille ou l'on vise. On change d'operateur en un clic
        et le geste suivant est au meme endroit qu'avant.

        `page` = -1 : la commande reste visible sur toutes les pages. */
    void ajoute (juce::Component* c, int page)
    {
        contenu_.add (c); pages_.add (page); addAndMakeVisible (c);
    }

    /** Cree la barre d'onglets, avec `n` etiquettes. */
    void reglePages (const juce::StringArray& etiquettes);

    /** ⭐ MASQUE UNE COMMANDE QUI N'A PAS DE SENS DANS L'ETAT COURANT.

        Distinct des pages : une page, c'est un choix de l'utilisateur ; un
        masque, c'est une commande qui ne s'applique tout simplement pas - le
        `morph` n'agit que sur la topologie Z-Plane. La laisser visible et
        inerte, c'est un bouton qui ment.

        /!\ La grille se REFLUE : la case ne reste pas vide, les suivantes
        remontent. Un trou serait aussi bavard que le bouton qu'on retire. */
    void masque (juce::Component* c, bool cache);

    /** ⭐ Ce qu'on fait d'un clic droit sur le FOND du groupe.

        /!\ SANS CA, UN EMPLACEMENT OCCUPE EST INATTEIGNABLE. Le menu ne
        propose « retirer » que sur un emplacement rempli, mais le seul chemin
        pour l'ouvrir visait toujours le premier emplacement LIBRE - donc
        jamais un effet pose. On pouvait ajouter, jamais enlever. */
    std::function<void()> surClicDroit;

    /** ⭐ Ce qu'on fait d'un glisser sur le FOND du groupe : le deplacement
        vertical parcouru, en pixels, une fois le bouton relache. */
    std::function<void (int)> surGlisse;

    /** ⭐⭐ LA HAUTEUR DONT CE GROUPE A BESOIN pour que RIEN ne soit rogne.

        /!\ SANS ELLE, LA PAGE DEVINE - ET ELLE DEVINAIT FAUX. La page FX
        donnait 72 pixels a chaque groupe ; il en fallait 96. Les boutons
        debordaient donc de 12 px, et comme un enfant est rogne par son parent,
        c'est exactement la bande du bas qui disparaissait - c'est-a-dire LE
        LIBELLE. Mesure : 1 pixel visible sur 13.

        Le symptome etait « on ne voit plus les noms des boutons », et il a ete
        cherche dans la police, dans la couleur et dans le nombre de colonnes.
        Il etait dans une soustraction.

        /!\ ET C'EST POURQUOI `Bascule` ET `Menu` S'AFFICHAIENT, EUX : ils
        ecrivent leur texte en HAUT. Seul `Bouton` le met en bas. Deux
        comportements dans la meme grille, dont un seul cassait - de quoi
        chercher longtemps du mauvais cote. */
    int hauteurVoulue() const;

    /** Nombre de colonnes de la grille interne. */
    int colonnes = 4;
    /** /!\ Hauteur MINIMALE d'une case. Un bouton rond de JUCE se dimensionne
        sur le plus petit cote : dans une case large et basse, il devient un
        point. C'est ce qui rendait les pages FX et MATRICE illisibles. */
    int hauteurCase = 62;

private:
    /** ⭐ Le nombre de rangees, calcule UNE FOIS et lu par `resized()` comme
        par `hauteurVoulue()`. Deux calculs separes finiraient par diverger -
        c'est exactement la faute qu'on vient de payer ailleurs. */
    int nbRangees() const;
    void buttonClicked (juce::Button*) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String titre_;
    juce::Colour teinte_;
    juce::OwnedArray<juce::Component> contenu_;
    juce::Array<int> pages_;
    juce::Array<juce::Component*> masques_;
    juce::OwnedArray<juce::TextButton> onglets_;
    int pageActive_ = 0;
};

/** ⭐⭐ LA BANDELETTE DE JEU : bend, molette, pression.

    Trois raisons d'exister, et la troisieme est la moins evidente :

    1. On peut ESSAYER l'aftertouch sans clavier a aftertouch. C'est la
       question posee : « je ne pourrais pas le tester, on fait comment ? ».
    2. Elle SUIT le MIDI. Bouger la molette du clavier fait bouger la
       bandelette - donc on voit ce que le plugin recoit, et le jour ou un
       contrôleur n'arrive pas, on le SAIT au lieu de le supposer.
    3. Elle rend ces trois sources visibles. Une source de matrice qui
       n'apparait nulle part en facade est une source que personne ne branche.

    /!\ LE BEND REVIENT AU CENTRE QUAND ON LACHE, les deux autres restent ou
    on les laisse. C'est le comportement du materiel - une molette de hauteur
    est a ressort, une molette de modulation ne l'est pas - et s'en ecarter
    ferait jouer faux sans qu'on comprenne pourquoi. */
class Bandelette : public juce::Component, private juce::Timer
{
public:
    explicit Bandelette (RomanceProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent& e) override { bouge (e); }
    void mouseDrag (const juce::MouseEvent& e) override { bouge (e); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void bouge (const juce::MouseEvent&);
    juce::Rectangle<int> rect (int i) const;
    RomanceProcessor& proc_;
    /** Ce que l'on a affiche au dernier tour : on ne redessine que si ca
        change, sinon on repeint douze fois par seconde pour rien. */
    float vu_[3] = { 0.0f, 0.0f, 0.0f };
    int attrape_ = -1;
};

/** La page JOUER : les trois macros en grand, et le strict necessaire. */
class PageJouer : public juce::Component, private juce::Button::Listener,
                  private juce::Timer
{
public:
    explicit PageJouer (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void buttonClicked (juce::Button*) override;
    RomanceProcessor* proc_ = nullptr;
    void timerCallback() override;
    juce::TextButton bDe_ { "DE" };
    juce::ComboBox coucheDe_;
    /** ⭐ La charge du fil audio, mesuree DANS le plugin. Elle vaut mieux que
        celle de l'hote : elle dit AUSSI ou part le temps. */
    juce::Label charge_;
    juce::OwnedArray<juce::Component> gros_;
    juce::OwnedArray<juce::Label> legendes_;
    std::unique_ptr<Groupe> machine_, sortie_, macros_, machineG_, jeu_;
    std::unique_ptr<Bandelette> bande_;
};

/** Le materiau, dessine.

    ⭐ On montre le tampon DEJA QUANTIFIE - celui que le lecteur parcourt. Une
    forme d'onde idealisee mentirait exactement la ou ce plugin est cense etre
    honnete : c'est le materiau degrade qu'on entend.

    /!\ Enveloppe min/max par colonne, pas un echantillon sur N. Sous-echantillonner
    un signal de 45 000 points sur 400 pixels donne un dessin qui depend de la
    phase et qui saute a chaque redessin, alors que le son n'a pas bouge.
*/
class Onde : public juce::Component, private juce::Timer
{
public:
    Onde (RomanceProcessor& p, int couche);
    void paint (juce::Graphics&) override;
    void changeCouche (int c) { couche_ = c; repaint(); }

private:
    void timerCallback() override;
    /** ⭐ Rend deux cycles du moteur courant quand il n'y a PAS de materiau -
        analogique, FM, table. Sans ca, l'affichage restait vide sur trois
        moteurs sur quatre : « qu'elle nous renseigne sur le son que produit
        l'operateur en question, peu importe le type ». */
    void rendApercu();
    RomanceProcessor& proc_;
    int couche_ = 0;
    std::vector<float> hauts_, bas_;
    double boucle_ = 0.0;
    int nEch_ = 0;
    int moteur_ = 0;
    /** Les tetes de lecture, une par note qui sonne. */
    float tetes_[RomanceProcessor::kTetesMax] = {};
    int nTetes_ = 0;
};

/** ⭐⭐ LA GRILLE DESSINABLE (Fors Junior).

    On clique, on trace, et le cycle qui en sort est l'oscillateur. Trois
    resolutions se melangent dans ce geste et il faut les tenir separees :

      - LA CELLULE n'a aucune quantification, c'est une aide de visee ;
      - LA VALEUR STOCKEE est toujours sur huit bits, donc baisser puis remonter
        la profondeur rend le dessin INTACT ;
      - LA QUANTIFICATION EST REELLE et se fait au rendu.

    /!\ ON DESSINE EN GLISSANT, pas cellule par cellule. Obliger a cliquer
    soixante-quatre fois pour tracer une forme, c'est transformer un geste en
    corvee - et le plaisir de dessiner est toute la raison d'etre de ce moteur.
    On interpole donc entre la position precedente et la nouvelle, sinon un
    glissement rapide laisse des trous.
*/
class Grille : public juce::Component, private juce::Timer
{
public:
    Grille (RomanceProcessor& p, int couche);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void changeCouche (int c) { couche_ = c; dernier_ = -1; relit(); }

private:
    void timerCallback() override;
    void relit();
    void trace (const juce::MouseEvent&);
    /** Enregistre ou charge une forme, par le selecteur de fichiers du systeme. */
    void fichierForme (bool enregistrer);
    /** /!\ IL DOIT SURVIVRE A L'APPEL : un selecteur asynchrone detruit avant
        que l'utilisateur ait choisi emporte son rappel avec lui. */
    std::unique_ptr<juce::FileChooser> selecteur_;
    RomanceProcessor& proc_;
    int couche_ = 0;
    romance::DessinAB d_;
    /** ⭐ Lequel des deux dessins on modifie : 0 = A, 1 = B.

        /!\ DESSINER DANS B L'ACTIVE. Un onglet B qu'il faudrait activer a part
        serait un piege : on dessine, rien ne change, et on cherche pourquoi. */
    int edite_ = 0;
    int version_ = -1;
    /** L'index de la derniere cellule touchee, pour interpoler pendant le
        glissement. -1 = pas de trace en cours. */
    int dernier_ = -1;
};

/** ⭐⭐ LE SPECTRE DESSINABLE - 64 bandes, en source.

    Meme geste que la grille temporelle, mais dans l'autre domaine : ici on
    dessine CE QU'ON ENTEND, bande par bande, au lieu d'une forme d'onde.

    Il existe parce que l'etalon a mesure que le poste le plus cher du format
    etait tout ce qui n'est PAS harmonique - bruit, inharmonicite,
    transitoires - et que notre recette leur consacrait deux bandes.

    /!\ L'AXE EST LOGARITHMIQUE, comme l'oreille. Sur un axe lineaire, la
    moitie des cellules couvriraient au-dessus de 10 kHz - la ou l'on ne
    dessine presque jamais - et les cinq premieres octaves tiendraient dans
    trois pixels.
*/
class BandesSpectre : public juce::Component, private juce::Timer
{
public:
    BandesSpectre (RomanceProcessor& p, int couche);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void changeCouche (int c) { couche_ = c; dernier_ = -1; relit(); }

private:
    void timerCallback() override;
    void relit();
    void trace (const juce::MouseEvent&);
    RomanceProcessor& proc_;
    int couche_ = 0;
    romance::Spectre sp_;
    int version_ = -1;
    int dernier_ = -1;
};

/** ⭐⭐ LA COURBE DU FILTRE - une fenetre qu'on ouvre, pas une page de plus.

    La demande, mot pour mot : « une fenetre ouvrable qui montre la frequence
    filtree haute et basse, et le type de filtre choisi - ca permet de ne pas
    encombrer et de ne pas passer notre vie dans des menus plutot que de faire
    de la musique ».

    ⭐ ET ELLE CORRIGE UN MALENTENDU QUE MON INTERFACE AVAIT CREE. J'avais mis
    le passe-haut et le passe-bas en ONGLETS `BAS / HAUT`, ce qui les fait
    passer pour une alternative. Ils travaillent ENSEMBLE - le passe-haut
    devant, le passe-bas derriere, en permanence. Un seul trace le montre;
    deux onglets le cachaient.

    /!\ LA COURBE EST MESUREE, PAS CALCULEE. On envoie une impulsion dans les
    VRAIS objets `PasseHaut` et `FiltreMusical` et on regarde ce qui sort.
    Redessiner la reponse a partir des formules serait une deuxieme
    implementation a maintenir : le jour ou la topologie analogique change son
    ecrasement de resonance, la courbe mentirait sans que rien ne le signale.

    /!\ La coupure du filtre musical est RELATIVE A LA HAUTEUR. La courbe est
    donc tracee pour une note de reference, et elle le dit - sinon on lirait
    une frequence absolue qui n'existe que pour un do3.
*/
class CourbeFiltre : public juce::Component, private juce::Timer
{
public:
    CourbeFiltre (RomanceProcessor& p, int couche);
    void paint (juce::Graphics&) override;
    void changeCouche (int c) { couche_ = c; recalcule(); }
    /** La note pour laquelle on trace, en Hz. */
    static constexpr double kNoteRef = 261.6255653;

private:
    void timerCallback() override;
    void recalcule();
    RomanceProcessor& proc_;
    int couche_ = 0;
    /** La reponse en dB, une valeur par bande logarithmique. */
    std::vector<float> courbe_;
    double fcBas_ = 1000.0, fcHaut_ = 20.0, reso_ = 0.0, resoHaut_ = 0.0;
    int topo_ = 0, pente_ = 1;
    /** L'empreinte des reglages, pour ne recalculer que s'ils ont bouge. */
    double signature_ = -1.0;
};

/** La page FX : ce que la machine ajoute apres les voix. */
/** ⭐⭐ LA PAGE FX - une console, pas une liste de modules.

    Refonte demandee par l'utilisateur, image a l'appui : une colonne de
    TRANCHES a gauche (A, B, C, D, MASTER) et, a droite, le RACK de celle qui
    est selectionnee. Le nom reste FX : ce n'est pas une page de plus, c'est la
    nouvelle version de celle-la.

    ⭐ ON N'AJOUTE UN EFFET QU'EN LE DEMANDANT. L'ancienne page montrait les dix
    groupes en permanence, plus une rangee de dix menus d'ordre - un mur, dont
    l'utilisateur a dit qu'il n'etait « pas tres beau ». Ici le rack est VIDE au
    depart ; un clic droit ouvre la liste des effets, et seuls ceux qu'on a
    poses occupent de la place.

    /!\ ET LE CLIC DROIT SERT AUSSI A RETIRER. Un menu qui ne sait qu'ajouter
    oblige a garder un « - » dans une liste pour defaire - exactement le
    mecanisme imposé qu'on retire. Sur un emplacement occupe, le meme menu
    propose le remplacement ET le retrait, en rouge, comme dans la reference.
*/
class PageFx : public juce::Component,
               private juce::Button::Listener,
               private juce::Timer
{
public:
    explicit PageFx (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void buttonClicked (juce::Button*) override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void construitRack();
    /** Ouvre la liste des effets. `slot` = -1 pour ajouter a la suite. */
    void menuEffets (int slot);
    /** Le prefixe de parametres de la tranche `t` : vide pour le master. */
    juce::String prefixe (int t) const;
    /** L'identifiant du parametre d'un emplacement. Le master et les tranches
        ne le nomment pas pareil - voir la note dans le .cpp. */
    juce::String idSlot (int tranche, int slot) const;
    /** La hauteur d'une ligne du rack, calculee depuis les groupes eux-memes.
        Lue par `resized()` ET par le glisser-deposer : un seul nombre. */
    int hauteurLigne_ = 78;
    /** Deplace l'effet de l'emplacement `de` vers `vers`, en decalant ce qui
        se trouve entre les deux. */
    void deplaceSlot (int de, int vers);
    /** Le premier emplacement libre du rack courant, ou -1. */
    int premierLibre() const;

    RomanceProcessor& proc_;
    /** 0..3 = les couches, 4 = le master. */
    int tranche_ = 4;
    juce::OwnedArray<juce::TextButton> onglets_;
    juce::OwnedArray<juce::Component>  faders_;
    juce::OwnedArray<Groupe> lignes_;
    /** L'effet pose dans chaque emplacement au dernier montage. */
    int poseDansSlot_[romance::kEmplacementsFx] = { -1 };
    /** La zone cliquable « ajouter un effet », sous le dernier emplacement. */
    juce::TextButton bAjoute_ { "+  ajouter un effet" };
    juce::Viewport vue_;
    juce::Component contenu_;
};


/** ⭐ LE CABLAGE, A LA PIGMENTS. Une source, autant de destinations qu'on veut.

    Le tableau a huit lignes « source / destination / couche / dose » disait
    tout et ne montrait rien : pour savoir ce qui bougeait, il fallait lire huit
    lignes et les recouper de tete. Ici on voit d'un coup d'oeil QUI va OU et
    COMBIEN, parce que c'est dessine au lieu d'etre ecrit.

    Le geste est celui de Pigments : on choisit une source en haut, puis on
    clique la destination. Une meme source peut alimenter toutes les
    destinations - c'est tout l'interet, et c'etait deja vrai dans le moteur,
    simplement invisible.

    /!\ IL Y A SEPT DESTINATIONS, PAS « N'IMPORTE QUEL BOUTON ». Pigments module
    tout parce que son moteur est cable pour ca ; ici les destinations sont un
    enum lu par la voix. Faire semblant d'offrir plus serait un mensonge
    d'interface - on montre exactement ce qui existe.

    /!\ Et le nombre de CABLES reste borne a `kNbRoutages`. Quand ils sont tous
    pris, le panneau le dit au lieu d'avaler le clic en silence.
*/
class Cablage : public juce::Component, private juce::Timer
{
public:
    explicit Cablage (RomanceProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Hauteur naturelle du panneau, pour que l'appelant n'ait pas a la deviner. */
    static int hauteurVoulue() noexcept;

    /** ⭐ PUBLIQUE, parce qu'une source doit avoir UNE couleur partout ou elle
        apparait. La bandelette de jeu la lit pour peindre BEND, MOLETTE et
        PRESSION des memes teintes que leurs pastilles dans la matrice - deux
        tables de couleurs finiraient par diverger. */
    static juce::Colour couleurSource (int src);

private:
    void timerCallback() override;

    juce::Rectangle<int> rectLigne (int i) const;
    int ligneSous (juce::Point<int> p) const;        // 0..kNbRoutages-1, ou -1
    /** La source armee, lue dans l'editeur - le panneau n'en possede plus. */
    int armee() const;
    void parentHierarchyChanged() override;
    void ouvreMenuDestination (int ligne);

    int lis (const juce::String& id) const;
    void ecrit (const juce::String& id, float valeur);

    RomanceProcessor& proc_;
    /** /!\ NON POSSEDANT, et trouve en remontant la hierarchie. Le panneau ne
        DECIDE plus quelle source est armee : il le DEMANDE. */
    class RomanceEditor* edit_ = nullptr;
    int cableSaisi_ = -1;
    float doseDepart_ = 0.0f;
    int xDepart_ = 0;
    juce::String message_;
};

/** La page MATRICE : les huit emplacements de modulation et leur cablage.

    ⭐ ILS EXISTAIENT DEJA ET ETAIENT TESTES, mais n'etaient atteignables que
    par l'editeur generique de l'hote. C'etait le plus gros ecart restant entre
    ce que le plugin FAIT et ce qu'on peut en FAIRE - un mecanisme inaccessible
    ne vaut pas mieux qu'un mecanisme absent.

    /!\ La MATRICE et les EMPLACEMENTS sont deux choses distinctes, et les
    melanger est l'erreur classique : un emplacement est une SOURCE (sa nature,
    sa vitesse, sa forme), une ligne de matrice est un CABLAGE (quelle source,
    vers quoi, sur quelle couche, combien). Une meme source peut alimenter
    plusieurs destinations, c'est tout l'interet.
*/
class PageMatrice : public juce::Component
{
public:
    explicit PageMatrice (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::OwnedArray<Groupe> emplacements_;
    juce::OwnedArray<Groupe> routages_;
    // /!\ Huit emplacements a 84 pixels ne tiennent pas dans la fenetre : ils
    // defilent. Retrecir les lignes rendrait les boutons invisibles - c'est
    // exactement le defaut qu'on vient de corriger.
    juce::Viewport vueG_, vueD_;
    juce::Component contG_, contD_;
    /** ⭐ Le cablage dessine, en haut : c'est lui qu'on regarde. Le tableau
        au-dessous reste pour les reglages fins (la couche, la dose au
        millieme) - montrer ne dispense pas de pouvoir ecrire. */
    std::unique_ptr<Cablage> cablage_;
};

/** Une vignette : un noeud de la lignee, cliquable. */
class Vignette : public juce::Component
{
public:
    std::function<void (int, int)> surClic;   // (generation, enfant)
    int gen = 0, idx = 0;
    double perte = 0.0;
    bool retenu = false, choisi = false;

    /** ⭐ CE QU'ON MONTRE MAINTENANT : la forme du son, pas son score.
        « 4,70 dB » ne se compare a rien pour l'oreille - l'utilisateur l'a dit,
        « on ne comprend pas les nombres ». Une percussion, une nappe et une
        cloche se distinguent d'un coup d'oeil a leur enveloppe.

        /!\ COPIE PAR VALEUR, ET C'EST TOUT L'OBJET DE CE COMMENTAIRE. La
        premiere version gardait un POINTEUR vers `Instantane::lignee`. Or le
        minuteur de la page recopie l'instantane douze fois par seconde : chaque
        recopie libere les vecteurs precedents, et les vignettes se retrouvaient
        a lire de la memoire rendue. Ca ne plantait pas toujours - ca plantait
        quand le tas avait ete reutilise, c'est-a-dire pendant une recherche.
        C'est ce qui faisait tomber le plugin ET l'hote avec lui.

        28 flottants par vignette, 384 vignettes au maximum : 43 ko. Le prix de
        la copie est risible compare a celui d'un pointeur qui survit a sa
        cible. */
    float apercu[romance::kApercu] = {};
    bool aApercu = false;
    int moteur = 0;            // 0 ROM, 1 analogique, 2 FM, 3 table
    double pireDeLaGen = 1.0;  // pour situer la qualite DANS sa generation

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override
    { if (surClic) surClic (gen, idx); }
};

/** ⭐ CE QUE GENOPATCH EST EN TRAIN DE FAIRE, dessine.

    /!\ CE N'EST PAS UN HABILLAGE. Avant le vote de moteur, la recherche passe
    par l'analyse, le vote d'octave et le vote de moteur - et l'interface ne
    montrait RIEN pendant ce temps. « La resynthese est tres lente a demarrer »
    etait donc pour moitie un probleme d'affichage : le travail avait commence,
    rien ne le disait.

    /!\ ET CE QU'ON MONTRE EST VRAI. Une barre qui avance toute seule pendant
    qu'on attend est un mensonge poli ; ici chaque etape s'allume quand elle
    commence et affiche ce qu'elle a TROUVE - la hauteur retenue, la perte de
    chaque moteur essaye. On explique l'attente au lieu de la masquer.
*/
class Deroule : public juce::Component
{
public:
    void paint (juce::Graphics&) override;

    romance::PhaseRecherche phase = romance::PhaseRecherche::Repos;
    int    moteurEssaye = -1;
    double perteMoteur[4] = { 1e9, 1e9, 1e9, 1e9 };
    double f0 = 0.0;
    double progression = 0.0;
    /** Avance a chaque rafraichissement : sert aux pointilles qui defilent. */
    int    tic = 0;

    static int hauteurVoulue() noexcept { return 84; }
};

/** La page RESYNTHESE - Genopatch.

    ⭐ ELLE MONTRE LA LIGNEE DE LA RECHERCHE, pas un resultat. Chaque ligne est
    une generation : le parent a gauche, ses enfants a droite. On clique un
    noeud pour l'entendre sur la couche choisie, et on repart de lui - MEME
    s'il n'est pas le meilleur.

    /!\ « L'echec sera musical » : c'est exactement ce geste-la. Un outil qui
    n'offrirait que la meilleure branche serait un bouton, pas un instrument.
*/
class PageResynthese : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer,
                       private juce::Button::Listener
{
public:
    explicit PageResynthese (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override
    { survol_ = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override
    { survol_ = false; repaint(); }

private:
    void timerCallback() override;
    void buttonClicked (juce::Button*) override;
    void reconstruitArbre (const romance::Instantane&);
    void poseNoeud (int gen, int idx);

    RomanceProcessor& proc_;
    juce::TextButton bLance_ { "CHERCHER" }, bStop_ { "ARRETER" },
                     bAffine_ { "AFFINER CE NOEUD" }, bPose_ { "POSER SUR LA COUCHE" };
    juce::ComboBox coucheCible_, budget_;
    /** ⭐⭐ LES QUATRE GAGNANTS - un par generateur.

        /!\ ILS REMPLACENT L'ARBRE COMME VUE PRINCIPALE. Les 48 generations
        d'une lignee ne sont pas un choix : chacune est l'ancetre de la
        suivante, et la derniere est par construction la meilleure de toutes.
        Ici, quatre sons authentiquement differents. L'arbre reste accessible
        derriere « detail » - il sert en mise au point, pas au quotidien. */
    juce::OwnedArray<juce::TextButton> cartes_;
    juce::TextButton bDetail_ { "detail" };

    /** ⭐⭐ L'ETAT DU RESEAU, ECRIT EN CLAIR SUR LA PAGE.

        /!\ UN REPLI SILENCIEUX EST LE PIRE DES DEUX MONDES - c'est le
        piege 40, mot pour mot : « une valeur de repli imprimee au milieu d'un
        rapport n'est pas un avertissement, c'est du texte que personne ne
        lit ». Ici elle est A COTE DU BOUTON qui lance la recherche, et elle
        distingue « absent » de « perime » - deux gestes opposes de la part de
        l'utilisateur : aller le chercher, ou le reentrainer. */
    juce::Label etatReseau_;

    juce::OwnedArray<Vignette> vignettes_;
    Deroule deroule_;
    juce::Viewport vue_;
    juce::Component arbre_;
    romance::Instantane snap_;
    int genChoisi_ = -1, idxChoisi_ = -1;
    bool survol_ = false;
    int nGensAffichees_ = 0;
};

/** La page ATELIER : une couche developpee a la fois, plus la machine. */
class PageAtelier : public juce::Component,
                    private juce::Button::Listener,
                    private juce::Timer
{
public:
    explicit PageAtelier (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void buttonClicked (juce::Button*) override;
    void construitCouche (int c);
    /** ⭐ N'AFFICHER QUE LE GENERATEUR ACTIF. Montrer les oscillateurs
        analogiques, les operateurs FM ET la lecture de ROM en meme temps, c'est
        afficher deux blocs qui ne font rien - et c'est de la place qu'on ne
        pourra pas donner aux moteurs suivants. */
    void majMoteurVisible();
    void majMorphVisible();
    void majSpectreVisible();
    void timerCallback() override
    { majMoteurVisible(); majMorphVisible(); majSpectreVisible(); }

    RomanceProcessor& proc_;
    juce::OwnedArray<juce::TextButton> onglets_;
    juce::TextButton bCopie_ { "COPIER" }, bColle_ { "COLLER" };
    juce::OwnedArray<Groupe> groupes_;
    std::unique_ptr<Onde> onde_;
    /** La grille dessinable, visible seulement sur le moteur a table d'ondes. */
    std::unique_ptr<Grille> grille_;
    static constexpr int kHautGrille = 130;
    /** Les bandes du spectre, visibles quand la source est « Spectre dessine ». */
    std::unique_ptr<BandesSpectre> spectre_;
    static constexpr int kHautSpectre = 130;
    /** La courbe du filtre, ouverte par le bouton COURBE du groupe FILTRE. */
    std::unique_ptr<CourbeFiltre> courbe_;
    /** Le groupe FILTRE et son bouton `morph`, pour masquer celui-ci hors
        topologie Z-Plane. Non possedants : le groupe appartient a `groupes_`. */
    Groupe* filtre_ = nullptr;
    juce::Component* morph_ = nullptr;
    juce::TextButton bCourbe_ { "COURBE" };
    static constexpr int kHautCourbe = 150;
    /** ⭐ Le meme panneau de cablage qu'en MATRICE, ici aussi : « accessible
        dans l'onglet atelier ». Deux instances independantes qui lisent les
        memes parametres - il n'y a rien a synchroniser. */
    std::unique_ptr<Cablage> cablage_;
    juce::Viewport vue_;
    juce::Component contenu_;
    int coucheVisible_ = 0;
    int moteurAffiche_ = -1;
};

class RomanceEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Button::Listener
{
public:
    explicit RomanceEditor (RomanceProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

    // ------------------------------------------------- le cablage par le geste

    /** Un cable qui arrive sur un bouton : sa source, et l'excursion qu'il
        provoque, EN COORDONNEES NORMALISEES du parametre vise. */
    struct Arrivee { int source; float a, b; };

    /** ⭐⭐ QUELS CABLES ARRIVENT SUR CE BOUTON, et jusqu'ou ils le poussent.

        /!\ L'EXCURSION EST CALCULEE DANS L'UNITE DU PARAMETRE PUIS RENORMALISEE
        par sa propre `NormalisableRange`. C'est ce qui rend l'anneau juste sur
        les reglages LOGARITHMIQUES : la coupure va de 0,5 a 250, une octave
        vers le haut n'y occupe pas la meme largeur selon l'endroit ou l'on
        est. Dessiner une longueur proportionnelle a la dose aurait ete faux
        partout sauf sur les plages lineaires. */
    int cablesDe (const juce::String& idComplet, Arrivee* sortie, int maxi) const;

    /** Ce bouton designe-t-il une destination ? Un depot ne doit etre accepte
        que si `brancheSur` saura l'honorer - accepter puis ne rien faire est
        pire que refuser. */
    bool peutRecevoir (const juce::String& idComplet) const;

    /** Cree un cable de `source` vers le parametre `idComplet`. Rend faux si
        les seize lignes sont prises. */
    bool brancheSur (const juce::String& idComplet, int source);

    /** ⭐ Change a chaque modification du cablage. Un bouton compare ce nombre
        au sien et ne se redessine que s'il a bouge - sinon cent boutons se
        repeindraient huit fois par seconde pour rien. */
    juce::int64 signatureCablage() const;

    /** La source que l'on survole dans la bande, ou 0. Les boutons qu'elle
        atteint s'allument. */
    int sourceSurvolee() const noexcept { return sourceSurvolee_; }
    void poseSourceSurvolee (int s) { sourceSurvolee_ = s; }

    /** ⭐⭐ LA SOURCE ARMEE - UNE SEULE, PARTAGEE PAR TOUTE LA FENETRE.

        /!\ IL Y EN AVAIT DEUX, ET CA SE VOYAIT : la bande du bas (celle qu'on
        attrape pour glisser) et une rangee de pastilles PROPRE au panneau de
        cablage, presente donc en double sur ATELIER et sur MATRICE. Deux
        selecteurs pour une seule notion, qui pouvaient de surcroit ne pas
        designer la meme source.

        La bande du bas est desormais le seul selecteur ; le panneau lit ce
        qu'elle arme. */
    int sourceArmee() const noexcept { return sourceArmee_; }
    void poseSourceArmee (int s) { sourceArmee_ = s; }

private:
    int sourceSurvolee_ = 0;
    int sourceArmee_ = 1;
    std::unique_ptr<BandeSources> bande_;
    void buttonClicked (juce::Button*) override;

    /** ⭐⭐ LA BARRE DE PRESETS.

        /!\ UN PRESET N'EST PAS UN REGLAGE DE PLUS, c'est l'etat ENTIER - les
        parametres, les quatre recettes, les quatre dessins, les quatre
        spectres. Il passe donc par `getStateInformation`, le meme chemin que
        l'hote utilise pour enregistrer le projet. Ecrire un format a part
        aurait garanti qu'un jour l'un des deux oublie quelque chose que
        l'autre sauve.

        /!\ LES FLECHES PARCOURENT LE DOSSIER, pas une liste en memoire. Un
        fichier ajoute a la main - copie depuis une autre machine, recu dans un
        message - doit apparaitre sans qu'on relance quoi que ce soit. */
    void chargePreset (const juce::File&);
    void voisinPreset (int direction);
    void fichierPreset (bool enregistrer);
    std::unique_ptr<juce::FileChooser> selecteur_;
    juce::Label nomPreset_;
    juce::TextButton bPrec_ { "<" }, bSuiv_ { ">" },
                     bEnr_ { "ENREGISTRER" }, bOuvre_ { "OUVRIR" };
    juce::File presetCourant_;

    RomanceProcessor& proc_;
    juce::TextButton bJouer_ { "JOUER" }, bAtelier_ { "ATELIER" },
                     bMatrice_ { "MATRICE" }, bFx_ { "FX" },
                     bResynth_ { "RESYNTHESE" };
    PageJouer   jouer_;
    PageAtelier atelier_;
    PageMatrice matrice_;
    PageFx      fx_;
    PageResynthese resynth_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RomanceEditor)
};
