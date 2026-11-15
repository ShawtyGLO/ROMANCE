#include "PluginProcessor.h"
#include "Bibliotheque.h"
#include <chrono>
#include "Prior.h"
#include "PluginEditor.h"

using namespace romance;

namespace
{
    juce::String pre (int c) { return "c" + juce::String (c + 1) + "_"; }
    juce::String nom (int c) { return "Couche " + juce::String (juce::CharPointer_UTF8 ("ABCD")).substring (c, c + 1) + " "; }

    // ROM par defaut, une par couche.
    // /!\ INVARIANT : une machine sans ROM est SILENCIEUSE. « Choisir D-50 et
    // construire son son » menerait au silence si rien ne jouait. Le plugin
    // embarque donc une ROM neutre - c'est le risque « plugin vide au demarrage »
    // sous une autre forme.
    /** Formes d'onde, generees BANDE LIMITEE au taux de la ROM.

        Une machine ne stocke pas une dent de scie mathematique, elle stocke ce
        que son convertisseur a laisse passer. On somme donc les harmoniques
        jusqu'a la Nyquist de la ROM : le repliement qu'on entendra ensuite
        viendra de la LECTURE (drop-sample), pas d'une forme mal fabriquee.
        Les deux ne sonnent pas pareil.
    */
    enum class Forme { Additif, Sinus, Triangle, Scie, Carre };

    double echForme (Forme f, double phase, int nHarm)
    {
        const double tau = 2.0 * juce::MathConstants<double>::pi;
        const double pi  = juce::MathConstants<double>::pi;
        double v = 0.0;
        switch (f)
        {
            case Forme::Sinus: return std::sin (tau * phase);
            case Forme::Triangle:
                for (int k = 1; k <= nHarm; k += 2)
                {
                    const double sg = (((k - 1) / 2) % 2 == 0) ? 1.0 : -1.0;
                    v += sg * std::sin (tau * (double) k * phase) / (double) (k * k);
                }
                return v * 8.0 / (pi * pi);
            case Forme::Carre:
                for (int k = 1; k <= nHarm; k += 2)
                    v += std::sin (tau * (double) k * phase) / (double) k;
                return v * 4.0 / pi;
            case Forme::Scie:
                for (int k = 1; k <= nHarm; ++k)
                    v += std::sin (tau * (double) k * phase) / (double) k;
                return v * 2.0 / pi;
            default: return 0.0;
        }
    }

    /** ROM d'une forme d'onde.

        LONGUEUR CALEE SUR UN NOMBRE ENTIER DE CYCLES. Sans ca la boucle claque
        a chaque tour - et on n'a volontairement pas mis de fondu de boucle dans
        le lecteur : c'est au MATERIAU d'etre juste, comme sur les machines.
    */
    void remplitRomForme (std::vector<double>& rom, Forme f)
    {
        const double f0 = 261.6255653;
        const double parCycle = kFsRomEII / f0;
        const int cycles = (int) std::round (0.25 * f0);
        const int n = (int) std::round ((double) cycles * parCycle);
        const int nHarm = (int) (0.45 * kFsRomEII / f0);
        rom.resize ((size_t) n);
        double crete = 1e-12;
        for (int i = 0; i < n; ++i)
        {
            const double ph = (double) i / parCycle;
            rom[(size_t) i] = echForme (f, ph - std::floor (ph), nHarm);
            crete = std::max (crete, std::abs (rom[(size_t) i]));
        }
        for (auto& v : rom) v *= 0.9 / crete;
    }

    /** Quatre recettes de depart, franchement differentes.

        Un instrument qui s'ouvre sur quatre fois le meme son ne donne envie de
        rien. Ce sont des points de depart honnetes, pas des presets d'usine -
        ceux-la viendront de la recherche.
    */
    romance::Recette recetteParDefaut (int couche)
    {
        romance::Recette r;
        r.f0 = 261.6255653;
        r.graine = (unsigned int) (1234 + couche * 977);
        switch (couche)
        {
            case 0:   // nappe : beaucoup de partiels, qui tiennent
                r.nPartiels = 20; r.dureeAttaque = 0.60; r.cyclesBoucle = 12;
                for (int k = 0; k < r.nPartiels; ++k)
                {
                    r.partiels[k].amplitude = std::pow ((double) (k + 1), -1.25);
                    r.partiels[k].decroiss  = 0.18 + 0.022 * (double) k;
                    r.partiels[k].desaccord = (k % 3 == 1) ? 4.0 : -3.0;
                    // les aigus GONFLENT : la nappe s ouvre au lieu de tomber
                    r.partiels[k].montee = (k >= 8) ? 2 : ((k >= 4) ? 1 : 0);
                }
                r.bruit[0].niveau = 0.06; r.bruit[0].coupure = 0.55;
                r.bruit[0].decroiss = 0.35;
                r.bruit[1].niveau = 0.03; r.bruit[1].coupure = 0.95;
                r.bruit[1].decroiss = 0.10;
                break;

            case 1:   // metallique : inharmonique, l'aigu part vite
                r.nPartiels = 16; r.dureeAttaque = 0.80; r.cyclesBoucle = 16;
                r.inharmonicite = 0.75;
                for (int k = 0; k < r.nPartiels; ++k)
                {
                    r.partiels[k].amplitude = std::pow ((double) (k + 1), -0.75);
                    r.partiels[k].decroiss  = 0.25 + 0.055 * (double) k;
                }
                r.transNiveau = 0.35; r.transDuree = 0.008; r.transCouleur = 0.85;
                r.bruit[1].niveau = 0.05; r.bruit[1].coupure = 0.98;
                r.bruit[1].decroiss = 0.55;
                break;

            case 2:   // orgue : partiels impairs, aucune decroissance
                r.nPartiels = 14; r.dureeAttaque = 0.12; r.cyclesBoucle = 6;
                for (int k = 0; k < r.nPartiels; ++k)
                {
                    r.partiels[k].amplitude = ((k % 2) == 0)
                                                ? std::pow ((double) (k + 1), -1.0) : 0.0;
                    r.partiels[k].decroiss  = 0.0;
                }
                break;

            default:  // piano electrique : un choc, puis ca s'eteint par le haut
                r.nPartiels = 12; r.dureeAttaque = 0.90; r.cyclesBoucle = 10;
                r.inharmonicite = 0.20;
                for (int k = 0; k < r.nPartiels; ++k)
                {
                    r.partiels[k].amplitude = std::pow ((double) (k + 1), -1.6);
                    r.partiels[k].decroiss  = 0.30 + 0.060 * (double) k;
                }
                r.transNiveau = 0.5; r.transDuree = 0.012; r.transCouleur = 0.6;
                r.bruit[0].niveau = 0.10; r.bruit[0].coupure = 0.7;
                r.bruit[0].decroiss = 0.85;
                // un vrai piano electrique monte legerement en hauteur au choc
                r.pitchProf = 1.2; r.pitchDuree = 0.025; r.pitchCourbe = 0.7;
                break;
        }
        return r;
    }

    void remplitRom (std::vector<double>& rom, int couche)
    {
        const double f0 = 261.6255653;
        const double parCycle = kFsRomEII / f0;
        const int cycles = (int) std::round (2.0 * f0);
        const int n = (int) std::round ((double) cycles * parCycle);
        rom.resize ((size_t) n);
        // profils harmoniques distincts, pour que les quatre couches ne soient
        // pas quatre fois le meme son
        const double pente[kNbCouches] = { 1.15, 1.70, 0.85, 2.40 };
        const bool   impairs[kNbCouches] = { false, false, true, false };
        double crete = 1e-12;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / kFsRomEII;
            double v = 0.0;
            for (int k = 1; k <= 16; ++k)
            {
                if (impairs[couche] && (k % 2) == 0) continue;
                const double f = 261.6255653 * (double) k;
                if (f >= 0.45 * kFsRomEII) break;
                v += std::pow ((double) k, -pente[couche])
                     * std::sin (2.0 * juce::MathConstants<double>::pi * f * t
                                 + 0.7 * (double) k);
            }
            rom[(size_t) i] = v;
            crete = std::max (crete, std::abs (v));
        }
        for (auto& v : rom) v *= 0.9 / crete;
    }
}

// ---------------------------------------------------------------- parametres

namespace
{
    using P  = juce::AudioParameterFloat;
    using PB = juce::AudioParameterBool;
    using PC = juce::AudioParameterChoice;
    using PI = juce::AudioParameterInt;
    using Liste = juce::AudioProcessorValueTreeState::ParameterLayout;

    /** ⭐⭐ LES PARAMETRES D'UN RACK, poses sous un prefixe.

        Appelee QUATRE fois - une par tranche. Le rack MASTER, lui, garde ses
        identifiants historiques sans prefixe.

        /!\ ET C'EST DELIBERE, meme si ca fait deux chemins. Renommer les
        parametres du master en `fx0_cho_mix` casserait tous les presets deja
        enregistres - y compris ceux de l'utilisateur - sans rien apporter. Le
        prix a payer est cette asymetrie, ecrite ici une fois, plutot qu'une
        migration d'etat a maintenir pour toujours.

        ⭐ ET ILS SONT MODULABLES DEPUIS LA MEME MATRICE. Le menu ne grossit
        pas pour autant : une destination globale reste UNE entree, et c'est le
        champ `couche` du cable qui dit sur QUEL rack elle agit (0 = master,
        1..4 = la tranche). Vingt-six destinations x cinq racks se cablent donc
        avec vingt-six entrees de menu au lieu de cent trente.
    */
    void ajouteRack (Liste& d, const juce::String& p, const juce::String& N)
    {
        d.add (std::make_unique<PB> (p + "cho", N + "Chorus actif", false));
        d.add (std::make_unique<PC> (p + "cho_nat", N + "Chorus nature",
                 juce::StringArray { "Seaux (BBD)", "Numerique" }, 0));
        d.add (std::make_unique<P>  (p + "cho_vit", N + "Chorus vitesse",
                 juce::NormalisableRange<float> (0.02f, 8.0f, 0.001f, 0.35f), 0.40f));
        d.add (std::make_unique<P>  (p + "cho_prof", N + "Chorus profondeur",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
        d.add (std::make_unique<P>  (p + "cho_mix", N + "Chorus melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));
        d.add (std::make_unique<P>  (p + "cho_coul", N + "Chorus couleur",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));

        d.add (std::make_unique<PB> (p + "ens", N + "Ensemble actif", false));
        d.add (std::make_unique<P>  (p + "ens_vit", N + "Ensemble vitesse",
                 juce::NormalisableRange<float> (0.05f, 4.0f, 0.001f, 0.5f), 0.6f));
        d.add (std::make_unique<P>  (p + "ens_prof", N + "Ensemble profondeur",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
        d.add (std::make_unique<P>  (p + "ens_mix", N + "Ensemble melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        d.add (std::make_unique<PB> (p + "del", N + "Delai actif", false));
        d.add (std::make_unique<P>  (p + "del_tps", N + "Delai temps (s)",
                 juce::NormalisableRange<float> (0.01f, 2.0f, 0.001f, 0.4f), 0.375f));
        d.add (std::make_unique<P>  (p + "del_rei", N + "Delai reinjection",
                 juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.35f));
        d.add (std::make_unique<P>  (p + "del_amo", N + "Delai amortissement",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
        d.add (std::make_unique<P>  (p + "del_pp", N + "Delai ping-pong",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "del_mix", N + "Delai melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        d.add (std::make_unique<PB> (p + "rev", N + "Reverbe active", false));
        d.add (std::make_unique<P>  (p + "rev_taille", N + "Reverbe taille",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "rev_amorti", N + "Reverbe amortissement",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "rev_mix", N + "Reverbe melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "rev_pre", N + "Reverbe pre-delai",
                 juce::NormalisableRange<float> (0.0f, 0.1f, 0.001f), 0.018f));

        // ⭐⭐ UNE BASCULE POUR LES DIX EFFETS, PAS POUR QUATRE.
        //
        // /!\ Le chorus, l'ensemble, le delai et la reverbe en avaient une ;
        // les six autres ne s'eteignaient qu'en ramenant leur melange a zero.
        // Retour de l'utilisateur : « on ne peut pas faire on/off sur tous les
        // fx ». Couper un effet en tournant un bouton PERD son reglage - ce
        // n'est pas un interrupteur, c'est une amputation.
        d.add (std::make_unique<PB> (p + "sat_on", N + "Saturation active", false));
        d.add (std::make_unique<PB> (p + "ton_on", N + "Tonalite active", false));
        d.add (std::make_unique<PB> (p + "pha_on", N + "Phaser actif", false));
        d.add (std::make_unique<PB> (p + "mf_on",  N + "Multifiltre actif", false));
        d.add (std::make_unique<PB> (p + "trf_on", N + "Transfert actif", false));
        d.add (std::make_unique<PB> (p + "cmp_on", N + "Compresseur actif", false));

        d.add (std::make_unique<P>  (p + "sat", N + "Saturation",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "sat_asym", N + "Saturation asymetrie",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
        d.add (std::make_unique<P>  (p + "ton_grave", N + "Tonalite grave (dB)",
                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
        d.add (std::make_unique<P>  (p + "ton_aigu", N + "Tonalite aigu (dB)",
                 juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

        d.add (std::make_unique<P>  (p + "pha_mix", N + "Phaser melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "pha_vit", N + "Phaser vitesse",
                 juce::NormalisableRange<float> (0.02f, 8.0f, 0.001f, 0.35f), 0.3f));
        d.add (std::make_unique<P>  (p + "pha_prof", N + "Phaser profondeur",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));
        d.add (std::make_unique<P>  (p + "pha_rei", N + "Phaser reinjection",
                 juce::NormalisableRange<float> (0.0f, 0.92f, 0.001f), 0.4f));
        d.add (std::make_unique<PI> (p + "pha_cel", N + "Phaser cellules", 2, 8, 4));

        d.add (std::make_unique<P>  (p + "mf_mix", N + "Multifiltre melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<PC> (p + "mf_type", N + "Multifiltre type",
                 juce::StringArray { "Passe-bas", "Passe-haut", "Passe-bande",
                                     "Rejecteur" }, 0));
        d.add (std::make_unique<P>  (p + "mf_coup", N + "Multifiltre coupure (Hz)",
                 juce::NormalisableRange<float> (20.0f, 18000.0f, 1.0f, 0.30f), 18000.0f));
        d.add (std::make_unique<P>  (p + "mf_reso", N + "Multifiltre resonance",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f));

        d.add (std::make_unique<P>  (p + "trf_mix", N + "Transfert melange",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<PC> (p + "trf_mach", N + "Transfert machine",
                 juce::StringArray { "Emulator II", "Japonais 32 k", "Propre" }, 0));

        d.add (std::make_unique<P>  (p + "cmp_seuil", N + "Compresseur seuil (dB)",
                 juce::NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -18.0f));
        d.add (std::make_unique<P>  (p + "cmp_taux", N + "Compresseur taux",
                 juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f));
        d.add (std::make_unique<P>  (p + "cmp_att", N + "Compresseur attaque (ms)",
                 juce::NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 5.0f));
        d.add (std::make_unique<P>  (p + "cmp_rel", N + "Compresseur relachement (ms)",
                 juce::NormalisableRange<float> (5.0f, 1000.0f, 1.0f, 0.4f), 120.0f));
        d.add (std::make_unique<P>  (p + "cmp_comp", N + "Compresseur compensation",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f));

        // ⭐ L'ORDRE DU RACK : dix emplacements, 0 = vide. C'est ce que le
        // glisser-deposer de l'interface reordonne.
        //
        // /!\ PAR DEFAUT TOUS VIDES sur une tranche. Un rack de tranche qui
        // naitrait plein ferait payer dix effets a chaque couche sans que
        // personne ne l'ait demande - et surtout, `RackFx::vide()` ne pourrait
        // plus sauter le rack, donc la fonctionnalite couterait meme a ceux
        // qui ne s'en servent pas.
        for (int i = 0; i < romance::kEmplacementsFx; ++i)
            d.add (std::make_unique<PI> (p + "slot" + juce::String (i + 1),
                     N + "emplacement " + juce::String (i + 1), 0,
                     romance::kEmplacementsFx, 0));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout RomanceProcessor::disposition()
{
    using P  = juce::AudioParameterFloat;
    using PC = juce::AudioParameterChoice;
    using PB = juce::AudioParameterBool;
    using PI = juce::AudioParameterInt;
    juce::AudioProcessorValueTreeState::ParameterLayout d;

    d.add (std::make_unique<PC> ("mode", "Mode de voix",
             juce::StringArray { "Polyphonique", "Mono", "Legato" }, 0));
    d.add (std::make_unique<PI> ("poly", "Polyphonie", 1, 32, 8));
    d.add (std::make_unique<P>  ("accordage", "Accordage",
             juce::NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f));
    // ⭐ LE GLIDE, EN SECONDES PAR OCTAVE, et il est sur la page JOUER parce
    // que c'est un geste de jeu, pas un reglage de timbre.
    // /!\ La courbe est en puissance 0,35 : sans ca, les neuf dixiemes de la
    // course du bouton tomberaient dans des glides trop lents pour etre
    // musicaux, et tout ce qui est utile serait ecrase contre la butee.
    d.add (std::make_unique<P>  ("glide", "Glide",
             juce::NormalisableRange<float> (0.0f, 4.0f, 0.001f, 0.35f), 0.0f));
    // /!\ DEUX DEMI-TONS PAR DEFAUT, la convention universelle. Un bend qui
    // ferait douze demi-tons chez nous et deux partout ailleurs rendrait
    // injouable tout ce que l'utilisateur sait deja faire.
    d.add (std::make_unique<P>  ("bend_portee", "Bend portee",
             juce::NormalisableRange<float> (0.0f, 24.0f, 1.0f), 2.0f));
    d.add (std::make_unique<P>  ("sortie", "Sortie",
             juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), -6.0f));

    d.add (std::make_unique<PB> ("bruit", "Bruit actif", false));
    d.add (std::make_unique<P>  ("bruit_niv", "Bruit niveau",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f));
    d.add (std::make_unique<P>  ("bruit_fc", "Bruit coupure",
             juce::NormalisableRange<float> (100.0f, 16000.0f, 1.0f, 0.3f), 4000.0f));
    d.add (std::make_unique<P>  ("bruit_att", "Bruit attaque",
             juce::NormalisableRange<float> (0.001f, 2.0f, 0.001f, 0.3f), 0.002f));
    d.add (std::make_unique<P>  ("bruit_dec", "Bruit decroissance",
             juce::NormalisableRange<float> (0.005f, 6.0f, 0.001f, 0.3f), 0.15f));
    d.add (std::make_unique<P>  ("bruit_sus", "Bruit tenue",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<P>  ("bruit_rel", "Bruit relachement",
             juce::NormalisableRange<float> (0.005f, 6.0f, 0.001f, 0.3f), 0.2f));

    // ---- les trois macros
    // /!\ TROIS, ET CHACUNE ADOSSEE A UN MECANISME MESURE. La premiere version
    // de la facade proposait « Air » et « LoFi » : deux mots inventes qui ne
    // correspondaient a rien dans le moteur. Ici chaque macro tire sur des
    // mecanismes qu'on a mesures separement, et sur des mecanismes DISJOINTS -
    // sinon deux boutons feraient la meme chose sous deux noms.
    //
    //   USURE     -> le CONVERTISSEUR : quantification et bande passante.
    //   GRAIN     -> l'HORLOGE : escalier de lecture, filtre asservi, boucle.
    //   MOUVEMENT -> l'INSTABILITE : wow, flutter, derive, chorus.
    //
    // ⭐ Elles n'ECRASENT PAS les reglages : elles s'ajoutent par-dessus. Une
    // macro qui ecrit dans les parametres se bat avec la main de l'utilisateur
    // et lui fait perdre ses reglages des qu'il la bouge.
    d.add (std::make_unique<P> ("mac_usure", "USURE",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<P> ("mac_grain", "GRAIN",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    // ⭐ LES QUATRE MACROS ASSIGNABLES. Elles ne touchent a RIEN par
    // elles-memes : ce sont des sources de modulation, et c'est un cable qui
    // decide de ce qu'elles font. A ne pas confondre avec USURE / GRAIN /
    // MOUVEMENT juste au-dessus, qui deplacent le patch (voir Macros.h).
    for (int i = 0; i < romance::kNbMacros; ++i)
        d.add (std::make_unique<P> ("macro" + juce::String (i + 1),
                 "MACRO " + juce::String (i + 1),
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    d.add (std::make_unique<P> ("mac_mouv", "MOUVEMENT",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ---- instabilite
    // /!\ Le wow et le flutter sont COMMUNS a toutes les voix (une seule
    // horloge maitre : un accord respire en bloc et garde son accord), la
    // derive est PROPRE a chaque voix et a chaque couche (chaque circuit a le
    // sien). Ce ne sont pas deux doses du meme effet.
    d.add (std::make_unique<PB> ("inst", "Instabilite active", true));
    // --- trait de CARTE DE VOIX (ce qu'un echantillonneur a vraiment)
    d.add (std::make_unique<P>  ("inst_tim", "Derive de timbre (%)",
             juce::NormalisableRange<float> (0.0f, 0.30f, 0.001f, 0.6f), 0.025f));
    d.add (std::make_unique<P>  ("inst_niv", "Derive de niveau (dB)",
             juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f, 0.6f), 0.12f));
    d.add (std::make_unique<P>  ("inst_der", "Derive de hauteur (cents)",
             juce::NormalisableRange<float> (0.0f, 20.0f, 0.01f, 0.5f), 0.6f));
    // --- trait de BANDE : a zero par defaut, une ROM n'a pas de pleurage
    d.add (std::make_unique<P>  ("inst_wow", "Pleurage - bande (cents)",
             juce::NormalisableRange<float> (0.0f, 40.0f, 0.01f, 0.5f), 0.0f));
    d.add (std::make_unique<P>  ("inst_wowv", "Pleurage vitesse",
             juce::NormalisableRange<float> (0.05f, 4.0f, 0.001f, 0.5f), 0.6f));
    d.add (std::make_unique<P>  ("inst_flut", "Scintillement - bande (cents)",
             juce::NormalisableRange<float> (0.0f, 20.0f, 0.01f, 0.5f), 0.0f));
    d.add (std::make_unique<P>  ("inst_flutv", "Scintillement vitesse",
             juce::NormalisableRange<float> (2.0f, 30.0f, 0.01f, 0.6f), 8.0f));

    // ---- effets
    // /!\ Pas des finitions : la mesure a montre qu'une signature comme celle
    // de PAD 1 est inatteignable sans modulation de hauteur, et sur le M1 comme
    // sur le D-50 les effets integres sont indissociables du son.
    d.add (std::make_unique<PB> ("cho", "Chorus actif", true));
    d.add (std::make_unique<PC> ("cho_nat", "Chorus nature",
             juce::StringArray { "Seaux (BBD)", "Numerique" }, 0));
    d.add (std::make_unique<P>  ("cho_vit", "Chorus vitesse",
             juce::NormalisableRange<float> (0.02f, 8.0f, 0.001f, 0.35f), 0.40f));
    d.add (std::make_unique<P>  ("cho_prof", "Chorus profondeur",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
    d.add (std::make_unique<P>  ("cho_mix", "Chorus melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));
    // couleur : 0 = transparent, 1 = seaux (BBD) d'epoque. Un chorus
    // parfaitement transparent n'appartient a aucune machine.
    d.add (std::make_unique<P>  ("cho_coul", "Chorus couleur (seaux)",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));

    // ---- ensemble (cordes)
    // /!\ Ce n'est PAS un chorus plus profond : ses trois lignes ont des taux
    // volontairement eloignes et irrationnels entre eux, ce qui produit un flou
    // continu ou l'oreille ne suit plus aucune ligne. Meme cablage, deux sons
    // sans rapport.
    d.add (std::make_unique<PB> ("ens", "Ensemble actif", false));
    d.add (std::make_unique<P>  ("ens_vit", "Ensemble vitesse",
             juce::NormalisableRange<float> (0.05f, 4.0f, 0.001f, 0.5f), 0.6f));
    d.add (std::make_unique<P>  ("ens_prof", "Ensemble profondeur",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
    d.add (std::make_unique<P>  ("ens_mix", "Ensemble melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ---- delai
    d.add (std::make_unique<PB> ("del", "Delai actif", false));
    d.add (std::make_unique<P>  ("del_tps", "Delai temps (s)",
             juce::NormalisableRange<float> (0.01f, 2.0f, 0.001f, 0.4f), 0.375f));
    d.add (std::make_unique<P>  ("del_rei", "Delai reinjection",
             juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.35f));
    d.add (std::make_unique<P>  ("del_amo", "Delai amortissement",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
    d.add (std::make_unique<P>  ("del_pp", "Delai ping-pong",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<P>  ("del_mix", "Delai melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ---- etage de sortie
    // ⭐ ASYMETRIQUE : une saturation symetrique ne produit que des harmoniques
    // impaires et sonne dure. C'est le leger desequilibre d'un etage reel qui
    // donne H2, celle qu'on entend comme de la chaleur.
    d.add (std::make_unique<P>  ("sat", "Saturation",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<P>  ("sat_asym", "Saturation asymetrie",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    // ---- tonalite (Baxandall)
    // /!\ Elle MONTE aussi bien qu'elle descend - les deux autres controles de
    // couleur du plugin (bande, couleur du chorus) ne savent que retirer.
    d.add (std::make_unique<P>  ("ton_grave", "Tonalite grave (dB)",
             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    d.add (std::make_unique<P>  ("ton_aigu", "Tonalite aigu (dB)",
             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    d.add (std::make_unique<PB> ("rev", "Reverbe active", true));
    d.add (std::make_unique<P>  ("rev_taille", "Reverbe taille",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
    d.add (std::make_unique<P>  ("rev_amorti", "Reverbe amortissement",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
    d.add (std::make_unique<P>  ("rev_mix", "Reverbe melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.22f));
    d.add (std::make_unique<P>  ("rev_pre", "Reverbe pre-delai",
             juce::NormalisableRange<float> (0.0f, 0.10f, 0.001f), 0.018f));

    // ---- ⭐ LA MACHINE : la saveur de convertisseur, en un seul menu.
    // Elle etait jusqu'ici eparpillee dans cinq reglages par couche - horloge,
    // quantification, coupure, pente, taux - donc personne ne pouvait comparer
    // deux epoques. C'est pourtant la these du projet qu'il s'agit d'entendre.
    d.add (std::make_unique<PC> ("machine", "MACHINE",
             [] { juce::StringArray a;
                  for (int i = 0; i < romance::kNbMachines; ++i)
                      a.add (romance::nomMachine (i));
                  return a; }(), 1));

    // ---- phaser
    // /!\ Le phaser CREUSE la ou le chorus DESACCORDE. C'est le seul mouvement
    // spectral du plugin qui ne vienne pas d'une hauteur - Small Stone, Phase
    // 90, le phaser du Prophet-5.
    d.add (std::make_unique<P>  ("pha_mix", "Phaser melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<P>  ("pha_vit", "Phaser vitesse",
             juce::NormalisableRange<float> (0.01f, 10.0f, 0.001f, 0.35f), 0.35f));
    d.add (std::make_unique<P>  ("pha_prof", "Phaser profondeur",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));
    d.add (std::make_unique<P>  ("pha_rei", "Phaser reinjection",
             juce::NormalisableRange<float> (0.0f, 0.92f, 0.001f), 0.4f));
    d.add (std::make_unique<PC> ("pha_cel", "Phaser cellules",
             juce::StringArray { "2 (une encoche)", "4 (Phase 90)",
                                 "6 (Small Stone)", "8 (epais)" }, 1));

    // ---- compresseur / limiteur de sortie
    // ⭐ Sa vraie raison d'etre : Genopatch rend des profils de niveaux tres
    // inegaux. Sans etage qui les egalise, comparer deux branches a l'oreille
    // revient a comparer deux volumes.
    // /!\ ACTIF PAR DEFAUT, a -6 dB. Seize voix de trois oscillateurs
    // d'unisson se somment sans aucune protection : un accord depasse
    // largement la pleine echelle, et ce qu'on entend alors n'est pas
    // l'instrument mais l'ecretage de l'hote. Un instrument doit pouvoir etre
    // joue a deux mains sans qu'on ait a chercher le bouton qui l'empeche de
    // saturer.
    d.add (std::make_unique<P>  ("cmp_seuil", "Compresseur seuil (dB)",
             juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -6.0f));
    d.add (std::make_unique<P>  ("cmp_taux", "Compresseur taux",
             juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f, 0.5f), 20.0f));
    d.add (std::make_unique<P>  ("cmp_att", "Compresseur attaque (ms)",
             juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.35f), 1.0f));
    d.add (std::make_unique<P>  ("cmp_rel", "Compresseur relachement (ms)",
             juce::NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.4f), 150.0f));
    // /!\ ZERO PAR DEFAUT. Mesure : avec la compensation a fond, l'etage
    // rendait un accord de sept notes PLUS FORT qu'il ne l'avait recu.
    d.add (std::make_unique<P>  ("cmp_comp", "Compresseur compensation",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ---- multi-filtre d'effet
    // /!\ Sa coupure est en HERTZ, pas en multiples de la hauteur : c'est ce
    // qui le distingue du filtre de couche, et ce qui en fait un geste de
    // mixage plutot qu'un reglage de timbre.
    // les six bascules du master (voir la note dans ajouteRack)
    d.add (std::make_unique<PB> ("sat_on", "Saturation active", true));
    d.add (std::make_unique<PB> ("ton_on", "Tonalite active", true));
    d.add (std::make_unique<PB> ("pha_on", "Phaser actif", true));
    d.add (std::make_unique<PB> ("mf_on",  "Multifiltre actif", true));
    d.add (std::make_unique<PB> ("trf_on", "Transfert actif", true));
    d.add (std::make_unique<PB> ("cmp_on", "Compresseur actif", true));

    d.add (std::make_unique<P>  ("mf_mix", "Multifiltre melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<PC> ("mf_type", "Multifiltre type",
             juce::StringArray { "Passe-bas", "Passe-haut", "Passe-bande",
                                 "Rejecteur" }, 0));
    // Ouvert par defaut lui aussi : un effet qu'on active doit d'abord ne
    // RIEN faire, sinon l'activer assombrit avant qu'on ait touche un bouton.
    d.add (std::make_unique<P>  ("mf_coup", "Multifiltre coupure (Hz)",
             juce::NormalisableRange<float> (20.0f, 18000.0f, 1.0f, 0.30f), 18000.0f));
    d.add (std::make_unique<P>  ("mf_reso", "Multifiltre resonance",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f));

    // ---- transfert de convertisseur
    // ⭐⭐ Le seul effet du plugin qui n'existe sur aucune machine : le mix
    // entier repasse par l'horloge et le filtre de reconstruction. Sur un
    // Emulator II le convertisseur est EN AMONT, il ne peut pas voir la
    // reverbe. Ici tout passe par le meme plugin, donc il peut.
    d.add (std::make_unique<P>  ("trf_mix", "Transfert convertisseur melange",
             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    d.add (std::make_unique<PC> ("trf_mach", "Transfert machine",
             [] { juce::StringArray a;
                  for (int i = 0; i < romance::kNbMachines; ++i)
                      a.add (romance::nomMachine (i));
                  return a; }(), 1));

    // ---- ⭐ LA CHAINE D'EFFETS EST ORDONNABLE
    // Six emplacements, chacun choisit un effet. L'ordre par defaut est celui
    // du trajet du signal dans une machine reelle (voir `traiteEffets`), mais
    // ce n'est plus qu'un DEFAUT : saturer avant la reverbe est un son, saturer
    // apres en est un autre, et aucun des deux n'est faux.
    //
    // /!\ Un meme effet choisi dans deux emplacements n'est traite QU'UNE FOIS.
    // Il n'y a qu'une instance de chaque : la traverser deux fois rebouclerait
    // son delai sur lui-meme et doublerait son melange, ce qui ne ressemble a
    // rien de reel et diverge sur la reverbe.
    {
        const juce::StringArray effets { "-", "Chorus", "Ensemble", "Delai",
                                         "Reverbe", "Saturation", "Tonalite",
                                         "Phaser", "Multifiltre",
                                         "Transfert convertisseur",
                                         "Compresseur" };
        for (int i = 0; i < romance::kEmplacementsFx; ++i)
            d.add (std::make_unique<PC> ("fx" + juce::String (i + 1),
                     "Chaine emplacement " + juce::String (i + 1),
                     effets, i + 1));
    }

    // ---- les huit emplacements de modulation
    // /!\ CABLAGES PAR DEFAUT PRE-ETABLIS : un synthe qui s'ouvre sans aucune
    // modulation cablee est un synthe sans caractere. Et `LFO 1 -> hauteur`
    // n'est pas une convention : la mesure a montre qu'une signature comme
    // celle de PAD 1 est inatteignable sans modulation de hauteur.
    for (int i = 0; i < kNbMods; ++i)
    {
        const auto m = "m" + juce::String (i + 1) + "_";
        const auto M = "Mod " + juce::String (i + 1) + " ";
        d.add (std::make_unique<PC> (m + "type", M + "nature",
                 juce::StringArray { "Aucun", "Enveloppe", "LFO", "Aleatoire" },
                 (i == 0) ? 2 : (i == 1 ? 1 : 0)));
        d.add (std::make_unique<P>  (m + "p1", M + "vitesse / attaque",
                 juce::NormalisableRange<float> (0.01f, 30.0f, 0.001f, 0.3f),
                 (i == 0) ? 0.30f : 0.05f));
        d.add (std::make_unique<P>  (m + "p2", M + "forme / decroissance",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (m + "p3", M + "symetrie / tenue",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (m + "p4", M + "relachement (enveloppe)",
                 juce::NormalisableRange<float> (0.005f, 8.0f, 0.001f, 0.3f), 0.3f));
        d.add (std::make_unique<PB> (m + "retrig", M + "redeclenche", true));
    }

    // ---- la matrice
    for (int i = 0; i < kNbRoutages; ++i)
    {
        const auto r = "r" + juce::String (i + 1) + "_";
        const auto Rn = "Routage " + juce::String (i + 1) + " ";
        // /!\ 1..8 = les emplacements, 9..12 = les macros. Les indices des
        // emplacements ne bougent pas : un patch enregistre avant les macros se
        // relit tel quel.
        // ⭐ LE CABLAGE 1 EST DEBRANCHE PAR DEFAUT, et c'est un retour d'ecoute.
        // Il portait « LFO 1 -> hauteur, dose 0,008 », ce qui a l'air minuscule
        // et vaut en fait +/- 19,2 CENTS a 0,30 Hz : un vibrato permanent, sur
        // toutes les machines, y compris en position Propre - et impossible a
        // couper depuis la page JOUER. L'utilisateur : « je suis oblige de
        // desactiver la hauteur dans la matrice ».
        //
        // /!\ La lecon n'est pas « la dose etait trop forte » mais qu'une dose
        // se lit dans l'unite de sa DESTINATION : la hauteur porte 24 demi-tons
        // a pleine dose, donc 0,008 n'est pas un petit nombre.
        d.add (std::make_unique<PI> (r + "src", Rn + "source (0 = aucune)",
                 0, kNbSources, (i == 1) ? 2 : 0));
        // ⭐ LES 38 DESTINATIONS DE LA TABLE, plus les sept d'origine.
        // /!\ LES SEPT PREMIERS INDICES N'ONT PAS BOUGE : un projet enregistre
        // avant la table se relit correctement. C'est la seule raison pour
        // laquelle les entrees 0 a 7 de `tableDestinations()` gardent leur
        // ordre historique, qui n'a par ailleurs aucune logique.
        {
            juce::StringArray dests;
            for (int k = 0; k < romance::kNbDestinationsTotal; ++k)
                dests.add (romance::nomDestinationTotale (k));
            d.add (std::make_unique<PC> (r + "dst", Rn + "destination", dests,
                         (i == 1) ? 2 : 0));
        }
        // ⭐ UN CHAMP, DEUX SENS - et c'est ce qui evite de multiplier le menu
        // des destinations par cinq. Sur une destination de COUCHE il
        // restreint (0 = toutes) ; sur un EFFET il choisit le rack
        // (0 = master, 1..4 = la tranche).
        d.add (std::make_unique<PI> (r + "couche", Rn + "couche / rack",
                 0, kNbCouches, 0));
        d.add (std::make_unique<P>  (r + "dose", Rn + "dose",
                 juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
                 (i == 1) ? 0.30f : 0.0f));
    }

    // ⭐⭐ LE RACK D'INSERT DE CHAQUE TRANCHE. Quatre racks complets, comme sur
    // une console - c'est ce que l'image du mixeur demandait, et la mesure des
    // couts a montre que rien ne s'y oppose (voir Rack.h).
    for (int c = 0; c < kNbCouches; ++c)
        ajouteRack (d, "fx" + juce::String (c + 1) + "_",
                    "Tranche " + juce::String ((char) ('A' + c)) + " ");

    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto p = pre (c), N = nom (c);
        d.add (std::make_unique<PB> (p + "actif", N + "active", c == 0));
        d.add (std::make_unique<PC> (p + "source", N + "source",
                 // ⭐ UN SEUL MENU POUR TOUT LE MATERIAU. Les quatre recettes
                 // ne sont plus attachees a une couche : on met ce qu'on veut
                 // ou l'on veut. « Recette (resynthese) » est la case ou
                 // atterrit ce que la recherche a trouve.
                 juce::StringArray { "Additif (ROM)", "Sinus", "Triangle",
                                     "Dent de scie", "Carre",
                                     "Nappe", "Metallique", "Orgue",
                                     "Piano electrique",
                                     "Recette (resynthese)",
                                     // ⭐ LE SECOND MATERIAU. Ce n'est pas un
                                     // cinquieme moteur : il fabrique une ROM,
                                     // donc il se lit par le chemin existant -
                                     // grain, boucle, quantification.
                                     "Spectre dessine" }, 5 + c));
        // ---- le spectre dessine (source « Spectre dessine »)
        //
        // /!\ LA PENTE REMPLACE SOIXANTE-QUATRE ENVELOPPES. L'etalon dit
        // qu'une enveloppe PAR BANDE ferait tomber la perte a 0,25 dB, mais
        // elle couterait plus d'octets que tout le reste du format. Une
        // enveloppe commune plus une pente en donne l'essentiel pour deux
        // octets - et c'est ce que fait la matiere : l'aigu s'eteint avant le
        // grave.
        d.add (std::make_unique<P>  (p + "sp_pente", N + "spectre pente",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
        d.add (std::make_unique<P>  (p + "sp_att", N + "spectre attaque",
                 juce::NormalisableRange<float> (0.0005f, 3.0f, 0.0001f, 0.3f), 0.005f));
        d.add (std::make_unique<P>  (p + "sp_chute", N + "spectre chute",
                 juce::NormalisableRange<float> (0.01f, 8.0f, 0.001f, 0.3f), 0.60f));
        d.add (std::make_unique<P>  (p + "sp_tenue", N + "spectre tenue",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.10f));

        // ---- le moteur de la couche
        // ⭐ Ce n'est pas une option parmi d'autres : c'est le choix qui decide
        // de tout le reste. Un Juno se decrit en six valeurs ici, et en
        // soixante-douze en additif - pour un resultat moins bon.
        d.add (std::make_unique<PC> (p + "moteur", N + "moteur",
                 juce::StringArray { "ROM (echantillon)", "Analogique", "FM",
                                     "Table d'ondes" }, 0));
        const juce::StringArray formes { "Dent de scie", "Impulsion",
                                         "Triangle", "Sinus" };
        d.add (std::make_unique<PC> (p + "va_f1", N + "osc 1 forme", formes, 0));
        d.add (std::make_unique<PC> (p + "va_f2", N + "osc 2 forme", formes, 1));
        d.add (std::make_unique<P>  (p + "va_mix", N + "melange osc",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<PI> (p + "va_semi", N + "osc 2 accord (demi-tons)",
                 -24, 24, 0));
        // /!\ C'est ce desaccord fin qui fait l'epaisseur analogique, pas le
        // nombre d'oscillateurs : deux VCO parfaitement accordes sonnent comme
        // un seul, en plus fort.
        d.add (std::make_unique<P>  (p + "va_cents", N + "osc 2 desaccord (cents)",
                 juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 7.0f));
        d.add (std::make_unique<P>  (p + "va_larg", N + "largeur d'impulsion",
                 juce::NormalisableRange<float> (0.05f, 0.95f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "va_sub", N + "sous-octave",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "va_bruit", N + "bruit de l'oscillateur",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        // ---- FM : quatre operateurs
        // ⭐ Une decroissance PAR OPERATEUR : c'est ce qui fait une cloche. Le
        // modulateur s'eteint plus vite que la porteuse, donc le son commence
        // metallique et finit en sinus.
        // ⭐⭐ LES NOMS DE LA FM RESTENT EN ANGLAIS, ET C'EST DELIBERE.
        //
        // Tout le reste du plugin est en francais. Ici non, parce que la FM a
        // un vocabulaire NORMALISE depuis le DX7 - ratio, feedback, breakpoint,
        // level scaling - qu'on retrouve identique dans FM8, Dexed, Operator,
        // Pivot et tous les manuels. Traduire, c'est obliger l'utilisateur a
        // apprendre NOTRE plugin puis a retraduire pour appliquer ailleurs ce
        // qu'il a compris. On lui fait apprendre la FM, pas ROMANCE.
        //
        // /!\ Et ca corrige une collision de ma fabrication : « suivi vitesse »
        // designait l'acceleration des enveloppes dans l'aigu (key rate
        // scaling) pendant que « vitesse » designait la sensibilite au toucher.
        // Deux notions sans rapport, un seul mot.
        d.add (std::make_unique<PC> (p + "fm_algo", N + "FM Algorithm",
                 juce::StringArray { "1 - chaine (metallique)", "2 - chaine + porteuse",
                                     "3 - deux modulateurs", "4 - paire + modulateur",
                                     "5 - deux paires", "6 - un mod, trois porteuses",
                                     "7 - paire + deux sinus",
                                     "8 - quatre sinus (additif)" }, 0));
        for (int o = 0; o < romance::kOperateurs; ++o)
        {
            const auto q = p + "fm" + juce::String (o + 1) + "_";
            const auto Q = N + "FM op " + juce::String (o + 1) + " ";
            d.add (std::make_unique<P> (q + "ratio", Q + "Ratio",
                     juce::NormalisableRange<float> (0.25f, 16.0f, 0.001f, 0.4f),
                     (o == 0) ? 1.0f : (o == 1 ? 2.0f : (o == 2 ? 1.0f : 3.5f))));
            d.add (std::make_unique<P> (q + "niv", Q + "Output Level",
                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                     (o == 0) ? 1.0f : (o == 1 ? 0.6f : (o == 2 ? 0.0f : 0.4f))));
            // ⭐⭐ UNE ENVELOPPE COMPLETE PAR OPERATEUR. « dec » n'est plus un
            // nombre sans unite entre 0 et 1 : c'est un TEMPS DE CHUTE en
            // secondes, avec son attaque, sa tenue et son relachement. C'est
            // exactement ce qui separe la cloche (tenue nulle) de
            // l'electro-piano (tenue basse mais non nulle) et de la nappe.
            d.add (std::make_unique<P> (q + "att", Q + "Attack",
                     juce::NormalisableRange<float> (0.0005f, 3.0f, 0.0001f, 0.3f),
                     0.002f));
            d.add (std::make_unique<P> (q + "dec", Q + "Decay",
                     juce::NormalisableRange<float> (0.005f, 8.0f, 0.001f, 0.3f),
                     (o == 0) ? 0.60f : (o == 1 ? 0.25f : (o == 2 ? 0.30f : 0.20f))));
            d.add (std::make_unique<P> (q + "ten", Q + "Sustain",
                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                     (o == 0) ? 0.80f : (o == 1 ? 0.30f : (o == 2 ? 0.40f : 0.20f))));
            d.add (std::make_unique<P> (q + "rel", Q + "Release",
                     juce::NormalisableRange<float> (0.005f, 8.0f, 0.001f, 0.3f), 0.30f));
            // ⭐ LE SUIVI DE CLAVIER EN NIVEAU - le vrai secret du DX7. Sans
            // lui, un modulateur juste sur un do2 rend le do6 STRIDENT.
            d.add (std::make_unique<P> (q + "ech", Q + "Level Scaling",
                     juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
                     (o == 0) ? 0.0f : 0.35f));
            // ⭐ Le desaccord fait BATTRE deux operateurs au meme rapport :
            // c'est la moitie de l'epaisseur d'un electro-piano DX.
            d.add (std::make_unique<P> (q + "des", Q + "Detune",
                     juce::NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f));
            // ⭐ Et la vitesse sur un MODULATEUR rend plus brillant, pas plus
            // fort : c'est ce qui rend le DX expressif.
            d.add (std::make_unique<P> (q + "vel", Q + "Velocity Sens",
                     juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        }
        d.add (std::make_unique<P> (p + "fm_bris", N + "FM Breakpoint",
                 juce::NormalisableRange<float> (24.0f, 96.0f, 1.0f), 60.0f));
        d.add (std::make_unique<P> (p + "fm_vit", N + "FM Rate Scaling",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        // ---- table d'ondes
        // ⭐ Le geste caracteristique n'est pas la lecture, c'est le BALAYAGE :
        // passer d'une image a l'autre pendant que la note tient. Cabler une
        // modulation sur « morph » fait exactement ca.
        d.add (std::make_unique<PC> (p + "tb_fam", N + "table famille",
                 juce::StringArray { "Harmoniques (sinus -> scie)",
                                     "Formants (transwave)",
                                     "Impaires (sinus -> carre)",
                                     "Bruitee",
                                     "Dessinee (grille)" }, 0));
        // ⭐ La grille : sa resolution HORIZONTALE (cellules) et VERTICALE
        // (bits). Ni l'une ni l'autre n'est destructive - le dessin est
        // toujours stocke en 64 cellules sur 8 bits.
        d.add (std::make_unique<PC> (p + "gr_cell", N + "grille cellules",
                 juce::StringArray { "8", "16", "32", "64" }, 2));
        d.add (std::make_unique<PC> (p + "gr_bits", N + "grille bits",
                 juce::StringArray { "3 bits (8 niveaux)", "4 bits (16)",
                                     "5 bits (32)", "6 bits (64)",
                                     "7 bits (128)", "8 bits (256)" }, 5));
        d.add (std::make_unique<PC> (p + "tb_lec", N + "table lecture",
                 juce::StringArray { "Fondu (Prophet VS)", "Saut (PPG)" }, 0));
        d.add (std::make_unique<P>  (p + "tb_pos", N + "table position",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "tb_sym", N + "table symetrie",
                 juce::NormalisableRange<float> (0.05f, 0.95f, 0.001f), 0.5f));

        d.add (std::make_unique<P> (p + "fm_ret", N + "FM Feedback",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

        // ⭐⭐ LE PASSE-HAUT DE COUCHE, sur les quatre moteurs. 20 Hz = ouvert,
        // donc c'est une position du bouton et non une case a cocher de plus.
        d.add (std::make_unique<P> (p + "ph_coup", N + "passe-haut coupure",
                 juce::NormalisableRange<float> (20.0f, 8000.0f, 1.0f, 0.28f), 20.0f));
        d.add (std::make_unique<P> (p + "ph_reso", N + "passe-haut resonance",
                 juce::NormalisableRange<float> (0.0f, 0.97f, 0.001f), 0.0f));
        d.add (std::make_unique<PC> (p + "ph_pente", N + "passe-haut pente",
                 juce::StringArray { "12 dB/oct (Juno)", "24 dB/oct" }, 0));

        d.add (std::make_unique<PI> (p + "note_min", N + "note min", 0, 127, 0));
        d.add (std::make_unique<PI> (p + "note_max", N + "note max", 0, 127, 127));
        // /!\ Ces deux-la etaient LUES et APPLIQUEES par le moteur mais exposees
        // nulle part : la moitie du mecanisme de zones etait inatteignable.
        d.add (std::make_unique<PI> (p + "velo_min", N + "velocite min", 0, 127, 0));
        d.add (std::make_unique<PI> (p + "velo_max", N + "velocite max", 0, 127, 127));
        d.add (std::make_unique<PI> (p + "transpose", N + "transposition (demi-tons)",
                 -48, 48, 0));
        d.add (std::make_unique<P>  (p + "accord", N + "accord fin (cents)",
                 juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
        d.add (std::make_unique<P>  (p + "desac", N + "desaccord (cents)",
                 juce::NormalisableRange<float> (0.0f, 60.0f, 0.1f), 8.0f));
        d.add (std::make_unique<P>  (p + "largeur", N + "largeur unisson",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "pan", N + "panoramique",
                 juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        // ---- ⭐ LA VELOCITE. Avant aujourd'hui elle ne faisait RIEN d'autre
        // qu'ouvrir ou fermer la zone : jouer fort ou doux donnait le meme son.
        // /!\ Les defauts ne sont PAS a zero. Un instrument livre sans reponse
        // au clavier n'est pas neutre, il est casse - personne ne pense a aller
        // chercher le bouton qui le rend vivant.
        d.add (std::make_unique<P>  (p + "v_niv", N + "velocite -> niveau",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.65f));
        d.add (std::make_unique<P>  (p + "v_coup", N + "velocite -> coupure",
                 juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.35f));
        d.add (std::make_unique<P>  (p + "v_att", N + "velocite -> attaque",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "v_dep", N + "velocite -> depart",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "v_courbe", N + "courbe de velocite",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "v_fondu", N + "fondu de zone (velocite)",
                 juce::NormalisableRange<float> (0.0f, 64.0f, 0.1f), 0.0f));

        // ---- ⭐ Les gestes de rompleur
        d.add (std::make_unique<P>  (p + "depart", N + "depart de lecture",
                 juce::NormalisableRange<float> (0.0f, 0.9f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "fondu_b", N + "fondu de boucle",
                 juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "retard", N + "retard de la couche (s)",
                 juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f, 0.4f), 0.0f));

        d.add (std::make_unique<P>  (p + "b_deb", N + "boucle debut",
                 juce::NormalisableRange<float> (0.0f, 0.98f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "b_fin", N + "boucle fin",
                 juce::NormalisableRange<float> (0.02f, 1.0f, 0.001f), 1.0f));

        // ---- le grain
        d.add (std::make_unique<PB> (p + "grain", N + "grain actif", true));
        d.add (std::make_unique<PC> (p + "horloge", N + "horloge",
                 juce::StringArray { "Variable", "Fixe" }, 0));
        d.add (std::make_unique<PC> (p + "quant", N + "quantification",
                 juce::StringArray { "Aucune", "mu-law", "8 bits", "12 bits", "16 bits" }, 1));
        d.add (std::make_unique<PB> (p + "asservi", N + "filtre asservi", true));
        // /!\ Sur l'Emulator II, filtre musical et filtre de reconstruction sont
        // LE MEME composant (le SSM2045 est le VCF/VCA de voie). Le cas etait
        // code mais n'etait exposé nulle part.
        d.add (std::make_unique<PB> (p + "fusion", N + "filtre fusionne au grain", false));
        d.add (std::make_unique<PC> (p + "pente_rec", N + "pente reconstruction",
                 juce::StringArray { "4 poles - 24 dB/oct (Emulator II)",
                                     "3 poles - 18 dB/oct (Akai S1000)" }, 0));
        d.add (std::make_unique<PC> (p + "topo", N + "topologie de filtre",
                 juce::StringArray { "4 poles - 24 dB/oct",
                                     "3 poles - 18 dB/oct",
                                     "Analogique - resonance qui amaigrit",
                                     "Z-Plane - morphable" }, 0));
        d.add (std::make_unique<P>  (p + "morph", N + "morph (Z-Plane seulement)",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        d.add (std::make_unique<P>  (p + "coup_rom", N + "coupure reconstruction",
                 juce::NormalisableRange<float> (0.04f, 0.49f, 0.001f),
                 (float) kCutoffRatio));

        // ---- le filtre musical
        // ⭐⭐ OUVERT PAR DEFAUT, et la plage va bien plus haut qu'avant.
        //
        // /!\ LA COUPURE EST RELATIVE A LA HAUTEUR. A 8 fois la fondamentale -
        // l'ancien defaut - un la1 ne gardait que huit harmoniques, soit une
        // coupure a 880 Hz : tout patch neuf naissait sombre, et il fallait
        // rouvrir le filtre a chaque fois. Le plafond de 40 n'y suffisait meme
        // pas, puisqu'il ne donnait que 2,2 kHz sur cette meme note.
        //
        // Le filtre garde tout son caractere - il se ferme quand on le tourne.
        // Ce qui change, c'est qu'il ne se ferme plus TOUT SEUL.
        d.add (std::make_unique<P>  (p + "coupure", N + "coupure (x hauteur)",
                 juce::NormalisableRange<float> (0.5f, 250.0f, 0.01f, 0.25f), 250.0f));
        d.add (std::make_unique<P>  (p + "reso", N + "resonance",
                 juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.0f));
        d.add (std::make_unique<P>  (p + "suivi", N + "suivi clavier",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

        // ---- l'ampli
        d.add (std::make_unique<P>  (p + "att", N + "attaque",
                 juce::NormalisableRange<float> (0.001f, 3.0f, 0.001f, 0.3f), 0.005f));
        d.add (std::make_unique<P>  (p + "dec", N + "decroissance",
                 juce::NormalisableRange<float> (0.005f, 8.0f, 0.001f, 0.3f), 0.4f));
        d.add (std::make_unique<P>  (p + "sus", N + "tenue",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
        d.add (std::make_unique<P>  (p + "rel", N + "relachement",
                 juce::NormalisableRange<float> (0.005f, 8.0f, 0.001f, 0.3f), 0.5f));
        d.add (std::make_unique<P>  (p + "niveau", N + "niveau",
                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
    }
    return d;
}

// ------------------------------------------------------------------ cycle

/** ⭐ Le meme dossier que les formes et les presets - une seule convention a
    retenir pour l'utilisateur. */
juce::File RomanceProcessor::fichierReseau()
{
    return romance::Bibliotheque::racine().getChildFile ("reseau.rnn");
}

RomanceProcessor::RomanceProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Sortie",
                        juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "ROMANCE", disposition())
{
    // /!\ On s'abonne au SEUL parametre qui en a besoin. S'abonner a tout
    // ferait passer chaque mouvement de bouton par un rappel qui ne sert a
    // rien - et sur un plugin a 300 parametres, ca se sent.
    params.addParameterListener ("machine", this);

    // ⭐⭐ ON CHARGE LE RESEAU ET ON LE DONNE A GENOPATCH.
    //
    // /!\ C'EST LA LIGNE QUI MANQUAIT DEPUIS TOUJOURS. `Genopatch::run`
    // appelait `resynthetise` sans son dernier argument - donc `nullptr` -
    // et tout l'apprentissage restait hors ligne sans que rien ne le dise.
    //
    // /!\ ICI, PAS DANS `prepareToPlay` : la lecture de 3,8 Mo n'a rien a
    // faire sur un appel que l'hote peut declencher au demarrage du transport.
    reseau.lit (fichierReseau().getFullPathName().toStdString());
    geno.poseReseau (reseau.pret ? &reseau : nullptr);

    for (int c = 0; c < kNbCouches; ++c)
    {
        // /!\ Reserve UNE FOIS, a la taille maximale possible. A partir d'ici
        // les tampons ne changent plus jamais d'adresse : une voix qui sonne
        // peut garder son pointeur sans risque.
        for (int b = 0; b < 2; ++b)
        {
            banques_[c][b].brute.reserve ((size_t) romance::kRomMax);
            banques_[c][b].quant.reserve ((size_t) romance::kRomMax);
        }
        recettes_[c] = recetteParDefaut (c);
    }
    construitRoms();
}

void RomanceProcessor::construitRoms()
{
    for (int c = 0; c < kNbCouches; ++c) { construitRom (c, true); construitDessin (c, true); }
}

void RomanceProcessor::construitRom (int c, bool force)
{
    const int src = (int) params.getRawParameterValue (
                        (pre (c) + "source").toRawUTF8())->load();
    const auto q = patch_.couche[c].grain.quant;

    const int actif = banqueActive_[c].load (std::memory_order_acquire);
    const auto& courante = banques_[c][actif];

    // La recette peut avoir change SANS que la source change : c'est le cas
    // apres une resynthese. On compare donc les trois.
    if (! force && src == sourcePosee_[c]
        && recettePosee_[c] == recetteVersion_[c]
        && courante.quantPosee == q
        && courante.ptr != nullptr) return;

    // ⭐ ON ECRIT DANS L'AUTRE BANQUE. Jamais dans celle qu'une voix lit.
    const int libre = 1 - actif;
    auto& b = banques_[c][libre];

    // /!\ On ne charge un preset QUE si l'utilisateur vient de le choisir.
    // A la toute premiere construction (`sourcePosee_ == -1`) et au
    // rechargement d'un etat, la recette deja en place fait foi - sinon on
    // ecraserait ce qu'une resynthese ou un preset sauvegarde a produit.
    if (src >= 5 && src <= 8 && sourcePosee_[c] >= 0 && sourcePosee_[c] != src)
        recettes_[c] = recetteParDefaut (src - 5);

    if (src == 10)
    {
        // ⭐⭐ LE SPECTRE DESSINE. Meme place que la recette dans la chaine :
        // il produit le tampon, et tout ce qui suit ne sait pas d'ou il vient.
        //
        // /!\ FABRIQUE SUR LE FIL MESSAGE, comme la ROM de recette. Soixante-
        // quatre bandes de vingt-quatre sinusoides, c'est quelques millions
        // d'operations - le fil audio n'en fait pas une seule (piege 28).
        romance::Spectre sp = spectres_[c];
        sp.pente   = params.getRawParameterValue ((pre (c) + "sp_pente").toRawUTF8())->load();
        sp.attaque = params.getRawParameterValue ((pre (c) + "sp_att").toRawUTF8())->load();
        sp.chute   = params.getRawParameterValue ((pre (c) + "sp_chute").toRawUTF8())->load();
        sp.tenue   = params.getRawParameterValue ((pre (c) + "sp_tenue").toRawUTF8())->load();
        spectres_[c] = sp;
        romance::rendSpectre (sp, b.brute, romance::kFsRomEII,
                              (int) (2.0 * romance::kFsRomEII), 1u);
        boucleRecette_[c] = 0.6;
    }
    else if (src >= 5)
    {
        const auto info = romance::rendRom (recettes_[c], b.brute);
        boucleRecette_[c] = info.boucleDebut();
    }
    else if (src == 0) remplitRom (b.brute, c);
    else               remplitRomForme (b.brute, (Forme) src);

    b.taille = (int) b.brute.size();
    b.quant.assign (b.brute.size(), 0.0);
    for (size_t i = 0; i < b.brute.size(); ++i) b.quant[i] = quantifie (b.brute[i], q);
    b.quantPosee = q;
    b.ptr = (q == Quant::Aucune) ? b.brute.data() : b.quant.data();

    sourcePosee_[c] = src;
    recettePosee_[c] = recetteVersion_[c];
    banqueActive_[c].store (libre, std::memory_order_release);
}

void RomanceProcessor::construitDessin (int c, bool force)
{
    const int iCell = (int) params.getRawParameterValue (
                          (pre (c) + "gr_cell").toRawUTF8())->load();
    const int iBits = (int) params.getRawParameterValue (
                          (pre (c) + "gr_bits").toRawUTF8())->load();
    romance::DessinAB d = dessins_[c];
    static const int kCell[4] = { 8, 16, 32, 64 };
    // /!\ LA FINESSE ET LES BITS VALENT POUR LES DEUX DESSINS. Ce sont des
    // proprietes de la GRILLE, pas du trait : deux dessins qui n'auraient ni la
    // meme largeur de cellule ni la meme profondeur ne pourraient pas se
    // fondre l'un dans l'autre - le morph comparerait des choses de nature
    // differente.
    d.a.nCellules = d.b.nCellules = kCell[std::max (0, std::min (3, iCell))];
    d.a.bits      = d.b.bits      = romance::kBitsMin + std::max (0, std::min (5, iBits));
    dessins_[c] = d;

    // /!\ ON NE REFABRIQUE QUE SI LE DESSIN A VRAIMENT CHANGE. Douze octaves
    // de seize images, c'est quelques dizaines de millisecondes : le faire a
    // chaque relecture de parametres tuerait l'interface pendant qu'on dessine.
    if (! force && ! dessinJamaisPose_[c] && d == dessinPose_[c]) return;

    const int actif = dessinActif_[c].load (std::memory_order_acquire);
    const int libre = 1 - actif;
    banquesDessin_[c][libre].fabrique (d, getSampleRate() > 0.0 ? getSampleRate() : 44100.0);
    dessinPose_[c] = d;
    dessinJamaisPose_[c] = false;
    dessinActif_[c].store (libre, std::memory_order_release);
}

void RomanceProcessor::poseSpectre (int c, const romance::Spectre& sp)
{
    if (c < 0 || c >= romance::kNbCouches) return;
    spectres_[c] = sp;
    ++spectreVersion_[c];
    refaireRoms_.store (true);
    triggerAsyncUpdate();
}

void RomanceProcessor::poseDessin (int c, const romance::DessinAB& d)
{
    if (c < 0 || c >= romance::kNbCouches) return;
    dessins_[c] = d;
    ++dessinVersion_[c];
    // ⭐ La fabrication passe par l'AsyncUpdater : dessiner, c'est bouger la
    // souris, donc soixante appels par seconde. Le declencheur les fusionne.
    refaireRoms_.store (true);
    triggerAsyncUpdate();
}

void RomanceProcessor::prepareToPlay (double sampleRate, int)
{
    fs_ = sampleRate;
    moteur_.prepare (sampleRate, kVoixMax);
    // /!\ Le banc global tourne au MEME taux de controle que celui des voix,
    // et avance du bon nombre de pas a chaque bloc (voir processBlock) : sinon
    // la vitesse des LFO dependrait de la taille de tampon de l'hote.
    for (auto& m : modsGlobaux_) m.prepare (sampleRate / (double) romance::kPasControle);
    dcG_ = dcD_ = dcXg_ = dcXd_ = 0.0;
    dcA_ = 1.0 - 2.0 * kPi * 5.0 / std::max (8000.0, sampleRate);
    uneNoteTenue_ = false;

    // ⭐ Les CINQ racks : un par couche, plus le master.
    for (auto& r : racks_) r.prepare (sampleRate);
    sortieLissee_ = juce::Decibels::decibelsToGain (
                        params.getRawParameterValue ("sortie")->load());
    relitParams();
    for (int c = 0; c < kNbCouches; ++c) { construitRom (c, false); construitDessin (c, false); }
}

bool RomanceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo();
}

// ------------------------------------------------------------------ params

void RomanceProcessor::relitParams() noexcept
{
    // /!\ getRawParameterValue rend NULLPTR si l'identifiant n'existe pas, et il
    // etait dereference sans controle. Un simple renommage de parametre faisait
    // donc planter le plugin - ca vient d'arriver en renommant `ordre` en `topo`.
    // On assert en debug (pour attraper la faute) et on rend une valeur neutre
    // en release (pour ne pas emporter l'hote).
    iCache_ = 0;

    // ⭐ Lecture par le cache. Les morceaux d'identifiant ne sont assembles
    // qu'au PREMIER passage ; ensuite on indexe, sans allouer une seule fois.
    auto lit = [this] (const juce::String& id) -> float
    {
        if (iCache_ < cacheParam_.size())
        {
           #if JUCE_DEBUG
            jassert (cacheNom_[iCache_] == id);
           #endif
            auto* v = cacheParam_[iCache_++];
            return v != nullptr ? v->load() : 0.0f;
        }
        auto* v = params.getRawParameterValue (id);
        jassert (v != nullptr);
        cacheParam_.push_back (v);
       #if JUCE_DEBUG
        cacheNom_.push_back (id);
       #endif
        ++iCache_;
        return v != nullptr ? v->load() : 0.0f;
    };
    // /!\ La chaine n'est construite QUE si le cache n'est pas encore rempli.
    // Sinon `lit (a + b)` allouerait quand meme - c'est tout ce qu'on cherche
    // a supprimer.
    auto litD = [this, &lit] (const juce::String& a, const char* b) -> float
    {
        if (iCache_ < cacheParam_.size())
        {
            auto* v = cacheParam_[iCache_++];
            return v != nullptr ? v->load() : 0.0f;
        }
        return lit (a + b);
    };
    auto f = [&lit] (const char* id) { return lit (juce::String (id)); };
    auto i = [&f] (const char* id) { return (int) f (id); };

    patch_.mode = (ModeVoix) i ("mode");
    patch_.polyphonie = i ("poly");
    patch_.accordage = f ("accordage");
    patch_.glide = f ("glide");
    patch_.bendPortee = f ("bend_portee");
    // Les trois controleurs ne sont PAS des parametres : ils viennent des
    // valeurs de jeu, ecrites par le MIDI ou par la bandelette de facade.
    patch_.bend     = jeuBend_.load();
    patch_.molette  = jeuMolette_.load();
    patch_.pression = jeuPression_.load();
    patch_.instab.actif        = f ("inst") > 0.5f;
    patch_.instab.wowProfCents  = f ("inst_wow");
    patch_.instab.wowVitesse    = f ("inst_wowv");
    patch_.instab.flutProfCents = f ("inst_flut");
    patch_.instab.flutVitesse   = f ("inst_flutv");
    patch_.instab.deriveCents    = f ("inst_der");
    patch_.instab.deriveTimbre   = f ("inst_tim");
    patch_.instab.deriveNiveauDb = f ("inst_niv");

    patch_.bruitActif = f ("bruit") > 0.5f;
    patch_.bruitNiveau = f ("bruit_niv");
    patch_.bruitCoupure = f ("bruit_fc");
    patch_.bruitA = f ("bruit_att");  patch_.bruitD = f ("bruit_dec");
    patch_.bruitS = f ("bruit_sus");  patch_.bruitR = f ("bruit_rel");

    for (int i = 0; i < kNbMods; ++i)
    {
        const auto m = "m" + juce::String (i + 1) + "_";
        auto gm = [&litD, &m] (const char* sfx) { return litD (m, sfx); };
        auto& mo = patch_.mods[i];
        mo.type = (TypeMod) (int) gm ("type");
        mo.p1 = gm ("p1"); mo.p2 = gm ("p2"); mo.p3 = gm ("p3"); mo.p4 = gm ("p4");
        mo.retrig = gm ("retrig") > 0.5f;
    }
    for (int i = 0; i < kNbRoutages; ++i)
    {
        const auto rr = "r" + juce::String (i + 1) + "_";
        auto gr = [&litD, &rr] (const char* sfx) { return litD (rr, sfx); };
        auto& ro = patch_.routages[i];
        ro.source = (int) gr ("src");
        ro.dest   = (int) gr ("dst");
        ro.couche = (int) gr ("couche");
        ro.dose   = gr ("dose");
    }

    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto p = pre (c);
        auto& r = patch_.couche[c];
        auto g = [&litD, &p] (const char* s) { return litD (p, s); };

        r.active  = g ("actif") > 0.5f;
        switch ((int) g ("moteur"))
        {
            case 1:  r.moteur = TypeMoteur::Analogique; break;
            case 2:  r.moteur = TypeMoteur::FM;         break;
            case 3:  r.moteur = TypeMoteur::Table;      break;
            default: r.moteur = TypeMoteur::Rom;        break;
        }
        auto forme = [] (int i) {
            switch (i) { case 1: return FormeOsc::Impulsion;
                         case 2: return FormeOsc::Triangle;
                         case 3: return FormeOsc::Sinus;
                         default: return FormeOsc::Scie; } };
        r.va.forme1   = forme ((int) g ("va_f1"));
        r.va.forme2   = forme ((int) g ("va_f2"));
        r.va.melange  = g ("va_mix");
        r.va.demiTons2 = (int) g ("va_semi");
        r.va.cents2   = g ("va_cents");
        r.va.largeur  = g ("va_larg");
        r.va.sub      = g ("va_sub");
        r.va.bruit    = g ("va_bruit");

        r.fm.algo = (int) g ("fm_algo");
        for (int o = 0; o < romance::kOperateurs; ++o)
        {
            const auto q = "fm" + juce::String (o + 1) + "_";
            r.fm.ratio[o]         = litD (p + q, "ratio");
            r.fm.niveau[o]        = litD (p + q, "niv");
            r.fm.attaque[o]       = litD (p + q, "att");
            r.fm.decli[o]         = litD (p + q, "dec");
            r.fm.tenue[o]         = litD (p + q, "ten");
            r.fm.relache[o]       = litD (p + q, "rel");
            r.fm.echelleNiveau[o] = litD (p + q, "ech");
            r.fm.desaccord[o]     = litD (p + q, "des");
            r.fm.veloSens[o]      = litD (p + q, "vel");
        }
        r.fm.retour        = g ("fm_ret");
        r.fm.pointBrisure  = g ("fm_bris");
        r.fm.echelleVitesse = g ("fm_vit");

        switch ((int) g ("tb_fam"))
        {
            case 1:  r.table.famille = FamilleTable::Formants; break;
            case 2:  r.table.famille = FamilleTable::Impaires; break;
            case 3:  r.table.famille = FamilleTable::Bruitee;  break;
            case 4:  r.table.famille = FamilleTable::Dessin;   break;
            // ⭐ Le pointeur vers la banque dessinee de la couche. Il designe
            // un membre stable du processeur, jamais un tampon realloue.
            default: r.table.famille = FamilleTable::Harmoniques; break;
        }
        r.table.lecture  = ((int) g ("tb_lec") == 1) ? LectureTable::Saut
                                                     : LectureTable::Fondu;
        r.table.position = g ("tb_pos");
        r.table.symetrie = g ("tb_sym");
        r.table.dessin = &banquesDessin_[c][dessinActif_[c].load (std::memory_order_acquire)];
        r.phCoupure = g ("ph_coup");
        r.phReso    = g ("ph_reso");
        r.phPente   = ((int) g ("ph_pente") == 1) ? 2 : 1;
        r.noteMin = (int) g ("note_min");
        r.noteMax = (int) g ("note_max");
        r.transpose  = (int) g ("transpose");
        r.accordFin  = g ("accord");
        r.desaccordCents = g ("desac");
        r.largeur = g ("largeur");
        r.pan     = g ("pan");
        r.boucleDebut = g ("b_deb");
        r.veloNiveau  = g ("v_niv");
        r.veloCoupure = g ("v_coup");
        r.veloAttaque = g ("v_att");
        r.veloDepart  = g ("v_dep");
        r.courbeVelo  = g ("v_courbe");
        r.fonduVelo   = g ("v_fondu");
        r.depart      = g ("depart");
        r.fonduBoucle = g ("fondu_b");
        r.retard      = g ("retard");
        r.boucleFin   = std::max ((double) g ("b_fin"), r.boucleDebut + 0.02);
        // /!\ Sur une ROM de recette, les points de boucle NE SONT PAS un
        // reglage : la recette a fabrique une queue exactement periodique a un
        // endroit precis, et boucler ailleurs claquerait. On les impose donc,
        // et c'est exactement ce que faisaient les machines - les points de
        // boucle etaient dans la ROM, pas sur la face avant.
        if (sourcePosee_[c] >= 5)
        {
            r.boucleDebut = boucleRecette_[c];
            r.boucleFin   = 1.0;
        }

        r.grain.actif   = g ("grain") > 0.5f;
        r.grain.horloge = ((int) g ("horloge") == 0) ? Horloge::Variable : Horloge::Fixe;
        r.grain.filtre  = (g ("asservi") > 0.5f) ? Filtre::Asservi : Filtre::Fixe;
        r.grain.coupure = g ("coup_rom");
        switch ((int) g ("quant"))
        {
            case 0:  r.grain.quant = Quant::Aucune; break;
            case 1:  r.grain.quant = Quant::MuLaw;  break;
            case 2:  r.grain.quant = Quant::Lin8;   break;
            case 3:  r.grain.quant = Quant::Lin12;  break;
            default: r.grain.quant = Quant::Lin16;  break;
        }

        switch ((int) g ("topo"))
        {
            case 1:  r.topo = Topologie::TroisPoles; break;
            case 2:  r.topo = Topologie::AnalogiqueBasseReso; break;
            case 3:  r.topo = Topologie::ZPlane; break;
            default: r.topo = Topologie::QuatrePoles; break;
        }
        // La pente du filtre de RECONSTRUCTION appartient a la machine et non au
        // filtre musical : le S1000 a 18 dB/oct la ou l'Emulator II en a 24, et
        // ca n'a rien a voir avec la topologie qu'on choisit pour jouer.
        r.grain.ordre = ((int) g ("pente_rec") == 0) ? 4 : 3;
        r.morph       = g ("morph");
        r.veloMin     = (int) g ("velo_min");
        r.veloMax     = (int) g ("velo_max");
        r.fusionneAvecGrain = g ("fusion") > 0.5f;
        r.coupureRel  = g ("coupure");
        r.reso        = g ("reso");
        r.suiviClavier= g ("suivi");

        r.attaque = g ("att");  r.decli = g ("dec");
        r.tenue   = g ("sus");  r.relache = g ("rel");
        r.niveau  = g ("niveau");
    }

    // /!\ EN DERNIER, et par-dessus tout le reste : les macros s'ajoutent aux
    // reglages lus, elles ne les remplacent pas. Si on les appliquait avant, la
    // lecture des parametres les effacerait aussitot.
    romance::appliqueMacros (patch_, f ("mac_usure"), f ("mac_grain"),
                             f ("mac_mouv"));

    for (int i = 0; i < romance::kNbMacros; ++i)
        patch_.macros[i] = litD ("macro", juce::String (i + 1).toRawUTF8());
}

/** ⭐ L'ORDRE DE LA CHAINE EST CELUI QUE L'UTILISATEUR A POSE.

    L'ordre par DEFAUT n'est pas arbitraire, il suit le trajet du signal dans
    une machine reelle :

      voix -> modulations (chorus, ensemble) -> delai -> reverbe
           -> ETAGE DE SORTIE (saturation, tonalite)

    La saturation y est en dernier parce que c'est un etage de sortie : saturer
    avant la reverbe salirait la queue, ce qu'aucun ampli ne fait - il sature ce
    qui lui arrive, reverbe comprise.

    /!\ Mais « ce qu'aucun ampli ne fait » n'est pas « ce qu'il ne faut pas
    faire » : saturer avant la reverbe est un son a part entiere. Le defaut dit
    d'ou l'on part, il ne dit pas ou l'on a le droit d'aller.

    /!\ UN EFFET N'EST TRAVERSE QU'UNE FOIS meme s'il est choisi deux fois. Il
    n'y a qu'une instance de chaque : la traverser deux fois rebouclerait le
    delai sur lui-meme et ferait diverger la reverbe.
*/
/** ⭐⭐ UN ECHANTILLON, DE BOUT EN BOUT : les voix, les racks de tranche, la
    somme, puis le rack master.

    /!\ UN SEUL ENDROIT OU LE SIGNAL S'ASSEMBLE. Ce trajet etait ecrit DEUX
    fois dans `processBlock` - une fois pour les echantillons avant un evenement
    MIDI, une fois pour ceux d'apres. Ajouter les racks de tranche a un seul des
    deux aurait donne un son qui change selon la position des notes dans le
    bloc, ce qui ne se diagnostique pas a l'oreille.

    /!\ ET ON NE PAIE LES BUS QUE SI QUELQU'UN S'EN SERT. Tant qu'aucune
    tranche n'a d'effet, on emprunte le chemin d'avant - une seule somme, aucun
    tableau a remettre a zero par echantillon.
*/
void RomanceProcessor::rendUnEchantillon (double& g, double& d) noexcept
{
    bool desRacks = false;
    for (int c = 0; c < kNbCouches; ++c) if (! racks_[c].vide()) desRacks = true;

    if (! desRacks)
    {
        moteur_.suivant (patch_, g, d);
        traiteEffets (g, d);
        return;
    }

    double bus[2 * romance::kNbCouches];
    double sg = 0.0, sd = 0.0;
    moteur_.suivantParCouche (patch_, bus, sg, sd);
    g = d = 0.0;
    for (int c = 0; c < kNbCouches; ++c)
    {
        double cg = bus[c * 2], cd = bus[c * 2 + 1];
        traiteRackCouche (c, cg, cd);
        g += cg; d += cd;
    }
    traiteEffets (g, d);
}

void RomanceProcessor::traiteEffets (double& g, double& d) noexcept
{
    // /!\ LE BLOCAGE DE CONTINUE EST EN TETE DE CHAINE, pas en queue. Une
    // continue qui traverse la saturation devient une ASYMETRIE : elle deplace
    // le point de fonctionnement et change le timbre au lieu de simplement
    // decaler le zero. On la retire avant que quoi que ce soit ne la voie.
    const double ng = g - dcXg_ + dcA_ * dcG_;   dcXg_ = g; dcG_ = ng; g = ng;
    const double nd = d - dcXd_ + dcA_ * dcD_;   dcXd_ = d; dcD_ = nd; d = nd;

    racks_[kMaster].traite (g, d);
}

/** ⭐⭐ LE RACK D'UNE COUCHE, avant la somme.

    /!\ ON SAUTE UN RACK VIDE. Les effets se court-circuitent deja a melange
    nul, mais l'APPEL coute : quatre racks vides, ce sont quarante appels de
    fonction par echantillon pour ne rien faire. Tant que l'utilisateur n'a
    rempli aucune tranche, le cout de cette fonctionnalite est exactement nul.
*/
void RomanceProcessor::traiteRackCouche (int c, double& g, double& d) noexcept
{
    if (c < 0 || c >= kNbCouches) return;
    if (racks_[c].vide()) return;
    racks_[c].traite (g, d);
}

// ------------------------------------------------------------------ audio

void RomanceProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals gardeDenormaux;

    // ⭐ CHRONOMETRE. /!\ Sans lui on optimise a l'aveugle : toutes mes mesures
    // hors ligne ne voyaient QUE le DSP - jamais la lecture des parametres, ni
    // la fabrication des ROM, ni l'hote. C'est exactement pour ca qu'elles
    // annoncaient 5 % la ou l'utilisateur en constatait 100.
    const auto tDebut = std::chrono::steady_clock::now();

    relitParams();
    const auto tParams = std::chrono::steady_clock::now();

    // /!\ ON NE FABRIQUE RIEN ICI. Une ROM de recette, ce sont quelques
    // millions de sin() et un redimensionnement : ca part sur le thread
    // message. Le thread audio se contente de LIRE la banque publiee et de
    // signaler qu'il en faudra une nouvelle.
    const double* romPtr[kNbCouches];
    int romTaille[kNbCouches];
    for (int c = 0; c < kNbCouches; ++c)
    {
        const int a = banqueActive_[c].load (std::memory_order_acquire);
        romPtr[c] = banques_[c][a].ptr;
        romTaille[c] = banques_[c][a].taille;

        const int src = (int) params.getRawParameterValue (
                            (pre (c) + "source").toRawUTF8())->load();
        if (src != sourcePosee_[c] || recettePosee_[c] != recetteVersion_[c]
            || banques_[c][a].quantPosee != patch_.couche[c].grain.quant)
            if (! refaireRoms_.exchange (true)) triggerAsyncUpdate();
    }

    // Les notes qui tiennent suivent la transposition et l'accord fin.
    moteur_.majHauteurs (patch_);

    // Les effets se reglent une fois par bloc : leurs coefficients ne changent
    // pas assez vite pour justifier un calcul par echantillon.
    const bool choOn = params.getRawParameterValue ("cho")->load() > 0.5f;
    const bool revOn = params.getRawParameterValue ("rev")->load() > 0.5f;
    {
        auto v = [this] (const char* id) { return params.getRawParameterValue (id)->load(); };

        // ⭐⭐ LA MODULATION GLOBALE DES EFFETS.
        //
        // Les huit emplacements tournent ici une SECONDE fois, en dehors des
        // voix - c'est la seule facon d'avoir une valeur unique a donner a une
        // reverbe unique. Les quatre macros s'y ajoutent comme sources.
        //
        // /!\ Ce banc-la n'est PAS celui des voix. Un LFO cable a la fois sur
        // la coupure (par voix) et sur la reverbe (global) donnera deux phases
        // differentes : c'est assume, parce que l'alternative - un seul banc -
        // voudrait dire que chaque note redemarre le LFO de la reverbe.
        // ⭐⭐ UN JEU D'APPORTS PAR RACK - c'etait la limite : seuls les effets
        // du MASTER etaient modulables. Un filtre balaye par un LFO sur la
        // couche 2 seulement etait impossible, alors que c'est le geste le
        // plus banal d'un synthe a quatre couches.
        //
        // /!\ LE CHAMP `couche` D'UN CABLE CHOISIT LE RACK, et 0 reste le
        // MASTER. C'est ce qui fait que tous les patchs deja enregistres se
        // relisent a l'identique : ils portent tous `couche = 0`, et ils
        // visaient tous le master puisqu'il n'y avait que lui.
        double apportsGlob[romance::kNbCouches + 1][romance::kNbDestGlobales] = {};
        {
            // /!\ ON AVANCE D'AUTANT DE PAS QUE LA VOIX EN AURAIT FAIT.
            // Avancer d'un pas par bloc ferait dependre la vitesse des LFO de
            // la taille de tampon de l'hote : le meme patch respirerait deux
            // fois plus vite en 256 qu'en 512 echantillons. C'est le genre de
            // bug qu'on ne voit jamais chez soi et qui arrive chez l'autre.
            const int nPas = juce::jmax (1, buffer.getNumSamples()
                                              / romance::kPasControle);
            double src[romance::kNbSources];
            for (int i = 0; i < romance::kNbMods; ++i)
            {
                double x = 0.0;
                for (int k = 0; k < nPas; ++k) x = modsGlobaux_[i].suivant (patch_.mods[i]);
                src[i] = x;
            }
            for (int i = 0; i < romance::kNbMacros; ++i)
                src[romance::kNbMods + i] = patch_.macros[i];
            // /!\ LE BANC GLOBAL N'A PAS DE VOIX, donc pas de note. Il prend
            // la DERNIERE jouee. Ce n'est pas un pis-aller : une destination
            // globale est un effet, il n'y en a qu'un, et la question « quelle
            // note ? » n'a de toute facon pas de reponse quand trois notes
            // sonnent. La derniere frappee est celle que l'oreille suit.
            src[romance::kIdSourceTouche - 1] = romance::sourceTouche (derniereNote_);
            src[romance::kIdSourceVelo   - 1] = romance::sourceVelo   (derniereVelo_);

            for (const auto& r : patch_.routages)
            {
                if (r.source <= 0 || r.source > romance::kNbSources) continue;
                if (r.dest < romance::kNbDestinations) continue;
                const int ig = r.dest - romance::kPremiereDestGlobale + 1;
                if (ig <= 0 || ig >= romance::kNbDestGlobales) continue;
                // /!\ La regle vit dans `Modulation.h`, pas ici : elle est
                // testable la-bas, et l'editeur doit dire exactement la meme
                // chose quand il ecrit « [master] » ou « [rack 2] ».
                apportsGlob[romance::rackDeRoutage (r.couche)][ig]
                    += src[r.source - 1] * r.dose;
            }
        }
        // `mg` vise le master ; `mgc` vise le rack d'une tranche. Une seule
        // fonction bornee derriere les deux : les plages d'un effet ne changent
        // pas selon l'endroit ou il est pose.
        auto mgr = [&apportsGlob] (int rack, romance::DestGlobale d, double base)
        {
            const int i = (int) d;
            return romance::appliqueDestGlobale (i, apportsGlob[rack][i], base);
        };
        auto mg = [&mgr] (romance::DestGlobale d, double base)
        {
            return mgr (kMaster, d, base);
        };
        using DG = romance::DestGlobale;

        racks_[kMaster].chorus.regleNature (params.getRawParameterValue ("cho_nat")->load() < 0.5f);
        racks_[kMaster].chorus.regle (mg (DG::ChorusVitesse, v ("cho_vit")),
                       mg (DG::ChorusProfondeur, v ("cho_prof")),
                       choOn ? mg (DG::ChorusMelange, v ("cho_mix")) : 0.0,
                       mg (DG::ChorusCouleur, v ("cho_coul")));
        racks_[kMaster].reverbe.regle (mg (DG::ReverbeTaille, v ("rev_taille")),
                        mg (DG::ReverbeAmorti, v ("rev_amorti")),
                        revOn ? mg (DG::ReverbeMelange, v ("rev_mix")) : 0.0,
                        v ("rev_pre"));

        const bool ensOn = params.getRawParameterValue ("ens")->load() > 0.5f;
        racks_[kMaster].ensemble.regle (mg (DG::EnsembleVitesse, v ("ens_vit")),
                         mg (DG::EnsembleProfondeur, v ("ens_prof")),
                         ensOn ? mg (DG::EnsembleMelange, v ("ens_mix")) : 0.0);
        const bool delOn = params.getRawParameterValue ("del")->load() > 0.5f;
        racks_[kMaster].delai.regle (mg (DG::DelaiTemps, v ("del_tps")),
                      mg (DG::DelaiReinjection, v ("del_rei")),
                      delOn ? mg (DG::DelaiMelange, v ("del_mix")) : 0.0,
                      v ("del_amo"), v ("del_pp"));
        racks_[kMaster].saturation.regle (mg (DG::Saturation, v ("sat")),
                           mg (DG::SaturationAsymetrie, v ("sat_asym")));
        racks_[kMaster].tonalite.regle (mg (DG::TonaliteGrave, v ("ton_grave")),
                         mg (DG::TonaliteAigu, v ("ton_aigu")));

        // ---- les quatre effets ajoutes
        static const int kCel[] = { 2, 4, 6, 8 };
        racks_[kMaster].phaser.regle (mg (DG::PhaserVitesse, v ("pha_vit")),
                       mg (DG::PhaserProfondeur, v ("pha_prof")),
                       mg (DG::PhaserMelange, v ("pha_mix")),
                       v ("pha_rei"),
                       kCel[juce::jlimit (0, 3, (int) v ("pha_cel"))]);

        racks_[kMaster].multiFiltre.regle (mg (DG::MultiFiltreCoupure, v ("mf_coup")),
                            mg (DG::MultiFiltreResonance, v ("mf_reso")),
                            (romance::TypeMultiFiltre) juce::jlimit (0, 3, (int) v ("mf_type")),
                            mg (DG::MultiFiltreMelange, v ("mf_mix")));

        racks_[kMaster].transfert.regle (romance::configMachine (
                              (romance::Machine) juce::jlimit (0, romance::kNbMachines - 1,
                                                               (int) v ("trf_mach"))),
                          mg (DG::TransfertMelange, v ("trf_mix")));

        racks_[kMaster].compresseur.regle (mg (DG::CompresseurSeuil, v ("cmp_seuil")),
                            v ("cmp_taux"), v ("cmp_att"), v ("cmp_rel"),
                            v ("cmp_comp"));

        // /!\ L'ordre est relu ICI, sur le thread audio, mais dans le bloc qui
        // ne tourne qu'une fois par bloc : le lire par echantillon coûterait
        // six lectures atomiques pour une valeur qui ne bouge pas.
        for (int i = 0; i < romance::kEmplacementsFx; ++i)
            racks_[kMaster].ordre[i] = (int) params.getRawParameterValue (
                              ("fx" + juce::String (i + 1)).toRawUTF8())->load();

        // ⭐⭐ LES QUATRE RACKS DE TRANCHE. Meme code que le master, sans la
        // matrice de modulation - voir la note dans `ajouteRack`.
        for (int c = 0; c < kNbCouches; ++c)
        {
            const auto q = "fx" + juce::String (c + 1) + "_";
            auto w = [this, &q] (const char* id) -> double
            {
                auto* r = params.getRawParameterValue (q + id);
                return r != nullptr ? (double) r->load() : 0.0;
            };
            // ⭐ `k` = la meme valeur, MODULEE pour CETTE tranche. Le reste du
            // bloc est mot pour mot celui du master : c'est voulu, les deux
            // doivent rester interchangeables.
            auto k = [&mgr, c] (romance::DestGlobale dd, double base)
            {
                return mgr (c, dd, base);
            };
            auto& R = racks_[c];
            R.chorus.regleNature (w ("cho_nat") < 0.5);
            R.chorus.regle (k (DG::ChorusVitesse, w ("cho_vit")),
                            k (DG::ChorusProfondeur, w ("cho_prof")),
                            w ("cho") > 0.5 ? k (DG::ChorusMelange, w ("cho_mix")) : 0.0,
                            k (DG::ChorusCouleur, w ("cho_coul")));
            R.ensemble.regle (k (DG::EnsembleVitesse, w ("ens_vit")),
                              k (DG::EnsembleProfondeur, w ("ens_prof")),
                              w ("ens") > 0.5 ? k (DG::EnsembleMelange, w ("ens_mix")) : 0.0);
            R.delai.regle (k (DG::DelaiTemps, w ("del_tps")),
                           k (DG::DelaiReinjection, w ("del_rei")),
                           w ("del") > 0.5 ? k (DG::DelaiMelange, w ("del_mix")) : 0.0,
                           w ("del_amo"), w ("del_pp"));
            R.reverbe.regle (k (DG::ReverbeTaille, w ("rev_taille")),
                             k (DG::ReverbeAmorti, w ("rev_amorti")),
                             w ("rev") > 0.5 ? k (DG::ReverbeMelange, w ("rev_mix")) : 0.0,
                             w ("rev_pre"));
            R.saturation.regle (w ("sat_on") > 0.5 ? k (DG::Saturation, w ("sat")) : 0.0,
                                k (DG::SaturationAsymetrie, w ("sat_asym")));
            R.tonalite.regle (w ("ton_on") > 0.5 ? k (DG::TonaliteGrave, w ("ton_grave")) : 0.0,
                              w ("ton_on") > 0.5 ? k (DG::TonaliteAigu, w ("ton_aigu")) : 0.0);
            R.phaser.regle (k (DG::PhaserVitesse, w ("pha_vit")),
                            k (DG::PhaserProfondeur, w ("pha_prof")),
                            w ("pha_on") > 0.5 ? k (DG::PhaserMelange, w ("pha_mix")) : 0.0,
                            w ("pha_rei"), (int) w ("pha_cel"));
            R.multiFiltre.regle (k (DG::MultiFiltreCoupure, w ("mf_coup")),
                                 k (DG::MultiFiltreResonance, w ("mf_reso")),
                                 (TypeMultiFiltre) (int) w ("mf_type"),
                                 w ("mf_on") > 0.5 ? k (DG::MultiFiltreMelange, w ("mf_mix")) : 0.0);
            R.transfert.regle (romance::configMachine (
                                   (romance::Machine) (int) w ("trf_mach")),
                               w ("trf_on") > 0.5 ? k (DG::TransfertMelange, w ("trf_mix")) : 0.0);
            R.compresseur.regle (k (DG::CompresseurSeuil, w ("cmp_seuil")),
                                 w ("cmp_taux"), w ("cmp_att"), w ("cmp_rel"),
                                 w ("cmp_on") > 0.5 ? w ("cmp_comp") : 0.0);
            for (int i = 0; i < romance::kEmplacementsFx; ++i)
                R.ordre[i] = (int) w (("slot" + juce::String (i + 1)).toRawUTF8());
        }

    }

    const int nEch = buffer.getNumSamples();
    const int nCan = buffer.getNumChannels();
    buffer.clear();

    const float cible = juce::Decibels::decibelsToGain (
                            params.getRawParameterValue ("sortie")->load());
    // /!\ Lissage du gain APPLIQUE : sans lui, bouger la tirette craque.
    // Lecon d'XPERT comp - ne lisser QUE ce qui multiplie le signal.
    const float lissage = 1.0f - std::exp (-1.0f / (0.015f * (float) fs_));

    auto* g = buffer.getWritePointer (0);
    auto* d = (nCan > 1) ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, nEch, meta.samplePosition);
        for (; pos < t; ++pos)
        {
            sortieLissee_ += lissage * (cible - sortieLissee_);
            double vg = 0.0, vd = 0.0;
            rendUnEchantillon (vg, vd);
            g[pos] = (float) vg * sortieLissee_;
            if (d) d[pos] = (float) vd * sortieLissee_;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            // ⭐ Les enveloppes GLOBALES se declenchent sur la PREMIERE note
            // d'une phrase. /!\ Les redeclencher a chaque note ferait sauter la
            // queue de reverbe a chaque doigt pose - un effet global n'a pas de
            // raison de repartir de zero parce qu'on a ajoute une note a un
            // accord qui sonne deja.
            if (! uneNoteTenue_)
            {
                for (int i = 0; i < romance::kNbMods; ++i)
                    modsGlobaux_[i].declenche (patch_.mods[i]);
                uneNoteTenue_ = true;
            }
            derniereNote_ = m.getNoteNumber();
            derniereVelo_ = (int) m.getVelocity();
            moteur_.noteOn (patch_, m.getNoteNumber(),
                            (int) (m.getVelocity()), romPtr, romTaille);
        }
        // ⭐⭐ LES CONTROLEURS DE JEU. Aucune de ces trois lignes n'existait :
        // le plugin recevait ces messages et les jetait.
        else if (m.isPitchWheel())
        {
            // /!\ 8192 EST LE CENTRE, PAS 8191 NI 8193. La plage MIDI va de 0
            // a 16383 et n'est donc PAS symetrique autour du centre : diviser
            // par 8192 des deux cotes donnerait -1,0 en bas et +0,9999 en haut.
            // On divise par la moitie qui correspond au sens du geste.
            const int w = m.getPitchWheelValue() - 8192;
            jeuBend_.store ((float) (w >= 0 ? (double) w / 8191.0
                                            : (double) w / 8192.0));
        }
        else if (m.isController() && m.getControllerNumber() == 1)
            jeuMolette_.store ((float) m.getControllerValue() / 127.0f);
        else if (m.isChannelPressure())
            jeuPression_.store ((float) m.getChannelPressureValue() / 127.0f);
        else if (m.isAftertouch())
            moteur_.posePressionPoly (m.getNoteNumber(),
                                      (double) m.getAfterTouchValue() / 127.0);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            // /!\ UN PANIC REMET LES CONTROLEURS A ZERO. Sans ca, un bend
            // laisse a fond au moment de l'arret desaccorde tout ce qu'on
            // jouera ensuite, sans que rien ne le montre.
            jeuBend_.store (0.0f); jeuMolette_.store (0.0f);
            jeuPression_.store (0.0f);
        }
        else if (m.isNoteOff())
        {
            moteur_.noteOff (patch_, m.getNoteNumber());
            if (! moteur_.uneNoteTenue())
            {
                for (auto& mm : modsGlobaux_) mm.relache();
                uneNoteTenue_ = false;
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            moteur_.toutCouper();
    }
    const auto tVoix0 = std::chrono::steady_clock::now();
    for (; pos < nEch; ++pos)
    {
        sortieLissee_ += lissage * (cible - sortieLissee_);
        double vg = 0.0, vd = 0.0;
        rendUnEchantillon (vg, vd);
        g[pos] = (float) vg * sortieLissee_;
    // ---- ce que le bloc a coute, en pourcentage du temps reel disponible
    {
        using S = std::chrono::duration<double>;
        const auto tFin = std::chrono::steady_clock::now();
        const double dispo = (double) nEch / std::max (8000.0, fs_);
        // ⭐ L'INSTANTANE DES TETES DE LECTURE, une fois par BLOC. Le faire par
        // echantillon coûterait seize atomiques par couche et par echantillon
        // pour dessiner une barre qui bouge de moins d'un pixel entre deux.
        {
            float t[kTetesMax];
            for (int c = 0; c < kNbCouches; ++c)
            {
                const int n = moteur_.positions (patch_, c, t, kTetesMax);
                for (int i = 0; i < n; ++i)
                    tetes_[c][i].store (t[i], std::memory_order_relaxed);
                nTetes_[c].store (n, std::memory_order_relaxed);
            }
        }

        if (dispo > 1e-9)
        {
            const double tot = std::chrono::duration_cast<S> (tFin - tDebut).count();
            const double par = std::chrono::duration_cast<S> (tParams - tDebut).count();
            // /!\ MOYENNE GLISSANTE, pas la valeur brute : un bloc isole ne dit
            // rien, et c'est la valeur SOUTENUE qui fait decrocher l'hote.
            auto lisse = [] (std::atomic<float>& a, double v)
            { a.store ((float) (0.92 * a.load() + 0.08 * v * 100.0)); };
            lisse (chargeTotale_, tot / dispo);
            lisse (chargeParams_, par / dispo);
            lisse (chargeVoix_, std::chrono::duration_cast<S> (tFin - tVoix0).count() / dispo);

            // le diviseur, sans lequel le pourcentage ne veut rien dire
            nNotes_.store (moteur_.nbActives());
            int nc = 0;
            for (int c = 0; c < kNbCouches; ++c) if (patch_.couche[c].active) ++nc;
            nCouches_.store (nc);
            moteurVu_.store ((int) patch_.couche[0].moteur);
        }
    }

        if (d) d[pos] = (float) vd * sortieLissee_;
    }
}

// ------------------------------------------------------------------ etat

romance::ConfigGrain RomanceProcessor::configRecherche() const noexcept
{
    return romance::grainTransparent (fs_);
}

const double* RomanceProcessor::materiau (int c, int& n, double& boucle) const noexcept
{
    if (c < 0 || c >= kNbCouches) { n = 0; boucle = 0.0; return nullptr; }
    const int a = banqueActive_[c].load (std::memory_order_acquire);
    n = banques_[c][a].taille;
    boucle = (sourcePosee_[c] >= 5) ? boucleRecette_[c] : 0.0;
    return banques_[c][a].ptr;
}

void RomanceProcessor::poseRecette (int c, const romance::Recette& r)
{
    if (c < 0 || c >= kNbCouches) return;
    recettes_[c] = r;
    ++recetteVersion_[c];
    construitRom (c, false);
}

void RomanceProcessor::parameterChanged (const juce::String& id, float valeur)
{
    // /!\ CE RAPPEL PEUT ARRIVER SUR LE THREAD AUDIO (automation de l'hote).
    // On n'y ecrit donc PAS quarante parametres : on note ce qu'il faut faire
    // et on le fait sur le thread message. Ecrire des parametres depuis le
    // thread audio marche neuf fois sur dix et casse la dixieme.
    if (id == "machine")
    {
        machineAPoser_.store ((int) valeur, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void RomanceProcessor::poseMachine (int machine)
{
    const auto cfg = romance::configMachine (
        (romance::Machine) juce::jlimit (0, romance::kNbMachines - 1, machine));

    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto p = "c" + juce::String (c + 1) + "_";
        auto met = [this, &p] (const char* sfx, float v)
        {
            if (auto* par = params.getParameter (p + sfx))
                par->setValueNotifyingHost (par->convertTo0to1 (v));
        };
        met ("grain",     cfg.actif ? 1.0f : 0.0f);
        met ("horloge",   (float) (int) cfg.horloge);
        met ("quant",     (float) (int) cfg.quant);
        met ("asservi",   (cfg.filtre == romance::Filtre::Asservi) ? 1.0f : 0.0f);
        met ("pente_rec", (cfg.ordre == 3) ? 1.0f : 0.0f);
        met ("coup_rom",  (float) juce::jlimit (0.04, 0.49, cfg.coupure));
    }
}

/** ⭐ La liste des parametres qui appartiennent a UNE couche.

    /!\ Elle est ecrite a la main plutot que deduite du prefixe « cN_ », et ce
    n'est pas de la paresse : deduire du prefixe copierait aussi ce qui ne doit
    pas l'etre le jour ou l'on ajoutera un parametre de couche qui n'a pas de
    sens a dupliquer. Une liste explicite se relit ; un filtre sur les noms se
    contente de marcher.
*/
static juce::StringArray parametresDeCouche()
{
    return { "actif", "source", "moteur",
             "va_f1", "va_f2", "va_mix", "va_semi", "va_cents", "va_larg",
             "va_sub", "va_bruit",
             "fm_algo", "fm_ret", "fm_bris", "fm_vit",
             "fm1_ratio", "fm1_niv", "fm1_att", "fm1_dec", "fm1_ten", "fm1_rel",
             "fm1_ech", "fm1_des", "fm1_vel",
             "fm2_ratio", "fm2_niv", "fm2_att", "fm2_dec", "fm2_ten", "fm2_rel",
             "fm2_ech", "fm2_des", "fm2_vel",
             "fm3_ratio", "fm3_niv", "fm3_att", "fm3_dec", "fm3_ten", "fm3_rel",
             "fm3_ech", "fm3_des", "fm3_vel",
             "fm4_ratio", "fm4_niv", "fm4_att", "fm4_dec", "fm4_ten", "fm4_rel",
             "fm4_ech", "fm4_des", "fm4_vel",
             "ph_coup", "ph_reso", "ph_pente",
             "sp_pente", "sp_att", "sp_chute", "sp_tenue",
             "tb_fam", "tb_lec", "tb_pos", "tb_sym", "gr_cell", "gr_bits",
             "note_min", "note_max", "velo_min", "velo_max",
             "transpose", "accord", "desac", "largeur", "pan",
             "b_deb", "b_fin", "depart", "fondu_b", "retard",
             "v_niv", "v_coup", "v_att", "v_dep", "v_courbe", "v_fondu",
             "grain", "horloge", "quant", "asservi", "fusion", "pente_rec",
             "topo", "morph", "coup_rom", "coupure", "reso", "suivi",
             "att", "dec", "sus", "rel", "niveau" };
}

void RomanceProcessor::copieCouche (int c)
{
    if (c < 0 || c >= kNbCouches) return;
    const auto p = "c" + juce::String (c + 1) + "_";
    presse_.clear();
    for (const auto& nom : parametresDeCouche())
        if (auto* v = params.getRawParameterValue (p + nom))
            presse_.push_back ({ nom, v->load() });
    presseRecette_ = recettes_[c];
    presseDessin_  = dessins_[c];      // le dessin fait partie de la couche
    presseSpectre_ = spectres_[c];
    presseOccupe_ = true;
}

void RomanceProcessor::colleCouche (int c)
{
    if (c < 0 || c >= kNbCouches || ! presseOccupe_) return;
    const auto p = "c" + juce::String (c + 1) + "_";
    for (const auto& e : presse_)
        if (auto* par = params.getParameter (p + e.first))
            par->setValueNotifyingHost (par->convertTo0to1 (e.second));
    poseRecette (c, presseRecette_);
    poseDessin  (c, presseDessin_);
    poseSpectre (c, presseSpectre_);
}

void RomanceProcessor::tireAuSort (int c)
{
    if (c < 0 || c >= kNbCouches) return;
    // /!\ La graine avance a chaque tirage. Sans ca, deux clics de suite
    // donneraient deux fois le meme patch - et l'on croirait que le bouton ne
    // marche pas.
    graineDe_ = graineDe_ * 1664525u + 1013904223u;
    romance::Des de (graineDe_);
    uint8_t prof[romance::kOctetsProfil];
    romance::tireProfil (de, prof);
    poseProfil (c, prof);
}

void RomanceProcessor::poseProfil (int c, const uint8_t* profil)
{
    if (c < 0 || c >= kNbCouches || profil == nullptr) return;

    const auto r = romance::coucheDepuisProfil (profil);
    const auto p = "c" + juce::String (c + 1) + "_";

    auto met = [this, &p] (const juce::String& suffixe, float valeur)
    {
        if (auto* par = params.getParameter (p + suffixe))
            par->setValueNotifyingHost (par->convertTo0to1 (valeur));
    };

    // ---- le materiau, puis le moteur qui le lira
    poseRecette (c, romance::depuisOctets (profil));
    // ⭐ Le DESSIN aussi est du materiau : il voyage dans le profil, comme la
    // recette, et pas comme un parametre. Genopatch peut donc rendre une forme
    // d'onde dessinee, et l'utilisateur la retouche ensuite a la souris.
    {
        // /!\ LE PROFIL NE PORTE QUE LE DESSIN A. Le morph vers B est un geste
        // de lutherie : la mesure (tools/mes_plafond.cpp) dit qu'une table
        // dessinee decrit MOINS bien un son reel que la recette - 6,84 dB
        // contre 4,77 - donc faire chercher un second dessin a Genopatch
        // couterait dix-sept octets pour un gain qu'on a mesure negatif.
        romance::DessinAB ab;
        ab.a = romance::dessinDepuisProfil (profil);
        ab.bActif = false;
        poseDessin (c, ab);
    }
    // ⭐ Et le SPECTRE. Les deux materiaux voyagent toujours ensemble dans le
    // profil ; c'est `kOffMateriau` qui dit lequel la couche lira.
    poseSpectre (c, romance::spectreDepuisProfil (profil));
    met ("moteur", (float) (int) r.moteur);

    // ---- l'enveloppe d'amplitude
    met ("att", (float) r.attaque);
    met ("dec", (float) r.decli);
    met ("sus", (float) r.tenue);
    met ("rel", (float) r.relache);

    // ---- analogique
    met ("va_f1",    (float) (int) r.va.forme1);
    met ("va_f2",    (float) (int) r.va.forme2);
    met ("va_mix",   (float) r.va.melange);
    met ("va_semi",  (float) r.va.demiTons2);
    met ("va_cents", (float) r.va.cents2);
    met ("va_larg",  (float) r.va.largeur);
    met ("va_sub",   (float) r.va.sub);
    met ("va_bruit", (float) r.va.bruit);

    // ---- FM
    met ("fm_algo", (float) r.fm.algo);
    met ("fm_ret",  (float) r.fm.retour);
    met ("fm_bris", (float) r.fm.pointBrisure);
    met ("fm_vit",  (float) r.fm.echelleVitesse);
    for (int o = 0; o < romance::kOperateurs; ++o)
    {
        const auto q = "fm" + juce::String (o + 1) + "_";
        met (q + "ratio", (float) r.fm.ratio[o]);
        met (q + "niv",   (float) r.fm.niveau[o]);
        met (q + "att",   (float) r.fm.attaque[o]);
        met (q + "dec",   (float) r.fm.decli[o]);
        met (q + "ten",   (float) r.fm.tenue[o]);
        met (q + "rel",   (float) r.fm.relache[o]);
        met (q + "ech",   (float) r.fm.echelleNiveau[o]);
        met (q + "des",   (float) r.fm.desaccord[o]);
        met (q + "vel",   (float) r.fm.veloSens[o]);
    }

    // ---- table d'ondes
    met ("tb_fam", (float) (int) r.table.famille);
    // /!\ Le menu est {Fondu, Saut} et l'enum est {Saut, Fondu} : l'ordre est
    // INVERSE. Ecrire (int) r.table.lecture poserait « Fondu » la ou la
    // recherche a trouve « Saut », sans rien signaler.
    met ("tb_lec", (r.table.lecture == romance::LectureTable::Saut) ? 1.0f : 0.0f);
    met ("tb_pos", (float) r.table.position);
    met ("tb_sym", (float) r.table.symetrie);
    {
        const auto d = romance::dessinDepuisProfil (profil);
        static const int kCell[4] = { 8, 16, 32, 64 };
        int iC = 2; for (int k = 0; k < 4; ++k) if (kCell[k] == d.nCellules) iC = k;
        met ("gr_cell", (float) iC);
        met ("gr_bits", (float) (d.bits - romance::kBitsMin));
    }

    // ---- filtre musical
    met ("topo",    (float) (int) r.topo);
    met ("coupure", (float) juce::jlimit (0.5, 250.0, r.coupureRel));
    met ("reso",    (float) r.reso);
    met ("morph",   (float) r.morph);
    met ("suivi",   (float) r.suiviClavier);
    met ("ph_coup", (float) juce::jlimit (20.0, 8000.0, r.phCoupure));
    met ("ph_reso", (float) r.phReso);
    met ("ph_pente", (r.phPente >= 2) ? 1.0f : 0.0f);

    // ---- unisson
    met ("desac",   (float) r.desaccordCents);
    met ("largeur", (float) r.largeur);

    // ---- grain
    met ("grain",    r.grain.actif ? 1.0f : 0.0f);
    met ("quant",    (float) (int) r.grain.quant);
    met ("horloge",  (float) (int) r.grain.horloge);
    met ("coup_rom", (float) juce::jlimit (0.04, 0.49, r.grain.coupure));

    // /!\ En dernier : la source doit passer sur « Recette », sinon on pose un
    // materiau que la couche ne lit pas - et l'utilisateur croit que la
    // resynthese n'a rien fait.
    // /!\ EN DERNIER, et il faut choisir le BON materiau. Poser une recette
    // en laissant la source sur « Spectre dessine » - ou l'inverse - donnerait
    // un son qui n'a rien a voir avec ce que la recherche a trouve, et
    // l'utilisateur conclurait que la resynthese ne marche pas.
    met ("source", (romance::materiauDeProfil (profil)
                        == romance::Materiau::SpectreDessine) ? 10.0f : 9.0f);
    met ("actif", 1.0f);
}

/** Fabrication des ROM, sur le thread message. */
void RomanceProcessor::handleAsyncUpdate()
{
    const int m = machineAPoser_.exchange (-1, std::memory_order_acq_rel);
    if (m >= 0) poseMachine (m);

    refaireRoms_ = false;
    // ⭐ Les deux materiaux, meme discipline : la ROM et le dessin se
    // fabriquent ICI, sur le fil message. Chacun se contente de repartir si
    // rien n'a change (mesure : 22 ms pour une banque dessinee complete).
    for (int c = 0; c < kNbCouches; ++c) { construitRom (c, false); construitDessin (c, false); }
}

void RomanceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto etat = params.copyState();
    // ⭐ Les quatre recettes voyagent avec le patch : 4 x 108 octets. C'est
    // TOUT le materiau sonore de l'instrument. Un preset complet pese moins
    // qu'une icone - et c'est la propriete qu'on perdrait en livrant des
    // samples.
    for (int c = 0; c < kNbCouches; ++c)
    {
        uint8_t o[romance::kOctetsRecette];
        romance::versOctets (recettes_[c], o);
        etat.setProperty ("recette" + juce::String (c),
                          juce::Base64::toBase64 (o, sizeof (o)), nullptr);
        // ⭐ Et le DESSIN : 64 octets, la meme logique que la recette. Ce n'est
        // pas un reglage automatisable, c'est du materiau.
        etat.setProperty ("dessin" + juce::String (c),
                          juce::Base64::toBase64 (dessins_[c].a.cellule,
                                                  sizeof (dessins_[c].a.cellule)), nullptr);
        // ⭐ Le second dessin voyage a part, et un drapeau dit s'il existe.
        //
        // /!\ SANS LE DRAPEAU, un preset enregistre AVANT le morph rechargerait
        // un B vide et sonnerait different - le dessin s'eteindrait en montant
        // « position ». Un ancien patch doit rendre exactement le meme son.
        etat.setProperty ("dessinB" + juce::String (c),
                          juce::Base64::toBase64 (dessins_[c].b.cellule,
                                                  sizeof (dessins_[c].b.cellule)), nullptr);
        etat.setProperty ("dessinBon" + juce::String (c), dessins_[c].bActif, nullptr);
        // ⭐ Et le SPECTRE : 64 octets, meme logique. Du materiau, pas un reglage.
        etat.setProperty ("spectre" + juce::String (c),
                          juce::Base64::toBase64 (spectres_[c].bande,
                                                  sizeof (spectres_[c].bande)), nullptr);
    }
    if (auto x = etat.createXml())
        copyXmlToBinary (*x, dest);
}

void RomanceProcessor::setStateInformation (const void* data, int taille)
{
    auto x = getXmlFromBinary (data, taille);
    if (x == nullptr || ! x->hasTagName (params.state.getType())) return;

    const auto etat = juce::ValueTree::fromXml (*x);
    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto b64 = etat.getProperty ("recette" + juce::String (c)).toString();
        if (b64.isEmpty()) continue;
        juce::MemoryOutputStream flux;
        if (! juce::Base64::convertFromBase64 (flux, b64)) continue;
        if (flux.getDataSize() < (size_t) romance::kOctetsRecette) continue;
        recettes_[c] = romance::depuisOctets (
                           static_cast<const uint8_t*> (flux.getData()));
        ++recetteVersion_[c];
    }
    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto b64 = etat.getProperty ("dessin" + juce::String (c)).toString();
        if (b64.isEmpty()) continue;
        juce::MemoryOutputStream flux;
        if (! juce::Base64::convertFromBase64 (flux, b64)) continue;
        if (flux.getDataSize() < sizeof (dessins_[c].a.cellule)) continue;
        std::memcpy (dessins_[c].a.cellule, flux.getData(), sizeof (dessins_[c].a.cellule));
        ++dessinVersion_[c];
    }
    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto b64 = etat.getProperty ("dessinB" + juce::String (c)).toString();
        // /!\ Un preset d'avant le morph n'a pas ces proprietes : `bActif`
        // reste faux et le patch sonne exactement comme avant.
        dessins_[c].bActif = (bool) etat.getProperty ("dessinBon" + juce::String (c), false);
        if (b64.isEmpty()) continue;
        juce::MemoryOutputStream flux;
        if (! juce::Base64::convertFromBase64 (flux, b64)) continue;
        if (flux.getDataSize() < sizeof (dessins_[c].b.cellule)) continue;
        std::memcpy (dessins_[c].b.cellule, flux.getData(), sizeof (dessins_[c].b.cellule));
        ++dessinVersion_[c];
    }
    for (int c = 0; c < kNbCouches; ++c)
    {
        const auto b64 = etat.getProperty ("spectre" + juce::String (c)).toString();
        if (b64.isEmpty()) continue;
        juce::MemoryOutputStream flux;
        if (! juce::Base64::convertFromBase64 (flux, b64)) continue;
        if (flux.getDataSize() < sizeof (spectres_[c].bande)) continue;
        std::memcpy (spectres_[c].bande, flux.getData(), sizeof (spectres_[c].bande));
        ++spectreVersion_[c];
    }
    params.replaceState (etat);

    // /!\ REPOUSSER LA VALEUR BRUTE DE CHAQUE PARAMETRE VERS L'HOTE.
    //
    // Sans ca, un parametre BOOLEEN garde la valeur normalisee qu'il avait
    // avant le rappel d'etat. JUCE stocke le brut d'un AudioParameterBool tel
    // quel (0,51 reste 0,51) et ne rafraichit pas quand le booleen DENORMALISE
    // n'a pas change - donc l'hote relit 0,51 la ou l'etat dit 1. Repere par
    // pluginval : « grain actif not restored on setStateInformation ».
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (auto* v = params.getRawParameterValue (rp->paramID))
                rp->setValueNotifyingHost (rp->convertTo0to1 (v->load()));

    for (int c = 0; c < kNbCouches; ++c) { construitRom (c, false); construitDessin (c, true); }
}

int RomanceProcessor::tetes (int couche, float* sortie, int maxi) const noexcept
{
    if (couche < 0 || couche >= kNbCouches) return 0;
    const int n = std::min (maxi, std::min (kTetesMax,
                      nTetes_[couche].load (std::memory_order_relaxed)));
    for (int i = 0; i < n; ++i)
        sortie[i] = tetes_[couche][i].load (std::memory_order_relaxed);
    return n;
}

juce::AudioProcessorEditor* RomanceProcessor::createEditor()
{
    return new RomanceEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RomanceProcessor();
}
