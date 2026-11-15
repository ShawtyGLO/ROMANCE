// =============================================================================
//  wav.h - Lecture et ecriture WAV minimales, partagees par les outils.
//
//  /!\ Extrait de resynth.cpp pour que corpus.cpp lise EXACTEMENT de la meme
//  facon. Deux lecteurs qui different d'un facteur 2 sur le 24 bits, et toutes
//  les statistiques du corpus sont fausses sans que rien ne le signale.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace outils
{

struct Son
{
    std::vector<double> x;
    double fs = 44100.0;
    bool ok = false;
};

/** PCM 8/16/24/32 et flottant 32/64, mixe en mono. */
inline Son litWav (const char* chemin)
{
    Son s;
    FILE* f = std::fopen (chemin, "rb");
    if (! f) return s;

    char id[4]; uint32_t taille = 0;
    if (std::fread (id, 1, 4, f) != 4 || std::memcmp (id, "RIFF", 4) != 0)
    { std::fclose (f); return s; }
    if (std::fread (&taille, 4, 1, f) != 1) { std::fclose (f); return s; }
    if (std::fread (id, 1, 4, f) != 4 || std::memcmp (id, "WAVE", 4) != 0)
    { std::fclose (f); return s; }

    uint16_t format = 1, canaux = 1, bits = 16;
    std::vector<uint8_t> donnees;
    while (std::fread (id, 1, 4, f) == 4)
    {
        uint32_t n = 0;
        if (std::fread (&n, 4, 1, f) != 1) break;
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            std::vector<uint8_t> b (n);
            if (std::fread (b.data(), 1, n, f) != n) break;
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
            if (std::fread (donnees.data(), 1, n, f) != n) donnees.clear();
            break;      // on n'a pas besoin de ce qui suit
        }
        else
        {
            std::fseek (f, (long) n, SEEK_CUR);
        }
        if (n & 1) std::fseek (f, 1, SEEK_CUR);
    }
    std::fclose (f);
    if (donnees.empty() || canaux == 0 || bits == 0 || s.fs < 1000.0) return s;

    const int oct = bits / 8;
    const size_t nEch = donnees.size() / (size_t) (oct * canaux);
    if (nEch == 0) return s;
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

inline void ecritWav (const char* chemin, const std::vector<double>& x, double fs)
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
        const double c = (v < -1.0) ? -1.0 : (v > 1.0 ? 1.0 : v);
        const int16_t s = (int16_t) (c * 32767.0 + (c >= 0.0 ? 0.5 : -0.5));
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

} // namespace outils
