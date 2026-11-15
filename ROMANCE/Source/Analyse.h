// =============================================================================
//  Analyse.h - D'un son vers une AMORCE de recette.
//
//  ⭐ L'analyse ne cherche pas a etre juste, elle cherche a etre UN BON POINT
//  DE DEPART. C'est la recherche qui finit le travail. Une analyse qui se croit
//  exacte fait perdre plus de temps qu'elle n'en gagne : elle enferme la
//  recherche dans un minimum local en pretendant l'y avoir menee.
//
//  /!\ UN ESTIMATEUR QUI REND SA BORNE N'ESTIME PAS, IL ECHOUE EN SILENCE.
//  Le prototype Python avait rendu 1204,3 Hz - exactement sa borne haute - sur
//  un son grave, et rien ne l'avait signale. D'ou ici : borne basse liee a la
//  resolution reelle (4 fs / n), correction d'octave explicite, affinage
//  parabolique, et un indicateur de confiance qu'on peut LIRE.
//
//  Ne depend que de <cmath>, <complex>, <algorithm> et <vector>.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "Perte.h"
#include "Recette.h"

namespace romance
{

struct RapportAnalyse
{
    double f0 = 0.0;
    double confiance = 0.0;      // 0..1 - part de l'energie expliquee par les partiels
    int    nPartiels = 0;
    double dureeS = 0.0;
    double balayageDemiTons = 0.0;
    double inharmonicite = 0.0;
    double partBruit = 0.0;      // 0..1
    bool   f0SurLaBorne = false; // /!\ le drapeau qui manquait au prototype
};

namespace detail
{
    /** Spectre d'amplitude d'une fenetre, avec zero-padding a la puissance de 2. */
    inline std::vector<double> spectre (const std::vector<double>& x, int deb,
                                        int n, int nFft)
    {
        std::vector<std::complex<double>> b ((size_t) nFft, {});
        for (int i = 0; i < n && deb + i < (int) x.size(); ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            b[(size_t) i] = x[(size_t) (deb + i)] * w;
        }
        fft (b);
        std::vector<double> m ((size_t) (nFft / 2 + 1));
        for (int k = 0; k <= nFft / 2; ++k) m[(size_t) k] = std::abs (b[(size_t) k]);
        return m;
    }

    /** Amplitude et frequence instantanee d'un partiel, par correlation. */
    inline void correle (const std::vector<double>& x, int deb, int n, double f,
                         double fs, double& amp, double& freq)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n && deb + i < (int) x.size(); ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            const double a = 2.0 * kPi * f * (double) i / fs;
            re += x[(size_t) (deb + i)] * w * std::cos (a);
            im += x[(size_t) (deb + i)] * w * std::sin (a);
        }
        amp = 2.0 * std::sqrt (re * re + im * im) / (double) n * 2.0;  // Hann : x2
        freq = f;
        (void) im;
    }
}

/** Premier echantillon utile.

    /!\ IL DOIT ETRE PARTAGE. L'analyse rognait le silence de tete alors que la
    comparaison partait de l'echantillon 0 : on comparait donc le debut du rendu
    au silence de la cible, et la perte mesurait un decalage plutot qu'un
    timbre.
*/
inline int trouveOnset (const std::vector<double>& x) noexcept
{
    double crete = 1e-12;
    for (double v : x) crete = std::max (crete, std::abs (v));
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > 0.02 * crete) return (int) i;
    return 0;
}

/** Hauteur fondamentale, par produit spectral harmonique.

    Rend 0 si l'estimation n'est pas credible - PLUTOT QUE DE RENDRE UNE BORNE.
*/
inline double estimeF0 (const std::vector<double>& x, double fs, int deb,
                        RapportAnalyse* rap = nullptr)
{
    const int n = std::min (16384, (int) x.size() - deb);
    if (n < 2048) return 0.0;
    int nFft = 1; while (nFft < n * 2) nFft <<= 1;
    const auto m = detail::spectre (x, deb, n, nFft);
    const int nb = nFft / 2;

    // /!\ La borne basse ne peut pas etre en dessous de ce que la fenetre
    // permet de VOIR : quatre raies au minimum pour parler d'une hauteur.
    const double fMin = std::max (32.0, 4.0 * fs / (double) nFft);
    const double fMax = 2500.0;
    const int kMin = std::max (1, (int) (fMin * nFft / fs));
    const int kMax = std::min (nb / 5, (int) (fMax * nFft / fs));
    if (kMax <= kMin) return 0.0;

    // produit spectral harmonique sur 5 rangs
    int meilleur = kMin; double mieux = -1.0;
    for (int k = kMin; k <= kMax; ++k)
    {
        double p = 0.0;
        for (int h = 1; h <= 5; ++h)
        {
            const int i = k * h;
            if (i > nb) break;
            p += std::log (1e-12 + m[(size_t) i]);
        }
        if (p > mieux) { mieux = p; meilleur = k; }
    }

    // ⭐ Correction d'octave EXPLICITE. Le PSH se trompe d'octave vers le haut
    // des que la fondamentale est faible - ce qui arrive tout le temps sur les
    // sons filtres.
    //
    // /!\ MAIS ON NE DESCEND PAS SUR LA SEULE PRESENCE D'ENERGIE A f/2. Ma
    // premiere version testait m[k/2] > 0,35 m[k] : sur un son riche, la fuite
    // spectrale suffit a passer ce seuil, et l'estimateur descendait DEUX
    // octaves de trop - PERCUSIVE KEY 1 rendait 36,5 Hz au lieu de 146.
    // Le bon test est structurel : si f/2 est la vraie fondamentale, alors les
    // harmoniques IMPAIRES de f/2 (soit f/2, 3f/2, 5f/2...) existent vraiment.
    // Si elles sont vides, ce qu'on voit a f/2 n'est pas une fondamentale.
    for (int essai = 0; essai < 2; ++essai)
    {
        const int demi = meilleur / 2;
        if (demi < kMin) break;
        double impair = 0.0, pair = 0.0;
        int nI = 0, nP2 = 0;
        for (int h = 1; h <= 8; ++h)
        {
            const int i = demi * h;
            if (i > nb) break;
            if (h % 2 == 1) { impair += m[(size_t) i]; ++nI; }
            else            { pair   += m[(size_t) i]; ++nP2; }
        }
        if (nI == 0 || nP2 == 0) break;
        impair /= (double) nI; pair /= (double) nP2;
        if (impair > 0.30 * pair) meilleur = demi;
        else break;
    }

    // affinage parabolique sur trois points
    double f0 = (double) meilleur * fs / (double) nFft;
    if (meilleur > 1 && meilleur < nb)
    {
        const double a = m[(size_t) (meilleur - 1)], b = m[(size_t) meilleur],
                     c = m[(size_t) (meilleur + 1)];
        const double den = a - 2.0 * b + c;
        if (std::abs (den) > 1e-18)
            f0 += (0.5 * (a - c) / den) * fs / (double) nFft;
    }

    if (rap != nullptr)
    {
        // /!\ Le drapeau qui manquait : si l'estimateur colle a sa borne, il
        // n'estime pas, il echoue. On le DIT au lieu de rendre un nombre.
        rap->f0SurLaBorne = (f0 <= fMin * 1.02) || (f0 >= fMax * 0.98);
    }
    return f0;
}

/** Fabrique une amorce de recette a partir d'un son mono. */
inline Recette amorce (const std::vector<double>& brut, double fs,
                       RapportAnalyse& rap, double f0Impose = 0.0)
{
    Recette r;
    if (brut.size() < 4096) return r;

    // --- normalisation et recherche du debut
    double crete = 1e-12;
    for (double v : brut) crete = std::max (crete, std::abs (v));
    std::vector<double> x (brut.size());
    for (size_t i = 0; i < brut.size(); ++i) x[i] = brut[i] / crete;

    const int onset = trouveOnset (x);
    if (onset > 0) x.erase (x.begin(), x.begin() + onset);
    rap.dureeS = (double) x.size() / fs;

    // --- hauteur, estimee dans la TENUE et non dans l'attaque
    const int debTenue = std::min ((int) x.size() - 4096,
                                   (int) (0.12 * fs));
    double f0 = (f0Impose > 0.0) ? f0Impose
                                 : estimeF0 (x, fs, std::max (0, debTenue), &rap);
    if (f0 <= 0.0) { rap.f0 = 0.0; return r; }
    rap.f0 = f0;
    r.f0 = f0;

    // --- enveloppes des partiels, par trames
    const int nFen = 2048, saut = 512;
    const int nTr = std::max (1, ((int) x.size() - nFen) / saut);
    const double nyq = 0.45 * fs;

    int nP = 0;
    std::vector<std::vector<double>> env;
    std::vector<double> fMes;
    for (int k = 1; k <= kPartielsMax; ++k)
    {
        const double f = f0 * (double) k;
        if (f >= nyq) break;
        std::vector<double> e ((size_t) nTr, 0.0);
        for (int t = 0; t < nTr; ++t)
        {
            double a, ff;
            detail::correle (x, t * saut, nFen, f, fs, a, ff);
            e[(size_t) t] = a;
        }
        env.push_back (e);
        fMes.push_back (f);
        ++nP;
    }
    if (nP == 0) return r;
    r.nPartiels = nP;
    rap.nPartiels = nP;

    // --- ajustement de chaque partiel
    double ampMax = 1e-12;
    for (int k = 0; k < nP; ++k)
        for (double v : env[(size_t) k]) ampMax = std::max (ampMax, v);

    for (int k = 0; k < nP; ++k)
    {
        const auto& e = env[(size_t) k];
        int iPic = 0; double pic = 0.0;
        for (int t = 0; t < nTr; ++t) if (e[(size_t) t] > pic) { pic = e[(size_t) t]; iPic = t; }

        auto& p = r.partiels[k];
        p.amplitude = pic / ampMax;

        // montee : temps pour atteindre 63 % du pic. Si le pic est des la
        // premiere trame, c'est une attaque immediate.
        double tMon = 0.0;
        for (int t = 0; t <= iPic; ++t)
            if (e[(size_t) t] >= 0.63 * pic) { tMon = (double) (t * saut) / fs; break; }
        int meilleurM = 0; double ecart = 1e9;
        for (int m = 0; m < 4; ++m)
        {
            const double d = std::abs (kMontees[m] - tMon);
            if (d < ecart) { ecart = d; meilleurM = m; }
        }
        p.montee = meilleurM;

        // decroissance : temps pour tomber a 1/e du pic, APRES le pic
        double tau = 30.0;
        for (int t = iPic; t < nTr; ++t)
            if (e[(size_t) t] <= pic * 0.3679)
            {
                tau = std::max (0.005, (double) ((t - iPic) * saut) / fs);
                break;
            }
        // tau = 30 * 0.001^d  ->  d = ln(tau/30) / ln(0.001)
        p.decroiss = std::max (0.0, std::min (1.0,
                        std::log (std::max (tau, 1e-4) / 30.0) / std::log (0.001)));
        p.desaccord = 0.0;
    }

    // --- balayage de hauteur a l'attaque (le 808)
    {
        const int n1 = std::min (4096, (int) x.size() / 4);
        const double fDebut = estimeF0 (x, fs, 0, nullptr);
        if (fDebut > 0.0 && n1 > 1024)
        {
            const double demi = 12.0 * std::log2 (std::max (fDebut, 1.0) / f0);
            if (std::abs (demi) > 0.5 && std::abs (demi) < 48.0)
            {
                r.pitchProf = demi;
                r.pitchDuree = 0.06;
                r.pitchCourbe = 0.5;
                rap.balayageDemiTons = demi;
            }
        }
    }

    // --- residu : ce que les partiels n'expliquent pas
    double eTot = 0.0;
    for (double v : x) eTot += v * v;
    double eHarm = 0.0;
    for (int k = 0; k < nP; ++k)
        for (double v : env[(size_t) k]) eHarm += 0.5 * v * v * (double) saut;
    rap.partBruit = std::max (0.0, std::min (1.0,
                        1.0 - eHarm / std::max (eTot, 1e-12)));
    rap.confiance = 1.0 - rap.partBruit;

    // deux bandes, grossierement : une grave et une aigue.
    // ⭐ Volontairement grossier - c'est la recherche qui affine. Une analyse
    // de bruit precise serait du temps perdu deux fois.
    r.bruit[0].niveau = std::min (0.8, rap.partBruit * 0.6);
    r.bruit[0].coupure = 0.35; r.bruit[0].decroiss = 0.45;
    r.bruit[1].niveau = std::min (0.5, rap.partBruit * 0.3);
    r.bruit[1].coupure = 0.90; r.bruit[1].decroiss = 0.55;

    // --- transitoire : energie des 12 premieres millisecondes
    {
        const int nT = std::min ((int) x.size(), (int) (0.012 * fs));
        double eT = 0.0; for (int i = 0; i < nT; ++i) eT += x[(size_t) i] * x[(size_t) i];
        const int nS = std::min ((int) x.size(), (int) (0.30 * fs));
        double eS = 0.0; for (int i = 0; i < nS; ++i) eS += x[(size_t) i] * x[(size_t) i];
        const double part = eT / std::max (eS, 1e-12) * (double) nS / (double) std::max (nT, 1);
        r.transNiveau = std::max (0.0, std::min (0.9, (part - 1.0) * 0.25));
        r.transDuree = 0.010;
        r.transCouleur = 0.6;
    }

    // --- duree et boucle
    r.dureeAttaque = std::max (0.05, std::min (2.04, rap.dureeS * 0.8));
    r.cyclesBoucle = std::max (2, std::min (64,
                        (int) std::round (f0 * 0.12)));   // ~120 ms de boucle
    r.graine = 1u;
    r.inharmonicite = 0.0;
    r.ratio[1] = 0.0; r.niveau[1] = 0.0;
    r.ratio[2] = 0.0; r.niveau[2] = 0.0;

    return normalise (r);
}

} // namespace romance
