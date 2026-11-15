// =============================================================================
//  mes_impact.cpp - QUE CHANGE REELLEMENT CHACUNE DES 38 DESTINATIONS ?
//
//  ⭐⭐ POURQUOI CE BANC EXISTE. Le menu des destinations en compte trente-huit,
//  et rien ne disait lesquelles valent la peine. Pigments a le meme probleme et
//  ne peut pas le resoudre - ils n'ont pas d'oracle. Nous, si : on peut POUSSER
//  une destination d'un bout a l'autre et mesurer ce que la perte y voit.
//
//  Deux usages, et le second compte plus que le premier :
//    1. trier le menu de la facade sur des mesures plutot que sur une opinion ;
//    2. PONDERER LES MUTATIONS DE GENOPATCH. Une destination a 0,0 dB est une
//       dimension morte exactement comme l'octet de relachement du piege 35 -
//       elle consomme des evaluations sans rien apprendre.
//
//  /!\ ON NE FABRIQUE AUCUN CHEMIN DE RENDU NOUVEAU. Tout passe par
//  `rendProfil`, en ECRIVANT DANS LES OCTETS DU PROFIL : l'emplacement de
//  modulation 1 devient une enveloppe a attaque nulle et tenue pleine - donc
//  une source quasi CONSTANTE a 1 - et le premier cable pointe sur la
//  destination qu'on teste. Un banc qui aurait son propre moteur mesurerait
//  son propre moteur.
//
//  /!\ ET IL Y A UN TEMOIN : la destination 0 (« - »). Elle doit rendre
//  EXACTEMENT 0,000 dB. Si elle rend autre chose, ce n'est pas la destination
//  qu'on mesure mais le bruit du protocole, et tout le tableau est faux.
//
//  ⚠️⚠️ CE QUE CE BANC NE PEUT PAS VOIR - A LIRE AVANT D'UTILISER LE TABLEAU.
//
//  Sa source est une ENVELOPPE, parce que le format du profil impose
//  `routages[i].source = i + 1` : seuls les emplacements de modulation sont
//  cablables depuis un profil. Or une enveloppe vaut ZERO a l'instant du
//  note-on. Toute destination LUE AU DECLENCHEMENT - attaque, decroissance,
//  tenue, relachement, largeur d'unisson - rendra donc 0,000 dB ici, qu'elle
//  soit branchee ou non.
//
//  Ca s'est verifie de la pire facon : le jour ou l'on a corrige l'application
//  de la matrice pour qu'elle precede le declenchement (elle arrivait un pas
//  trop tard, donc jamais), ce banc a rendu des chiffres STRICTEMENT
//  IDENTIQUES - a la troisieme decimale, sur les trente-sept lignes. C'est le
//  piege 42 pour la troisieme fois : un banc incapable de voir l'effet qu'on
//  lui demande de juger. La correction est prouvee par le controle 45 de la
//  suite de tests, qui utilise la VELOCITE comme source - elle, non nulle au
//  note-on.
//
//  Donc : un 0,000 dans ce tableau veut dire « invisible A CE PROTOCOLE »,
//  jamais « destination morte ». Et `niveau` y est nul pour une troisieme
//  raison encore : l'empreinte normalise la crete de chaque trame, elle est
//  donc SOURDE a un changement de gain pur - par construction, pas par defaut.
//
//  Usage : mes_impact.exe [nProfils]
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "Modulation.h"
#include "Perte.h"
#include "Profil.h"
#include "Prior.h"

using namespace romance;

namespace
{
    const double kFs = 22050.0;
    const int    kN  = 22050;
    const double kF0 = 220.0;

    /** Pose l'emplacement 1 en enveloppe « toujours a 1 », et le cable 1 vers
        `dest` avec la dose voulue. */
    void poseCable (uint8_t* o, int dest, bool pleineDose)
    {
        using namespace profil;
        uint8_t* m = o + kOffMod;          // emplacement 1
        m[0] = 160;                        // 160 >> 6 == 2 -> Enveloppe
        m[1] = 0;                          // attaque : lg(0) = 1 ms
        m[2] = 0;                          // decroissance (non lue en env.)
        m[3] = 255;                        // TENUE = 1 -> la source vaut 1
        m[4] = 255;                        // relachement long

        uint8_t* c = o + kOffCable;        // cable 1
        // /!\ L'OCTET SE CALCULE DEPUIS L'INDICE, il ne s'ecrit pas en dur.
        // C'est le piege 13 : le jour ou la table change de taille, une
        // constante ecrite a la main se met a designer une autre destination
        // sans que rien ne le signale.
        c[0] = (uint8_t) std::min (255.0, ((double) dest + 0.5)
                                            * 256.0 / (double) kNbDestinations);
        c[1] = pleineDose ? 255 : 128;     // li(128) ~ 0, li(255) = +1
    }

    double impact (const uint8_t* base, int dest)
    {
        uint8_t a[kOctetsProfil], b[kOctetsProfil];
        for (int i = 0; i < kOctetsProfil; ++i) { a[i] = base[i]; b[i] = base[i]; }
        poseCable (a, dest, false);
        poseCable (b, dest, true);

        std::vector<double> ya, yb;
        rendProfil (a, ya, kN, kFs, kF0);
        rendProfil (b, yb, kN, kFs, kF0);
        const double p = perte (empreinte (ya, kFs, kPlancherPropre),
                                empreinte (yb, kFs, kPlancherPropre));
        return std::isfinite (p) ? p : 0.0;
    }
}

int main (int argc, char** argv)
{
    const int nProfils = (argc > 1) ? std::atoi (argv[1]) : 8;

    std::printf ("\nIMPACT DES DESTINATIONS - ce que chacune change vraiment\n");
    std::printf ("=======================================================\n");
    std::printf ("  %d profils tires, dose 0 contre dose +1\n\n", nProfils);

    std::vector<uint8_t> bases ((size_t) nProfils * kOctetsProfil);
    for (int t = 0; t < nProfils; ++t)
    {
        Des de ((unsigned int) (t * 15485863u + 17u));
        tireProfil (de, &bases[(size_t) t * kOctetsProfil]);
    }

    // ---- LE TEMOIN D'ABORD. Un banc dont le temoin n'est pas verifie AVANT le
    // resultat est un banc dont on ne saura pas si le resultat veut dire
    // quelque chose (invariant 10 du projet).
    double temoin = 0.0;
    for (int t = 0; t < nProfils; ++t)
        temoin += impact (&bases[(size_t) t * kOctetsProfil], 0);
    temoin /= (double) nProfils;
    std::printf ("  TEMOIN (destination « - ») : %.6f dB\n", temoin);
    if (temoin > 0.01)
    {
        std::printf ("  !! LE TEMOIN N'EST PAS NUL - le protocole bruite la mesure,\n"
                     "     aucun chiffre de ce tableau n'est utilisable.\n\n");
        return 2;
    }
    std::printf ("  -> nul, donc ce qui suit mesure bien la destination.\n\n");

    struct Ligne { int dest; double moyenne, pire; };
    std::vector<Ligne> lignes;
    for (int d = 1; d < kNbDestinations; ++d)
    {
        double s = 0.0, mx = 0.0;
        for (int t = 0; t < nProfils; ++t)
        {
            const double v = impact (&bases[(size_t) t * kOctetsProfil], d);
            s += v; mx = std::max (mx, v);
        }
        lignes.push_back ({ d, s / (double) nProfils, mx });
    }
    std::sort (lignes.begin(), lignes.end(),
               [] (const Ligne& a, const Ligne& b) { return a.moyenne > b.moyenne; });

    std::printf ("  %-20s %10s %10s %s\n", "destination", "moyenne", "max", "bouton");
    std::printf ("  ---------------------------------------------------------------\n");
    for (const auto& l : lignes)
        std::printf ("  %-20s %9.3f %10.3f  %s\n", nomDestination (l.dest),
                     l.moyenne, l.pire, idParamDestination (l.dest));

    // /!\ ET ON NOMME CE QUI NE SERT A RIEN. Une destination sous le seuil est
    // soit mal branchee, soit sans effet sur ce generateur-la : dans les deux
    // cas c'est une information, pas un detail de presentation.
    int morts = 0;
    for (const auto& l : lignes) if (l.pire < 0.05) ++morts;
    std::printf ("\n  %d destination(s) sous 0,05 dB MEME AU MAXIMUM sur %d profils.\n",
                 morts, nProfils);
    // ⚠️ ON NOMME LA LIMITE DU BANC ICI AUSSI, PAS SEULEMENT EN TETE DE
    // FICHIER. Un tableau se lit ; un commentaire de code, non. Sans cette
    // ligne, « 13 destinations mortes » finira par etre cite comme un
    // resultat - et cinq de ces treize marchent parfaitement.
    std::printf ("  /!\\ MAIS UN 0,000 ICI VEUT DIRE « INVISIBLE A CE PROTOCOLE ».\n"
                 "      La source est une ENVELOPPE, nulle au note-on : tout ce qui\n"
                 "      est lu AU DECLENCHEMENT (attaque, chute, tenue, relachement,\n"
                 "      largeur d'unisson) sortira a zero meme branche. Et `niveau`\n"
                 "      y est nul parce que l'empreinte normalise chaque trame -\n"
                 "      elle est sourde a un gain pur, par construction.\n"
                 "      Voir le controle 45 de la suite de tests.\n\n");
    return 0;
}
