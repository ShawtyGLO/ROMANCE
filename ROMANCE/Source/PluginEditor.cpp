#include "PluginEditor.h"
#include "Bibliotheque.h"
#include <vector>

namespace { juce::Colour kMuted() { return facade::kTexte.withAlpha (0.6f); } }

using namespace facade;
using namespace romance;

// ------------------------------------------------------------------ Bouton

Bouton::Bouton (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                const juce::String& nom, juce::Colour teinte)
    : teinte_ (teinte)
{
    curseur_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // /!\ La valeur n'apparait QUE pendant qu'on tourne. Retour de
    // l'utilisateur : « beaucoup d'info parfois superflus ».
    curseur_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    curseur_.setPopupDisplayEnabled (true, true, this);
    curseur_.setColour (juce::Slider::textBoxTextColourId, kTexteFort);
    curseur_.setColour (juce::Slider::rotarySliderFillColourId, teinte);
    curseur_.setColour (juce::Slider::rotarySliderOutlineColourId, kTrait);
    curseur_.setColour (juce::Slider::thumbColourId, kTexteFort);
    addAndMakeVisible (curseur_);

    nom_.setText (nom, juce::dontSendNotification);
    nom_.setJustificationType (juce::Justification::centred);
    nom_.setColour (juce::Label::textColourId, kTexte);
    nom_.setFont (juce::Font (11.0f));
    addAndMakeVisible (nom_);

    id_ = id;
    lien_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                (s, id, curseur_);
    // /!\ HUIT FOIS PAR SECONDE, ET SEULEMENT POUR COMPARER UNE SIGNATURE. Le
    // cablage ne bouge que quand on le change ; ce minuteur ne repeint donc
    // presque jamais. C'est ce qui rend l'anneau gratuit sur cent boutons.
    startTimerHz (8);
}

void Bouton::timerCallback()
{
    if (editeur() == nullptr) return;
    // ⭐⭐ PENDANT UN GLISSER, ON MONTRE OU L'ON PEUT LACHER.
    //
    // /!\ SANS CA LE GESTE EST INDEVINABLE. Retour de l'utilisateur : « on
    // peut glisser mais deposer sur un controle ne marche pas ». Or beaucoup
    // de boutons ne SONT pas des destinations - les reglages d'un modulateur,
    // la dose d'un cable, la source d'un lien. Lacher dessus ne fait rien, et
    // rien ne distinguait ce refus d'une panne. Un refus silencieux se lit
    // comme un bug : c'est le piege 40 applique a un geste.
    const bool cible = edit_->isDragAndDropActive() && edit_->peutRecevoir (id_);
    if (cible != cibleDrag_) { cibleDrag_ = cible; repaint(); }
    const auto sig = edit_->signatureCablage();
    // ⭐ La source survolee entre AUSSI dans la comparaison : sans elle, passer
    // la souris sur une pastille n'allumerait rien tant que le cablage n'a pas
    // change.
    const juce::int64 total = sig * 31 + edit_->sourceSurvolee();
    if (total == signature_) return;
    signature_ = total;
    relitCables();
    repaint();
}

/** ⭐⭐ QUELS CABLES ARRIVENT ICI. Tout le mecanisme tient dans une question
    que le code ne savait pas poser jusqu'a aujourd'hui : ce bouton-la, qui le
    pousse et jusqu'ou ? */
void Bouton::relitCables()
{
    cables_.clearQuick();
    if (edit_ == nullptr) return;
    RomanceEditor::Arrivee tmp[romance::kNbRoutages];
    const int n = edit_->cablesDe (id_, tmp, romance::kNbRoutages);
    for (int i = 0; i < n; ++i)
        cables_.add ({ tmp[i].source, tmp[i].a, tmp[i].b });
}

/** ⭐ ON RESOUT L'EDITEUR A LA DEMANDE, pas seulement dans
    `parentHierarchyChanged`.

    /!\ UN BOUTON PEUT NAITRE AVANT D'ETRE RATTACHE. Ceux du rack FX sont
    construits dans `construitRack()` et ajoutes a un `Groupe` qui n'est pas
    encore dans la fenetre : a cet instant `findParentComponentOfClass` rend
    nul. JUCE previent bien les descendants quand un ancetre change de parent,
    mais faire dependre TOUT le glisser-deposer de cet ordre-la, c'est se
    preparer un « ca ne marche que sur certaines pages » impossible a lire. */
RomanceEditor* Bouton::editeur()
{
    if (edit_ == nullptr) edit_ = findParentComponentOfClass<RomanceEditor>();
    return edit_;
}

bool Bouton::isInterestedInDragSource (const SourceDetails& d)
{
    // /!\ ON REFUSE CE QU'ON NE SAIT PAS BRANCHER, et on le refuse AVANT que
    // le curseur n'arrive : un bouton qui accepte un depot puis ne fait rien
    // est pire qu'un bouton qui n'accepte pas - l'utilisateur croit avoir
    // cable et cherche ensuite pourquoi rien ne bouge.
    if (! d.description.toString().startsWith ("src:")) return false;
    // /!\ ON POSE LA QUESTION A L'EDITEUR PLUTOT QUE DE REFAIRE LE DECOUPAGE.
    // Ma premiere version le refaisait ici a coups de `fromFirstOccurrenceOf` :
    // deux chemins pour une meme decision, donc la garantie qu'un jour l'un
    // accepte un depot que l'autre refuse - et l'utilisateur croit avoir cable.
    auto* e = editeur();
    return e != nullptr && e->peutRecevoir (id_);
}

void Bouton::itemDropped (const SourceDetails& d)
{
    survole_ = false;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false)
                        .getIntValue();
    if (auto* e = editeur()) if (src > 0) e->brancheSur (id_, src);
    signature_ = -1;      // force la relecture au prochain tour
    repaint();
}

void Bouton::resized()
{
    auto r = getLocalBounds();
    nom_.setBounds (r.removeFromBottom (13));
    // ⭐ CARRE ET CENTRE. Un bouton rond de JUCE prend le plus petit cote de sa
    // case : dans une case de 90 x 26, il faisait 26 pixels et se voyait a
    // peine. On lui donne un carre, et on le centre.
    const int c = juce::jmax (16, juce::jmin (r.getWidth(), r.getHeight()) - 2);
    curseur_.setBounds (r.withSizeKeepingCentre (c, c));
}

void Bouton::parentHierarchyChanged()
{
    // la bulle de valeur s'affiche dans le plus haut parent, sinon elle est
    // rognee par le bouton et le nombre devient illisible
    if (auto* haut = getTopLevelComponent())
        curseur_.setPopupDisplayEnabled (true, true, haut);
    // ⭐ On REMONTE jusqu'a l'editeur au lieu de se le faire passer. Cent
    // appels de `new Bouton` n'ont pas eu a changer.
    edit_ = findParentComponentOfClass<RomanceEditor>();
    signature_ = -1;
}

/** ⭐⭐ L'ANNEAU DE MODULATION.

    La question qu'on se pose devant un synthe n'est pas « quels cables
    existent ? » mais « pourquoi ce bouton bouge ? ». La liste de la page
    MATRICE repondait a la premiere ; l'anneau repond a la seconde, et il y
    repond SANS CHANGER DE PAGE.

    /!\ IL DESSINE L'EXCURSION, PAS LA DOSE. Une dose de 0,3 sur la coupure
    vaut 1,2 octave, la meme sur le panoramique vaut 0,3 : des arcs de meme
    longueur mentiraient sur ce qui compte. Les bornes arrivent deja
    normalisees par la plage du parametre - voir `RomanceEditor::cablesDe`.

    /!\ ET IL SE DESSINE HORS DU CURSEUR, sur un rayon plus grand. Par-dessus,
    il masquerait l'aiguille - donc la valeur qu'on est en train de regler. */
void Bouton::paint (juce::Graphics& g)
{
    if (survole_)
    {
        g.setColour (kAccent.withAlpha (0.30f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }
    else if (cibleDrag_)
    {
        // ⭐ Un liserre, pas un aplat : on doit encore lire la valeur du bouton
        // pendant qu'on vise.
        g.setColour (kAccent.withAlpha (0.55f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.4f);
    }
    if (cables_.isEmpty()) return;

    const auto zone = curseur_.getBounds().toFloat();
    const float cx = zone.getCentreX(), cy = zone.getCentreY();
    const float r0 = juce::jmin (zone.getWidth(), zone.getHeight()) * 0.5f;
    const auto rot = curseur_.getRotaryParameters();
    const float a0 = rot.startAngleRadians, a1 = rot.endAngleRadians;

    const int survolee = (edit_ != nullptr) ? edit_->sourceSurvolee() : 0;
    for (int i = 0; i < cables_.size(); ++i)
    {
        const auto& c = cables_.getReference (i);
        // /!\ UN ANNEAU PAR CABLE, EMPILE VERS L'EXTERIEUR. Les superposer
        // ferait qu'un bouton pousse par trois sources n'en montrerait qu'une.
        const float ray = r0 + 2.5f + 3.0f * (float) i;
        if (ray > juce::jmin (getWidth(), getHeight()) * 0.5f) break;

        const float ang0 = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, c.a);
        const float ang1 = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, c.b);
        juce::Path p;
        p.addCentredArc (cx, cy, ray, ray, 0.0f,
                         juce::jmin (ang0, ang1), juce::jmax (ang0, ang1), true);
        const auto teinte = Cablage::couleurSource (c.source);
        // ⭐ Survoler une source ETEINT les autres au lieu d'allumer la sienne :
        // c'est le contraste qui rend la reponse lisible, pas la luminosite.
        const float alpha = (survolee == 0) ? 0.85f
                          : (survolee == c.source ? 1.0f : 0.15f);
        g.setColour (teinte.withAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (2.2f));
    }
}

// ------------------------------------------------------------------ Bascule

Bascule::Bascule (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                  const juce::String& nom)
{
    bouton_.setButtonText (nom);
    bouton_.setColour (juce::ToggleButton::textColourId, kTexte);
    bouton_.setColour (juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible (bouton_);
    lien_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                (s, id, bouton_);
}

void Bascule::resized() { bouton_.setBounds (getLocalBounds()); }

// ------------------------------------------------------------------ Menu

Menu::Menu (juce::AudioProcessorValueTreeState& s, const juce::String& id,
            const juce::String& nom)
{
    boite_.setColour (juce::ComboBox::backgroundColourId, kFond);
    boite_.setColour (juce::ComboBox::textColourId, kTexteFort);
    boite_.setColour (juce::ComboBox::outlineColourId, kTrait);

    // /!\ IL FAUT REMPLIR LA LISTE SOI-MEME. `ComboBoxAttachment` relie une
    // valeur, il ne cree pas les entrees : sans ces trois lignes tous les menus
    // s'affichent VIDES, et le lien fonctionne quand meme - donc rien ne
    // signale la faute a la compilation. Vu a la capture d'ecran, pas au test.
    if (auto* par = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        boite_.addItemList (par->choices, 1);
    jassert (boite_.getNumItems() > 0);
    addAndMakeVisible (boite_);

    nom_.setText (nom, juce::dontSendNotification);
    nom_.setColour (juce::Label::textColourId, kTexte);
    nom_.setFont (juce::Font (11.0f));
    addAndMakeVisible (nom_);

    lien_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                (s, id, boite_);
}

void Menu::resized()
{
    auto r = getLocalBounds();
    nom_.setBounds (r.removeFromTop (13));
    // /!\ Hauteur bornee : sans ca le menu s'etire sur toute la case et ecrase
    // visuellement les boutons ronds d'a cote.
    boite_.setBounds (r.removeFromTop (juce::jmin (26, r.getHeight())).reduced (1, 0));
}

// ------------------------------------------------------------------ Groupe

Groupe::Groupe (const juce::String& titre, juce::Colour teinte)
    : titre_ (titre), teinte_ (teinte) {}

void Groupe::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kPanneau);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (kTrait);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (teinte_);
    g.fillRoundedRectangle (r.getX() + 8.0f, r.getY() + 9.0f, 3.0f, 10.0f, 1.5f);
    g.setColour (kTexteFort);
    g.setFont (juce::Font (11.5f, juce::Font::bold));
    g.drawText (titre_, (int) r.getX() + 17, (int) r.getY() + 6,
                (int) r.getWidth() - 20, 16, juce::Justification::centredLeft);
}

void Groupe::reglePages (const juce::StringArray& etiquettes)
{
    onglets_.clear();
    for (int i = 0; i < etiquettes.size(); ++i)
    {
        auto* b = onglets_.add (new juce::TextButton (etiquettes[i]));
        b->setColour (juce::TextButton::buttonColourId, kFond);
        b->setColour (juce::TextButton::buttonOnColourId, teinte_.withAlpha (0.45f));
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->setColour (juce::TextButton::textColourOnId, kTexteFort);
        b->setClickingTogglesState (false);
        b->setToggleState (i == pageActive_, juce::dontSendNotification);
        b->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                            | (i < etiquettes.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        b->addListener (this);
        addAndMakeVisible (b);
    }
    resized();
}

void Groupe::masque (juce::Component* c, bool cache)
{
    const bool present = masques_.contains (c);
    if (cache == present) return;          // rien a faire, et rien a redessiner
    if (cache) masques_.add (c); else masques_.removeAllInstancesOf (c);
    resized();
}

void Groupe::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown() && surClicDroit) surClicDroit();
}

void Groupe::mouseUp (const juce::MouseEvent& e)
{
    // /!\ ON N'AGIT QUE SI LA SOURIS A VRAIMENT VOYAGE. Sans ce seuil, un
    // simple clic sur le fond d'un groupe - pour lui donner le focus, ou par
    // maladresse - reordonnerait la chaine.
    if (! e.mods.isRightButtonDown() && surGlisse
        && std::abs (e.getDistanceFromDragStartY()) > 12)
        surGlisse (e.getDistanceFromDragStartY());
}

void Groupe::buttonClicked (juce::Button* b)
{
    for (int i = 0; i < onglets_.size(); ++i)
        if (onglets_[i] == b && i != pageActive_)
        {
            pageActive_ = i;
            for (int j = 0; j < onglets_.size(); ++j)
                onglets_[j]->setToggleState (j == i, juce::dontSendNotification);
            resized();
            return;
        }
}

int Groupe::nbRangees() const
{
    const int cols = juce::jmax (1, colonnes);
    int col = 0, ligne = 0, n = 0;
    for (int i = 0; i < contenu_.size(); ++i)
    {
        if (! ((pages_[i] < 0 || pages_[i] == pageActive_)
               && ! masques_.contains (contenu_[i]))) continue;
        // /!\ UN MENU PREND DEUX CASES - la meme regle que dans `resized()`,
        // et c'est pour ca qu'elle est ici plutot que recopiee la-bas.
        const int larg = (dynamic_cast<Menu*> (contenu_[i]) != nullptr) ? 2 : 1;
        if (col + larg > cols) { col = 0; ++ligne; }
        col += larg; ++n;
        if (col >= cols) { col = 0; ++ligne; }
    }
    if (n == 0) return 1;
    return juce::jmax (1, ligne + (col > 0 ? 1 : 0));
}

int Groupe::hauteurVoulue() const
{
    // 8 de marge en haut + l'entete + les rangees + 8 de marge en bas.
    return 8 + 18 + nbRangees() * hauteurCase + 8;
}

void Groupe::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto entete = r.removeFromTop (18);

    // /!\ Les onglets vivent DANS la ligne de titre, pas au-dessus de la
    // grille : les ajouter sur leur propre ligne aurait mange la hauteur des
    // boutons, donc refait exactement les commandes minuscules qu'on a
    // corrigees (voir hauteurCase).
    if (! onglets_.isEmpty())
    {
        // /!\ La largeur suit L'ETIQUETTE. Fixee a 26 pixels, elle convenait a
        // « 1 2 3 4 » et tronquait « BAS / HAUT » des le premier essai.
        int lb = 26;
        for (auto* b : onglets_)
            lb = juce::jmax (lb, 12 + 8 * b->getButtonText().length());
        const int h = 15;
        auto bande = entete.removeFromRight (lb * onglets_.size());
        bande = bande.withHeight (h).withY (entete.getY() + 1);
        for (int i = 0; i < onglets_.size(); ++i)
            onglets_[i]->setBounds (bande.removeFromLeft (lb));
    }

    // ⭐ On ne dispose QUE ce qui est visible. Une commande d'une autre page
    // n'occupe aucune case : c'est ce qui garantit que les sept boutons d'un
    // operateur tombent toujours aux sept memes places.
    std::vector<juce::Component*> vus;
    for (int i = 0; i < contenu_.size(); ++i)
    {
        const bool visible = (pages_[i] < 0 || pages_[i] == pageActive_)
                          && ! masques_.contains (contenu_[i]);
        contenu_[i]->setVisible (visible);
        if (visible) vus.push_back (contenu_[i]);
    }

    const int n = (int) vus.size();
    if (n == 0) return;
    const int cols = juce::jmax (1, colonnes);
    const int lw = r.getWidth() / cols;

    // /!\ UN MENU PREND DEUX CASES. Vu a la capture : « Recette (108 octets) »
    // et « 4 poles - 24 dB/oct » s'affichaient tronques en « Rece... » et
    // « 4 pol... ». Un bouton rond se contente d'une case carree, un menu a
    // besoin de lire son contenu - sinon il ne sert a rien.
    int col = 0, ligne = 0;
    std::vector<juce::Rectangle<int>> places;
    for (int i = 0; i < n; ++i)
    {
        const int larg = (dynamic_cast<Menu*> (vus[(size_t) i]) != nullptr) ? 2 : 1;
        if (col + larg > cols) { col = 0; ++ligne; }
        places.push_back ({ col, ligne, larg, 1 });
        col += larg;
        if (col >= cols) { col = 0; ++ligne; }
    }
    // ⭐ La MEME source que `hauteurVoulue()` : si les deux comptaient les
    // rangees chacun de leur cote, la page finirait par reserver une hauteur
    // qui ne correspond plus a ce qu'on y met.
    const int nl = nbRangees();
    (void) ligne;
    // /!\ On respecte `hauteurCase` meme si le groupe est plus petit : mieux
    // vaut deborder legerement que rendre les boutons invisibles. C'est le
    // groupe qui doit s'adapter, pas les commandes qui doivent disparaitre.
    const int lh = juce::jmax (hauteurCase, r.getHeight() / nl);
    for (int i = 0; i < n; ++i)
        vus[(size_t) i]->setBounds (r.getX() + places[(size_t) i].getX() * lw,
                                r.getY() + places[(size_t) i].getY() * lh,
                                places[(size_t) i].getWidth() * lw - 4, lh - 4);
}

// ------------------------------------------------------------------ JOUER

PageJouer::PageJouer (RomanceProcessor& p) : proc_ (&p)
{
    auto& s = p.params;

    // ⭐⭐ LE DE. Il ne tire pas au hasard dans les parametres - ce serait un
    // generateur de bruit. Il utilise `tireProfil`, le prior calibre sur les
    // quantiles mesures de 1 665 sons du corpus. On avait construit cet objet
    // pour entrainer le reseau et on ne s'en servait pas pour jouer.
    bDe_.addListener (this);
    bDe_.setColour (juce::TextButton::buttonColourId, kAccent.withAlpha (0.35f));
    bDe_.setColour (juce::TextButton::textColourOffId, kTexteFort);
    addAndMakeVisible (bDe_);
    coucheDe_.addItemList ({ "couche A", "couche B", "couche C", "couche D" }, 1);
    coucheDe_.setSelectedId (1, juce::dontSendNotification);
    coucheDe_.setColour (juce::ComboBox::backgroundColourId, kFond);
    coucheDe_.setColour (juce::ComboBox::textColourId, kTexteFort);
    addAndMakeVisible (coucheDe_);

    charge_.setFont (juce::Font (juce::FontOptions (10.5f)));
    charge_.setColour (juce::Label::textColourId, kTexte);
    charge_.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (charge_);
    startTimerHz (4);

    // ⭐ Les trois macros en grand, comme la partie haute de Pure LoFi.
    // Chacune tire sur un mecanisme MESURE et DISJOINT des deux autres :
    // le convertisseur, l'horloge, l'instabilite.
    // /!\ USURE N'EST PLUS EN FACADE. Mesure : 0,83 dB d'ecart entre ses deux
    // extremes, contre 1,89 pour GRAIN - et surtout, elle ne fait que CHOISIR
    // LE CONVERTISSEUR, ce qui est une propriete de la machine qu'on imite,
    // pas un geste qu'on repete en jouant. Sa place est dans la resynthese
    // (quelle machine reproduit-on ?) et dans l'atelier (parametre `quant`),
    // pas au premier plan. Le parametre `mac_usure` existe toujours : les
    // presets qui s'en servent ne cassent pas.
    gros_.add (new Bouton (s, "mac_grain", "GRAIN", kAccent2));
    gros_.add (new Bouton (s, "mac_mouv",  "MOUVEMENT", kAccent3));
    for (auto* b : gros_) addAndMakeVisible (b);

    // /!\ Une legende PAR bouton, pas une phrase centree. Ma premiere version
    // dessinait « le convertisseur   l'horloge   l'instabilite » en une seule
    // chaine : a la capture, elle chevauchait les noms des boutons. Trois
    // etiquettes posees sous leur bouton ne peuvent pas se decaler.
    for (const char* t : { "l'horloge", "l'instabilite" })
    {
        auto* l = new juce::Label ({}, t);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, kTexte.withAlpha (0.65f));
        l->setFont (juce::Font (11.0f));
        legendes_.add (l);
        addAndMakeVisible (l);
    }

    machine_ = std::make_unique<Groupe> ("MACHINE", kAccent2);
    machine_->colonnes = 5;   // le menu en prend 2, plus trois boutons
    machine_->ajoute (new Menu (s, "mode", "voix"));
    machine_->ajoute (new Bouton (s, "accordage", "accord", kAccent2));
    machine_->ajoute (new Bouton (s, "cho_mix", "chorus", kAccent2));
    machine_->ajoute (new Bouton (s, "rev_mix", "reverbe", kAccent2));
    addAndMakeVisible (machine_.get());

    // ⭐⭐ LE MENU DE MACHINE, en haut de la page ou l'on joue. C'est le
    // reglage qui decide de la SAVEUR - et « Propre » y est une position de
    // plein droit : l'instrument sans aucun caractere de rompleur.
    machineG_ = std::make_unique<Groupe> ("MACHINE (saveur du convertisseur)", kAccent);
    machineG_->colonnes = 2;
    machineG_->ajoute (new Menu (s, "machine", "convertisseur"));
    addAndMakeVisible (machineG_.get());

    // ⭐ LES QUATRE MACROS, comme chez Pigments : sur la page ou l'on joue,
    // parce que c'est en jouant qu'on s'en sert. Elles ne font rien tant qu'un
    // cable ne part pas d'elles (page MATRICE ou bas de l'ATELIER).
    macros_ = std::make_unique<Groupe> ("MACROS (a cabler dans la matrice)", kAccent3);
    macros_->colonnes = 4;
    for (int i = 0; i < romance::kNbMacros; ++i)
        macros_->ajoute (new Bouton (s, "macro" + juce::String (i + 1),
                                     "macro " + juce::String (i + 1), kAccent3));
    addAndMakeVisible (macros_.get());

    // ⭐⭐ LE GLIDE EST UN GESTE DE JEU, donc il est ici et pas dans l'atelier.
    // Et la portee du bend l'accompagne : les deux repondent a la meme
    // question - comment le clavier parle a l'instrument.
    jeu_ = std::make_unique<Groupe> ("JEU", kAccent2);
    jeu_->colonnes = 2;
    jeu_->ajoute (new Bouton (s, "glide", "glide", kAccent2));
    jeu_->ajoute (new Bouton (s, "bend_portee", "bend +/-", kAccent2));
    addAndMakeVisible (jeu_.get());

    bande_ = std::make_unique<Bandelette> (p);
    addAndMakeVisible (bande_.get());

    sortie_ = std::make_unique<Groupe> ("SORTIE", kAccent);
    sortie_->colonnes = 1;
    sortie_->ajoute (new Bouton (s, "sortie", "niveau", kAccent));
    addAndMakeVisible (sortie_.get());
}

void PageJouer::paint (juce::Graphics&) {}

void PageJouer::timerCallback()
{
    if (proc_ == nullptr) return;
    const auto ch = proc_->charge();
    // /!\ On montre AUSSI la part des parametres. Un chiffre global dit qu'il
    // y a un probleme ; la decomposition dit ou il est.
    static const char* kM[] = { "ROM", "ANA", "FM", "TBL" };
    // ⭐ LE COUT PAR VOIX-COUCHE : c'est LUI qui se compare a une mesure. Le
    // pourcentage brut depend du nombre de notes tenues, donc il ne dit rien
    // tout seul.
    const int n = juce::jmax (1, ch.notes * juce::jmax (1, ch.couches));
    charge_.setText ("CPU " + juce::String (ch.total, 1) + " %  ·  "
                       + juce::String (ch.notes) + " voix x "
                       + juce::String (ch.couches) + " couches "
                       + kM[juce::jlimit (0, 3, ch.moteur)] + "  ·  "
                       + juce::String (ch.voix / (float) n, 2) + " %/voix-couche",
                     juce::dontSendNotification);
    charge_.setColour (juce::Label::textColourId,
                       ch.total > 70.0f ? kAccent : kTexte);
}

void PageJouer::buttonClicked (juce::Button* b)
{
    if (b == &bDe_ && proc_ != nullptr)
        proc_->tireAuSort (juce::jlimit (0, romance::kNbCouches - 1,
                                         coucheDe_.getSelectedId() - 1));
}

void PageJouer::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (8);
    auto haut = r.removeFromTop (152);
    auto leg  = r.removeFromTop (16);
    const int w = haut.getWidth() / 3;
    for (int i = 0; i < gros_.size(); ++i)
    {
        gros_[i]->setBounds (haut.getX() + i * w, haut.getY(), w, haut.getHeight());
        legendes_[i]->setBounds (leg.getX() + i * w, leg.getY(), w, leg.getHeight());
    }
    r.removeFromTop (12);
    {
        auto ligne = r.removeFromTop (76);
        auto droite = ligne.removeFromRight (150);
        charge_.setBounds (droite.withWidth (330).withX (droite.getRight() - 330)
                                 .removeFromTop (16));
        bDe_.setBounds (droite.removeFromTop (30).reduced (2));
        droite.removeFromTop (4);
        coucheDe_.setBounds (droite.removeFromTop (24).reduced (2));
        if (machineG_ != nullptr) machineG_->setBounds (ligne);
    }
    r.removeFromTop (8);
    // ⭐ Les quatre macros assignables juste sous les trois grandes : c'est la
    // rangee qu'on attrape en jouant, comme le bandeau de Pigments.
    if (macros_ != nullptr) macros_->setBounds (r.removeFromTop (100));
    r.removeFromTop (10);
    auto bas = r.removeFromTop (108);
    sortie_->setBounds (bas.removeFromRight (104));
    bas.removeFromRight (10);
    if (jeu_ != nullptr) { jeu_->setBounds (bas.removeFromRight (150));
                           bas.removeFromRight (10); }
    machine_->setBounds (bas);
    r.removeFromTop (10);
    if (bande_ != nullptr) bande_->setBounds (r.removeFromTop (54));
}

// -------------------------------------------------------- LA BANDELETTE DE JEU

Bandelette::Bandelette (RomanceProcessor& p) : proc_ (p)
{
    // /!\ DOUZE FOIS PAR SECONDE SUFFIT. Ce n'est pas un afficheur de niveau :
    // on regarde une molette, pas une crete.
    startTimerHz (12);
}

juce::Rectangle<int> Bandelette::rect (int i) const
{
    const int w = juce::jmax (60, (getWidth() - 24) / 3);
    return { 4 + i * (w + 8), 16, w, getHeight() - 20 };
}

void Bandelette::timerCallback()
{
    bool change = false;
    for (int i = 0; i < 3; ++i)
    {
        const float v = proc_.litJeu (i);
        if (std::abs (v - vu_[i]) > 0.002f) { vu_[i] = v; change = true; }
    }
    if (change) repaint();
}

void Bandelette::bouge (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
    {
        const auto b = rect (i);
        if (attrape_ != i && ! b.contains (e.getPosition())) continue;
        attrape_ = i;
        // Position horizontale -> valeur. On borne au rectangle : sortir en
        // glissant ne doit pas relacher la prise, sinon on n'atteint jamais
        // les extremes.
        const double t = juce::jlimit (0.0, 1.0,
                            (double) (e.getPosition().getX() - b.getX())
                                / juce::jmax (1.0, (double) b.getWidth()));
        proc_.poseJeu (i, (float) (i == 0 ? (t * 2.0 - 1.0) : t));
        repaint();
        return;
    }
}

void Bandelette::mouseUp (const juce::MouseEvent&)
{
    // /!\ SEUL LE BEND REVIENT. Une molette de modulation qui retomberait a
    // zero des qu'on la lache serait inutilisable - c'est justement un
    // controleur qu'on POSE.
    if (attrape_ == 0) { proc_.poseJeu (0, 0.0f); repaint(); }
    attrape_ = -1;
}

void Bandelette::paint (juce::Graphics& g)
{
    static const char* kNoms[3] = { "BEND", "MOLETTE", "PRESSION" };
    for (int i = 0; i < 3; ++i)
    {
        const auto b = rect (i).toFloat();
        g.setColour (kFond.brighter (0.08f));
        g.fillRoundedRectangle (b, 3.0f);

        // ⭐ La meme teinte que les pastilles de la matrice : une source doit
        // avoir UNE couleur, partout ou elle apparait.
        const auto teinte = Cablage::couleurSource (romance::kIdSourceBend + i);
        const float v = vu_[i];
        if (i == 0)
        {
            // /!\ LE BEND SE DESSINE DEPUIS LE CENTRE. Une barre partant de la
            // gauche ferait croire qu'un bend au repos est « a moitie ».
            const float cx = b.getCentreX();
            const float x = cx + v * b.getWidth() * 0.5f;
            g.setColour (teinte.withAlpha (0.75f));
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, x),
                                        b.getY() + 2.0f, std::abs (x - cx),
                                        b.getHeight() - 4.0f), 2.0f);
            g.setColour (kTexte.withAlpha (0.35f));
            g.drawLine (cx, b.getY() + 2.0f, cx, b.getBottom() - 2.0f, 1.0f);
        }
        else
        {
            g.setColour (teinte.withAlpha (0.75f));
            g.fillRoundedRectangle (b.getX() + 2.0f, b.getY() + 2.0f,
                                    juce::jmax (0.0f, v * (b.getWidth() - 4.0f)),
                                    b.getHeight() - 4.0f, 2.0f);
        }
        g.setColour (kTexteFort);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (kNoms[i], b.toNearestInt(), juce::Justification::centred);
    }
    g.setColour (kMuted());
    g.setFont (juce::Font (juce::FontOptions (9.5f)));
    g.drawText ("jeu - suit le MIDI, et permet de l'essayer sans clavier adapte",
                getLocalBounds().removeFromTop (14),
                juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ ONDE

// ------------------------------------------------------------ la courbe du filtre

namespace
{
    /** Combien de points on trace. Une bande par pixel serait du gaspillage :
        l'oeil ne lit pas mieux et le calcul est refait a chaque changement. */
    constexpr int kPointsCourbe = 180;
    constexpr double kFLo = 20.0, kFHi = 20000.0;
}

CourbeFiltre::CourbeFiltre (RomanceProcessor& p, int couche)
    : proc_ (p), couche_ (couche)
{
    courbe_.assign ((size_t) kPointsCourbe, 0.0f);
    recalcule();
    startTimerHz (12);
}

void CourbeFiltre::timerCallback()
{
    // ⭐ On ne recalcule QUE si un reglage a bouge. Une impulsion de 8192
    // points plus une FFT douze fois par seconde pour redessiner la meme
    // courbe, c'est le genre de gaspillage qui ne se voit pas et qui fait
    // ramer l'interface.
    const auto p = "c" + juce::String (couche_ + 1) + "_";
    auto v = [this, &p] (const char* id) -> double
    {
        auto* r = proc_.params.getRawParameterValue (p + id);
        return r != nullptr ? (double) r->load() : 0.0;
    };
    const double sig = v ("coupure") * 1.0 + v ("reso") * 13.0 + v ("topo") * 101.0
                     + v ("morph") * 7.0 + v ("ph_coup") * 0.31 + v ("ph_reso") * 57.0
                     + v ("ph_pente") * 211.0 + v ("suivi") * 3.0;
    if (std::abs (sig - signature_) < 1e-9) return;
    signature_ = sig;
    recalcule();
    repaint();
}

void CourbeFiltre::recalcule()
{
    const auto p = "c" + juce::String (couche_ + 1) + "_";
    auto v = [this, &p] (const char* id) -> double
    {
        auto* r = proc_.params.getRawParameterValue (p + id);
        return r != nullptr ? (double) r->load() : 0.0;
    };

    const double fs = 44100.0;
    const double suivi = v ("suivi");
    // La coupure est RELATIVE : on la ramene en hertz pour la note de reference,
    // exactement comme le fait `Couche::declenche`.
    fcBas_    = v ("coupure") * (kNoteRef * suivi + 261.6 * (1.0 - suivi));
    reso_     = v ("reso");
    topo_     = (int) v ("topo");
    fcHaut_   = v ("ph_coup");
    resoHaut_ = v ("ph_reso");
    pente_    = ((int) v ("ph_pente") == 1) ? 2 : 1;

    romance::PasseHaut ph; ph.prepare (fs);
    ph.regle (fcHaut_, resoHaut_, pente_);
    romance::FiltreMusical fm; fm.prepare (fs);
    fm.regle ((romance::Topologie) juce::jlimit (0, 3, topo_),
              fcBas_, reso_, v ("morph"));

    // /!\ L'IMPULSION EST FENETREE EN SORTIE, pas en entree. Une reponse
    // tronquee net donne des ondulations de plusieurs decibels dans la courbe -
    // on lirait une resonance la ou il n'y a qu'une troncature.
    constexpr int n = 8192;
    std::vector<std::complex<double>> b ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double x = (i == 0) ? 1.0 : 0.0;
        x = ph.traite (x);
        double g = x, d = x;
        (void) d;
        g = fm.traite (g);
        const double w = 0.5 + 0.5 * std::cos (kPi * (double) i / (double) n);
        b[(size_t) i] = g * w;
    }
    romance::fft (b);

    for (int k = 0; k < kPointsCourbe; ++k)
    {
        const double f = kFLo * std::pow (kFHi / kFLo,
                             (double) k / (double) (kPointsCourbe - 1));
        const int raie = juce::jlimit (1, n / 2 - 1,
                             (int) std::round (f * (double) n / fs));
        const double m = std::abs (b[(size_t) raie]);
        courbe_[(size_t) k] = (float) juce::jlimit (-60.0, 24.0,
                                  20.0 * std::log10 (std::max (1e-6, m)));
    }
}

void CourbeFiltre::paint (juce::Graphics& g)
{
    auto rf = getLocalBounds().toFloat();
    g.setColour (kPanneau);
    g.fillRoundedRectangle (rf, 6.0f);
    g.setColour (kTrait);
    g.drawRoundedRectangle (rf.reduced (0.5f), 6.0f, 1.0f);

    auto r = getLocalBounds().reduced (10, 8);
    auto entete = r.removeFromTop (16);

    static const char* kTopo[] = { "4 poles - 24 dB/oct", "3 poles - 18 dB/oct",
                                   "analogique", "Z-Plane" };
    g.setColour (kTexteFort);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText (kTopo[juce::jlimit (0, 3, topo_)], entete, juce::Justification::centredLeft);

    // ⭐ LES DEUX FREQUENCES, EN CLAIR. C'est la moitie de la demande : lire
    // ce qui est filtre en haut et en bas sans ouvrir de menu.
    g.setFont (juce::Font (11.0f));
    g.setColour (kAccent2);
    juce::String txt;
    if (fcHaut_ > 21.0) txt << "haut " << juce::String (fcHaut_, 0) << " Hz   ";
    else                txt << "haut ouvert   ";
    txt << "bas " << juce::String (fcBas_, 0) << " Hz";
    g.drawText (txt, entete, juce::Justification::centredRight);

    auto z = r.reduced (0, 4);
    if (z.getWidth() < 20 || z.getHeight() < 20) return;

    auto yDe = [&z] (double dB)
    {
        return (float) (z.getBottom() - (dB + 60.0) / 84.0 * z.getHeight());
    };
    auto xDe = [&z] (double f)
    {
        return (float) (z.getX() + std::log (f / kFLo) / std::log (kFHi / kFLo)
                                     * z.getWidth());
    };

    // grille : les octaves, et le 0 dB
    g.setColour (kTrait.withAlpha (0.5f));
    for (double f = 31.25; f < kFHi; f *= 2.0)
        g.drawVerticalLine ((int) xDe (f), (float) z.getY(), (float) z.getBottom());
    g.setColour (kTrait);
    g.drawHorizontalLine ((int) yDe (0.0), (float) z.getX(), (float) z.getRight());

    // les deux coupures, reperees
    g.setColour (kAccent2.withAlpha (0.45f));
    if (fcHaut_ > 21.0)
        g.drawVerticalLine ((int) xDe (fcHaut_), (float) z.getY(), (float) z.getBottom());
    g.drawVerticalLine ((int) xDe (juce::jlimit (kFLo, kFHi, fcBas_)),
                        (float) z.getY(), (float) z.getBottom());

    // ⭐ LE TRACE, et l'aire sous lui : c'est ce qui rend une courbe lisible
    // d'un coup d'oeil au lieu de demander qu'on la suive du regard.
    juce::Path trace, aire;
    for (int k = 0; k < kPointsCourbe; ++k)
    {
        const double f = kFLo * std::pow (kFHi / kFLo,
                             (double) k / (double) (kPointsCourbe - 1));
        const float x = xDe (f), y = yDe ((double) courbe_[(size_t) k]);
        if (k == 0) { trace.startNewSubPath (x, y); aire.startNewSubPath (x, (float) z.getBottom()); aire.lineTo (x, y); }
        else        { trace.lineTo (x, y); aire.lineTo (x, y); }
    }
    aire.lineTo ((float) z.getRight(), (float) z.getBottom());
    aire.closeSubPath();
    g.setColour (kAccent2.withAlpha (0.16f));
    g.fillPath (aire);
    g.setColour (kAccent2);
    g.strokePath (trace, juce::PathStrokeType (1.6f));

    g.setColour (kTexte.withAlpha (0.55f));
    g.setFont (juce::Font (10.0f));
    g.drawText ("trace pour un do3 - la coupure basse suit le clavier",
                z.removeFromBottom (12), juce::Justification::centredRight);
}

// ------------------------------------------------------- les bandes de spectre

BandesSpectre::BandesSpectre (RomanceProcessor& p, int couche)
    : proc_ (p), couche_ (couche)
{
    relit();
    startTimerHz (6);
}

void BandesSpectre::relit()
{
    sp_ = proc_.spectre (couche_);
    version_ = proc_.versionSpectre (couche_);
    repaint();
}

void BandesSpectre::timerCallback()
{
    // /!\ On ne relit PAS pendant qu'on trace : le dessin en cours est la
    // verite, et le rattraper depuis le processeur ferait sauter le trait.
    if (dernier_ >= 0) return;
    if (proc_.versionSpectre (couche_) != version_) relit();
}

void BandesSpectre::trace (const juce::MouseEvent& e)
{
    auto r = getLocalBounds().reduced (4);
    if (r.getWidth() < 8 || r.getHeight() < 8) return;
    const int n = romance::kBandesSpectre;

    const double fx = (double) (e.x - r.getX()) / (double) r.getWidth();
    const double fy = (double) (e.y - r.getY()) / (double) r.getHeight();
    const int i = juce::jlimit (0, n - 1, (int) (fx * (double) n));
    const double a = juce::jlimit (0.0, 1.0, 1.0 - fy);
    const uint8_t v = (uint8_t) std::lround (a * 255.0);

    if (dernier_ >= 0 && dernier_ != i)
    {
        // ⭐ On remplit tout le chemin parcouru : sans ca, un glissement
        // rapide laisse des bandes intactes derriere lui.
        const double aPrec = (double) sp_.bande[dernier_] / 255.0;
        const int pas = (i > dernier_) ? 1 : -1;
        const int nb = std::abs (i - dernier_);
        for (int k = 1; k <= nb; ++k)
        {
            const double t = (double) k / (double) nb;
            sp_.bande[dernier_ + k * pas] =
                (uint8_t) std::lround ((aPrec + (a - aPrec) * t) * 255.0);
        }
    }
    else sp_.bande[i] = v;

    dernier_ = i;
    proc_.poseSpectre (couche_, sp_);
    version_ = proc_.versionSpectre (couche_);
    repaint();
}

void BandesSpectre::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        sp_.plat();
        proc_.poseSpectre (couche_, sp_);
        version_ = proc_.versionSpectre (couche_);
        repaint();
        return;
    }
    dernier_ = -1;
    trace (e);
}

void BandesSpectre::mouseDrag (const juce::MouseEvent& e) { if (dernier_ >= 0) trace (e); }
void BandesSpectre::mouseUp (const juce::MouseEvent&)     { dernier_ = -1; }

void BandesSpectre::paint (juce::Graphics& g)
{
    auto rf = getLocalBounds().toFloat();
    g.setColour (kFond);
    g.fillRoundedRectangle (rf, 3.0f);
    g.setColour (kTrait);
    g.drawRoundedRectangle (rf.reduced (0.5f), 3.0f, 1.0f);

    auto r = getLocalBounds().reduced (4);
    if (r.getWidth() < 8 || r.getHeight() < 8) return;
    const int n = romance::kBandesSpectre;
    const double lc = (double) r.getWidth() / (double) n;

    // ⭐ LES OCTAVES, REPEREES. Sans elles, un axe logarithmique de 64 cellules
    // est une rangee de barres sans echelle - on dessine sans savoir ou.
    g.setColour (kTrait.withAlpha (0.55f));
    g.setFont (juce::Font (9.0f));
    for (double f = 31.25; f < 20000.0; f *= 2.0)
    {
        const double u = std::log (f / 20.0) / std::log (0.45 * 44100.0 / 20.0);
        if (u < 0.0 || u > 1.0) continue;
        const float x = (float) (r.getX() + u * r.getWidth());
        g.drawVerticalLine ((int) x, (float) r.getY(), (float) r.getBottom());
        g.setColour (kMuted());
        g.drawText (f < 1000.0 ? juce::String ((int) f)
                               : (juce::String (f / 1000.0, 0) + "k"),
                    (int) x + 2, r.getBottom() - 12, 30, 11,
                    juce::Justification::centredLeft);
        g.setColour (kTrait.withAlpha (0.55f));
    }

    for (int i = 0; i < n; ++i)
    {
        const float x = (float) (r.getX() + (double) i * lc);
        const float w = (float) std::max (1.0, lc - 1.0);
        const double a = (double) sp_.bande[i] / 255.0;
        const float h = (float) (a * r.getHeight());
        g.setColour (kAccent2.withAlpha (0.85f));
        g.fillRect (x, (float) r.getBottom() - h, w, h);
    }

    g.setColour (kMuted());
    g.setFont (juce::Font (9.5f));
    g.drawText ("bande large = bruit   bande etroite = partiel",
                r.getX() + 6, r.getY() + 3, 300, 12,
                juce::Justification::centredLeft);
}

// ------------------------------------------------------- la grille dessinable

Grille::Grille (RomanceProcessor& p, int couche) : proc_ (p), couche_ (couche)
{
    relit();
    startTimerHz (6);
}

void Grille::relit()
{
    d_ = proc_.dessin (couche_);
    version_ = proc_.versionDessin (couche_);
    repaint();
}

void Grille::timerCallback()
{
    // /!\ On ne relit PAS pendant qu'on trace : le dessin en cours est la
    // verite, et le rattraper depuis le processeur ferait sauter le trait.
    if (dernier_ >= 0) return;
    if (proc_.versionDessin (couche_) != version_
        || proc_.dessin (couche_).a.nCellules != d_.a.nCellules
        || proc_.dessin (couche_).a.bits != d_.a.bits)
        relit();
}

void Grille::trace (const juce::MouseEvent& e)
{
    auto r = getLocalBounds().reduced (4);
    if (r.getWidth() < 8 || r.getHeight() < 8) return;
    romance::Dessin& cible = (edite_ == 0) ? d_.a : d_.b;
    // ⭐ Dessiner dans B l'ACTIVE : sinon on dessine et rien ne change.
    if (edite_ == 1) d_.bActif = true;
    const int n = cible.nCellules;

    const double fx = (double) (e.x - r.getX()) / (double) r.getWidth();
    const double fy = (double) (e.y - r.getY()) / (double) r.getHeight();
    const int i = std::max (0, std::min (n - 1, (int) (fx * (double) n)));
    const double a = std::max (-1.0, std::min (1.0, 1.0 - 2.0 * fy));

    if (dernier_ >= 0 && dernier_ != i)
    {
        // ⭐ On remplit tout le chemin parcouru depuis le dernier appel : sans
        // ca, un glissement rapide laisse des cellules intactes derriere lui.
        const double aPrec = cible.lit (dernier_, n);
        const int pas = (i > dernier_) ? 1 : -1;
        const int nb = std::abs (i - dernier_);
        for (int k = 1; k <= nb; ++k)
        {
            const double t = (double) k / (double) nb;
            cible.pose (dernier_ + k * pas, aPrec + (a - aPrec) * t, n);
        }
    }
    else
        cible.pose (i, a, n);

    dernier_ = i;
    proc_.poseDessin (couche_, d_);
    version_ = proc_.versionDessin (couche_);
    repaint();
}

void Grille::mouseDown (const juce::MouseEvent& e)
{
    // l'onglet A / B occupe le coin haut droit
    {
        auto r = getLocalBounds().reduced (4);
        const juce::Rectangle<int> o (r.getRight() - 56, r.getY() + 2, 54, 14);
        if (o.contains (e.getPosition()))
        {
            edite_ = (e.x < o.getCentreX()) ? 0 : 1;
            repaint();
            return;
        }
    }

    // clic droit : on revient au sinus, le dessin de depart
    if (e.mods.isRightButtonDown())
    {
        // ⭐⭐ LE MENU DES FORMES DE DEPART. Dessiner DEUX formes coherentes a
        // la souris est un travail ; dessiner l'une et poser l'autre depuis une
        // forme connue est un geste. C'est ce qui rend le morph A -> B
        // utilisable au lieu d'etre une promesse.
        juce::PopupMenu m;
        m.addSectionHeader (edite_ == 0 ? "DESSIN A" : "DESSIN B");
        using F = romance::Dessin::Forme;
        static const F kF[8] = { F::Sinus, F::Triangle, F::Scie, F::Carre,
                                 F::Impulsion, F::Impaires, F::DeuxTiers, F::Cloche };
        for (int k = 0; k < 8; ++k)
            m.addItem (k + 1, romance::Dessin::nomForme (kF[k]));
        m.addSeparator();
        // ⭐ Copier l'autre cote : le geste le plus utile pour partir d'une
        // variation plutot que de zero.
        m.addItem (20, edite_ == 0 ? "copier B dans A" : "copier A dans B");
        if (d_.bActif) m.addItem (21, "desactiver le morph (A seul)");
        m.addSeparator();
        // ⭐ La bibliotheque de formes. Un fichier de 128 octets se sauvegarde,
        // se copie sur une autre machine, s'envoie dans un message - ce qu'un
        // etat interne au plugin ne permet pas.
        m.addItem (30, "enregistrer cette forme...");
        m.addItem (31, "charger une forme...");

        m.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (190),
            [this] (int choix)
            {
                if (choix == 0) return;
                romance::Dessin& cible = (edite_ == 0) ? d_.a : d_.b;
                if (choix >= 1 && choix <= 8)
                {
                    using F2 = romance::Dessin::Forme;
                    static const F2 kF2[8] = { F2::Sinus, F2::Triangle, F2::Scie,
                                               F2::Carre, F2::Impulsion, F2::Impaires,
                                               F2::DeuxTiers, F2::Cloche };
                    cible.pose (kF2[choix - 1]);
                    if (edite_ == 1) d_.bActif = true;
                }
                else if (choix == 20)
                {
                    const romance::Dessin& autre = (edite_ == 0) ? d_.b : d_.a;
                    for (int i = 0; i < romance::kCellulesMax; ++i)
                        cible.cellule[i] = autre.cellule[i];
                    if (edite_ == 1) d_.bActif = true;
                }
                else if (choix == 21) d_.bActif = false;
                else if (choix == 30 || choix == 31) { fichierForme (choix == 30); return; }

                proc_.poseDessin (couche_, d_);
                version_ = proc_.versionDessin (couche_);
                repaint();
            });
        return;
    }
    dernier_ = -1;
    trace (e);
}

/** ⭐⭐ ENREGISTRER OU CHARGER UNE FORME.

    /!\ UN SELECTEUR NATIF, PAS UNE LISTE MAISON. L'utilisateur a demande des
    DOSSIERS : lui rendre son explorateur lui donne le renommage, les
    sous-dossiers, la recherche et la suppression - tout ce qu'une liste
    reconstruite a moitie n'aurait pas, et qu'il faudrait ensuite maintenir.

    /!\ ET LE SELECTEUR EST ASYNCHRONE. Une boucle modale dans un plugin bloque
    le fil message de l'HOTE : certains hotes s'en remettent, d'autres non, et
    ca ne se voit qu'en concert.
*/
void Grille::fichierForme (bool enregistrer)
{
    const auto dossier = romance::Bibliotheque::dossierFormes();
    const auto ext = "*" + romance::Bibliotheque::extensionForme();

    selecteur_ = std::make_unique<juce::FileChooser> (
        enregistrer ? "Enregistrer la forme" : "Charger une forme",
        juce::File (dossier), ext);

    const int drapeaux = enregistrer
        ? (juce::FileBrowserComponent::saveMode
         | juce::FileBrowserComponent::warnAboutOverwriting)
        : (juce::FileBrowserComponent::openMode
         | juce::FileBrowserComponent::canSelectFiles);

    selecteur_->launchAsync (drapeaux,
        [this, enregistrer] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (enregistrer)
            {
                if (! f.hasFileExtension (romance::Bibliotheque::extensionForme()))
                    f = f.withFileExtension (romance::Bibliotheque::extensionForme());
                romance::ecritForme (f, d_);
            }
            else
            {
                romance::DessinAB lu;
                if (! romance::litForme (f, lu)) return;   // fichier illisible
                d_ = lu;
                proc_.poseDessin (couche_, d_);
                version_ = proc_.versionDessin (couche_);
                repaint();
            }
        });
}

void Grille::mouseDrag (const juce::MouseEvent& e) { if (dernier_ >= 0) trace (e); }
void Grille::mouseUp (const juce::MouseEvent&)     { dernier_ = -1; }

void Grille::paint (juce::Graphics& g)
{
    auto rf = getLocalBounds().toFloat();
    g.setColour (kFond);
    g.fillRoundedRectangle (rf, 3.0f);
    g.setColour (kTrait);
    g.drawRoundedRectangle (rf.reduced (0.5f), 3.0f, 1.0f);

    auto r = getLocalBounds().reduced (4);
    if (r.getWidth() < 8 || r.getHeight() < 8) return;

    // ⭐ L'ONGLET A / B, dans le coin. Deux mots, pas un panneau.
    {
        auto o = juce::Rectangle<int> (r.getRight() - 56, r.getY() + 2, 54, 14);
        for (int k = 0; k < 2; ++k)
        {
            auto c2 = o.removeFromLeft (27);
            const bool actif = (k == edite_);
            g.setColour (actif ? kAccent3.withAlpha (0.45f) : kPanneau);
            g.fillRect (c2.reduced (1));
            g.setColour (actif ? kTexteFort
                               : ((k == 1 && ! d_.bActif) ? kMuted() : kTexte));
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText (k == 0 ? "A" : "B", c2, juce::Justification::centred);
        }
    }

    const romance::Dessin& vu = (edite_ == 0) ? d_.a : d_.b;
    const int n = vu.nCellules;
    const double lc = (double) r.getWidth() / (double) n;

    // ⭐ LES PALIERS DE QUANTIFICATION, dessines. C'est ce qui rend la
    // profondeur de bits comprehensible sans lire un manuel : a 4 bits on VOIT
    // les seize marches ou le dessin va tomber.
    const int niveaux = 1 << std::max (romance::kBitsMin,
                                       std::min (romance::kBitsMax, vu.bits));
    if (niveaux <= 64)
    {
        g.setColour (kTrait.withAlpha (0.55f));
        for (int k = 0; k < niveaux; ++k)
        {
            const double a = -1.0 + 2.0 * (double) k / (double) (niveaux - 1);
            const float y = (float) (r.getY() + (0.5 - 0.5 * a) * r.getHeight());
            g.drawHorizontalLine ((int) y, (float) r.getX(), (float) r.getRight());
        }
    }

    // la ligne de zero, plus marquee que les paliers
    g.setColour (kTrait);
    const float y0 = (float) (r.getCentreY());
    g.drawHorizontalLine ((int) y0, (float) r.getX(), (float) r.getRight());

    for (int i = 0; i < n; ++i)
    {
        const float x = (float) (r.getX() + (double) i * lc);
        const float w = (float) std::max (1.0, lc - 1.0);
        const double aStock = vu.lit (i, n);
        const double aRendu = vu.rendu (i, n);

        // /!\ DEUX TRAITS PAR CELLULE, ET C'EST TOUT LE PROPOS : le fantome
        // clair est la valeur STOCKEE en huit bits, la barre pleine est ce
        // qu'on ENTEND apres quantification. Ne montrer que la seconde ferait
        // croire que baisser les bits detruit le dessin - alors qu'il est
        // intact dessous, et qu'il revient si l'on remonte.
        if (std::abs (aStock - aRendu) > 1e-9)
        {
            const float ys = (float) (r.getY() + (0.5 - 0.5 * aStock) * r.getHeight());
            g.setColour (kAccent3.withAlpha (0.30f));
            g.fillRect (x, std::min (ys, y0), w, std::abs (ys - y0));
        }
        const float yr = (float) (r.getY() + (0.5 - 0.5 * aRendu) * r.getHeight());
        g.setColour (kAccent3);
        g.fillRect (x, std::min (yr, y0), w, std::max (1.0f, std::abs (yr - y0)));
    }
}

Onde::Onde (RomanceProcessor& p, int couche) : proc_ (p), couche_ (couche)
{
    startTimerHz (8);   // le materiau ne change pas assez vite pour plus
}

/** ⭐⭐ CE QUE LE MOTEUR PRODUIT VRAIMENT, quel que soit son type.

    /!\ ON REND PAR LA VRAIE `Couche`, pas par une formule d'affichage. Une
    reimplementation « juste pour dessiner » serait un deuxieme moteur a tenir
    a jour, et elle mentirait des le premier reglage qu'on ajoute. Ici le
    dessin passe par le meme filtre, le meme unisson et le meme etage de
    reconstruction que le son.

    /!\ SUR LE FIL MESSAGE, huit fois par seconde, et sur DEUX cycles - pas
    deux secondes. Rendre par la vraie couche coute ce que coute une voix ;
    le faire sur une seconde d'audio a chaque rafraichissement rendrait
    l'interface poussive pour dessiner un trait qui ne bouge pas.
*/
void Onde::rendApercu()
{
    const double fs = 44100.0;
    const double hz = 130.81;                 // do2 : deux cycles tiennent large
    const int n = (int) (2.0 * fs / hz);      // exactement deux cycles

    romance::Couche c;
    c.prepare (fs);
    auto r = proc_.reglagesCouche (couche_);
    // /!\ On NEUTRALISE l'enveloppe et le retard : on veut voir la FORME, pas
    // l'attaque. Sans ca le dessin montrerait surtout une rampe de volume.
    r.attaque = 0.0005; r.decli = 8.0; r.tenue = 1.0; r.relache = 8.0;
    r.retard = 0.0; r.veloNiveau = 0.0;
    c.declenche (r, hz / 261.6255653, hz, 1.0, 1.0);

    hauts_.assign ((size_t) n, 0.0f);
    bas_.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        double g = 0.0, d = 0.0;
        c.suivant (r, nullptr, 0, g, d, 0.0, 0.0);
        hauts_[(size_t) i] = (float) g;
        bas_[(size_t) i]   = (float) g;
    }
    // normalisation : on compare des formes, pas des volumes
    float crete = 1e-6f;
    for (float v : hauts_) crete = std::max (crete, std::abs (v));
    const float k = 0.92f / crete;
    for (int i = 0; i < n; ++i) { hauts_[(size_t) i] *= k; bas_[(size_t) i] *= k; }
    nEch_ = n;
    boucle_ = 0.0;
}

void Onde::timerCallback()
{
    nTetes_ = proc_.tetes (couche_, tetes_, RomanceProcessor::kTetesMax);

    const auto id = "c" + juce::String (couche_ + 1) + "_moteur";
    auto* pm = proc_.params.getRawParameterValue (id.toRawUTF8());
    moteur_ = (pm != nullptr) ? (int) pm->load() : 0;
    if (moteur_ != 0) { rendApercu(); repaint(); return; }

    int n = 0; double b = 0.0;
    const double* rom = proc_.materiau (couche_, n, b);
    if (rom == nullptr || n < 2) { hauts_.clear(); bas_.clear(); repaint(); return; }

    const int cols = juce::jmax (1, getWidth());
    if ((int) hauts_.size() != cols) { hauts_.resize ((size_t) cols); bas_.resize ((size_t) cols); }
    for (int x = 0; x < cols; ++x)
    {
        const int i0 = (int) ((double) x       / cols * n);
        const int i1 = (int) ((double) (x + 1) / cols * n);
        float hi = -2.0f, lo = 2.0f;
        for (int i = i0; i < juce::jmax (i0 + 1, i1) && i < n; ++i)
        {
            const float v = (float) rom[i];
            hi = juce::jmax (hi, v); lo = juce::jmin (lo, v);
        }
        hauts_[(size_t) x] = hi; bas_[(size_t) x] = lo;
    }
    boucle_ = b; nEch_ = n;
    repaint();
}

void Onde::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (kFond);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (kTrait);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 2.0f, r.getRight() - 2.0f);

    if (hauts_.empty())
    {
        g.setColour (kMuted());
        g.setFont (juce::Font (11.0f));
        g.drawText ("materiau en cours de fabrication", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    const float cy = r.getCentreY(), demi = r.getHeight() * 0.46f;
    g.setColour (kAccent);
    for (size_t x = 0; x < hauts_.size(); ++x)
    {
        const float hi = cy - hauts_[x] * demi, lo = cy - bas_[x] * demi;
        g.drawVerticalLine ((int) x + (int) r.getX(),
                            juce::jmin (hi, lo), juce::jmax (hi, lo) + 1.0f);
    }

    if (boucle_ > 0.0 && boucle_ < 1.0)
    {
        // /!\ Le point de boucle est une information du MATERIAU, pas une
        // decoration : sur une ROM de recette c'est lui qui decide de ce qu'on
        // entend une fois l'attaque passee.
        const float x = r.getX() + (float) boucle_ * r.getWidth();
        g.setColour (kAccent3);
        g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
        g.setFont (juce::Font (9.5f));
        g.drawText ("boucle", (int) x + 4, (int) r.getY() + 3, 60, 12,
                    juce::Justification::centredLeft);
    }

    // ⭐⭐ LES TETES DE LECTURE - une barre par note qui sonne, qui avance a la
    // vitesse de lecture.
    //
    // /!\ ELLES SONT NORMALISEES EN AMONT (voir Couche::position). Le lecteur
    // de ROM compte en echantillons, les trois autres moteurs en phase de
    // cycle : deux grandeurs sans rapport. Si la conversion se faisait ici,
    // elle serait a refaire - et a rater - a chaque moteur ajoute.
    for (int i = 0; i < nTetes_; ++i)
    {
        const float x = r.getX() + juce::jlimit (0.0f, 1.0f, tetes_[i]) * r.getWidth();
        g.setColour (kAccent.withAlpha (0.85f));
        g.drawVerticalLine ((int) x, r.getY() + 1.0f, r.getBottom() - 1.0f);
    }

    g.setColour (kMuted());
    g.setFont (juce::Font (9.5f));
    // ⭐ Ce qu'on regarde n'est pas la meme chose selon le moteur, et le dire
    // evite de croire qu'un cycle de FM est un echantillon de deux secondes.
    g.drawText (moteur_ == 0 ? (juce::String (nEch_) + " echantillons")
                             : juce::String ("deux cycles - do2"),
                (int) r.getX() + 6, (int) r.getBottom() - 15,
                160, 12, juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ FX

PageFx::PageFx (RomanceProcessor& p) : proc_ (p)
{
    static const char* kNom[5] = { "A", "B", "C", "D", "MASTER" };
    for (int t = 0; t < 5; ++t)
    {
        auto* b = onglets_.add (new juce::TextButton (kNom[t]));
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::buttonOnColourId, kAccent2.withAlpha (0.45f));
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->setColour (juce::TextButton::textColourOnId, kTexteFort);
        b->setToggleState (t == tranche_, juce::dontSendNotification);
        b->addListener (this);
        addAndMakeVisible (b);

        // ⭐ Le niveau de la tranche : celui de la couche, ou la sortie
        // generale pour le master. Ce sont les parametres qui existent deja -
        // la page ne cree pas un deuxieme reglage de volume a cote du premier.
        faders_.add (new Bouton (proc_.params,
                                 (t == 4) ? juce::String ("sortie")
                                          : ("c" + juce::String (t + 1) + "_niveau"),
                                 "niveau", (t == 4) ? kAccent : kAccent2));
        addAndMakeVisible (faders_.getLast());
    }

    bAjoute_.setColour (juce::TextButton::buttonColourId, kFond);
    bAjoute_.setColour (juce::TextButton::textColourOffId, kTexte.withAlpha (0.7f));
    bAjoute_.addListener (this);
    contenu_.addAndMakeVisible (bAjoute_);

    vue_.setViewedComponent (&contenu_, false);
    vue_.setScrollBarsShown (true, false);
    addAndMakeVisible (vue_);

    construitRack();
    startTimerHz (6);
}

juce::String PageFx::prefixe (int t) const
{
    // /!\ Le master garde ses identifiants HISTORIQUES, sans prefixe. Les
    // renommer aurait casse tous les presets deja enregistres.
    return (t == 4) ? juce::String() : ("fx" + juce::String (t + 1) + "_");
}

/** ⭐⭐ L'IDENTIFIANT D'UN EMPLACEMENT - et les deux racks ne le nomment pas
    pareil.

    /!\ LE MASTER NE PRENAIT AUCUN EFFET, et c'etait ca. Ses emplacements
    s'appellent `fx1`..`fx10` depuis l'origine ; les tranches, ajoutees ensuite,
    utilisent `fx<c>_slot<i>`. Mon interface composait « prefixe + slot », donc
    elle cherchait `slot1` pour le master - un parametre qui n'existe pas.
    `getRawParameterValue` rendait nullptr, l'emplacement paraissait vide, et
    poser un effet n'ecrivait nulle part.

    C'est le prix de l'asymetrie assumee sur les noms du master. Elle est
    payee ici, une fois, plutot que par une migration d'etat. */
juce::String PageFx::idSlot (int t, int i) const
{
    // ⭐ ON DELEGUE. La regle est dans `Rack.h`, sans JUCE, donc testee.
    // Elle etait ecrite deux fois ; la deuxieme copie a rendu le rack master
    // intouchable pendant une semaine (voir le commentaire la-bas).
    return juce::String (romance::idEmplacementFx (t, i));
}

int PageFx::premierLibre() const
{
    const auto p = prefixe (tranche_);
    for (int i = 0; i < romance::kEmplacementsFx; ++i)
    {
        auto* v = proc_.params.getRawParameterValue (idSlot (tranche_, i));
        if (v != nullptr && (int) v->load() == 0) return i;
    }
    return -1;
}

void PageFx::menuEffets (int slot)
{
    const auto p = prefixe (tranche_);
    const int cible = (slot >= 0) ? slot : premierLibre();
    if (cible < 0) return;                       // rack plein : rien a proposer

    auto* v = proc_.params.getRawParameterValue (idSlot (tranche_, cible));
    const int actuel = (v != nullptr) ? (int) v->load() : 0;

    juce::PopupMenu m;
    m.addSectionHeader ((tranche_ == 4 ? juce::String ("MASTER")
                                       : juce::String ("TRANCHE ") + (char) ('A' + tranche_))
                        + " - emplacement " + juce::String (cible + 1));
    for (int e = 1; e <= romance::kEmplacementsFx; ++e)
        m.addItem (e, romance::nomEffet (e), true, e == actuel);
    if (actuel > 0)
    {
        m.addSeparator();
        // ⭐ RETIRER EST DANS LE MEME MENU, en rouge, comme dans la reference.
        // Un menu qui ne saurait qu'ajouter obligerait a garder un « - » dans
        // une liste pour defaire - c'est le mecanisme impose qu'on retire.
        juce::PopupMenu::Item r ("retirer");
        r.itemID = 99;
        r.colour = juce::Colour (0xffd05a5a);
        m.addItem (r);
    }

    const juce::String id = idSlot (tranche_, cible);
    m.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (180),
        [this, id, cible] (int choix)
        {
            if (choix == 0) return;
            if (auto* par = proc_.params.getParameter (id))
            {
                const float val = (choix == 99) ? 0.0f : (float) choix;
                par->setValueNotifyingHost (par->convertTo0to1 (val));
            }
        });
}

void PageFx::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown()) menuEffets (-1);
}

void PageFx::buttonClicked (juce::Button* b)
{
    if (b == &bAjoute_) { menuEffets (-1); return; }
    for (int t = 0; t < onglets_.size(); ++t)
        if (onglets_[t] == b && t != tranche_)
        {
            tranche_ = t;
            for (int k = 0; k < onglets_.size(); ++k)
                onglets_[k]->setToggleState (k == t, juce::dontSendNotification);
            for (int i = 0; i < romance::kEmplacementsFx; ++i) poseDansSlot_[i] = -1;
            construitRack();
            resized();
            return;
        }
}

void PageFx::timerCallback()
{
    // ⭐ On ne reconstruit que si le CONTENU d'un emplacement a change.
    // Reconstruire a chaque battement detruirait le bouton qu'on est en train
    // de tourner - un defaut qui se manifeste par « le reglage saute quand je
    // le bouge » et qu'on va ensuite chercher dans le DSP.
    bool change = false;
    for (int i = 0; i < romance::kEmplacementsFx; ++i)
    {
        // /!\ `idSlot`, PAS UNE COMPOSITION A LA MAIN. C'est ici que la
        // deuxieme copie de la regle vivait, et elle rendait nullptr sur le
        // master - donc « emplacement vide », donc reconstruction du rack a
        // chaque battement, donc des boutons qu'on ne pouvait pas toucher.
        auto* v = proc_.params.getRawParameterValue (idSlot (tranche_, i));
        const int e = (v != nullptr) ? (int) v->load() : 0;
        if (e != poseDansSlot_[i]) { poseDansSlot_[i] = e; change = true; }
    }
    if (change) { construitRack(); resized(); }
}

/** ⭐⭐ REORDONNER SANS RIEN CASSER.

    Demande de l'utilisateur : « peut-on glisser l'ordre des modules, pour
    rearranger le routing sans tout casser ? »

    /!\ ON DEPLACE LE NUMERO D'EFFET, PAS SES REGLAGES. Les reglages d'un
    effet vivent dans SES parametres (`cho_mix`, `rev_taille`...), jamais dans
    l'emplacement. Deplacer un effet ne fait donc que changer l'ordre de
    traversee : sa vitesse de chorus, son temps de delai et son melange le
    suivent sans qu'on ait rien a copier. C'est exactement le « sans tout
    casser » demande, et ca tient au fait que les deux ont ete separes des le
    depart.

    /!\ ET ON DECALE, ON N'ECHANGE PAS. Faire passer un effet de la place 2 a
    la place 5 doit remonter 3, 4 et 5 d'un cran - pas troquer 2 contre 5, ce
    qui melangerait l'ordre du reste.
*/
void PageFx::deplaceSlot (int de, int vers)
{
    const int n = romance::kEmplacementsFx;
    de   = juce::jlimit (0, n - 1, de);
    vers = juce::jlimit (0, n - 1, vers);
    if (de == vers) return;

    int v[romance::kEmplacementsFx];
    for (int i = 0; i < n; ++i)
    {
        auto* p = proc_.params.getRawParameterValue (idSlot (tranche_, i));
        v[i] = (p != nullptr) ? (int) p->load() : 0;
    }

    const int porte = v[de];
    if (vers > de) for (int i = de; i < vers; ++i) v[i] = v[i + 1];
    else           for (int i = de; i > vers; --i) v[i] = v[i - 1];
    v[vers] = porte;

    for (int i = 0; i < n; ++i)
        if (auto* par = proc_.params.getParameter (idSlot (tranche_, i)))
            par->setValueNotifyingHost (par->convertTo0to1 ((float) v[i]));
}

void PageFx::construitRack()
{
    lignes_.clear();
    auto& s = proc_.params;
    const auto p = prefixe (tranche_);

    for (int i = 0; i < romance::kEmplacementsFx; ++i)
    {
        auto* v = s.getRawParameterValue (idSlot (tranche_, i));
        const int e = (v != nullptr) ? (int) v->load() : 0;
        poseDansSlot_[i] = e;
        // ⭐ UN EMPLACEMENT VIDE N'EXISTE PAS A L'ECRAN. C'est toute la
        // difference avec l'ancienne page : on ne montre que ce qu'on a pose.
        if (e == 0) continue;

        auto* g = new Groupe (juce::String (i + 1) + "  " + romance::nomEffet (e),
                              kAccent2);
        // /!\ HUIT COLONNES, PAS DOUZE. L'effet le plus fourni occupe sept
        // cases (un menu en vaut deux) : a douze colonnes, chaque commande
        // recevait un douzieme de la largeur et son etiquette etait tronquee.
        // Retour de l'utilisateur : « les noms des boutons ne sont plus
        // marques ».
        g->colonnes = 8;
        switch (e)
        {
            case 1:
                g->ajoute (new Bascule (s, p + "cho", "on"));
                g->ajoute (new Menu (s, p + "cho_nat", "nature"));
                g->ajoute (new Bouton (s, p + "cho_vit", "vitesse", kAccent2));
                g->ajoute (new Bouton (s, p + "cho_prof", "profondeur", kAccent2));
                g->ajoute (new Bouton (s, p + "cho_coul", "couleur", kAccent2));
                g->ajoute (new Bouton (s, p + "cho_mix", "melange", kAccent2));
                break;
            case 2:
                g->ajoute (new Bascule (s, p + "ens", "on"));
                g->ajoute (new Bouton (s, p + "ens_vit", "vitesse", kAccent2));
                g->ajoute (new Bouton (s, p + "ens_prof", "profondeur", kAccent2));
                g->ajoute (new Bouton (s, p + "ens_mix", "melange", kAccent2));
                break;
            case 3:
                g->ajoute (new Bascule (s, p + "del", "on"));
                g->ajoute (new Bouton (s, p + "del_tps", "temps", kAccent2));
                g->ajoute (new Bouton (s, p + "del_rei", "reinjection", kAccent2));
                g->ajoute (new Bouton (s, p + "del_amo", "amortissement", kAccent2));
                g->ajoute (new Bouton (s, p + "del_pp", "ping-pong", kAccent2));
                g->ajoute (new Bouton (s, p + "del_mix", "melange", kAccent2));
                break;
            case 4:
                g->ajoute (new Bascule (s, p + "rev", "on"));
                g->ajoute (new Bouton (s, p + "rev_taille", "taille", kAccent2));
                g->ajoute (new Bouton (s, p + "rev_amorti", "amortissement", kAccent2));
                g->ajoute (new Bouton (s, p + "rev_pre", "pre-delai", kAccent2));
                g->ajoute (new Bouton (s, p + "rev_mix", "melange", kAccent2));
                break;
            case 5:
                g->ajoute (new Bascule (s, p + "sat_on", "on"));
                g->ajoute (new Bouton (s, p + "sat", "dose", kAccent2));
                g->ajoute (new Bouton (s, p + "sat_asym", "asymetrie", kAccent2));
                break;
            case 6:
                g->ajoute (new Bascule (s, p + "ton_on", "on"));
                g->ajoute (new Bouton (s, p + "ton_grave", "grave", kAccent2));
                g->ajoute (new Bouton (s, p + "ton_aigu", "aigu", kAccent2));
                break;
            case 7:
                g->ajoute (new Bascule (s, p + "pha_on", "on"));
                g->ajoute (new Bouton (s, p + "pha_vit", "vitesse", kAccent2));
                g->ajoute (new Bouton (s, p + "pha_prof", "profondeur", kAccent2));
                g->ajoute (new Bouton (s, p + "pha_rei", "reinjection", kAccent2));
                g->ajoute (new Bouton (s, p + "pha_cel", "cellules", kAccent2));
                g->ajoute (new Bouton (s, p + "pha_mix", "melange", kAccent2));
                break;
            case 8:
                g->ajoute (new Bascule (s, p + "mf_on", "on"));
                g->ajoute (new Menu (s, p + "mf_type", "type"));
                g->ajoute (new Bouton (s, p + "mf_coup", "coupure", kAccent2));
                g->ajoute (new Bouton (s, p + "mf_reso", "resonance", kAccent2));
                g->ajoute (new Bouton (s, p + "mf_mix", "melange", kAccent2));
                break;
            case 9:
                g->ajoute (new Bascule (s, p + "trf_on", "on"));
                g->ajoute (new Menu (s, p + "trf_mach", "machine"));
                g->ajoute (new Bouton (s, p + "trf_mix", "melange", kAccent2));
                break;
            case 10:
                g->ajoute (new Bascule (s, p + "cmp_on", "on"));
                g->ajoute (new Bouton (s, p + "cmp_seuil", "seuil", kAccent2));
                g->ajoute (new Bouton (s, p + "cmp_taux", "taux", kAccent2));
                g->ajoute (new Bouton (s, p + "cmp_att", "attaque", kAccent2));
                g->ajoute (new Bouton (s, p + "cmp_rel", "relachement", kAccent2));
                g->ajoute (new Bouton (s, p + "cmp_comp", "compensation", kAccent2));
                break;
            default: break;
        }
        // ⭐ Le clic droit sur CETTE ligne ouvre le menu de CET emplacement -
        // donc avec « retirer », qui etait jusqu'ici inatteignable.
        g->surClicDroit = [this, i] { menuEffets (i); };
        // ⭐ Le glisser vertical reordonne. La hauteur d'une ligne est la
        // meme partout, donc le nombre de crans se lit directement.
        g->surGlisse = [this, i] (int dy)
        {
            // /!\ LA MEME HAUTEUR QUE CELLE QUI SERT A POSER LES LIGNES. Elle
            // etait ecrite en dur a 78 des deux cotes : le jour ou l'une bouge,
            // glisser d'une ligne en deplace deux, et personne ne fait le lien.
            const int h = juce::jmax (1, hauteurLigne_);
            deplaceSlot (i, i + (int) std::lround ((double) dy / (double) h));
        };
        lignes_.add (g);
        contenu_.addAndMakeVisible (g);
    }
}

void PageFx::paint (juce::Graphics& g)
{
    g.setColour (kTexte);
    g.setFont (juce::Font (11.0f));
    g.drawText ("TRANCHE", 16, 12, 80, 16, juce::Justification::centredLeft);
    g.setColour (kMuted());
    g.setFont (juce::Font (10.0f));
    g.drawText ("clic droit : ajouter, remplacer ou retirer un effet",
                16, getHeight() - 20, 380, 14, juce::Justification::centredLeft);
}

void PageFx::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (18);

    auto colonne = r.removeFromLeft (108);
    for (int t = 0; t < onglets_.size(); ++t)
    {
        auto ligne = colonne.removeFromTop (74);
        onglets_[t]->setBounds (ligne.removeFromTop (22).reduced (2, 0));
        if (t < faders_.size()) faders_[t]->setBounds (ligne.reduced (2));
        colonne.removeFromTop (4);
    }

    r.removeFromLeft (10);
    r.removeFromBottom (18);
    vue_.setBounds (r);

    // ⭐⭐ LA HAUTEUR VIENT DES GROUPES, ELLE N'EST PLUS DEVINEE.
    //
    // /!\ ELLE VALAIT 78, IL EN FALLAIT 102. Chaque groupe recevait donc 12
    // pixels de moins que necessaire, ses boutons debordaient, et comme un
    // enfant est rogne par son parent, c'est la bande du bas qui disparaissait -
    // c'est-a-dire le LIBELLE. Un seul pixel sur treize restait visible.
    //
    // /!\ ET LE 78 ETAIT ECRIT DEUX FOIS : ici, et dans `surGlisse` pour
    // convertir un deplacement en nombre de crans. Les deux devaient rester
    // d'accord sans que rien ne l'impose - le nombre magique du piege 13.
    // Il est maintenant calcule, une fois, et partage.
    int hg = 0;
    for (auto* l : lignes_) hg = juce::jmax (hg, l->hauteurVoulue());
    hauteurLigne_ = juce::jmax (60, hg + 6);
    const int h = hauteurLigne_;

    contenu_.setSize (juce::jmax (200, vue_.getWidth() - 14),
                      h * lignes_.size() + 44);
    for (int i = 0; i < lignes_.size(); ++i)
        lignes_[i]->setBounds (0, i * h, contenu_.getWidth(), h - 6);
    bAjoute_.setBounds (0, h * lignes_.size() + 4, contenu_.getWidth(), 30);
}

// ------------------------------------------------------------------ MATRICE

// ----------------------------------------------------------- le cablage

namespace
{
    constexpr int kHautLigne = 26;
    constexpr int kHautChip  = 26;
    constexpr int kLargeNom  = 132;

    /** Les familles de destinations, pour que le menu soit lisible.
        /!\ Trente-huit entrees a plat, ce serait une liste ou l'on cherche.
        Les bornes suivent l'ordre de `tableDestinations()`. */
    struct Famille { const char* nom; int premier, dernier; };
    const Famille kFamilles[] =
    {
        { "principales",   1, 7 },
        { "analogique",    8, 12 },
        { "FM",           13, 25 },
        { "table d'ondes", 26, 27 },
        { "filtre",       28, 29 },
        { "lecture",      30, 31 },
        { "accord",       32, 33 },
        { "enveloppe",    34, 37 },
        // ⭐ Les effets. Ils sont GLOBAUX - une seule reverbe pour tout
        // l'instrument - mais ils sont dans le meme menu : obliger
        // l'utilisateur a savoir qu'il existe deux matrices serait lui faire
        // porter une contrainte d'implementation.
        { "effets (global)", romance::kNbDestinations,
                             romance::kNbDestinationsTotal - 1 },
    };
}

int Cablage::hauteurVoulue() noexcept
{
    // ⭐ Plus de rangee de pastilles ici : elle vit en bas de la fenetre, sur
    // TOUTES les pages. Le panneau y gagne la hauteur d'une ligne de plus.
    return 8 + romance::kNbRoutages * kHautLigne + 20;
}

juce::Colour Cablage::couleurSource (int src)
{
    // /!\ Les macros ont leur propre demi-teinte : ce ne sont pas des
    // modulateurs qui bougent tout seuls, c'est la main de l'utilisateur. Les
    // peindre comme les huit autres ferait croire qu'un patch respire alors
    // qu'il attend qu'on tourne un bouton.
    // ⭐ TROIS FAMILLES, TROIS TEINTES - et ce ne sont pas trois decorations.
    // Un modulateur bouge tout seul, une macro attend la main, une source de
    // JEU attend le clavier. Les peindre pareil ferait croire qu'un patch
    // respire alors qu'il ne bougera qu'a la prochaine note.
    if (src >= romance::kIdSourceTouche)
        return juce::Colour::fromHSV (0.50f, 0.45f,
                   0.95f - 0.10f * (float) (src - romance::kIdSourceTouche), 1.0f);
    if (src > romance::kNbMods)
        return juce::Colour::fromHSV (0.13f, 0.30f,
                   0.95f - 0.10f * (float) (src - romance::kNbMods), 1.0f);
    // /!\ Huit teintes ECARTEES sur le cercle, pas huit variations d'une meme :
    // le panneau ne sert a rien si l'oeil ne distingue pas deux cables.
    return juce::Colour::fromHSV ((float) ((src - 1) % 8) / 8.0f,
                                  0.55f, 0.95f, 1.0f);
}

Cablage::Cablage (RomanceProcessor& p) : proc_ (p)
{
    startTimerHz (12);
}

void Cablage::timerCallback() { repaint(); }

int Cablage::lis (const juce::String& id) const
{
    if (auto* v = proc_.params.getRawParameterValue (id)) return (int) std::round (v->load());
    return 0;
}

void Cablage::ecrit (const juce::String& id, float valeur)
{
    if (auto* par = proc_.params.getParameter (id))
        par->setValueNotifyingHost (par->convertTo0to1 (valeur));
}

juce::Rectangle<int> Cablage::rectLigne (int i) const
{
    return { 4, 8 + i * kHautLigne,
             juce::jmax (60, getWidth() - 8), kHautLigne - 3 };
}

/** La source armee vient de la bande du bas. Nulle part ailleurs. */
int Cablage::armee() const
{
    return (edit_ != nullptr) ? edit_->sourceArmee() : 1;
}

void Cablage::parentHierarchyChanged()
{
    edit_ = findParentComponentOfClass<RomanceEditor>();
}

int Cablage::ligneSous (juce::Point<int> p) const
{
    for (int i = 0; i < romance::kNbRoutages; ++i)
        if (rectLigne (i).contains (p)) return i;
    return -1;
}

void Cablage::ouvreMenuDestination (int ligne)
{
    juce::PopupMenu m;
    m.addItem (1, "- (debrancher)");

    // ⭐⭐ LES PLUS EFFICACES EN PREMIER, ET C'EST NOTRE AVANTAGE SUR PIGMENTS.
    //
    // Ils ont soixante-quatre sources et aucune idee de celles qui comptent ;
    // nous avons MESURE ce que chaque octet du profil change reellement, en
    // le poussant d'un extreme a l'autre (voir piege 35) :
    //
    //     filtre coupure   17,6 dB      chorus melange    2,8 dB
    //     filtre resonance  4,3 dB      env attaque      0,72 dB
    //     reverbe melange   4,0 dB      env relachement  0,34 dB
    //
    // /!\ QUATRE DE CES SEPT ENTREES SONT MESUREES, LES TROIS AUTRES NON -
    // « table position », « FM op1 niveau » et « morph » y sont parce qu'elles
    // sont LE GESTE de leur generateur (le balayage du PPG, l'indice de
    // modulation, le fondu de topologie). Les melanger sans le dire ferait
    // passer une opinion pour une mesure ; c'est ecrit ici pour que la
    // prochaine personne sache lesquelles verifier.
    {
        juce::PopupMenu sm;
        for (int k : { (int) romance::Dest::Coupure,
                       romance::destinationDeParam ("reso"),
                       romance::destinationDeParam ("tb_pos"),
                       romance::destinationDeParam ("fm1_niv"),
                       (int) romance::Dest::Morph,
                       romance::kPremiereDestGlobale + (int) romance::DestGlobale::ReverbeMelange - 1,
                       romance::kPremiereDestGlobale + (int) romance::DestGlobale::ChorusMelange - 1 })
            if (k > 0 && k < romance::kNbDestinationsTotal)
                sm.addItem (k + 1, romance::nomDestinationTotale (k));
        m.addSubMenu ("LES PLUS EFFICACES", sm);
        m.addSeparator();
    }

    for (const auto& f : kFamilles)
    {
        juce::PopupMenu sm;
        for (int k = f.premier; k <= f.dernier
                                && k < romance::kNbDestinationsTotal; ++k)
            sm.addItem (k + 1, romance::nomDestinationTotale (k));
        m.addSubMenu (f.nom, sm);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, ligne] (int choix)
        {
            if (choix <= 0) return;
            const auto r = "r" + juce::String (ligne + 1) + "_";
            const int dest = choix - 1;
            if (dest == 0) { ecrit (r + "src", 0.0f); ecrit (r + "dst", 0.0f); }
            else
            {
                ecrit (r + "dst", (float) dest);
                if (lis (r + "src") <= 0) ecrit (r + "src", (float) armee());
            }
            repaint();
        });
}

void Cablage::mouseDown (const juce::MouseEvent& e)
{
    const int l = ligneSous (e.getPosition());
    if (l < 0) return;

    const auto r = "r" + juce::String (l + 1) + "_";
    const auto ligne = rectLigne (l);

    // ⭐ LA MOITIE GAUCHE NOMME, LA MOITIE DROITE DOSE. Un panneau ou le meme
    // geste fait deux choses selon l'endroit est plus rapide qu'un panneau ou
    // chaque chose a son bouton - c'est le parti de Pigments, et il tient parce
    // que la separation est VISIBLE (le nom a gauche, la barre a droite).
    if (e.getPosition().x < ligne.getX() + kLargeNom)
    {
        // clic sur le nom : on branche, on change de destination, ou on
        // reaffecte la source armee a un cable existant
        if (lis (r + "src") > 0 && lis (r + "dst") > 0 && e.mods.isShiftDown())
        { ecrit (r + "src", (float) armee()); repaint(); return; }
        ouvreMenuDestination (l);
        return;
    }

    if (lis (r + "src") <= 0 || lis (r + "dst") <= 0) { ouvreMenuDestination (l); return; }
    cableSaisi_ = l;
    xDepart_ = e.getPosition().x;
    if (auto* v = proc_.params.getRawParameterValue (r + "dose"))
        doseDepart_ = v->load();
}

void Cablage::mouseDrag (const juce::MouseEvent& e)
{
    if (cableSaisi_ < 0) return;
    // 200 pixels pour toute la plage : assez fin pour viser, assez court pour
    // aller d'un extreme a l'autre sans lacher la souris.
    const float delta = (float) (e.getPosition().x - xDepart_) / 200.0f * 2.0f;
    ecrit ("r" + juce::String (cableSaisi_ + 1) + "_dose",
           juce::jlimit (-1.0f, 1.0f, doseDepart_ + delta));
    repaint();
}

void Cablage::mouseUp (const juce::MouseEvent&) { cableSaisi_ = -1; }

void Cablage::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int l = ligneSous (e.getPosition());
    if (l < 0) return;
    const auto r = "r" + juce::String (l + 1) + "_";
    ecrit (r + "src", 0.0f);
    ecrit (r + "dst", 0.0f);
    ecrit (r + "dose", 0.0f);
    repaint();
}

void Cablage::paint (juce::Graphics& g)
{
    g.setColour (facade::kFond.brighter (0.06f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);

    // ⭐ PLUS DE PASTILLES ICI. Elles vivent en bas de la fenetre, visibles sur
    // toutes les pages : les avoir aussi dans ce panneau faisait DEUX
    // selecteurs pour une seule notion, sur ATELIER comme sur MATRICE - et
    // rien ne garantissait qu'ils designent la meme source.

    // ---- les huit cables
    // ⭐ UNE LIGNE PAR CABLE, pas une ligne par destination : il y a trente-huit
    // destinations et huit cables. Montrer les destinations vides serait montrer
    // trente lignes ou il ne se passe rien.
    for (int i = 0; i < romance::kNbRoutages; ++i)
    {
        const auto ligne = rectLigne (i);
        const auto r = "r" + juce::String (i + 1) + "_";
        const int src = lis (r + "src");
        const int dst = lis (r + "dst");
        const bool vide = (src <= 0 || dst <= 0);

        g.setColour (facade::kFond.darker (vide ? 0.10f : 0.25f));
        g.fillRoundedRectangle (ligne.toFloat(), 3.0f);

        auto zoneNom = ligne.withWidth (kLargeNom).reduced (6, 0);
        if (vide)
        {
            g.setColour (kMuted().withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("+ brancher " + juce::String (romance::nomSource (armee())),
                        ligne.reduced (8, 0), juce::Justification::centredLeft);
            continue;
        }

        const auto teinte = couleurSource (src);
        g.setColour (teinte);
        g.fillRoundedRectangle ((float) ligne.getX() + 4.0f,
                                (float) ligne.getY() + 5.0f, 4.0f,
                                (float) ligne.getHeight() - 10.0f, 2.0f);

        const int couche = lis (r + "couche");
        g.setColour (facade::kTexteFort);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (juce::String (romance::nomSource (src))
                      + " -> " + romance::nomDestinationTotale (dst)
                      // ⭐ LE MEME CHAMP DIT DEUX CHOSES SELON LA DESTINATION,
                      // et il faut donc l'ECRIRE differemment. Sur une
                      // destination de couche, `couche` restreint a une couche
                      // (0 = toutes) ; sur un effet, il choisit le RACK
                      // (0 = master). Afficher « c0 » dans les deux cas ferait
                      // lire « toutes » la ou il faut lire « master ».
                      + (dst >= romance::kNbDestinations
                          ? (couche == 0 ? juce::String (" [master]")
                                         : " [rack " + juce::String (couche) + "]")
                          : (couche == 0 ? juce::String()
                                         : " c" + juce::String (couche))),
                    zoneNom.withTrimmedLeft (6), juce::Justification::centredLeft);

        // ⭐ LA DOSE EST UNE BARRE PARTANT DU CENTRE, et son signe se voit :
        // une modulation negative n'est pas une petite modulation.
        const float dose = proc_.params.getRawParameterValue (r + "dose")->load();
        auto zone = ligne.withTrimmedLeft (kLargeNom).reduced (4, 4);
        g.setColour (facade::kFond.darker (0.5f));
        g.fillRoundedRectangle (zone.toFloat(), 2.0f);

        const float centre = (float) zone.getCentreX();
        const float demi = (float) zone.getWidth() * 0.5f - 2.0f;
        juce::Rectangle<float> barre (juce::jmin (centre, centre + demi * dose),
                                      (float) zone.getY() + 1.0f,
                                      std::abs (demi * dose),
                                      (float) zone.getHeight() - 2.0f);
        g.setColour (teinte.withAlpha (0.85f));
        g.fillRect (barre);
        g.setColour (facade::kTrait);
        g.drawVerticalLine ((int) centre, (float) zone.getY(),
                            (float) zone.getBottom());
    }

    g.setColour (message_.isEmpty() ? kMuted() : facade::kAccent3);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (message_.isEmpty()
                  ? "source en haut (8 emplacements + 4 macros) - clic a gauche : "
                    "destination - "
                    "glisser a droite : dose - maj+clic : changer de source - "
                    "double-clic : retirer"
                  : message_,
                getLocalBounds().removeFromBottom (18).reduced (8, 0),
                juce::Justification::centredLeft);
}

PageMatrice::PageMatrice (RomanceProcessor& p)
{
    cablage_ = std::make_unique<Cablage> (p);
    addAndMakeVisible (cablage_.get());

    auto& s = p.params;

    // --- les huit emplacements : chacun est une SOURCE de nature libre
    for (int i = 0; i < romance::kNbMods; ++i)
    {
        const juce::String m = "m" + juce::String (i + 1) + "_";
        auto* g = new Groupe ("MOD " + juce::String (i + 1), kAccent3);
        g->colonnes = 7;
        g->ajoute (new Menu (s, m + "type", "nature"));
        // /!\ Les noms changent de sens selon la nature choisie : p1 est une
        // vitesse pour un LFO et une attaque pour une enveloppe. On l'ecrit
        // dans l'etiquette plutot que de faire semblant que c'est la meme chose.
        g->ajoute (new Bouton (s, m + "p1", "vitesse / att", kAccent3));
        g->ajoute (new Bouton (s, m + "p2", "forme / dec", kAccent3));
        g->ajoute (new Bouton (s, m + "p3", "symetrie / tenue", kAccent3));
        g->ajoute (new Bouton (s, m + "p4", "relachement", kAccent3));
        g->ajoute (new Bascule (s, m + "retrig", "redecl."));
        emplacements_.add (g);
        contG_.addAndMakeVisible (g);
    }

    // --- les huit lignes de cablage
    vueG_.setViewedComponent (&contG_, false);
    vueD_.setViewedComponent (&contD_, false);
    vueG_.setScrollBarsShown (true, false);
    vueD_.setScrollBarsShown (true, false);
    addAndMakeVisible (vueG_);
    addAndMakeVisible (vueD_);

    for (int i = 0; i < romance::kNbRoutages; ++i)
    {
        const juce::String r = "r" + juce::String (i + 1) + "_";
        auto* g = new Groupe ("LIEN " + juce::String (i + 1), kAccent2);
        g->colonnes = 7;
        g->ajoute (new Bouton (s, r + "src", "source", kAccent2));
        g->ajoute (new Menu (s, r + "dst", "vers"));
        g->ajoute (new Bouton (s, r + "couche", "couche", kAccent2));
        g->ajoute (new Bouton (s, r + "dose", "dose", kAccent2));
        routages_.add (g);
        contD_.addAndMakeVisible (g);
    }
}

void PageMatrice::paint (juce::Graphics& g)
{
    g.setColour (kMuted());
    g.setFont (juce::Font (10.5f));
    g.drawText ("une SOURCE peut alimenter plusieurs LIENS", 12, 2,
                420, 14, juce::Justification::centredLeft);
}

void PageMatrice::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (14);

    // ⭐ Le cablage dessine en HAUT, le tableau au-dessous. On lit avant
    // d'ecrire : le panneau dit qui va ou, le tableau sert aux reglages que le
    // dessin ne sait pas porter (la couche, la dose au millieme).
    cablage_->setBounds (r.removeFromTop (Cablage::hauteurVoulue()));
    r.removeFromTop (8);

    // deux colonnes : les sources a gauche, les cablages a droite
    auto gauche = r.removeFromLeft (r.getWidth() / 2);
    r.removeFromLeft (6);
    vueG_.setBounds (gauche);
    vueD_.setBounds (r);

    // /!\ 84 pixels et non 52 : en dessous, les boutons ronds disparaissent
    // completement - la page ne montrait plus que des etiquettes.
    const int h = 84;
    const int lg = juce::jmax (60, gauche.getWidth() - 14);
    const int ld = juce::jmax (60, r.getWidth() - 14);
    contG_.setSize (lg, romance::kNbMods * h);
    contD_.setSize (ld, romance::kNbRoutages * h);
    for (int i = 0; i < emplacements_.size(); ++i)
        emplacements_[i]->setBounds (0, i * h, lg, h - 4);
    for (int i = 0; i < routages_.size(); ++i)
        routages_[i]->setBounds (0, i * h, ld, h - 4);
}

// ------------------------------------------------------------- RESYNTHESE

void Vignette::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // ⭐ LA QUALITE EST RELATIVE A SA GENERATION. Une echelle absolue
    // (perte / 8) coloriait tout un arbre de la meme teinte : quand toutes les
    // branches valent 4,6 dB, il n'y a plus rien a comparer. Ce qui interesse,
    // c'est laquelle est meilleure ICI.
    const float q = (float) juce::jlimit (0.0, 1.0,
                        (pireDeLaGen > 1e-9) ? (1.0 - perte / pireDeLaGen) : 0.5);
    g.setColour (kPanneau.interpolatedWith (kAccent3, 0.15f + q * 0.45f));
    g.fillRoundedRectangle (r, 3.0f);

    // ---- l'enveloppe, dessinee
    if (aApercu)
    {
        auto z = r.reduced (4.0f, 3.0f);
        z.removeFromLeft (22.0f);                    // la place du nom de moteur
        juce::Path p;
        const float base = z.getBottom();
        p.startNewSubPath (z.getX(), base);
        for (int i = 0; i < romance::kApercu; ++i)
        {
            const float x = z.getX() + z.getWidth() * (float) i
                                     / (float) (romance::kApercu - 1);
            p.lineTo (x, base - z.getHeight() * juce::jlimit (0.0f, 1.0f, apercu[i]));
        }
        p.lineTo (z.getRight(), base);
        g.setColour (kTexteFort.withAlpha (0.55f));
        g.fillPath (p);
    }

    // ---- le moteur, en trois lettres. C'est l'information qui change tout
    // depuis que la recherche choisit son generateur : deux branches de meme
    // score peuvent etre un Juno et une cloche FM.
    static const char* kM[] = { "ROM", "ANA", "FM", "TBL" };
    g.setColour (kAccent.withAlpha (0.95f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (kM[juce::jlimit (0, 3, moteur)],
                getLocalBounds().reduced (3, 0).withWidth (22),
                juce::Justification::centredLeft);

    // le nombre reste, mais en petit et dans un coin : il sert a departager
    // deux formes qui se ressemblent, plus a decrire le son
    g.setColour (kTexte.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText (juce::String (perte, 1), getLocalBounds().reduced (4, 1),
                juce::Justification::bottomRight);

    if (choisi)      { g.setColour (kTexteFort);               g.drawRoundedRectangle (r, 3.0f, 2.0f); }
    else if (retenu) { g.setColour (kAccent.withAlpha (0.9f)); g.drawRoundedRectangle (r, 3.0f, 1.5f); }
    else             { g.setColour (kTrait);                   g.drawRoundedRectangle (r, 3.0f, 1.0f); }
}


PageResynthese::PageResynthese (RomanceProcessor& p) : proc_ (p)
{
    addAndMakeVisible (deroule_);

    for (auto* b : { &bLance_, &bStop_, &bAffine_, &bPose_ })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->addListener (this);
        addAndMakeVisible (b);
    }
    // ⭐⭐ L'ETAT DU RESEAU, A COTE DU BOUTON QUI LANCE LA RECHERCHE.
    //
    // /!\ ET IL EST ROUGE QUAND IL MANQUE. Le piege 40 est explicite : une
    // valeur de repli imprimee au milieu d'un rapport n'est pas un
    // avertissement, c'est du texte que personne ne lit. Ce qui se lit, c'est
    // une couleur qui detonne a l'endroit ou l'on clique.
    etatReseau_.setJustificationType (juce::Justification::centredLeft);
    etatReseau_.setFont (juce::Font (juce::FontOptions (10.5f)));
    {
        const bool ok = proc_.reseau.pret;
        etatReseau_.setText (juce::String (ok ? "" : "/!" "\  ") + proc_.diagnosticReseau(),
                             juce::dontSendNotification);
        etatReseau_.setColour (juce::Label::textColourId,
                               ok ? kTexte : juce::Colour (0xffd08a5a));
    }
    // /!\ ON DIT AUSSI OU LE METTRE. « aucun reseau » sans le chemin oblige a
    // aller lire le code pour savoir quoi faire.
    etatReseau_.setTooltip (RomanceProcessor::fichierReseau().getFullPathName());
    addAndMakeVisible (etatReseau_);

    coucheCible_.addItemList ({ "Couche A", "Couche B", "Couche C", "Couche D" }, 1);
    coucheCible_.setSelectedId (1, juce::dontSendNotification);
    coucheCible_.setColour (juce::ComboBox::backgroundColourId, kFond);
    coucheCible_.setColour (juce::ComboBox::textColourId, kTexteFort);
    addAndMakeVisible (coucheCible_);

    // /!\ Le temps de generation est un CHOIX de l'utilisateur, pas une
    // constante : c'est lui qui decide combien de patience il investit.
    budget_.addItemList ({ "rapide (600)", "normal (2000)",
                           "long (6000)", "tres long (20000)" }, 1);
    budget_.setSelectedId (2, juce::dontSendNotification);
    budget_.setColour (juce::ComboBox::backgroundColourId, kFond);
    budget_.setColour (juce::ComboBox::textColourId, kTexteFort);
    addAndMakeVisible (budget_);

    // ⭐ Les quatre cartes, une par generateur. Elles restent grises tant que
    // leur recherche n'est pas finie - on montre l'attente, pas un vide.
    static const char* kNomMoteur[4] = { "ROM", "ANALOGIQUE", "FM", "TABLE" };
    for (int m = 0; m < 4; ++m)
    {
        auto* b = cartes_.add (new juce::TextButton (kNomMoteur[m]));
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kMuted());
        b->addListener (this);
        addAndMakeVisible (b);
    }
    bDetail_.setClickingTogglesState (true);
    bDetail_.setColour (juce::TextButton::buttonColourId, kPanneau);
    bDetail_.setColour (juce::TextButton::buttonOnColourId, kAccent2.withAlpha (0.4f));
    bDetail_.setColour (juce::TextButton::textColourOffId, kMuted());
    bDetail_.setColour (juce::TextButton::textColourOnId, kTexteFort);
    bDetail_.addListener (this);
    addAndMakeVisible (bDetail_);

    vue_.setViewedComponent (&arbre_, false);
    vue_.setScrollBarsShown (true, false);
    addChildComponent (vue_);          // repliee par defaut
    startTimerHz (6);
}

bool PageResynthese::isInterestedInFileDrag (const juce::StringArray& f)
{
    for (const auto& n : f)
        if (n.endsWithIgnoreCase (".wav") || n.endsWithIgnoreCase (".aif")
            || n.endsWithIgnoreCase (".aiff") || n.endsWithIgnoreCase (".flac"))
            return true;
    return false;
}

void PageResynthese::filesDropped (const juce::StringArray& f, int, int)
{
    survol_ = false;
    for (const auto& n : f)
        if (proc_.geno.chargeCible (juce::File (n))) break;
    repaint();
}

void PageResynthese::buttonClicked (juce::Button* b)
{
    if (b == &bDetail_) { vue_.setVisible (bDetail_.getToggleState()); resized(); return; }

    // ⭐ POSER UN GAGNANT. Un clic sur une carte pose CE profil-la : c'est le
    // geste central de la page, et il ne passe par aucun menu.
    for (int m = 0; m < cartes_.size(); ++m)
        if (cartes_[m] == b)
        {
            if (m >= snap_.pretsMoteur) return;        // pas encore pret
            const int c = juce::jlimit (0, romance::kNbCouches - 1,
                                        coucheCible_.getSelectedId() - 1);
            proc_.poseProfil (c, snap_.profils[m]);
            for (int k = 0; k < cartes_.size(); ++k)
                cartes_[k]->setColour (juce::TextButton::buttonColourId,
                                       (k == m) ? kAccent.withAlpha (0.35f) : kPanneau);
            return;
        }

    static const int budgets[4] = { 600, 2000, 6000, 20000 };
    const int bud = budgets[juce::jlimit (0, 3, budget_.getSelectedId() - 1)];

    if (b == &bLance_)  proc_.geno.lance (bud, proc_.configRecherche());
    else if (b == &bStop_) proc_.geno.arrete();
    else if (b == &bAffine_)
    {
        if (genChoisi_ >= 0 && genChoisi_ < (int) snap_.lignee.size()
            && idxChoisi_ >= 0
            && idxChoisi_ < (int) snap_.lignee[(size_t) genChoisi_].enfants.size())
            proc_.geno.affineDepuis (snap_.lignee[(size_t) genChoisi_]
                                        .enfants[(size_t) idxChoisi_],
                                     bud, proc_.configRecherche());
    }
    else if (b == &bPose_) poseNoeud (genChoisi_, idxChoisi_);
}

void PageResynthese::poseNoeud (int gen, int idx)
{
    const int c = juce::jlimit (0, romance::kNbCouches - 1,
                                coucheCible_.getSelectedId() - 1);
    if (gen < 0 || gen >= (int) snap_.lignee.size())
    {
        // aucun noeud choisi : on pose le meilleur profil trouve
        proc_.poseProfil (c, snap_.profil);
    }
    else
    {
        const auto& g = snap_.lignee[(size_t) gen];
        if (idx < 0 || idx >= (int) g.enfants.size()) return;
        proc_.poseProfil (c, g.enfants[(size_t) idx].profil);
    }
}

void PageResynthese::reconstruitArbre (const romance::Instantane& s)
{
    vignettes_.clear();
    const int lignes = (int) s.lignee.size();
    int y = 0;
    for (int gi = 0; gi < lignes; ++gi)
    {
        const auto& g = s.lignee[(size_t) gi];
        // /!\ La pire perte de la generation sert d'echelle. Sans elle, toutes
        // les vignettes d'un arbre convergent vers la meme couleur des que la
        // recherche a trouve quelque chose de correct.
        double pire = 1e-9;
        for (const auto& n : g.enfants) pire = juce::jmax (pire, n.perte);
        for (int e = 0; e < (int) g.enfants.size(); ++e)
        {
            auto* v = new Vignette();
            v->gen = gi; v->idx = e;
            v->perte = g.enfants[(size_t) e].perte;
            v->retenu = g.enfants[(size_t) e].retenu;
            for (int k = 0; k < romance::kApercu; ++k)
                v->apercu[k] = g.enfants[(size_t) e].apercu[k];
            v->aApercu = true;
            v->moteur = (int) g.enfants[(size_t) e].couche().moteur;
            v->pireDeLaGen = pire;
            v->choisi = (gi == genChoisi_ && e == idxChoisi_);
            v->surClic = [this] (int a, int b)
            {
                genChoisi_ = a; idxChoisi_ = b;
                poseNoeud (a, b);          // on l'entend tout de suite
                reconstruitArbre (snap_);
            };
            arbre_.addAndMakeVisible (v);
            vignettes_.add (v);
        }
        y += 38;
    }
    arbre_.setSize (juce::jmax (100, vue_.getWidth() - 12), juce::jmax (10, y));

    // placement
    int i = 0;
    for (int gi = 0; gi < lignes; ++gi)
    {
        const auto& g = s.lignee[(size_t) gi];
        const int n = juce::jmax (1, (int) g.enfants.size());
        const int w = juce::jmax (58, (arbre_.getWidth() - 60) / n);
        for (int e = 0; e < (int) g.enfants.size(); ++e, ++i)
            vignettes_[i]->setBounds (54 + e * w, gi * 38, w - 3, 34);
    }
    nGensAffichees_ = lignes;
    arbre_.repaint();
}

void Deroule::paint (juce::Graphics& g)
{
    using P = romance::PhaseRecherche;
    auto r = getLocalBounds().reduced (2);

    g.setColour (facade::kFond.brighter (0.05f));
    g.fillRoundedRectangle (r.toFloat(), 5.0f);

    static const char* kNoms[] = { "analyse", "hauteur", "moteur", "affinage" };
    const int etapeCourante =
          (phase == P::Analyse)  ? 0
        : (phase == P::Octave)   ? 1
        : (phase == P::Moteur)   ? 2
        : (phase == P::Affinage) ? 3 : -1;

    // ---- les quatre etapes, en chaine
    auto ligne = r.removeFromTop (34).reduced (10, 4);
    const int large = ligne.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto b = ligne.withX (ligne.getX() + i * large).withWidth (large - 8);
        const bool faite = (etapeCourante > i) || (etapeCourante < 0 && phase == P::Repos && false);
        const bool active = (etapeCourante == i);

        g.setColour (active ? facade::kAccent3.withAlpha (0.75f)
                            : faite ? facade::kAccent3.withAlpha (0.25f)
                                    : facade::kFond.brighter (0.13f));
        g.fillRoundedRectangle (b.toFloat(), 4.0f);

        // ⭐ L'ETAPE ACTIVE RESPIRE. Un point qui se deplace vaut mieux qu'une
        // barre de progression inventee : il dit « ca travaille » sans
        // pretendre savoir combien de temps il reste.
        if (active)
        {
            const float x = (float) b.getX() + 6.0f
                          + (float) ((tic * 3) % juce::jmax (1, b.getWidth() - 12));
            g.setColour (facade::kTexteFort.withAlpha (0.85f));
            g.fillEllipse (x, (float) b.getBottom() - 4.0f, 3.0f, 3.0f);
        }

        g.setColour (active ? facade::kTexteFort
                            : faite ? facade::kTexte : kMuted().withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (kNoms[i], b, juce::Justification::centred);
    }

    // ---- la ligne de detail : ce qu'on a trouve jusqu'ici
    auto bas = r.reduced (10, 2);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));

    if (phase == P::Repos)
    {
        g.setColour (kMuted());
        g.drawText ("depose un son, choisis un budget, et lance la recherche",
                    bas, juce::Justification::centredLeft);
        return;
    }

    juce::String detail;
    if (phase == P::Analyse)
        detail = "on cherche le debut du son et sa hauteur";
    else if (phase == P::Octave)
        detail = "cinq octaves mises en concurrence - la perte tranche"
               + (f0 > 0.0 ? " (autour de " + juce::String (f0, 1) + " Hz)"
                           : juce::String());
    else if (phase == P::Moteur)
        detail = "quatre generateurs essayes tour a tour sur "
               + juce::String (f0, 1) + " Hz";
    else
        detail = "on affine le generateur retenu - chaque branche est une version";

    g.setColour (facade::kTexte);
    g.drawText (detail, bas.removeFromTop (14), juce::Justification::centredLeft);

    // ---- les quatre moteurs et ce qu'ils ont donne
    static const char* kM[] = { "ROM", "ANA", "FM", "TBL" };
    auto zone = bas.removeFromTop (18);
    const int lm = zone.getWidth() / 4;
    double meilleure = 1e9;
    for (int m = 0; m < 4; ++m)
        if (perteMoteur[m] < 1e8) meilleure = juce::jmin (meilleure, perteMoteur[m]);

    for (int m = 0; m < 4; ++m)
    {
        auto b = zone.withX (zone.getX() + m * lm).withWidth (lm - 6);
        const bool essaye = perteMoteur[m] < 1e8;
        const bool gagnant = essaye && perteMoteur[m] <= meilleure + 1e-9;
        const bool encours = (phase == P::Moteur && moteurEssaye == m);

        g.setColour (gagnant ? facade::kAccent.withAlpha (0.55f)
                             : encours ? facade::kAccent3.withAlpha (0.45f)
                                       : facade::kFond.brighter (0.10f));
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (essaye || encours ? facade::kTexteFort : kMuted().withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (essaye ? (juce::String (kM[m]) + "  " + juce::String (perteMoteur[m], 2))
                           : juce::String (kM[m]),
                    b.reduced (4, 0), juce::Justification::centredLeft);
    }
}

void PageResynthese::timerCallback()
{
    // ⭐ Les cartes se remplissent AU FUR ET A MESURE : la premiere est
    // jouable pendant que la quatrieme cherche encore. Attendre les quatre
    // ferait patienter devant un ecran mort.
    {
        const auto e = proc_.geno.lis();
        static const char* kNomMoteur[4] = { "ROM", "ANALOGIQUE", "FM", "TABLE" };
        int meilleur = -1; double mp = 1e9;
        for (int m = 0; m < e.pretsMoteur && m < 4; ++m)
            if (e.pertes[m] < mp) { mp = e.pertes[m]; meilleur = m; }
        for (int m = 0; m < cartes_.size(); ++m)
        {
            const bool pret = (m < e.pretsMoteur);
            juce::String t (kNomMoteur[m]);
            if (pret)              t << "\n" << juce::String (e.pertes[m], 2) << " dB";
            else if (m == e.moteurEnCours) t << "\ncherche...";
            else                   t << "\n-";
            if (cartes_[m]->getButtonText() != t) cartes_[m]->setButtonText (t);
            cartes_[m]->setColour (juce::TextButton::textColourOffId,
                                   pret ? (m == meilleur ? kTexteFort : kTexte) : kMuted());
        }
    }

    const auto s = proc_.geno.lis();
    deroule_.phase = s.phase;
    deroule_.moteurEssaye = s.moteurEssaye;
    deroule_.f0 = s.f0Vue;
    deroule_.progression = s.progression;
    for (int i = 0; i < 4; ++i) deroule_.perteMoteur[i] = s.perteMoteur[i];
    ++deroule_.tic;
    deroule_.repaint();
    const bool change = ((int) s.lignee.size() != nGensAffichees_);
    snap_ = s;
    if (change) reconstruitArbre (snap_);
    repaint();
}

/*  /!\ NOTE SUR CE MINUTEUR, parce que la faute etait ici et qu'elle est
    facile a refaire.

    `snap_ = s` LIBERE les vecteurs de l'instantane precedent. Tant que les
    vignettes ne portaient que des nombres copies, c'etait sans consequence.
    Le jour ou l'une d'elles a garde un POINTEUR vers `lignee`, ce minuteur est
    devenu une machine a produire des pointeurs pendants - douze par seconde.

    Regle : rien de ce qui survit a un tour de minuteur ne doit pointer dans
    `snap_`. Tout ce qu'une vignette affiche, elle le possede.
*/

void PageResynthese::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto haut = r.removeFromTop (96);

    g.setColour (survol_ ? kAccent.withAlpha (0.20f) : kPanneau);
    g.fillRoundedRectangle (haut.reduced (10).toFloat(), 5.0f);
    g.setColour (survol_ ? kAccent : kTrait);
    g.drawRoundedRectangle (haut.reduced (10).toFloat(), 5.0f, 1.0f);

    g.setColour (kTexteFort);
    g.setFont (juce::Font (12.0f));
    if (snap_.aUneCible)
        g.drawText ("cible : " + snap_.nomCible, haut.reduced (22, 14),
                    juce::Justification::topLeft);
    else
        g.drawText ("depose un son ici - il sera lu, analyse, resynthetise, puis JETE",
                    haut.reduced (22, 14), juce::Justification::topLeft);

    g.setColour (kMuted());
    g.setFont (juce::Font (10.5f));
    if (snap_.aUneCible)
    {
        juce::String l = "perte " + juce::String (snap_.perteCourante, 3) + " dB";
        if (snap_.perteDepart > 0.0)
            l += "   (amorce " + juce::String (snap_.perteDepart, 3) + ")";
        l += "   " + juce::String (snap_.evaluations) + " evaluations";
        g.drawText (l, haut.reduced (22, 14).withTrimmedTop (18),
                    juce::Justification::topLeft);
    }

    // barre de progression
    if (snap_.enCours)
    {
        auto b = haut.reduced (22, 0).withY (haut.getBottom() - 26).withHeight (6);
        g.setColour (kTrait);
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (kAccent3);
        g.fillRoundedRectangle (b.withWidth ((int) (b.getWidth() * snap_.progression))
                                  .toFloat(), 3.0f);
    }

    // legende de l'arbre
    g.setColour (kMuted());
    g.setFont (juce::Font (10.0f));
    for (int gi = 0; gi < nGensAffichees_ && gi < 60; ++gi)
    {
        const int y = vue_.getY() + gi * 30 - vue_.getViewPositionY() + 8;
        if (y > vue_.getY() && y < vue_.getBottom() - 8)
            g.drawText ("gen " + juce::String (gi + 1), 12, y, 46, 14,
                        juce::Justification::centredLeft);
    }
}

void PageResynthese::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (96);
    auto barre = r.removeFromTop (34).reduced (10, 4);
    bLance_.setBounds (barre.removeFromLeft (96));
    barre.removeFromLeft (5);
    bStop_.setBounds (barre.removeFromLeft (80));
    barre.removeFromLeft (5);
    budget_.setBounds (barre.removeFromLeft (120));
    barre.removeFromLeft (5);
    coucheCible_.setBounds (barre.removeFromLeft (92));
    barre.removeFromLeft (5);
    bAffine_.setBounds (barre.removeFromLeft (146));
    barre.removeFromLeft (5);
    bPose_.setBounds (barre.removeFromLeft (168));
    barre.removeFromLeft (10);
    etatReseau_.setBounds (barre);

    // ⭐ Le deroule est entre la barre et l'arbre : c'est la qu'on regarde en
    // attendant, et c'est la que l'arbre apparaitra.
    deroule_.setBounds (r.removeFromTop (Deroule::hauteurVoulue()).reduced (10, 4));

    // ⭐ LES QUATRE GAGNANTS OCCUPENT LA PLACE PRINCIPALE. L'arbre, lui, ne
    // s'ouvre que si on le demande.
    {
        auto bande = r.removeFromTop (74).reduced (10, 6);
        bDetail_.setBounds (bande.removeFromRight (64).reduced (0, 18));
        bande.removeFromRight (8);
        const int l = juce::jmax (60, bande.getWidth() / 4 - 6);
        for (int m = 0; m < cartes_.size(); ++m)
        {
            cartes_[m]->setBounds (bande.removeFromLeft (l));
            bande.removeFromLeft (6);
        }
    }

    if (bDetail_.getToggleState())
    {
        vue_.setBounds (r.reduced (10, 6));
        if (nGensAffichees_ > 0) reconstruitArbre (snap_);
    }
}

// ------------------------------------------------------------------ ATELIER

PageAtelier::PageAtelier (RomanceProcessor& p) : proc_ (p)
{
    // B - le bandeau des couches. Une seule est developpee a la fois : retour
    // de l'utilisateur, « couche D dans atelier ne sert a rien » quand les
    // quatre sont affichees en meme temps.
    static const char* noms[] = { "A", "B", "C", "D" };
    for (int c = 0; c < kNbCouches; ++c)
    {
        auto* b = new juce::TextButton (noms[c]);
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->addListener (this);
        onglets_.add (b);
        contenu_.addAndMakeVisible (b);
    }
    // ⭐ Copier une couche vers une autre. Ca n'existait pas, et refaire a la
    // main quarante reglages pour obtenir une couche jumelle desaccordee est
    // exactement le genre de corvee qui decourage d'essayer.
    for (auto* b : { &bCopie_, &bColle_ })
    {
        b->addListener (this);
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        contenu_.addAndMakeVisible (b);
    }

    onde_ = std::make_unique<Onde> (p, 0);
    contenu_.addAndMakeVisible (onde_.get());

    // ⭐ LE CABLAGE EST ICI AUSSI - c'est la demande : « accessibles dans
    // l'onglet atelier ». On regle une couche et on cable ce qui la fait
    // bouger sans changer de page.
    grille_ = std::make_unique<Grille> (p, 0);
    contenu_.addChildComponent (grille_.get());

    // ⭐ Les bandes de spectre : visibles quand la SOURCE est « Spectre
    // dessine », independamment du moteur. C'est un materiau, pas un
    // generateur - il se lit comme n'importe quelle ROM.
    spectre_ = std::make_unique<BandesSpectre> (p, 0);
    contenu_.addChildComponent (spectre_.get());

    // ⭐ La courbe est REPLIEE par defaut : c'est une fenetre qu'on ouvre quand
    // on en a besoin, pas une page de plus a faire defiler.
    bCourbe_.setClickingTogglesState (true);
    bCourbe_.setColour (juce::TextButton::buttonColourId, kPanneau);
    bCourbe_.setColour (juce::TextButton::buttonOnColourId, kAccent2.withAlpha (0.45f));
    bCourbe_.setColour (juce::TextButton::textColourOffId, kTexte);
    bCourbe_.setColour (juce::TextButton::textColourOnId, kTexteFort);
    bCourbe_.addListener (this);
    contenu_.addAndMakeVisible (bCourbe_);
    courbe_ = std::make_unique<CourbeFiltre> (p, 0);
    contenu_.addChildComponent (courbe_.get());

    cablage_ = std::make_unique<Cablage> (p);
    contenu_.addAndMakeVisible (cablage_.get());

    // /!\ La page DEFILE. Les groupes ont deja leur hauteur minimale lisible
    // (voir Groupe::hauteurCase) : ajouter le cablage sans defilement aurait
    // voulu dire les retrecir, donc refaire les boutons minuscules qu'on vient
    // de corriger.
    vue_.setViewedComponent (&contenu_, false);
    vue_.setScrollBarsShown (true, false);
    addAndMakeVisible (vue_);

    construitCouche (0);
    startTimerHz (8);
}

void PageAtelier::construitCouche (int c)
{
    // /!\  est un OwnedArray : le vider DETRUIT le groupe filtre et
    // son bouton morph. Garder des pointeurs dessus sans les remettre a zero,
    // c'est exactement le piege 22 - un pointeur vers un objet libere, relu par
    // le timer douze fois par seconde.
    filtre_ = nullptr; morph_ = nullptr;
    groupes_.clear();
    if (grille_ != nullptr) grille_->changeCouche (c);
    if (courbe_ != nullptr) courbe_->changeCouche (c);
    if (spectre_ != nullptr) spectre_->changeCouche (c);
    auto& s = proc_.params;
    const juce::String p = "c" + juce::String (c + 1) + "_";

    // A - le MATERIAU : d'ou vient le son.
    auto* mat = new Groupe ("MATERIAU", kAccent);
    mat->colonnes = 6;
    mat->ajoute (new Menu (s, p + "moteur", "moteur"));
    mat->ajoute (new Menu (s, p + "source", "source (ROM)"));
    mat->ajoute (new Bascule (s, p + "actif", "on"));
    mat->ajoute (new Bouton (s, p + "niveau", "niveau", kAccent));
    mat->ajoute (new Bouton (s, p + "sp_pente", "pente spectre", kAccent));
    mat->ajoute (new Bouton (s, p + "sp_att",   "sp attaque", kAccent));
    mat->ajoute (new Bouton (s, p + "sp_chute", "sp chute", kAccent));
    mat->ajoute (new Bouton (s, p + "sp_tenue", "sp tenue", kAccent));
    groupes_.add (mat);

    // ⭐ Le moteur ANALOGIQUE : six valeurs pour ce que la ROM decrit en
    // soixante-douze. C'est le moteur des Juno, OB-8 et Jupiter du corpus.
    auto* osc = new Groupe ("OSCILLATEURS (analogique)", kAccent);
    osc->colonnes = 8;
    osc->ajoute (new Menu (s, p + "va_f1", "osc 1"));
    osc->ajoute (new Menu (s, p + "va_f2", "osc 2"));
    osc->ajoute (new Bouton (s, p + "va_mix", "melange", kAccent));
    osc->ajoute (new Bouton (s, p + "va_semi", "accord", kAccent));
    osc->ajoute (new Bouton (s, p + "va_cents", "desaccord", kAccent));
    osc->ajoute (new Bouton (s, p + "va_larg", "largeur", kAccent));
    osc->ajoute (new Bouton (s, p + "va_sub", "sous-octave", kAccent));
    osc->ajoute (new Bouton (s, p + "va_bruit", "bruit", kAccent));
    groupes_.add (osc);

    // ⭐ Le moteur FM : les cloches, les metalliques, les pianos electriques.
    // Ce que l'additif decrit mal, la FM le decrit avec deux valeurs.
    auto* fm = new Groupe ("OPERATORS (FM)", kAccent2);
    fm->colonnes = 8;
    // Toujours visible : ce qui vaut pour les quatre operateurs a la fois.
    fm->ajoute (new Menu (s, p + "fm_algo", "algorithm"));
    fm->ajoute (new Bouton (s, p + "fm_ret",  "feedback", kAccent2));
    fm->ajoute (new Bouton (s, p + "fm_bris", "breakpoint", kAccent2));
    fm->ajoute (new Bouton (s, p + "fm_vit",  "rate scaling", kAccent2));
    // ⭐ Puis SEPT commandes par operateur, toujours aux sept memes places.
    for (int o = 1; o <= 4; ++o)
    {
        const juce::String q = p + "fm" + juce::String (o) + "_";
        fm->ajoute (new Bouton (s, q + "ratio", "ratio",     kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "niv",   "level",      kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "att",   "attack",     kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "dec",   "decay",       kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "ten",   "sustain",       kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "rel",   "release", kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "ech",   "lvl scaling", kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "des",   "detune",   kAccent2), o - 1);
        fm->ajoute (new Bouton (s, q + "vel",   "vel sens",     kAccent2), o - 1);
    }
    fm->reglePages ({ "1", "2", "3", "4" });
    groupes_.add (fm);

    // ⭐ Le moteur TABLE D'ONDES : PPG Wave, Prophet VS, transwaves du VFX.
    // Ce qu'il sait faire et que les autres ne savent pas : BALAYER.
    auto* tab = new Groupe ("TABLE D'ONDES", kAccent3);
    tab->colonnes = 6;
    tab->ajoute (new Menu (s, p + "tb_fam", "famille"));
    tab->ajoute (new Menu (s, p + "tb_lec", "lecture"));
    tab->ajoute (new Bouton (s, p + "tb_pos", "position", kAccent3));
    tab->ajoute (new Bouton (s, p + "tb_sym", "symetrie", kAccent3));
    tab->ajoute (new Menu (s, p + "gr_cell", "grille"));
    tab->ajoute (new Menu (s, p + "gr_bits", "bits"));
    groupes_.add (tab);

    // A - la LECTURE : comment la machine lit ce materiau. C'est ici que vit
    // le mecanisme generationnel (horloge variable contre taux fixe).
    auto* lec = new Groupe ("LECTURE", kAccent2);
    lec->colonnes = 8;
    lec->ajoute (new Bascule (s, p + "grain", "grain"));
    lec->ajoute (new Menu (s, p + "horloge", "horloge"));
    lec->ajoute (new Menu (s, p + "quant", "quantification"));
    lec->ajoute (new Bascule (s, p + "asservi", "filtre asservi"));
    lec->ajoute (new Bouton (s, p + "coup_rom", "bande", kAccent2));
    lec->ajoute (new Menu (s, p + "pente_rec", "pente"));
    lec->ajoute (new Bouton (s, p + "b_deb", "boucle debut", kAccent2));
    lec->ajoute (new Bouton (s, p + "b_fin", "boucle fin", kAccent2));
    groupes_.add (lec);

    // hauteur et unisson
    auto* hau = new Groupe ("HAUTEUR", kAccent);
    hau->colonnes = 4;
    hau->ajoute (new Bouton (s, p + "transpose", "transpose", kAccent));
    hau->ajoute (new Bouton (s, p + "accord", "accord fin", kAccent));
    hau->ajoute (new Bouton (s, p + "desac", "desaccord", kAccent));
    hau->ajoute (new Bouton (s, p + "largeur", "largeur", kAccent));
    groupes_.add (hau);

    // le filtre musical
    // ⭐ Le passe-haut est DANS le groupe filtre, en onglet : c'est le meme
    // etage de la voix, et le separer aurait suggere un module optionnel alors
    // qu'il est toujours la, sur les quatre moteurs.
    // ⭐⭐ PLUS D'ONGLETS `BAS / HAUT`. Ils faisaient croire a un CHOIX entre
    // les deux filtres alors qu'ils travaillent ensemble - le passe-haut
    // devant, le passe-bas derriere, en permanence. L'utilisateur a demande
    // « pouvoir filtrer le haut et le bas en meme temps » : c'etait deja le
    // cas, et c'est mon interface qui le cachait.
    auto* fil = new Groupe ("FILTRE", kAccent2);
    fil->colonnes = 9;
    fil->ajoute (new Menu (s, p + "topo", "topologie"));
    fil->ajoute (new Bouton (s, p + "coupure", "coupure bas", kAccent2));
    fil->ajoute (new Bouton (s, p + "reso", "reso bas", kAccent2));
    // /!\ `morph` n'agit QUE sur la topologie Z-Plane - le parametre s'appelle
    // meme « morph (Z-Plane seulement) ». Affiche en permanence, c'est un
    // bouton qui ne fait rien trois fois sur quatre.
    morph_ = new Bouton (s, p + "morph", "morph", kAccent2);
    fil->ajoute (morph_);
    filtre_ = fil;
    fil->ajoute (new Bouton (s, p + "suivi", "suivi clavier", kAccent2));
    fil->ajoute (new Bouton (s, p + "ph_coup", "coupure haut", kAccent2));
    fil->ajoute (new Bouton (s, p + "ph_reso", "reso haut", kAccent2));
    fil->ajoute (new Menu (s, p + "ph_pente", "pente haut"));
    groupes_.add (fil);

    // C - UNE ADSR PAR COUCHE, en clair. Retour de l'utilisateur : « adsr
    // comme pigments et serum ». Les emplacements de modulation servent a en
    // creer d'AUTRES et a les assigner ou l'on veut - ils ne remplacent pas
    // celle-ci.
    auto* env = new Groupe ("ENVELOPPE", kAccent3);
    env->colonnes = 4;
    env->ajoute (new Bouton (s, p + "att", "attaque", kAccent3));
    env->ajoute (new Bouton (s, p + "dec", "decroissance", kAccent3));
    env->ajoute (new Bouton (s, p + "sus", "tenue", kAccent3));
    env->ajoute (new Bouton (s, p + "rel", "relachement", kAccent3));
    groupes_.add (env);

    // les zones - modele ZEN-Core
    auto* zon = new Groupe ("ZONE", kAccent);
    zon->colonnes = 5;
    zon->ajoute (new Bouton (s, p + "note_min", "note min", kAccent));
    zon->ajoute (new Bouton (s, p + "note_max", "note max", kAccent));
    zon->ajoute (new Bouton (s, p + "velo_min", "velo min", kAccent));
    zon->ajoute (new Bouton (s, p + "velo_max", "velo max", kAccent));
    zon->ajoute (new Bouton (s, p + "pan", "panoramique", kAccent));
    zon->ajoute (new Bouton (s, p + "v_fondu", "fondu de zone", kAccent));
    zon->ajoute (new Bouton (s, p + "retard", "retard (tone delay)", kAccent));
    groupes_.add (zon);

    // ⭐⭐ LA VELOCITE. Elle ne faisait rien du tout jusqu'a aujourd'hui :
    // `accepte()` etait le seul endroit du plugin qui la lisait.
    auto* vel = new Groupe ("VELOCITE ET LECTURE", kAccent3);
    vel->colonnes = 5;
    vel->ajoute (new Bouton (s, p + "v_niv", "-> niveau", kAccent3));
    vel->ajoute (new Bouton (s, p + "v_coup", "-> coupure", kAccent3));
    vel->ajoute (new Bouton (s, p + "v_att", "-> attaque", kAccent3));
    vel->ajoute (new Bouton (s, p + "v_dep", "-> depart", kAccent3));
    vel->ajoute (new Bouton (s, p + "v_courbe", "courbe", kAccent3));
    vel->ajoute (new Bouton (s, p + "depart", "depart de lecture", kAccent));
    vel->ajoute (new Bouton (s, p + "fondu_b", "fondu de boucle", kAccent));
    groupes_.add (vel);

    for (auto* g : groupes_) contenu_.addAndMakeVisible (g);
    coucheVisible_ = c;
    moteurAffiche_ = -1;      // force la reevaluation
    if (onde_ != nullptr) onde_->changeCouche (c);
    for (int i = 0; i < onglets_.size(); ++i)
        onglets_[i]->setColour (juce::TextButton::buttonColourId,
                                (i == c) ? kAccent.withAlpha (0.35f) : kPanneau);
    resized();
}

void PageAtelier::majSpectreVisible()
{
    if (spectre_ == nullptr) return;
    const auto id = "c" + juce::String (coucheVisible_ + 1) + "_source";
    auto* p = proc_.params.getRawParameterValue (id.toRawUTF8());
    // /!\ C'est la SOURCE qui commande, pas le moteur : le spectre fabrique
    // une ROM, donc il vit sur le moteur ROM comme la recette.
    const bool actif = (p != nullptr) && ((int) p->load() == 10);
    if (actif != spectre_->isVisible()) { spectre_->setVisible (actif); resized(); }
}

void PageAtelier::majMorphVisible()
{
    if (filtre_ == nullptr || morph_ == nullptr) return;
    const auto id = "c" + juce::String (coucheVisible_ + 1) + "_topo";
    auto* p = proc_.params.getRawParameterValue (id.toRawUTF8());
    const int t = (p != nullptr) ? (int) p->load() : 0;
    filtre_->masque (morph_, t != 3);      // 3 = Z-Plane
}

void PageAtelier::majMoteurVisible()
{
    if (groupes_.size() < 10) return;
    const auto id = "c" + juce::String (coucheVisible_ + 1) + "_moteur";
    auto* p = proc_.params.getRawParameterValue (id.toRawUTF8());
    const int m = (p != nullptr) ? (int) p->load() : 0;
    if (m == moteurAffiche_) return;
    moteurAffiche_ = m;

    // 1 = OSCILLATEURS, 2 = OPERATEURS FM, 3 = TABLE, 4 = LECTURE (ROM)
    groupes_[1]->setVisible (m == 1);
    groupes_[2]->setVisible (m == 2);
    groupes_[3]->setVisible (m == 3);
    groupes_[4]->setVisible (m == 0);
    if (grille_ != nullptr) grille_->setVisible (m == 3);
    majMorphVisible();
    // la source de ROM n'a de sens que sur le moteur ROM
    resized();
}

void PageAtelier::buttonClicked (juce::Button* b)
{
    if (b == &bCourbe_)
    {
        if (courbe_ != nullptr) courbe_->setVisible (bCourbe_.getToggleState());
        resized();
        return;
    }
    if (b == &bCopie_) { proc_.copieCouche (coucheVisible_); return; }
    if (b == &bColle_) { proc_.colleCouche (coucheVisible_); return; }
    for (int i = 0; i < onglets_.size(); ++i)
        if (onglets_[i] == b && i != coucheVisible_) { construitCouche (i); return; }
}

void PageAtelier::paint (juce::Graphics& g)
{
    g.setColour (kTexte);
    g.setFont (juce::Font (11.0f));
    g.drawText ("COUCHE", 16, 14, 60, 16, juce::Justification::centredLeft);
}

void PageAtelier::resized()
{
    vue_.setBounds (getLocalBounds());

    // FM : 5 cases fixes + 7 cases d'operateur sur 8 colonnes = 2 lignes.
    // TABLE : deux lignes aussi, plus la grille dessinable en dessous.
    const int hMoteur = (moteurAffiche_ == 2) ? 158
                      : (moteurAffiche_ == 3) ? 158 : 96;
    const int hGrille = (moteurAffiche_ == 3) ? (kHautGrille + 6) : 0;
    const int hSpectre = (spectre_ != nullptr && spectre_->isVisible())
                            ? (kHautSpectre + 6) : 0;
    const int hCourbe = (courbe_ != nullptr && bCourbe_.getToggleState())
                            ? (kHautCourbe + 6) : 0;
    const int total = 12 + 30 + 8 + 74 + 10 + 96 + 8 + hMoteur + hGrille + hSpectre + 6 + 96
                    + hCourbe + 6 + 96 + 6 + 96 + 12 + Cablage::hauteurVoulue() + 12;
    contenu_.setSize (juce::jmax (200, vue_.getWidth() - 14), total);

    auto r = contenu_.getLocalBounds().reduced (12);
    auto bandeau = r.removeFromTop (30);
    bandeau.removeFromLeft (66);
    for (int i = 0; i < onglets_.size(); ++i)
        onglets_[i]->setBounds (bandeau.getX() + i * 44, bandeau.getY(), 40, 24);
    bCopie_.setBounds (bandeau.getRight() - 150, bandeau.getY(), 70, 24);
    bColle_.setBounds (bandeau.getRight() - 76, bandeau.getY(), 70, 24);
    r.removeFromTop (8);
    if (onde_ != nullptr) onde_->setBounds (r.removeFromTop (74));
    r.removeFromTop (10);

    // /!\ Les groupes sont poses A LA MAIN et non dans une pile : leurs
    // hauteurs naturelles sont tres differentes (une ligne de boutons contre
    // deux lignes de menus). Une pile uniforme donnerait des cases vides en
    // haut et des menus ecrases en bas.
    if (groupes_.size() < 10) return;
    auto g1 = r.removeFromTop (96);
    groupes_[0]->setBounds (g1.removeFromLeft (r.getWidth() * 4 / 9));  // MATERIAU
    g1.removeFromLeft (8);
    groupes_[5]->setBounds (g1);                                        // HAUTEUR
    r.removeFromTop (8);

    // ⭐ UN SEUL GENERATEUR A LA FOIS. Les trois blocs occupent la MEME place :
    // celui qui n'est pas actif n'est pas juste grise, il n'est pas la. C'est
    // ce qui laissera de la place aux moteurs suivants sans que la page
    // devienne un mur.
    {
        auto zone = r.removeFromTop (hMoteur);
        r.removeFromTop (6);
        if (moteurAffiche_ == 1)      groupes_[1]->setBounds (zone);
        else if (moteurAffiche_ == 2) groupes_[2]->setBounds (zone);
        else if (moteurAffiche_ == 3) groupes_[3]->setBounds (zone);
        else                          groupes_[4]->setBounds (zone);
    }
    // ⭐ La grille prend TOUTE LA LARGEUR, sous le groupe table. Une grille
    // logee dans une case de la grille de commandes serait un timbre-poste :
    // on ne dessine pas dans quarante pixels.
    if (grille_ != nullptr && moteurAffiche_ == 3)
    {
        grille_->setBounds (r.removeFromTop (kHautGrille));
        r.removeFromTop (6);
    }

    // ⭐ Les bandes de spectre prennent aussi toute la largeur, sous le
    // groupe MATERIAU dont elles dependent.
    if (spectre_ != nullptr && spectre_->isVisible())
    {
        spectre_->setBounds (r.removeFromTop (kHautSpectre));
        r.removeFromTop (6);
    }

    auto g3 = r.removeFromTop (96);
    auto zf = g3.removeFromLeft (r.getWidth() * 5 / 9);
    // le bouton COURBE se pose DANS le coin du groupe filtre
    bCourbe_.setBounds (zf.getRight() - 74, zf.getY() + 5, 62, 16);
    groupes_[6]->setBounds (zf);                                        // FILTRE
    g3.removeFromLeft (8);
    groupes_[7]->setBounds (g3);                                        // ENVELOPPE
    r.removeFromTop (6);
    if (courbe_ != nullptr && bCourbe_.getToggleState())
    {
        courbe_->setBounds (r.removeFromTop (kHautCourbe));
        r.removeFromTop (6);
    }
    groupes_[8]->setBounds (r.removeFromTop (96));   // ZONE
    r.removeFromTop (6);
    if (groupes_.size() > 9) groupes_[9]->setBounds (r.removeFromTop (96)); // VELOCITE
    r.removeFromTop (12);
    if (cablage_ != nullptr)
        cablage_->setBounds (r.removeFromTop (Cablage::hauteurVoulue()));
}

// ------------------------------------------------------------------ editeur

RomanceEditor::RomanceEditor (RomanceProcessor& p)
    : juce::AudioProcessorEditor (&p), proc_ (p), jouer_ (p), atelier_ (p),
      matrice_ (p), fx_ (p), resynth_ (p)
{
    for (auto* b : { &bJouer_, &bAtelier_, &bMatrice_, &bFx_, &bResynth_ })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->setColour (juce::TextButton::textColourOnId, kTexteFort);
        b->addListener (this);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (jouer_);
    addChildComponent (atelier_);
    addChildComponent (matrice_);
    addChildComponent (fx_);
    addChildComponent (resynth_);
    for (auto* b : { &bPrec_, &bSuiv_, &bEnr_, &bOuvre_ })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanneau);
        b->setColour (juce::TextButton::textColourOffId, kTexte);
        b->addListener (this);
        addAndMakeVisible (b);
    }
    nomPreset_.setText ("(sans nom)", juce::dontSendNotification);
    nomPreset_.setColour (juce::Label::textColourId, kTexteFort);
    nomPreset_.setColour (juce::Label::backgroundColourId, kPanneau);
    nomPreset_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (nomPreset_);

    // ⭐⭐ LA BANDE DES SOURCES, EN BAS ET SUR TOUTES LES PAGES. C'est ce qui
    // fait qu'on cable sans changer de page : on attrape ici, on lache la-bas.
    bande_ = std::make_unique<BandeSources> (*this);
    addAndMakeVisible (bande_.get());

    bJouer_.setColour (juce::TextButton::buttonColourId, kAccent.withAlpha (0.35f));
    setSize (960, 748);
}

void RomanceEditor::buttonClicked (juce::Button* b)
{
    if (b == &bPrec_)  { voisinPreset (-1); return; }
    if (b == &bSuiv_)  { voisinPreset (+1); return; }
    if (b == &bEnr_)   { fichierPreset (true);  return; }
    if (b == &bOuvre_) { fichierPreset (false); return; }

    jouer_.setVisible   (b == &bJouer_);
    atelier_.setVisible (b == &bAtelier_);
    matrice_.setVisible (b == &bMatrice_);
    fx_.setVisible      (b == &bFx_);
    resynth_.setVisible (b == &bResynth_);
    for (auto* bt : { &bJouer_, &bAtelier_, &bMatrice_, &bFx_, &bResynth_ })
        bt->setColour (juce::TextButton::buttonColourId,
                       (bt == b) ? kAccent.withAlpha (0.35f) : kPanneau);
}

// ===========================================================================
//  ⭐⭐ LE CABLAGE PAR LE GESTE - ce qui rend la matrice lisible.
//
//  Trois fonctions, et elles repondent aux trois questions qu'on se pose
//  devant un synthe modulaire :
//    · `cablesDe`   - pourquoi ce bouton bouge ? (l'anneau)
//    · `brancheSur` - je veux que CETTE source pousse CE bouton (le depot)
//    · `signatureCablage` - est-ce que quelque chose a change ? (le rafraichi)
// ===========================================================================

namespace
{
    /** ⭐ DECOUPE UN IDENTIFIANT DE PARAMETRE EN (DESTINATION, COUCHE/RACK).

        Trois formes coexistent dans le plugin, et c'est le prix de ne pas
        avoir renomme les parametres du master le jour ou les racks de tranche
        sont arrives (voir la note dans `ajouteRack`) :

            c2_coupure      un reglage de la couche 2
            fx3_rev_mix     un effet du rack de la tranche 3
            rev_mix         le meme effet, sur le rack MASTER

        Rend faux si l'identifiant ne designe aucune destination - un bouton de
        forme d'onde, un menu, un interrupteur. */
    bool analyseIdParam (const juce::String& id, int& dest, int& couche)
    {
        dest = 0; couche = 0;
        if (id.length() > 3 && id[0] == 'c' && id[1] >= '1' && id[1] <= '4'
            && id[2] == '_')
        {
            const auto court = id.substring (3);
            const int d = romance::destinationDeParam (court.toRawUTF8());
            if (d <= 0) return false;
            dest = d; couche = id[1] - '0';
            return true;
        }
        if (id.startsWith ("fx") && id.length() > 4 && id[2] >= '1' && id[2] <= '4'
            && id[3] == '_')
        {
            const auto court = id.substring (4);
            const int g = romance::destGlobaleDeParam (court.toRawUTF8());
            if (g <= 0) return false;
            dest = romance::kPremiereDestGlobale + g - 1;
            couche = id[2] - '0';
            return true;
        }
        const int g = romance::destGlobaleDeParam (id.toRawUTF8());
        if (g <= 0) return false;
        dest = romance::kPremiereDestGlobale + g - 1;
        couche = 0;                       // le master
        return true;
    }

    /** L'amplitude que peut prendre une source, en base 1.

        /!\ UNE ENVELOPPE, UNE MACRO, LA VELOCITE ET LA PRESSION SONT
        UNIPOLAIRES. Dessiner leur anneau des deux cotes de la valeur courante
        montrerait une excursion vers le bas qui n'arrive jamais - et c'est
        precisement le detail qui rend un anneau credible ou decoratif. */
    void amplitudeSource (const juce::AudioProcessorValueTreeState& p, int src,
                          double& xMin, double& xMax)
    {
        xMin = -1.0; xMax = 1.0;
        if (src >= romance::kIdSourceTouche)
        {
            // la touche et le bend partent du centre ; les trois autres de zero
            if (src != romance::kIdSourceTouche && src != romance::kIdSourceBend)
                xMin = 0.0;
            return;
        }
        if (src > romance::kNbMods) { xMin = 0.0; return; }        // une macro
        auto* t = p.getRawParameterValue ("m" + juce::String (src) + "_type");
        if (t != nullptr && (int) t->load() == 1) xMin = 0.0;      // enveloppe
    }
}

bool RomanceEditor::peutRecevoir (const juce::String& idComplet) const
{
    int dest = 0, couche = 0;
    return analyseIdParam (idComplet, dest, couche);
}

int RomanceEditor::cablesDe (const juce::String& idComplet, Arrivee* sortie,
                             int maxi) const
{
    int dest = 0, couche = 0;
    if (! analyseIdParam (idComplet, dest, couche)) return 0;

    auto* brut = proc_.params.getRawParameterValue (idComplet);
    if (brut == nullptr) return 0;
    const auto plage = proc_.params.getParameterRange (idComplet);
    const double base = (double) brut->load();

    int n = 0;
    for (int i = 0; i < romance::kNbRoutages && n < maxi; ++i)
    {
        const auto r = "r" + juce::String (i + 1) + "_";
        auto lire = [this, &r] (const char* q) -> double
        {
            auto* v = proc_.params.getRawParameterValue (r + q);
            return v != nullptr ? (double) v->load() : 0.0;
        };
        const int src = (int) lire ("src");
        const int dst = (int) lire ("dst");
        const int cch = (int) lire ("couche");
        const double dose = lire ("dose");
        if (src <= 0 || dst != dest || std::abs (dose) < 1e-4) continue;

        // /!\ LA REGLE DE COUCHE N'EST PAS LA MEME DES DEUX COTES. Sur une
        // destination de couche, 0 veut dire TOUTES ; sur un effet, 0 veut dire
        // LE MASTER. Les traiter pareil ferait qu'un cable vers la reverbe
        // master allumerait aussi les anneaux des quatre racks de tranche.
        if (dest < romance::kNbDestinations) { if (cch != 0 && cch != couche) continue; }
        else                                 { if (cch != couche) continue; }

        double xMin = -1.0, xMax = 1.0;
        amplitudeSource (proc_.params, src, xMin, xMax);
        const double vA = romance::valeurModulee (dest, xMin * dose, base);
        const double vB = romance::valeurModulee (dest, xMax * dose, base);
        sortie[n].source = src;
        sortie[n].a = plage.convertTo0to1 ((float) juce::jlimit ((double) plage.start,
                                                (double) plage.end, vA));
        sortie[n].b = plage.convertTo0to1 ((float) juce::jlimit ((double) plage.start,
                                                (double) plage.end, vB));
        ++n;
    }
    return n;
}

bool RomanceEditor::brancheSur (const juce::String& idComplet, int source)
{
    int dest = 0, couche = 0;
    if (! analyseIdParam (idComplet, dest, couche)) return false;
    if (source <= 0 || source > romance::kNbSources) return false;

    for (int i = 0; i < romance::kNbRoutages; ++i)
    {
        const auto r = "r" + juce::String (i + 1) + "_";
        auto* s = proc_.params.getRawParameterValue (r + "src");
        auto* d = proc_.params.getRawParameterValue (r + "dst");
        if (s == nullptr || d == nullptr) continue;
        if ((int) s->load() > 0 && (int) d->load() > 0) continue;   // ligne prise

        auto pose = [this, &r] (const char* q, float v)
        {
            if (auto* p = proc_.params.getParameter (r + q))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        pose ("src", (float) source);
        pose ("dst", (float) dest);
        pose ("couche", (float) couche);

        // ⭐⭐ ET LA DOSE SE CALCULE, ELLE NE SE DEVINE PAS.
        //
        // /!\ C'EST LE PIEGE 17, ET IL EST FATAL ICI. Une dose « raisonnable »
        // de 0,3 vaut 7,2 DEMI-TONS sur la hauteur et 0,3 sur le panoramique :
        // poser la meme partout ferait que la moitie des depots produisent un
        // effet grotesque et l'autre moitie rien du tout. On cherche donc, par
        // dichotomie, la dose qui deplace le reglage d'UN QUART DE SA COURSE -
        // franchement audible, jamais absurde, et vrai quelle que soit l'unite.
        auto* brut = proc_.params.getRawParameterValue (idComplet);
        const auto plage = proc_.params.getParameterRange (idComplet);
        double dose = 0.30;
        if (brut != nullptr)
        {
            const double base = (double) brut->load();
            const float n0 = plage.convertTo0to1 ((float) base);
            // La cible s'eloigne du bord : sur un reglage deja a fond, pousser
            // vers le haut ne montrerait rien.
            const double signe = (n0 > 0.5f) ? -1.0 : 1.0;
            double lo = 0.0, hi = 1.0;
            for (int k = 0; k < 24; ++k)
            {
                const double m = 0.5 * (lo + hi);
                const double v = romance::valeurModulee (dest, signe * m, base);
                const float nv = plage.convertTo0to1 ((float) juce::jlimit (
                                     (double) plage.start, (double) plage.end, v));
                if (std::abs (nv - n0) < 0.25f) lo = m; else hi = m;
            }
            dose = signe * 0.5 * (lo + hi);
        }
        pose ("dose", (float) dose);
        return true;
    }
    return false;
}

juce::int64 RomanceEditor::signatureCablage() const
{
    juce::int64 h = 1469598103934665603LL;
    for (int i = 0; i < romance::kNbRoutages; ++i)
    {
        const auto r = "r" + juce::String (i + 1) + "_";
        for (const char* q : { "src", "dst", "couche", "dose" })
            if (auto* v = proc_.params.getRawParameterValue (r + q))
                h = (h ^ (juce::int64) (v->load() * 1000.0f)) * 1099511628211LL;
    }
    return h;
}

// ------------------------------------------------------- LA BANDE DES SOURCES

BandeSources::BandeSources (RomanceEditor& e) : edit_ (e)
{
    startTimerHz (6);
}

void BandeSources::timerCallback() { repaint(); }

juce::Rectangle<int> BandeSources::rect (int i) const
{
    const int n = romance::kNbSources;
    const int w = juce::jmax (26, (getWidth() - 8) / n);
    return { 4 + i * w, 3, w - 3, getHeight() - 6 };
}

int BandeSources::sous (juce::Point<int> p) const
{
    for (int i = 0; i < romance::kNbSources; ++i)
        if (rect (i).contains (p)) return i + 1;
    return 0;
}

void BandeSources::mouseMove (const juce::MouseEvent& e)
{
    const int s = sous (e.getPosition());
    if (s != edit_.sourceSurvolee()) { edit_.poseSourceSurvolee (s); repaint(); }
}

void BandeSources::mouseExit (const juce::MouseEvent&)
{
    edit_.poseSourceSurvolee (0);
    repaint();
}

void BandeSources::mouseDown (const juce::MouseEvent& e)
{
    // ⭐ C'EST ICI, ET NULLE PART AILLEURS, QU'ON ARME UNE SOURCE. Le panneau
    // de cablage avait sa propre rangee de pastilles : deux selecteurs pour
    // une seule notion, affiches en meme temps sur ATELIER et sur MATRICE.
    const int i = sous (e.getPosition());
    if (i > 0) edit_.poseSourceArmee (i);
    edit_.poseSourceSurvolee (i);
    repaint();
}

void BandeSources::mouseDrag (const juce::MouseEvent& e)
{
    const int s = sous (e.getMouseDownPosition());
    if (s <= 0 || edit_.isDragAndDropActive()) return;
    // ⭐ L'image tiree est la PASTILLE elle-meme, a sa taille : on voit ce
    // qu'on transporte, et on vise avec.
    juce::Image img (juce::Image::ARGB, 74, 20, true);
    {
        juce::Graphics g (img);
        g.setColour (Cablage::couleurSource (s).withAlpha (0.9f));
        g.fillRoundedRectangle (0.0f, 0.0f, 74.0f, 20.0f, 4.0f);
        g.setColour (kFond);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (romance::nomSource (s), 0, 0, 74, 20,
                    juce::Justification::centred);
    }
    edit_.startDragging ("src:" + juce::String (s), this, juce::ScaledImage (img));
}

void BandeSources::paint (juce::Graphics& g)
{
    g.setColour (kFond.brighter (0.05f));
    g.fillRect (getLocalBounds());
    const int survolee = edit_.sourceSurvolee();
    for (int i = 0; i < romance::kNbSources; ++i)
    {
        const auto b = rect (i).toFloat();
        const auto teinte = Cablage::couleurSource (i + 1);
        const bool vif = (survolee == 0 || survolee == i + 1);
        g.setColour (teinte.withAlpha (vif ? 0.75f : 0.20f));
        g.fillRoundedRectangle (b, 3.0f);
        // ⭐ LA SOURCE ARMEE PORTE UN CADRE - c'est l'information que l'ancienne
        // rangee de pastilles portait, et il fallait la reprendre ici en la
        // retirant la-bas.
        if (edit_.sourceArmee() == i + 1)
        {
            g.setColour (kTexteFort);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.6f);
        }
        g.setColour (kFond.darker (0.8f).withAlpha (vif ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (i + 1 >= romance::kIdSourceTouche
                                                     ? 8.5f : 10.0f)));
        g.drawText (romance::nomSource (i + 1), b.toNearestInt(),
                    juce::Justification::centred);
    }
}

void RomanceEditor::chargePreset (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    juce::MemoryBlock bloc;
    if (! f.loadFileAsData (bloc)) return;
    // /!\ LE MEME CHEMIN QUE L'HOTE. `setStateInformation` sait deja recharger
    // les recettes, les dessins et les spectres ; un chargeur maison finirait
    // par en oublier un.
    proc_.setStateInformation (bloc.getData(), (int) bloc.getSize());
    presetCourant_ = f;
    nomPreset_.setText (f.getFileNameWithoutExtension(), juce::dontSendNotification);
}

void RomanceEditor::voisinPreset (int direction)
{
    // ⭐ On relit le DOSSIER a chaque fois : un preset copie a la main doit
    // apparaitre sans qu'on relance le plugin.
    auto fichiers = romance::Bibliotheque::dossierPresets().findChildFiles (
                        juce::File::findFiles, false,
                        "*" + romance::Bibliotheque::extensionPreset());
    if (fichiers.isEmpty()) return;
    fichiers.sort();

    int i = fichiers.indexOf (presetCourant_);
    // /!\ Si le preset courant n'est pas dans le dossier - jamais enregistre,
    // ou supprime entre-temps - on repart du premier plutot que de ne rien
    // faire : un bouton qui ne repond pas laisse croire a une panne.
    i = (i < 0) ? 0 : (i + direction);
    if (i < 0) i = fichiers.size() - 1;
    if (i >= fichiers.size()) i = 0;
    chargePreset (fichiers[i]);
}

void RomanceEditor::fichierPreset (bool enregistrer)
{
    const auto dossier = romance::Bibliotheque::dossierPresets();
    const auto ext = "*" + romance::Bibliotheque::extensionPreset();

    selecteur_ = std::make_unique<juce::FileChooser> (
        enregistrer ? "Enregistrer le preset" : "Ouvrir un preset",
        juce::File (dossier), ext);

    const int drapeaux = enregistrer
        ? (juce::FileBrowserComponent::saveMode
         | juce::FileBrowserComponent::warnAboutOverwriting)
        : (juce::FileBrowserComponent::openMode
         | juce::FileBrowserComponent::canSelectFiles);

    selecteur_->launchAsync (drapeaux,
        [this, enregistrer] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (enregistrer)
            {
                if (! f.hasFileExtension (romance::Bibliotheque::extensionPreset()))
                    f = f.withFileExtension (romance::Bibliotheque::extensionPreset());
                juce::MemoryBlock bloc;
                proc_.getStateInformation (bloc);
                if (f.replaceWithData (bloc.getData(), bloc.getSize()))
                {
                    presetCourant_ = f;
                    nomPreset_.setText (f.getFileNameWithoutExtension(),
                                        juce::dontSendNotification);
                }
            }
            else chargePreset (f);
        });
}

void RomanceEditor::paint (juce::Graphics& g)
{
    g.fillAll (kFond);
    g.setColour (kTexteFort);
    g.setFont (juce::Font (17.0f, juce::Font::bold));
    g.drawText ("ROMANCE", 16, 12, 200, 24, juce::Justification::centredLeft);
    g.setColour (kTrait);
    g.drawHorizontalLine (48, 0.0f, (float) getWidth());
}

void RomanceEditor::resized()
{
    auto r = getLocalBounds();
    auto haut = r.removeFromTop (48);

    // ⭐ La barre de presets, a gauche des onglets : le nom, deux fleches, et
    // les deux gestes qui touchent au disque.
    {
        auto p = haut.removeFromLeft (430).reduced (0, 12);
        p.removeFromLeft (110);                      // le titre ROMANCE
        bPrec_.setBounds (p.removeFromLeft (24));
        p.removeFromLeft (3);
        nomPreset_.setBounds (p.removeFromLeft (140));
        p.removeFromLeft (3);
        bSuiv_.setBounds (p.removeFromLeft (24));
        p.removeFromLeft (6);
        bOuvre_.setBounds (p.removeFromLeft (58));
        p.removeFromLeft (3);
        bEnr_.setBounds (p.removeFromLeft (92));
    }

    haut.removeFromRight (12);
    bResynth_.setBounds (haut.removeFromRight (108).reduced (0, 12));
    haut.removeFromRight (5);
    bFx_.setBounds (haut.removeFromRight (48).reduced (0, 12));
    haut.removeFromRight (5);
    bMatrice_.setBounds (haut.removeFromRight (86).reduced (0, 12));
    haut.removeFromRight (5);
    bAtelier_.setBounds (haut.removeFromRight (86).reduced (0, 12));
    haut.removeFromRight (5);
    bJouer_.setBounds (haut.removeFromRight (78).reduced (0, 12));
    // /!\ LA BANDE SE RESERVE SA PLACE AVANT LES PAGES. La poser apres
    // laisserait les pages passer dessous : elles occuperaient la hauteur
    // entiere et la bande recouvrirait leur derniere rangee de boutons.
    if (bande_ != nullptr) bande_->setBounds (r.removeFromBottom (28));
    jouer_.setBounds (r);
    atelier_.setBounds (r);
    matrice_.setBounds (r);
    fx_.setBounds (r);
    resynth_.setBounds (r);
}
