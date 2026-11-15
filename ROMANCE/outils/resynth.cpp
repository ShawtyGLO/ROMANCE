// =============================================================================
//  resynth.cpp - Un son en entree, 108 octets en sortie.
//
//  ⭐ CET OUTIL UTILISE EXACTEMENT LES MEMES EN-TETES QUE LE PLUGIN. Pas une
//  reimplementation, pas un portage : le meme Recette.h, le meme Grain.h, la
//  meme Perte.h. Sinon on optimiserait pour une machine qu'on n'a pas, et la
//  recette trouvee ne sonnerait pas pareil une fois chargee.
//
//  Usage :
//    resynth.exe <fichier.wav> [budget] [graine]
//    resynth.exe --dossier <dossier>          (tous les .wav)
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "Analyse.h"
#include "Perte.h"
#include "Recette.h"
#include "Recherche.h"

using namespace romance;

// ------------------------------------------------------------------ WAV

struct Son
{
    std::vector<double> x;
    double fs = 44100.0;
    bool ok = false;
};

/** Lecteur WAV minimal : PCM 8/16/24/32 et flottant 32/64, mixe en mono. */
static Son litWav (const char* chemin)
{
    Son s;
    FILE* f = std::fopen (chemin, "rb");
    if (! f) { std::printf ("  !! fichier introuvable\n"); return s; }

    char id[4]; uint32_t taille = 0;
    std::fread (id, 1, 4, f);
    if (std::memcmp (id, "RIFF", 4) != 0) { std::fclose (f); return s; }
    std::fread (&taille, 4, 1, f);
    std::fread (id, 1, 4, f);
    if (std::memcmp (id, "WAVE", 4) != 0) { std::fclose (f); return s; }

    uint16_t format = 1, canaux = 1, bits = 16;
    std::vector<uint8_t> donnees;
    while (std::fread (id, 1, 4, f) == 4)
    {
        uint32_t n = 0;
        if (std::fread (&n, 4, 1, f) != 1) break;
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            std::vector<uint8_t> b (n);
            std::fread (b.data(), 1, n, f);
            if (n >= 16)
            {
                std::memcpy (&format, b.data() + 0, 2);
                std::memcpy (&canaux, b.data() + 2, 2);
                uint32_t sr = 0; std::memcpy (&sr, b.data() + 4, 4);
                s.fs = (double) sr;
                std::memcpy (&bits, b.data() + 14, 2);
            }
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            donnees.resize (n);
            std::fread (donnees.data(), 1, n, f);
        }
        else
        {
            std::fseek (f, (long) n, SEEK_CUR);
        }
        if (n & 1) std::fseek (f, 1, SEEK_CUR);
    }
    std::fclose (f);
    if (donnees.empty() || canaux == 0) return s;

    const int oct = bits / 8;
    const size_t nEch = donnees.size() / (size_t) (oct * canaux);
    s.x.resize (nEch);
    for (size_t i = 0; i < nEch; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < canaux; ++c)
        {
            const uint8_t* p = donnees.data() + (i * canaux + (size_t) c) * (size_t) oct;
            double v = 0.0;
            if (format == 3 && bits == 32)      { float t; std::memcpy (&t, p, 4); v = t; }
            else if (format == 3 && bits == 64) { double t; std::memcpy (&t, p, 8); v = t; }
            else if (bits == 8)  v = ((double) p[0] - 128.0) / 128.0;
            else if (bits == 16) { int16_t t; std::memcpy (&t, p, 2); v = t / 32768.0; }
            else if (bits == 24)
            {
                int32_t t = (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8)
                                       | ((uint32_t) p[2] << 16));
                if (t & 0x800000) t |= (int32_t) 0xFF000000;
                v = t / 8388608.0;
            }
            else if (bits == 32) { int32_t t; std::memcpy (&t, p, 4); v = t / 2147483648.0; }
            acc += v;
        }
        s.x[i] = acc / (double) canaux;
    }
    s.ok = true;
    return s;
}

static void ecritWav (const char* chemin, const std::vector<double>& x, double fs)
{
    FILE* f = std::fopen (chemin, "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) x.size();
    const uint32_t octets = n * 2;
    const uint32_t sr = (uint32_t) fs;
    const uint32_t ba = sr * 2; const uint16_t un = 1, deux = 2, seize = 16;
    const uint32_t t16 = 16, taille = 36 + octets;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&taille, 4, 1, f);
    std::fwrite ("WAVE", 1, 4, f); std::fwrite ("fmt ", 1, 4, f);
    std::fwrite (&t16, 4, 1, f); std::fwrite (&un, 2, 1, f); std::fwrite (&un, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&ba, 4, 1, f);
    std::fwrite (&deux, 2, 1, f); std::fwrite (&seize, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&octets, 4, 1, f);
    for (double v : x)
    {
        const double c = std::max (-1.0, std::min (1.0, v));
        const int16_t s = (int16_t) std::lround (c * 32767.0);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// ------------------------------------------------------------------ main

static int traite (const std::string& chemin, int budget, unsigned int graine)
{
    const auto pos = chemin.find_last_of ("\\/");
    const std::string nom = (pos == std::string::npos) ? chemin : chemin.substr (pos + 1);
    std::printf ("\n=== %s\n", nom.c_str());

    const Son s = litWav (chemin.c_str());
    if (! s.ok) { std::printf ("  !! lecture impossible\n"); return 1; }
    std::printf ("  %.2f s a %.0f Hz\n", (double) s.x.size() / s.fs, s.fs);

    ReglagesRecherche reg;
    reg.evaluations = budget;
    reg.graine = graine;
    // ⭐ On ajuste PROPREMENT. Le grain est une saveur qu'on applique apres, au
    // choix - pas quelque chose qu'on cuit dans le materiau.
    const auto cfg = grainTransparent (s.fs);

    RapportAnalyse rap;
    int dernier = -1;
    const auto st = resynthetise (s.x, s.fs, cfg, reg, rap,
        [&] (const EtatRecherche& e)
        {
            const int pct = 10 * (e.evaluationsFaites * 10 / std::max (1, budget));
            if (pct != dernier)
            {
                dernier = pct;
                std::printf ("\r  recherche %3d %%  perte %.3f dB  pas %.3f      ",
                             pct, e.perteMeilleure, e.sigma);
                std::fflush (stdout);
            }
            return true;
        });
    std::printf ("\r                                                          \r");

    std::printf ("  analyse : f0 %.2f Hz%s, %d partiels, bruit %.0f %%",
                 rap.f0, rap.f0SurLaBorne ? "  /!\\ SUR LA BORNE" : "",
                 rap.nPartiels, 100.0 * rap.partBruit);
    if (std::abs (rap.balayageDemiTons) > 0.5)
        std::printf (", balayage %+.1f demi-tons", rap.balayageDemiTons);
    std::printf ("\n");

    // /!\ On affiche la part des bandes au plancher AVEC le score : sans elle
    // un joli chiffre peut ne rien vouloir dire (cf. BASS 1 et ses 0,30 dB).
    {
        std::vector<double> c (s.x.begin() + trouveOnset (s.x), s.x.end());
        if ((int) c.size() > reg.nEchComparaison) c.resize ((size_t) reg.nEchComparaison);
        std::printf ("  cible   : %.0f %% des bandes au plancher "
                     "(au-dela de ~80 %%, le score n'est pas comparable)\n",
                     100.0 * partAuPlancher (empreinte (c, s.fs)));
    }

    const double gain = st.perteDepart - st.perteMeilleure;
    std::printf ("  perte   : amorce %.3f dB  ->  %.3f dB   (%+.1f %%, %d evaluations)\n",
                 st.perteDepart, st.perteMeilleure,
                 -100.0 * gain / std::max (st.perteDepart, 1e-9),
                 st.evaluationsFaites);

    // --- sorties : le PROFIL entier, et un rendu a ecouter
    const std::string base = chemin.substr (0, chemin.find_last_of ('.'));
    if (FILE* f = std::fopen ((base + ".profil").c_str(), "wb"))
    {
        std::fwrite (st.profil, 1, kOctetsProfil, f);
        std::fclose (f);
    }
    static const char* kNomMoteur[] = { "ROM", "analogique", "FM", "table d'ondes" };
    std::printf ("  moteur  : %s\n",
                 kNomMoteur[(int) coucheDepuisProfil (st.profil).moteur]);
    // /!\ DEUX rendus, et c'est tout le point : le MEME profil, propre puis
    // avec le grain de la machine. C'est a l'oreille de choisir la saveur, pas
    // a la recherche de l'avoir decidee.
    const int nS = (int) std::min (s.x.size(), (size_t) (s.fs * 3.0));
    const double f0Rendu = (rap.f0 > 0.0) ? rap.f0 : 220.0;
    std::vector<double> y;
    uint8_t prof[kOctetsProfil];
    for (int i = 0; i < kOctetsProfil; ++i) prof[i] = st.profil[i];
    prof[kOffGrain + 0] = 0;                       // sans grain
    rendProfil (prof, y, nS, s.fs, f0Rendu);
    ecritWav ((base + " - resynth propre.wav").c_str(), y, s.fs);
    prof[kOffGrain + 0] = 64 + 64;                 // grain actif, mu-law
    rendProfil (prof, y, nS, s.fs, f0Rendu);
    ecritWav ((base + " - resynth grain.wav").c_str(), y, s.fs);
    const std::string court = nom.substr (0, nom.find_last_of ('.'));
    std::printf ("  ecrit   : %s.profil (184 o), \"%s - resynth propre.wav\""
                 " et \"%s - resynth grain.wav\"\n",
                 court.c_str(), court.c_str(), court.c_str());
    return 0;
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage : resynth <fichier.wav> [budget] [graine]\n");
        return 1;
    }
    const int budget = (argc > 2) ? std::atoi (argv[2]) : 3000;
    const unsigned int graine = (argc > 3) ? (unsigned int) std::atoi (argv[3]) : 12345u;
    return traite (argv[1], budget, graine);
}
