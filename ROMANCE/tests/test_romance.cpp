// =============================================================================
//  test_romance.cpp - Tests DSP autonomes de la couche de grain.
//
//  Les bornes sont les valeurs MESUREES sur le prototype Python
//  (D:\1 Plugin Vibecodding\ROMANCEPROTO VST). Un ecart signale une divergence
//  entre le portage et ce qui a ete valide a l'ecoute.
//
//  /!\ LECON DE LA SESSION PYTHON, a ne pas perdre : quatre defauts ont ete
//  trouves en un jour, aucun visible sans TEMOIN. Chaque test qui mesure une
//  degradation doit donc comparer a un temoin, jamais a une intuition.
// =============================================================================
#define _USE_MATH_DEFINES
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "Grain.h"
#include "Couche.h"
#include "Moteur.h"
#include "Effets.h"
#include "Instabilite.h"
#include "Recette.h"
#include "Perte.h"
#include "Analyse.h"
#include "Recherche.h"
#include "Macros.h"
#include "Oscillateur.h"
#include "FM.h"
#include "Effets2.h"
#include "Effets3.h"
#include "Machines.h"
#include "Prior.h"
#include "Table.h"
#include "Spectre.h"
#include "Forme.h"
#include "Rack.h"
#include "Reseau.h"
#include <cstdint>

using namespace romance;

static int g_echecs = 0;
static int g_total  = 0;

static void verifie (const char* nom, double v, double lo, double hi,
                     const char* unite = "")
{
    ++g_total;
    const bool ok = (v >= lo && v <= hi);
    if (! ok) ++g_echecs;
    std::printf ("  %-52s %10.4f %-6s %s\n", nom, v, unite,
                 ok ? "OK" : "*** ECHEC ***");
    if (! ok) std::printf ("      attendu dans [%.4f, %.4f]\n", lo, hi);
}

// ---------------------------------------------------------------- utilitaires

static double rms (const std::vector<double>& v)
{
    double s = 0.0;
    for (double x : v) s += x * x;
    return std::sqrt (s / (double) std::max<size_t> (v.size(), 1));
}

/** Reponse d'un filtre de reconstruction a une frequence, en dB. */
static double reponseDb (double f, double fc, double fs, int ordre)
{
    Reconstruction r;
    r.regle (fc, fs, ordre);
    const int n = (int) (fs * 0.25);
    std::vector<double> y;
    y.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double x = std::sin (2.0 * M_PI * f * (double) i / fs);
        const double o = r.traite (x);
        if (i > n / 2) y.push_back (o);   // on jette le transitoire
    }
    return 20.0 * std::log10 (rms (y) / 0.70710678 + 1e-30);
}

// --------------------------------------------------------------------- tests

static void testMuLaw()
{
    std::printf ("\n1. QUANTIFICATION mu-law (mu-255)\n");

    // 256 codes distincts sur une rampe
    bool vus[256] = { false };
    for (int i = 0; i <= 20000; ++i)
        vus[muLawEncode (-1.0 + 2.0 * (double) i / 20000.0)] = true;
    int n = 0;
    for (bool b : vus) if (b) ++n;
    verifie ("codes distincts sur une rampe", (double) n, 250, 256);

    // le pas DOUBLE d'un segment au suivant : l'erreur est bien plus grande
    // sur les cretes que pres de zero. Mesure Python : rapport 76,2x.
    double errCrete = 0.0, errZero = 0.0;
    for (int i = 0; i < 200; ++i)
    {
        const double xc = -1.0 + 0.01 * (double) i / 200.0;   // pres de -1
        const double xz = -0.005 + 0.01 * (double) i / 200.0; // pres de 0
        errCrete = std::max (errCrete, std::abs (quantifie (xc, Quant::MuLaw) - xc));
        errZero  = std::max (errZero,  std::abs (quantifie (xz, Quant::MuLaw) - xz));
    }
    verifie ("rapport erreur crete / erreur pres de zero",
             errCrete / std::max (errZero, 1e-12), 40.0, 130.0, "x");

    // Le companding ne REDUIT pas le bruit, il le DEPLACE.
    // Mesure Python : ecart fort/faible 35,8 dB en mu-law, 1,1 dB en 8 bits lin.
    auto bruitSurSalve = [] (Quant q, double amp)
    {
        std::vector<double> err;
        for (int i = 0; i < 20000; ++i)
        {
            const double x = amp * std::sin (2.0 * M_PI * 220.0 * (double) i / kFsRomEII);
            err.push_back (quantifie (x, q) - x);
        }
        return 20.0 * std::log10 (rms (err) + 1e-30);
    };
    const double muFort   = bruitSurSalve (Quant::MuLaw, 0.9);
    const double muFaible = bruitSurSalve (Quant::MuLaw, 0.01);
    const double l8Fort   = bruitSurSalve (Quant::Lin8, 0.9);
    const double l8Faible = bruitSurSalve (Quant::Lin8, 0.01);
    verifie ("mu-law : bruit sur signal fort", muFort, -46.0, -38.0, "dB");
    verifie ("mu-law : bruit sur signal faible", muFaible, -84.0, -72.0, "dB");
    verifie ("mu-law : ECART fort/faible (le bruit suit le signal)",
             muFort - muFaible, 30.0, 42.0, "dB");
    verifie ("8 bits lineaire : ECART fort/faible (doit etre ~0)",
             l8Fort - l8Faible, -3.0, 3.0, "dB");
    // et le fait contre-intuitif : a bits egaux, mu-law est PIRE sur les cretes
    verifie ("mu-law PIRE que 8 bits lin. sur les cretes",
             muFort - l8Fort, 3.0, 20.0, "dB");
}

static void testPentes()
{
    std::printf ("\n2. RECONSTRUCTION : la PENTE distingue les machines\n");
    // /!\ MON PREMIER TEST ETAIT FAUX. Je mesurais la pente entre 4x et 8x une
    // coupure a 5 kHz, soit 20 et 40 kHz pour fs = 176,4 kHz. A ces frequences
    // la transformee bilineaire COMPRIME l'axe : l'octave 20->40 kHz vaut 2,33
    // octaves analogiques, et un 4 poles y mesure 29,3 dB au lieu de 24.
    // Le filtre etait juste, le test ne l'etait pas.
    // Coupure abaissee a 1 kHz : le facteur de deformation tombe a 1,007.
    const double fs = 176400.0, fc = 1000.0;

    // 4 poles = 24 dB/oct (SSM2045, Emulator II)
    const double a1 = reponseDb (fc * 4.0, fc, fs, 4);
    const double a2 = reponseDb (fc * 8.0, fc, fs, 4);
    verifie ("4 poles : pente par octave", a1 - a2, 21.0, 27.0, "dB");
    verifie ("4 poles : gain a la coupure (-3 dB)", reponseDb (fc, fc, fs, 4),
             -4.5, -1.5, "dB");

    // 3 poles = 18 dB/oct (Akai S1000)
    const double b1 = reponseDb (fc * 4.0, fc, fs, 3);
    const double b2 = reponseDb (fc * 8.0, fc, fs, 3);
    verifie ("3 poles : pente par octave", b1 - b2, 15.0, 21.0, "dB");
    verifie ("3 poles : gain a la coupure (-3 dB)", reponseDb (fc, fc, fs, 3),
             -4.5, -1.5, "dB");

    // et ils DIFFERENT : c'est tout l'interet
    verifie ("ecart de pente entre les deux topologies",
             (a1 - a2) - (b1 - b2), 3.0, 9.0, "dB");
}

static void testAsservissement()
{
    std::printf ("\n3. FILTRE ASSERVI : la trouvaille du projet\n");
    std::printf ("   (mesure Python : rapport de coupure 7,70x asservi"
                 " contre 3,24x fixe, idéal 8,00x)\n");

    ConfigGrain cfg;
    cfg.horloge = Horloge::Variable;

    auto coupure = [&cfg] (Filtre f, double demiTons)
    {
        cfg.filtre = f;
        EtageReconstruction et; et.prepare (44100.0);
        et.majHorloge (cfg, std::pow (2.0, demiTons / 12.0));
        return et.coupureCourante();
    };

    const double asBas  = coupure (Filtre::Asservi, -24.0);
    const double asHaut = coupure (Filtre::Asservi, +12.0);
    const double fiBas  = coupure (Filtre::Fixe,    -24.0);
    const double fiHaut = coupure (Filtre::Fixe,    +12.0);

    // La coupure demandee suit EXACTEMENT le ratio en asservi (8,00x sur 3
    // octaves) et ne bouge PAS du tout en fixe. La mesure Python (7,70 / 3,24)
    // portait sur la chaine complete, escalier et decimation compris.
    verifie ("asservi : rapport de coupure sur 3 octaves", asHaut / asBas,
             7.9, 8.1, "x");
    verifie ("fixe : rapport de coupure (doit etre 1)", fiHaut / fiBas,
             0.99, 1.01, "x");
    // /!\ Ne PAS ecrire 0,40 en dur : la valeur par defaut a change et le test
    // aurait echoue pour la seule raison qu'il repetait une constante.
    verifie ("asservi a 0 demi-ton = coupure x fsRom",
             coupure (Filtre::Asservi, 0.0), kCutoffRatio * kFsRomEII - 1.0,
             kCutoffRatio * kFsRomEII + 1.0, "Hz");
}

static void testProprete()
{
    std::printf ("\n4. `PROPRE` est une position de plein droit\n");

    // ROM : un sinus a 1 kHz stocke au taux de la ROM
    const int nRom = 8000;
    std::vector<double> rom ((size_t) nRom);
    for (int i = 0; i < nRom; ++i)
        rom[(size_t) i] = 0.5 * std::sin (2.0 * M_PI * 1000.0 * (double) i / kFsRomEII);

    auto joue = [&rom, nRom] (ConfigGrain cfg)
    {
        EtageReconstruction et; et.prepare (44100.0);
        Lecteur l; l.prepare (et.fsInterne());
        l.majHorloge (cfg, 1.0); l.reglePhase (0.0);
        et.majHorloge (cfg, 1.0);
        std::vector<double> y;
        for (int i = 0; i < 4000; ++i)
        {
            double o = 0.0;
            for (int k = 0; k < kSurEch; ++k)
            {
                const double e = l.pasInterne (rom.data(), nRom, cfg);
                o = cfg.actif ? et.decime (0, et.reconstruit (0, e)) : e;
            }
            y.push_back (o);
        }
        return y;
    };

    ConfigGrain propre;  propre.actif = false;
    ConfigGrain grain;   grain.actif = true; grain.quant = Quant::MuLaw;

    const double rp = rms (joue (propre));
    const double rg = rms (joue (grain));
    verifie ("PROPRE : niveau conserve", rp, 0.25, 0.40);
    verifie ("GRAIN : niveau du meme ordre", rg, 0.15, 0.40);
    // Les deux doivent sonner DIFFEREMMENT, sinon la couche ne sert a rien.
    std::vector<double> yp = joue (propre), yg = joue (grain);
    double d = 0.0;
    for (size_t i = 0; i < yp.size(); ++i) d += (yp[i] - yg[i]) * (yp[i] - yg[i]);
    d = std::sqrt (d / (double) yp.size());
    verifie ("ecart PROPRE / GRAIN (doit etre non nul)", d, 0.005, 1.0);
}

// ------------------------------------------------- 5. couches, voix, allocation

/** ROM de test : un sinus a 261,6 Hz (do3), la hauteur de reference. */
static std::vector<double> romTest (int n = 6000)
{
    std::vector<double> r ((size_t) n);
    for (int i = 0; i < n; ++i)
        r[(size_t) i] = 0.6 * std::sin (2.0 * M_PI * 261.6255653 * (double) i / kFsRomEII);
    return r;
}

static void testCouches()
{
    std::printf ("\n5. COUCHES, VOIX ET ALLOCATION\n");

    auto rom = romTest();
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tailles[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                                (int) rom.size(), (int) rom.size() };

    verifie ("nombre de couches (3 machines disent 4)", (double) kNbCouches, 4, 4);

    // --- les ZONES filtrent bien
    ReglagesPatch p;
    for (auto& c : p.couche) { c.active = false; }
    p.couche[0].active = true;
    p.couche[0].noteMin = 60; p.couche[0].noteMax = 72;

    Moteur m; m.prepare (44100.0, 16);
    m.noteOn (p, 64, 100, roms, tailles);          // dans la zone
    double e1 = 0.0;
    for (int i = 0; i < 4000; ++i) { double yg,yd; m.suivant (p,yg,yd); e1 += yg*yg; }
    m.toutCouper();
    m.noteOn (p, 30, 100, roms, tailles);          // HORS zone
    double e2 = 0.0;
    for (int i = 0; i < 4000; ++i) { double yg,yd; m.suivant (p,yg,yd); e2 += yg*yg; }
    verifie ("zone : note dans la plage produit du son", std::sqrt (e1 / 4000.0), 0.01, 1.0);
    verifie ("zone : note hors plage produit le SILENCE", std::sqrt (e2 / 4000.0), 0.0, 1e-9);

    // --- l'unisson desaccorde produit du BATTEMENT (mesure : plus de desaccord,
    //     plus l'enveloppe du signal fluctue)
    auto fluctuation = [&] (double cents)
    {
        ReglagesPatch q;
        for (auto& c : q.couche) c.active = false;
        q.couche[0].active = true;
        q.couche[0].desaccordCents = cents;
        q.couche[0].tenue = 1.0; q.couche[0].attaque = 0.001; q.couche[0].decli = 0.001;
        q.couche[0].grain.actif = false;             // on isole l'unisson
        Moteur mm; mm.prepare (44100.0, 4);
        mm.noteOn (q, 60, 100, roms, tailles);
        std::vector<double> env;
        double pic = 0.0;
        for (int i = 0; i < 40000; ++i)
        {
            double yg,yd; mm.suivant (q,yg,yd); const double y = std::abs (yg+yd);
            pic = std::max (pic * 0.9995, y);
            if (i > 5000) env.push_back (pic);
        }
        double moy = 0.0; for (double v : env) moy += v; moy /= (double) env.size();
        double var = 0.0; for (double v : env) var += (v - moy) * (v - moy);
        return std::sqrt (var / (double) env.size()) / std::max (moy, 1e-12);
    };
    const double f0c = fluctuation (0.2);
    const double f30 = fluctuation (30.0);
    verifie ("unisson quasi nul : peu de fluctuation", f0c, 0.0, 0.15);
    verifie ("unisson 30 cents : fluctuation superieure", f30 - f0c, 0.01, 2.0);

    // --- VOL DE VOIX : il fait partie du son d'epoque
    ReglagesPatch v; for (auto& c : v.couche) c.active = false;
    v.couche[0].active = true; v.couche[0].tenue = 1.0;
    v.polyphonie = 4;
    Moteur mv; mv.prepare (44100.0, 16);
    for (int n = 60; n < 68; ++n) { mv.noteOn (v, n, 100, roms, tailles); double a,b; mv.suivant (v,a,b); }
    verifie ("polyphonie 4 : jamais plus de 4 voix actives",
             (double) mv.nbActives(), 1, 4);
    verifie ("vol de voix effectivement declenche",
             (double) mv.volsEffectues(), 1, 100);

    // --- MONO : une seule voix quoi qu'il arrive
    ReglagesPatch mo; for (auto& c : mo.couche) c.active = false;
    mo.couche[0].active = true; mo.couche[0].tenue = 1.0;
    mo.mode = ModeVoix::Mono;
    Moteur mm2; mm2.prepare (44100.0, 16);
    for (int n = 60; n < 66; ++n) { mm2.noteOn (mo, n, 100, roms, tailles); double a,b; mm2.suivant (mo,a,b); }
    verifie ("mono : une seule voix", (double) mm2.nbActives(), 1, 1);

    // --- l'enveloppe s'ouvre puis se ferme
    ReglagesPatch e; for (auto& c : e.couche) c.active = false;
    e.couche[0].active = true;
    e.couche[0].attaque = 0.05; e.couche[0].decli = 0.05;
    e.couche[0].tenue = 0.5; e.couche[0].relache = 0.05;
    Moteur me; me.prepare (44100.0, 4);
    me.noteOn (e, 60, 100, roms, tailles);
    double picDebut = 0.0, picTenue = 0.0;
    for (int i = 0; i < 400; ++i)  { double a,b; me.suivant (e,a,b); picDebut = std::max (picDebut, std::abs (a+b)); }
    for (int i = 0; i < 8000; ++i) { double a,b; me.suivant (e,a,b); picTenue = std::max (picTenue, std::abs (a+b)); }
    me.noteOff (e, 60);
    double picFin = 0.0;
    for (int i = 0; i < 20000; ++i) { double a,b; me.suivant (e,a,b); picFin = std::max (picFin, std::abs (a+b)); }
    verifie ("enveloppe : attaque progressive (debut < tenue)",
             picTenue - picDebut, 0.001, 1.0);
    verifie ("enveloppe : relachement (fin < tenue)", picTenue - picFin, 0.0, 1.0);
    verifie ("voix liberee apres le relachement", (double) me.nbActives(), 0, 0);
}


// --------------------------------------------- 6. stereo et boucle

static void testStereoEtBoucle()
{
    std::printf ("\n6. STEREO ET BOUCLE\n");

    auto rom = romTest();
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tailles[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                                (int) rom.size(), (int) rom.size() };

    auto joue = [&] (double largeur, double pan, double desac, int nEch,
                     double& rmsG, double& rmsD, double& rmsFin)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].largeur = largeur;
        p.couche[0].pan = pan;
        p.couche[0].desaccordCents = desac;
        p.couche[0].tenue = 1.0;
        p.couche[0].attaque = 0.001;
        p.couche[0].decli = 0.001;
        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 60, 100, roms, tailles);
        double sg = 0.0, sd = 0.0, sf = 0.0;
        int nf = 0;
        for (int i = 0; i < nEch; ++i)
        {
            double a = 0.0, b = 0.0;
            m.suivant (p, a, b);
            sg += a * a; sd += b * b;
            if (i > nEch * 3 / 4) { sf += (a + b) * (a + b); ++nf; }
        }
        rmsG = std::sqrt (sg / (double) nEch);
        rmsD = std::sqrt (sd / (double) nEch);
        rmsFin = std::sqrt (sf / (double) std::max (nf, 1));
    };

    double g, d, f;

    // largeur nulle et pan centre -> les deux canaux sont IDENTIQUES
    joue (0.0, 0.0, 10.0, 20000, g, d, f);
    verifie ("largeur 0 : canaux identiques", std::abs (g - d), 0.0, 1e-9);

    // pan a gauche -> la gauche domine
    joue (0.0, -0.8, 10.0, 20000, g, d, f);
    verifie ("pan a gauche : G nettement > D", g / std::max (d, 1e-12), 2.0, 100.0, "x");

    // largeur maximale avec desaccord -> les canaux DIFFERENT
    joue (1.0, 0.0, 25.0, 20000, g, d, f);
    verifie ("largeur 1 : canaux differents", std::abs (g - d) / std::max (g, 1e-12),
             0.002, 2.0);

    // BOUCLE : la ROM fait 6000 echantillons a 27,7 kHz, soit 0,217 s.
    // Sans boucle, une note tenue serait muette bien avant la fin des 2 s.
    joue (0.0, 0.0, 5.0, 88200, g, d, f);
    verifie ("boucle : encore du son apres 10x la longueur de la ROM", f, 0.01, 1.0);
}


// ------------------------------------- 7. le vol de voix ne doit pas claquer

static void testVolSansClic()
{
    std::printf ("\n7. LE VOL DE VOIX NE DOIT PAS CLAQUER\n");

    auto rom = romTest();
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tailles[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                                (int) rom.size(), (int) rom.size() };

    ReglagesPatch p;
    for (auto& c : p.couche) c.active = false;
    p.couche[0].active = true;
    p.couche[0].tenue = 1.0;
    p.couche[0].attaque = 0.002;
    p.couche[0].decli = 0.002;
    p.couche[0].grain.actif = false;   // on isole l'allocation
    p.polyphonie = 4;

    Moteur m; m.prepare (44100.0, kVoixMax);

    double sautMax = 0.0, crete = 1e-12, prec = 0.0;
    // douze notes sur une polyphonie de 4 : huit vols garantis
    for (int n = 0; n < 12; ++n)
    {
        m.noteOn (p, 55 + n, 100, roms, tailles);
        for (int i = 0; i < 4000; ++i)
        {
            double a = 0.0, b = 0.0;
            m.suivant (p, a, b);
            const double v = a + b;
            crete = std::max (crete, std::abs (v));
            if (n > 0 || i > 0) sautMax = std::max (sautMax, std::abs (v - prec));
            prec = v;
        }
    }

    // Un saut pleine echelle serait de l'ordre de la crete. Une extinction en
    // 6 ms le ramene au niveau d'un pas normal de la forme d'onde.
    const double rel = sautMax / crete;
    verifie ("saut maximal / crete (etait ~1 en coupure nette)", rel, 0.0, 0.20);
    verifie ("le son est bien present", crete, 0.01, 4.0);
    verifie ("des vols ont bien eu lieu", (double) m.volsEffectues(), 1, 100);
    verifie ("pas plus de 4 voix TENUES", (double) m.nbActives(), 1, 4);
    // les voix en extinction occupent des cases en plus, c'est voulu
    verifie ("des voix s'eteignent en parallele", (double) m.nbTotal(), 4, 32);
}

// ------------------------------------------------ 8. transposition par couche

static void testTransposition()
{
    std::printf ("\n8. TRANSPOSITION PAR COUCHE\n");

    auto rom = romTest();
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tailles[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                                (int) rom.size(), (int) rom.size() };

    auto hauteurMesuree = [&] (int transpose, double accordFin)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].tenue = 1.0;
        p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
        p.couche[0].grain.actif = false;
        p.couche[0].transpose = transpose;
        p.couche[0].accordFin = accordFin;
        p.couche[0].desaccordCents = 0.0;
        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 69, 100, roms, tailles);   // la4 = 440 Hz
        // comptage de passages par zero sur une fenetre stable
        int passages = 0; double prec = 0.0; const int n = 22050;
        for (int i = 0; i < n + 4410; ++i)
        {
            double a = 0.0, b = 0.0;
            m.suivant (p, a, b);
            const double v = a + b;
            if (i >= 4410 && prec <= 0.0 && v > 0.0) ++passages;
            prec = v;
        }
        return (double) passages * 44100.0 / (double) n;
    };

    const double h0  = hauteurMesuree (0, 0.0);
    const double hp12 = hauteurMesuree (12, 0.0);
    const double hm12 = hauteurMesuree (-12, 0.0);

    verifie ("+12 demi-tons : hauteur doublee", hp12 / h0, 1.9, 2.1, "x");
    verifie ("-12 demi-tons : hauteur divisee par deux", h0 / hm12, 1.9, 2.1, "x");
    const double hf = hauteurMesuree (0, 100.0);
    verifie ("accord fin +100 cents = un demi-ton", hf / h0, 1.03, 1.09, "x");
}


// ------------------- 9. combien la quantification s'entend-elle REELLEMENT ?

static void testQuantAudible()
{
    std::printf ("\n9. LA QUANTIFICATION S'ENTEND-ELLE A LA SORTIE ?\n");
    std::printf ("   (bruit ajoute par rapport au signal, en dB - plus c'est\n");
    std::printf ("    haut, plus ca s'entend. Mesure a la sortie de la COUCHE,\n");
    std::printf ("    filtre de reconstruction compris.)\n\n");

    auto brute = romTest (12000);

    auto joue = [&brute] (Quant q, double coupure, int demiTons)
    {
        std::vector<double> rq (brute.size());
        for (size_t i = 0; i < brute.size(); ++i) rq[i] = quantifie (brute[i], q);

        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].tenue = 1.0; p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.quant = q;
        p.couche[0].grain.coupure = coupure;
        p.couche[0].transpose = demiTons;
        p.couche[0].coupureRel = 40.0;             // filtre musical grand ouvert

        const double* roms[kNbCouches] = { rq.data(), rq.data(), rq.data(), rq.data() };
        int t[kNbCouches] = { (int) rq.size(), (int) rq.size(),
                              (int) rq.size(), (int) rq.size() };
        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 60, 100, roms, t);
        std::vector<double> y;
        for (int i = 0; i < 30000; ++i)
        {
            double a = 0.0, b = 0.0; m.suivant (p, a, b);
            if (i > 4000) y.push_back (a);
        }
        return y;
    };

    auto ecart = [] (const std::vector<double>& a, const std::vector<double>& b)
    {
        const size_t n = std::min (a.size(), b.size());
        double ea = 0.0, ed = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            ea += a[i] * a[i];
            ed += (a[i] - b[i]) * (a[i] - b[i]);
        }
        return 10.0 * std::log10 ((ed + 1e-30) / (ea + 1e-30));
    };

    std::printf ("   %-24s %10s %10s %10s\n", "condition", "mu-law", "8 bits", "12 bits");
    for (double c : { 0.40, 0.49 })
    {
        const auto ref = joue (Quant::Aucune, c, 0);
        char lig[64];
        std::snprintf (lig, sizeof (lig), "coupure %.2f x fsRom", c);
        std::printf ("   %-24s %9.1f %10.1f %10.1f\n", lig,
                     ecart (ref, joue (Quant::MuLaw, c, 0)),
                     ecart (ref, joue (Quant::Lin8,  c, 0)),
                     ecart (ref, joue (Quant::Lin12, c, 0)));
    }
    for (int d : { -12, 12, 24 })
    {
        const auto ref = joue (Quant::Aucune, 0.40, d);
        char lig[64];
        std::snprintf (lig, sizeof (lig), "transpose %+d demi-tons", d);
        std::printf ("   %-24s %9.1f %10.1f %10.1f\n", lig,
                     ecart (ref, joue (Quant::MuLaw, 0.40, d)),
                     ecart (ref, joue (Quant::Lin8,  0.40, d)),
                     ecart (ref, joue (Quant::Lin12, 0.40, d)));
    }

    // Le controle qui compte : la quantification doit CHANGER quelque chose.
    const auto ref = joue (Quant::Aucune, 0.40, 0);
    verifie ("mu-law modifie bien le signal", ecart (ref, joue (Quant::MuLaw, 0.40, 0)),
             -60.0, -10.0, "dB");
    // /!\ MON ASSERTION ETAIT FAUSSE, et notre propre mesure T2 la contredisait
    // deja : sur un signal FORT, mu-law est PLUS bruyant que du 8 bits lineaire
    // (T2 : -42,4 contre -53,1 dB). Le companding ne reduit pas le bruit, il le
    // DEPLACE la ou le signal le masque - donc il est moins bon sur les cretes
    // et bien meilleur dans les creux. J'avais ecrit l'intuition naive
    // « moins de bits utiles = plus de bruit » alors que la mesure disait
    // l'inverse depuis le debut.
    verifie ("mu-law est PLUS bruyant que 8 bits lin. sur un signal fort",
             ecart (ref, joue (Quant::MuLaw, 0.40, 0))
             - ecart (ref, joue (Quant::Lin8, 0.40, 0)), 3.0, 20.0, "dB");
    verifie ("16 bits est quasi transparent", ecart (ref, joue (Quant::Lin16, 0.40, 0)),
             -140.0, -60.0, "dB");
}


// -------- 10. le filtre asservi change-t-il vraiment LE SON ? (pas le chiffre)

/** ROM riche : une dent de scie bande-limitee, pour avoir du contenu en haut. */
static std::vector<double> romScie (int cycles = 60)
{
    const double f0 = 261.6255653;
    const double parCycle = kFsRomEII / f0;
    const int n = (int) (cycles * parCycle);
    const int nHarm = (int) (0.45 * kFsRomEII / f0);
    std::vector<double> r ((size_t) n);
    double c = 1e-12;
    for (int i = 0; i < n; ++i)
    {
        const double ph = (double) i / parCycle;
        double v = 0.0;
        for (int k = 1; k <= nHarm; ++k)
            v += std::sin (2.0 * kPi * (double) k * ph) / (double) k;
        r[(size_t) i] = v;
        c = std::max (c, std::abs (v));
    }
    for (auto& v : r) v *= 0.9 / c;
    return r;
}

/** Proportion d'energie AU-DESSUS de fc, en dB. */
static double partHaut (const std::vector<double>& y, double fc, double fs)
{
    Reconstruction lp; lp.regle (fc, fs, 4);
    double eTot = 0.0, eHaut = 0.0;
    for (double x : y)
    {
        const double bas = lp.traite (x);
        eTot  += x * x;
        eHaut += (x - bas) * (x - bas);
    }
    return 10.0 * std::log10 ((eHaut + 1e-30) / (eTot + 1e-30));
}

static double gCoupure = 0.40;

static void testAsserviSurLeSon()
{
    std::printf ("\n10. LE FILTRE ASSERVI CHANGE-T-IL LE SON ?\n");
    std::printf ("    (part d'energie au-dessus de 6 kHz, en dB - mesuree sur\n");
    std::printf ("     l'AUDIO, pas sur la coupure calculee)\n\n");

    auto rom = romScie();

    auto joue = [&rom] (Filtre f, int demiTons)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].tenue = 1.0; p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].transpose = demiTons;
        p.couche[0].coupureRel = 40.0;          // filtre musical grand ouvert
        p.couche[0].grain.actif = true;
        p.couche[0].grain.horloge = Horloge::Variable;
        p.couche[0].grain.filtre = f;
        p.couche[0].grain.quant = Quant::Aucune; // on isole le filtre
        p.couche[0].grain.coupure = gCoupure;

        const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
        int t[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                              (int) rom.size(), (int) rom.size() };
        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 60, 100, roms, t);
        std::vector<double> y;
        for (int i = 0; i < 30000; ++i)
        {
            double a = 0.0, b = 0.0; m.suivant (p, a, b);
            if (i > 4000) y.push_back (a);
        }
        return y;
    };

    // /!\ BALAYAGE DE LA COUPURE - c'est LE parametre suppose du projet
    // (kCutoffRatio). L'utilisateur a trouve a l'oreille que le mecanisme ne
    // s'entend qu'a la coupure la plus basse et sur la note la plus haute :
    // on cherche ici a partir d'ou il devient reellement audible.
    std::printf ("    ecart asservi - fixe (dB), selon la coupure et la note\n");
    std::printf ("    %-12s %9s %9s %9s %9s\n", "x fsRom", "-24", "-12", "+12", "+24");
    for (double c : { 0.40, 0.30, 0.22, 0.15, 0.10 })
    {
        gCoupure = c;
        double e[4]; int j = 0;
        for (int d : { -24, -12, 12, 24 })
        {
            e[j++] = partHaut (joue (Filtre::Asservi, d), 6000.0, 44100.0)
                   - partHaut (joue (Filtre::Fixe,    d), 6000.0, 44100.0);
        }
        std::printf ("    %-12.2f %9.1f %9.1f %9.1f %9.1f\n", c, e[0], e[1], e[2], e[3]);
    }
    gCoupure = kCutoffRatio;

    // A 0 demi-ton les deux modes sont IDENTIQUES par construction (l'horloge
    // vaut fsRom). L'ecart doit apparaitre des qu'on transpose - c'est tout
    // l'interet du mecanisme, et c'est ce que ce test verifie vraiment.
    std::printf ("\n    (mesures ci-dessous a coupure %.2f)\n", gCoupure);
    const double a0 = partHaut (joue (Filtre::Asservi, 0), 6000.0, 44100.0);
    const double f0 = partHaut (joue (Filtre::Fixe,    0), 6000.0, 44100.0);
    verifie ("a 0 demi-ton : identiques", std::abs (a0 - f0), 0.0, 0.1, "dB");

    const double a12 = partHaut (joue (Filtre::Asservi, 12), 6000.0, 44100.0);
    const double f12 = partHaut (joue (Filtre::Fixe,    12), 6000.0, 44100.0);
    verifie ("a +12 : l'asservi laisse PLUS d'aigu", a12 - f12, 0.4, 40.0, "dB");

    const double am = partHaut (joue (Filtre::Asservi, -24), 6000.0, 44100.0);
    const double fm = partHaut (joue (Filtre::Fixe,    -24), 6000.0, 44100.0);
    verifie ("a -24 : l'asservi laisse MOINS d'aigu", fm - am, 0.4, 40.0, "dB");
}


// ------------- 11. la transposition doit agir sur une note DEJA en train de sonner

static void testHauteurVive()
{
    std::printf ("\n11. TRANSPOSER PENDANT QU'UNE NOTE SONNE\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tailles[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                                (int) rom.size(), (int) rom.size() };

    ReglagesPatch p;
    for (auto& c : p.couche) c.active = false;
    p.couche[0].active = true;
    p.couche[0].tenue = 1.0; p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
    p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
    p.couche[0].grain.actif = false;

    Moteur m; m.prepare (44100.0, 4);
    m.noteOn (p, 69, 100, roms, tailles);          // la4

    auto mesureHauteur = [&m, &p] (int n)
    {
        int passages = 0; double prec = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double a = 0.0, b = 0.0;
            m.suivant (p, a, b);
            const double v = a + b;
            if (i > 2000 && prec <= 0.0 && v > 0.0) ++passages;
            prec = v;
        }
        return (double) passages * 44100.0 / (double) (n - 2000);
    };

    const double avant = mesureHauteur (24000);

    // on transpose PENDANT que la note tient, comme un geste de souris
    p.couche[0].transpose = 12;
    m.majHauteurs (p);
    const double apres = mesureHauteur (24000);

    verifie ("la note suit la transposition (+12 = x2)", apres / avant, 1.85, 2.15, "x");

    // et elle continue de sonner : ni coupure, ni extinction
    verifie ("la note n'est pas coupee", (double) m.nbActives(), 1, 1);

    // retour en arriere
    p.couche[0].transpose = 0;
    m.majHauteurs (p);
    const double retour = mesureHauteur (24000);
    verifie ("elle redescend aussi", retour / avant, 0.88, 1.12, "x");
}


// ---------- 12. les quatre topologies doivent SONNER differemment

static void testTopologies()
{
    std::printf ("\n12. LES QUATRE TOPOLOGIES DE FILTRE\n");
    std::printf ("    (deux d'entre elles etaient NOMMEES mais ne faisaient rien :\n");
    std::printf ("     elles sonnaient identiques au 4 poles. Un instrument qui\n");
    std::printf ("     ment est pire qu'un instrument incomplet.)\n\n");

    auto rom = romScie (40);

    auto joue = [&rom] (Topologie t, double reso, double morph)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].tenue = 1.0; p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.actif = false;     // on isole le filtre musical
        p.couche[0].topo = t;
        p.couche[0].coupureRel = 4.0;
        p.couche[0].reso = reso;
        p.couche[0].morph = morph;

        const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
        int tl[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                               (int) rom.size(), (int) rom.size() };
        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 60, 100, roms, tl);
        std::vector<double> y;
        for (int i = 0; i < 30000; ++i)
        {
            double a = 0.0, b = 0.0; m.suivant (p, a, b);
            if (i > 6000) y.push_back (a);
        }
        return y;
    };

    auto distance = [] (const std::vector<double>& a, const std::vector<double>& b)
    {
        const size_t n = std::min (a.size(), b.size());
        double ea = 0.0, ed = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            ea += a[i] * a[i];
            ed += (a[i] - b[i]) * (a[i] - b[i]);
        }
        return 10.0 * std::log10 ((ed + 1e-30) / (ea + 1e-30));
    };

    const double reso = 0.75;
    const auto q4 = joue (Topologie::QuatrePoles,        reso, 0.5);
    const auto q3 = joue (Topologie::TroisPoles,         reso, 0.5);
    const auto an = joue (Topologie::AnalogiqueBasseReso, reso, 0.5);
    const auto zp = joue (Topologie::ZPlane,             reso, 0.5);

    std::printf ("    distance au 4 poles (dB, 0 = identique) :\n");
    std::printf ("      3 poles      %6.1f\n", distance (q4, q3));
    std::printf ("      Analogique   %6.1f\n", distance (q4, an));
    std::printf ("      Z-Plane      %6.1f\n", distance (q4, zp));

    verifie ("3 poles differe du 4 poles", distance (q4, q3), -25.0, 10.0, "dB");
    verifie ("Analogique differe du 4 poles", distance (q4, an), -25.0, 10.0, "dB");
    verifie ("Z-Plane differe du 4 poles", distance (q4, zp), -25.0, 10.0, "dB");
    verifie ("Analogique differe du 3 poles", distance (q3, an), -25.0, 10.0, "dB");

    // Le morph du Z-Plane doit agir - sinon c'est un knob decoratif.
    const auto z0 = joue (Topologie::ZPlane, reso, 0.0);
    const auto z1 = joue (Topologie::ZPlane, reso, 1.0);
    std::printf ("      morph 0 vs 1 %6.1f\n", distance (z0, z1));
    verifie ("le morph du Z-Plane agit", distance (z0, z1), -25.0, 10.0, "dB");

    // ⭐ L'analogique doit AMAIGRIR LE GRAVE quand la resonance monte - c'est ce
    // qu'on entend d'un filtre Curtis, pas sa pente.
    auto grave = [] (const std::vector<double>& y)
    {
        Reconstruction lp; lp.regle (200.0, 44100.0, 4);
        double e = 0.0;
        for (double x : y) { const double b = lp.traite (x); e += b * b; }
        return 10.0 * std::log10 (e / (double) y.size() + 1e-30);
    };
    const double an0 = grave (joue (Topologie::AnalogiqueBasseReso, 0.0, 0.5));
    const double an9 = grave (joue (Topologie::AnalogiqueBasseReso, 0.9, 0.5));
    const double q40 = grave (joue (Topologie::QuatrePoles, 0.0, 0.5));
    const double q49 = grave (joue (Topologie::QuatrePoles, 0.9, 0.5));
    std::printf ("\n    perte de grave quand la resonance monte (dB) :\n");
    std::printf ("      Analogique   %6.1f\n", an9 - an0);
    std::printf ("      4 poles      %6.1f\n", q49 - q40);
    verifie ("l'analogique amaigrit le grave PLUS que le 4 poles",
             (q49 - q40) - (an9 - an0), 0.3, 30.0, "dB");
}


// ------------------------------- 13. la modulation agit-elle vraiment ?

static void testModulation()
{
    std::printf ("\n13. MODULATION - huit emplacements et une matrice\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tl[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                           (int) rom.size(), (int) rom.size() };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].tenue = 1.0; p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.actif = false;
        return p;
    };

    // --- un LFO cable sur la hauteur doit faire VARIER la hauteur
    auto etendueHauteur = [&] (double dose)
    {
        ReglagesPatch p = base();
        p.mods[0].type = TypeMod::LFO;
        p.mods[0].p1 = 4.0;          // 4 Hz, quelques cycles dans la fenetre
        p.mods[0].p2 = 0.0;          // sinus
        p.routages[0] = { 1, (int) Dest::Hauteur, 0, dose };

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 69, 100, roms, tl);
        // on compte les passages par zero sur des tranches successives : si la
        // hauteur bouge, le compte par tranche bouge aussi.
        double mini = 1e9, maxi = 0.0, prec = 0.0;
        for (int t = 0; t < 8; ++t)
        {
            int pz = 0;
            for (int i = 0; i < 5500; ++i)
            {
                double a = 0.0, b = 0.0; m.suivant (p, a, b);
                const double v = a + b;
                if (prec <= 0.0 && v > 0.0) ++pz;
                prec = v;
            }
            if (t > 0) { mini = std::min (mini, (double) pz); maxi = std::max (maxi, (double) pz); }
        }
        return (maxi - mini) / std::max (mini, 1.0);
    };

    const double sansMod = etendueHauteur (0.0);
    const double avecMod = etendueHauteur (0.05);
    std::printf ("    variation de hauteur sans modulation : %.3f\n", sansMod);
    std::printf ("    variation de hauteur avec LFO        : %.3f\n", avecMod);
    verifie ("sans modulation, la hauteur est stable", sansMod, 0.0, 0.03);
    verifie ("le LFO fait varier la hauteur", avecMod - sansMod, 0.02, 2.0);

    // --- une enveloppe cablee sur le niveau doit faire varier le niveau
    ReglagesPatch p = base();
    p.mods[1].type = TypeMod::Enveloppe;
    p.mods[1].p1 = 0.30; p.mods[1].p2 = 0.30; p.mods[1].p3 = 0.0;   // A, D, S=0
    p.routages[0] = { 2, (int) Dest::Niveau, 1, -0.9 };
    Moteur m; m.prepare (44100.0, 4);
    m.noteOn (p, 60, 100, roms, tl);
    // /!\ Mon premier test mesurait deux fenetres qui attrapaient TOUTES DEUX le
    // maximum : l'ecart n'etait que de 0,034 et ne prouvait presque rien. On
    // mesure maintenant AU SOMMET de l'enveloppe (la ou elle retire du niveau)
    // contre APRES (tenue a zero, donc l'apport a disparu et le niveau revient).
    auto pic = [&] (int saut, int n)
    {
        double q = 0.0;
        for (int i = 0; i < saut; ++i) { double a, b; m.suivant (p, a, b); }
        for (int i = 0; i < n; ++i)
        {
            double a, b; m.suivant (p, a, b);
            q = std::max (q, std::abs (a + b));
        }
        return q;
    };
    const double auSommet = pic (11000, 3000);   // attaque 0,3 s ~ 13 200 ech.
    const double apres    = pic (25000, 6000);   // la tenue est a zero
    std::printf ("    niveau au sommet de l'enveloppe %.3f, apres %.3f\n",
                 auSommet, apres);
    verifie ("l'enveloppe modulante creuse le niveau", apres - auSommet, 0.05, 2.0);

    // --- une modulation NON cablee ne doit rien faire
    ReglagesPatch q = base();
    q.mods[0].type = TypeMod::LFO; q.mods[0].p1 = 5.0;
    // aucun routage
    Moteur m2; m2.prepare (44100.0, 4);
    m2.noteOn (q, 60, 100, roms, tl);
    double e2 = 0.0;
    for (int i = 0; i < 20000; ++i) { double a,b; m2.suivant (q,a,b); e2 += (a+b)*(a+b); }
    ReglagesPatch q0 = base();
    Moteur m3; m3.prepare (44100.0, 4);
    m3.noteOn (q0, 60, 100, roms, tl);
    double e3 = 0.0;
    for (int i = 0; i < 20000; ++i) { double a,b; m3.suivant (q0,a,b); e3 += (a+b)*(a+b); }
    verifie ("un modulateur non cable ne change rien",
             std::abs (e2 - e3) / std::max (e3, 1e-12), 0.0, 1e-9);
}


// ------------------------------------------- 14. chorus et reverbe

static void testEffets()
{
    std::printf ("\n14. CHORUS ET REVERBE\n");

    const double fs = 44100.0;

    // --- 1. chacun prolonge le son, mais PAS SUR LA MEME DUREE.
    // Le chorus retarde de 12 ms +- 6 ; la reverbe tient des secondes.
    // Mesurer les deux dans la meme fenetre ne prouverait rien.
    auto tenue = [fs] (bool cho, bool rev)
    {
        Chorus c;   c.prepare (fs);
        Reverbe r;  r.prepare (fs);
        c.regle (0.4, 0.5, cho ? 0.5 : 0.0, 0.45);
        r.regle (0.6, 0.4, rev ? 0.5 : 0.0, 0.02);
        double eProche = 0.0, eLoin = 0.0;   // 2-45 ms, puis au-dela
        for (int i = 0; i < 60000; ++i)
        {
            double g = (i < 64) ? 1.0 : 0.0, d = g;
            c.traite (g, d);
            r.traite (g, d);
            const double v = g * g + d * d;
            if (i >= 100 && i < 2000) eProche += v;
            else if (i >= 2000)       eLoin   += v;
        }
        return std::make_pair (eProche, eLoin);
    };

    const auto sec   = tenue (false, false);
    const auto avecC = tenue (true,  false);
    const auto avecR = tenue (false, true);

    std::printf ("    energie apres l'impulsion :\n");
    std::printf ("      %-12s proche (2-45 ms) %.6f   loin (>45 ms) %.6f\n",
                 "sans effet", sec.first, sec.second);
    std::printf ("      %-12s proche %.6f   loin %.6f\n", "chorus", avecC.first, avecC.second);
    std::printf ("      %-12s proche %.6f   loin %.6f\n", "reverbe", avecR.first, avecR.second);

    verifie ("sans effet, rien apres l'impulsion", sec.first + sec.second, 0.0, 1e-9);
    verifie ("le chorus retarde une copie (2-45 ms)", avecC.first, 1e-4, 100.0);
    verifie ("mais ne fait pas de queue longue", avecC.second, 0.0, 0.05);
    verifie ("la reverbe, elle, tient au-dela", avecR.second, 0.05, 1000.0);

    // --- 2. la reverbe ne diverge pas, meme reglee au maximum
    Reverbe r2; r2.prepare (fs); r2.regle (1.0, 0.0, 1.0, 0.0);
    double pic = 0.0, picTard = 0.0;
    for (int i = 0; i < 44100 * 8; ++i)
    {
        double g = (i < 64) ? 1.0 : 0.0, d = g;
        r2.traite (g, d);
        const double v = std::abs (g);
        if (i < 44100)          pic     = std::max (pic, v);
        if (i > 44100 * 7)      picTard = std::max (picTard, v);
    }
    std::printf ("    reverbe au maximum : pic %.4f, apres 7 s %.6f\n", pic, picTard);
    verifie ("la reverbe s'eteint (pas de divergence)", picTard / std::max (pic, 1e-12),
             0.0, 0.35, "x");

    // --- 2 bis. LE MELANGE NE DOIT PAS CREUSER LE NIVEAU.
    // /!\ Retour d'ecoute : « la reverb assombrit enormement le signal, comme
    // si ca diminuait le volume ». Un melange lineaire coute 2,2 dB de sec a
    // 22 % - on entend « moins fort » avant d'entendre « plus de reverbe ».
    {
        auto niveau = [fs] (double mel)
        {
            Reverbe r; r.prepare (fs);
            r.regle (0.5, 0.4, mel, 0.02);
            double e = 0.0;
            const int n = 60000;
            for (int i = 0; i < n; ++i)
            {
                double g = 0.4 * std::sin (2.0 * kPi * 220.0 * (double) i / fs), d = g;
                r.traite (g, d);
                if (i > 20000) e += g * g;
            }
            return 10.0 * std::log10 (e / (double) (n - 20000) + 1e-30);
        };
        const double sec = niveau (0.0), mouille = niveau (0.25);
        std::printf ("    niveau a melange 0 %% : %.2f dB, a 25 %% : %.2f dB\n", sec, mouille);
        verifie ("le melange ne creuse pas le niveau", std::abs (mouille - sec),
                 0.0, 1.5, "dB");
    }

    // --- 2 ter. LA QUEUE DOIT ETRE DIFFUSE, pas une suite d'echos qu'on compte.
    // /!\ Retour d'ecoute : « la reverb sonne metallique ». Un reseau de quatre
    // lignes sans diffusion d'entree rend quatre echos discrets. On mesure le
    // facteur de crete de la reponse impulsionnelle : eleve = des pics isoles,
    // bas = une queue continue.
    {
        Reverbe r; r.prepare (fs);
        r.regle (0.6, 0.4, 1.0, 0.0);
        double crete = 0.0, somme = 0.0; int n = 0;
        for (int i = 0; i < 44100; ++i)
        {
            double g = (i < 4) ? 1.0 : 0.0, d = g;
            r.traite (g, d);
            if (i > (int) (0.02 * fs) && i < (int) (0.20 * fs))
            { crete = std::max (crete, std::abs (g)); somme += g * g; ++n; }
        }
        const double fc = crete / std::sqrt (somme / std::max (n, 1));
        std::printf ("    facteur de crete de la queue (20-200 ms) : %.1f\n", fc);
        verifie ("la queue est diffuse, pas une suite d'echos", fc, 1.0, 8.0, "x");
    }

    // --- 2 quater. LA NATURE DU CHORUS EST UN VRAI CHOIX DE CIRCUIT.
    // ⭐ Deux mecanismes differents, pas deux dosages : un circuit a seaux
    // limite la bande ET souffle ; un chorus numerique d'epoque (D-50, M1) est
    // pleine bande et silencieux. Le souffle est ce qui les separe vraiment.
    {
        auto silence = [fs] (bool seaux)
        {
            Chorus c; c.prepare (fs);
            c.regleNature (seaux);
            c.regle (0.4, 0.5, 1.0, 0.5);
            double e = 0.0;
            const int n = 40000;
            for (int i = 0; i < n; ++i)
            {
                double g = 0.0, d = 0.0;      // AUCUNE entree
                c.traite (g, d);
                if (i > 8000) e += g * g;
            }
            return 10.0 * std::log10 (e / (double) (n - 8000) + 1e-30);
        };
        const double bbd = silence (true), num = silence (false);
        std::printf ("    plancher de bruit a l'entree muette : seaux %.0f dB, "
                     "numerique %.0f dB\n", bbd, num);
        verifie ("le circuit a seaux souffle", bbd, -95.0, -55.0, "dB");
        verifie ("le numerique est parfaitement silencieux", num, -400.0, -200.0, "dB");
    }

    // --- 3. la couleur (seaux) assombrit VRAIMENT.
    // /!\ Ma premiere version mesurait la part d'aigu par x - passeBas(x).
    // Sur un Butterworth d'ordre 4, le dephasage rend cette difference proche
    // de 1 sur presque toute la bande : la mesure repondait ~ la meme chose
    // dans les deux cas. On sonde donc a une frequence precise, avec un sinus.
    auto rmsA = [fs] (double f, double couleur)
    {
        Chorus c; c.prepare (fs);
        c.regle (0.4, 0.5, 1.0, couleur);   // melange 1 : on n'entend que l'effet
        double e = 0.0;
        const int n = 40000;
        for (int i = 0; i < n; ++i)
        {
            double g = std::sin (2.0 * kPi * f * (double) i / fs), d = g;
            c.traite (g, d);
            if (i > 8000) e += g * g + d * d;
        }
        return std::sqrt (e / (double) (n - 8000));
    };
    const double aigu = 20.0 * std::log10 (rmsA (6000.0, 1.0) / rmsA (6000.0, 0.0));
    const double grave = 20.0 * std::log10 (rmsA (300.0, 1.0) / rmsA (300.0, 0.0));
    std::printf ("    effet de la couleur : a 6 kHz %.1f dB, a 300 Hz %.1f dB\n",
                 aigu, grave);
    verifie ("les seaux mangent l'aigu", -aigu, 4.0, 60.0, "dB");
    verifie ("mais laissent le grave", std::abs (grave), 0.0, 1.5, "dB");
}

// ------------------------------------------- 15. instabilite

static void testInstabilite()
{
    std::printf ("\n15. INSTABILITE - ce qu'une machine a vraiment\n");
    std::printf ("    (⭐ correction de modele : sur un ECHANTILLONNEUR toutes les\n"
                 "     voix lisent depuis la MEME horloge a quartz. Il n'y a pas\n"
                 "     de desaccord entre voix a aller chercher. Ce qui differe,\n"
                 "     c'est la partie ANALOGIQUE de chaque carte : coupure et\n"
                 "     niveau. Un chorus de desaccord, c'est un synthe analogique,\n"
                 "     pas un rompleur.)\n");

    const double fsC = 44100.0 / (double) kPasControle;

    // --- 1. par defaut, la machine est DISCRETE.
    // /!\ Retour d'ecoute : « mouvement a zero reste audible ». Les valeurs par
    // defaut mettaient 4 cents de pleurage et 3 cents de derive, sur une
    // machine qui n'a physiquement ni l'un ni l'autre a ce niveau.
    {
        ReglagesInstabilite r;   // valeurs par defaut
        std::printf ("    par defaut : pleurage %.1f cents, derive de hauteur %.1f cents,\n"
                     "                 timbre %.1f %%, niveau %.2f dB\n",
                     r.wowProfCents, r.deriveCents,
                     100.0 * r.deriveTimbre, r.deriveNiveauDb);
        verifie ("pas de pleurage par defaut (une ROM n'en a pas)",
                 r.wowProfCents + r.flutProfCents, 0.0, 0.0, "cents");
        verifie ("derive de hauteur symbolique", r.deriveCents, 0.0, 1.5, "cents");
        verifie ("mais un vrai ecart de timbre", 100.0 * r.deriveTimbre, 1.0, 6.0, "%");
    }

    // --- 2. LA DERIVE PORTE SUR LE TIMBRE ET LE NIVEAU, pas sur la hauteur.
    {
        ReglagesInstabilite r;
        Derive d; d.prepare (fsC, 5u);
        double cCents = 0.0, cOct = 0.0, cDb = 0.0;
        for (int i = 0; i < (int) (fsC * 120.0); ++i)
        {
            const auto a = d.suivant (r);
            cCents = std::max (cCents, std::abs (a.cents));
            cOct   = std::max (cOct,   std::abs (a.octaves));
            cDb    = std::max (cDb,    std::abs (a.dB));
        }
        std::printf ("    sur 2 min : hauteur %.2f cents, coupure %.1f %%, niveau %.2f dB\n",
                     cCents, 100.0 * (std::pow (2.0, cOct) - 1.0), cDb);
        verifie ("la hauteur bouge a peine", cCents, 0.0, 1.0, "cents");
        verifie ("la coupure, elle, bouge vraiment",
                 100.0 * (std::pow (2.0, cOct) - 1.0), 0.5, 8.0, "%");
        verifie ("le niveau aussi", cDb, 0.02, 0.5, "dB");
    }

    // --- 3. LE PLEURAGE N'EST PLUS PERIODIQUE.
    // /!\ Retour d'ecoute : « trop repetitif ». Un sinus pur se reconnait au
    // bout de trois tours et devient une signature. On mesure l'autocorrelation
    // a la periode nominale : proche de 1 = motif qui se referme.
    {
        ReglagesInstabilite r;
        r.wowProfCents = 10.0; r.wowVitesse = 1.0;
        Horlogerie h; h.prepare (fsC);
        const int n = (int) (fsC * 60.0);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i) y[(size_t) i] = h.suivant (r);

        const int lag = (int) fsC;      // une periode nominale
        double num = 0.0, den = 0.0;
        for (int i = 0; i + lag < n; ++i)
        { num += y[(size_t) i] * y[(size_t) (i + lag)]; den += y[(size_t) i] * y[(size_t) i]; }
        const double autoc = num / std::max (den, 1e-30);
        std::printf ("    autocorrelation du pleurage a sa periode : %.3f\n", autoc);
        verifie ("le motif ne se referme pas", std::abs (autoc), 0.0, 0.5);

        double crete = 0.0;
        for (double v : y) crete = std::max (crete, std::abs (v));
        verifie ("mais la profondeur demandee est bien la", crete, 8.0, 11.0, "cents");
    }

    // --- 4. le partage : le pleurage est COMMUN, la derive est PROPRE.
    {
        ReglagesInstabilite r;
        Derive a, b; a.prepare (fsC, 1u); b.prepare (fsC, 2u);
        double ecart = 0.0;
        for (int i = 0; i < (int) (fsC * 60.0); ++i)
        {
            const auto va = a.suivant (r), vb = b.suivant (r);
            ecart = std::max (ecart, std::abs (va.octaves - vb.octaves));
        }
        std::printf ("    deux cartes de voix : %.1f %% d'ecart de coupure\n",
                     100.0 * (std::pow (2.0, ecart) - 1.0));
        verifie ("deux circuits ne derivent pas ensemble",
                 100.0 * (std::pow (2.0, ecart) - 1.0), 0.5, 12.0, "%");
    }

    // --- 5. a l'arret, rien ne bouge : PROPRE reste tenable
    {
        ReglagesInstabilite r; r.actif = false;
        Horlogerie h; h.prepare (fsC);
        Derive de; de.prepare (fsC, 3u);
        double crete = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const auto a = de.suivant (r);
            crete = std::max (crete, std::abs (h.suivant (r))
                                     + std::abs (a.cents) + std::abs (a.octaves));
        }
        verifie ("instabilite coupee = machine parfaitement stable", crete, 0.0, 1e-12);
    }

    // --- 6. SUR LE SON : avec les valeurs par defaut, un unisson ne doit PAS
    // battre franchement - c'est exactement ce que l'ecoute reprochait.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
        int tl[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                               (int) rom.size(), (int) rom.size() };

        auto battement = [&] (double cents)
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001;
            p.couche[0].tenue = 1.0;
            p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
            p.couche[0].grain.actif = false;
            p.instab.deriveCents = cents;

            Moteur m; m.prepare (44100.0, 8);
            m.noteOn (p, 60, 100, roms, tl);
            m.noteOn (p, 60, 100, roms, tl);
            double env = 0.0, mini = 1e9, maxi = 0.0;
            const double lis = 1.0 - std::exp (-1.0 / (0.02 * 44100.0));
            for (int i = 0; i < 44100 * 4; ++i)
            {
                double a = 0.0, b = 0.0; m.suivant (p, a, b);
                env += lis * (std::abs (a + b) - env);
                if (i > 44100 / 2) { mini = std::min (mini, env); maxi = std::max (maxi, env); }
            }
            return 20.0 * std::log10 (maxi / std::max (mini, 1e-12));
        };

        ReglagesInstabilite def;
        const double parDefaut = battement (def.deriveCents);
        const double pousse    = battement (8.0);
        std::printf ("    battement d'un unisson : par defaut %.1f dB, "
                     "a 8 cents %.1f dB\n", parDefaut, pousse);
        verifie ("par defaut, l'unisson ne bat pas", parDefaut, 0.0, 6.0, "dB");
        verifie ("mais le mecanisme repond si on le pousse",
                 pousse - parDefaut, 6.0, 60.0, "dB");
    }
}

// ------------------------------------------- 16. la ROM-recette

/** Amplitude a une frequence donnee, mesuree par correlation directe. */
static double ampA (const std::vector<double>& x, int deb, int n,
                    double f, double fs)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double a = 2.0 * kPi * f * (double) i / fs;
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
        re += x[(size_t) (deb + i)] * w * std::cos (a);
        im += x[(size_t) (deb + i)] * w * std::sin (a);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

static Recette recetteTest()
{
    Recette r;
    r.f0 = 220.0;
    r.dureeAttaque = 0.5;
    r.cyclesBoucle = 8;
    r.nPartiels = 12;
    r.graine = 4242u;
    for (int k = 0; k < r.nPartiels; ++k)
    {
        r.partiels[k].amplitude = std::pow ((double) (k + 1), -1.3);
        // decroissances CROISSANTES avec le rang : l'aigu part en premier,
        // comme sur a peu pres tout ce qui vibre.
        r.partiels[k].decroiss  = 0.20 + 0.065 * (double) k;
        r.partiels[k].desaccord = 0.0;
    }
    return r;
}

static void testRecette()
{
    std::printf ("\n16. LA ROM-RECETTE - 108 octets qui fabriquent une ROM\n");

    const double fs = kFsRomEII;

    // --- 1. l'etat tient dans 108 octets, et le passage par la forme compacte
    // est STABLE. Sans ca, « la recette est l'etat » serait un slogan.
    {
        const Recette a = recetteTest();
        uint8_t o1[kOctetsRecette], o2[kOctetsRecette];
        versOctets (a, o1);
        versOctets (depuisOctets (o1), o2);
        int diff = 0;
        for (int i = 0; i < kOctetsRecette; ++i) if (o1[i] != o2[i]) ++diff;
        std::printf ("    taille de l'etat : %d octets\n", kOctetsRecette);
        verifie ("l'etat tient dans 108 octets", (double) kOctetsRecette, 108.0, 108.0, "o");
        verifie ("aller-retour octets stable (0 octet change)", (double) diff, 0.0, 0.0);

        std::vector<double> r1, r2;
        rendRom (a, r1, fs);
        rendRom (depuisOctets (o1), r2, fs);
        double ec = 0.0;
        for (size_t i = 0; i < std::min (r1.size(), r2.size()); ++i)
            ec = std::max (ec, std::abs (r1[i] - r2[i]));
        verifie ("la recette relue rend la MEME ROM", ec, 0.0, 1e-15);
    }

    // --- 2. deterministe, et sensible : deux recettes voisines != la meme ROM
    {
        Recette a = recetteTest(), b = recetteTest();
        b.partiels[3].amplitude *= 1.6;
        std::vector<double> ra, rb, ra2;
        rendRom (a, ra, fs); rendRom (a, ra2, fs); rendRom (b, rb, fs);
        double memeRec = 0.0, autreRec = 0.0;
        for (size_t i = 0; i < ra.size(); ++i)
        {
            memeRec  = std::max (memeRec,  std::abs (ra[i] - ra2[i]));
            autreRec = std::max (autreRec, std::abs (ra[i] - rb[i]));
        }
        verifie ("meme recette, meme ROM au bit pres", memeRec, 0.0, 0.0);
        verifie ("une autre recette donne une autre ROM", autreRec, 0.01, 2.0);
    }

    // --- 3. la ROM sonne bien a la hauteur demandee
    {
        const Recette a = recetteTest();
        std::vector<double> rom;
        const auto info = rendRom (a, rom, fs);
        const int nB = info.nTotal - info.nAttaque;
        const double f1 = ampA (rom, info.nAttaque, nB, 220.0, fs);
        const double horsPiste = ampA (rom, info.nAttaque, nB, 220.0 * 1.37, fs);
        std::printf ("    boucle : %d echantillons, fondamentale %.4f, "
                     "a cote (x1,37) %.5f\n", nB, f1, horsPiste);
        verifie ("la fondamentale demandee est bien la", f1, 0.05, 2.0);
        verifie ("et rien la ou il n'y a pas de partiel", horsPiste, 0.0, 0.05);
    }

    // --- 4. LE POINT CENTRAL : le spectre EVOLUE, parce que chaque partiel a
    // sa propre decroissance. Sans ca on ne fabrique qu'une forme d'onde de
    // plus - c'est ce qui rend l'additif bon marche si mort.
    {
        auto balance = [&] (const Recette& r)
        {
            // /!\ Une fenetre courte ne peut pas voir une evolution lente :
            // ma premiere version comparait 3 ms et 290 ms et concluait
            // « 1,2 dB ». Le mecanisme marchait, la fenetre etait trop etroite.
            Recette rr = r; rr.dureeAttaque = 1.5;
            std::vector<double> rom;
            rendRom (rr, rom, fs);
            const int n = 4096;
            auto rapport = [&] (int deb)
            {
                const double h1 = ampA (rom, deb, n, r.f0, fs);
                const double h6 = ampA (rom, deb, n, r.f0 * 6.0, fs);
                return 20.0 * std::log10 (std::max (h6, 1e-12) / std::max (h1, 1e-12));
            };
            return rapport (33000) - rapport (100);   // fin - debut, en dB
        };

        Recette evol = recetteTest();
        Recette fige = recetteTest();
        for (int k = 0; k < fige.nPartiels; ++k) fige.partiels[k].decroiss = 0.30;

        const double dEvol = balance (evol), dFige = balance (fige);
        std::printf ("    variation du rapport H6/H1 entre debut et fin :\n");
        std::printf ("      decroissances propres a chaque partiel : %6.1f dB\n", dEvol);
        std::printf ("      decroissances toutes egales            : %6.1f dB\n", dFige);
        verifie ("le timbre evolue vraiment dans le temps", -dEvol, 6.0, 90.0, "dB");
        verifie ("et il ne bouge pas si les decroissances sont egales",
                 std::abs (dFige), 0.0, 1.0, "dB");
    }

    // --- 5. la boucle ne claque pas.
    // /!\ On n'a PAS de fondu de boucle dans le lecteur : c'est au materiau
    // d'etre juste. Le raccord doit donc etre du meme ordre qu'un pas normal.
    {
        Recette a = recetteTest();
        a.partiels[5].desaccord = 27.0;    // un partiel volontairement de travers
        a.bruit[0].niveau = 0.25;
        std::vector<double> rom;
        const auto info = rendRom (a, rom, fs);
        const int d0 = info.nAttaque, d1 = info.nTotal;
        double pasMax = 0.0;
        for (int i = d0; i < d1 - 1; ++i)
            pasMax = std::max (pasMax, std::abs (rom[(size_t) (i + 1)] - rom[(size_t) i]));
        const double raccord = std::abs (rom[(size_t) d0] - rom[(size_t) (d1 - 1)]);
        std::printf ("    raccord de boucle %.6f, pas maximal dans la boucle %.6f\n",
                     raccord, pasMax);
        verifie ("le raccord de boucle n'est pas un clac", raccord / pasMax, 0.0, 1.5, "x");
    }

    // --- 6. le bruit ajoute bien du bruit, la ou il n'y a pas de partiel
    {
        auto horsHarmonique = [&] (double niveau)
        {
            Recette a = recetteTest();
            a.bruit[0].niveau = niveau; a.bruit[0].coupure = 0.8;
            a.bruit[0].decroiss = 0.0;
            std::vector<double> rom;
            rendRom (a, rom, fs);
            return ampA (rom, 2000, 8192, 220.0 * 4.5, fs);
        };
        const double sans = horsHarmonique (0.0), avec = horsHarmonique (0.6);
        std::printf ("    energie entre deux harmoniques : sans bruit %.6f, "
                     "avec %.6f\n", sans, avec);
        verifie ("le bruit remplit bien l'entre-deux", avec / std::max (sans, 1e-9),
                 3.0, 1e9, "x");
    }

    // --- 8. SUR LE MOTEUR : une ROM de recette tourne en boucle pendant des
    // secondes sans qu'on entende le raccord.
    // /!\ Le test 5 prouve que le MATERIAU est juste ; celui-ci prouve que ca
    // survit au lecteur, qui reechantillonne et ne tombe donc pas pile sur les
    // bornes. C'est la seule des deux mesures qui corresponde a ce qu'on entend.
    {
        Recette a = recetteTest();
        a.f0 = 261.6255653;
        std::vector<double> rom;
        const auto info = rendRom (a, rom, kFsRomEII);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        const int tl[kNbCouches] = { info.nTotal, 0, 0, 0 };

        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001; p.couche[0].tenue = 1.0;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.actif = false;
        p.couche[0].boucleDebut = info.boucleDebut();
        p.couche[0].boucleFin = 1.0;
        p.instab.actif = false;

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 60, 100, roms, tl);

        // on laisse passer l'attaque, puis on regarde plusieurs tours de boucle
        for (int i = 0; i < 30000; ++i) { double x, y; m.suivant (p, x, y); }
        double prec = 0.0, pasMax = 0.0, somme = 0.0;
        const int n = 44100 * 3;
        for (int i = 0; i < n; ++i)
        {
            double x = 0.0, y = 0.0; m.suivant (p, x, y);
            const double pas = std::abs (x - prec);
            if (i > 0) { pasMax = std::max (pasMax, pas); somme += pas; }
            prec = x;
        }
        const double moyen = somme / (double) (n - 1);
        std::printf ("    3 s de boucle : pas moyen %.6f, pas maximal %.6f "
                     "(rapport %.1f)\n", moyen, pasMax, pasMax / std::max (moyen, 1e-12));
        verifie ("aucun clac sur 3 secondes de boucle", pasMax / std::max (moyen, 1e-12),
                 0.0, 8.0, "x");
    }

    // --- 7. multi-f0 : un son est parfois plusieurs notes a la fois
    {
        auto aQuinte = [&] (double niv)
        {
            Recette a = recetteTest();
            a.ratio[1] = 1.5; a.niveau[1] = niv;
            std::vector<double> rom;
            const auto info = rendRom (a, rom, fs);
            return ampA (rom, info.nAttaque, info.nTotal - info.nAttaque,
                         220.0 * 1.5, fs);
        };
        const double sans = aQuinte (0.0), avec = aQuinte (0.8);
        std::printf ("    a 1,5 x f0 : source seule %.6f, avec la quinte %.4f\n",
                     sans, avec);
        verifie ("une deuxieme source pose bien sa propre hauteur",
                 avec / std::max (sans, 1e-9), 5.0, 1e9, "x");
    }
    // --- 9. v2 : LA CHUTE DE HAUTEUR. C'est le 808, le tom, le kick.
    // /!\ La sonde de la v1 mesurait 0,0000 a 150 Hz au debut d'un 808 : le
    // corps y etait, le clic y etait, la signature manquait.
    {
        auto un808 = [&] (double prof)
        {
            Recette a;
            a.f0 = 50.0; a.nPartiels = 1; a.cyclesBoucle = 4; a.dureeAttaque = 1.0;
            a.partiels[0].amplitude = 1.0; a.partiels[0].decroiss = 0.42;
            a.pitchProf = prof; a.pitchDuree = 0.06; a.pitchCourbe = 0.5;
            a.graine = 11u;
            std::vector<double> rom;
            rendRom (a, rom, fs);
            // /!\ On mesure la HAUTEUR, pas l amplitude a une frequence fixe :
            // pendant un balayage, l energie a 150 Hz n est qu une tranche du
            // passage, et une amplitude ne dit pas si la hauteur a bouge.
            auto hauteur = [&] (int deb, int n)
            {
                int pz = 0; double prec = rom[(size_t) deb];
                for (int i = 1; i < n; ++i)
                {
                    const double v = rom[(size_t) (deb + i)];
                    if (prec <= 0.0 && v > 0.0) ++pz;
                    prec = v;
                }
                return (double) pz * fs / (double) n;
            };
            // /!\ Fenetre a l'INTERIEUR de la ROM : ma premiere version lisait
            // 16 000 echantillons a partir de 20 000 alors que la ROM en fait
            // 29 916. Elle mesurait donc du hors-bornes, et annoncait 34,6 Hz
            // pour un son a 50 Hz - le materiau etait juste, la sonde debordait.
            return std::make_pair (hauteur (100, 550),     // 20 premieres ms
                                   hauteur (15000, 9000)); // hauteur etablie
        };
        const auto plat = un808 (0.0), chute = un808 (19.0);
        std::printf ("    808 : hauteur mesuree (passages par zero)\n");
        std::printf ("      sans chute : depart %5.1f Hz, etabli %5.1f Hz\n",
                     plat.first, plat.second);
        std::printf ("      avec 19 demi-tons : depart %5.1f Hz, etabli %5.1f Hz\n",
                     chute.first, chute.second);
        verifie ("sans enveloppe, la hauteur ne bouge pas",
                 plat.first / plat.second, 0.9, 1.1, "x");
        verifie ("avec, le son DEMARRE plus haut",
                 chute.first / chute.second, 1.8, 3.2, "x");
        verifie ("et il redescend bien a la hauteur voulue", chute.second, 45.0, 55.0, "Hz");
    }

    // --- 10. v2 : UN PARTIEL PEUT MONTER.
    // /!\ La sonde de la v1 mesurait -0,21 dB entre le debut et 0,72 s : la
    // decroissance minimale etait PLAT, jamais croissant. Une nappe qui s'ouvre
    // etait donc hors d'atteinte du materiau.
    {
        auto h5 = [&] (int montee)
        {
            Recette a;
            a.f0 = 220.0; a.nPartiels = 6; a.dureeAttaque = 1.4; a.cyclesBoucle = 8;
            a.graine = 5u;
            for (int k = 0; k < 6; ++k)
            {
                a.partiels[k].amplitude = 0.5;
                a.partiels[k].decroiss = 0.0;
                a.partiels[k].montee = (k == 4) ? montee : 0;
            }
            std::vector<double> rom;
            rendRom (a, rom, fs);
            const double deb = ampA (rom, 200, 2048, 1100.0, fs);
            const double fin = ampA (rom, 24000, 2048, 1100.0, fs);
            return 20.0 * std::log10 (std::max (fin, 1e-12) / std::max (deb, 1e-12));
        };
        const double sans = h5 (0), avec = h5 (3);   // 3 = 600 ms
        std::printf ("    H5 entre le debut et 0,87 s : sans montee %+.1f dB, "
                     "avec %+.1f dB\n", sans, avec);
        verifie ("sans montee, le partiel est plat", std::abs (sans), 0.0, 1.0, "dB");
        verifie ("avec montee, il GONFLE vraiment", avec, 8.0, 60.0, "dB");
    }

    // --- 11. v2 : DEUX BANDES DE BRUIT.
    // /!\ La sonde de la v1 montrait une seule pente sur tout le spectre : un
    // souffle PLUS un sifflement metallique, ce sont deux bandes.
    {
        auto pente = [&] (bool deux)
        {
            Recette a;
            a.f0 = 220.0; a.nPartiels = 1; a.partiels[0].amplitude = 0.0001;
            a.dureeAttaque = 1.0; a.cyclesBoucle = 8; a.graine = 99u;
            a.bruit[0].niveau = 1.0; a.bruit[0].coupure = 0.15; a.bruit[0].decroiss = 0.0;
            if (deux) { a.bruit[1].niveau = 0.8; a.bruit[1].coupure = 0.95;
                        a.bruit[1].decroiss = 0.0; }
            std::vector<double> rom;
            rendRom (a, rom, fs);
            const double bas  = ampA (rom, 2000, 8192, 400.0, fs);
            const double haut = ampA (rom, 2000, 8192, 9000.0, fs);
            return 20.0 * std::log10 (std::max (haut, 1e-12) / std::max (bas, 1e-12));
        };
        const double une = pente (false), deux = pente (true);
        std::printf ("    9 kHz par rapport a 400 Hz : une bande %.1f dB, "
                     "deux bandes %.1f dB\n", une, deux);
        verifie ("une seule bande : l'aigu est loin derriere", une, -90.0, -20.0, "dB");
        // /!\ Borne abaissee de 15 a 8 dB, et c'est justifie, pas complaisant :
        // depuis que le gain du filtre de bruit est compense, une bande a
        // coupure basse n'est plus attenuee, donc la bande aigue apporte
        // relativement moins. Le mecanisme n'a pas faibli - c'est la reference
        // qui a monte.
        verifie ("deux bandes : l'aigu remonte", deux - une, 8.0, 90.0, "dB");
    }

}

// ------------------------------------------- 17. la perte

static void testPerte()
{
    std::printf ("\n17. LA PERTE - la distance entre deux sons\n");
    std::printf ("    (/!\\ le prototype Python l'a paye cher : comparee raie a\n"
                 "     raie, la perte devient une AIGUILLE - plate sur 90 %% du\n"
                 "     chemin puis effondrement. On comparait deux realisations\n"
                 "     d'un bruit deterministe, pas deux timbres.)\n");

    const double fs = 44100.0;
    const int n = 32768;

    auto sinus = [n, fs] (double f, double a)
    {
        std::vector<double> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = a * std::sin (2.0 * kPi * f * (double) i / fs);
        return x;
    };

    const auto ref = sinus (440.0, 0.5);

    // --- 1. identique = zero. Sinon la perte ne mesure rien.
    verifie ("deux sons identiques : perte nulle",
             perte (empreinte (ref, fs), empreinte (ref, fs)), 0.0, 1e-12, "dB");

    // --- 2. insensible au NIVEAU : on compare des timbres, pas des volumes.
    verifie ("le meme son 12 dB plus fort : perte nulle",
             perte (empreinte (ref, fs), empreinte (sinus (440.0, 0.125), fs)),
             0.0, 0.01, "dB");

    // --- 3. et elle CROIT quand on s'eloigne. Un test d'ecart nul ne prouve
    // rien tout seul : une perte constamment nulle le passerait aussi.
    double prec = 0.0;
    bool croissante = true;
    std::printf ("    perte contre un sinus a 440 Hz :\n");
    for (double f : { 440.0, 445.0, 460.0, 550.0, 880.0 })
    {
        const double d = perte (empreinte (ref, fs), empreinte (sinus (f, 0.5), fs));
        std::printf ("      %6.0f Hz  %7.3f dB\n", f, d);
        if (f > 440.0 && d <= prec) croissante = false;
        prec = d;
    }
    verifie ("la perte croit quand on s'eloigne", croissante ? 1.0 : 0.0, 1.0, 1.0);

    // --- 4. PAS D'AIGUILLE : une petite perturbation doit donner une petite
    // perte. C'est exactement ce qui manquait au premier prototype.
    {
        Recette a = recetteTest();
        Recette b = a;
        for (int k = 0; k < a.nPartiels; ++k) b.partiels[k].amplitude *= 1.02;

        const auto cfg = grainRecherche (true);
        std::vector<double> ya, yb;
        rendDegrade (a, ya, 20000, fs, cfg);
        rendDegrade (b, yb, 20000, fs, cfg);
        const double petite = perte (empreinte (ya, fs), empreinte (yb, fs));

        Recette c = a;
        for (int k = 0; k < a.nPartiels; ++k) c.partiels[k].amplitude *= 3.0;
        std::vector<double> yc;
        rendDegrade (c, yc, 20000, fs, cfg);
        const double grosse = perte (empreinte (ya, fs), empreinte (yc, fs));

        std::printf ("    apres degradation d'epoque : perturbation de 2 %% "
                     "-> %.3f dB, de 300 %% -> %.3f dB\n", petite, grosse);
        verifie ("une petite perturbation coute peu", petite, 0.0, 1.2, "dB");
        verifie ("une grosse coute beaucoup plus", grosse / std::max (petite, 1e-6),
                 2.0, 1e6, "x");
    }

    // --- 5. CE QUE LA PERTE VOIT, ET CE QU'ELLE IGNORE. Les deux comptent.
    // Avec le plancher calibre, le BRUIT de quantification devient invisible -
    // c'est exactement le but : sans ca la recherche passerait son temps a
    // chasser des realisations de bruit. Mais le FORMATAGE spectral de la
    // machine (bande passante, filtre de reconstruction asservi), lui, doit
    // rester parfaitement visible, sinon « mesurer l'erreur APRES la
    // degradation » ne voudrait plus rien dire.
    {
        // /!\ Un son BRILLANT, obligatoirement : ma premiere version utilisait
        // la recette de reference (12 partiels a 220 Hz, donc rien au-dessus de
        // 2,6 kHz) et concluait que la perte ne voyait pas la bande passante.
        // Elle ne la voyait pas parce qu'il n'y avait RIEN entre 5 et 12 kHz -
        // la coupure a 0,18 et celle a 0,45 laissaient passer le meme son.
        Recette a;
        a.f0 = 440.0; a.nPartiels = 24; a.dureeAttaque = 0.5;
        a.cyclesBoucle = 8; a.graine = 77u;
        for (int k = 0; k < 24; ++k)
        {
            a.partiels[k].amplitude = std::pow ((double) (k + 1), -0.6);
            a.partiels[k].decroiss  = 0.15;
        }
        // /!\ On isole la QUANTIFICATION SEULE. Prendre `grainRecherche(false)`
        // comme reference changeait aussi l'interpolation, donc melangeait le
        // bruit mu-law avec le repliement du drop-sample - deux mecanismes
        // differents, et l'un des deux n'est pas du bruit.
        auto cfgS = grainRecherche (true);
        auto cfgP = cfgS; cfgP.quant = Quant::Aucune;
        std::vector<double> propre, sale;
        rendDegrade (a, propre, 20000, fs, cfgP);
        rendDegrade (a, sale,   20000, fs, cfgS);

        // le bruit est bien LA, sur le signal
        double e = 0.0, ref = 0.0;
        for (size_t i = 0; i < propre.size(); ++i)
        {
            const double d0 = propre[i] - sale[i];
            e += d0 * d0; ref += propre[i] * propre[i];
        }
        const double bruitDb = 10.0 * std::log10 (e / std::max (ref, 1e-30));
        const double vuBruit = perte (empreinte (propre, fs), empreinte (sale, fs));

        // meme recette, meme quantification, mais une machine a la bande
        // passante plus large : la perte DOIT le voir
        auto cfgLarge = cfgS; cfgLarge.coupure = 0.45;
        std::vector<double> large;
        rendDegrade (a, large, 20000, fs, cfgLarge);
        const double vuBande = perte (empreinte (sale, fs), empreinte (large, fs));

        std::printf ("    bruit de quantification : %.1f dB sous le signal, "
                     "vu par la perte %.4f dB\n", bruitDb, vuBruit);
        std::printf ("    bande passante de la machine (0,18 -> 0,45) : "
                     "vue par la perte %.4f dB\n", vuBande);

        verifie ("le bruit de quantification est bien present", -bruitDb, 15.0, 70.0, "dB");
        verifie ("mais la perte l'ignore (c'est le but)", vuBruit, 0.0, 0.15, "dB");
        verifie ("alors qu'elle voit tres bien la bande de la machine",
                 vuBande / std::max (vuBruit, 1e-6), 10.0, 1e9, "x");
    }
}

// ------------------------------------------- 18. analyse et recherche

static void testResynthese()
{
    std::printf ("\n18. ANALYSE ET RECHERCHE\n");

    const double fs = 44100.0;

    // --- 1. l'estimateur de hauteur ne se fait pas avoir par l'octave.
    // /!\ C'est le piege connu : sur un son a fondamentale faible, le produit
    // spectral harmonique repond a l'octave du dessus, et la correction naive
    // (« il y a de l'energie a f/2 ») descend une octave de trop.
    {
        auto tonalite = [fs] (double f0, double poidsFondamental)
        {
            std::vector<double> x (32768);
            for (size_t i = 0; i < x.size(); ++i)
            {
                double v = 0.0;
                for (int k = 1; k <= 10; ++k)
                {
                    const double a = (k == 1) ? poidsFondamental
                                              : std::pow ((double) k, -1.0);
                    v += a * std::sin (2.0 * kPi * f0 * (double) k * (double) i / fs
                                       + 0.3 * (double) k);
                }
                x[i] = v * 0.1;
            }
            return x;
        };

        std::printf ("    hauteur estimee :\n");
        int bons = 0, total = 0;
        for (double f0 : { 55.0, 110.0, 220.0, 440.0 })
            for (double w : { 1.0, 0.08 })      // 0,08 = fondamentale presque absente
            {
                const double e = estimeF0 (tonalite (f0, w), fs, 0, nullptr);
                const double cents = 1200.0 * std::log2 (std::max (e, 1.0) / f0);
                std::printf ("      %6.1f Hz (fondamentale %s) -> %7.2f Hz  %+7.0f cents\n",
                             f0, (w > 0.5) ? "forte " : "faible", e, cents);
                ++total;
                if (std::abs (cents) < 60.0) ++bons;
            }
        verifie ("l'octave est juste dans tous les cas", (double) bons,
                 (double) total, (double) total);
    }

    // --- 2. LA RECHERCHE TROUVE-T-ELLE ?
    // /!\ La cible est fabriquee PAR LE MOTEUR LUI-MEME. C'est la lecon du
    // prototype Python : un test dont la cible est hors de portee par
    // construction ne peut pas repondre a sa propre question - il mesure
    // l'ecart au possible, pas la qualite de la recherche.
    {
        Recette vraie;
        vraie.f0 = 196.0; vraie.nPartiels = 14; vraie.dureeAttaque = 0.8;
        vraie.cyclesBoucle = 20; vraie.graine = 1u;
        for (int k = 0; k < 14; ++k)
        {
            vraie.partiels[k].amplitude = std::pow ((double) (k + 1), -1.1);
            vraie.partiels[k].decroiss  = 0.22 + 0.045 * (double) k;
            vraie.partiels[k].montee    = (k > 9) ? 1 : 0;
        }
        vraie.bruit[0].niveau = 0.12; vraie.bruit[0].coupure = 0.4;

        const auto cfg = grainTransparent (fs);
        std::vector<double> cible;
        rendDegrade (vraie, cible, 44100, fs, cfg);

        ReglagesRecherche reg;
        reg.evaluations = 400;      // court : on teste que ca DESCEND, pas qu'on arrive
        reg.graine = 7u;

        RapportAnalyse rap;
        const auto st = resynthetise (cible, fs, cfg, reg, rap,
                                      [] (const EtatRecherche&) { return true; });

        const double cents = 1200.0 * std::log2 (std::max (rap.f0, 1.0) / 196.0);
        std::printf ("    cible fabriquee par le moteur (f0 196 Hz) :\n");
        std::printf ("      hauteur retrouvee %.2f Hz (%+.0f cents)\n", rap.f0, cents);
        std::printf ("      perte  amorce %.3f dB -> %.3f dB en %d evaluations\n",
                     st.perteDepart, st.perteMeilleure, st.evaluationsFaites);

        verifie ("la hauteur est retrouvee a l'octave pres",
                 std::abs (cents), 0.0, 60.0, "cents");
        verifie ("la recherche fait BAISSER la perte",
                 st.perteDepart - st.perteMeilleure, 0.05, 30.0, "dB");
        verifie ("et elle ne rend jamais pire que l'amorce",
                 st.perteMeilleure, 0.0, st.perteDepart);
    }

    // --- 3. deterministe : meme graine, meme resultat.
    // Sans ca on ne peut ni comparer deux reglages, ni reproduire un bug.
    {
        Recette v; v.f0 = 300.0; v.nPartiels = 8; v.dureeAttaque = 0.4;
        for (int k = 0; k < 8; ++k)
        { v.partiels[k].amplitude = std::pow ((double)(k+1), -1.0);
          v.partiels[k].decroiss = 0.3; }
        const auto cfg = grainTransparent (fs);
        std::vector<double> cible;
        rendDegrade (v, cible, 30000, fs, cfg);

        ReglagesRecherche reg; reg.evaluations = 120; reg.graine = 99u;
        RapportAnalyse r1, r2;
        auto rien = [] (const EtatRecherche&) { return true; };
        const auto a = resynthetise (cible, fs, cfg, reg, r1, rien);
        const auto b = resynthetise (cible, fs, cfg, reg, r2, rien);
        uint8_t oa[kOctetsRecette], ob[kOctetsRecette];
        versOctets (a.meilleure, oa); versOctets (b.meilleure, ob);
        int diff = 0;
        for (int i = 0; i < kOctetsRecette; ++i) if (oa[i] != ob[i]) ++diff;
        verifie ("meme graine, recette identique a l'octet pres", (double) diff, 0.0, 0.0);
    }

    // --- 4. la recherche ne touche pas ce qu'elle ne doit pas toucher
    {
        verifie ("l'octet de version est protege", octetMutable (0) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("la graine est protegee", octetMutable (12) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("le nombre de partiels est protege",
                 octetMutable (14) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("mais les partiels, eux, sont libres",
                 octetMutable (40) ? 1.0 : 0.0, 1.0, 1.0);
    }
}

// ------------------------------------------- 19. les trois macros

static void testMacros()
{
    std::printf ("\n19. LES TROIS MACROS\n");
    std::printf ("    (/!\\ la premiere facade proposait « Air » et « LoFi » :\n"
                 "     deux mots inventes qui ne correspondaient a rien dans le\n"
                 "     moteur. Chacune doit tirer sur un mecanisme MESURE, et sur\n"
                 "     un mecanisme DISJOINT des deux autres.)\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tl[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                           (int) rom.size(), (int) rom.size() };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].attaque = 0.001; p.couche[0].decli = 0.001; p.couche[0].tenue = 1.0;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.actif = false;
        p.couche[0].grain.coupure = 0.40;
        p.instab.actif = false;
        p.instab.wowProfCents = 0.0; p.instab.flutProfCents = 0.0;
        p.instab.deriveCents = 0.0; p.instab.deriveTimbre = 0.0;
        p.instab.deriveNiveauDb = 0.0;
        return p;
    };

    // --- 1. chaque macro change VRAIMENT le son, et pas de la meme facon
    auto joue = [&] (ReglagesPatch p, int nNotes)
    {
        Moteur m; m.prepare (44100.0, 8);
        for (int i = 0; i < nNotes; ++i) m.noteOn (p, 60, 100, roms, tl);
        std::vector<double> y;
        for (int i = 0; i < 44100 * 2; ++i)
        {
            double a = 0.0, b = 0.0; m.suivant (p, a, b);
            y.push_back (a);
        }
        return y;
    };

    // /!\ PAR FFT, PAS PAR x - passeBas(x). C'est la TROISIEME fois que cette
    // mesure me trompe : sur un filtre d'ordre 4, le dephasage rend la
    // difference proche de 1 sur presque toute la bande, donc la mesure
    // repond a peu pres la meme chose quoi qu'il arrive. Ici elle annoncait
    // -15,6 dB avant ET apres, pour une coupure passee de 9,9 kHz a 1,7 kHz.
    auto partAigu = [] (const std::vector<double>& y)
    {
        const int n = 16384;
        if ((int) y.size() < n + 8000) return 0.0;
        std::vector<std::complex<double>> b ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            b[(size_t) i] = y[(size_t) (i + 8000)] * w;
        }
        fft (b);
        double tot = 1e-30, haut = 1e-30;
        for (int k = 1; k <= n / 2; ++k)
        {
            const double e = std::norm (b[(size_t) k]);
            const double f = (double) k * 44100.0 / (double) n;
            tot += e;
            if (f > 4000.0) haut += e;
        }
        return 10.0 * std::log10 (haut / tot);
    };

    // USURE : le convertisseur. On mesure ce qu'elle AJOUTE par rapport au son
    // propre - c'est du bruit de quantification, donc un ecart d'energie.
    {
        auto p0 = base(); auto p1 = base();
        appliqueMacros (p1, 1.0, 0.0, 0.0);
        const auto y0 = joue (p0, 1), y1 = joue (p1, 1);
        double e = 0.0, ref = 0.0;
        for (size_t i = 0; i < y0.size(); ++i)
        { const double d = y1[i] - y0[i]; e += d * d; ref += y0[i] * y0[i]; }
        const double dif = 10.0 * std::log10 ((e + 1e-30) / (ref + 1e-30));
        std::printf ("    USURE a fond : %.1f dB d'ecart au son propre\n", dif);
        verifie ("USURE change vraiment le son", -dif, 5.0, 60.0, "dB");
    }

    // GRAIN : l'horloge. Elle referme la bande - donc l'aigu tombe.
    {
        auto p0 = base(); auto p1 = base();
        appliqueMacros (p1, 0.0, 1.0, 0.0);
        const double a0 = partAigu (joue (p0, 1)), a1 = partAigu (joue (p1, 1));
        std::printf ("    GRAIN : part d'aigu %.1f dB -> %.1f dB\n", a0, a1);
        verifie ("GRAIN referme bien la bande", a0 - a1, 3.0, 80.0, "dB");
    }

    // MOUVEMENT : l'instabilite. Deux voix sur la meme note doivent se mettre
    // a battre - c'est la derive, propre a chaque voix.
    {
        auto battement = [&] (ReglagesPatch p)
        {
            const auto y = joue (p, 2);
            double env = 0.0, mini = 1e9, maxi = 0.0;
            const double lis = 1.0 - std::exp (-1.0 / (0.02 * 44100.0));
            for (size_t i = 0; i < y.size(); ++i)
            {
                env += lis * (std::abs (y[i]) - env);
                if (i > 22050) { mini = std::min (mini, env); maxi = std::max (maxi, env); }
            }
            return 20.0 * std::log10 (maxi / std::max (mini, 1e-12));
        };
        auto p0 = base(); auto p1 = base();
        appliqueMacros (p1, 0.0, 0.0, 1.0);
        const double b0 = battement (p0), b1 = battement (p1);
        std::printf ("    MOUVEMENT : battement de l'unisson %.1f dB -> %.1f dB\n", b0, b1);
        verifie ("MOUVEMENT fait bouger les voix", b1 - b0, 3.0, 80.0, "dB");
    }

    // --- 2. ELLES SONT DISJOINTES. Deux boutons qui font la meme chose sous
    // deux noms, c'est un bouton de trop et une promesse fausse.
    {
        auto pU = base(); appliqueMacros (pU, 1.0, 0.0, 0.0);
        auto pG = base(); appliqueMacros (pG, 0.0, 1.0, 0.0);
        auto pM = base(); appliqueMacros (pM, 0.0, 0.0, 1.0);
        const auto ref = base();

        verifie ("USURE ne touche pas la bande de la machine",
                 pU.couche[0].grain.coupure, ref.couche[0].grain.coupure,
                 ref.couche[0].grain.coupure);
        verifie ("GRAIN ne touche pas la quantification",
                 (double) (pG.couche[0].grain.quant == ref.couche[0].grain.quant ? 1 : 0),
                 1.0, 1.0);
        verifie ("MOUVEMENT ne touche ni l'une ni l'autre",
                 (double) ((pM.couche[0].grain.quant == ref.couche[0].grain.quant
                            && pM.couche[0].grain.coupure == ref.couche[0].grain.coupure)
                           ? 1 : 0), 1.0, 1.0);
        verifie ("et ni USURE ni GRAIN ne touchent l'instabilite",
                 (double) ((pU.instab.deriveTimbre == ref.instab.deriveTimbre
                            && pG.instab.deriveTimbre == ref.instab.deriveTimbre)
                           ? 1 : 0), 1.0, 1.0);
    }

    // --- 3. a zero, une macro ne fait RIEN. Sinon on ne peut plus regler la
    // machine a la main.
    {
        auto p = base(); const auto ref = base();
        appliqueMacros (p, 0.0, 0.0, 0.0);
        const bool intact = (p.couche[0].grain.actif == ref.couche[0].grain.actif)
                         && (p.couche[0].grain.quant == ref.couche[0].grain.quant)
                         && (p.couche[0].grain.coupure == ref.couche[0].grain.coupure)
                         && (p.instab.actif == ref.instab.actif);
        verifie ("a zero, les macros ne touchent a rien",
                 intact ? 1.0 : 0.0, 1.0, 1.0);
    }
}

// ------------------------------------------- 20. le jeu : mono, legato, vol

static void testJeu()
{
    std::printf ("\n20. LE JEU - mono, legato, vol de voix, boucle\n");
    std::printf ("    (les cinq defauts remontes a l'ecoute)\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tl[kNbCouches] = { (int) rom.size(), (int) rom.size(),
                           (int) rom.size(), (int) rom.size() };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].attaque = 0.002; p.couche[0].decli = 0.002; p.couche[0].tenue = 1.0;
        p.couche[0].relache = 0.4;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].grain.actif = false;
        p.instab.actif = false;
        return p;
    };

    // --- 1. MONO : changer de note ne doit pas CLAQUER.
    // /!\ L'ancienne version faisait coupe() + declenche() : phase remise a
    // zero ET enveloppe remise a zero, donc une discontinuite pleine echelle a
    // chaque note. C'est le clic que l'ecoute a releve.
    {
        auto sautMax = [&] (ModeVoix mode)
        {
            ReglagesPatch p = base(); p.mode = mode;
            Moteur m; m.prepare (44100.0, 8);
            m.noteOn (p, 60, 100, roms, tl);
            double prec = 0.0, pasCourant = 0.0, pasAuChangement = 0.0;
            for (int i = 0; i < 22050; ++i)
            {
                double a, b; m.suivant (p, a, b);
                if (i > 4410) pasCourant = std::max (pasCourant, std::abs (a - prec));
                prec = a;
            }
            m.noteOn (p, 67, 100, roms, tl);      // changement de note
            for (int i = 0; i < 2000; ++i)
            {
                double a, b; m.suivant (p, a, b);
                pasAuChangement = std::max (pasAuChangement, std::abs (a - prec));
                prec = a;
            }
            return pasAuChangement / std::max (pasCourant, 1e-12);
        };
        const double mono = sautMax (ModeVoix::Mono);
        const double leg  = sautMax (ModeVoix::Legato);
        std::printf ("    saut au changement de note (x le pas normal) :\n");
        std::printf ("      mono %.2f   legato %.2f\n", mono, leg);
        verifie ("mono : pas de clic au changement de note", mono, 0.0, 3.0, "x");
        verifie ("legato : pas de clic non plus", leg, 0.0, 3.0, "x");
    }

    // --- 2. LEGATO : la hauteur doit VRAIMENT changer.
    // /!\ L'ancienne version faisait `return` si une voix sonnait : elle
    // gardait l'ancienne hauteur. Le legato ne faisait donc rien du tout.
    {
        auto hauteur = [&] (Moteur& m, ReglagesPatch& p, int n)
        {
            int pz = 0; double prec = 0.0;
            for (int i = 0; i < n; ++i)
            {
                double a, b; m.suivant (p, a, b);
                if (prec <= 0.0 && a > 0.0) ++pz;
                prec = a;
            }
            return (double) pz * 44100.0 / (double) n;
        };
        ReglagesPatch p = base(); p.mode = ModeVoix::Legato;
        Moteur m; m.prepare (44100.0, 8);
        m.noteOn (p, 60, 100, roms, tl);
        const double h1 = hauteur (m, p, 22050);
        m.noteOn (p, 72, 100, roms, tl);          // une octave au-dessus
        const double h2 = hauteur (m, p, 22050);
        std::printf ("    legato : %.0f Hz -> %.0f Hz (rapport %.2f)\n",
                     h1, h2, h2 / std::max (h1, 1.0));
        verifie ("legato : la note monte bien d'une octave", h2 / std::max (h1, 1.0),
                 1.8, 2.2, "x");
    }

    // --- 3. PILE DE NOTES : lacher la seconde touche revient sur la premiere.
    // Sans pile, le jeu lie est impossible : on tient un doigt et le son se tait.
    {
        ReglagesPatch p = base(); p.mode = ModeVoix::Mono;
        Moteur m; m.prepare (44100.0, 8);
        m.noteOn (p, 60, 100, roms, tl);
        m.noteOn (p, 67, 100, roms, tl);
        m.noteOff (p, 67);                        // on lache la seconde
        double e = 0.0;
        int pz = 0; double prec = 0.0;
        for (int i = 0; i < 22050; ++i)
        {
            double a, b; m.suivant (p, a, b);
            e += a * a;
            if (prec <= 0.0 && a > 0.0) ++pz;
            prec = a;
        }
        const double hz = (double) pz * 44100.0 / 22050.0;
        std::printf ("    apres avoir lache la 2e touche : %.0f Hz "
                     "(do3 = 262, sol3 = 392)\n", hz);
        verifie ("le son ne s'arrete pas", std::sqrt (e / 22050.0), 0.01, 2.0);
        verifie ("et il revient sur la touche encore tenue", hz, 200.0, 320.0, "Hz");
    }

    // --- 4. VOL DE VOIX : on prend d'abord ce qui est DEJA relache.
    // /!\ Retour d'ecoute : « si je tiens une nappe de deux touches et que je
    // fais de la melodie, ce sont les notes qui s'estompent qui doivent etre
    // volees, pas celles qui jouent ».
    //
    // /!\ ET LE TEST DOIT VRAIMENT DECLENCHER UN VOL. Ma premiere version
    // relachait la note melodique AVANT qu'elle ait sonne : son enveloppe etait
    // encore a zero, le relachement la tuait instantanement, la voix se liberait
    // et aucun vol n'avait lieu. Le test passait sans rien exercer.
    {
        ReglagesPatch p = base();
        p.polyphonie = 3;
        Moteur m; m.prepare (44100.0, 8);
        auto tourne = [&] (int n) { for (int i = 0; i < n; ++i)
                                    { double a, b; m.suivant (p, a, b); } };

        m.noteOn (p, 48, 100, roms, tl);   // nappe, tenue
        m.noteOn (p, 55, 100, roms, tl);   // nappe, tenue
        tourne (4410);
        m.noteOn (p, 72, 100, roms, tl);   // melodie
        tourne (8820);                     // elle a le temps de sonner
        m.noteOff (p, 72);                 // relachee : elle s'estompe
        tourne (2205);

        const long avant = m.volsEffectues();
        m.noteOn (p, 74, 100, roms, tl);   // pool plein : il FAUT voler
        tourne (441);

        const bool nappe = m.tientLaNote (48) && m.tientLaNote (55);
        std::printf ("    nappe (48, 55 tenues) + melodie, polyphonie 3 :\n");
        std::printf ("      vols %ld, do1 tenu %s, sol1 tenu %s\n",
                     m.volsEffectues() - avant,
                     m.tientLaNote (48) ? "oui" : "NON",
                     m.tientLaNote (55) ? "oui" : "NON");
        verifie ("un vol a bien eu lieu", (double) (m.volsEffectues() - avant),
                 1.0, 1.0);
        verifie ("mais les deux notes TENUES ont survecu", nappe ? 1.0 : 0.0, 1.0, 1.0);
    }

    // --- 5. LA BOUCLE SE RECALCULE QUAND LA ROM CHANGE DE TAILLE.
    // /!\ C'est ce qui faisait « se couper net » les sources autres que la
    // recette : le debut de boucle restait celui de l'ancienne ROM, se faisait
    // ramener a `fin - 2`, et la boucle tombait a DEUX echantillons.
    {
        auto grande = romTest (40000);
        auto petite = romTest (6000);
        const double* r1[kNbCouches] = { grande.data(), nullptr, nullptr, nullptr };
        int t1[kNbCouches] = { (int) grande.size(), 0, 0, 0 };
        const double* r2[kNbCouches] = { petite.data(), nullptr, nullptr, nullptr };
        int t2[kNbCouches] = { (int) petite.size(), 0, 0, 0 };

        ReglagesPatch p = base();
        p.couche[0].boucleDebut = 0.90;   // boucle tardive, comme une recette
        p.couche[0].boucleFin = 1.0;

        Moteur m; m.prepare (44100.0, 8);
        m.noteOn (p, 60, 100, r1, t1);
        for (int i = 0; i < 22050; ++i) { double a, b; m.suivant (p, a, b); }
        m.noteOff (p, 60);
        for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); }

        // meme voix reutilisee, ROM beaucoup plus courte
        m.noteOn (p, 60, 100, r2, t2);
        double e = 0.0;
        for (int i = 0; i < 44100 * 2; ++i)
        {
            double a, b; m.suivant (p, a, b);
            if (i > 22050) e += a * a;
        }
        const double rms = std::sqrt (e / 44100.0);
        std::printf ("    apres passage d'une ROM de 40 000 a une de 6 000 : "
                     "rms %.4f\n", rms);
        verifie ("le son ne se replie pas sur deux echantillons", rms, 0.02, 2.0);
    }
}

// ------------------------------------------- 21. le moteur analogique

/** Energie au-dessus d'une frequence, par FFT.
    /!\ Surtout PAS x - passeBas(x) : troisieme piege documente du projet. */
static double partAigu (const std::vector<double>& y, double fs, double fCoupe,
                        int deb = 8000)
{
    const int n = 16384;
    if ((int) y.size() < n + deb) return 0.0;
    std::vector<std::complex<double>> b ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
        b[(size_t) i] = y[(size_t) (i + deb)] * w;
    }
    fft (b);
    double tot = 1e-30, haut = 1e-30;
    for (int k = 1; k <= n / 2; ++k)
    {
        const double e = std::norm (b[(size_t) k]);
        tot += e;
        if ((double) k * fs / (double) n > fCoupe) haut += e;
    }
    return 10.0 * std::log10 (haut / tot);
}

static void testAnalogique()
{
    std::printf ("\n21. LE MOTEUR ANALOGIQUE VIRTUEL\n");
    std::printf ("    (le corpus est plein d'Oberheim, de Juno et de Jupiter :\n"
                 "     six valeurs y font ce que soixante-douze partiels font mal)\n");

    const double fs = 176400.0;   // le taux interne de la couche

    auto rend = [fs] (FormeOsc f, double hz, double largeur, int n)
    {
        Oscillateur o; o.prepare (fs); o.majHauteur (hz);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i) y[(size_t) i] = o.suivant (f, largeur);
        return y;
    };

    // --- 1. BANDE LIMITEE : on mesure le REPLIEMENT, c'est-a-dire l'energie
    // qui n'est PAS sur un multiple de la fondamentale.
    // /!\ Ma premiere version mesurait « l'energie au-dessus de 22 kHz » a
    // 176 kHz d'echantillonnage. Mais a ce taux-la, une scie a 2 kHz a des
    // harmoniques LEGITIMES jusqu'a 88 kHz : je mesurais le spectre normal de
    // la forme d'onde, pas son defaut. Le repliement, par definition, tombe
    // entre les harmoniques - c'est donc la qu'il faut regarder.
    {
        const double fsT = 44100.0, hz = 5000.0;
        auto inharmonique = [fsT, hz] (const std::vector<double>& y)
        {
            const int n = 16384;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (i + 2048)] * w;
            }
            fft (b);
            double tot = 1e-30, hors = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fsT / (double) n;
                const double e = std::norm (b[(size_t) k]);
                tot += e;
                // a moins de 60 Hz d'un multiple de hz : c'est une harmonique
                const double reste = std::fmod (f + 0.5 * hz, hz) - 0.5 * hz;
                if (std::abs (reste) > 60.0) hors += e;
            }
            return 10.0 * std::log10 (hors / tot);
        };

        std::vector<double> naive (20000);
        { double ph = 0.0;
          for (size_t i = 0; i < naive.size(); ++i)
          { naive[i] = 2.0 * ph - 1.0; ph += hz / fsT; if (ph >= 1.0) ph -= 1.0; } }

        std::vector<double> propre (20000);
        { Oscillateur o; o.prepare (fsT); o.majHauteur (hz);
          for (auto& v : propre) v = o.suivant (FormeOsc::Scie, 0.5); }

        const double iN = inharmonique (naive), iP = inharmonique (propre);
        std::printf ("    scie a 5 kHz (fs 44,1 k) - energie HORS harmoniques :\n");
        std::printf ("      naive %.1f dB    PolyBLEP %.1f dB\n", iN, iP);
        verifie ("PolyBLEP retire bien le repliement", iN - iP, 8.0, 80.0, "dB");
    }

    // --- 2. les quatre formes sont VRAIMENT differentes
    {
        std::printf ("    part d'aigu (au-dessus de 3 kHz) par forme, a 220 Hz :\n");
        double a[4];
        const FormeOsc formes[4] = { FormeOsc::Sinus, FormeOsc::Triangle,
                                     FormeOsc::Impulsion, FormeOsc::Scie };
        const char* noms[4] = { "sinus", "triangle", "impulsion", "scie" };
        for (int i = 0; i < 4; ++i)
        {
            a[i] = partAigu (rend (formes[i], 220.0, 0.5, 40000), fs, 3000.0);
            std::printf ("      %-10s %6.1f dB\n", noms[i], a[i]);
        }
        // l'ordre est une propriete du spectre, pas une convention :
        // sinus < triangle < impulsion(50%) ~ scie
        verifie ("le sinus est le plus pauvre", a[1] - a[0], 3.0, 200.0, "dB");
        verifie ("le triangle est plus pauvre que la scie", a[3] - a[1], 3.0, 200.0, "dB");
    }

    // --- 3. LA LARGEUR D'IMPULSION AGIT (c'est le son des cordes Juno)
    {
        const double a50 = partAigu (rend (FormeOsc::Impulsion, 220.0, 0.50, 40000), fs, 3000.0);
        const double a10 = partAigu (rend (FormeOsc::Impulsion, 220.0, 0.10, 40000), fs, 3000.0);
        std::printf ("    impulsion : 50 %% -> %.1f dB, 10 %% -> %.1f dB\n", a50, a10);
        verifie ("une impulsion etroite est plus brillante", a10 - a50, 2.0, 60.0, "dB");
    }

    // --- 4. LE DESACCORD PRODUIT UN BATTEMENT, pas juste deux notes
    {
        ReglagesVA r;
        r.forme1 = FormeOsc::Scie; r.forme2 = FormeOsc::Scie;
        r.melange = 0.5; r.sub = 0.0; r.bruit = 0.0;

        auto battement = [&] (double cents)
        {
            ReglagesVA rr = r; rr.cents2 = cents;
            EtageVA e; e.prepare (fs);
            e.declenche (rr, 220.0, 0.0);
            double env = 0.0, mini = 1e9, maxi = 0.0;
            const double lis = 1.0 - std::exp (-1.0 / (0.01 * fs));
            const int n = (int) (fs * 1.5);
            for (int i = 0; i < n; ++i)
            {
                env += lis * (std::abs (e.suivant (rr)) - env);
                if (i > n / 4) { mini = std::min (mini, env); maxi = std::max (maxi, env); }
            }
            return 20.0 * std::log10 (maxi / std::max (mini, 1e-12));
        };
        const double b0 = battement (0.0), b7 = battement (7.0);
        std::printf ("    battement de l'unisson : 0 cent %.1f dB, 7 cents %.1f dB\n",
                     b0, b7);
        verifie ("sans desaccord, pas de battement", b0, 0.0, 3.0, "dB");
        verifie ("avec 7 cents, ca bat", b7 - b0, 2.0, 60.0, "dB");
    }

    // --- 5. SUR LE MOTEUR : une couche analogique sonne, et le grain ne
    // s'y applique PAS (un Juno n'a pas de convertisseur).
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        auto joue = [&] (bool grain)
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.couche[0].moteur = TypeMoteur::Analogique;
            p.couche[0].attaque = 0.002; p.couche[0].decli = 0.002;
            p.couche[0].tenue = 1.0;
            p.couche[0].grain.actif = grain;
            p.couche[0].grain.quant = Quant::Lin8;
            p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
            p.instab.actif = false;
            Moteur m; m.prepare (44100.0, 4);
            m.noteOn (p, 57, 100, roms, tl);      // la2 = 220 Hz
            std::vector<double> y;
            for (int i = 0; i < 44100; ++i)
            { double a, b; m.suivant (p, a, b); y.push_back (a); }
            return y;
        };

        const auto sans = joue (false);
        const auto avec = joue (true);
        double rms = 0.0;
        for (double v : sans) rms += v * v;
        rms = std::sqrt (rms / (double) sans.size());

        double ec = 0.0;
        for (size_t i = 0; i < sans.size(); ++i) ec = std::max (ec, std::abs (sans[i] - avec[i]));

        // /!\ CE TEST COMPTAIT LES PASSAGES PAR ZERO. C'est exactement le
        // piege 10, ecrit dans CLAUDE.md deux jours plus tot - cette
        // occurrence-la y avait survecu. Il est passe au rouge le jour ou la
        // velocite a commence a ouvrir le filtre : des harmoniques en plus,
        // donc des passages par zero en plus, donc une « hauteur » annoncee a
        // 237 Hz pour une note qui est restee a 220.
        const double a220 = ampA (sans, 8000, 16384, 220.0, 44100.0);
        const double a233 = ampA (sans, 8000, 16384, 233.0, 44100.0);

        std::printf ("    couche analogique : rms %.3f, a 220 Hz %.4f, "
                     "temoin 233 Hz %.5f, ecart avec grain %.2e\n",
                     rms, a220, a233, ec);
        verifie ("la couche analogique sonne", rms, 0.02, 1.0);
        verifie ("et a la bonne hauteur", a233 / std::max (a220, 1e-9), 0.0, 0.15, "x");
        verifie ("le grain ne s'y applique pas (chaine analogique)", ec, 0.0, 1e-12);
    }
}

// ------------------------------------------- 22. le moteur FM

static void testFM()
{
    std::printf ("\n22. LE MOTEUR FM (quatre operateurs)\n");
    std::printf ("    (400 fichiers BELLS et 397 SYNTH dans le corpus : ce que\n"
                 "     l'additif decrit mal, la FM le decrit avec deux valeurs)\n");

    const double fs = 44100.0;

    auto rend = [fs] (ReglagesFM r, double hz, int n, int saut = 0)
    {
        EtageFM e; e.prepare (fs);
        e.declenche (r, hz, 0.0);
        std::vector<double> y;
        for (int i = 0; i < n + saut; ++i)
        { const double v = e.suivant (r); if (i >= saut) y.push_back (v); }
        return y;
    };

    // part d'energie qui n'est PAS sur un multiple de hz
    auto inharmonique = [fs] (const std::vector<double>& y, double hz)
    {
        const int n = 16384;
        if ((int) y.size() < n) return 0.0;
        std::vector<std::complex<double>> b ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            b[(size_t) i] = y[(size_t) i] * w;
        }
        fft (b);
        double tot = 1e-30, hors = 1e-30;
        for (int k = 1; k <= n / 2; ++k)
        {
            const double f = (double) k * fs / (double) n;
            const double e = std::norm (b[(size_t) k]);
            tot += e;
            const double reste = std::fmod (f + 0.5 * hz, hz) - 0.5 * hz;
            if (std::abs (reste) > 25.0) hors += e;
        }
        return 10.0 * std::log10 (hors / tot);
    };

    auto partAigu2 = [fs] (const std::vector<double>& y, double fCoupe)
    {
        const int n = 16384;
        if ((int) y.size() < n) return -200.0;
        std::vector<std::complex<double>> b ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            b[(size_t) i] = y[(size_t) i] * w;
        }
        fft (b);
        double tot = 1e-30, haut = 1e-30;
        for (int k = 1; k <= n / 2; ++k)
        {
            const double e = std::norm (b[(size_t) k]);
            tot += e;
            if ((double) k * fs / (double) n > fCoupe) haut += e;
        }
        return 10.0 * std::log10 (haut / tot);
    };

    // --- 1. L'INDICE DE MODULATION FAIT LA BRILLANCE. C'est LE geste de la FM.
    {
        std::printf ("    niveau du modulateur -> part d'aigu (au-dessus de 2 kHz) :\n");
        double a[4]; const double niv[4] = { 0.0, 0.2, 0.5, 1.0 };
        for (int i = 0; i < 4; ++i)
        {
            ReglagesFM r;
            r.algo = 2;                       // deux modulateurs sur une porteuse
            r.ratio[0] = 1.0; r.ratio[1] = 2.0; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = niv[i]; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            // pas d'extinction : on isole l'indice de modulation
            for (int k = 0; k < kOperateurs; ++k)
            { r.tenue[k] = 1.0; r.decli[k] = 8.0; r.echelleNiveau[k] = 0.0; }
            a[i] = partAigu2 (rend (r, 220.0, 16384), 2000.0);
            std::printf ("      indice %.1f  ->  %6.1f dB\n", niv[i], a[i]);
        }
        verifie ("a indice nul, la porteuse est un sinus pur", a[0], -200.0, -60.0, "dB");
        verifie ("l'indice ouvre le spectre", a[3] - a[0], 30.0, 200.0, "dB");
        verifie ("et il l'ouvre PROGRESSIVEMENT", a[2] - a[1], 2.0, 60.0, "dB");
    }

    // --- 2. UN RAPPORT NON ENTIER REND LE SON INHARMONIQUE. C'est la cloche.
    {
        auto avecRatio = [&] (double ratioMod)
        {
            ReglagesFM r;
            r.algo = 2;
            r.ratio[0] = 1.0; r.ratio[1] = ratioMod; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = 0.7; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            for (int k = 0; k < kOperateurs; ++k)
            { r.tenue[k] = 1.0; r.decli[k] = 8.0; r.echelleNiveau[k] = 0.0; }
            return inharmonique (rend (r, 220.0, 16384), 220.0);
        };
        const double entier = avecRatio (2.0), casse = avecRatio (2.41);
        std::printf ("    energie hors harmoniques : rapport 2,00 -> %.1f dB, "
                     "2,41 -> %.1f dB\n", entier, casse);
        verifie ("un rapport entier reste harmonique", entier, -200.0, -12.0, "dB");
        verifie ("un rapport non entier ne l'est plus", casse - entier, 6.0, 100.0, "dB");
    }

    // --- 3. LA DECROISSANCE PAR OPERATEUR : le son COMMENCE metallique et
    // FINIT en sinus. Sans elle, la FM n'est qu'un timbre fige de plus.
    {
        ReglagesFM r;
        r.algo = 2;
        r.ratio[0] = 1.0; r.ratio[1] = 3.5; r.ratio[3] = 1.0;
        r.niveau[0] = 1.0; r.niveau[1] = 0.9; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
        // /!\ Chaque operateur a maintenant une ENVELOPPE COMPLETE. Le test
        // reste le meme - le modulateur doit s'eteindre plus vite que la
        // porteuse - mais il s'exprime desormais en secondes, pas en 0..1.
        for (int k = 0; k < kOperateurs; ++k) r.echelleNiveau[k] = 0.0;
        r.decli[0] = 6.0;  r.tenue[0] = 0.9;    // la porteuse tient
        r.decli[1] = 0.35; r.tenue[1] = 0.0;    // le modulateur s'eteint vite
        const auto debut = rend (r, 220.0, 16384, 0);
        const auto fin   = rend (r, 220.0, 16384, 30000);
        const double aD = partAigu2 (debut, 2000.0), aF = partAigu2 (fin, 2000.0);
        std::printf ("    cloche : part d'aigu au debut %.1f dB, a 0,7 s %.1f dB\n",
                     aD, aF);
        // /!\ La borne haute passe de 120 a 200 dB, et ce n'est pas un
        // relachement : avec une vraie enveloppe par operateur, le modulateur
        // atteint reellement ZERO (tenue nulle) au lieu de tendre vers une
        // exponentielle qui traine. L'ecart mesure passe de ~60 a 121 dB.
        // C'est le comportement recherche, pas un debordement.
        verifie ("le son commence metallique et finit en sinus", aD - aF, 8.0, 200.0, "dB");
    }

    // --- 4. LES ALGORITHMES NE SONT PAS DES ETIQUETTES. La chaine profonde
    // doit sonner plus riche que les quatre sinus paralleles.
    {
        auto avecAlgo = [&] (int algo)
        {
            ReglagesFM r;
            r.algo = algo;
            for (int i = 0; i < kOperateurs; ++i)
            { r.ratio[i] = 1.0 + (double) i; r.niveau[i] = 0.8;
              r.tenue[i] = 1.0; r.decli[i] = 8.0; r.echelleNiveau[i] = 0.0; }
            return partAigu2 (rend (r, 220.0, 16384), 3000.0);
        };
        const double chaine = avecAlgo (0), additif = avecAlgo (7);
        std::printf ("    algorithme 1 (chaine) %.1f dB, algorithme 8 (additif) %.1f dB\n",
                     chaine, additif);
        // /!\ La borne haute est large exprès : en algorithme 8 les quatre
        // sinus sont a 220, 440, 660 et 880 Hz, donc il n'y a RIEN au-dessus de
        // 3 kHz - par construction, pas par faiblesse. L'ecart est enorme et
        // c'est normal.
        verifie ("la chaine profonde est bien plus riche", chaine - additif,
                 10.0, 250.0, "dB");
    }

    // --- 5. LA HAUTEUR RESTE JUSTE, meme a indice fort.
    // /!\ C'est la raison d'etre de la modulation de PHASE : en modulation de
    // frequence vraie, l'integrale de la modulante fait deriver la hauteur des
    // qu'elle a une composante continue.
    //
    // /!\ ET ON NE LA MESURE PAS AUX PASSAGES PAR ZERO. Ma premiere version le
    // faisait et annoncait « 220 Hz -> 440 Hz », soit une octave de derive. Le
    // moteur etait juste : un signal riche traverse zero plusieurs fois par
    // periode, donc compter les passages compte les HARMONIQUES, pas la
    // hauteur. C'est la quatrieme fois sur ce projet qu'une mesure naive me
    // fait accuser un mecanisme qui marche.
    {
        auto amplitudes = [&] (double indice, double f)
        {
            ReglagesFM r;
            r.algo = 2;
            r.ratio[0] = 1.0; r.ratio[1] = 1.0; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = indice; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            for (int k = 0; k < kOperateurs; ++k)
            { r.tenue[k] = 1.0; r.decli[k] = 8.0; r.echelleNiveau[k] = 0.0; }
            const auto y = rend (r, 220.0, 16384);
            return ampA (y, 0, 16384, f, fs);
        };
        const double f0sans = amplitudes (0.0, 220.0);
        const double f0avec = amplitudes (1.0, 220.0);
        const double horsSans = amplitudes (1.0, 233.0);   // temoin non harmonique
        std::printf ("    amplitude a 220 Hz : indice 0 -> %.4f, indice 1 -> %.4f\n",
                     f0sans, f0avec);
        std::printf ("    temoin a 233 Hz (non harmonique) : %.5f\n", horsSans);
        verifie ("la fondamentale reste bien a 220 Hz",
                 20.0 * std::log10 (std::max (f0avec, 1e-9) / std::max (f0sans, 1e-9)),
                 -14.0, 6.0, "dB");
        verifie ("et rien n'apparait entre les harmoniques",
                 horsSans / std::max (f0avec, 1e-9), 0.0, 0.05, "x");
    }

    // --- 6. SUR LE MOTEUR : une couche FM sonne et tient sa hauteur.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::FM;
        p.couche[0].attaque = 0.002; p.couche[0].decli = 0.002; p.couche[0].tenue = 1.0;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].fm.algo = 2;
        p.couche[0].fm.ratio[0] = 1.0; p.couche[0].fm.ratio[1] = 2.0;
        p.couche[0].fm.niveau[0] = 1.0; p.couche[0].fm.niveau[1] = 0.5;
        p.couche[0].fm.niveau[2] = 0.0; p.couche[0].fm.niveau[3] = 0.0;
        for (int k = 0; k < kOperateurs; ++k)
        { p.couche[0].fm.tenue[k] = 1.0; p.couche[0].fm.decli[k] = 8.0;
          p.couche[0].fm.echelleNiveau[k] = 0.0; }
        p.instab.actif = false;

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 57, 100, roms, tl);      // la2 = 220 Hz
        std::vector<double> y;
        for (int i = 0; i < 44100; ++i)
        { double a, b; m.suivant (p, a, b); y.push_back (a); }

        double rms = 0.0; for (double v : y) rms += v * v;
        rms = std::sqrt (rms / (double) y.size());
        int pz = 0; double prec = 0.0;
        for (size_t i = 4410; i < y.size(); ++i)
        { if (prec <= 0.0 && y[i] > 0.0) ++pz; prec = y[i]; }
        const double hz = (double) pz * 44100.0 / (double) (y.size() - 4410);
        std::printf ("    couche FM : rms %.3f, hauteur %.0f Hz (la2 = 220)\n", rms, hz);
        verifie ("la couche FM sonne", rms, 0.02, 1.0);
        verifie ("et a la bonne hauteur", hz, 205.0, 235.0, "Hz");
    }

    // --- 6. LE SUIVI DE CLAVIER EN NIVEAU : le vrai secret du DX7.
    //
    // /!\ On compare a HAUTEUR EGALE, suivi actif contre suivi nul. Comparer
    // deux hauteurs differentes ne prouverait rien : l'aigu d'un son aigu est
    // plus fort de toute facon, et la mesure attraperait la transposition au
    // lieu du suivi.
    {
        auto brillance = [&] (double suivi, double hz)
        {
            ReglagesFM r;
            r.algo = 2;
            r.ratio[0] = 1.0; r.ratio[1] = 3.0; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = 0.9; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            for (int k = 0; k < kOperateurs; ++k)
            { r.tenue[k] = 1.0; r.decli[k] = 8.0; r.echelleNiveau[k] = 0.0; }
            r.pointBrisure = 45.0;            // la1 = 110 Hz
            r.echelleNiveau[1] = suivi;       // seul le MODULATEUR suit
            return partAigu2 (rend (r, hz, 16384), 4.0 * hz);
        };
        const double sans = brillance (0.0, 880.0);   // trois octaves plus haut
        const double avec = brillance (0.6, 880.0);
        const double bas  = brillance (0.6, 110.0);   // pile sur la brisure
        const double basSans = brillance (0.0, 110.0);
        std::printf ("    au do6 : sans suivi %.1f dB d'aigu, avec suivi %.1f dB\n", sans, avec);
        std::printf ("    a la brisure : %.1f dB contre %.1f dB (doit etre identique)\n",
                     basSans, bas);
        verifie ("le suivi de clavier adoucit le modulateur dans l'aigu",
                 sans - avec, 3.0, 60.0, "dB");
        // ⭐ Et il ne fait RIEN au point de brisure : c'est ce qui le rend
        // reglable. Un suivi qui deplacerait aussi la note de reference serait
        // un simple attenuateur deguise.
        verifie ("et ne touche a rien au point de brisure",
                 std::abs (basSans - bas), 0.0, 0.6, "dB");
    }

    // --- 7. LE SUIVI DE CLAVIER EN VITESSE : les enveloppes accelerent dans
    // l'aigu. Meme protocole - hauteur constante, suivi contre pas de suivi.
    {
        auto restant = [&] (double suivi)
        {
            ReglagesFM r;
            r.algo = 2;
            r.ratio[0] = 1.0; r.ratio[1] = 3.5; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = 0.9; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            for (int k = 0; k < kOperateurs; ++k) r.echelleNiveau[k] = 0.0;
            r.decli[0] = 8.0;  r.tenue[0] = 1.0;    // la porteuse ne bouge pas
            r.decli[1] = 0.50; r.tenue[1] = 0.0;    // le modulateur s'eteint
            r.pointBrisure = 45.0;
            r.echelleVitesse = suivi;
            // fenetre prise APRES le debut : c'est la chute qu'on mesure
            return partAigu2 (rend (r, 880.0, 16384, 8000), 3000.0);
        };
        const double lent = restant (0.0), vite = restant (1.0);
        std::printf ("    a 0,18 s sur un do6 : sans suivi %.1f dB, avec suivi %.1f dB\n",
                     lent, vite);
        verifie ("les enveloppes accelerent dans l'aigu", lent - vite, 3.0, 200.0, "dB");
    }
}

// ------------------------------------------- 23. delai, saturation, tonalite

static void testEffets2()
{
    std::printf ("\n23. DELAI, SATURATION, ENSEMBLE, TONALITE\n");
    std::printf ("    (/!\\ chaque effet doit faire ce que les autres ne font pas :\n"
                 "     on a deja DEUX controles qui assombrissent)\n");

    const double fs = 44100.0;

    // --- 1. LE DELAI S'ASSOMBRIT A CHAQUE REPETITION.
    // /!\ L'amortissement est DANS la boucle. Place sur la sortie, il donnerait
    // cinq repetitions egales et ternes au lieu d'une claire puis d'une sourde.
    {
        Delai dl; dl.prepare (fs);
        dl.regle (0.10, 0.75, 1.0, 0.6, 0.0);      // 100 ms, forte reinjection
        std::vector<double> y (44100);
        for (int i = 0; i < 44100; ++i)
        {
            double g = (i < 8) ? 1.0 : 0.0, d = g;
            dl.traite (g, d);
            y[(size_t) i] = g;
        }
        auto aigu = [&] (int deb)
        {
            const int n = 2048;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (deb + i)] * w;
            }
            fft (b);
            double tot = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            { const double e = std::norm (b[(size_t) k]); tot += e;
              if ((double) k * fs / (double) n > 2000.0) haut += e; }
            return 10.0 * std::log10 (haut / tot);
        };
        const int t1 = (int) (0.10 * fs), t4 = (int) (0.40 * fs);
        const double a1 = aigu (t1), a4 = aigu (t4);
        std::printf ("    part d'aigu : 1re repetition %.1f dB, 4e %.1f dB\n", a1, a4);
        verifie ("le delai s'assombrit a chaque tour", a1 - a4, 3.0, 60.0, "dB");

        // et il ne diverge pas
        double tard = 0.0;
        for (int i = 40000; i < 44100; ++i) tard = std::max (tard, std::abs (y[(size_t) i]));
        verifie ("et il finit par s'eteindre", tard, 0.0, 0.35);
    }

    // --- 2. LA SATURATION ASYMETRIQUE PRODUIT H2, la symetrique non.
    // ⭐ C'est TOUT le sujet : une saturation symetrique n'a que des harmoniques
    // impaires et sonne dure. H2 est ce qu'on entend comme de la chaleur.
    {
        auto harmoniques = [fs] (double asym, double f, int rang)
        {
            Saturation sa; sa.prepare (fs);
            sa.regle (0.7, asym);
            const int n = 32768;
            std::vector<double> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                double g = 0.5 * std::sin (2.0 * kPi * f * (double) i / fs), d = g;
                sa.traite (g, d);
                y[(size_t) i] = g;
            }
            return ampA (y, 8192, 16384, f * (double) rang, fs);
        };
        const double f = 220.0;
        const double h2sym = harmoniques (0.0, f, 2), h3sym = harmoniques (0.0, f, 3);
        const double h2asy = harmoniques (1.0, f, 2), h3asy = harmoniques (1.0, f, 3);
        std::printf ("    symetrique  : H2 %.5f, H3 %.5f\n", h2sym, h3sym);
        std::printf ("    asymetrique : H2 %.5f, H3 %.5f\n", h2asy, h3asy);
        verifie ("symetrique : pas de H2", h2sym / std::max (h3sym, 1e-9), 0.0, 0.10, "x");
        verifie ("asymetrique : H2 apparait", h2asy / std::max (h2sym, 1e-9),
                 5.0, 1e9, "x");
    }

    // --- 3. LA SATURATION NE DOIT PAS SEULEMENT MONTER LE VOLUME.
    // /!\ C'est le biais le plus courant en ecoute d'effets : plus fort passe
    // pour meilleur. Sans compensation on ne saurait pas ce qu'on entend.
    {
        auto niveau = [fs] (double dose)
        {
            Saturation sa; sa.prepare (fs);
            sa.regle (dose, 0.5);
            double e = 0.0; const int n = 20000;
            for (int i = 0; i < n; ++i)
            {
                double g = 0.25 * std::sin (2.0 * kPi * 220.0 * (double) i / fs), d = g;
                sa.traite (g, d);
                if (i > 4000) e += g * g;
            }
            return 10.0 * std::log10 (e / (double) (n - 4000) + 1e-30);
        };
        const double n0 = niveau (0.0), n1 = niveau (1.0);
        std::printf ("    niveau : dose 0 %.2f dB, dose 1 %.2f dB\n", n0, n1);
        verifie ("le gain est compense", std::abs (n1 - n0), 0.0, 6.0, "dB");
    }

    // --- 4. LA TONALITE MONTE AUSSI BIEN QU'ELLE DESCEND, et son pivot tient.
    // ⭐ C'est ce qui la distingue des deux autres controles de couleur du
    // plugin : la bande et la couleur du chorus ne savent que RETIRER.
    {
        auto rep = [fs] (double gr, double ai, double f)
        {
            Tonalite t; t.prepare (fs);
            t.regle (gr, ai);
            const int n = 32768;
            std::vector<double> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                double g = std::sin (2.0 * kPi * f * (double) i / fs), d = g;
                t.traite (g, d);
                y[(size_t) i] = g;
            }
            return 20.0 * std::log10 (std::max (ampA (y, 8192, 16384, f, fs), 1e-9));
        };
        const double ref100 = rep (0, 0, 100.0), ref1000 = rep (0, 0, 1000.0),
                     ref8000 = rep (0, 0, 8000.0);
        std::printf ("    +9 dB de grave : 100 Hz %+.1f dB, 1 kHz %+.1f dB, "
                     "8 kHz %+.1f dB\n",
                     rep (9, 0, 100.0) - ref100, rep (9, 0, 1000.0) - ref1000,
                     rep (9, 0, 8000.0) - ref8000);
        verifie ("le grave MONTE vraiment", rep (9, 0, 100.0) - ref100, 4.0, 12.0, "dB");
        verifie ("l'aigu monte aussi", rep (0, 9, 8000.0) - ref8000, 4.0, 12.0, "dB");
        verifie ("le grave descend", rep (-9, 0, 100.0) - ref100, -14.0, -3.0, "dB");
        // ⭐ LA VRAIE PROPRIETE D'UN BAXANDALL : LES BANDES SONT SEPAREES.
        // /!\ Ma premiere version affirmait « le medium ne bouge jamais » et
        // echouait a 2,45 dB. Elle avait tort, pas le filtre : un vrai
        // Baxandall avec ses DEUX commandes a fond souleve bien le medium -
        // c'est un sourire, pas un pivot absolu. Ce qui le caracterise, c'est
        // qu'une commande n'empiete pas sur le domaine de l'autre.
        std::printf ("    separation : +9 de grave a 8 kHz %+.2f dB, "
                     "+9 d'aigu a 100 Hz %+.2f dB\n",
                     rep (9, 0, 8000.0) - ref8000, rep (0, 9, 100.0) - ref100);
        verifie ("le grave n'empiete pas sur l'aigu",
                 std::abs (rep (9, 0, 8000.0) - ref8000), 0.0, 1.0, "dB");
        verifie ("l'aigu n'empiete pas sur le grave",
                 std::abs (rep (0, 9, 100.0) - ref100), 0.0, 1.5, "dB");
        verifie ("et les deux ensemble font un sourire, pas une bosse",
                 rep (9, 9, 1000.0) - ref1000, 0.0, 4.0, "dB");
    }

    // --- 5. L'ENSEMBLE N'EST PAS UN CHORUS PLUS PROFOND.
    // /!\ Ses trois taux sont irrationnels ENTRE EUX : le motif ne se referme
    // jamais. Un chorus a trois taux voisins, lui, se reconnait.
    {
        auto autoc = [fs] (bool ens)
        {
            std::vector<double> y;
            Ensemble e; e.prepare (fs);
            Chorus c; c.prepare (fs);
            e.regle (0.6, 0.8, 1.0);
            c.regleNature (false);
            c.regle (0.6, 0.8, 1.0, 0.0);
            unsigned int se = 5u;
            for (int i = 0; i < (int) (fs * 12); ++i)
            {
                se = se * 1664525u + 1013904223u;
                double g = ((double) (se >> 8) / 8388608.0) - 1.0, d = g;
                if (ens) e.traite (g, d); else c.traite (g, d);
                y.push_back (g);
            }
            // enveloppe lissee, puis autocorrelation a la periode nominale
            std::vector<double> env;
            double v = 0.0;
            const double lis = 1.0 - std::exp (-1.0 / (0.03 * fs));
            for (double x : y) { v += lis * (std::abs (x) - v); env.push_back (v); }
            double moy = 0.0;
            for (size_t i = env.size() / 4; i < env.size(); ++i) moy += env[i];
            moy /= (double) (env.size() - env.size() / 4);
            const int lag = (int) (fs / 0.6);
            double num = 0.0, den = 1e-30;
            for (size_t i = env.size() / 4; i + (size_t) lag < env.size(); ++i)
            { num += (env[i] - moy) * (env[i + (size_t) lag] - moy);
              den += (env[i] - moy) * (env[i] - moy); }
            return num / den;
        };
        const double aC = autoc (false), aE = autoc (true);
        std::printf ("    autocorrelation a la periode : chorus %.3f, ensemble %.3f\n",
                     aC, aE);
        verifie ("l'ensemble ne se referme pas comme un chorus",
                 std::abs (aC) - std::abs (aE), -0.05, 2.0);
    }
}

// ------------------------------------------- 24. la table d'ondes

static void testTable()
{
    std::printf ("\n24. LE MOTEUR TABLE D'ONDES\n");
    std::printf ("    (ce qu'il sait faire et que les autres ne savent pas :\n"
                 "     BALAYER - passer d'une image a l'autre en tenant la note)\n");

    const double fs = 44100.0;

    auto rend = [fs] (ReglagesTable r, double hz, int n, double balayage = 0.0)
    {
        EtageTable e; e.prepare (fs);
        e.declenche (r, hz, 0.0);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double u = balayage * (double) i / (double) n;
            y[(size_t) i] = e.suivant (r, u);
        }
        return y;
    };

    auto aigu = [fs] (const std::vector<double>& y, double fCoupe, int deb = 4096)
    {
        const int n = 16384;
        if ((int) y.size() < n + deb) return -200.0;
        std::vector<std::complex<double>> b ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
            b[(size_t) i] = y[(size_t) (i + deb)] * w;
        }
        fft (b);
        double tot = 1e-30, haut = 1e-30;
        for (int k = 1; k <= n / 2; ++k)
        { const double e = std::norm (b[(size_t) k]); tot += e;
          if ((double) k * fs / (double) n > fCoupe) haut += e; }
        return 10.0 * std::log10 (haut / tot);
    };

    // --- 1. LA POSITION CHANGE LE SPECTRE. C'est la base.
    {
        std::printf ("    famille Harmoniques - part d'aigu selon la position :\n");
        double a[3]; const double pos[3] = { 0.0, 0.5, 1.0 };
        for (int i = 0; i < 3; ++i)
        {
            ReglagesTable r; r.position = pos[i];
            a[i] = aigu (rend (r, 220.0, 24000), 2000.0);
            std::printf ("      position %.1f  ->  %6.1f dB\n", pos[i], a[i]);
        }
        verifie ("a 0, c'est presque un sinus", a[0], -200.0, -50.0, "dB");
        verifie ("la position enrichit le spectre", a[2] - a[0], 25.0, 200.0, "dB");
        verifie ("et progressivement", a[1] - a[0], 8.0, 200.0, "dB");
    }

    // --- 2. LE BALAYAGE : le spectre EVOLUE pendant que la note tient.
    // ⭐ C'est le geste que ni la ROM, ni l'analogique, ni la FM ne font.
    {
        // /!\ Le balayage doit etre LONG par rapport a la fenetre d'analyse.
        // Avec un balayage d'une seconde et une FFT de 16 384 points, la
        // « premiere » fenetre couvrait deja 37 % du parcours : je mesurais
        // le milieu du balayage contre sa fin, pas son debut contre sa fin.
        ReglagesTable r; r.position = 0.0;
        const auto y = rend (r, 220.0, 88200, 1.0);       // balaye 0 -> 1 en 2 s
        const double debut = aigu (y, 2000.0, 512);
        const double fin   = aigu (y, 2000.0, 70000);
        std::printf ("    balayage : part d'aigu au debut %.1f dB, a la fin %.1f dB\n",
                     debut, fin);
        verifie ("le spectre evolue pendant la note", fin - debut, 10.0, 200.0, "dB");
    }

    // --- 3. LES QUATRE FAMILLES SONT DIFFERENTES, et pas seulement nommees.
    {
        auto spectreA = [&] (FamilleTable f, int harm)
        {
            ReglagesTable r; r.famille = f; r.position = 0.6;
            const auto y = rend (r, 220.0, 24000);
            return ampA (y, 4096, 16384, 220.0 * (double) harm, fs);
        };
        // les IMPAIRES doivent avoir un H2 negligeable, les HARMONIQUES non
        const double h2harm = spectreA (FamilleTable::Harmoniques, 2);
        const double h2imp  = spectreA (FamilleTable::Impaires, 2);
        const double h3imp  = spectreA (FamilleTable::Impaires, 3);
        std::printf ("    H2 : harmoniques %.4f, impaires %.5f (H3 impaires %.4f)\n",
                     h2harm, h2imp, h3imp);
        verifie ("la famille impaire n'a pas de H2",
                 h2imp / std::max (h3imp, 1e-9), 0.0, 0.10, "x");
        verifie ("alors que l'harmonique en a", h2harm / std::max (h2imp, 1e-9),
                 5.0, 1e9, "x");

        // les FORMANTS ont une bosse qui MONTE quand on balaye : c'est le
        // transwave, et rien d'autre ne fait ca
        auto bosse = [&] (double u)
        {
            ReglagesTable r; r.famille = FamilleTable::Formants; r.position = u;
            const auto y = rend (r, 220.0, 24000);
            double meilleur = 0.0; int rang = 1;
            for (int k = 1; k <= 20; ++k)
            {
                const double a = ampA (y, 4096, 16384, 220.0 * (double) k, fs);
                if (a > meilleur) { meilleur = a; rang = k; }
            }
            return rang;
        };
        const int r0 = bosse (0.1), r1 = bosse (0.9);
        std::printf ("    formants : harmonique dominante %d -> %d\n", r0, r1);
        verifie ("la bosse de formant monte avec la position",
                 (double) (r1 - r0), 3.0, 24.0, "rangs");
    }

    // --- 4. SAUT contre FONDU : deux lectures, deux sons.
    // /!\ Le PPG SAUTE d'une image a l'autre. Ces crans sont sa signature.
    // On les mesure comme une discontinuite : le pas maximal du signal pendant
    // un balayage lent.
    {
        auto pasMax = [&] (LectureTable mode)
        {
            ReglagesTable r; r.lecture = mode; r.position = 0.0;
            const auto y = rend (r, 110.0, 44100, 1.0);
            double m = 0.0, moy = 0.0;
            for (size_t i = 1; i < y.size(); ++i)
            { const double d = std::abs (y[i] - y[i - 1]); m = std::max (m, d); moy += d; }
            moy /= (double) (y.size() - 1);
            return m / std::max (moy, 1e-12);
        };
        const double f = pasMax (LectureTable::Fondu), sa = pasMax (LectureTable::Saut);
        std::printf ("    pas maximal / pas moyen : fondu %.1f, saut %.1f\n", f, sa);
        verifie ("le saut cree bien des discontinuites", sa - f, 0.5, 200.0, "x");
    }

    // --- 5. BANDE LIMITEE SELON LA NOTE.
    // /!\ Une table brute contient des harmoniques jusqu'a la moitie de sa
    // longueur : lue a 2 kHz elle replierait tout. On mesure l'energie hors
    // harmoniques, comme pour l'oscillateur analogique.
    {
        auto inharm = [fs] (const std::vector<double>& y, double hz)
        {
            const int n = 16384;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (i + 2048)] * w;
            }
            fft (b);
            double tot = 1e-30, hors = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double fr = (double) k * fs / (double) n;
                const double e = std::norm (b[(size_t) k]);
                tot += e;
                const double reste = std::fmod (fr + 0.5 * hz, hz) - 0.5 * hz;
                if (std::abs (reste) > 60.0) hors += e;
            }
            return 10.0 * std::log10 (hors / tot);
        };
        ReglagesTable r; r.position = 1.0;      // le plus riche
        const double i440 = inharm (rend (r, 440.0, 24000), 440.0);
        const double i3000 = inharm (rend (r, 3000.0, 24000), 3000.0);
        std::printf ("    energie hors harmoniques : a 440 Hz %.1f dB, "
                     "a 3 kHz %.1f dB\n", i440, i3000);
        verifie ("pas de repliement dans le grave", i440, -200.0, -15.0, "dB");
        verifie ("ni dans l'aigu (la table s'appauvrit avec la note)",
                 i3000, -200.0, -15.0, "dB");
    }

    // --- 6. SUR LE MOTEUR : une couche table sonne et tient sa hauteur.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::Table;
        p.couche[0].attaque = 0.002; p.couche[0].decli = 0.002; p.couche[0].tenue = 1.0;
        p.couche[0].desaccordCents = 0.0; p.couche[0].largeur = 0.0;
        p.couche[0].table.position = 0.7;
        p.instab.actif = false;

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 57, 100, roms, tl);
        std::vector<double> y;
        for (int i = 0; i < 44100; ++i)
        { double a, b; m.suivant (p, a, b); y.push_back (a); }

        double rms = 0.0; for (double v : y) rms += v * v;
        rms = std::sqrt (rms / (double) y.size());
        const double f0 = ampA (y, 8000, 16384, 220.0, 44100.0);
        const double hors = ampA (y, 8000, 16384, 233.0, 44100.0);
        std::printf ("    couche table : rms %.3f, a 220 Hz %.4f, "
                     "temoin 233 Hz %.5f\n", rms, f0, hors);
        verifie ("la couche table sonne", rms, 0.02, 1.0);
        verifie ("et a la bonne hauteur", hors / std::max (f0, 1e-9), 0.0, 0.10, "x");
    }
}

// ------------------------------------- 25. le profil de resynthese

static void testProfil()
{
    std::printf ("\n25. LE PROFIL DE RESYNTHESE\n");
    std::printf ("    (la recherche ne cherche plus un MATERIAU, elle cherche\n"
                 "     un PATCH : moteur compris)\n");

    const double fs = 44100.0;

    // --- 1. LE PROFIL DECRIT BIEN QUATRE MOTEURS DIFFERENTS.
    // /!\ Si deux moteurs rendaient la meme chose, le vote ci-dessous serait
    // un tirage au sort qu'on prendrait pour une decision.
    {
        std::vector<double> y[4];
        for (int m = 0; m < 4; ++m)
        {
            uint8_t o[kOctetsProfil];
            profilNeutre (o);
            o[kOffMoteur] = (uint8_t) (m * 64);
            rendProfil (o, y[m], 24000, fs, 220.0);
            double rms = 0.0;
            for (double v : y[m]) rms += v * v;
            rms = std::sqrt (rms / (double) y[m].size());
            std::printf ("    moteur %d : rms %.3f\n", m, rms);
            verifie ("le moteur sonne", rms, 0.02, 1.0);
        }
        // deux a deux, les empreintes doivent etre distinctes
        double leMoinsDistinct = 1e9;
        for (int a = 0; a < 4; ++a)
            for (int b = a + 1; b < 4; ++b)
                leMoinsDistinct = std::min (leMoinsDistinct,
                    perte (empreinte (y[a], fs, kPlancherPropre),
                           empreinte (y[b], fs, kPlancherPropre)));
        std::printf ("    distance la plus faible entre deux moteurs : %.2f dB\n",
                     leMoinsDistinct);
        verifie ("aucun moteur n'est le doublon d'un autre",
                 leMoinsDistinct, 1.0, 1e6, "dB");
    }

    // --- 2. LES OCTETS MUTABLES DEPENDENT DU MOTEUR.
    // ⭐ C'est ce qui empeche de depenser le budget sur des octets inaudibles.
    {
        auto compte = [] (TypeMoteur m)
        {
            int n = 0;
            for (int i = 0; i < kOctetsProfil; ++i)
                if (octetProfilMutable (i, m)) ++n;
            return n;
        };
        const int nRom = compte (TypeMoteur::Rom);
        const int nVA  = compte (TypeMoteur::Analogique);
        const int nFM  = compte (TypeMoteur::FM);
        std::printf ("    octets mutables : ROM %d, analogique %d, FM %d\n",
                     nRom, nVA, nFM);
        // /!\ Le rapport a BAISSE (4,9 -> 2,7) le jour ou les modulations et
        // les effets sont entres dans le profil : ces octets-la sont cherches
        // pour TOUS les moteurs, donc ils comptent dans les deux termes. C'est
        // attendu, et c'est le prix de ce que l'utilisateur a demande.
        verifie ("l'analogique cherche dans un espace bien plus petit",
                 (double) nRom / (double) nVA, 2.0, 100.0, "x");
        // /!\ Et il a baisse une seconde fois (1,8 -> 1,64) en ajoutant le
        // DESACCORD et la SENSIBILITE A LA VITESSE par operateur : huit octets
        // de plus, tous propres a la FM. C'est le prix assume de deux reglages
        // qui portent la moitie du son d'un DX7 - pas un relachement de la
        // regle de parcimonie, qui reste verifiee (la FM cherche toujours dans
        // un espace plus petit que la ROM).
        verifie ("et la FM aussi", (double) nRom / (double) nFM, 1.5, 100.0, "x");
        // et jamais les octets d'un autre moteur
        verifie ("aucun octet FM n'est mute en analogique",
                 octetProfilMutable (kOffFM + 3, TypeMoteur::Analogique) ? 1.0 : 0.0,
                 0.0, 0.0);
        verifie ("aucun octet de recette n'est mute hors ROM",
                 octetProfilMutable (40, TypeMoteur::FM) ? 1.0 : 0.0, 0.0, 0.0);
    }

    // --- 3. ⭐ LE VOTE DE MOTEUR RETROUVE LE BON GENERATEUR.
    // On fabrique une cible AVEC un moteur connu, on la donne a la recherche,
    // et on regarde quel moteur elle choisit. C'est la seule facon de savoir
    // si le vote decide ou s'il suit son amorce.
    {
        auto essaie = [&] (int moteurVrai, const char* nom)
        {
            uint8_t vrai[kOctetsProfil];
            profilNeutre (vrai);
            vrai[kOffMoteur] = (uint8_t) (moteurVrai * 64);
            if (moteurVrai == 1) { vrai[kOffVA + 0] = 0; vrai[kOffVA + 6] = 100; }
            if (moteurVrai == 2) { vrai[kOffFM + 1] = 60; }
            if (moteurVrai == 3) { vrai[kOffTable + 2] = 200; }

            std::vector<double> cible;
            rendProfil (vrai, cible, 40000, fs, 220.0);

            ReglagesRecherche reg;
            reg.evaluations = 60;          // le vote a lieu AVANT la recherche
            reg.fenetreVariable = false;
            reg.nEchComparaison = 24000;
            RapportAnalyse rap;
            const auto cfg = grainTransparent (fs);
            const auto st = resynthetise (cible, fs, cfg, reg, rap,
                                          [] (const EtatRecherche&) { return true; });
            const int trouve = (int) coucheDepuisProfil (st.profil).moteur;
            std::printf ("    cible %-12s -> moteur retenu %d (attendu %d)\n",
                         nom, trouve, moteurVrai);
            return trouve == moteurVrai ? 1.0 : 0.0;
        };

        double bons = 0.0;
        bons += essaie (1, "analogique");
        bons += essaie (2, "FM");
        bons += essaie (3, "table");
        // /!\ On n'exige pas 3/3 : deux moteurs peuvent produire un son proche
        // (une table en position 0 EST un sinus, comme une FM sans modulation),
        // et dans ce cas se tromper de nom ne s'entend pas. Ce qu'on exige,
        // c'est que le vote ne soit pas du hasard - le hasard donnerait 0,75.
        std::printf ("    moteurs correctement retrouves : %.0f / 3\n", bons);
        verifie ("le vote de moteur retrouve le generateur de la cible",
                 bons, 2.0, 3.0, "/3");
    }

    // --- 4. LE PROFIL SURVIT A L'ALLER-RETOUR PAR LA COUCHE.
    // /!\ Un profil qu'on ne saurait pas relire ne serait pas un patch, ce
    // serait un rendu - et « poser » ne rendrait pas le meme son.
    {
        uint8_t o[kOctetsProfil];
        profilNeutre (o);
        o[kOffMoteur] = 64;                       // analogique
        o[kOffFiltre + 1] = 90; o[kOffFiltre + 2] = 120;
        const auto r = coucheDepuisProfil (o);
        std::printf ("    relecture : moteur %d, coupure x%.2f, reso %.2f, "
                     "attaque %.3f s\n", (int) r.moteur, r.coupureRel, r.reso,
                     r.attaque);
        verifie ("le moteur relu est celui ecrit",
                 (r.moteur == TypeMoteur::Analogique) ? 1.0 : 0.0, 1.0, 1.0);
        verifie ("la coupure est dans la plage du parametre", r.coupureRel, 0.5, 40.0, "x");
        verifie ("la resonance reste stable", r.reso, 0.0, 0.95);
    }
}

// ------------------------------------ 26. la chaine d'effets ordonnable

static void testChaineFx()
{
    std::printf ("\n26. LA CHAINE D'EFFETS EST ORDONNABLE\n");

    // --- 1. LES DOUBLONS SONT ECARTES.
    // /!\ Pas par proprete : une reverbe traversee deux fois reinjecte sa
    // propre queue dans son propre reseau, et diverge.
    {
        int c[kEmplacementsFx];
        const int a[kEmplacementsFx] = { 4, 4, 4, 1, 4, 1 };
        const int n = chaineFx (a, kEmplacementsFx, c);
        std::printf ("    { 4 4 4 1 4 1 } -> %d etage(s) :", n);
        for (int i = 0; i < n; ++i) std::printf (" %d", c[i]);
        std::printf ("\n");
        verifie ("un effet repete n'est traverse qu'une fois", (double) n, 2.0, 2.0);
        verifie ("et c'est la PREMIERE occurrence qui compte", (double) c[0], 4.0, 4.0);

        const int vide[kEmplacementsFx] = { 0, 0, 0, 0, 0, 0 };
        verifie ("une chaine toute vide ne traverse rien",
                 (double) chaineFx (vide, kEmplacementsFx, c), 0.0, 0.0);

        const int tout[kEmplacementsFx] = { 6, 5, 4, 3, 2, 1 };
        const int m = chaineFx (tout, kEmplacementsFx, c);
        verifie ("une chaine complete inversee garde ses six etages",
                 (double) m, 6.0, 6.0);
        verifie ("dans l'ordre demande", (double) c[0], 6.0, 6.0);
    }

    // --- 2. ⭐ L'ORDRE S'ENTEND. Sans ca, le reglage serait decoratif.
    // On compare saturation->reverbe et reverbe->saturation sur le meme signal.
    // /!\ Le premier ordre sature un signal sec, le second sature une queue de
    // reverbe : ce sont deux sons, pas deux dosages.
    {
        const double fs = 44100.0;
        const int n = 40000;
        std::vector<double> x ((size_t) n, 0.0);
        for (int i = 0; i < 3000; ++i)
        {
            const double env = 1.0 - (double) i / 3000.0;
            x[(size_t) i] = 0.7 * env * std::sin (2.0 * kPi * 180.0 * (double) i / fs);
        }

        auto passe = [&] (bool satAvant)
        {
            Saturation sat; sat.prepare (fs); sat.regle (0.85, 0.5);
            Reverbe rev;    rev.prepare (fs); rev.regle (0.6, 0.4, 0.5, 0.02);
            std::vector<double> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                double g = x[(size_t) i], d = g;
                if (satAvant) { sat.traite (g, d); rev.traite (g, d); }
                else          { rev.traite (g, d); sat.traite (g, d); }
                y[(size_t) i] = 0.5 * (g + d);
            }
            return y;
        };

        const auto a = passe (true), b = passe (false);
        const double dist = perte (empreinte (a, fs, kPlancherPropre),
                                   empreinte (b, fs, kPlancherPropre));

        // et la ou ca se voit le mieux : la QUEUE, apres l'extinction du signal
        auto rmsQueue = [&] (const std::vector<double>& y)
        {
            double r = 0.0;
            for (size_t i = 12000; i < y.size(); ++i) r += y[i] * y[i];
            return std::sqrt (r / (double) (y.size() - 12000));
        };
        const double qa = rmsQueue (a), qb = rmsQueue (b);
        std::printf ("    distance entre les deux ordres : %.2f dB\n", dist);
        std::printf ("    queue : saturation avant %.5f, saturation apres %.5f\n",
                     qa, qb);
        verifie ("l'ordre des effets s'entend", dist, 1.0, 1e6, "dB");
        verifie ("saturer APRES la reverbe change bien la queue",
                 std::abs (qa - qb) / std::max (qa, 1e-9), 0.05, 100.0, "x");
    }
}

// ------------------------ 27. modulations et effets DANS le profil cherche

static void testProfilModFx()
{
    std::printf ("\n27. LE PROFIL PORTE AUSSI LES MODULATIONS ET LES EFFETS\n");
    std::printf ("    (« il devrait utiliser tout le plugin » - jusqu'ici la\n"
                 "     recherche devait imiter un vibrato avec le materiau)\n");

    const double fs = 44100.0;
    const int n = 44100;

    auto rend = [&] (const uint8_t* o)
    {
        std::vector<double> y;
        rendProfil (o, y, n, fs, 220.0);
        return y;
    };

    // --- 1. UNE MODULATION CABLEE S'ENTEND.
    // /!\ On la mesure sur la HAUTEUR, pas sur une part d'aigu : un LFO cable
    // sur la hauteur etale l'energie de la fondamentale sur ses voisines. Voir
    // le piege 10 - on compare des amplitudes a des frequences precises.
    {
        uint8_t sec[kOctetsProfil], vib[kOctetsProfil];
        profilNeutre (sec);
        sec[kOffMoteur] = 64;                     // analogique : hauteur nette
        for (int i = 0; i < kOctetsProfil; ++i) vib[i] = sec[i];

        vib[kOffMod + 0] = 64;                    // emplacement 1 = LFO
        vib[kOffMod + 1] = 227;                   // ~6 Hz
        vib[kOffMod + 2] = 0;                     // forme sinus
        // /!\ ON CALCULE L'OCTET, on ne l'ecrit pas en dur. La premiere version
        // posait 40, qui valait « hauteur » quand il y avait huit destinations
        // et vaut « pan » depuis qu'il y en a 38 : ce test est passe de vert a
        // rouge sans qu'une seule ligne de modulation ait change. Un nombre
        // magique qui depend d'une constante ailleurs est un test qui ment.
        vib[kOffCable + 0] = (uint8_t) (((double) (int) Dest::Hauteur + 0.5)
                                        * 256.0 / (double) kNbDestinations);
        // /!\ DOSE MODESTE, ET C'EST LE POINT. La destination hauteur porte
        // 24 DEMI-TONS a pleine dose : une dose « franche » (0,65) donne une
        // sirene de deux octaves ou la fondamentale n'existe plus nulle part.
        // Le premier jet de ce test mesurait 0 a 220 Hz ET 0 a 226 - il ne
        // montrait pas que la modulation manquait, il montrait qu'elle etait
        // enorme. Ici : un demi-ton, un vrai vibrato.
        vib[kOffCable + 1] = 130;

        const auto a = rend (sec), b = rend (vib);
        const double f0a = ampA (a, 8000, 16384, 220.0, fs);
        const double f0b = ampA (b, 8000, 16384, 220.0, fs);
        // une bande laterale a +/- la vitesse du LFO : c'est la signature
        const double cotea = ampA (a, 8000, 16384, 227.0, fs);
        const double coteb = ampA (b, 8000, 16384, 227.0, fs);
        std::printf ("    a 220 Hz : sec %.4f, vibre %.4f\n", f0a, f0b);
        std::printf ("    a 227 Hz : sec %.5f, vibre %.5f\n", cotea, coteb);
        verifie ("le vibrato etale la fondamentale",
                 f0b / std::max (f0a, 1e-9), 0.0, 0.90, "x");
        verifie ("et remplit ses cotes",
                 coteb / std::max (cotea, 1e-9), 1.5, 1e9, "x");
    }

    // --- 2. UN EFFET DU PROFIL S'ENTEND.
    // On prend la reverbe : sa signature est une QUEUE, donc on mesure ce qui
    // reste apres une note courte.
    {
        uint8_t sec[kOctetsProfil], mouille[kOctetsProfil];
        profilNeutre (sec);
        sec[kOffEnv + 1] = 40;                    // decroissance courte
        sec[kOffEnv + 2] = 0;                     // pas de tenue
        for (int i = 0; i < kOctetsProfil; ++i) mouille[i] = sec[i];
        mouille[kOffFx + 7] = 220;                // melange de reverbe

        auto queue = [] (const std::vector<double>& y)
        {
            double r = 0.0;
            for (size_t i = 22000; i < y.size(); ++i) r += y[i] * y[i];
            return std::sqrt (r / (double) (y.size() - 22000));
        };
        const double qs = queue (rend (sec)), qm = queue (rend (mouille));
        std::printf ("    queue apres extinction : sec %.5f, reverbe %.5f\n", qs, qm);
        verifie ("la reverbe du profil laisse une queue",
                 qm / std::max (qs, 1e-9), 2.0, 1e9, "x");
    }

    // --- 3. ⭐ ET LA RECHERCHE SAIT LES RETROUVER.
    // /!\ C'est le seul controle qui compte vraiment : un octet qu'on peut
    // ecrire mais que la recherche n'atteint jamais ne sert a rien. On
    // fabrique une cible AVEC de la reverbe et on regarde si la recherche va
    // la chercher LA plutot que d'essayer de la peindre dans le materiau.
    {
        uint8_t vrai[kOctetsProfil];
        profilNeutre (vrai);
        vrai[kOffMoteur] = 64;                    // analogique
        vrai[kOffEnv + 1] = 40; vrai[kOffEnv + 2] = 0;
        vrai[kOffFx + 7] = 210;                   // reverbe bien presente

        std::vector<double> cible;
        rendProfil (vrai, cible, 40000, fs, 220.0);

        ReglagesRecherche reg;
        reg.evaluations = 400;
        reg.fenetreVariable = false;
        reg.nEchComparaison = 24000;
        RapportAnalyse rap;
        const auto cfg = grainTransparent (fs);
        const auto st = resynthetise (cible, fs, cfg, reg, rap,
                                      [] (const EtatRecherche&) { return true; });

        const int melange = st.profil[kOffFx + 7];
        std::printf ("    melange de reverbe : cible %d, trouve %d\n",
                     (int) vrai[kOffFx + 7], melange);
        std::printf ("    perte %.2f -> %.2f dB en %d evaluations\n",
                     st.perteDepart, st.perteMeilleure, st.evaluationsFaites);
        verifie ("la recherche monte le melange de reverbe",
                 (double) melange, 40.0, 255.0, "/255");
        verifie ("et elle progresse", st.perteDepart - st.perteMeilleure,
                 0.05, 1e6, "dB");
    }
}

// -------------------------------- 28. tout moduler : la table de destinations

static void testDestinations()
{
    std::printf ("\n28. TOUT MODULER - la table des destinations\n");

    // --- 1. LES BORNES TIENNENT, QUELLE QUE SOIT LA DOSE.
    // /!\ C'EST LE CONTROLE QUI COMPTE LE PLUS DE TOUT CE FICHIER. On a deja
    // fait diverger le filtre une fois en laissant une valeur sortir de sa
    // plage ; avec un LFO cable dessus, ca voudrait dire qu'il diverge une fois
    // par cycle. On pousse donc chaque destination aux deux extremes.
    {
        int hors = 0;
        for (int i = 8; i < kNbDestinations; ++i)
        {
            const auto& d = tableDestinations()[i];
            if (d.champ == nullptr) continue;
            for (double x : { -4.0, -1.0, 1.0, 4.0 })
            {
                ReglagesCouche r;
                appliqueDestination (i, x, r);
                const double v = *d.champ (r);
                if (v < d.mini - 1e-9 || v > d.maxi + 1e-9 || ! std::isfinite (v))
                {
                    ++hors;
                    std::printf ("    HORS BORNES : %s -> %g (plage %g..%g)\n",
                                 d.nom, v, d.mini, d.maxi);
                }
            }
        }
        std::printf ("    %d destinations poussees a +/-4 de dose\n",
                     kNbDestinations - 8);
        verifie ("aucune destination ne sort de sa plage", (double) hors, 0.0, 0.0);
    }

    // --- 2. CHAQUE DESTINATION FAIT QUELQUE CHOSE.
    // /!\ Une entree de table qui pointe le mauvais champ compile parfaitement
    // et ne se voit jamais : le cablage marche, la valeur bouge, mais pas celle
    // qu'annonce le nom. Le seul controle mecanisable est plus faible - que la
    // valeur bouge - mais il attrape le vrai accident, le pointeur qui n'a pas
    // ete mis a jour apres un renommage.
    {
        int inertes = 0;
        for (int i = 8; i < kNbDestinations; ++i)
        {
            const auto& d = tableDestinations()[i];
            if (d.champ == nullptr) continue;
            ReglagesCouche a, b;
            // on part du milieu de la plage pour que les deux sens soient libres
            *d.champ (a) = *d.champ (b) = 0.5 * (d.mini + d.maxi);
            appliqueDestination (i, 0.35, a);
            appliqueDestination (i, -0.35, b);
            if (std::abs (*d.champ (a) - *d.champ (b)) < 1e-9)
            { ++inertes; std::printf ("    INERTE : %s\n", d.nom); }
        }
        verifie ("chaque destination deplace bien son champ",
                 (double) inertes, 0.0, 0.0);
    }

    // --- 3. LES SEPT PREMIERES N'ONT PAS BOUGE DE PLACE.
    // /!\ Un projet enregistre avant la table stocke l'INDICE de sa
    // destination. Inserer une entree avant « coupure » transformerait
    // silencieusement tous les filtres modules en autre chose.
    {
        verifie ("hauteur est toujours 1", (double) (int) Dest::Hauteur, 1.0, 1.0);
        verifie ("coupure est toujours 2", (double) (int) Dest::Coupure, 2.0, 2.0);
        verifie ("morph est toujours 7", (double) (int) Dest::Morph, 7.0, 7.0);
        std::printf ("    destination 2 = « %s », destination 7 = « %s »\n",
                     nomDestination (2), nomDestination (7));
    }

    // --- 4. ⭐ UNE DESTINATION GENERIQUE S'ENTEND SUR LE MOTEUR.
    // On prend « table position » : c'est le balayage du PPG, et rien d'autre
    // dans le plugin ne sait le faire.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        auto joue = [&] (bool cable)
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.couche[0].moteur = TypeMoteur::Table;
            p.couche[0].table.position = 0.0;
            p.couche[0].attaque = 0.002; p.couche[0].decli = 4.0;
            p.couche[0].tenue = 1.0; p.couche[0].largeur = 0.0;
            p.couche[0].desaccordCents = 0.0;
            p.instab.actif = false;
            if (cable)
            {
                p.mods[0].type = TypeMod::Enveloppe;
                p.mods[0].p1 = 0.9; p.mods[0].p2 = 2.0; p.mods[0].p3 = 1.0;
                p.routages[0] = { 1, 26, 0, 1.0 };     // 26 = table position
            }
            Moteur m; m.prepare (44100.0, 4);
            m.noteOn (p, 57, 100, roms, tl);
            std::vector<double> y ((size_t) 44100);
            for (int i = 0; i < 44100; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            return y;
        };

        auto aigu = [] (const std::vector<double>& y, int deb)
        {
            const int n = 8192; const double fs = 44100.0;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (i + deb)] * w;
            }
            fft (b);
            double tot = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            { const double e = std::norm (b[(size_t) k]); tot += e;
              if ((double) k * fs / (double) n > 1500.0) haut += e; }
            return 10.0 * std::log10 (haut / tot);
        };

        const auto sec = joue (false), mod = joue (true);
        const double s0 = aigu (sec, 2000), s1 = aigu (sec, 30000);
        const double m0 = aigu (mod, 2000), m1 = aigu (mod, 30000);
        std::printf ("    part d'aigu, sans cable : %.1f -> %.1f dB\n", s0, s1);
        std::printf ("    avec l'enveloppe sur la position : %.1f -> %.1f dB\n", m0, m1);
        // /!\ BORNE ELARGIE, ET IL FAUT DIRE POURQUOI. Sans cable, la part
        // d'aigu vaut -142 dB : a ce niveau on ne mesure plus un signal mais le
        // plancher numerique, et sa « variation » n'a aucun sens musical. Le
        // jour ou la velocite a ouvert le filtre de 0,4 octave, ce plancher est
        // passe de 1,9 a 3,1 dB de variation - un test qui echoue sur du
        // silence est un test qui surveille la mauvaise chose.
        verifie ("sans cable, le spectre ne bouge pas", std::abs (s1 - s0), 0.0, 6.0, "dB");
        verifie ("et il n'y a de toute facon presque rien a entendre",
                 s1, -300.0, -100.0, "dB");
        // /!\ La comparaison qui compte est CABLE contre PAS CABLE, pas debut
        // contre fin : la table part de la position 0, qui est presque un
        // sinus, et l'enveloppe l'ouvre. Mesurer l'ecart dans le temps ne voit
        // qu'un morceau de l'attaque et sous-estime l'effet de vingt fois.
        verifie ("la destination generique atteint bien le moteur",
                 m1 - s1, 20.0, 400.0, "dB");
    }
}

static int juce_strcmp_local (const char* a, const char* b) noexcept
{
    while (*a && *a == *b) { ++a; ++b; }
    return (int) (unsigned char) *a - (int) (unsigned char) *b;
}

// ------------------- 29. modulation globale (effets) et macros assignables

static void testGlobalEtMacros()
{
    std::printf ("\n29. MODULER LES EFFETS, ET LES QUATRE MACROS\n");

    // --- 1. LES BORNES DES DESTINATIONS GLOBALES TIENNENT.
    // /!\ Meme raison qu'a la 28, et un cran plus grave : une reinjection de
    // delai poussee a 1,0 ne s'eteint plus JAMAIS. Un LFO cable dessus sans
    // borne, et le plugin monte tout seul jusqu'a saturation.
    {
        int hors = 0;
        for (int i = 1; i < kNbDestGlobales; ++i)
        {
            const auto& d = tableDestGlobales()[i];
            for (double x : { -4.0, -1.0, 1.0, 4.0 })
                for (double base : { d.mini, 0.5 * (d.mini + d.maxi), d.maxi })
                {
                    const double v = appliqueDestGlobale (i, x, base);
                    if (v < d.mini - 1e-9 || v > d.maxi + 1e-9 || ! std::isfinite (v))
                    { ++hors; std::printf ("    HORS BORNES : %s -> %g\n", d.nom, v); }
                }
        }
        std::printf ("    %d destinations globales, poussees a +/-4\n",
                     kNbDestGlobales - 1);
        verifie ("aucune destination globale ne sort de sa plage",
                 (double) hors, 0.0, 0.0);
        // celle qui compte : la reinjection du delai ne doit jamais valoir 1
        verifie ("la reinjection du delai reste sous 1",
                 appliqueDestGlobale ((int) DestGlobale::DelaiReinjection, 4.0, 0.9),
                 0.0, 0.95);
    }

    // --- 2. LES INDICES NE SE CHEVAUCHENT PAS.
    // /!\ Les destinations globales se rangent APRES les 38 autres dans une
    // liste unique. Si les deux plages se recouvraient, un cable vise vers la
    // reverbe modulerait une resonance - et rien ne le signalerait.
    {
        std::printf ("    %d destinations par voix + %d globales = %d au menu\n",
                     kNbDestinations, kNbDestGlobales - 1, kNbDestinationsTotal);
        verifie ("le total est coherent", (double) kNbDestinationsTotal,
                 (double) (kNbDestinations + kNbDestGlobales - 1),
                 (double) (kNbDestinations + kNbDestGlobales - 1));
        verifie ("la premiere globale suit la derniere locale",
                 (double) kPremiereDestGlobale, (double) kNbDestinations,
                 (double) kNbDestinations);
        // le nom lu par le menu doit correspondre des deux cotes de la couture
        std::printf ("    a la couture : %d = « %s », %d = « %s »\n",
                     kNbDestinations - 1, nomDestinationTotale (kNbDestinations - 1),
                     kNbDestinations, nomDestinationTotale (kNbDestinations));
        verifie ("la premiere globale n'est pas « - »",
                 (double) (juce_strcmp_local (nomDestinationTotale (kNbDestinations), "-") != 0),
                 1.0, 1.0);
    }

    // --- 3. ⭐ UNE MACRO EST UNE SOURCE, ET ELLE EST UNIPOLAIRE.
    // /!\ Un LFO au repos vaut 0 parce qu'il oscille autour de 0. Une macro au
    // repos vaut 0 parce que le BOUTON est a gauche : si on la centrait comme
    // un LFO, un patch fraichement ouvert modulerait deja a fond dans le
    // negatif, et l'utilisateur entendrait un son qu'il n'a pas demande.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        auto joue = [&] (double macro)
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.couche[0].moteur = TypeMoteur::Analogique;
            p.couche[0].coupureRel = 6.0;
            p.couche[0].attaque = 0.002; p.couche[0].decli = 4.0;
            p.couche[0].tenue = 1.0; p.couche[0].largeur = 0.0;
            p.couche[0].desaccordCents = 0.0;
            p.instab.actif = false;
            p.macros[0] = macro;
            // macro 1 = source 9, vers « resonance » (destination 28)
            p.routages[0] = { kNbMods + 1, 28, 0, 0.9 };

            Moteur m; m.prepare (44100.0, 4);
            m.noteOn (p, 45, 100, roms, tl);
            std::vector<double> y ((size_t) 30000);
            for (int i = 0; i < 30000; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            return y;
        };

        // la resonance se voit comme une bosse : on mesure l'energie autour de
        // la coupure (6 x 110 Hz = 660 Hz) rapportee au total
        auto bosse = [] (const std::vector<double>& y)
        {
            const int n = 16384; const double fs = 44100.0;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (i + 8000)] * w;
            }
            fft (b);
            double tot = 1e-30, autour = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fs / (double) n;
                const double e = std::norm (b[(size_t) k]);
                tot += e;
                if (f > 480.0 && f < 900.0) autour += e;
            }
            return 10.0 * std::log10 (autour / tot);
        };

        const double bas = bosse (joue (0.0)), haut = bosse (joue (1.0));
        std::printf ("    macro a 0 : %.2f dB autour de la coupure\n", bas);
        std::printf ("    macro a 1 : %.2f dB\n", haut);
        verifie ("la macro atteint sa destination", haut - bas, 1.0, 60.0, "dB");

        // et le controle qui compte : sans cable, la macro ne fait RIEN
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.instab.actif = false;
            p.macros[0] = 1.0;              // a fond, mais debranchee
            Moteur m; m.prepare (44100.0, 4);
            m.noteOn (p, 45, 100, roms, tl);
            double rms = 0.0;
            std::vector<double> y ((size_t) 8000);
            for (int i = 0; i < 8000; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; rms += a * a; }
            rms = std::sqrt (rms / 8000.0);
            std::printf ("    macro a fond mais debranchee : rms %.3f\n", rms);
            verifie ("une macro debranchee laisse le son intact", rms, 0.005, 1.0);
        }
    }
}

// ------------------------------ 30. phaser, compresseur, multifiltre, transfert

static void testEffets3()
{
    std::printf ("\n30. LES QUATRE EFFETS AJOUTES\n");
    const double fs = 44100.0;
    const int n = 60000;

    // un bruit rose grossier : de l'energie partout, ce qu'il faut pour voir
    // ce qu'un filtre fait
    auto bruit = [&] ()
    {
        std::vector<double> x ((size_t) n);
        unsigned int e = 1234u;
        double b0 = 0.0, b1 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            e = e * 1664525u + 1013904223u;
            const double u = (double) (e >> 8) / 8388608.0 - 1.0;
            b0 = 0.99 * b0 + 0.05 * u;
            b1 = 0.90 * b1 + 0.25 * u;
            x[(size_t) i] = 0.35 * (b0 + b1 + 0.2 * u);
        }
        return x;
    };
    const auto x = bruit();

    auto ampBande = [fs] (const std::vector<double>& y, double f0, double f1, int deb)
    {
        const int m = 16384;
        std::vector<std::complex<double>> b ((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) m);
            b[(size_t) i] = y[(size_t) (i + deb)] * w;
        }
        fft (b);
        double s2 = 1e-30;
        for (int k = 1; k <= m / 2; ++k)
        {
            const double f = (double) k * fs / (double) m;
            if (f >= f0 && f <= f1) s2 += std::norm (b[(size_t) k]);
        }
        return 10.0 * std::log10 (s2);
    };

    // --- 1. LE PHASER CREUSE, il ne desaccorde pas.
    // /!\ Sa signature n'est PAS « ca bouge » - le chorus aussi bouge. C'est
    // que le spectre presente des CREUX qui se deplacent : a un instant donne,
    // certaines bandes sont annulees. On mesure donc l'ecart entre deux
    // instants DANS UNE MEME BANDE ETROITE.
    {
        Phaser ph; ph.prepare (fs);
        ph.regle (1.2, 0.8, 1.0, 0.6, 6);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
        { double a = x[(size_t) i], b = a; ph.traite (a, b); y[(size_t) i] = 0.5 * (a + b); }

        double mini = 1e9, maxi = -1e9;
        for (int t = 2000; t + 16384 < n; t += 4000)
        {
            const double e = ampBande (y, 380.0, 520.0, t);
            mini = std::min (mini, e); maxi = std::max (maxi, e);
        }
        const double refMini = ampBande (x, 380.0, 520.0, 2000);
        const double refMaxi = ampBande (x, 380.0, 520.0, 30000);
        std::printf ("    bande 380-520 Hz : sec %.1f a %.1f dB, phase %.1f a %.1f dB\n",
                     std::min (refMini, refMaxi), std::max (refMini, refMaxi), mini, maxi);
        verifie ("le phaser creuse et rebouche une bande etroite",
                 maxi - mini, 6.0, 80.0, "dB");
        verifie ("alors que le signal sec y est stable",
                 std::abs (refMaxi - refMini), 0.0, 3.0, "dB");
    }

    // --- 2. LE COMPRESSEUR REDUIT LA DYNAMIQUE, et ne fait rien au repos.
    {
        // un signal a deux niveaux : fort puis faible
        std::vector<double> deux ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double a = (i < n / 2) ? 0.9 : 0.09;
            deux[(size_t) i] = a * std::sin (2.0 * kPi * 220.0 * (double) i / fs);
        }
        auto passe = [&] (double seuil, double taux)
        {
            Compresseur c; c.prepare (fs);
            c.regle (seuil, taux, 5.0, 80.0, 1.0);
            std::vector<double> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            { double a = deux[(size_t) i], b = a; c.traite (a, b); y[(size_t) i] = a; }
            return y;
        };
        auto rms = [] (const std::vector<double>& y, int a, int b)
        {
            double s2 = 0.0;
            for (int i = a; i < b; ++i) s2 += y[(size_t) i] * y[(size_t) i];
            return 20.0 * std::log10 (std::sqrt (s2 / (double) (b - a)) + 1e-12);
        };

        const auto sec = passe (0.0, 1.0);           // inactif
        const auto comp = passe (-24.0, 8.0);
        const double dSec  = rms (sec, 5000, 20000) - rms (sec, 35000, 55000);
        const double dComp = rms (comp, 5000, 20000) - rms (comp, 35000, 55000);
        std::printf ("    ecart fort/faible : sec %.1f dB, comprime %.1f dB\n",
                     dSec, dComp);
        verifie ("le compresseur reduit la dynamique", dSec - dComp, 6.0, 40.0, "dB");
        // /!\ ET AU REPOS IL EST TRANSPARENT. Un compresseur qui colore a seuil
        // 0 dB est un compresseur qu'on ne peut plus retirer de la chaine.
        double ecart = 0.0;
        for (int i = 0; i < n; ++i)
            ecart = std::max (ecart, std::abs (sec[(size_t) i] - deux[(size_t) i]));
        std::printf ("    ecart maximal au repos : %.2e\n", ecart);
        verifie ("au repos il ne touche a rien", ecart, 0.0, 1e-9);
    }

    // --- 3. LE MULTIFILTRE : quatre types, quatre spectres.
    {
        auto passe = [&] (TypeMultiFiltre t)
        {
            MultiFiltre f; f.prepare (fs);
            f.regle (1000.0, 0.3, t, 1.0);
            std::vector<double> y ((size_t) n);
            for (int i = 0; i < n; ++i)
            { double a = x[(size_t) i], b = a; f.traite (a, b); y[(size_t) i] = a; }
            return y;
        };
        const double grave = 200.0, aigu = 6000.0;
        const auto pb = passe (TypeMultiFiltre::PasseBas);
        const auto ph = passe (TypeMultiFiltre::PasseHaut);
        const double pbG = ampBande (pb, 100.0, 300.0, 4000) - ampBande (x, 100.0, 300.0, 4000);
        const double pbA = ampBande (pb, 5000.0, 8000.0, 4000) - ampBande (x, 5000.0, 8000.0, 4000);
        const double phG = ampBande (ph, 100.0, 300.0, 4000) - ampBande (x, 100.0, 300.0, 4000);
        const double phA = ampBande (ph, 5000.0, 8000.0, 4000) - ampBande (x, 5000.0, 8000.0, 4000);
        std::printf ("    passe-bas  : %+.1f dB a %.0f Hz, %+.1f dB a %.0f Hz\n",
                     pbG, grave, pbA, aigu);
        std::printf ("    passe-haut : %+.1f dB a %.0f Hz, %+.1f dB a %.0f Hz\n",
                     phG, grave, phA, aigu);
        verifie ("le passe-bas retire l'aigu", pbG - pbA, 12.0, 90.0, "dB");
        verifie ("le passe-haut retire le grave", phA - phG, 12.0, 90.0, "dB");
    }

    // --- 3 bis. ET IL TIENT SUR TOUTE LA PLAGE, RESONANCE COMPRISE.
    //
    // /!\ CE CONTROLE MANQUAIT, et le filtre DIVERGEAIT au-dessus de 6 kHz.
    // La forme de Chamberlin d'origine demande f + q < 2, soit fc < fs/6 ; sa
    // borne etait posee a 0,45 fs. Un passe-bas a 10 kHz sortait un infini en
    // quelques centaines d'echantillons - inaudible dans un test qui reste a
    // 1 kHz, et fatal des que l'utilisateur monte la coupure.
    {
        int deborde = 0;
        double pire = 0.0; double fPire = 0.0;
        for (double fc = 100.0; fc < 0.49 * fs; fc *= 1.35)
            for (double res : { 0.0, 0.5, 0.99 })
                for (TypeMultiFiltre t : { TypeMultiFiltre::PasseBas,
                                           TypeMultiFiltre::PasseHaut,
                                           TypeMultiFiltre::PasseBande,
                                           TypeMultiFiltre::Rejecteur })
                {
                    MultiFiltre f; f.prepare (fs);
                    f.regle (fc, res, t, 1.0);
                    double crete = 0.0;
                    for (int i = 0; i < 20000; ++i)
                    {
                        double a = (i % 97 == 0) ? 1.0 : 0.0, b = a;
                        f.traite (a, b);
                        crete = std::max (crete, std::abs (a));
                        if (! (std::abs (a) < 1e6)) break;
                    }
                    if (! (crete < 1e6)) ++deborde;
                    if (crete > pire) { pire = crete; fPire = fc; }
                }
        std::printf ("    multifiltre balaye de 100 Hz a %.0f Hz : %d divergences, "
                     "crete maximale %.2f (a %.0f Hz)\n",
                     0.49 * fs, deborde, pire, fPire);
        verifie ("le multifiltre ne diverge a aucune coupure", (double) deborde, 0.0, 0.0);
        // ⭐ Et il ne se contente pas de ne pas exploser : un filtre resonant
        // honnete ne depasse pas quelques fois l'entree.
        verifie ("et sa resonance reste bornee", pire, 0.0, 40.0);
    }

    // --- 4. ⭐ LE TRANSFERT DE CONVERTISSEUR.
    // /!\ Ce qui prouve que c'est un CONVERTISSEUR et pas un simple filtre :
    // il replie. Une sinusoide au-dessus de la moitie de l'horloge doit
    // reapparaitre EN DESSOUS, a une frequence qui n'a aucun rapport
    // harmonique avec elle. C'est le maintien d'ordre zero non filtre en amont
    // - exactement ce que les machines de l'epoque faisaient.
    {
        const double f = 15000.0;                  // au-dessus de 24000/2
        std::vector<double> sinus ((size_t) n);
        for (int i = 0; i < n; ++i)
            sinus[(size_t) i] = 0.8 * std::sin (2.0 * kPi * f * (double) i / fs);

        Transfert tr; tr.prepare (fs);
        auto cfg = configMachine (Machine::Fairlight);   // horloge 24 kHz
        tr.regle (cfg, 1.0);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
        { double a = sinus[(size_t) i], b = a; tr.traite (a, b); y[(size_t) i] = a; }

        // l'image repliee : 24000 - 15000 = 9000 Hz
        const double image = ampA (y, 8000, 16384, 9000.0, fs);
        const double avant = ampA (sinus, 8000, 16384, 9000.0, fs);
        std::printf ("    a 9 kHz (repli de 15 kHz par une horloge a 24 kHz) :\n");
        std::printf ("      sec %.6f, apres transfert %.5f\n", avant, image);
        verifie ("le transfert replie, comme un vrai convertisseur",
                 image / std::max (avant, 1e-9), 20.0, 1e9, "x");

        // et a melange nul il ne fait RIEN
        Transfert tr2; tr2.prepare (fs);
        tr2.regle (cfg, 0.0);
        double ecart = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double a = sinus[(size_t) i], b = a;
            tr2.traite (a, b);
            ecart = std::max (ecart, std::abs (a - sinus[(size_t) i]));
        }
        verifie ("a melange nul il est transparent", ecart, 0.0, 1e-12);
    }
}

// --------------------------------------------- 31. les saveurs de machine

static void testMachines()
{
    std::printf ("\n31. LES MACHINES, ET LA POSITION « PROPRE »\n");

    // --- 1. ⭐⭐ « PROPRE » EST VRAIMENT PROPRE.
    // /!\ C'EST LE CONTROLE QUI COMPTE. L'utilisateur a demande « la
    // possibilite de rendre le plugin completement clean » : si la position
    // Propre laissait une quantification 16 bits ou un filtre a 0,45, elle
    // serait une saveur de plus au lieu d'etre une reference - et toute
    // comparaison avec les autres machines deviendrait bancale.
    {
        const auto c = configMachine (Machine::Propre);
        std::printf ("    Propre : grain %s, quantification %d, horloge %d\n",
                     c.actif ? "actif" : "inactif", (int) c.quant, (int) c.horloge);
        verifie ("le grain est desactive", c.actif ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("aucune quantification",
                 (c.quant == Quant::Aucune) ? 1.0 : 0.0, 1.0, 1.0);
        verifie ("horloge fixe", (c.horloge == Horloge::Fixe) ? 1.0 : 0.0, 1.0, 1.0);

        // et la quantification « Aucune » doit etre l'identite, pas un arrondi
        double pire = 0.0;
        for (int i = -1000; i <= 1000; ++i)
        {
            const double v = (double) i / 1000.0;
            pire = std::max (pire, std::abs (quantifie (v, c.quant) - v));
        }
        std::printf ("    erreur de quantification maximale : %.2e\n", pire);
        verifie ("« aucune » ne touche pas l'echantillon", pire, 0.0, 1e-12);
    }

    // --- 2. LES SAVEURS SONT DISTINCTES, et pas seulement nommees.
    // /!\ Deux machines qui rendraient le meme son feraient d'un menu de six
    // entrees un menu de quatre, sans que rien ne le dise.
    {
        const double fs = 44100.0;
        const int n = 30000;
        std::vector<double> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            x[(size_t) i] = 0.3 * (std::sin (2.0 * kPi * 300.0 * t)
                                 + std::sin (2.0 * kPi * 2100.0 * t)
                                 + std::sin (2.0 * kPi * 7300.0 * t));
        }
        std::vector<double> rendu[kNbMachines];
        for (int m = 0; m < kNbMachines; ++m)
        {
            Transfert tr; tr.prepare (fs);
            tr.regle (configMachine ((Machine) m), 1.0);
            rendu[m].resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            { double a = x[(size_t) i], b = a; tr.traite (a, b); rendu[m][(size_t) i] = a; }
        }

        double pireProche = 1e9;
        int im = 0, jm = 0;
        for (int a = 1; a < kNbMachines; ++a)        // on saute Propre
            for (int b = a + 1; b < kNbMachines; ++b)
            {
                const double p = perte (empreinte (rendu[a], fs, kPlancherPropre),
                                        empreinte (rendu[b], fs, kPlancherPropre));
                if (p < pireProche) { pireProche = p; im = a; jm = b; }
            }
        std::printf ("    les deux plus proches : %s\n      et %s -> %.2f dB\n",
                     nomMachine (im), nomMachine (jm), pireProche);
        verifie ("aucune machine n'est le doublon d'une autre",
                 pireProche, 0.8, 1e6, "dB");

        // et Propre doit etre le plus proche du signal d'origine
        double dPropre = perte (empreinte (x, fs, kPlancherPropre),
                                empreinte (rendu[0], fs, kPlancherPropre));
        double dPire = 0.0;
        for (int m = 1; m < kNbMachines; ++m)
            dPire = std::max (dPire, perte (empreinte (x, fs, kPlancherPropre),
                                            empreinte (rendu[m], fs, kPlancherPropre)));
        std::printf ("    distance au signal d'origine : Propre %.3f dB, "
                     "la plus marquee %.2f dB\n", dPropre, dPire);
        verifie ("Propre ne s'entend pas", dPropre, 0.0, 0.5, "dB");
        verifie ("les autres s'entendent", dPire, 2.0, 1e6, "dB");
    }

    // --- 3. LA CHAINE ACCEPTE DIX EMPLACEMENTS.
    {
        int c[kEmplacementsFx];
        const int tout[kEmplacementsFx] = { 1, 2, 7, 3, 4, 8, 9, 5, 6, 10 };
        const int nn = chaineFx (tout, kEmplacementsFx, c);
        std::printf ("    chaine par defaut : %d etages\n", nn);
        verifie ("les dix effets tiennent dans la chaine",
                 (double) nn, (double) kEmplacementsFx, (double) kEmplacementsFx);
        verifie ("et le compresseur est le dernier",
                 (double) c[kEmplacementsFx - 1], 10.0, 10.0);
    }
}

// -------------------------- 32. les defauts entendus par l'utilisateur

static void testRetoursEcoute()
{
    std::printf ("\n32. CE QUE L'UTILISATEUR A ENTENDU\n");

    const double fs = 44100.0;
    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].attaque = 0.005; p.couche[0].decli = 0.35;
        p.couche[0].tenue = 0.30;    p.couche[0].relache = 0.3;
        p.couche[0].largeur = 0.0;   p.couche[0].desaccordCents = 0.0;
        p.couche[0].va.cents2 = 0.0;
        p.instab.actif = false;
        for (auto& m : p.mods) m.type = TypeMod::Aucun;
        for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }
        return p;
    };
    auto crete = [] (const std::vector<double>& y, int a, int b)
    {
        double m = 0.0;
        for (int i = a; i < b && i < (int) y.size(); ++i)
            m = std::max (m, std::abs (y[(size_t) i]));
        return m;
    };

    // --- 1. ⭐⭐ « UNE NOTE JOUEE APRES UNE AUTRE DEVIENT PLUS FAIBLE ».
    // /!\ La cause etait un test de trop courte vue : on regardait si la voix
    // SONNAIT ENCORE au lieu de regarder si une TOUCHE ETAIT TENUE. Une note
    // relachee sonne pendant tout son relachement - on enchainait donc en
    // legato sur une queue qui s'eteint, et la note suivante heritait du
    // niveau residuel. Mesure avant correction : 0,26 en legato, 0,84 en mono.
    {
        static const char* nom[] = { "poly", "mono", "legato" };
        for (int mode = 0; mode < 3; ++mode)
        {
            auto p = base();
            p.mode = (mode == 0) ? ModeVoix::Poly
                   : (mode == 1) ? ModeVoix::Mono : ModeVoix::Legato;
            p.polyphonie = (mode == 0) ? 16 : 1;

            Moteur m; m.prepare (fs, 16);
            std::vector<double> y;
            m.noteOn (p, 60, 100, roms, tl);
            for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }
            m.noteOff (p, 60);                 // touche LACHEE
            m.noteOn (p, 60, 100, roms, tl);   // et on rejoue tout de suite
            for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }

            const double c1 = crete (y, 0, 22050), c2 = crete (y, 44100, 66150);
            std::printf ("    %-7s : %.4f puis %.4f -> %.2f\n",
                         nom[mode], c1, c2, c2 / std::max (c1, 1e-9));
            verifie ("la deuxieme note sonne aussi fort que la premiere",
                     c2 / std::max (c1, 1e-9), 0.90, 1.60, "x");
        }
    }

    // --- 2. ET LE LEGATO DOIT QUAND MEME LIER quand la touche EST tenue.
    // /!\ La correction ne doit pas casser ce qu'elle protege : si l'on
    // redeclenchait aussi quand un doigt reste pose, le legato n'existerait
    // plus. On verifie les deux sens, sinon on a juste deplace le bug.
    {
        auto p = base();
        p.mode = ModeVoix::Legato; p.polyphonie = 1;
        p.couche[0].tenue = 1.0; p.couche[0].decli = 8.0;

        Moteur m; m.prepare (fs, 16);
        std::vector<double> y;
        m.noteOn (p, 60, 100, roms, tl);
        for (int i = 0; i < 22050; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }
        m.noteOn (p, 67, 100, roms, tl);       // seconde note SANS lacher
        for (int i = 0; i < 22050; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }

        // un redeclenchement se verrait comme un creux : l'attaque repart
        const double avant = crete (y, 20000, 22000);
        const double apres = crete (y, 22100, 24100);
        std::printf ("    legato lie : %.4f avant la liaison, %.4f apres\n",
                     avant, apres);
        verifie ("le legato ne redeclenche pas l'enveloppe",
                 apres / std::max (avant, 1e-9), 0.75, 1.35, "x");
    }

    // --- 3. « EN PROPRE IL Y A QUAND MEME DU WOBBLE ».
    // ⭐ Ce n'etait pas le grain : c'etait le CABLAGE PAR DEFAUT. « LFO 1 ->
    // hauteur, dose 0,008 » a l'air minuscule et vaut +/- 19,2 cents, parce que
    // la destination hauteur porte 24 demi-tons a pleine dose. Une dose se lit
    // dans l'unite de sa destination.
    {
        const double cents = 0.008 * 24.0 * 100.0;
        std::printf ("    dose 0,008 sur la hauteur = +/- %.1f cents\n", cents);
        verifie ("ce qui n'a rien d'un reglage neutre", cents, 15.0, 25.0, "cents");
        // et la correction : plus aucun cablage de hauteur par defaut
        verifie ("un vibrato musical serait plutot sous 8 cents",
                 0.0025 * 24.0 * 100.0, 0.0, 8.0, "cents");
    }

    // --- 4. LE LIMITEUR EMPECHE UN ACCORD DE DEPASSER LA PLEINE ECHELLE.
    // /!\ Seize voix de trois oscillateurs se somment sans protection : ce
    // qu'on entend alors n'est pas l'instrument mais l'ecretage de l'hote.
    {
        auto p = base();
        p.couche[0].tenue = 1.0; p.couche[0].decli = 8.0;
        p.mode = ModeVoix::Poly; p.polyphonie = 16;
        Moteur m; m.prepare (fs, 16);
        for (int n : { 48, 52, 55, 60, 64, 67, 72 })
            m.noteOn (p, n, 110, roms, tl);

        Compresseur c; c.prepare (fs);
        // /!\ ATTAQUE COURTE ET AUCUNE COMPENSATION - et c'est une mesure qui
        // l'a impose. Avec 8 ms d'attaque et la compensation a fond, le
        // limiteur RENDAIT L'ACCORD PLUS FORT : 1,80 avant, 2,02 apres. Le
        // detecteur, lisse sur 8 ms, voyait 0,9 la ou la crete instantanee
        // valait 1,8 - il reduisait donc de moitie moins que ce que la
        // compensation rajoutait. Un etage de securite se regle sur ce qu'il
        // VOIT, pas sur ce qu'on suppose qu'il voit.
        c.regle (-6.0, 20.0, 1.0, 150.0, 0.0);
        double sans = 0.0, avec = 0.0;
        for (int i = 0; i < 44100; ++i)
        {
            double a, b; m.suivant (p, a, b);
            sans = std::max (sans, std::abs (a));
            double ca = a, cb = b;
            c.traite (ca, cb);
            if (i > 4410) avec = std::max (avec, std::abs (ca));
        }
        std::printf ("    accord de 7 notes : crete %.3f sans limiteur, "
                     "%.3f avec\n", sans, avec);
        verifie ("le limiteur ramene la crete sous la pleine echelle",
                 avec, 0.0, 1.0);
    }
}

// ------------------------ 33. la velocite et les gestes de rompleur

static void testVelocite()
{
    std::printf ("\n33. LA VELOCITE, ET LES GESTES DE ROMPLEUR\n");
    std::printf ("    (avant aujourd'hui, `accepte()` etait le SEUL endroit du\n"
                 "     plugin qui lisait la velocite)\n");

    const double fs = 44100.0;
    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].attaque = 0.005; p.couche[0].decli = 8.0;
        p.couche[0].tenue = 1.0;     p.couche[0].relache = 0.3;
        p.couche[0].largeur = 0.0;   p.couche[0].desaccordCents = 0.0;
        p.couche[0].va.cents2 = 0.0;
        p.couche[0].veloNiveau = 0.0; p.couche[0].veloCoupure = 0.0;
        p.instab.actif = false;
        for (auto& m : p.mods) m.type = TypeMod::Aucun;
        for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }
        return p;
    };
    auto joue = [&] (ReglagesPatch p, int velo, int n)
    {
        Moteur m; m.prepare (fs, 16);
        m.noteOn (p, 60, velo, roms, tl);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i) { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
        return y;
    };
    auto crete = [] (const std::vector<double>& y, int a, int b)
    {
        double m = 0.0;
        for (int i = a; i < b && i < (int) y.size(); ++i)
            m = std::max (m, std::abs (y[(size_t) i]));
        return m;
    };

    // --- 1. LA VELOCITE FAIT LE NIVEAU.
    {
        auto p = base(); p.couche[0].veloNiveau = 0.65;
        const double fort  = crete (joue (p, 127, 20000), 2000, 18000);
        const double doux  = crete (joue (p, 30,  20000), 2000, 18000);
        std::printf ("    niveau : velo 127 -> %.4f, velo 30 -> %.4f (%.1f dB)\n",
                     fort, doux, 20.0 * std::log10 (fort / std::max (doux, 1e-9)));
        verifie ("jouer doux sonne moins fort",
                 20.0 * std::log10 (fort / std::max (doux, 1e-9)), 4.0, 40.0, "dB");

        // et a zero, elle ne doit RIEN faire (compatibilite avec l'ancien son)
        auto q = base();
        const double a = crete (joue (q, 127, 20000), 2000, 18000);
        const double b = crete (joue (q, 30,  20000), 2000, 18000);
        verifie ("a zero elle ne change rien", std::abs (a - b) / std::max (a, 1e-9),
                 0.0, 0.02, "x");
    }

    // --- 2. LA VELOCITE OUVRE LE FILTRE, et ca TIENT dans le temps.
    // /!\ C'est le piege : `majHauteur` recalcule la coupure a chaque pas de
    // controle. Une valeur posee au declenchement serait effacee au bout de
    // 16 echantillons - un filtre qui s'ouvre puis se referme aussitot, ce qui
    // ne s'entend pas comme un bug mais comme un effet rate.
    {
        auto p = base();
        p.couche[0].moteur = TypeMoteur::Analogique;
        p.couche[0].coupureRel = 4.0;
        p.couche[0].veloCoupure = 0.6;

        auto aigu = [fs] (const std::vector<double>& y, int deb)
        {
            const int n = 8192;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) (i + deb)] * w;
            }
            fft (b);
            double tot = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            { const double e = std::norm (b[(size_t) k]); tot += e;
              if ((double) k * fs / (double) n > 1500.0) haut += e; }
            return 10.0 * std::log10 (haut / tot);
        };
        const auto yF = joue (p, 127, 44100), yD = joue (p, 20, 44100);
        const double f0 = aigu (yF, 2000), f1 = aigu (yF, 30000);
        const double d0 = aigu (yD, 2000), d1 = aigu (yD, 30000);
        std::printf ("    part d'aigu : fort %.1f puis %.1f dB, doux %.1f puis %.1f dB\n",
                     f0, f1, d0, d1);
        verifie ("frappe fort, le filtre est plus ouvert", f0 - d0, 3.0, 60.0, "dB");
        verifie ("et il le RESTE une seconde plus tard", f1 - d1, 3.0, 60.0, "dB");
    }

    // --- 3. LE DEPART DE LECTURE.
    {
        auto p = base();
        p.couche[0].depart = 0.0;
        const auto a = joue (p, 100, 4000);
        p.couche[0].depart = 0.5;
        const auto b = joue (p, 100, 4000);
        double ecart = 0.0;
        for (size_t i = 500; i < 3000; ++i) ecart += std::abs (a[i] - b[i]);
        std::printf ("    ecart cumule entre depart 0 et depart 0,5 : %.2f\n", ecart);
        verifie ("le depart de lecture change ce qu'on entend", ecart, 5.0, 1e9);
    }

    // --- 4. LE TONE DELAY retarde vraiment, il ne mange pas le debut.
    // /!\ La difference est tout le point : si l'on laissait l'enveloppe courir
    // pendant le retard, la couche entrerait a mi-decroissance au lieu
    // d'entrer a l'attaque.
    {
        auto p = base();
        p.couche[0].retard = 0.25;
        const auto y = joue (p, 100, 44100);
        const double avant = crete (y, 0, 10000);     // avant 0,227 s
        const double apres = crete (y, 13000, 30000);
        std::printf ("    retard 0,25 s : crete %.5f avant, %.4f apres\n", avant, apres);
        verifie ("rien ne sort pendant le retard", avant, 0.0, 1e-9);
        verifie ("et la couche entre a pleine attaque ensuite", apres, 0.02, 1.0);
    }

    // --- 5. LE FONDU DE ZONE remplace l'interrupteur.
    {
        auto p = base();
        p.couche[0].veloMin = 64; p.couche[0].veloMax = 127;
        p.couche[0].fonduVelo = 0.0;
        const double dur56 = crete (joue (p, 56, 20000), 2000, 18000);
        p.couche[0].fonduVelo = 32.0;
        const double doux56 = crete (joue (p, 56, 20000), 2000, 18000);
        std::printf ("    velo 56 (sous la zone) : sans fondu %.5f, avec %.4f\n",
                     dur56, doux56);
        verifie ("sans fondu, c'est un interrupteur", dur56, 0.0, 1e-9);
        verifie ("avec fondu, la couche entre progressivement", doux56, 0.005, 1.0);
    }

    // --- 6. LE FONDU DE BOUCLE supprime la discontinuite au point de bouclage.
    // /!\ On fabrique expres une ROM qui NE boucle PAS proprement : un materiau
    // bien fait n'a pas de clic, donc il ne prouverait rien.
    {
        std::vector<double> mauvaise ((size_t) 8000);
        for (int i = 0; i < 8000; ++i)
            mauvaise[(size_t) i] = 0.6 * std::sin (2.0 * kPi * 7.3 * (double) i / 1000.0);
        const double* r2[kNbCouches] = { mauvaise.data(), nullptr, nullptr, nullptr };
        int t2[kNbCouches] = { (int) mauvaise.size(), 0, 0, 0 };

        auto pasMax = [&] (double fondu)
        {
            auto p = base();
            p.couche[0].boucleDebut = 0.1; p.couche[0].boucleFin = 0.6;
            p.couche[0].fonduBoucle = fondu;
            p.couche[0].grain.actif = false;
            Moteur m; m.prepare (fs, 16);
            m.noteOn (p, 60, 100, r2, t2);
            std::vector<double> y ((size_t) 44100);
            for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            double mx = 0.0, moy = 0.0;
            for (size_t i = 5001; i < y.size(); ++i)
            { const double dd = std::abs (y[i] - y[i - 1]); mx = std::max (mx, dd); moy += dd; }
            moy /= (double) (y.size() - 5001);
            return mx / std::max (moy, 1e-12);
        };
        const double sans = pasMax (0.0), avec = pasMax (0.25);
        std::printf ("    pas maximal / pas moyen : sans fondu %.1f, avec %.1f\n",
                     sans, avec);
        verifie ("le fondu de boucle adoucit le point de bouclage",
                 sans - avec, 0.5, 1e6, "x");
    }
}

// ---------------- 34. ce que l'utilisateur a entendu, deuxieme serie

// -------------------------------------------- 34. la grille dessinable

// ------------------- 35. passe-haut de couche, desaccord et vitesse par operateur

static void testPasseHautEtOperateurs()
{
    std::printf ("\n35. PASSE-HAUT DE COUCHE, DESACCORD ET VITESSE PAR OPERATEUR\n");
    std::printf ("    (/!\\ le passe-haut vit dans la CHAINE DE COUCHE, donc\n"
                 "     sur les quatre moteurs et les quatre couches)\n");

    const double fs = 44100.0;

    // --- 1. LE PASSE-HAUT RETIRE LE GRAVE, ET SEULEMENT LUI.
    {
        auto reponse = [&] (double fSignal, double fCoupe, double reso, int pente)
        {
            PasseHaut h; h.prepare (fs);
            h.regle (fCoupe, reso, pente);
            double crete = 0.0;
            const int n = 40000;
            for (int i = 0; i < n; ++i)
            {
                const double x = std::sin (2.0 * kPi * fSignal * (double) i / fs);
                const double y = h.traite (x);
                if (i > n / 2) crete = std::max (crete, std::abs (y));
            }
            return 20.0 * std::log10 (std::max (1e-12, crete));
        };
        const double grave = reponse (60.0, 400.0, 0.0, 1);
        const double aigu  = reponse (4000.0, 400.0, 0.0, 1);
        std::printf ("    coupure 400 Hz, 12 dB/oct : %+.1f dB a 60 Hz, %+.1f dB a 4 kHz\n",
                     grave, aigu);
        verifie ("le passe-haut retire le grave", grave, -60.0, -10.0, "dB");
        verifie ("et laisse l'aigu intact", aigu, -0.6, 0.6, "dB");

        // ⭐ 24 dB/oct retire DEUX FOIS PLUS par octave. Sans cette mesure,
        // « pente » ne serait qu'une etiquette.
        const double g12 = reponse (100.0, 400.0, 0.0, 1);
        const double g24 = reponse (100.0, 400.0, 0.0, 2);
        std::printf ("    a 100 Hz : %+.1f dB en 12 dB/oct, %+.1f dB en 24\n", g12, g24);
        // Deux octaves sous la coupure : 24 dB attendus en 12 dB/oct, 48 en
        // 24 - soit exactement 24 dB d'ecart. La borne haute etait posee a 24,0
        // et refusait donc la valeur juste.
        verifie ("la pente raide retire davantage", g12 - g24, 8.0, 30.0, "dB");
    }

    // --- 2. ET SA RESONANCE CHANTE (MS-20) SANS DIVERGER.
    {
        auto crete = [&] (double reso)
        {
            PasseHaut h; h.prepare (fs);
            h.regle (800.0, reso, 1);
            double mx = 0.0;
            for (int i = 0; i < 40000; ++i)
            {
                const double x = std::sin (2.0 * kPi * 800.0 * (double) i / fs);
                const double y = h.traite (x);
                if (i > 20000) mx = std::max (mx, std::abs (y));
            }
            return 20.0 * std::log10 (std::max (1e-12, mx));
        };
        const double plat = crete (0.0), pointu = crete (0.95);
        std::printf ("    a la coupure : %+.1f dB sans resonance, %+.1f dB a 0,95\n",
                     plat, pointu);
        verifie ("la resonance fabrique une bosse", pointu - plat, 6.0, 40.0, "dB");

        // /!\ ET ELLE TIENT QUAND LA COUPURE BOUGE. C'est le cas ou le
        // multifiltre divergeait (piege 32), et le seul qui compte vraiment :
        // une coupure fixe ne prouve rien puisqu'on la module.
        PasseHaut h; h.prepare (fs);
        double mx = 0.0;
        for (int i = 0; i < 200000; ++i)
        {
            const double u = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / 5000.0);
            h.regle (30.0 + u * 15000.0, 0.97, 2);
            const double x = (i % 89 == 0) ? 1.0 : 0.0;
            const double y = h.traite (x);
            mx = std::max (mx, std::abs (y));
            if (! (std::abs (y) < 1e6)) break;
        }
        std::printf ("    coupure balayee de 30 Hz a 15 kHz a resonance 0,97 : "
                     "crete %.2f\n", mx);
        verifie ("le passe-haut ne diverge pas quand on le module", mx, 0.0, 40.0);
    }

    // --- 3. IL EST LA SUR LES QUATRE MOTEURS.
    //
    // /!\ C'EST LE CONTROLE QUI COMPTE POUR LA DEMANDE. Un passe-haut qui ne
    // marcherait que sur un moteur laisserait le build-up de grave exactement
    // la ou il se produit - dans un empilement de couches, donc de moteurs
    // differents.
    {
        std::vector<double> rom;
        Recette rec = recetteTest();
        const auto info = rendRom (rec, rom);

        for (int m = 0; m < 4; ++m)
        {
            auto joue = [&] (double fCoupe)
            {
                ReglagesPatch p;
                for (auto& c : p.couche) c.active = false;
                p.couche[0].active = true;
                p.couche[0].moteur = (TypeMoteur) m;
                p.couche[0].phCoupure = fCoupe;
                p.couche[0].phReso = 0.0;
                p.couche[0].phPente = 2;
                p.couche[0].grain.actif = false;
                p.instab.actif = false;
                for (auto& md : p.mods) md.type = TypeMod::Aucun;
                for (auto& rt : p.routages) { rt.source = 0; rt.dest = 0; rt.dose = 0.0; }

                const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
                int tl[kNbCouches] = { info.nTotal, 0, 0, 0 };
                Moteur mo; mo.prepare (fs, 8);
                mo.noteOn (p, 45, 110, roms, tl);   // la1 = 110 Hz, bien grave
                std::vector<double> y;
                for (int i = 0; i < 30000; ++i)
                { double a, b; mo.suivant (p, a, b); y.push_back (a); }
                return y;
            };
            const auto ouvert = joue (20.0);
            const auto ferme  = joue (2000.0);

            auto energieGrave = [fs] (const std::vector<double>& y)
            {
                const int n = 16384;
                std::vector<std::complex<double>> b ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                    b[(size_t) i] = y[(size_t) (i + 4000)] * w;
                }
                fft (b);
                double e = 1e-30;
                for (int k = 1; k <= n / 2; ++k)
                    if ((double) k * fs / (double) n < 300.0) e += std::norm (b[(size_t) k]);
                return 10.0 * std::log10 (e);
            };
            const double d = energieGrave (ouvert) - energieGrave (ferme);
            static const char* kNom[4] = { "ROM", "analogique", "FM", "table" };
            std::printf ("    moteur %-11s : grave retire %5.1f dB\n", kNom[m], d);
            verifie ("le passe-haut agit sur ce moteur", d, 10.0, 200.0, "dB");
        }
    }

    // --- 4. LE DESACCORD PAR OPERATEUR FAIT BATTRE.
    //
    // Deux operateurs au MEME rapport, l'un desaccorde : l'enveloppe du son
    // doit se mettre a onduler. C'est la moitie de l'epaisseur d'un DX7.
    {
        auto ondulation = [&] (double cents)
        {
            ReglagesFM r;
            r.algo = 7;                 // quatre sinus additifs : pas de FM, que la somme
            for (int i = 0; i < kOperateurs; ++i)
            {
                r.ratio[i] = 1.0;
                r.niveau[i] = (i < 2) ? 1.0 : 0.0;
                r.tenue[i] = 1.0; r.decli[i] = 8.0;
                r.echelleNiveau[i] = 0.0; r.desaccord[i] = 0.0;
            }
            r.desaccord[1] = cents;

            EtageFM e; e.prepare (fs);
            e.declenche (r, 220.0, 0.0);
            std::vector<double> y;
            for (int i = 0; i < 88200; ++i) y.push_back (e.suivant (r));

            // enveloppe : crete par tranches de 20 ms, puis ecart entre le
            // maximum et le minimum de cette enveloppe
            double mini = 1e9, maxi = 0.0;
            const int pas = 882;
            for (size_t d0 = 4410; d0 + (size_t) pas < y.size(); d0 += (size_t) pas)
            {
                double c = 0.0;
                for (int i = 0; i < pas; ++i) c = std::max (c, std::abs (y[d0 + (size_t) i]));
                mini = std::min (mini, c); maxi = std::max (maxi, c);
            }
            return 20.0 * std::log10 (std::max (1e-9, maxi) / std::max (1e-9, mini));
        };
        const double sans = ondulation (0.0), avec = ondulation (8.0);
        std::printf ("    deux operateurs au meme rapport : ondulation %.1f dB sans "
                     "desaccord, %.1f dB a 8 cents\n", sans, avec);
        verifie ("sans desaccord le son ne bat pas", sans, 0.0, 1.0, "dB");
        verifie ("avec desaccord il bat", avec, 12.0, 200.0, "dB");
    }

    // --- 5. LA VITESSE PAR OPERATEUR REND PLUS BRILLANT, PAS PLUS FORT.
    //
    // /!\ C'EST TOUTE LA DIFFERENCE AVEC UN REGLAGE DE VOLUME, et c'est
    // pourquoi le DX7 la regle par operateur. Posee sur un MODULATEUR, jouer
    // plus fort ouvre le timbre ; posee sur une porteuse, ca ne fait que
    // monter le son. On mesure donc le RAPPORT aigu/fondamental, qui ne bouge
    // pas si l'on ne fait que changer le volume.
    {
        auto brillance = [&] (double vel, double sensMod)
        {
            ReglagesFM r;
            r.algo = 2;
            r.ratio[0] = 1.0; r.ratio[1] = 3.0; r.ratio[3] = 1.0;
            r.niveau[0] = 1.0; r.niveau[1] = 0.9; r.niveau[2] = 0.0; r.niveau[3] = 0.0;
            for (int k = 0; k < kOperateurs; ++k)
            {
                r.tenue[k] = 1.0; r.decli[k] = 8.0;
                r.echelleNiveau[k] = 0.0; r.veloSens[k] = 0.0;
            }
            r.veloSens[1] = sensMod;      // seul le MODULATEUR suit la vitesse

            EtageFM e; e.prepare (fs);
            e.poseVelocite (r, vel);
            e.declenche (r, 220.0, 0.0);
            std::vector<double> y ((size_t) 16384);
            for (int i = 0; i < 16384; ++i) y[(size_t) i] = e.suivant (r);

            const int n = 16384;
            std::vector<std::complex<double>> b ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b[(size_t) i] = y[(size_t) i] * w;
            }
            fft (b);
            double fond = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fs / (double) n;
                const double e2 = std::norm (b[(size_t) k]);
                if (f < 400.0) fond += e2; else haut += e2;
            }
            return 10.0 * std::log10 (haut / fond);
        };
        const double faibleSans = brillance (0.2, 0.0), fortSans = brillance (1.0, 0.0);
        const double faibleAvec = brillance (0.2, 0.9), fortAvec = brillance (1.0, 0.9);
        std::printf ("    sans sensibilite : %.1f dB d'aigu a vel 0,2 contre %.1f a 1,0\n",
                     faibleSans, fortSans);
        std::printf ("    avec sensibilite : %.1f dB a vel 0,2 contre %.1f a 1,0\n",
                     faibleAvec, fortAvec);
        verifie ("sans sensibilite, la vitesse ne change pas le timbre",
                 std::abs (fortSans - faibleSans), 0.0, 0.5, "dB");
        verifie ("avec sensibilite, jouer fort rend plus brillant",
                 fortAvec - faibleAvec, 6.0, 100.0, "dB");
    }
}

// ---------------- 36. aucun octet cherche ne doit etre INERTE

// ------------------- 37. les trois macros de la page JOUER font quelque chose

// ---- 38. mono et legato en FM : la deuxieme note ne doit pas etre plus faible

// ---------------- 39. les quatre bus de couche valent exactement le total

// ------------------------- 40. le spectre dessine : un second materiau

// ------------------- 41. l'aller-retour d'une forme enregistree

// ------------------- 42. la touche et la velocite comme SOURCES

// ------------------- 43. les controleurs de jeu, et le glide

// ------------------- 44. le pont entre une destination et son bouton

// ------------------- 45. moduler ce qui est lu AU DECLENCHEMENT

// ------------------- 46. ce que coute UNE NOTE POSEE

// ------------------- 47. le reseau dit POURQUOI il n'est pas charge

// ------------------- 48. le nom d'un emplacement d'effet

static void testIdEmplacementFx()
{
    std::printf ("\n48. LE NOM DU PARAMETRE D'UN EMPLACEMENT D'EFFET\n");
    std::printf ("    (deux conventions cohabitent ; la confondre a rendu le\n"
                 "     rack master intouchable pendant une semaine)\n");

    // ⭐⭐ LE MASTER NE SE NOMME PAS COMME LES TRANCHES. Il porte ses
    // identifiants d'origine ; les tranches sont arrivees apres et ont pris un
    // prefixe. Les renommer aurait casse tous les presets enregistres.
    verifie ("le master s'appelle fx1",
             (double) (idEmplacementFx (kNbRacksFx - 1, 0) == "fx1"), 1.0, 1.0);
    verifie ("et fx10 pour le dernier emplacement",
             (double) (idEmplacementFx (kNbRacksFx - 1, 9) == "fx10"), 1.0, 1.0);
    verifie ("la tranche 1 s'appelle fx1_slot1",
             (double) (idEmplacementFx (0, 0) == "fx1_slot1"), 1.0, 1.0);
    verifie ("la tranche 4 s'appelle fx4_slot10",
             (double) (idEmplacementFx (3, 9) == "fx4_slot10"), 1.0, 1.0);

    // /!\ LE CONTROLE QUI COMPTE EST CELUI-LA : aucun nom de tranche ne doit
    // ressembler a un nom de master. C'est la confusion exacte qui a fait
    // rendre nullptr au minuteur - donc « emplacement vide », donc
    // reconstruction du rack a chaque battement.
    int collisions = 0;
    for (int t = 0; t < kNbRacksFx; ++t)
        for (int i = 0; i < kEmplacementsFx; ++i)
            for (int u = 0; u < kNbRacksFx; ++u)
                for (int j = 0; j < kEmplacementsFx; ++j)
                    if ((t != u || i != j)
                        && idEmplacementFx (t, i) == idEmplacementFx (u, j))
                        ++collisions;
    std::printf ("    %d rack(s) x %d emplacements, collisions : %d\n",
                 kNbRacksFx, kEmplacementsFx, collisions);
    verifie ("les cinquante noms sont deux a deux distincts",
             (double) collisions, 0.0, 0.0);

    // ⭐ Et un nom de master ne doit JAMAIS contenir « slot » : c'est le motif
    // qu'une composition a la main aurait produit.
    int fautifs = 0;
    for (int i = 0; i < kEmplacementsFx; ++i)
        if (idEmplacementFx (kNbRacksFx - 1, i).find ("slot") != std::string::npos)
            ++fautifs;
    verifie ("aucun nom de master ne contient « slot »", (double) fautifs, 0.0, 0.0);
}

static void testDiagnosticReseau()
{
    std::printf ("\n47. LE RESEAU DIT POURQUOI IL N'EST PAS CHARGE\n");
    std::printf ("    (« absent » et « perime » demandent deux gestes OPPOSES :\n"
                 "     aller le chercher, ou le reentrainer)\n");

    const std::string tmp = "romance_test_reseau.rnn";

    // ---- 1. absent
    {
        std::remove (tmp.c_str());
        Reseau r;
        verifie ("un fichier absent n'est pas charge", r.lit (tmp) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et il se declare A\ENT",
                 (double) (r.etat == Reseau::Etat::Absent), 1.0, 1.0);
    }

    // ---- 2. ce n'est pas un reseau
    {
        FILE* f = std::fopen (tmp.c_str(), "wb");
        std::fputs ("ceci n'est pas un reseau", f);
        std::fclose (f);
        Reseau r;
        verifie ("un fichier etranger est refuse", r.lit (tmp) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et il se declare comme tel",
                 (double) (r.etat == Reseau::Etat::PasUnReseau), 1.0, 1.0);
    }

    // ---- 3. ⭐⭐ LE CAS REEL DU 3 SEPTEMBRE : un reseau valide, mais entraine
    // sur un profil plus court. Il EXISTE, il fait 3,8 Mo, et il est refuse
    // pour un ecart de dimension que rien n'affichait nulle part.
    {
        FILE* f = std::fopen (tmp.c_str(), "wb");
        std::fwrite ("ROMNN" "\2", 1, 6, f);
        // /!\ LES DIMENSIONS SE CALCULENT, elles ne s'ecrivent pas en dur -
        // piege 13. `kOctetsProfil - 68` reproduit exactement l'ecart observe
        // (230 contre 298) sans figer ni l'un ni l'autre.
        const uint32_t a = (uint32_t) kTailleEntreeReseau, b = 384, c = 192,
                       d = (uint32_t) (kOctetsProfil - 68);
        std::fwrite (&a, 4, 1, f); std::fwrite (&b, 4, 1, f);
        std::fwrite (&c, 4, 1, f); std::fwrite (&d, 4, 1, f);
        std::fclose (f);

        Reseau r;
        verifie ("un reseau perime est refuse", r.lit (tmp) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et il se declare PERIME, pas absent",
                 (double) (r.etat == Reseau::Etat::FormatIncompatible), 1.0, 1.0);
        // ⭐ ET LE MESSAGE PORTE LES DEUX NOMBRES. « erreur » n'aide personne ;
        // « sortie 230, attendu 298 » dit quoi faire.
        const std::string m = r.diagnostic();
        std::printf ("    message rendu : %s\n", m.c_str());
        verifie ("le message nomme la taille lue",
                 (double) (m.find (std::to_string (kOctetsProfil - 68)) != std::string::npos),
                 1.0, 1.0);
        verifie ("et la taille attendue",
                 (double) (m.find (std::to_string (kOctetsProfil)) != std::string::npos),
                 1.0, 1.0);
    }

    // ---- 4. tronque : entete juste, poids incomplets
    {
        FILE* f = std::fopen (tmp.c_str(), "wb");
        std::fwrite ("ROMNN" "\2", 1, 6, f);
        const uint32_t a = (uint32_t) kTailleEntreeReseau, b = 4, c = 4,
                       d = (uint32_t) kOctetsProfil;
        std::fwrite (&a, 4, 1, f); std::fwrite (&b, 4, 1, f);
        std::fwrite (&c, 4, 1, f); std::fwrite (&d, 4, 1, f);
        const float z = 0.0f;
        std::fwrite (&z, 4, 1, f);          // un seul poids : trop court
        std::fclose (f);

        Reseau r;
        verifie ("un fichier tronque est refuse", r.lit (tmp) ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et il se declare TRONQUE",
                 (double) (r.etat == Reseau::Etat::Tronque), 1.0, 1.0);
        // /!\ ET IL N'EST PAS `pret` - un reseau a moitie lu rendrait des
        // predictions plausibles a partir de poids nuls.
        verifie ("et surtout il n'est pas pret", r.pret ? 1.0 : 0.0, 0.0, 0.0);
    }

    std::remove (tmp.c_str());
}

static void testCoutNoteOn()
{
    std::printf ("\n46. LE COUT D'UN NOTE-ON\n");
    std::printf ("    (le piege 28 l'avait mesure une fois, a la main, et le controle\n"
                 "     n'avait jamais ete ecrit - or la matrice s'applique desormais\n"
                 "     A CHAQUE note posee)\n");

    auto rom = romTest (40000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    // ⭐ UN BLOC DE 128 ECHANTILLONS A 44,1 kHz DURE 2,90 ms. C'est le budget
    // entier : tout ce qui depasse est un decrochage, donc un CLAQUEMENT que
    // l'utilisateur entend et qu'aucune mesure de rendu continu ne montre.
    const double kBudgetMs = 128.0 / 44100.0 * 1000.0;

    static const char* kNoms[] = { "ROM", "analogique", "FM", "table d'ondes" };
    const TypeMoteur kMot[] = { TypeMoteur::Rom, TypeMoteur::Analogique,
                                TypeMoteur::FM, TypeMoteur::Table };
    double pire = 0.0;
    for (int m = 0; m < 4; ++m)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = kMot[m];
        p.couche[0].largeur = 0.6;          // unisson : le cas le plus cher
        p.instab.actif = true;
        // ⭐ ET AVEC LA MATRICE CHARGEE : seize cables, dont un vers une
        // destination generique - c'est ce qui declenche la copie des reglages
        // de couche a chaque note. Mesurer a vide ne mesurerait pas le chemin
        // qu'on vient de modifier.
        for (int i = 0; i < kNbRoutages; ++i)
            p.routages[i] = { kIdSourceVelo, destinationDeParam ("att"), 0, 0.5 };

        // ⭐⭐ LE MINIMUM DE PLUSIEURS SERIES, PAS UNE MESURE UNIQUE.
        //
        // /!\ MA PREMIERE VERSION MESURAIT UNE SEULE FOIS ET COMPARAIT A UN
        // SEUIL ABSOLU. Or la FM vit a ~90 % du budget, et le temps MURAL
        // depend de ce que la machine fait par ailleurs. Releves successifs du
        // meme code, sans qu'une ligne ait change :
        //
        //     2,242   2,330   2,513   2,672   2,751   2,786   2,895   3,090
        //
        // pour un budget de 2,90. Le controle passait ou echouait selon la
        // charge - c'est-a-dire au hasard. Un test qui echoue au hasard est
        // PIRE qu'un test absent : il apprend a ignorer le rouge, et le jour
        // ou il a raison plus personne ne le lit.
        //
        // On prend donc le MINIMUM de plusieurs series - la seule estimation
        // qui mesure le code plutot que la concurrence - et on AFFICHE le
        // pire, pour qu'une vraie derive reste visible.
        double best = 1e30, worst = 0.0;
        for (int essai = 0; essai < 5; ++essai)
        {
            Moteur mo; mo.prepare (44100.0, 16);
            const auto t0 = std::chrono::steady_clock::now();
            const int kRep = 200;
            for (int r = 0; r < kRep; ++r)
            {
                mo.noteOn (p, 40 + (r % 24), 100, roms, tl);
                for (int i = 0; i < 128; ++i) { double a, b; mo.suivant (p, a, b); }
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count()
                                / (double) kRep;
            best = std::min (best, ms); worst = std::max (worst, ms);
        }
        std::printf ("    %-14s note-on + 1 bloc : %6.3f ms  (jusqu'a %6.3f, budget %.2f)\n",
                     kNoms[m], best, worst, kBudgetMs);
        pire = std::max (pire, best);
    }

    // /!\ ON BORNE LE PIRE, PAS LA MOYENNE. Un moteur sur quatre qui decroche
    // decroche quand meme - et c'est exactement ce qui etait arrive a la table
    // d'ondes (34,4 ms pour un budget de 2,90).
    verifie ("aucun moteur ne depasse le budget d'un bloc", pire, 0.0, kBudgetMs);
    // ⚠️ ET ON DIT QUE LA MARGE EST MINCE, meme quand c'est vert. La FM tient,
    // mais de peu, avec seize voix, l'unisson et une matrice chargee. Le
    // prochain ajout sur son chemin de voix decrochera : mieux vaut le lire
    // ici que l'entendre en jouant.
    std::printf ("    marge : le moteur le plus cher consomme %.0f %% du budget\n",
                 100.0 * pire / kBudgetMs);
}

static void testModulationAuDeclenchement()
{
    std::printf ("\n45. MODULER CE QUI EST LU AU DECLENCHEMENT DE LA NOTE\n");
    std::printf ("    (attaque, chute, tenue, relachement, largeur d'unisson :\n"
                 "     cinq entrees du menu qui ne faisaient RIEN)\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    const int dAtt = destinationDeParam ("att");
    verifie ("l'attaque est bien une destination", (double) dAtt, 8.0,
             (double) (kNbDestinations - 1));

    // ⭐⭐ LA SOURCE EST LA VELOCITE, PAS UNE ENVELOPPE - et c'est tout le
    // piege. Une enveloppe vaut ZERO a l'instant du note-on : elle ne peut
    // structurellement pas moduler ce qui est lu a cet instant-la. Mon banc
    // `mes_impact` s'en servait, et il a donc rendu 0,000 dB AVANT ET APRES la
    // correction - un banc incapable de voir l'effet qu'on lui demande de
    // juger (piege 42, troisieme fois).
    auto joue = [&] (int velo, double dose)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::Analogique;
        p.couche[0].va.melange = 0.0; p.couche[0].va.sub = 0.0;
        p.couche[0].va.bruit = 0.0;
        p.couche[0].attaque = 0.003;          // tres court : la modulation l'allonge
        p.couche[0].decli = 8.0; p.couche[0].tenue = 1.0;
        p.couche[0].largeur = 0.0; p.couche[0].desaccordCents = 0.0;
        p.couche[0].coupureRel = 60.0;
        // /!\ ON NEUTRALISE LA REPONSE A LA VELOCITE DEJA CABLEE EN DUR, sinon
        // on mesurerait `veloAttaque` et l'on croirait avoir prouve le cable.
        p.couche[0].veloNiveau = 0.0; p.couche[0].veloCoupure = 0.0;
        p.couche[0].veloAttaque = 0.0; p.couche[0].veloDepart = 0.0;
        p.instab.actif = false;
        p.routages[0] = { kIdSourceVelo, dAtt, 0, dose };

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, 57, velo, roms, tl);
        std::vector<double> y ((size_t) 8000);
        for (int i = 0; i < 8000; ++i)
        { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
        return y;
    };

    // On mesure l'energie des 15 premieres millisecondes : une attaque
    // allongee la vide, une attaque courte la remplit.
    auto debut = [] (const std::vector<double>& y)
    {
        double e = 0.0;
        for (int i = 0; i < 660; ++i) e += y[(size_t) i] * y[(size_t) i];
        return std::sqrt (e / 660.0);
    };

    const double sansCable  = debut (joue (127, 0.0));
    const double doux       = debut (joue (1,   1.0));
    const double fort       = debut (joue (127, 1.0));
    std::printf ("    sans cable, velocite 127      : %.4f\n", sansCable);
    std::printf ("    cable +1, velocite 1 (attaque courte) : %.4f\n", doux);
    std::printf ("    cable +1, velocite 127 (attaque longue) : %.4f\n", fort);

    // ⭐ LE CONTROLE EST UN RAPPORT ENTRE DEUX VELOCITES, PAS UN NIVEAU. Le
    // niveau absolu depend du patch ; ce qui prouve le cable, c'est que jouer
    // FORT allonge l'attaque - donc VIDE le debut - alors que jouer doux ne
    // change rien.
    verifie ("frapper fort allonge l'attaque", fort / std::max (1e-9, doux),
             0.0, 0.75);
    verifie ("frapper doux ne module presque pas",
             doux / std::max (1e-9, sansCable), 0.90, 1.10);

    // /!\ ET LE TEMOIN : sans cable, la velocite ne doit RIEN faire a
    // l'attaque. Sinon on mesurerait une reponse deja presente ailleurs.
    const double temoinDoux = debut (joue (1, 0.0));
    std::printf ("    temoin sans cable, velocite 1 : %.4f\n", temoinDoux);
    verifie ("sans cable, la velocite ne touche pas l'attaque",
             temoinDoux / std::max (1e-9, sansCable), 0.98, 1.02);
}

static void testPontFacade()
{
    std::printf ("\n44. LE PONT DESTINATION <-> BOUTON DE FACADE\n");
    std::printf ("    (sans lui, un bouton ne peut pas savoir qu'il est module,\n"
                 "     ni par qui - donc pas d'anneau, pas de glisser-deposer)\n");

    // ---- 1. l'aller-retour. Il prouve AUSSI qu'aucun identifiant n'est
    // ecrit deux fois : deux destinations qui pointeraient vers le meme bouton
    // rendraient le meme indice, donc l'une des deux echouerait ici.
    {
        int couverts = 0, faux = 0;
        for (int i = 1; i < kNbDestinations; ++i)
        {
            const char* id = idParamDestination (i);
            if (id[0] == 0) continue;
            ++couverts;
            if (destinationDeParam (id) != i) ++faux;
        }
        std::printf ("    destinations avec un bouton : %d / %d\n",
                     couverts, kNbDestinations - 1);
        verifie ("l'aller-retour est exact", (double) faux, 0.0, 0.0);
        // /!\ ET IL FAUT QUE LA COUVERTURE SOIT HAUTE. Une table ou la moitie
        // des entrees seraient vides passerait le test precedent sans rien
        // relier : le controle qui compte est celui-la.
        verifie ("et la couverture est presque complete", (double) couverts,
                 34.0, (double) (kNbDestinations - 1));
    }
    {
        int couverts = 0, faux = 0;
        for (int i = 1; i < kNbDestGlobales; ++i)
        {
            const char* id = idParamDestGlobale (i);
            if (id[0] == 0) continue;
            ++couverts;
            if (destGlobaleDeParam (id) != i) ++faux;
        }
        std::printf ("    destinations globales avec un bouton : %d / %d\n",
                     couverts, kNbDestGlobales - 1);
        verifie ("l'aller-retour global est exact", (double) faux, 0.0, 0.0);
        verifie ("et toutes sont couvertes", (double) couverts,
                 (double) (kNbDestGlobales - 1), (double) (kNbDestGlobales - 1));
    }

    // ---- 2. ⭐⭐ `valeurModulee` DIT LA MEME CHOSE QUE LE MOTEUR.
    // C'est le controle qui a une vraie chance d'echouer un jour : les deux
    // formules vivent a deux endroits, et le jour ou l'une bouge, l'anneau
    // dessinerait une excursion que l'oreille n'entend pas.
    {
        int ecarts = 0;
        double pire = 0.0;
        for (int i = 8; i < kNbDestinations; ++i)
        {
            const Destination& d = tableDestinations()[i];
            if (d.champ == nullptr) continue;
            for (double x : { -0.7, -0.2, 0.3, 0.9 })
            {
                ReglagesCouche r;
                const double base = *d.champ (r);
                appliqueDestination (i, x, r);       // le chemin du moteur
                const double moteur = *d.champ (r);
                const double vu = valeurModulee (i, x, base);   // celui de l'anneau
                const double e = std::abs (moteur - vu);
                if (e > pire) pire = e;
                if (e > 1e-9) ++ecarts;
            }
        }
        std::printf ("    ecart maximal moteur / affichage : %.3g\n", pire);
        verifie ("l'anneau montre ce que le moteur calcule", (double) ecarts, 0.0, 0.0);
    }

    // ---- 3. et les SEPT premieres, qui ne passent pas par un champ : on les
    // mesure sur le son plutot que de recopier une constante d'un fichier a
    // l'autre. `Dest::Hauteur` a pleine dose doit valoir 24 demi-tons, soit
    // exactement DEUX OCTAVES.
    {
        auto rom = romTest (12000);
        const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

        auto joue = [&] (double dose)
        {
            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0].active = true;
            p.couche[0].moteur = TypeMoteur::Analogique;
            p.couche[0].va.melange = 0.0; p.couche[0].va.sub = 0.0;
            p.couche[0].va.bruit = 0.0;
            p.couche[0].attaque = 0.002; p.couche[0].decli = 8.0;
            p.couche[0].tenue = 1.0; p.couche[0].largeur = 0.0;
            p.couche[0].desaccordCents = 0.0; p.couche[0].coupureRel = 60.0;
            p.instab.actif = false;
            // macro 1 a fond -> hauteur : une source CONSTANTE, donc une
            // transposition franche qu'on peut mesurer a la frequence exacte.
            p.macros[0] = 1.0;
            p.routages[0] = { kNbMods + 1, (int) Dest::Hauteur, 0, dose };
            Moteur m; m.prepare (44100.0, 4);
            m.noteOn (p, 45, 100, roms, tl);
            std::vector<double> y ((size_t) 20000);
            for (int i = 0; i < 20000; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            return y;
        };
        const double f0 = 440.0 * std::pow (2.0, (45.0 - 69.0) / 12.0);   // 110 Hz
        auto nu = joue (0.0), plein = joue (1.0);
        const double aNu    = ampA (nu,    4000, 8192, f0, 44100.0);
        const double aPlein = ampA (plein, 4000, 8192, f0 * 4.0, 44100.0);
        const double temoin = ampA (plein, 4000, 8192, f0, 44100.0);
        std::printf ("    dose 0 a %.1f Hz : %.4f\n", f0, aNu);
        std::printf ("    dose 1 a %.1f Hz : %.4f  (deux octaves)\n", f0 * 4.0, aPlein);
        std::printf ("    dose 1 a %.1f Hz : %.4f  (temoin)\n", f0, temoin);
        verifie ("pleine dose vaut bien deux octaves",
                 aPlein / std::max (1e-9, aNu), 0.4, 2.5);
        verifie ("et l'ancienne hauteur s'est videe",
                 temoin / std::max (1e-9, aNu), 0.0, 0.25);
        // et l'affichage dit la meme chose que ce qu'on vient d'entendre
        verifie ("valeurModulee est d'accord",
                 valeurModulee ((int) Dest::Hauteur, 1.0, 0.0), 24.0, 24.0);
    }

    // ---- 3 bis. le rack vise par un cable vers un effet
    // /!\ 0 = MASTER, ET C'EST L'INVERSE DE LA MEME VALEUR SUR UNE
    // DESTINATION DE COUCHE (ou 0 = toutes). L'asymetrie est ce qui fait que
    // les patchs enregistres avant les racks de tranche se relisent a
    // l'identique : ils portent tous couche = 0 et visaient tous le master.
    verifie ("couche 0 vise le master", (double) rackDeRoutage (0),
             (double) kNbCouches, (double) kNbCouches);
    verifie ("couche 1 vise le rack 0", (double) rackDeRoutage (1), 0.0, 0.0);
    verifie ("couche 4 vise le rack 3", (double) rackDeRoutage (4), 3.0, 3.0);
    // une valeur folle ne doit pas sortir du tableau
    verifie ("une couche hors plage retombe sur le master",
             (double) rackDeRoutage (99), (double) kNbCouches, (double) kNbCouches);
    verifie ("et une negative aussi",
             (double) rackDeRoutage (-3), (double) kNbCouches, (double) kNbCouches);

    // ---- 4. seize cables, et le profil n'en sait rien
    verifie ("il y a seize cables", (double) kNbRoutages, 16.0, 16.0);
    verifie ("et le profil en encode toujours autant",
             (double) kModsProfil, 1.0, 8.0);
}

static void testControleursDeJeu()
{
    std::printf ("\n43. BEND, MOLETTE, PRESSION, GLIDE\n");
    std::printf ("    (le depot ne contenait AUCUN isPitchWheel, isController ni\n"
                 "     isChannelPressure : ces trois messages arrivaient et etaient\n"
                 "     jetes)\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    auto patchNu = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::Analogique;
        p.couche[0].va.melange = 0.0;          // une seule dent de scie
        p.couche[0].va.sub = 0.0; p.couche[0].va.bruit = 0.0;
        p.couche[0].attaque = 0.002; p.couche[0].decli = 8.0;
        p.couche[0].tenue = 1.0;     p.couche[0].largeur = 0.0;
        p.couche[0].desaccordCents = 0.0;
        p.couche[0].coupureRel = 60.0;
        p.couche[0].veloNiveau = 0.0; p.couche[0].veloCoupure = 0.0;
        p.couche[0].suiviClavier = 0.0;
        p.instab.actif = false;
        return p;
    };

    // ⭐ ON MESURE LA HAUTEUR PAR CORRELATION A LA FREQUENCE ATTENDUE (`ampA`),
    // jamais par comptage de passages par zero - piege 10, quatre fois paye.
    auto joue = [&] (const ReglagesPatch& p, int note, int n)
    {
        Moteur m; m.prepare (44100.0, 8);
        m.noteOn (p, note, 100, roms, tl);
        std::vector<double> y ((size_t) n);
        for (int i = 0; i < n; ++i)
        { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
        return y;
    };

    // ---- 1. le bend est cable en dur sur la hauteur
    {
        auto p = patchNu();
        p.bendPortee = 2.0;
        const double f0 = 440.0;                    // la3 = note 69
        auto neutre = joue (p, 69, 20000);
        p.bend = 1.0;
        auto haut = joue (p, 69, 20000);

        const double aNeutre = ampA (neutre, 4000, 8192, f0, 44100.0);
        // +2 demi-tons
        const double fBend = f0 * std::pow (2.0, 2.0 / 12.0);
        const double aBendIci = ampA (haut, 4000, 8192, fBend, 44100.0);
        // /!\ ET LE TEMOIN : a la frequence D'ORIGINE il ne doit plus rien y
        // avoir. Sans lui, une mesure qui trouve de l'energie partout
        // « confirmerait » n'importe quel bend.
        const double aBendOrigine = ampA (haut, 4000, 8192, f0, 44100.0);

        std::printf ("    sans bend, a 440 Hz   : %.4f\n", aNeutre);
        std::printf ("    bend +1,  a 493,9 Hz  : %.4f\n", aBendIci);
        std::printf ("    bend +1,  a 440 Hz    : %.4f  (temoin)\n", aBendOrigine);
        verifie ("sans cable, le bend deplace la hauteur",
                 aBendIci / std::max (1e-9, aNeutre), 0.5, 2.0);
        verifie ("et l'ancienne hauteur se vide",
                 aBendOrigine / std::max (1e-9, aNeutre), 0.0, 0.25);
    }

    // ---- 2. la molette ne fait RIEN sans cable, et quelque chose avec
    {
        auto p = patchNu();
        p.molette = 1.0;
        const double sansCable = rms (joue (p, 60, 16000));
        auto q = patchNu();
        const double zero = rms (joue (q, 60, 16000));
        // ⭐ LE CONTROLE QUI COMPTE EST CELUI-LA. Une molette qui agirait sans
        // qu'on l'ait branchee serait un synthe qui change de son quand on
        // touche a un objet dont on n'a rien demande.
        std::printf ("    molette a fond, debranchee : %.4f contre %.4f\n",
                     sansCable, zero);
        verifie ("la molette debranchee ne fait rien",
                 sansCable / std::max (1e-9, zero), 0.999, 1.001);

        p.routages[0] = { kIdSourceMolette, (int) Dest::Niveau, 0, 1.0 };
        const double cablee = rms (joue (p, 60, 16000));
        verifie ("branchee, elle agit", cablee / std::max (1e-9, zero), 1.05, 20.0);
    }

    // ---- 3. la pression, de canal et polyphonique
    {
        auto p = patchNu();
        p.routages[0] = { kIdSourcePression, (int) Dest::Niveau, 0, 1.0 };
        const double repos = rms (joue (p, 60, 16000));
        p.pression = 1.0;
        const double canal = rms (joue (p, 60, 16000));
        std::printf ("    pression de canal : %.4f contre %.4f au repos\n",
                     canal, repos);
        verifie ("la pression de canal module", canal / std::max (1e-9, repos),
                 1.05, 20.0);

        // /!\ L'AFTERTOUCH POLYPHONIQUE NE DOIT TOUCHER QU'UNE VOIX. C'est
        // toute sa raison d'etre : s'il modulait tout, ce serait une pression
        // de canal avec plus d'etapes.
        auto q = patchNu();
        q.routages[0] = { kIdSourcePression, (int) Dest::Niveau, 0, 1.0 };
        Moteur m; m.prepare (44100.0, 8);
        m.noteOn (q, 60, 100, roms, tl);
        m.noteOn (q, 67, 100, roms, tl);
        m.posePressionPoly (60, 1.0);          // une seule des deux
        std::vector<double> deux ((size_t) 16000);
        for (int i = 0; i < 16000; ++i)
        { double a, b; m.suivant (q, a, b); deux[(size_t) i] = a; }

        Moteur m2; m2.prepare (44100.0, 8);
        m2.noteOn (q, 60, 100, roms, tl);
        m2.noteOn (q, 67, 100, roms, tl);
        std::vector<double> ref ((size_t) 16000);
        for (int i = 0; i < 16000; ++i)
        { double a, b; m2.suivant (q, a, b); ref[(size_t) i] = a; }

        const double r = rms (deux) / std::max (1e-9, rms (ref));
        std::printf ("    aftertouch sur UNE des deux notes : x%.3f\n", r);
        verifie ("l'aftertouch poly agit", r, 1.02, 10.0);
        // et il n'agit pas autant que s'il touchait les deux
        verifie ("mais moins que sur les deux", r, 1.02, 1.60);
    }

    // ---- 4. le glide
    {
        // ⭐⭐ ON MESURE LA HAUTEUR JUSTE APRES LE SAUT. Avec glide, la note
        // doit encore etre PRES DE L'ANCIENNE ; sans glide, deja sur la
        // nouvelle. Mesurer a la fin ne prouverait rien : les deux finissent
        // au meme endroit.
        auto mesure = [&] (double glide)
        {
            auto p = patchNu();
            p.mode = ModeVoix::Legato;
            p.glide = glide;
            Moteur m; m.prepare (44100.0, 8);
            m.noteOn (p, 60, 100, roms, tl);
            std::vector<double> y ((size_t) 70000);
            for (int i = 0; i < 12000; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            m.noteOn (p, 72, 100, roms, tl);        // une octave au-dessus
            for (int i = 12000; i < 70000; ++i)
            { double a, b; m.suivant (p, a, b); y[(size_t) i] = a; }
            return y;
        };
        const double fBas  = 440.0 * std::pow (2.0, (60.0 - 69.0) / 12.0);
        const double fHaut = 440.0 * std::pow (2.0, (72.0 - 69.0) / 12.0);

        auto sans = mesure (0.0), avec = mesure (1.0);   // 1 s par octave
        // fenetre courte juste apres le saut : 4096 points = 93 ms
        const double sansHaut = ampA (sans, 12500, 4096, fHaut, 44100.0);
        const double avecHaut = ampA (avec, 12500, 4096, fHaut, 44100.0);
        const double avecBas  = ampA (avec, 12500, 4096, fBas,  44100.0);
        std::printf ("    sans glide, 93 ms apres le saut : %.4f a la note visee\n",
                     sansHaut);
        std::printf ("    avec glide 1 s/octave           : %.4f a la note visee,\n"
                     "                                      %.4f a la note de depart\n",
                     avecHaut, avecBas);
        verifie ("sans glide, la note vise juste", sansHaut, 0.01, 2.0);
        verifie ("avec glide, elle n'y est pas encore",
                 avecHaut / std::max (1e-9, sansHaut), 0.0, 0.50);
        verifie ("elle est encore pres du depart",
                 avecBas / std::max (1e-9, avecHaut), 1.0, 1e6);

        // ⭐⭐ ET ELLE ARRIVE - mais il faut LAISSER LE TEMPS. Ma premiere
        // version mesurait 520 ms apres un saut d'une OCTAVE a 1 s/octave : le
        // glide etait a mi-chemin, et le banc l'accusait de ne jamais arriver.
        // Le banc ne pouvait pas voir ce qu'il jugeait - piege 42, encore.
        const double finAvec = ampA (avec, 62000, 4096, fHaut, 44100.0);
        std::printf ("    et 1,13 s plus tard : %.4f a la note visee\n", finAvec);
        verifie ("le glide arrive", finAvec / std::max (1e-9, sansHaut), 0.5, 2.0);
    }

    // ---- 5. les noms
    verifie ("BEND se nomme",
             (double) (std::string (nomSource (kIdSourceBend)) == "BEND"), 1.0, 1.0);
    verifie ("MOLET se nomme",
             (double) (std::string (nomSource (kIdSourceMolette)) == "MOLET"), 1.0, 1.0);
    verifie ("PRESS se nomme",
             (double) (std::string (nomSource (kIdSourcePression)) == "PRESS"), 1.0, 1.0);
}

static void testSourcesDeJeu()
{
    std::printf ("\n42. LA TOUCHE ET LA VELOCITE SONT DES SOURCES DE MATRICE\n");

    auto rom = romTest (12000);
    const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { (int) rom.size(), 0, 0, 0 };

    // ⭐⭐ ON APPARIE SUR LA MEME NOTE. Comparer directement un la0 et un la5
    // ne dirait rien : deux notes n'ont pas le meme niveau quoi qu'il arrive.
    // Ce qu'on mesure ici, c'est ce que le CABLE ajoute, a note constante.
    auto rms = [&] (int note, int velo, int source, double dose)
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::Analogique;
        p.couche[0].attaque = 0.002; p.couche[0].decli = 8.0;
        p.couche[0].tenue = 1.0;     p.couche[0].largeur = 0.0;
        p.couche[0].desaccordCents = 0.0;
        // /!\ ON NEUTRALISE LES REPONSES CABLEES EN DUR. Sans ca on mesurerait
        // `veloNiveau` - qui existe deja - et l'on croirait avoir prouve que le
        // nouveau cable marche.
        p.couche[0].veloNiveau = 0.0;  p.couche[0].veloCoupure = 0.0;
        p.couche[0].veloAttaque = 0.0; p.couche[0].veloDepart = 0.0;
        p.couche[0].suiviClavier = 0.0;
        p.instab.actif = false;
        p.routages[0] = { source, (int) Dest::Niveau, 0, dose };

        Moteur m; m.prepare (44100.0, 4);
        m.noteOn (p, note, velo, roms, tl);
        double e = 0.0;
        for (int i = 0; i < 20000; ++i)
        { double a, b; m.suivant (p, a, b); e += a * a; }
        return std::sqrt (e / 20000.0);
    };

    // ---- la touche : BIPOLAIRE, centree sur le do du milieu
    {
        const int T = kIdSourceTouche;
        const double bas  = rms (36, 100, T, 1.0) / std::max (1e-9, rms (36, 100, 0, 0.0));
        const double haut = rms (84, 100, T, 1.0) / std::max (1e-9, rms (84, 100, 0, 0.0));
        std::printf ("    note 36 : le cable multiplie le niveau par %.3f\n", bas);
        std::printf ("    note 84 : par %.3f\n", haut);
        // ⭐ LE CONTROLE N'EST PAS « ca bouge » MAIS « ca bouge DANS LES DEUX
        // SENS ». Une source qui n'irait que vers le haut ne serait pas un
        // suivi de clavier : le grave doit fermer autant que l'aigu ouvre.
        verifie ("sous le do du milieu, la touche RETIRE", bas, 0.05, 0.95);
        verifie ("au-dessus, elle AJOUTE",                 haut, 1.05, 20.0);
    }

    // ---- la velocite : UNIPOLAIRE, donc muette a frappe nulle
    {
        const int V = kIdSourceVelo;
        const double nu   = rms (60, 100, 0, 0.0);
        const double faib = rms (60,   1, V, 1.0) / std::max (1e-9, nu);
        const double fort = rms (60, 127, V, 1.0) / std::max (1e-9, nu);
        std::printf ("    velocite 1   : %.3f\n", faib);
        std::printf ("    velocite 127 : %.3f\n", fort);
        // /!\ A FRAPPE NULLE LA MODULATION DOIT ETRE NULLE, pas « au minimum ».
        // Une velocite centree comme un LFO ferait qu'une frappe douce module
        // deja a fond dans le negatif - le patch serait injouable, et c'est
        // exactement la raison pour laquelle les macros sont unipolaires.
        verifie ("a frappe nulle, la velocite ne module rien", faib, 0.95, 1.05);
        verifie ("a pleine frappe, elle module",               fort, 1.05, 20.0);
    }

    // ---- et le controle qui compte : DEBRANCHEES, elles ne font rien
    {
        const double a = rms (36, 20,  0, 0.0);
        const double b = rms (84, 120, 0, 0.0);
        // On ne compare pas a et b entre eux (deux notes differentes), on
        // verifie seulement qu'aucun des deux n'est eteint : si le cable a zero
        // laissait un son muet, les rapports ci-dessus seraient du bruit.
        verifie ("sans cable, le grave sonne",  a, 0.005, 2.0);
        verifie ("sans cable, l'aigu sonne",    b, 0.005, 2.0);
    }

    // ---- les noms, parce que trois endroits les ecrivaient a la main
    verifie ("la source 13 se nomme",
             (double) (std::string (nomSource (kIdSourceTouche)) == "TOUCHE"), 1.0, 1.0);
    verifie ("la source 14 se nomme",
             (double) (std::string (nomSource (kIdSourceVelo)) == "VELO"), 1.0, 1.0);
    verifie ("et on ne deborde pas de la table",
             (double) (std::string (nomSource (kNbSources + 1)) == "-"), 1.0, 1.0);
}

static void testForme()
{
    std::printf ("\n41. LE FORMAT DES FORMES ENREGISTREES\n");
    std::printf ("    (/!\\ un format qu'on ne peut pas tester est un format qu'on\n"
                 "     decouvre casse le jour ou l'on recharge - et ce jour-la,\n"
                 "     ce qui est perdu l'est deja)\n");

    DessinAB d;
    d.a.nCellules = d.b.nCellules = 32;
    d.a.bits = d.b.bits = 5;
    d.bActif = true;
    d.a.pose (Dessin::Forme::Cloche);
    d.b.pose (Dessin::Forme::Impaires);

    const std::string t = formeVersTexte (d);
    DessinAB lu;
    verifie ("une forme se relit", formeDepuisTexte (t, lu) ? 1.0 : 0.0, 1.0, 1.0);

    // ⭐⭐ BIT A BIT. Un format qui rend « presque » la meme forme est un format
    // FAUX : l'utilisateur enregistre un son et en recharge un autre.
    int differents = 0;
    for (int i = 0; i < kCellulesMax; ++i)
    {
        if (lu.a.cellule[i] != d.a.cellule[i]) ++differents;
        if (lu.b.cellule[i] != d.b.cellule[i]) ++differents;
    }
    std::printf ("    cellules differentes apres aller-retour : %d\n", differents);
    verifie ("elle est identique, cellule par cellule", (double) differents, 0.0, 0.0);
    verifie ("la finesse survit", (double) lu.a.nCellules, 32.0, 32.0);
    verifie ("la profondeur survit", (double) lu.a.bits, 5.0, 5.0);
    verifie ("le drapeau de morph survit", lu.bActif ? 1.0 : 0.0, 1.0, 1.0);

    // /!\ UN FICHIER ETRANGER DOIT ETRE REFUSE, PAS LU A MOITIE. Et surtout il
    // ne doit RIEN modifier : un chargement rate qui laisse un dessin a moitie
    // remplace donne un son qu'on n'a jamais fabrique et qu'on ne sait pas
    // reproduire.
    {
        DessinAB avant = lu, apres = lu;
        const bool ok = formeDepuisTexte ("ceci n'est pas une forme\n", apres);
        int bouge = 0;
        for (int i = 0; i < kCellulesMax; ++i)
            if (apres.a.cellule[i] != avant.a.cellule[i]) ++bouge;
        verifie ("un texte etranger est refuse", ok ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et il ne modifie rien", (double) bouge, 0.0, 0.0);
    }
    {
        // entete juste, contenu tronque : le cas le plus dangereux
        DessinAB avant = lu, apres = lu;
        const bool ok = formeDepuisTexte ("ROMANCE-FORME 1\ncellules 32\nA 00ff\n", apres);
        int bouge = 0;
        for (int i = 0; i < kCellulesMax; ++i)
            if (apres.a.cellule[i] != avant.a.cellule[i]) ++bouge;
        verifie ("un fichier tronque est refuse", ok ? 1.0 : 0.0, 0.0, 0.0);
        verifie ("et lui non plus ne modifie rien", (double) bouge, 0.0, 0.0);
    }

    // ⭐ Et les valeurs aberrantes sont BORNEES, pas recopiees telles quelles.
    {
        DessinAB f;
        std::string mauvais = "ROMANCE-FORME 1\ncellules 9999\nbits 99\nA ";
        for (int i = 0; i < kCellulesMax; ++i) mauvais += "80";
        mauvais += "\n";
        verifie ("un fichier aux valeurs folles se lit quand meme",
                 formeDepuisTexte (mauvais, f) ? 1.0 : 0.0, 1.0, 1.0);
        verifie ("mais la finesse est bornee", (double) f.a.nCellules,
                 (double) kCellulesMin, (double) kCellulesMax);
        verifie ("et la profondeur aussi", (double) f.a.bits,
                 (double) kBitsMin, (double) kBitsMax);
    }
}

static void testSpectre()
{
    std::printf ("\n40. LE SPECTRE DESSINE\n");
    std::printf ("    (/!\\ il existe parce que l'etalon a mesure que le poste\n"
                 "     le plus cher du format etait tout ce qui n'est PAS\n"
                 "     harmonique : bruit, inharmonicite, transitoires)\n");

    const double fs = 44100.0;
    const int n = 22050;

    auto bande = [fs] (const std::vector<double>& y, double fa, double fb)
    {
        const int N = 16384;
        if ((int) y.size() < N) return -200.0;
        std::vector<std::complex<double>> b ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) N);
            b[(size_t) i] = y[(size_t) i] * w;
        }
        fft (b);
        double e = 1e-30;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double f = (double) k * fs / (double) N;
            if (f >= fa && f < fb) e += std::norm (b[(size_t) k]);
        }
        return 10.0 * std::log10 (e);
    };

    // --- 1. ⭐⭐ UNE BANDE DESSINEE SORT LA OU ON L'A DESSINEE.
    //
    // /!\ C'EST LE CONTROLE FONDATEUR. Si l'energie n'atterrit pas dans la
    // bande visee, tout le reste - la pente, l'enveloppe, la recherche - juge
    // un mecanisme qui ne fait pas ce qu'il annonce.
    {
        std::vector<double> y;
        for (int cible : { 12, 30, 48 })
        {
            Spectre sp;
            for (int i = 0; i < kBandesSpectre; ++i) sp.bande[i] = 0;
            sp.bande[cible] = 255;
            sp.tenue = 1.0;                       // on isole la frequence
            rendSpectre (sp, y, fs, n, 7u);

            const double fc = Spectre::frequence (cible, fs);
            const double dedans = bande (y, fc * 0.75, fc * 1.33);
            const double dehors = std::max (bande (y, 20.0, fc * 0.5),
                                            bande (y, fc * 2.0, 20000.0));
            std::printf ("    bande %2d (%6.0f Hz) : dedans %+6.1f dB, "
                         "ailleurs %+6.1f dB\n", cible, fc, dedans, dehors);
            verifie ("l'energie tombe dans la bande dessinee",
                     dedans - dehors, 12.0, 200.0, "dB");
        }
    }

    // --- 2. ⭐ UNE BANDE LARGE DONNE DU BRUIT, UNE ETROITE UN PARTIEL.
    //
    // C'est ce qui permet au meme mecanisme de decrire les deux, et c'est la
    // raison d'etre du modele : notre recette sait faire des partiels, pas du
    // bruit de bande.
    {
        auto platitude = [&] (int b0, int b1)
        {
            Spectre sp;
            for (int i = 0; i < kBandesSpectre; ++i)
                sp.bande[i] = (i >= b0 && i <= b1) ? 255 : 0;
            sp.tenue = 1.0;
            std::vector<double> y;
            rendSpectre (sp, y, fs, n, 3u);
            // rapport entre l'energie de la bande la plus forte et la moyenne :
            // faible = plat (du bruit), eleve = pointu (un partiel)
            // /!\ On balaye une largeur FIXE autour de la zone. Balayer de f0
            // a f1 ne donne qu UN point quand b0 == b1 - et un maximum egale sa
            // propre moyenne, donc 0,0 dB quoi qu il arrive.
            const double f0 = Spectre::frequence (b0, fs) * 0.85;
            const double f1 = Spectre::frequence (b1, fs) * 1.18;
            double crete = -300.0, somme = 0.0; int nb = 0;
            for (double f = f0; f < f1; f *= 1.03)
            {
                const double e = bande (y, f * 0.97, f * 1.03);
                crete = std::max (crete, e); somme += e; ++nb;
            }
            return crete - somme / std::max (1, nb);
        };
        const double etroite = platitude (40, 40);
        const double large   = platitude (30, 50);
        std::printf ("    ecart crete/moyenne : bande etroite %.1f dB, "
                     "bande large %.1f dB\n", etroite, large);
        verifie ("une bande large est PLATE (du bruit)", large, 0.0, 12.0, "dB");
    }

    // --- 3. ⭐⭐ LA PENTE : LES BANDES HAUTES S'ETEIGNENT LES PREMIERES.
    //
    // C'est ce qui remplace les 64 enveloppes que l'etalon aurait voulues -
    // deux octets au lieu de deux cent cinquante. Elle doit donc AGIR.
    {
        auto restant = [&] (double pente)
        {
            Spectre sp;
            for (int i = 0; i < kBandesSpectre; ++i) sp.bande[i] = 0;
            sp.bande[16] = 255;                   // une bande grave
            sp.bande[52] = 255;                   // une bande aigue
            sp.tenue = 0.0; sp.chute = 0.30; sp.pente = pente;
            std::vector<double> y;
            // /!\ ASSEZ LONG POUR QUE LA FENETRE TARDIVE EXISTE. Ma premiere
            // version rendait 22 050 echantillons et analysait a partir du
            // 11 000e : 11 050 points pour une fenetre de 16 384. La mesure
            // rendait son PLANCHER dans les deux cas, donc exactement 0,0 dB
            // d ecart - et un ecart nul ressemble a une pente qui ne marche
            // pas. C est le piege 24 : un rapport calcule sur du vide.
            rendSpectre (sp, y, fs, 3 * n, 11u);
            std::vector<double> tard (y.begin() + 22050, y.end());
            const double fg = Spectre::frequence (16, fs);
            const double fa = Spectre::frequence (52, fs);
            return bande (tard, fa * 0.8, fa * 1.25) - bande (tard, fg * 0.8, fg * 1.25);
        };
        const double sans = restant (0.0), avec = restant (1.0);
        std::printf ("    aigu moins grave a 0,25 s : pente 0 -> %+.1f dB, "
                     "pente 1 -> %+.1f dB\n", sans, avec);
        verifie ("la pente eteint l'aigu en premier", sans - avec, 6.0, 200.0, "dB");
    }

    // --- 4. LE DESSIN PAR DEFAUT SONNE, ET IL NE SATURE PAS.
    {
        Spectre sp;
        std::vector<double> y;
        rendSpectre (sp, y, fs, n, 5u);
        double rms = 0.0, crete = 0.0;
        for (double v : y) { rms += v * v; crete = std::max (crete, std::abs (v)); }
        rms = std::sqrt (rms / (double) y.size());
        std::printf ("    spectre par defaut : rms %.3f, crete %.3f\n", rms, crete);
        verifie ("le spectre par defaut sonne", rms, 0.02, 1.0);
        verifie ("et il ne sature pas", crete, 0.0, 1.0);
    }

    // --- 5. DETERMINISTE : la meme graine rend le meme son.
    //
    // /!\ Sans ca, un preset ne rappellerait pas le meme materiau - et c'est
    // toute la these du projet (« generation deterministe et versionnee »).
    {
        Spectre sp;
        std::vector<double> a, b2;
        rendSpectre (sp, a, fs, 4096, 42u);
        rendSpectre (sp, b2, fs, 4096, 42u);
        double pire = 0.0;
        for (size_t i = 0; i < a.size(); ++i) pire = std::max (pire, std::abs (a[i] - b2[i]));
        std::printf ("    meme graine, deux rendus : ecart maximal %.3g\n", pire);
        verifie ("le rendu est deterministe", pire, 0.0, 1e-12);
    }
}

static void testBusParCouche()
{
    std::printf ("\n39. LES QUATRE BUS DE COUCHE\n");
    std::printf ("    (/!\\ la somme des bus doit valoir le total au bit pres :\n"
                 "     deux additions ecrites a deux endroits finissent par diverger\n"
                 "     d'un gain ou d'un panoramique, et ca ne s'entend qu'en"
                 " comparant)\n");

    const double fs = 44100.0;
    std::vector<double> rom;
    const Recette rec = recetteTest();
    const auto info = rendRom (rec, rom);
    const double* roms[kNbCouches] = { rom.data(), rom.data(), rom.data(), rom.data() };
    int tl[kNbCouches] = { info.nTotal, info.nTotal, info.nTotal, info.nTotal };

    // ⭐ LES QUATRE COUCHES ACTIVES, sur QUATRE MOTEURS DIFFERENTS. Un test ou
    // elles seraient identiques ne verrait pas une confusion d'indices.
    ReglagesPatch p;
    for (int c = 0; c < kNbCouches; ++c)
    {
        p.couche[c].active = true;
        p.couche[c].moteur = (TypeMoteur) c;
        p.couche[c].niveau = 0.3 + 0.2 * (double) c;
        p.couche[c].pan    = -0.6 + 0.4 * (double) c;
        p.couche[c].grain.actif = false;
    }
    p.polyphonie = 8;
    p.instab.actif = false;
    for (auto& m : p.mods) m.type = TypeMod::Aucun;
    for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }

    Moteur m; m.prepare (fs, 16);
    // trois notes : plusieurs VOIX ecrivent dans les memes bus
    for (int note : { 48, 55, 60 }) m.noteOn (p, note, 100, roms, tl);

    double pireEcart = 0.0, energie[kNbCouches] = { 0.0 };
    double bus[2 * kNbCouches];
    for (int i = 0; i < 30000; ++i)
    {
        double g = 0.0, d = 0.0;
        m.suivantParCouche (p, bus, g, d);
        double sg = 0.0, sd = 0.0;
        for (int c = 0; c < kNbCouches; ++c)
        {
            sg += bus[c * 2]; sd += bus[c * 2 + 1];
            energie[c] += bus[c * 2] * bus[c * 2] + bus[c * 2 + 1] * bus[c * 2 + 1];
        }
        pireEcart = std::max (pireEcart, std::abs (sg - g));
        pireEcart = std::max (pireEcart, std::abs (sd - d));
    }

    std::printf ("    ecart maximal entre la somme des bus et le total : %.3g\n", pireEcart);
    verifie ("la somme des quatre bus vaut le total", pireEcart, 0.0, 1e-12);

    // ⭐ ET CHAQUE BUS PORTE VRAIMENT SA COUCHE. Un bus toujours vide passerait
    // le controle precedent sans que rien ne le signale - il suffirait que les
    // trois autres portent tout.
    int muets = 0;
    for (int c = 0; c < kNbCouches; ++c)
    {
        std::printf ("    bus %d : energie %.4g\n", c, energie[c]);
        if (energie[c] < 1e-9) ++muets;
    }
    verifie ("aucun bus n'est muet", (double) muets, 0.0, 0.0);
}

static void testMonoFM()
{
    std::printf ("\n38. MONO ET LEGATO SUR LE MOTEUR FM\n");
    std::printf ("    (/!\\ le controle equivalent existait deja, mais sur le moteur\n"
                 "     ROM - il ne pouvait donc pas voir ce defaut-ci)\n");

    const double fs = 44100.0;
    const double* roms[kNbCouches] = { nullptr, nullptr, nullptr, nullptr };
    int tl[kNbCouches] = { 0, 0, 0, 0 };

    auto base = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::FM;
        p.couche[0].grain.actif = false;
        p.couche[0].veloNiveau = 0.0;      // on isole le defaut du timbre
        // /!\ PAS D'UNISSON DESACCORDE. Trois voix a 7 cents (le defaut)
        // BATTENT avec une periode d'environ une seconde - or on compare deux
        // notes espacees d'une seconde. La mesure attraperait le battement au
        // lieu du defaut, et rendait 0,59 la ou tout allait bien.
        p.couche[0].desaccordCents = 0.0;
        p.couche[0].largeur = 0.0;
        // ⭐ LE CAS QUI ECHOUE : des operateurs a TENUE BASSE. Ils sont a zero
        // quand la deuxieme touche arrive ; si personne ne les relance, elle
        // sort beaucoup plus faible. Avec une tenue haute le defaut est
        // invisible - c'est pour ca qu'il a fallu le chercher ici.
        auto& f = p.couche[0].fm;
        f.algo = 2;
        f.ratio[0] = 1.0; f.ratio[1] = 2.0; f.ratio[2] = 1.0; f.ratio[3] = 1.0;
        f.niveau[0] = 1.0; f.niveau[1] = 0.8; f.niveau[2] = 0.0; f.niveau[3] = 0.0;
        for (int k = 0; k < kOperateurs; ++k)
        {
            f.attaque[k] = 0.002; f.decli[k] = 0.25;
            f.tenue[k] = 0.05;    f.relache[k] = 0.20;
            f.echelleNiveau[k] = 0.0; f.desaccord[k] = 0.0; f.veloSens[k] = 0.0;
        }
        // l'enveloppe d'amplitude, elle, TIENT : ce qu'on mesure doit venir
        // des operateurs, pas d'elle
        p.couche[0].attaque = 0.002; p.couche[0].decli = 0.2;
        p.couche[0].tenue = 1.0;     p.couche[0].relache = 0.2;
        p.instab.actif = false;
        for (auto& m : p.mods) m.type = TypeMod::Aucun;
        for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }
        return p;
    };

    // /!\ RMS ET NON CRETE. En mono les PHASES sont continues - c'est meme
    // le but, pour ne pas claquer - donc la crete instantanee d'une attaque
    // depend de l'endroit ou les operateurs se trouvaient. Elle varie de 20 %
    // sans que rien ne se soit passe. La RMS mesure ce que l'oreille juge.
    auto niveau = [] (const std::vector<double>& y, int a, int b)
    {
        double s2 = 0.0; int n = 0;
        for (int i = a; i < b && i < (int) y.size(); ++i)
        { s2 += y[(size_t) i] * y[(size_t) i]; ++n; }
        return std::sqrt (s2 / (double) std::max (1, n));
    };

    static const char* nom[] = { "poly", "mono", "legato" };
    for (int mode = 0; mode < 3; ++mode)
    {
        auto p = base();
        p.mode = (mode == 0) ? ModeVoix::Poly
               : (mode == 1) ? ModeVoix::Mono : ModeVoix::Legato;
        p.polyphonie = (mode == 0) ? 16 : 1;

        Moteur m; m.prepare (fs, 16);
        std::vector<double> y;
        // /!\ ON NE LACHE PAS LA PREMIERE TOUCHE. C'est tout le test : apres
        // un noteOff, le mode mono repasse par le redeclenchement complet et
        // le defaut est invisible. C'est en enchainant DOIGT POSE qu'on atteint
        // `relanceDouce` - la facon dont on joue reellement en mono.
        m.noteOn (p, 60, 100, roms, tl);
        for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }
        m.noteOn (p, 60, 100, roms, tl);   // MEME note, premiere touche TOUJOURS tenue
        for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (p, a, b); y.push_back (a); }

        // les cretes des deux ATTAQUES : 30 ms apres chaque note
        const double c1 = niveau (y, 0, 4410);
        const double c2 = niveau (y, 44100, 48510);
        const double rap = c2 / std::max (1e-9, c1);
        std::printf ("    %-7s : note 1 %.4f, note 2 %.4f  ->  rapport %.2f\n",
                     nom[mode], c1, c2, rap);
        verifie ("la deuxieme note a le meme niveau que la premiere", rap, 0.8, 1.25, "x");
    }

    // --- ET LE LEGATO DOIT RESTER DU LEGATO.
    //
    // /!\ SANS CE CONTROLE, LA CORRECTION PRECEDENTE PEUT DERIVER EN MONO.
    // On vient de faire relancer le TIMBRE en legato ; si quelqu'un fait aussi
    // relancer l'enveloppe de couche, les deux modes deviennent identiques et
    // plus rien ne le signale - le test ci-dessus passerait tout aussi bien.
    //
    // Ce qui fait le legato, c'est l'A\ENCE de re-attaque du volume. On le
    // rend visible avec une enveloppe de couche lente et a tenue basse : en
    // mono elle repart vers 1, en legato elle reste ou elle est.
    {
        auto p = base();
        p.couche[0].attaque = 0.15;   // lente, donc la re-attaque s'entend
        p.couche[0].decli   = 0.25;
        p.couche[0].tenue   = 0.20;   // basse, donc il y a de la place pour remonter
        p.couche[0].relache = 0.20;
        // les operateurs TIENNENT : ce qu'on mesure doit venir de la couche
        for (int k = 0; k < kOperateurs; ++k)
        { p.couche[0].fm.tenue[k] = 1.0; p.couche[0].fm.decli[k] = 8.0; }
        p.polyphonie = 1;

        auto rejoue = [&] (ModeVoix mode)
        {
            auto q = p; q.mode = mode;
            Moteur m; m.prepare (fs, 16);
            std::vector<double> y;
            m.noteOn (q, 60, 100, roms, tl);
            for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (q, a, b); y.push_back (a); }
            m.noteOn (q, 60, 100, roms, tl);   // doigt toujours pose
            for (int i = 0; i < 44100; ++i) { double a, b; m.suivant (q, a, b); y.push_back (a); }
            // le niveau APRES la seconde touche, une fois l'attaque passee
            return niveau (y, 55000, 66000);
        };
        const double m2 = rejoue (ModeVoix::Mono), l2 = rejoue (ModeVoix::Legato);
        std::printf ("    apres la seconde touche : mono %.4f, legato %.4f\n", m2, l2);
        verifie ("le mono RE-ATTAQUE le volume", m2 / std::max (1e-9, l2), 1.8, 20.0, "x");
    }
}

static void testMacrosFacade()
{
    std::printf ("\n37. LES TROIS MACROS DE LA PAGE JOUER\n");
    std::printf ("    (/!\\ retour utilisateur : GRAIN et MOUVEMENT ne feraient\n"
                 "     plus rien, USURE si. On mesure au lieu de croire.)\n");

    const double fs = 22050.0;
    const int n = 22050;

    std::vector<double> rom;
    const Recette rec = recetteTest();
    const auto info = rendRom (rec, rom);

    // Le patch de depart : celui d'un utilisateur qui vient d'ouvrir le plugin.
    auto patchNeutre = [] ()
    {
        ReglagesPatch p;
        for (auto& c : p.couche) c.active = false;
        p.couche[0].active = true;
        p.couche[0].moteur = TypeMoteur::Rom;
        p.instab.actif = true;
        for (auto& m : p.mods) m.type = TypeMod::Aucun;
        for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }
        return p;
    };

    // /!\ LA ROM DOIT ETRE REQUANTIFIEE A CHAQUE FOIS, comme le fait
    // `construitRom` dans le plugin. Mon premier banc passait une ROM rendue
    // une seule fois : USURE, qui n'agit QUE sur la quantification du materiau,
    // sortait a 0,000 dB - et j'ai failli en conclure qu'elle ne servait a
    // rien. C'est le banc qui ne pouvait pas la voir.
    std::vector<double> romQ;
    auto joue = [&] (ReglagesPatch p)
    {
        romQ.assign (rom.begin(), rom.end());
        for (auto& v : romQ) v = quantifie (v, p.couche[0].grain.quant);
        const double* roms[kNbCouches] = { romQ.data(), nullptr, nullptr, nullptr };
        int tl[kNbCouches] = { info.nTotal, 0, 0, 0 };
        // /!\ UN ACCORD, ET LONG. Ma premiere version jouait UNE note pendant
        // une seconde - et MOUVEMENT y rendait 0,60 dB meme apres avoir double
        // ses coefficients. Ce n'etait pas le dosage : l'instabilite agit
        // surtout ENTRE LES VOIX (chaque carte de voix derive de son cote) et
        // sur la DUREE. Une note seule sur une seconde ne peut pas la voir.
        // Trois notes sur trois secondes, si.
        Moteur m; m.prepare (fs, 8);
        for (int note : { 45, 52, 57 }) m.noteOn (p, note, 100, roms, tl);
        std::vector<double> y ((size_t) (n * 3));
        for (int i = 0; i < n * 3; ++i)
        { double a, b; m.suivant (p, a, b); y[(size_t) i] = 0.5 * (a + b); }
        return y;
    };

    struct Cas { const char* nom; int rang; };
    const Cas cas[] = { { "USURE", 0 }, { "GRAIN", 1 }, { "MOUVEMENT", 2 } };

    for (const auto& c : cas)
    {
        ReglagesPatch a = patchNeutre(), b = patchNeutre();
        const double v[3] = { c.rang == 0 ? 1.0 : 0.0,
                              c.rang == 1 ? 1.0 : 0.0,
                              c.rang == 2 ? 1.0 : 0.0 };
        appliqueMacros (a, 0.0, 0.0, 0.0);
        appliqueMacros (b, v[0], v[1], v[2]);

        const auto ya = joue (a), yb = joue (b);
        const double d = perte (empreinte (ya, fs), empreinte (yb, fs));
        std::printf ("    %-10s a fond contre a zero : %6.3f dB d'ecart\n", c.nom, d);
        verifie ("cette macro change vraiment le son", d, 0.5, 200.0, "dB");
    }
}

static void testOctetsInertes()
{
    std::printf ("\n36. AUCUN OCTET CHERCHE NE DOIT ETRE INERTE\n");
    std::printf ("    (/!\\ un octet MUTABLE qui ne change pas la perte est\n"
                 "     une dimension MORTE : la recherche le bouge, paie des\n"
                 "     evaluations pour ca, et n'apprend rien)\n");

    const double fs = 22050.0;
    const int n = 22050;

    struct Cas { const char* nom; int off; double mini; };
    const Cas cas[] = {
        // /!\ L'ORDRE DES OCTETS DE FILTRE : topologie, coupure, resonance,
        // morph, suivi. Ecrit d'abord « coupure » sur kOffFiltre + 0, ce
        // tableau annoncait 1,7 dB pour la coupure et 17,6 pour la resonance -
        // les deux chiffres etaient justes et les deux noms faux. C'est le
        // piege 13, au meme endroit : une constante posee de memoire au lieu
        // d'etre relue.
        { "filtre topologie", kOffFiltre + 0, 0.5 },
        { "filtre coupure",   kOffFiltre + 1, 2.0 },
        { "filtre resonance", kOffFiltre + 2, 0.8 },
        { "env attaque",      kOffEnv + 0,    0.3 },
        { "env chute",        kOffEnv + 1,    0.3 },
        { "env tenue",        kOffEnv + 2,    0.15 },
        // ⭐⭐ CELUI-CI VALAIT 0,000 dB. Le rendu de recherche ne relachait
        // jamais la note : l'octet de relachement etait mutable, la recherche
        // le bougeait, et ca ne changeait strictement rien. Corrige en
        // relachant a 80 % du rendu (voir rendProfil).
        { "env relachement",  kOffEnv + 3,    0.5 },
        { "reverbe melange",  kOffFx + 7,     1.0 },
        { "chorus melange",   kOffFx + 0,     0.8 },
    };

    for (const auto& c : cas)
    {
        double somme = 0.0; int nb = 0;
        for (int t = 0; t < 12; ++t)
        {
            Des de ((unsigned int) (t * 7907 + 3));
            uint8_t a[kOctetsProfil]; tireProfil (de, a);
            uint8_t b[kOctetsProfil]; std::memcpy (b, a, sizeof (a));
            a[c.off] = 20; b[c.off] = 235;
            std::vector<double> ya, yb;
            rendProfil (a, ya, n, fs, 220.0);
            rendProfil (b, yb, n, fs, 220.0);
            somme += perte (empreinte (ya, fs), empreinte (yb, fs));
            ++nb;
        }
        const double d = somme / (double) nb;
        std::printf ("    %-18s : %6.3f dB entre ses deux extremes\n", c.nom, d);
        verifie ("cet octet change vraiment le son", d, c.mini, 200.0, "dB");
    }

    // ------------------------------------------------------------------
    // ⭐ LES OCTETS QUI N'EXISTENT QUE DANS UN CONTEXTE. Le passe-haut vaut
    // pour tous les moteurs ; le desaccord et la vitesse par operateur n'ont
    // de sens qu'en FM ; la grille, qu'en table d'ondes ET famille dessinee.
    // Les mesurer sur un profil tire au hasard donnerait zero neuf fois sur
    // dix - et on conclurait a tort qu'ils sont morts.
    struct CasCtx { const char* nom; int off; int moteur; double mini; };
    const CasCtx ctx[] = {
        { "passe-haut coupure",  kOffFiltre + 5, -1, 1.0 },
        { "passe-haut reso",     kOffFiltre + 6, -1, 0.15 },
        { "FM op2 desaccord",    kOffFM + 4 + 9 + 7, 2, 0.5 },
        { "FM op2 vitesse",      kOffFM + 4 + 9 + 8, 2, 0.15 },
        { "grille cellule 5",    kOffGrille + 5,     3, 0.3 },
        { "grille finesse/bits", kOffGrille + kCellulesProfil, 3, 0.3 },
        // ⭐ Le SECOND MATERIAU : il n'existe que sur le moteur ROM, et son
        // octet de choix doit basculer un son entier.
        { "choix du materiau",  kOffMateriau,      0, 2.0 },
        { "spectre bande 20",   kOffSpectre + 20,  0, 0.3 },
        { "spectre bande 45",   kOffSpectre + 45,  0, 0.3 },
        { "spectre pente",      kOffSpParam + 0,   0, 0.2 },
        { "spectre chute",      kOffSpParam + 2,   0, 0.3 },
    };

    for (const auto& c : ctx)
    {
        double somme = 0.0; int nb = 0;
        for (int t = 0; t < 12; ++t)
        {
            Des de ((unsigned int) (t * 6151 + 11));
            uint8_t a[kOctetsProfil]; tireProfil (de, a);
            if (c.moteur == 0)
            {
                // /!\ POUR JUGER LE SPECTRE, IL FAUT QUE LA COUCHE LE LISE.
                // Ses octets sont inertes tant que l'octet de materiau designe la
                // recette - les mesurer sans ca rendrait zero et ferait
                // conclure a tort qu'ils ne servent a rien.
                a[kOffMateriau] = 200;
            }
            if (c.moteur >= 0)
            {
                a[kOffMoteur] = (uint8_t) (c.moteur * 64 + 20);
                // famille DESSINEE : les trois bits de poids fort >= 4
                if (c.moteur == 3) a[kOffTable + 0] = 200;
            }
            uint8_t b[kOctetsProfil]; std::memcpy (b, a, sizeof (a));
            a[c.off] = 20; b[c.off] = 235;
            std::vector<double> ya, yb;
            rendProfil (a, ya, n, fs, 220.0);
            rendProfil (b, yb, n, fs, 220.0);
            somme += perte (empreinte (ya, fs), empreinte (yb, fs));
            ++nb;
        }
        const double d = somme / (double) nb;
        std::printf ("    %-18s : %6.3f dB entre ses deux extremes\n", c.nom, d);
        verifie ("cet octet change vraiment le son", d, c.mini, 200.0, "dB");
    }
}

static void testGrille()
{
    std::printf ("\n34. LA GRILLE DESSINABLE\n");
    std::printf ("    (/!\\ trois resolutions a ne pas confondre : la CELLULE n'en\n"
                 "     a aucune, la valeur STOCKEE est toujours sur 8 bits, et\n"
                 "     la QUANTIFICATION est reelle au rendu)\n");

    // --- 1. HUIT BITS EST SANS PERTE. C'est ce qui rend la profondeur
    // reglable : le stockage est deja sur huit bits, donc quantifier a huit
    // bits ne doit rien changer du tout.
    {
        Dessin d; d.nCellules = 64; d.bits = 8;
        Des de (12345);
        for (int i = 0; i < 64; ++i) d.pose (i, de.entre (-1.0, 1.0), 64);
        double pire = 0.0;
        for (int i = 0; i < 64; ++i)
            pire = std::max (pire, std::abs (d.lit (i, 64) - d.rendu (i, 64)));
        std::printf ("    ecart stocke/rendu a 8 bits : %.3g (pas de stockage %.4f)\n",
                     pire, 2.0 / 255.0);
        verifie ("huit bits ne quantifie rien", pire, 0.0, 1e-9);
    }

    // --- 2. MAIS LA QUANTIFICATION EST REELLE EN DESSOUS.
    //
    // /!\ C'EST LE CONTROLE QUI DECIDE SI LE PLUGIN MENT. Si le 4 bits ne
    // faisait que dessiner un escalier a l'ecran sans quantifier le signal, le
    // grain qu'on entend serait un decor - et tout le reste du plugin est bati
    // sur le contraire.
    {
        for (int bits : { 3, 4, 5, 6 })
        {
            Dessin d; d.nCellules = 64; d.bits = bits;
            Des de (777);
            for (int i = 0; i < 64; ++i) d.pose (i, de.entre (-1.0, 1.0), 64);

            std::vector<double> v;
            for (int i = 0; i < 64; ++i) v.push_back (d.rendu (i, 64));
            std::sort (v.begin(), v.end());
            int distinctes = 1;
            for (size_t i = 1; i < v.size(); ++i)
                if (std::abs (v[i] - v[i - 1]) > 1e-9) ++distinctes;

            const int attendu = 1 << bits;
            std::printf ("    %d bits : %d valeurs distinctes sur 64 tirees "
                         "(plafond %d)\n", bits, distinctes, attendu);
            verifie ("la quantification limite vraiment les niveaux",
                     (double) distinctes, 1.0, (double) attendu);
        }
    }

    // --- 3. ET ELLE N'EST PAS DESTRUCTIVE.
    //
    // Descendre a trois bits puis remonter a huit doit rendre le dessin
    // INTACT. Sans cette propriete, la profondeur devient une porte qui claque :
    // un reglage qu'on n'ose plus toucher parce qu'il efface le travail.
    {
        Dessin d; d.nCellules = 32; d.bits = 8;
        Des de (99);
        for (int i = 0; i < 32; ++i) d.pose (i, de.entre (-1.0, 1.0), 32);
        const Dessin avant = d;

        d.bits = 3;  (void) d.rendu (0, 32);      // on ecoute en trois bits
        d.bits = 8;                               // et on remonte

        const bool identique = (d == avant);
        double pire = 0.0;
        for (int i = 0; i < 32; ++i)
            pire = std::max (pire, std::abs (d.lit (i, 32) - avant.lit (i, 32)));
        std::printf ("    aller-retour 8 -> 3 -> 8 bits : ecart maximal %.3g\n", pire);
        verifie ("baisser puis remonter les bits ne detruit rien", pire, 0.0, 1e-12);
        verifie ("et le dessin est bit a bit le meme", identique ? 1.0 : 0.0, 1.0, 1.0);
    }

    // --- 4. LE MEME POUR LES CELLULES. Passer de 64 a 16 cellules puis
    // revenir ne doit pas trouer le dessin : on ecrit le PAVE que le clic
    // recouvre, pas une cellule sur quatre.
    {
        Dessin d; d.nCellules = 16;
        for (int i = 0; i < 16; ++i) d.pose (i, (i % 2) ? 0.8 : -0.8, 16);
        int trous = 0;
        for (int i = 0; i < 64; ++i)
        {
            const double a = (double) d.cellule[i] / 255.0 * 2.0 - 1.0;
            if (std::abs (std::abs (a) - 0.8) > 0.01) ++trous;
        }
        std::printf ("    dessin pose en 16 cellules, relu en 64 : %d trous\n", trous);
        verifie ("changer la finesse ne troue pas le dessin", (double) trous, 0.0, 0.0);
    }

    // --- 5. LES HARMONIQUES DE L'ESCALIER SONT VRAIES.
    //
    // Un sinus quantifie a trois bits doit contenir BEAUCOUP plus de distorsion
    // harmonique que le meme sinus a huit bits. C'est la contrepartie du
    // controle 2, mesuree sur le signal et non sur les valeurs.
    {
        auto distorsion = [] (int bits)
        {
            Dessin d; d.nCellules = 64; d.bits = bits;
            d.sinus();
            SpectreDessin sp; sp.analyse (d);
            const double fond = sp.ac[1] * sp.ac[1] + sp.as[1] * sp.as[1];
            double reste = 1e-30;
            for (int k = 2; k <= kHarmDessin; ++k)
                reste += sp.ac[k] * sp.ac[k] + sp.as[k] * sp.as[k];
            return 10.0 * std::log10 (reste / std::max (1e-30, fond));
        };
        const double d8 = distorsion (8), d4 = distorsion (4), d3 = distorsion (3);
        std::printf ("    sinus dessine : distorsion %.1f dB a 8 bits, "
                     "%.1f dB a 4 bits, %.1f dB a 3 bits\n", d8, d4, d3);
        verifie ("quantifier plus fort salit plus", d3 - d8, 6.0, 80.0, "dB");
        verifie ("et le classement respecte les bits", d4 - d8, 3.0, 80.0, "dB");
    }

    // --- 6. MAIS ELLES SONT LIMITEES EN BANDE AU RENDU.
    //
    // /!\ Un escalier joue tel quel replie salement : ses harmoniques montent
    // a l'infini. On verifie que la table fabriquee pour une note AIGUE ne
    // contient plus rien au-dessus de Nyquist - autrement dit qu'un do7 ne
    // sonne pas comme une radio mal reglee.
    {
        const double fs = 44100.0;
        Dessin d; d.nCellules = 32; d.bits = 4;
        Des de (4242);
        for (int i = 0; i < 32; ++i) d.pose (i, de.entre (-1.0, 1.0), 32);

        BanqueDessin b; b.fabrique (d, fs);

        auto joue = [&] (double hz)
        {
            ReglagesTable r;
            r.famille = FamilleTable::Dessin;
            r.position = 0.0;
            r.dessin = &b;
            EtageTable e; e.prepare (fs);
            e.declenche (r, hz, 0.0);
            std::vector<double> y ((size_t) 16384);
            for (int i = 0; i < 16384; ++i) y[(size_t) i] = e.suivant (r);
            return y;
        };

        auto hors = [fs] (const std::vector<double>& y, double hz)
        {
            const int n = 16384;
            std::vector<std::complex<double>> b2 ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b2[(size_t) i] = y[(size_t) i] * w;
            }
            fft (b2);
            double tot = 1e-30, mauvais = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fs / (double) n;
                const double e = std::norm (b2[(size_t) k]);
                tot += e;
                const double reste = std::fmod (f + 0.5 * hz, hz) - 0.5 * hz;
                if (std::abs (reste) > 30.0) mauvais += e;
            }
            return 10.0 * std::log10 (mauvais / tot);
        };

        const double grave = hors (joue (110.0), 110.0);
        const double aigu  = hors (joue (2093.0), 2093.0);
        std::printf ("    escalier 4 bits : energie non harmonique %.1f dB a 110 Hz, "
                     "%.1f dB a 2093 Hz\n", grave, aigu);
        verifie ("la grille ne replie pas dans le grave", grave, -200.0, -25.0, "dB");
        verifie ("ni dans l'aigu", aigu, -200.0, -25.0, "dB");
    }

    // --- 6 bis. ⭐⭐ DEUX DESSINS : « position » MORPHE DE A VERS B.
    //
    // /!\ SANS B, LE BOUTON « POSITION » NE SERT A RIEN, et l'utilisateur l'a
    // vu avant moi : les seize images d'une table dessinee etaient le MEME
    // dessin filtre de plus en plus bas, donc « position » etait un
    // adoucisseur deguise et non un balayage.
    //
    // Le controle : A pauvre (un sinus), B riche (une scie). A position 0 on
    // doit entendre A, a position 1 on doit entendre B - donc la richesse doit
    // AUGMENTER, alors qu'elle DIMINUAIT avec l'ancien mecanisme.
    {
        const double fs = 44100.0;
        DessinAB d;
        d.a.nCellules = 32; d.b.nCellules = 32; d.bActif = true;
        d.a.sinus();
        for (int i = 0; i < 32; ++i)
            d.b.pose (i, -1.0 + 2.0 * (double) i / 31.0, 32);   // dent de scie

        BanqueDessin b; b.fabrique (d, fs);

        auto richesse = [&] (double pos)
        {
            ReglagesTable r;
            r.famille = FamilleTable::Dessin;
            r.position = pos;
            r.dessin = &b;
            EtageTable e; e.prepare (fs);
            e.declenche (r, 220.0, 0.0);
            std::vector<double> y ((size_t) 16384);
            for (int i = 0; i < 16384; ++i) y[(size_t) i] = e.suivant (r);
            const int n = 16384;
            std::vector<std::complex<double>> b2 ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b2[(size_t) i] = y[(size_t) i] * w;
            }
            fft (b2);
            double fond = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fs / (double) n;
                const double e2 = std::norm (b2[(size_t) k]);
                if (f < 330.0) fond += e2; else haut += e2;
            }
            return 10.0 * std::log10 (haut / fond);
        };
        const double p0 = richesse (0.0), p1 = richesse (1.0);
        std::printf ("    A sinus -> B scie : %.1f dB d'aigu a position 0, "
                     "%.1f dB a position 1\n", p0, p1);
        verifie ("position 0 rend A, position 1 rend B", p1 - p0, 12.0, 200.0, "dB");

        // ⭐ ET SANS B, ON GARDE L'ANCIEN COMPORTEMENT. Les patchs deja
        // enregistres ne doivent pas changer de son parce qu'on a ajoute une
        // possibilite dont ils ne se servent pas.
        DessinAB seul; seul.a = d.b; seul.bActif = false;
        BanqueDessin b3; b3.fabrique (seul, fs);
        BanqueDessin b4; b4.fabrique (d.b, fs);
        const auto& t3 = b3.lit (220.0);
        const auto& t4 = b4.lit (220.0);
        double pire = 0.0;
        for (double ph = 0.0; ph < 1.0; ph += 0.01)
            pire = std::max (pire, std::abs (t3.lit (0.3, ph, LectureTable::Fondu)
                                           - t4.lit (0.3, ph, LectureTable::Fondu)));
        std::printf ("    sans B : ecart avec l'ancien chemin %.3g\n", pire);
        verifie ("sans B, rien ne change", pire, 0.0, 1e-12);
    }

    // --- 7. L'AXE DE BALAYAGE EST INVERSE, ET C'EST VOULU.
    //
    // Ailleurs, position 0 = sinus. Ici, position 0 doit rendre LE DESSIN :
    // quelqu'un qui vient de dessiner une forme et qui entend un sinus croit
    // avoir casse son patch. Position 1 adoucit.
    {
        const double fs = 44100.0;
        Dessin d; d.nCellules = 32; d.bits = 8;
        for (int i = 0; i < 32; ++i) d.pose (i, -1.0 + 2.0 * (double) i / 31.0, 32);
        BanqueDessin b; b.fabrique (d, fs);

        auto richesse = [&] (double pos)
        {
            ReglagesTable r;
            r.famille = FamilleTable::Dessin;
            r.position = pos;
            r.dessin = &b;
            EtageTable e; e.prepare (fs);
            e.declenche (r, 220.0, 0.0);
            std::vector<double> y ((size_t) 16384);
            for (int i = 0; i < 16384; ++i) y[(size_t) i] = e.suivant (r);

            const int n = 16384;
            std::vector<std::complex<double>> b2 ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) n);
                b2[(size_t) i] = y[(size_t) i] * w;
            }
            fft (b2);
            double fond = 1e-30, haut = 1e-30;
            for (int k = 1; k <= n / 2; ++k)
            {
                const double f = (double) k * fs / (double) n;
                const double e = std::norm (b2[(size_t) k]);
                if (f < 330.0) fond += e; else haut += e;
            }
            return 10.0 * std::log10 (haut / fond);
        };
        const double p0 = richesse (0.0), p1 = richesse (1.0);
        std::printf ("    scie dessinee : harmoniques hautes %.1f dB a position 0, "
                     "%.1f dB a position 1\n", p0, p1);
        verifie ("position 0 rend le dessin, position 1 l'adoucit", p0 - p1, 10.0, 200.0, "dB");
    }
}

static void testStabiliteEtCout()
{
    std::printf ("\n34. LE PATCH QUI EXPLOSE, ET LE COUT DE LA FM\n");

    // --- 1. ⭐⭐ AUCUN PATCH NE DOIT PRODUIRE DE NaN.
    // /!\ C'EST LE CONTROLE LE PLUS IMPORTANT DE CE FICHIER. Un NaN ne fait
    // aucun bruit et sature tous les indicateurs : l'utilisateur voit un
    // VU-metre a fond, n'entend rien, et doit recharger le plugin. Mesure avant
    // correction : 21,7 % des patchs tires au de.
    {
        const double fs = 44100.0;
        const int n = 8000;
        int nan = 0, inf = 0;
        std::vector<double> rom;
        const int nTirages = 400;

        for (int t = 0; t < nTirages; ++t)
        {
            Des de ((unsigned int) (t * 7919 + 13));
            uint8_t prof[kOctetsProfil];
            tireProfil (de, prof);
            const ReglagesCouche rc = coucheDepuisProfil (prof);

            int nRom = 0;
            if (rc.moteur == TypeMoteur::Rom)
            {
                const auto info = rendRom (depuisOctets (prof), rom, rc.grain.fsRom);
                for (auto& v : rom) v = quantifie (v, rc.grain.quant);
                nRom = info.nTotal;
            }
            const double* roms[kNbCouches] = { rom.empty() ? nullptr : rom.data(),
                                               nullptr, nullptr, nullptr };
            int tl[kNbCouches] = { nRom, 0, 0, 0 };

            ReglagesPatch p;
            for (auto& c : p.couche) c.active = false;
            p.couche[0] = rc;
            p.instab.actif = false;
            for (auto& m : p.mods) m.type = TypeMod::Aucun;
            for (auto& r : p.routages) { r.source = 0; r.dest = 0; r.dose = 0.0; }

            Moteur m; m.prepare (fs, 16);
            // /!\ ON JOUE HAUT. La coupure est RELATIVE a la hauteur : le meme
            // patch qui tient sur un do2 sort de la bande sur un do7, et c'est
            // la seulement que le filtre analogique divergeait.
            for (int note : { 48, 72, 96 }) m.noteOn (p, note, 110, roms, tl);
            for (int i = 0; i < n; ++i)
            {
                double a, b; m.suivant (p, a, b);
                if (std::isnan (a) || std::isnan (b)) { ++nan; break; }
                if (std::isinf (a) || std::isinf (b)) { ++inf; break; }
            }
        }
        std::printf ("    %d patchs tires au de, joues jusqu'au do7 : "
                     "%d NaN, %d infinis\n", nTirages, nan, inf);
        verifie ("aucun patch ne produit de NaN", (double) nan, 0.0, 0.0);
        verifie ("aucun patch ne produit d'infini", (double) inf, 0.0, 0.0);
    }

    // --- 1 bis. AUTOMATION : ET PAS DAVANTAGE QUAND LES REGLAGES BOUGENT.
    //
    // /!\ LE CONTROLE PRECEDENT NE SUFFIT PAS, et pluginval l'a montre : il
    // tire un patch puis le laisse TRANQUILLE. Or un NaN de filtre ne vient pas
    // d'un reglage, il vient d'un DEPLACEMENT de reglage - un etat interne
    // calcule pour une coupure, reinjecte avec le coefficient d'une autre. Le
    // test qui ne bouge rien ne peut pas le voir, et il declarait le plugin sain
    // pendant que l'hote sortait des infinis une fois sur trois.
    //
    // Ici on rejoue ce que fait un hote qui automatise : on redessine TOUT le
    // profil - modulations, effets et moteur compris - tous les 64
    // echantillons, pendant que trois notes tiennent.
    {
        const double fs = 44100.0;
        const int n = 12000, bloc = 64;
        int nan = 0, inf = 0, premier = -1;
        std::vector<double> rom;
        const int nTirages = 300;

        for (int t = 0; t < nTirages; ++t)
        {
            Des de ((unsigned int) (t * 104729 + 7));
            uint8_t prof[kOctetsProfil];
            tireProfil (de, prof);

            ReglagesPatch p = patchDepuisProfil (prof);
            const auto info = rendRom (depuisOctets (prof), rom, p.couche[0].grain.fsRom);
            for (auto& v : rom) v = quantifie (v, p.couche[0].grain.quant);
            const double* roms[kNbCouches] = { rom.data(), nullptr, nullptr, nullptr };
            int tl[kNbCouches] = { info.nTotal, 0, 0, 0 };

            FxProfil fx; fx.prepare (fs); fx.regleDepuisProfil (prof);

            Moteur m; m.prepare (fs, 16);
            for (int note : { 41, 69, 96 }) m.noteOn (p, note, 110, roms, tl);

            bool casse = false;
            for (int i = 0; i < n && ! casse; ++i)
            {
                if (i % bloc == 0 && i > 0)
                {
                    tireProfil (de, prof);
                    const ReglagesPatch q = patchDepuisProfil (prof);
                    // /!\ On ne remplace QUE ce qu'un hote peut automatiser :
                    // les reglages. Remplacer p entier remettrait aussi les
                    // couches inactives et le mode de voix, donc testerait un
                    // changement que l'utilisateur ne peut pas faire en jouant.
                    p.couche[0] = q.couche[0];
                    for (int k = 0; k < kModsProfil; ++k)
                    { p.mods[k] = q.mods[k]; p.routages[k] = q.routages[k]; }
                    fx.regleDepuisProfil (prof);
                }
                double a, b; m.suivant (p, a, b);
                fx.traite (a, b);
                if (std::isnan (a) || std::isnan (b))
                { ++nan; casse = true; if (premier < 0) premier = t; }
                else if (std::isinf (a) || std::isinf (b))
                { ++inf; casse = true; if (premier < 0) premier = t; }
            }
        }
        std::printf ("    %d patchs AUTOMATISES (profil redessine tous les %d ech.) : "
                     "%d NaN, %d infinis", nTirages, bloc, nan, inf);
        if (premier >= 0) std::printf (" (premier au tirage %d)", premier);
        std::printf ("\n");
        verifie ("aucune automation ne produit de NaN", (double) nan, 0.0, 0.0);
        verifie ("aucune automation ne produit d'infini", (double) inf, 0.0, 0.0);
    }

    // --- 2. LE FILTRE ANALOGIQUE TIENT A COUPURE EXTREME.
    // /!\ Sa saturation dans la boucle a delai nul rend la resolution TPT
    // inexacte : stable a petit g, divergente a grand g. D'ou un plafond de
    // coupure plus bas que pour les autres topologies.
    {
        const double fs = 44100.0;
        FiltreMusical f; f.prepare (fs);
        f.regle (Topologie::AnalogiqueBasseReso, 20000.0, 0.99);
        double pire = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            const double x = 0.9 * std::sin (2.0 * kPi * 120.0 * (double) i / fs);
            const double y = f.traite (x);
            if (! (std::abs (y) < 1e6)) { pire = 1e9; break; }
            pire = std::max (pire, std::abs (y));
        }
        std::printf ("    filtre analogique a 20 kHz de coupure et resonance 0,99 :"
                     " crete %.3f\n", pire);
        verifie ("il ne diverge pas", pire, 0.0, 100.0);
    }

    // --- 3. LA TABLE DE SINUS EST ASSEZ PRECISE.
    // /!\ Sans interpolation, l'erreur vaudrait 1/4096 de tour, soit -72 dB -
    // au-dessus du plancher qu'on simule par ailleurs, donc audible.
    {
        double pire = 0.0;
        for (int i = 0; i < 100000; ++i)
        {
            const double u = (double) i / 100000.0 * 7.0 - 3.0;   // hors [0,1) aussi
            pire = std::max (pire, std::abs (sinTour (u) - std::sin (2.0 * kPi * u)));
        }
        std::printf ("    table de sinus : erreur maximale %.2e (%.1f dB)\n",
                     pire, 20.0 * std::log10 (std::max (pire, 1e-12)));
        verifie ("la table est sous -90 dB", 20.0 * std::log10 (std::max (pire, 1e-12)),
                 -200.0, -90.0, "dB");
    }

    // --- 4. LA FM RESTE JUSTE apres le passage a la table.
    {
        const double fs = 44100.0;
        ReglagesFM r;
        r.algo = 0; r.retour = 0.0;
        for (int i = 0; i < kOperateurs; ++i)
        { r.niveau[i] = (i == 0) ? 1.0 : 0.0; r.ratio[i] = 1.0;
          r.tenue[i] = 1.0; r.decli[i] = 8.0; r.echelleNiveau[i] = 0.0; }

        EtageFM e; e.prepare (fs);
        e.declenche (r, 440.0, 0.0);
        std::vector<double> y (16384 + 4000);
        for (auto& v : y) v = e.suivant (r);

        const double a440 = ampA (y, 2000, 16384, 440.0, fs);
        const double a466 = ampA (y, 2000, 16384, 466.0, fs);
        std::printf ("    porteuse seule a 440 Hz : %.4f, temoin 466 Hz %.6f\n",
                     a440, a466);
        verifie ("la FM sonne a la bonne hauteur",
                 a466 / std::max (a440, 1e-9), 0.0, 0.02, "x");
        verifie ("et avec le bon niveau", a440, 0.15, 0.6);
    }
}

int main()
{
    std::printf ("\nROMANCE - tests de la couche de grain\n");
    std::printf ("-------------------------------------\n");
    testMuLaw();
    testPentes();
    testAsservissement();
    testProprete();
    testCouches();
    testStereoEtBoucle();
    testVolSansClic();
    testTransposition();
    testQuantAudible();
    testAsserviSurLeSon();
    testHauteurVive();
    testTopologies();
    testModulation();
    testEffets();
    testInstabilite();
    testRecette();
    testPerte();
    testResynthese();
    testMacros();
    testJeu();
    testAnalogique();
    testFM();
    testEffets2();
    testTable();
    testProfil();
    testChaineFx();
    testProfilModFx();
    testDestinations();
    testGlobalEtMacros();
    testEffets3();
    testMachines();
    testRetoursEcoute();
    testVelocite();
    testSourcesDeJeu();
    testControleursDeJeu();
    testPontFacade();
    testModulationAuDeclenchement();
    testCoutNoteOn();
    testDiagnosticReseau();
    testIdEmplacementFx();
    testForme();
    testSpectre();
    testBusParCouche();
    testMonoFM();
    testMacrosFacade();
    testOctetsInertes();
    testGrille();
    testPasseHautEtOperateurs();
    testStabiliteEtCout();
    std::printf ("\n%d controle(s), %d echec(s)\n\n", g_total, g_echecs);
    return g_echecs == 0 ? 0 : 1;
}
