// =============================================================================
//  train.cpp - Entrainer le reseau, en C++ pur.
//
//  ⭐ POURQUOI PAS PYTORCH. Trois raisons, dans cet ordre :
//    1. l'entrainement partage LES MEMES EN-TETES que le plugin, donc l'entree
//       vue a l'entrainement est celle vue a l'inference, par construction et
//       pas par discipline ;
//    2. aucune installation sur la machine de l'utilisateur (2,5 Go de CUDA
//       pour un perceptron de 600 000 poids, ce serait disproportionne) ;
//    3. les poids sortent directement dans le format que le plugin lit.
//  Un reseau de cette taille tient largement sur douze coeurs.
//
//  /!\ LA METRIQUE PRINCIPALE N'EST PAS LA PERTE D'ENTRAINEMENT. C'est la
//  RECUPERATION DES PARAMETRES sur un lot tenu a part : erreur en cents sur la
//  hauteur, en dB sur les amplitudes. La verite y est connue, donc l'erreur est
//  non ambigue - c'est la seule mesure dont on soit sur. Une perte qui baisse
//  ne prouve rien toute seule, on l'a deja verifie deux fois sur ce projet.
//
//  Usage : train.exe <jeu.bin> <sortie.rnn> [epoques] [threads] [reprise.rnn]
// =============================================================================
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "Perte.h"
#include "Recette.h"
#include "Reseau.h"

using namespace romance;

// ------------------------------------------------------------------ donnees

struct Jeu
{
    int n = 0, nEnt = 0, nCib = 0;
    std::vector<int8_t>  entrees;
    std::vector<float>   fenetres;
    std::vector<uint8_t> cibles;
    bool ok = false;
};

static Jeu litJeu (const std::string& chemin)
{
    Jeu j;
    FILE* f = std::fopen (chemin.c_str(), "rb");
    if (! f) return j;
    char sig[6];
    // /!\ DEUX SIGNATURES, ET C'EST LE DEFAUT QUI A TUE L'ETAPE 9.
    // `dataset.exe` ecrit "ROMDS\1", `distille.exe` ecrit "ROMDS\2" - meme
    // format, numero de version different. Ce lecteur n'acceptait que la
    // premiere, donc l'affinage sur les sons reels n'a JAMAIS pu tourner :
    // « jeu illisible », code de retour 1, et le script continuait quand meme.
    if (std::fread (sig, 1, 6, f) != 6
        || std::memcmp (sig, "ROMDS", 5) != 0
        || (sig[5] != 1 && sig[5] != 2))
    { std::fclose (f); return j; }
    uint32_t n = 0, te = 0, tc = 0;
    std::fread (&n, 4, 1, f); std::fread (&te, 4, 1, f); std::fread (&tc, 4, 1, f);
    j.n = (int) n; j.nEnt = (int) te; j.nCib = (int) tc;
    if (j.nEnt != kTailleReduite || j.nCib != kOctetsProfil)
    { std::fclose (f); return j; }
    j.entrees.resize ((size_t) j.n * j.nEnt);
    j.fenetres.assign ((size_t) j.n, 1.5f);
    j.cibles.resize ((size_t) j.n * j.nCib);
    for (int i = 0; i < j.n; ++i)
    {
        if (std::fread (j.entrees.data() + (size_t) i * j.nEnt, 1, (size_t) te, f) != te) break;
        if (std::fread (&j.fenetres[(size_t) i], 4, 1, f) != 1) break;
        if (std::fread (j.cibles.data()  + (size_t) i * j.nCib, 1, (size_t) tc, f) != tc) break;
    }
    std::fclose (f);
    j.ok = true;
    return j;
}

/** int8 -> dB, l'inverse exact de la quantification de dataset.cpp. */
static inline float versDb (int8_t q) noexcept
{
    return (float) (((double) q + 128.0) / 255.0 * (-kPlancherPropre) + kPlancherPropre);
}

// ------------------------------------------------------- poids qui bougent

struct Gradients
{
    std::vector<float> W1, b1, W2, b2, W3, b3;
    void dimensionne (const Reseau& r)
    {
        W1.assign (r.W1.size(), 0.0f); b1.assign (r.b1.size(), 0.0f);
        W2.assign (r.W2.size(), 0.0f); b2.assign (r.b2.size(), 0.0f);
        W3.assign (r.W3.size(), 0.0f); b3.assign (r.b3.size(), 0.0f);
    }
    void zero()
    {
        std::fill (W1.begin(), W1.end(), 0.0f); std::fill (b1.begin(), b1.end(), 0.0f);
        std::fill (W2.begin(), W2.end(), 0.0f); std::fill (b2.begin(), b2.end(), 0.0f);
        std::fill (W3.begin(), W3.end(), 0.0f); std::fill (b3.begin(), b3.end(), 0.0f);
    }
    void ajoute (const Gradients& g)
    {
        for (size_t i = 0; i < W1.size(); ++i) W1[i] += g.W1[i];
        for (size_t i = 0; i < b1.size(); ++i) b1[i] += g.b1[i];
        for (size_t i = 0; i < W2.size(); ++i) W2[i] += g.W2[i];
        for (size_t i = 0; i < b2.size(); ++i) b2[i] += g.b2[i];
        for (size_t i = 0; i < W3.size(); ++i) W3[i] += g.W3[i];
        for (size_t i = 0; i < b3.size(); ++i) b3[i] += g.b3[i];
    }
};

/** ⭐ TOUS LES OCTETS NE SE VALENT PAS. Se tromper d'un cran sur la hauteur
    (octets 1-2) rend le son inutilisable ; se tromper d'un cran sur
    l'amplitude du 19e partiel ne s'entend pas. Une perte uniforme sur les 108
    octets apprendrait au reseau a soigner les details et a rater l'essentiel.
    Les poids ci-dessous ne sont pas arbitraires : ils suivent l'ordre dans
    lequel une erreur s'entend. */
static float poidsOctet (int i) noexcept
{
    // ⭐ L'emplacement 1 ne porte plus l'octet bas de f0 mais le LOG DE LA
    // HAUTEUR, normalise et continu (voir Reseau.h). L'emplacement 2 ne sert
    // plus a rien : l'octet haut se recompose.
    // /!\ ZERO : la hauteur vient de l'analyse, pas du reseau (voir Reseau.h).
    if (i == kSlotF0 || i == 2) return 0.0f;
    if (i == 88 || i == 91) return 0.0f;    // octets bas des ratios : indevinables
    if (i == 3)  return 3.0f;               // duree d'attaque
    if (i == 4)  return 2.0f;               // longueur de boucle
    if (i == 5)  return 1.5f;               // inharmonicite
    if (i >= 6  && i <= 8)  return 2.0f;    // bruit 1
    if (i >= 9  && i <= 11) return 2.0f;    // transitoire
    if (i == 14) return 2.0f;               // nombre de partiels
    if (i >= 16 && i <= 87)
    {
        // les premiers partiels portent l'essentiel de l'energie
        const int k = (i - 16) / 3;
        const int champ = (i - 16) % 3;
        const float rang = 1.0f / (1.0f + 0.20f * (float) k);
        // amplitude > decroissance > desaccord
        const float ch = (champ == 0) ? 3.0f : (champ == 1 ? 2.0f : 1.0f);
        return rang * ch;
    }
    if (i >= 88 && i <= 93) return 1.5f;    // multi-f0
    if (i >= 94 && i <= 96) return 2.0f;    // enveloppe de hauteur
    if (i >= 97 && i <= 99) return 1.5f;    // bruit 2
    if (i < kOctetsRecette) return 0.0f;    // version, graine, reserve

    // ---- ⭐ LE RESTE DU PROFIL : moteur, generateurs, filtre, modulations, fx
    //
    // /!\ LE MOTEUR PORTE LE POIDS LE PLUS LOURD DU JEU, et de loin. Se
    // tromper d'un partiel coute un peu de timbre ; se tromper de moteur rend
    // les soixante autres octets sans objet, puisqu'ils decrivent un
    // generateur qui n'est pas celui-la. C'est la seule sortie ou l'erreur
    // n'est pas graduelle.
    if (i == kOffMoteur) return 8.0f;

    if (i >= kOffEnv && i < kOffEnv + 4) return 3.0f;

    // /!\ Les octets d'un generateur INACTIF sont dans la cible a leur valeur
    // neutre - le prior ne les tire pas. Les ponderer normalement apprendrait
    // au reseau a predire « neutre » les trois quarts du temps, ce qui est vrai
    // en moyenne et inutile en pratique. On les garde a poids modere : c'est le
    // moteur devine qui decidera lesquels comptent.
    if (i >= kOffVA    && i < kOffVA + 8)    return 1.5f;
    if (i >= kOffFM    && i < kOffFM + 14)   return 1.2f;
    if (i >= kOffTable && i < kOffTable + 4) return 1.5f;

    if (i >= kOffFiltre && i < kOffFiltre + 5) return 2.5f;
    if (i >= kOffUni   && i < kOffUni + 2)   return 1.0f;
    if (i >= kOffGrain && i < kOffGrain + 3) return 1.5f;
    if (i >= kOffMod   && i < kOffMod + kModsProfil * 5)   return 1.0f;
    if (i >= kOffCable && i < kOffCable + kModsProfil * 2) return 1.5f;
    if (i >= kOffFx    && i < kOffFx + 15)   return 1.5f;
    return 0.0f;
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage : train <jeu.bin> <sortie.rnn> [epoques] [threads] [reprise.rnn]\n");
        return 1;
    }
    const std::string cheminJeu = argv[1], cheminOut = argv[2];
    const int nEpoques = (argc > 3) ? std::atoi (argv[3]) : 30;
    // /!\ ZERO FIL EST IMPOSSIBLE, et il faut le forcer ici plutot que de
    // compter sur l'appelant. `tools/train.ps1` a passe 0 en croyant dire
    // « automatique » : la boucle de gradients ne s'executait alors JAMAIS, la
    // perte affichee valait 0,00000 a chaque epoque, le reseau restait
    // aleatoire - et le script se terminait avec succes. Trente epoques en deux
    // minutes sur 200 000 exemples : le seul indice etait la duree.
    const int autoFils = (int) std::max (1u, std::thread::hardware_concurrency());
    const int demande = (argc > 4) ? std::atoi (argv[4]) : autoFils;
    const int nThreads = (demande > 0) ? demande : autoFils;
    const std::string reprise = (argc > 5) ? argv[5] : "";

    std::printf ("\nROMANCE - entrainement\n----------------------\n");
    Jeu j = litJeu (cheminJeu);
    if (! j.ok || j.n < 100) { std::printf ("  !! jeu illisible : %s\n", cheminJeu.c_str()); return 1; }

    // /!\ Un lot de VALIDATION tenu a part, jamais vu a l'entrainement. Sans
    // lui on ne mesure que la capacite du reseau a retenir, pas a generaliser.
    // /!\ Sur un petit jeu, un huitieme ne fait que quelques exemples : la
    // validation devient du bruit. On en garde un cinquieme, et on le DIT.
    const int nVal = (j.n < 500) ? std::max (4, j.n / 5) : std::min (4000, j.n / 8);
    if (j.n < 500)
        std::printf ("  /!\\ petit jeu : la validation ne porte que sur %d exemples,\n"
                     "      elle indique une tendance, pas une mesure.\n", nVal);
    const int nEnt = j.n - nVal;
    std::printf ("  %d exemples (%d entrainement, %d validation), %d threads\n",
                 j.n, nEnt, nVal, nThreads);

    Reseau r;
    r.dimensionne();
    if (! reprise.empty() && r.lit (reprise))
        std::printf ("  reprise depuis %s\n", reprise.c_str());
    else
    {
        // initialisation de He, adaptee aux ReLU a fuite
        unsigned int e = 987654321u;
        auto al = [&e] () { e = e * 1664525u + 1013904223u;
                            return (double) (e >> 8) / 8388608.0 - 1.0; };
        const double s1 = std::sqrt (2.0 / (double) r.nEntree);
        const double s2 = std::sqrt (2.0 / (double) r.nC1);
        const double s3 = std::sqrt (2.0 / (double) r.nC2);
        for (auto& w : r.W1) w = (float) (al() * s1 * 1.732);
        for (auto& w : r.W2) w = (float) (al() * s2 * 1.732);
        for (auto& w : r.W3) w = (float) (al() * s3 * 1.732);
    }

    std::vector<float> poids ((size_t) kOctetsProfil);
    float sommePoids = 0.0f;
    for (int i = 0; i < kOctetsProfil; ++i)
    { poids[(size_t) i] = poidsOctet (i); sommePoids += poids[(size_t) i]; }

    // Adam
    Gradients m, v, gTotal;
    m.dimensionne (r); v.dimensionne (r); gTotal.dimensionne (r);
    const float lr0 = 0.0015f, beta1 = 0.9f, beta2 = 0.999f, eps = 1e-8f;
    long long pas = 0;

    // ⭐⭐ LE LOT S'ADAPTE AU JEU, et ce n'est pas un reglage de confort.
    //
    // /!\ FIXE A 256, IL ANNULAIT TOUTE L'ETAPE 9. La boucle s'ecrit
    // `for (deb = 0; deb + lot <= nEnt; deb += lot)` : avec 188 exemples
    // d'entrainement - la taille du jeu distille - la condition est fausse des
    // le premier tour. ZERO lot, ZERO mise a jour, et l'affichage rendait
    // « perte 0.00000 » a chaque epoque parce que la somme est divisee par
    // max(1, nLots). C'est exactement la signature du piege 14, au meme
    // endroit du raisonnement : une division qui fabrique un zero rassurant a
    // partir d'un travail qui n'a pas eu lieu.
    const int lot = std::max (8, std::min (256, nEnt / 4));
    std::vector<int> ordre ((size_t) nEnt);
    for (int i = 0; i < nEnt; ++i) ordre[(size_t) i] = i;
    unsigned int melange = 12345u;

    std::vector<Gradients> gPar ((size_t) nThreads);
    for (auto& g : gPar) g.dimensionne (r);

    double meilleureVal = 1e30;

    for (int ep = 0; ep < nEpoques; ++ep)
    {
        // melange de Fisher-Yates
        for (int i = nEnt - 1; i > 0; --i)
        {
            melange = melange * 1664525u + 1013904223u;
            const int k = (int) (melange % (unsigned int) (i + 1));
            std::swap (ordre[(size_t) i], ordre[(size_t) k]);
        }

        const float lr = lr0 * (float) std::pow (0.94, (double) ep);
        double perteEp = 0.0;
        int nLots = 0;

        for (int deb = 0; deb + lot <= nEnt; deb += lot)
        {
            for (auto& g : gPar) g.zero();
            std::atomic<double> perteLot { 0.0 };

            auto travail = [&] (int idT)
            {
                Gradients& g = gPar[(size_t) idT];
                std::vector<float> ent ((size_t) r.nEntree), h1, h2,
                                   sor ((size_t) r.nSortie),
                                   dSor ((size_t) r.nSortie),
                                   dH2 ((size_t) r.nC2), dH1 ((size_t) r.nC1);
                double pl = 0.0;
                for (int b = idT; b < lot; b += nThreads)
                {
                    const int ix = ordre[(size_t) (deb + b)];
                    const int8_t* src = j.entrees.data() + (size_t) ix * j.nEnt;
                    for (int i = 0; i < kTailleReduite; ++i) ent[(size_t) i] = versDb (src[i]);
                    const uint8_t* cib0 = j.cibles.data() + (size_t) ix * j.nCib;
                    {
                        const double fv = ((double) ((uint16_t) cib0[1]
                                           | ((uint16_t) cib0[2] << 8))) / 16.0;
                        ent[(size_t) kTailleReduite] =
                            (float) (2.0 * normeDepuisF0 (fv) - 1.0);
                        ent[(size_t) (kTailleReduite + 1)] = (float)
                            (2.0 * normeDepuisFenetre ((double) j.fenetres[(size_t) ix]) - 1.0);
                    }
                    r.avant (ent.data(), sor.data(), h1, h2);

                    const uint8_t* cib = j.cibles.data() + (size_t) ix * j.nCib;
                    // /!\ La cible de l'emplacement f0 est CONTINUE, pas l'octet.
                    const double f0v = ((double) ((uint16_t) cib[1]
                                        | ((uint16_t) cib[2] << 8))) / 16.0;
                    const float cibleF0 = (float) normeDepuisF0 (f0v);
                    for (int i = 0; i < r.nSortie; ++i)
                    {
                        const float t = (i == kSlotF0) ? cibleF0
                                                       : (float) cib[i] / 255.0f;
                        const float d = sor[(size_t) i] - t;
                        const float w = poids[(size_t) i];
                        pl += (double) (w * d * d);
                        // sigmoide + erreur quadratique : la derivee porte deja
                        // le facteur s(1-s)
                        dSor[(size_t) i] = 2.0f * w * d
                                         * sor[(size_t) i] * (1.0f - sor[(size_t) i]);
                    }

                    // couche 3
                    std::fill (dH2.begin(), dH2.end(), 0.0f);
                    for (int o = 0; o < r.nSortie; ++o)
                    {
                        const float d = dSor[(size_t) o];
                        if (d == 0.0f) continue;
                        float* gw = g.W3.data() + (size_t) o * r.nC2;
                        const float* w = r.W3.data() + (size_t) o * r.nC2;
                        for (int i = 0; i < r.nC2; ++i)
                        { gw[i] += d * h2[(size_t) i]; dH2[(size_t) i] += d * w[i]; }
                        g.b3[(size_t) o] += d;
                    }
                    // couche 2
                    std::fill (dH1.begin(), dH1.end(), 0.0f);
                    for (int o = 0; o < r.nC2; ++o)
                    {
                        const float d = dH2[(size_t) o] * ((h2[(size_t) o] > 0.0f) ? 1.0f : 0.01f);
                        float* gw = g.W2.data() + (size_t) o * r.nC1;
                        const float* w = r.W2.data() + (size_t) o * r.nC1;
                        for (int i = 0; i < r.nC1; ++i)
                        { gw[i] += d * h1[(size_t) i]; dH1[(size_t) i] += d * w[i]; }
                        g.b2[(size_t) o] += d;
                    }
                    // couche 1
                    for (int o = 0; o < r.nC1; ++o)
                    {
                        const float d = dH1[(size_t) o] * ((h1[(size_t) o] > 0.0f) ? 1.0f : 0.01f);
                        if (d == 0.0f) continue;
                        float* gw = g.W1.data() + (size_t) o * r.nEntree;
                        for (int i = 0; i < r.nEntree - 1; ++i)
                            gw[i] += d * normeEntree (ent[(size_t) i]);
                        gw[r.nEntree - 1] += d * ent[(size_t) (r.nEntree - 1)];
                        g.b1[(size_t) o] += d;
                    }
                }
                double att = perteLot.load();
                while (! perteLot.compare_exchange_weak (att, att + pl)) {}
            };

            std::vector<std::thread> fils;
            for (int t = 0; t < nThreads; ++t) fils.emplace_back (travail, t);
            for (auto& f : fils) f.join();

            gTotal.zero();
            for (auto& g : gPar) gTotal.ajoute (g);

            ++pas;
            const float c1 = 1.0f - (float) std::pow ((double) beta1, (double) pas);
            const float c2 = 1.0f - (float) std::pow ((double) beta2, (double) pas);
            const float inv = 1.0f / (float) lot;

            auto maj = [&] (std::vector<float>& w, std::vector<float>& gg,
                            std::vector<float>& mm, std::vector<float>& vv)
            {
                for (size_t i = 0; i < w.size(); ++i)
                {
                    const float g = gg[i] * inv;
                    mm[i] = beta1 * mm[i] + (1.0f - beta1) * g;
                    vv[i] = beta2 * vv[i] + (1.0f - beta2) * g * g;
                    w[i] -= lr * (mm[i] / c1) / (std::sqrt (vv[i] / c2) + eps);
                }
            };
            maj (r.W1, gTotal.W1, m.W1, v.W1); maj (r.b1, gTotal.b1, m.b1, v.b1);
            maj (r.W2, gTotal.W2, m.W2, v.W2); maj (r.b2, gTotal.b2, m.b2, v.b2);
            maj (r.W3, gTotal.W3, m.W3, v.W3); maj (r.b3, gTotal.b3, m.b3, v.b3);

            perteEp += perteLot.load() / (double) lot;
            ++nLots;
        }

        // --- validation, et surtout N1 : la recuperation des parametres
        double perteVal = 0.0, errF0Cents = 0.0, errAmpDb = 0.0;
        int nF0 = 0, nAmp = 0;
        {
            std::vector<float> ent ((size_t) r.nEntree), h1, h2,
                               sor ((size_t) r.nSortie);
            for (int k = 0; k < nVal; ++k)
            {
                const int ix = nEnt + k;
                const int8_t* src = j.entrees.data() + (size_t) ix * j.nEnt;
                for (int i = 0; i < kTailleReduite; ++i) ent[(size_t) i] = versDb (src[i]);
                const uint8_t* cib = j.cibles.data() + (size_t) ix * j.nCib;
                {
                    const double fv = ((double) ((uint16_t) cib[1]
                                       | ((uint16_t) cib[2] << 8))) / 16.0;
                    ent[(size_t) kTailleReduite] =
                        (float) (2.0 * normeDepuisF0 (fv) - 1.0);
                    ent[(size_t) (kTailleReduite + 1)] = (float)
                        (2.0 * normeDepuisFenetre ((double) j.fenetres[(size_t) ix]) - 1.0);
                }
                r.avant (ent.data(), sor.data(), h1, h2);

                const double f0v = ((double) ((uint16_t) cib[1]
                                    | ((uint16_t) cib[2] << 8))) / 16.0;
                const float cibleF0 = (float) normeDepuisF0 (f0v);
                uint8_t pred[kOctetsProfil];
                for (int i = 0; i < r.nSortie; ++i)
                {
                    const float t = (i == kSlotF0) ? cibleF0 : (float) cib[i] / 255.0f;
                    const float d = sor[(size_t) i] - t;
                    perteVal += (double) (poids[(size_t) i] * d * d);
                    pred[i] = (uint8_t) std::max (0.0f, std::min (255.0f,
                                std::round (sor[(size_t) i] * 255.0f)));
                }
                pred[0] = 2;
                {
                    const double f0p = f0DepuisNorme ((double) sor[(size_t) kSlotF0]);
                    const uint16_t q = (uint16_t) std::max (0.0, std::min (65535.0,
                                            std::round (f0p * 16.0)));
                    pred[1] = (uint8_t) (q & 0xFF); pred[2] = (uint8_t) (q >> 8);
                    pred[88] = 0; pred[91] = 0;
                }
                const Recette rp = depuisOctets (pred);
                const Recette rv = depuisOctets (cib);
                if (rv.f0 > 20.0 && rp.f0 > 20.0)
                { errF0Cents += std::abs (1200.0 * std::log2 (rp.f0 / rv.f0)); ++nF0; }
                for (int p = 0; p < std::min (8, rv.nPartiels); ++p)
                {
                    const double a = std::max (1e-4, rv.partiels[p].amplitude);
                    const double b = std::max (1e-4, rp.partiels[p].amplitude);
                    errAmpDb += std::abs (20.0 * std::log10 (b / a)); ++nAmp;
                }
            }
        }
        perteVal /= (double) std::max (1, nVal);

        // /!\ SIX EMPLACEMENTS, CINQ ARGUMENTS. Le compilateur le disait
        // (warning C4473) et je suis passe a cote : la colonne « f0 » affichait
        // en fait l'erreur d'AMPLITUDE, et la colonne « amplitudes » lisait de
        // la memoire non initialisee - d'ou son 0,0 dB immuable, pendant un
        // entrainement qui marchait comme pendant un qui ne faisait rien. Une
        // metrique constante n'est pas une bonne nouvelle, c'est un soupcon.
        std::printf ("  epoque %2d/%d   perte %.5f   val %.5f   "
                     "f0 %.1f cents   amplitudes %.1f dB\n",
                     ep + 1, nEpoques, perteEp / std::max (1, nLots), perteVal,
                     errF0Cents / std::max (1, nF0),
                     errAmpDb / std::max (1, nAmp));
        std::fflush (stdout);

        // /!\ UN RESEAU QUI N'A RECU AUCUNE MISE A JOUR NE DOIT PAS ETRE
        // ECRIT. Sans ce garde-fou, l'etape 9 a produit un `.rnn` complet et
        // valide - donc charge sans broncher par le plugin - a partir de zero
        // apprentissage. Le fichier existait, la chaine se terminait en vert,
        // et l'evaluation finale jugeait un reseau que personne n'avait
        // entraine.
        if (nLots == 0)
        {
            std::printf ("  !! aucun lot : le jeu (%d exemples d'entrainement) est "
                         "plus petit qu'un lot. Rien n'a ete appris.\n", nEnt);
            return 3;
        }

        if (perteVal < meilleureVal)
        {
            meilleureVal = perteVal;
            r.ecrit (cheminOut);
        }
    }

    std::printf ("  meilleure validation %.5f -> %s\n", meilleureVal, cheminOut.c_str());
    return 0;
}
