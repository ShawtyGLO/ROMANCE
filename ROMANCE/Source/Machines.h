// =============================================================================
//  Machines.h - Les saveurs de convertisseur, en un seul menu.
//
//  ⭐ CE MENU EXISTE POUR POUVOIR COMPARER. Tout le projet repose sur une these
//  qui s'ecoute : l'horloge variable et l'interpolation ne font pas le meme
//  son, et c'est une frontiere de generation (Emulator II / PPG d'un cote, M1
//  et la suite de l'autre). Cette these etait jusqu'ici enfouie dans cinq
//  reglages separes - horloge, quantification, coupure, ordre, taux. Personne,
//  pas meme moi, ne pouvait passer d'une saveur a l'autre pour verifier.
//
//  /!\ CE NE SONT PAS DES PRESETS DE SON. Une machine ne touche NI au materiau,
//  NI au filtre musical, NI aux effets : elle ne decrit que le CONVERTISSEUR.
//  On doit pouvoir changer de machine sans que le patch change de note, de
//  timbre ou d'enveloppe - sinon la comparaison ne veut rien dire, parce qu'on
//  comparerait deux sons au lieu de comparer deux convertisseurs.
//
//  ⭐⭐ ET « PROPRE » EST UNE MACHINE DE PLEIN DROIT. Demande de l'utilisateur :
//  « la possibilite de rendre le plugin completement clean, sans caractere
//  rompleur ». Ce n'est pas un mode degrade ni un outil de debug : c'est la
//  position ou l'instrument devient un synthetiseur ordinaire - quatre moteurs,
//  un filtre, une matrice - et ou l'on entend enfin ce que le grain apportait,
//  par son absence.
//
//  Les chiffres viennent de la documentation lue (voir la note Obsidian) :
//  fsRom, ordre du filtre et pente sont MESURES ou documentes, pas choisis.
// =============================================================================
#pragma once

#include <algorithm>

#include "Grain.h"

namespace romance
{

enum class Machine
{
    Propre,          // aucun grain - un synthe ordinaire
    EmulatorII,      // 1984 : 8 bits compande, horloge variable, SSM2045
    PPG,             // 1981 : 8 bits, horloge variable, filtre plus raide
    Akai,            // 1988 : 12 bits, horloge variable, 3 poles
    Fairlight,       // 1979 : 8 bits, horloge variable, tres basse
    Japonais,        // 1988+ : 16 bits, taux FIXE, interpolation
    NB
};

constexpr int kNbMachines = (int) Machine::NB;

inline const char* nomMachine (int i) noexcept
{
    static const char* n[] =
    {
        "Propre (aucun grain)",
        "Emulator II (1984, 8 bits mu-law)",
        "PPG Wave (1981, 8 bits)",
        "Akai S1000 (1988, 12 bits)",
        "Fairlight (1979, 8 bits, 24 kHz)",
        "Japonais (1988+, 16 bits, taux fixe)"
    };
    return (i >= 0 && i < kNbMachines) ? n[i] : n[0];
}

/** La configuration de convertisseur d'une machine.

    /!\ `coupure` est une FRACTION de l'horloge, pas un hertz : c'est ce qui
    fait que le filtre suit la note quand l'horloge est variable. La donner en
    hertz reviendrait a decrire une machine a taux fixe.
*/
inline ConfigGrain configMachine (Machine m) noexcept
{
    ConfigGrain c;
    switch (m)
    {
        case Machine::Propre:
            // ⭐ TOUT EST NEUTRALISE, y compris la quantification. Laisser une
            // quantification 16 bits « parce que ca ne s'entend pas » serait
            // une demi-mesure : on veut une position ou il n'y a RIEN a
            // entendre, pour que la comparaison soit franche.
            c.actif   = false;
            c.horloge = Horloge::Fixe;
            c.filtre  = Filtre::Fixe;
            c.quant   = Quant::Aucune;
            c.fsRom   = 48000.0;
            c.fsMachine = 48000.0;
            c.ordre   = 4;
            c.coupure = 0.45;
            break;

        case Machine::EmulatorII:
            // 27,7 kHz, 8 bits compandes, SSM2045 4 poles asservi a l'horloge
            c.actif = true;
            c.horloge = Horloge::Variable;
            c.filtre  = Filtre::Asservi;
            c.quant   = Quant::MuLaw;
            c.fsRom   = kFsRomEII;
            c.ordre   = 4;
            c.coupure = kCutoffRatio;
            break;

        case Machine::PPG:
            // /!\ Le PPG n'est PAS un lecteur d'echantillons : c'est une table
            // d'ondes. Ce qu'on emprunte ici est son convertisseur - 8 bits,
            // horloge variable - pas sa synthese, qui est le moteur Table.
            c.actif = true;
            c.horloge = Horloge::Variable;
            c.filtre  = Filtre::Asservi;
            c.quant   = Quant::Lin8;
            c.fsRom   = 26000.0;
            c.ordre   = 4;
            c.coupure = 0.20;
            break;

        case Machine::Akai:
            // S1000 : 12 bits, et surtout un filtre a 3 POLES - 18 dB/octave.
            // C'est la difference qui s'entend le plus avec l'Emulator : la
            // pente laisse passer davantage de repliement dans l'aigu.
            c.actif = true;
            c.horloge = Horloge::Variable;
            c.filtre  = Filtre::Asservi;
            c.quant   = Quant::Lin12;
            c.fsRom   = 44100.0;
            c.ordre   = 3;
            c.coupure = 0.22;
            break;

        case Machine::Fairlight:
            // CMI : 24 kHz, 8 bits. La plus basse, et de loin la plus marquee.
            c.actif = true;
            c.horloge = Horloge::Variable;
            c.filtre  = Filtre::Asservi;
            c.quant   = Quant::Lin8;
            c.fsRom   = 24000.0;
            c.ordre   = 4;
            c.coupure = 0.16;
            break;

        case Machine::Japonais:
            // ⭐ LA FRONTIERE DE GENERATION. Taux FIXE et interpolation : la
            // hauteur ne bouge plus l'horloge, donc le timbre ne change plus
            // avec la note. C'est exactement ce que le projet cherche a rendre
            // audible - et pourquoi ces machines sonnent « propres » a cote.
            c.actif = true;
            c.horloge = Horloge::Fixe;
            c.filtre  = Filtre::Fixe;
            c.quant   = Quant::Lin16;
            c.fsRom   = 32000.0;
            c.fsMachine = kFsMachineJP;
            c.ordre   = 4;
            c.coupure = 0.45;
            break;

        default: break;
    }
    return c;
}

} // namespace romance
