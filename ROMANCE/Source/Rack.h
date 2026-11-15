// =============================================================================
//  Rack.h - UN RACK D'EFFETS, REUTILISABLE.
//
//  ⭐⭐ CINQ EXEMPLAIRES : un par couche, plus le master. C'est l'architecture
//  d'une console - une tranche par source, et un bus general - et c'est ce que
//  l'utilisateur a demande en montrant l'image d'un mixeur.
//
//  /!\ J'AVAIS ECARTE CETTE FORME EN INVOQUANT LE COUT DES EFFETS TEMPORELS,
//  SANS L'AVOIR MESURE. La mesure dit le contraire, et par un facteur dix :
//
//      chorus 128 ns/ech   ensemble 125   phaser 85   reverbe 77
//      transfert 29        delai 18       multifiltre 14   tonalite 7
//
//  Les dix effets reunis coutent 2,1 % du fil audio ; quatre chaines completes
//  en couteraient 8,5 %, face aux 73,7 % que prennent les voix. La reverbe, que
//  je presentais comme le verrou, coute 0,34 %. Il n'y avait pas de contrainte -
//  il y avait une supposition.
//
//  /!\ CE QUI COUTE VRAIMENT ICI, C'EST LA MEMOIRE : cinq jeux de lignes a
//  retard et de reseaux de reverbe. C'est le chiffre a surveiller si l'on veut
//  un jour plus de couches, pas le temps de calcul.
//
//  Ne depend que des trois fichiers d'effets.
// =============================================================================
#pragma once

#include <string>

#include "Effets.h"
#include "Effets2.h"
#include "Effets3.h"

namespace romance
{

/** Quatre tranches plus le master. Le DERNIER indice est le master. */
constexpr int kNbRacksFx = 5;

/** Les dix effets d'un rack, plus l'ordre dans lequel on les traverse.

    ⭐ L'ORDRE EST UNE DONNEE DU RACK, pas un reglage global. Deux tranches
    peuvent enchainer les memes effets dans deux ordres differents - c'est
    exactement ce qu'on fait sur une console, et c'est ce que le glisser-deposer
    de l'interface manipule.
*/
struct RackFx
{
    Chorus      chorus;
    Ensemble    ensemble;
    Delai       delai;
    Reverbe     reverbe;
    Saturation  saturation;
    Tonalite    tonalite;
    Phaser      phaser;
    MultiFiltre multiFiltre;
    Transfert   transfert;
    Compresseur compresseur;

    /** 0 = emplacement vide, 1..10 = l'effet a cet endroit de la chaine.

        L'ordre par defaut suit le trajet du signal dans une machine reelle :
        modulations, puis temps, puis etage de sortie. */
    int ordre[kEmplacementsFx] = { 1, 2, 3, 4, 5, 6, 0, 0, 0, 0 };

    void prepare (double fs)
    {
        chorus.prepare (fs);      ensemble.prepare (fs);
        delai.prepare (fs);       reverbe.prepare (fs);
        saturation.prepare (fs);  tonalite.prepare (fs);
        phaser.prepare (fs);      multiFiltre.prepare (fs);
        transfert.prepare (fs);   compresseur.prepare (fs);
    }

    /** ⭐ La chaine reellement traversee, dedoublonnee.

        /!\ LE DOUBLON EST LE PIEGE, et il l'est encore plus ici qu'avant : il
        n'y a qu'UNE instance de chaque effet PAR RACK. La traverser deux fois
        rebouclerait le delai sur sa propre sortie. `chaineFx` garde la
        premiere occurrence et ignore les suivantes. */
    inline void traite (double& g, double& d) noexcept
    {
        int chaine[kEmplacementsFx];
        const int n = chaineFx (ordre, kEmplacementsFx, chaine);
        for (int i = 0; i < n; ++i)
            switch (chaine[i])
            {
                case 1:  chorus.traite (g, d);      break;
                case 2:  ensemble.traite (g, d);    break;
                case 3:  delai.traite (g, d);       break;
                case 4:  reverbe.traite (g, d);     break;
                case 5:  saturation.traite (g, d);  break;
                case 6:  tonalite.traite (g, d);    break;
                case 7:  phaser.traite (g, d);      break;
                case 8:  multiFiltre.traite (g, d); break;
                case 9:  transfert.traite (g, d);   break;
                case 10: compresseur.traite (g, d); break;
                default: break;
            }
    }

    /** ⭐ Vrai si ce rack ne fait RIEN - aucun emplacement occupe.

        /!\ Ca vaut la peine de le demander : une tranche dont le rack est vide
        ne doit pas payer dix appels de fonction par echantillon. Les effets se
        court-circuitent deja a melange nul, mais l'appel lui-meme coute, et
        quatre racks vides sur quatre couches, c'est quarante appels par
        echantillon pour ne rien faire. */
    bool vide() const noexcept
    {
        for (int i = 0; i < kEmplacementsFx; ++i)
            if (ordre[i] > 0 && ordre[i] <= kEmplacementsFx) return false;
        return true;
    }
};

/** Les noms des effets, dans l'ordre des numeros d'emplacement.
    Sert au menu « remplir un emplacement » de l'interface. */
/** ⭐⭐ LE NOM DU PARAMETRE D'UN EMPLACEMENT - LA REGLE VIT ICI, ET NULLE PART
    AILLEURS.

    Cinq racks, et DEUX conventions de nommage :

        master (tranche 4)   fx1 .. fx10
        tranches 0..3        fx1_slot1 .. fx4_slot10

    /!\ L'ASYMETRIE EST DELIBEREE : le master porte ses identifiants
    d'origine, les tranches sont arrivees apres. Les renommer aurait casse tous
    les presets deja enregistres.

    /!\ ET ELLE A DEJA COUTE DEUX FOIS. Premiere fois : l'interface composait
    « prefixe + slot » pour tout le monde, donc le MASTER N'ACCEPTAIT AUCUN
    EFFET. Corrige a un endroit... et la deuxieme occurrence est restee dans le
    minuteur de la page FX, ou elle a fait bien pire : `getRawParameterValue
    ("slot1")` rendait nullptr, donc « emplacement vide », donc « le contenu a
    change », donc RECONSTRUCTION DU RACK A CHAQUE BATTEMENT. Le rack master
    etait detruit et refabrique plusieurs fois par seconde - on ne pouvait
    litteralement pas toucher un bouton, puisqu'il n'existait plus quand la
    souris arrivait dessus.

    C'est le piege 19 mot pour mot : un piege documente survit dans
    l'occurrence qu'on n'a pas relue. La regle est donc ecrite UNE FOIS, ici,
    sans JUCE - donc testable en deux secondes (controle 48). */
inline std::string idEmplacementFx (int tranche, int emplacement)
{
    const std::string n = std::to_string (emplacement + 1);
    return (tranche == kNbRacksFx - 1)
             ? ("fx" + n)
             : ("fx" + std::to_string (tranche + 1) + "_slot" + n);
}

inline const char* nomEffet (int e) noexcept
{
    static const char* k[11] = { "-", "Chorus", "Ensemble", "Delai", "Reverbe",
                                 "Saturation", "Tonalite", "Phaser",
                                 "Multifiltre", "Transfert", "Compresseur" };
    return k[(e >= 0 && e <= 10) ? e : 0];
}

} // namespace romance
