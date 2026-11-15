// =============================================================================
//  Macros.h - Les trois grands boutons de la page JOUER.
//
//  /!\ TROIS, ET CHACUNE ADOSSEE A UN MECANISME MESURE. La premiere version de
//  la facade proposait « Air » et « LoFi » : deux mots inventes qui ne
//  correspondaient a rien dans le moteur. Ici chaque macro tire sur des
//  mecanismes qu'on a mesures separement, et surtout sur des mecanismes
//  DISJOINTS - sinon deux boutons feraient la meme chose sous deux noms.
//
//    USURE     -> le CONVERTISSEUR : quantification.
//    GRAIN     -> l'HORLOGE : escalier de lecture, filtre asservi, bande.
//    MOUVEMENT -> l'INSTABILITE : wow, flutter, derive.
//
//  ⭐ Elles n'ECRASENT PAS les reglages, elles s'ajoutent par-dessus. Une macro
//  qui ecrit dans les parametres se bat avec la main de l'utilisateur et lui
//  fait perdre ses reglages des qu'il la bouge.
// =============================================================================
#pragma once

#include <algorithm>
#include "Moteur.h"

namespace romance
{

/** Les trois macros, appliquees PAR-DESSUS les reglages lus.

    /!\ Aucune ne se contente de deplacer un parametre : chacune tire sur
    plusieurs mecanismes a la fois, c'est ce qui fait qu'un seul bouton donne
    une impression et pas un reglage. Et elles restent DISJOINTES - Usure ne
    touche pas a l'horloge, Grain ne touche pas a la quantification.
*/
inline void appliqueMacros (ReglagesPatch& patch_, double usure,
                            double grain, double mouv) noexcept
{
    for (int c = 0; c < kNbCouches; ++c)
    {
        auto& r = patch_.couche[c];

        // --- USURE : le convertisseur. Mesure T2 : mu-law est 8,7 dB plus
        // bruyant que le lineaire 8 bits sur les signaux forts, et c'est
        // exactement ce qu'on veut entendre monter.
        if (usure > 0.01)
        {
            r.grain.actif = true;
            if (usure > 0.66)      r.grain.quant = Quant::Lin8;
            else if (usure > 0.20) r.grain.quant = Quant::MuLaw;
            else                    r.grain.quant = Quant::Lin12;
        }

        // --- GRAIN : l'horloge. L'escalier de lecture et le filtre asservi -
        // le mecanisme generationnel (Emulator II contre M1), pas un filtre
        // de plus. Mesure : kCutoffRatio 0,18 donne +0,96 dB d'aigu a +12
        // demi-tons et -1,01 dB a -24 ; c'est audible, on l'accentue.
        if (grain > 0.01)
        {
            r.grain.actif = true;
            r.grain.horloge = Horloge::Variable;
            r.grain.filtre  = Filtre::Asservi;
            // de la coupure reglee vers 0,07 : la bande se referme
            // /!\ Meme correction de dosage : la cible passe de 0,07 a 0,045
            // de l'horloge. A 0,07 sur une horloge de 32 kHz la bande se
            // refermait a 2,2 kHz - deja etroit, mais le trajet depuis 0,18
            // ne faisait que 1,89 dB de difference mesuree. A 0,045 le geste
            // s'entend sur toute sa course.
            r.grain.coupure = r.grain.coupure
                            + (0.045 - r.grain.coupure) * grain;
        }
    }

    // --- MOUVEMENT : l'instabilite. Mesure : a profondeur egale, le wow
    // (commun) ne fait PAS battre un unisson (0,0 dB) alors que la derive
    // (propre a chaque voix) le fait battre de 30,4 dB. La macro pousse donc
    // les deux, mais elles ne font pas la meme chose.
    if (mouv > 0.01)
    {
        // /!\ ELLE POUSSE CE QUI EXISTE VRAIMENT SUR CES MACHINES : le
        // vieillissement des cartes de voix (coupure, niveau), pas un desaccord
        // entre voix - l'horloge d'un echantillonneur est partagee. Voir la
        // correction de modele dans Instabilite.h.
        //
        // Coefficients : machine neuve = 1 % de tolerance, machine ancienne
        // ~ 8 % (condensateurs et trimmers qui ont derive). La macro parcourt
        // cet intervalle.
        auto& i = patch_.instab;
        i.actif = true;
        // /!\ DOSAGE INCHANGE, ET C'EST UNE DECISION MESUREE.
        //
        // Retour de l'utilisateur : « ce bouton ne fonctionne plus ». Mon
        // premier banc lui donnait 0,60 dB - sous le seuil de l'audible - et
        // j'ai double les coefficients. C'etait LE BANC qui avait tort : il
        // jouait UNE note pendant UNE seconde, alors que l'instabilite agit
        // entre les VOIX et sur la DUREE. Sur un accord de trois notes tenu
        // trois secondes, la macro rendait deja 1,97 dB.
        //
        // Les coefficients doubles n'achetaient que +0,2 dB pour une derive de
        // timbre de 18 % au lieu de 10 % - beaucoup de desordre pour rien. On
        // revient donc aux valeurs d'origine.
        i.deriveTimbre   += 0.075 * mouv;   // 2,5 % -> 10 %
        i.deriveNiveauDb += 0.55 * mouv;    // 0,12 dB -> 0,67 dB
        i.deriveCents    += 2.4 * mouv;     // reste discret : 0,6 -> 3 cents
        // le pleurage est un trait de BANDE : la macro ne l'invente pas si
        // l'utilisateur ne l'a pas demande.
        if (i.wowProfCents > 0.0)  i.wowProfCents  += 6.0 * mouv;
        if (i.flutProfCents > 0.0) i.flutProfCents += 2.5 * mouv;
    }
}


} // namespace romance
