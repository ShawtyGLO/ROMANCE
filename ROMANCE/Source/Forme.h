// =============================================================================
//  Forme.h - LE FORMAT D'UNE FORME D'ONDE ENREGISTREE.
//
//  ⭐ SEPARE DU STOCKAGE, ET C'EST LA REGLE DU PROJET. `Bibliotheque.h` sait ou
//  vivent les fichiers et depend de JUCE ; ce fichier-ci sait ce qu'il y a
//  DEDANS et n'en depend pas. Il se teste donc dans la suite principale, en
//  quelques secondes, sans hote et sans compiler le plugin.
//
//  /!\ UN FORMAT QU'ON NE PEUT PAS TESTER EST UN FORMAT QU'ON DECOUVRE CASSE LE
//  JOUR OU L'ON RECHARGE. Et ce jour-la, ce qui est perdu l'est deja.
//
//  ⭐ DU TEXTE, ET DE L'HEXADECIMAL PLUTOT QUE DU BASE64. Deux fois plus long,
//  et lisible : on voit les cellules, on les corrige a la main, on en genere
//  par script. Pour 128 octets, la place ne compte pas ; la lisibilite, si.
//
//  /!\ ET IL PORTE SON NUMERO DE VERSION. Une forme ecrite aujourd'hui doit se
//  relire dans un an. Sans ce numero, le premier changement de format rend
//  illisible tout ce qui a ete enregistre - et on ne s'en apercoit qu'apres.
// =============================================================================
#pragma once

#include <string>

#include "Dessin.h"

namespace romance
{

/** Serialise un dessin A/B en texte. */
inline std::string formeVersTexte (const DessinAB& d)
{
    auto hex = [] (const uint8_t* o, int n)
    {
        static const char* c = "0123456789abcdef";
        std::string t;
        for (int i = 0; i < n; ++i) { t += c[o[i] >> 4]; t += c[o[i] & 15]; }
        return t;
    };
    std::string t = "ROMANCE-FORME 1\n";
    t += "cellules " + std::to_string (d.a.nCellules) + "\n";
    t += "bits "     + std::to_string (d.a.bits) + "\n";
    t += "morph "    + std::string (d.bActif ? "1" : "0") + "\n";
    t += "A " + hex (d.a.cellule, kCellulesMax) + "\n";
    t += "B " + hex (d.b.cellule, kCellulesMax) + "\n";
    return t;
}

/** Relit un dessin A/B depuis le texte.

    /!\ ON NE MODIFIE `d` QUE SI TOUT EST LU. Remplir au fur et a mesure
    laisserait, sur un fichier tronque, un dessin a MOITIE remplace - donc un
    son qu'on n'a jamais fabrique et qu'on ne sait pas reproduire. Un echec doit
    laisser exactement ce qui etait la.
*/
inline bool formeDepuisTexte (const std::string& texte, DessinAB& d)
{
    if (texte.compare (0, 14, "ROMANCE-FORME ") != 0) return false;

    auto valHex = [] (char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    DessinAB tmp;
    bool aVuA = false;
    size_t deb = 0;
    while (deb <= texte.size())
    {
        size_t fin = texte.find('\n', deb);
        if (fin == std::string::npos) fin = texte.size();
        const std::string l = texte.substr (deb, fin - deb);
        deb = fin + 1;
        const size_t esp = l.find (' ');
        if (esp == std::string::npos) continue;
        const std::string cle = l.substr (0, esp);
        const std::string val = l.substr (esp + 1);

        if (cle == "cellules") tmp.a.nCellules = tmp.b.nCellules = std::atoi (val.c_str());
        else if (cle == "bits") tmp.a.bits = tmp.b.bits = std::atoi (val.c_str());
        else if (cle == "morph") tmp.bActif = (std::atoi (val.c_str()) != 0);
        else if (cle == "A" || cle == "B")
        {
            if ((int) val.size() < kCellulesMax * 2) return false;
            uint8_t* cible = (cle == "A") ? tmp.a.cellule : tmp.b.cellule;
            for (int i = 0; i < kCellulesMax; ++i)
            {
                const int h = valHex (val[(size_t) (i * 2)]);
                const int b = valHex (val[(size_t) (i * 2 + 1)]);
                if (h < 0 || b < 0) return false;
                cible[i] = (uint8_t) (h * 16 + b);
            }
            if (cle == "A") aVuA = true;
        }
    }
    if (! aVuA) return false;

    // /!\ On BORNE ce qu'on relit. Un fichier corrompu - ou modifie a la main
    // par curiosite - ne doit pas pouvoir poser une finesse de 300 cellules ou
    // une profondeur de 40 bits dans une structure qui n'en attend pas tant.
    tmp.a.nCellules = std::max (kCellulesMin, std::min (kCellulesMax, tmp.a.nCellules));
    tmp.b.nCellules = tmp.a.nCellules;
    tmp.a.bits = std::max (kBitsMin, std::min (kBitsMax, tmp.a.bits));
    tmp.b.bits = tmp.a.bits;
    d = tmp;
    return true;
}

} // namespace romance
