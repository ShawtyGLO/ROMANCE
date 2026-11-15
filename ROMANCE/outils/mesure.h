// =============================================================================
//  mesure.h - Ce qu'on retient d'un son, et rien de plus.
//
//  /!\ PARTAGE PAR corpus.cpp ET dataset.cpp. Le corpus definit la region ou
//  l'on veut tirer ; le jeu d'entrainement doit etre juge par EXACTEMENT la
//  meme mesure, sinon la comparaison ne veut rien dire et le taux
//  d'acceptation ne mesure plus rien.
//
//  ⭐ Volontairement peu de chiffres, et chacun correspond a un axe que la
//  recette sait bouger. Mesurer ce que le generateur ne peut pas reproduire ne
//  sert qu'a se faire peur.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "Analyse.h"
#include "Perte.h"
#include "wav.h"

namespace outils
{

using namespace romance;

/** Ce qu'on retient d'un son. Volontairement peu de chiffres : chacun doit
    correspondre a un axe que la recette sait bouger. Mesurer ce que le
    generateur ne peut pas reproduire ne sert qu'a se faire peur. */
struct Mesure
{
    bool   ok = false;
    double dureeS = 0.0;
    double f0 = 0.0;
    bool   f0Suspecte = false;
    double partBruit = 0.0;      // 0..1
    double centroideHz = 0.0;    // brillance
    double decroissS = 0.0;      // temps pour tomber de 12 dB
    double attaqueS = 0.0;       // temps pour atteindre 90 % de la crete
    double transitoire = 0.0;    // energie des 12 premieres ms, relative
};

inline Mesure mesure (const Son& s)
{
    Mesure m;
    if (! s.ok || s.x.size() < 4096) return m;

    const int onset = trouveOnset (s.x);
    std::vector<double> x (s.x.begin() + onset, s.x.end());
    if (x.size() < 4096) return m;

    double crete = 1e-12;
    for (double v : x) crete = std::max (crete, std::abs (v));
    for (auto& v : x) v /= crete;
    m.dureeS = (double) x.size() / s.fs;

    // --- hauteur, estimee dans la tenue
    const int deb = std::min ((int) x.size() - 4096, (int) (0.10 * s.fs));
    RapportAnalyse rap;
    m.f0 = estimeF0 (x, s.fs, std::max (0, deb), &rap);
    m.f0Suspecte = rap.f0SurLaBorne || m.f0 <= 0.0;

    // --- enveloppe : attaque et decroissance
    {
        const double lis = 1.0 - std::exp (-1.0 / (0.005 * s.fs));
        double env = 0.0, pic = 0.0; int iPic = 0;
        std::vector<double> e (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            env += lis * (std::abs (x[i]) - env);
            e[i] = env;
            if (env > pic) { pic = env; iPic = (int) i; }
        }
        m.attaqueS = (double) iPic / s.fs;
        const double seuil = pic * 0.251;      // -12 dB
        m.decroissS = m.dureeS - m.attaqueS;
        for (size_t i = (size_t) iPic; i < e.size(); ++i)
            if (e[i] <= seuil) { m.decroissS = (double) ((int) i - iPic) / s.fs; break; }
    }

    // --- brillance : centroide spectral sur la tenue
    {
        const int n = 8192;
        const int d0 = std::min ((int) x.size() - n, (int) (0.05 * s.fs));
        if (d0 >= 0)
        {
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = x[(size_t) (d0 + i)] * w;
            }
            fft (b);
            double num = 0.0, den = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double a = std::abs (b[(size_t) k]);
                const double f = (double) k * s.fs / (double) n;
                num += a * f; den += a;
            }
            m.centroideHz = num / den;
        }
    }

    // --- part de bruit : ce que les 24 premieres harmoniques n'expliquent pas
    if (m.f0 > 20.0 && ! m.f0Suspecte)
    {
        const int n = 8192;
        const int d0 = std::min ((int) x.size() - n, (int) (0.05 * s.fs));
        if (d0 >= 0)
        {
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = x[(size_t) (d0 + i)] * w;
            }
            fft (b);
            std::vector<double> mag ((size_t) (n / 2 + 1));
            double tot = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            { mag[(size_t) k] = std::norm (b[(size_t) k]); tot += mag[(size_t) k]; }

            // on compte l'energie dans une bande etroite autour de chaque
            // harmonique, largeur = deux raies de part et d'autre
            double harm = 0.0;
            for (int h = 1; h <= kPartielsMax; ++h)
            {
                const double f = m.f0 * (double) h;
                if (f >= 0.45 * s.fs) break;
                const int k0 = (int) std::round (f * n / s.fs);
                for (int k = k0 - 2; k <= k0 + 2; ++k)
                    if (k >= 1 && k <= n / 2) harm += mag[(size_t) k];
            }
            m.partBruit = std::max (0.0, std::min (1.0, 1.0 - harm / tot));
        }
    }
    else
    {
        m.partBruit = 1.0;
    }

    // --- transitoire
    {
        const int nT = std::min ((int) x.size(), (int) (0.012 * s.fs));
        const int nS = std::min ((int) x.size(), (int) (0.30 * s.fs));
        double eT = 0.0, eS = 1e-30;
        for (int i = 0; i < nT; ++i) eT += x[(size_t) i] * x[(size_t) i];
        for (int i = 0; i < nS; ++i) eS += x[(size_t) i] * x[(size_t) i];
        m.transitoire = (eT / (double) std::max (nT, 1)) / (eS / (double) std::max (nS, 1));
    }

    m.ok = true;
    return m;
}


} // namespace outils
