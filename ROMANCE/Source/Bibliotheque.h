// =============================================================================
//  Bibliotheque.h - OU VIVENT LES FORMES ET LES PRESETS.
//
//  ⭐⭐ DEUX BIBLIOTHEQUES, UN SEUL MECANISME. Une forme d'onde et un preset ne
//  contiennent pas la meme chose, mais ils se rangent, se nomment, se chargent
//  et se suppriment de la meme facon. Les ecrire deux fois aurait donne deux
//  interfaces differentes pour deux gestes identiques.
//
//  ⭐ DES FICHIERS, ET C'EST UN CHOIX DE L'UTILISATEUR. Une bibliotheque
//  interne serait plus simple a vivre - aucun chemin, aucune permission - mais
//  elle enferme : ce qu'on fabrique ne peut ni se sauvegarder, ni se donner, ni
//  se fabriquer par un script. Un fichier de 128 octets s'envoie dans un
//  message ; un etat interne, non.
//
//  /!\ LE FORMAT DES FORMES EST DU TEXTE, DELIBEREMENT. Soixante-quatre octets
//  auraient tenu dans un binaire deux fois plus petit, et personne n'aurait pu
//  l'ouvrir. Ici on lit ce qu'il y a dedans, on le corrige a la main, on le
//  genere par script - et quand une forme sonnera mal, on pourra REGARDER au
//  lieu d'instrumenter le plugin.
//
//  /!\ ET IL PORTE SON NUMERO DE VERSION. Une forme ecrite aujourd'hui doit se
//  relire dans un an. Sans ce numero, le premier changement de format rend
//  illisible tout ce qui a ete enregistre - et on ne s'en apercoit qu'apres.
// =============================================================================
#pragma once

#include <JuceHeader.h>

#include "Dessin.h"
#include "Forme.h"

namespace romance
{

/** Les dossiers de l'utilisateur, crees a la demande. */
struct Bibliotheque
{
    /** `Documents/ROMANCE/` - visible, sauvegardable, partageable. */
    static juce::File racine()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("ROMANCE");
        return d;
    }

    static juce::File dossierFormes()
    {
        auto d = racine().getChildFile ("formes");
        // /!\ On cree a la DEMANDE, pas au demarrage. Un plugin qui fabrique
        // des dossiers dans Documents des qu'on le charge est un plugin qui
        // salit, meme chez ceux qui ne s'en servent jamais.
        if (! d.exists()) d.createDirectory();
        return d;
    }

    static juce::File dossierPresets()
    {
        auto d = racine().getChildFile ("presets");
        if (! d.exists()) d.createDirectory();
        return d;
    }

    static juce::String extensionForme()  { return ".romwave"; }
    static juce::String extensionPreset() { return ".romance"; }
};

// ------------------------------------------------------- formes d'onde
//
// /!\ LE FORMAT EST DANS Forme.h, PAS ICI. Ce fichier sait OU vivent les
// fichiers ; Forme.h sait ce qu'il y a DEDANS et ne depend pas de JUCE - donc
// il se teste dans la suite principale. C'est la regle du projet, et je l'avais
// enfreinte en ecrivant les deux ensemble.

inline bool ecritForme (const juce::File& f, const DessinAB& d)
{
    return f.replaceWithText (juce::String (formeVersTexte (d)));
}

inline bool litForme (const juce::File& f, DessinAB& d)
{
    if (! f.existsAsFile()) return false;
    return formeDepuisTexte (f.loadFileAsString().toStdString(), d);
}

} // namespace romance
