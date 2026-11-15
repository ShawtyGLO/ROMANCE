// =============================================================================
//  Moteur.h - La voix (4 couches + bruit) et l'allocation de voix.
//
//  /!\ LE VOL DE VOIX FAIT PARTIE DU SON D'EPOQUE. Un pad qui se coupe parce
//  que la machine n'a que huit voix, c'est une signature, pas un defaut. Les
//  presets machine posent donc la valeur historique (Emulator II 8, M1 16) et
//  l'AFFICHENT ; l'utilisateur reste libre de la changer.
//  « Restreindre sans empecher » - et la REGLE de vol compte autant que le
//  nombre.
//
//  /!\ Un PATCH doit rester un objet AUTONOME avec sa propre sortie. Le niveau
//  PERFORMANCE (8 parts, a la Wavestation) est reporte, mais l'architecture ne
//  doit rien supposer sur le fait d'etre seul - sinon c'est une reecriture.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Modulation.h"
#include "Instabilite.h"

namespace romance
{

constexpr int kNbCouches = 4;   // JV-1080, ZEN-Core, Wavestation : trois
                                // machines le disent independamment.
constexpr int kVoixMax   = 32;

/** ⭐⭐ SUR QUEL RACK AGIT UN CABLE VERS UN EFFET.

    Cinq racks : les quatre tranches, puis le master. Le champ `couche` d'un
    cable les designe, et **0 vaut MASTER** - pas « tous ».

    /!\ C'EST L'INVERSE DU MEME CHAMP SUR UNE DESTINATION DE COUCHE, ou 0 veut
    dire TOUTES. L'asymetrie n'est pas une elegance : elle est ce qui fait que
    tous les patchs enregistres avant les racks de tranche se relisent a
    l'identique. Ils portent tous `couche = 0`, et ils visaient tous le master
    puisqu'il n'y avait que lui. Choisir « 0 = tous les racks » aurait fait
    sonner cinq reverbes la ou l'utilisateur en avait cable une.

    Rend l'indice de rack, 0..kNbCouches (le dernier est le master). */
inline int rackDeRoutage (int couche) noexcept
{
    return (couche <= 0 || couche > kNbCouches) ? kNbCouches : (couche - 1);
}


enum class ModeVoix { Poly, Mono, Legato };

/** Le bruit : sa couleur est MESUREE par l'analyse, seul son niveau est cherche.
    Invariant du projet : on ne cherche pas ce qu'on peut mesurer. */
class CoucheBruit
{
public:
    void prepare (double fs) noexcept { fs_ = fs; env_.prepare (fs); f_.prepare (fs); }

    void declenche (double a, double d, double s, double r, double coupure) noexcept
    {
        env_.regle (a, d, s, r);
        env_.declenche();
        f_.regle (Topologie::QuatrePoles, coupure, 0.0);
        etat_ = 22222;
    }
    void relache() noexcept { env_.relache(); }
    void extinctionRapide() noexcept { env_.extinctionRapide(); }
    void coupe()   noexcept { env_.coupe(); f_.reset(); }
    bool active() const noexcept { return env_.active(); }

    inline double suivant (double niveau) noexcept
    {
        if (! env_.active()) return 0.0;
        // generateur deterministe : meme graine, memes octets. Condition de
        // l'etat a ~100 octets (le patch ne stocke qu'une recette).
        etat_ = etat_ * 1664525u + 1013904223u;
        const double b = ((double) (etat_ >> 8) / 8388608.0) - 1.0;
        return f_.traite (b) * env_.suivant() * niveau;
    }

private:
    double fs_ = 44100.0;
    unsigned int etat_ = 22222;
    Enveloppe env_;
    FiltreMusical f_;
};

struct ReglagesPatch
{
    ReglagesCouche couche[kNbCouches];
    ReglagesMod mods[kNbMods];
    Routage     routages[kNbRoutages];
    bool   bruitActif = false;
    double bruitNiveau = 0.2, bruitCoupure = 4000.0;
    double bruitA = 0.002, bruitD = 0.15, bruitS = 0.0, bruitR = 0.2;

    ReglagesInstabilite instab;

    /** ⭐ Les quatre macros assignables, dans [0, 1]. Ce sont des SOURCES de
        modulation comme les huit emplacements - elles sont ici et pas dans la
        voix parce qu'il n'y en a qu'un jeu pour tout l'instrument. */
    double macros[kNbMacros] = { 0.0, 0.0, 0.0, 0.0 };

    ModeVoix mode = ModeVoix::Poly;
    int  polyphonie = 16;          // valeur de la machine, affichee comme telle
    double accordage = 440.0;

    /** ⭐ LE GLIDE SE COMPTE EN SECONDES PAR OCTAVE, pas en secondes tout
        court. Un temps FIXE fait qu'une seconde mineure met aussi longtemps
        qu'une dixieme : le glissando s'entend alors comme un defaut de
        justesse sur les petits intervalles et comme un escargot sur les
        grands. A vitesse constante, un demi-ton glisse douze fois plus vite
        qu'une octave - c'est ce que font les machines a portamento. */
    double glide = 0.0;            // s/octave ; 0 = pas de glide

    /** ⭐⭐ LES CONTROLEURS DE JEU. Ils vivent dans le patch et non dans la
        voix parce qu'ils sont de CANAL : une molette bouge tout l'instrument.
        La pression fait exception - voir `Voix::pression_` - parce que
        l'aftertouch POLYPHONIQUE existe et vaut la peine d'etre respecte. */
    double bend = 0.0;             // -1 .. +1
    double bendPortee = 2.0;       // demi-tons a pleine deviation
    double molette = 0.0;          // 0 .. 1
    double pression = 0.0;         // 0 .. 1  (pression de CANAL)
};

/** Une voix : jusqu'a 4 couches plus le bruit. */
class Voix
{
public:
    void prepare (double fs, int indice = 0) noexcept
    {
        fs_ = fs;
        for (auto& c : couches_) c.prepare (fs);
        bruit_.prepare (fs);
        // /!\ Les modulateurs tournent au TAUX DE CONTROLE, pas au taux audio.
        for (auto& m : mods_) m.prepare (fs / (double) kPasControle);
        // Une derive par COUCHE et par VOIX : chaque circuit a la sienne.
        // La graine depend des deux, sinon toutes les voix deriveraient
        // ensemble - ce qui serait un wow, pas une derive.
        for (int i = 0; i < kNbCouches; ++i)
            derives_[i].prepare (fs / (double) kPasControle,
                                 (unsigned int) (indice * 13 + i + 1));
        compteur_ = 0;
    }

    void declenche (const ReglagesPatch& p, int note, int velo,
                    const double* const* roms, const int* tailles,
                    double noteDepart = -1.0) noexcept
    {
        note_ = note; velo_ = velo; age_ = 0; libre_ = false;
        pression_ = 0.0;
        // /!\ UNE NOTE PART DE SA PROPRE HAUTEUR SAUF SI L'ON GLISSE. Heriter
        // de la hauteur de la voix precedente sans le vouloir ferait partir la
        // premiere note apres un vol d'un endroit arbitraire.
        hauteurLissee_ = (p.glide > 1e-4 && noteDepart >= 0.0)
                            ? noteDepart : (double) note;
        enExtinction_ = false; tenue_ = true;

        // ⭐⭐ LA MATRICE S'APPLIQUE AVANT DE DECLENCHER LES COUCHES.
        //
        // /!\ ELLE S'APPLIQUAIT APRES, ET CINQ DESTINATIONS DU MENU NE
        // FAISAIENT RIEN. `mes_impact.exe` les a nommees : attaque,
        // decroissance, tenue, relachement et largeur d'unisson rendaient
        // **exactement 0,000 dB** sur six profils. Zero exact, pas « peu » -
        // le code n'etait pas atteint (piege 38). Ces reglages sont lus UNE
        // FOIS, au declenchement ; la modulation arrivait un pas de controle
        // trop tard, donc jamais.
        //
        // /!\ ET LES MODULATEURS SONT DECLENCHES D'ABORD, MAIS ON NE LES FAIT
        // PAS AVANCER (`avance = false`). Les faire avancer ici donnerait a la
        // premiere note un pas de LFO d'avance sur toutes les suivantes.
        for (int i = 0; i < kNbMods; ++i) mods_[i].declenche (p.mods[i]);
        {
            double v[kNbSources];
            litSources (p, v, false);
            calculeCouchesModulees (p, v);
        }

        for (int i = 0; i < kNbCouches; ++i)
        {
            gainsZone_[i] = Couche::gainZone (p.couche[i], note, velo);
            actives_[i] = gainsZone_[i] > 0.0;
            if (! actives_[i]) continue;
            // ⭐ Les reglages MODULES, pas les reglages poses : c'est toute la
            // correction. « velocite -> attaque » passe par ici.
            const ReglagesCouche& rc = modulee_[i] ? couchesMod_[i] : p.couche[i];
            // la transposition est une propriete de la COUCHE : deux couches
            // peuvent jouer la meme note a deux octaves d'ecart.
            const double demi = (double) (note - 69)
                              + (double) p.couche[i].transpose
                              + rc.accordFin / 100.0;
            const double hz = p.accordage * std::pow (2.0, demi / 12.0);
            // ratio de lecture : la ROM est ecrite pour do3 (261,6 Hz)
            couches_[i].declenche (rc, hz / 261.6255653, hz,
                                   Couche::velNormalisee (rc, velo),
                                   gainsZone_[i]);
        }
        // ⭐ ON COPIE LE MATERIAU, on ne le suit pas.
        // /!\ La voix gardait un pointeur sur le TABLEAU de pointeurs du
        // processeur, donc elle relisait l'adresse a chaque echantillon :
        // changer de source pendant qu'une note sonnait remplacait la ROM sous
        // elle, d'ou le craquement. Une note garde maintenant le materiau avec
        // lequel elle a commence - ce qui est aussi le comportement des
        // machines : recharger un echantillon ne modifie pas les notes en
        // cours, il s'applique aux suivantes.
        for (int i = 0; i < kNbCouches; ++i)
        {
            roms_[i] = (roms != nullptr) ? roms[i] : nullptr;
            tailles_[i] = (tailles != nullptr) ? tailles[i] : 0;
        }
        if (p.bruitActif)
            bruit_.declenche (p.bruitA, p.bruitD, p.bruitS, p.bruitR, p.bruitCoupure);
        compteur_ = 0;
        // /!\ ON NE REMET PLUS `modulee_` A FAUX ICI : la copie modulee vient
        // d'etre calculee au-dessus, et l'effacer rendrait la correction
        // inoperante des le premier echantillon.
    }

    /** Relance la note sur la MEME voix, sans couper le son (mono). */
    void relance (const ReglagesPatch& p, int note, int velo) noexcept
    {
        note_ = note; velo_ = velo; age_ = 0; enExtinction_ = false; tenue_ = true;
        for (int i = 0; i < kNbCouches; ++i)
        {
            if (! actives_[i]) continue;
            const double demi = (double) (note - 69)
                              + (double) p.couche[i].transpose
                              + (modulee_[i] ? couchesMod_[i].accordFin
                                             : p.couche[i].accordFin) / 100.0;
            const double hz = p.accordage * std::pow (2.0, demi / 12.0);
            couches_[i].relanceDouce (p.couche[i], hz / 261.6255653, hz,
                                      Couche::velNormalisee (p.couche[i], velo),
                                      Couche::gainZone (p.couche[i], note, velo));
        }
        for (int i = 0; i < kNbMods; ++i)
            if (p.mods[i].retrig) mods_[i].declenche (p.mods[i]);
    }

    /** Legato : on glisse vers la nouvelle note. L'enveloppe d'amplitude ne
        redeclenche pas - c'est ce qui fait le legato - mais le TIMBRE, si
        (voir Couche::relanceTimbre). */
    void poseBus (double* b) noexcept { bus_ = b; }

    void glisse (const ReglagesPatch& p, int note) noexcept
    {
        note_ = note;
        // Sans glide on saute ; avec, `pasDeControle` rattrape tout seul.
        if (p.glide <= 1e-4) hauteurLissee_ = (double) note;
        majHauteur (p);
        for (int i = 0; i < kNbCouches; ++i)
            if (actives_[i])
                couches_[i].relanceTimbre (modulee_[i] ? couchesMod_[i] : p.couche[i]);
    }

    void relache() noexcept
    {
        tenue_ = false;
        for (auto& c : couches_) c.relache();
        bruit_.relache();
        for (auto& m : mods_) m.relache();
    }

    /** La voix rend sa place mais FINIT DE SONNER, en quelques millisecondes. */
    void rendLaPlace() noexcept
    {
        for (auto& c : couches_) c.extinctionRapide();
        bruit_.extinctionRapide();
        enExtinction_ = true;
    }
    bool enExtinction() const noexcept { return enExtinction_; }

    /** Ou en est la couche `i` de cette voix, entre 0 et 1. -1 si elle ne
        sonne pas. */
    double position (const ReglagesPatch& p, int i) const noexcept
    {
        if (libre_ || ! actives_[i] || ! couches_[i].active()) return -1.0;
        return couches_[i].position (modulee_[i] ? couchesMod_[i] : p.couche[i],
                                     tailles_[i]);
    }

    void coupe() noexcept
    {
        for (auto& c : couches_) c.coupe();
        bruit_.coupe();
        for (auto& m : mods_) m.coupe();
        libre_ = true; tenue_ = false;
    }

    /** Recalcule la hauteur des notes qui sonnent deja.

        Sans ca, bouger la transposition pendant qu'une note tient ne fait rien
        jusqu'a la note suivante - le son reste sur l'ancienne hauteur.
    */
    void majHauteur (const ReglagesPatch& p) noexcept
    {
        if (libre_ || enExtinction_) return;
        for (int i = 0; i < kNbCouches; ++i)
        {
            if (! actives_[i]) continue;
            const auto& a = apports_[i];
            const double demi = (hauteurLissee_ - 69.0)
                              + (double) p.couche[i].transpose
                              + (modulee_[i] ? couchesMod_[i].accordFin
                                             : p.couche[i].accordFin) / 100.0
                              // ⭐ LE BEND EST CABLE EN DUR ICI. C'est la seule
                              // source de jeu qui agisse sans qu'on l'ait
                              // branchee, et c'est voulu : personne ne
                              // s'attend a devoir cabler sa molette de hauteur.
                              + p.bend * p.bendPortee
                              + a.hauteur
                              // wow + flutter (commun) et derive (propre)
                              + (wowCents_ + deriveVoix_[i].cents) / 100.0;
            const double hz = p.accordage * std::pow (2.0, demi / 12.0);
            // ⭐ C'est ICI que vit la vraie personnalite d'une carte de voix :
            // la coupure et le niveau, pas la hauteur.
            const ReglagesCouche& rc = modulee_[i] ? couchesMod_[i] : p.couche[i];
            couches_[i].majHauteur (rc, hz / 261.6255653, hz,
                                    a.desaccord,
                                    a.coupure + deriveVoix_[i].octaves,
                                    a.coupureGrain, a.morph);
        }
    }

    /** Un pas de controle : on calcule les huit modulateurs, on applique la
        matrice, puis on repercute sur les couches. */
    /** ⭐⭐ LA VALEUR DES SOURCES, SANS FAIRE AVANCER LES MODULATEURS.

        Il fallait pouvoir la demander DEUX FOIS : au pas de controle, ou les
        modulateurs avancent d'un pas, et au DECLENCHEMENT d'une note, ou ils
        ne doivent surtout pas avancer - sinon la premiere note d'un patch
        entendrait ses LFO decales d'un pas par rapport a la suivante. */
    void litSources (const ReglagesPatch& p, double* v, bool avance) noexcept
    {
        for (int i = 0; i < kNbMods; ++i)
            v[i] = avance ? mods_[i].suivant (p.mods[i]) : mods_[i].valeur();
        // /!\ Les macros sont UNIPOLAIRES : le bouton est a zero quand on ne
        // s'en sert pas, et la modulation doit alors etre nulle.
        for (int i = 0; i < kNbMacros; ++i) v[kNbMods + i] = p.macros[i];
        v[kIdSourceTouche - 1] = sourceTouche (note_);
        v[kIdSourceVelo   - 1] = sourceVelo   (velo_);
        v[kIdSourceBend   - 1] = sourceBend   (p.bend);
        v[kIdSourceMolette - 1] = sourceUnipolaire (p.molette);
        // /!\ LA PRESSION DE LA VOIX L'EMPORTE SUR CELLE DU CANAL.
        v[kIdSourcePression - 1] = sourceUnipolaire (
                                       pression_ > 0.0 ? pression_ : p.pression);
    }

    /** ⭐⭐ LA COPIE MODULEE DES REGLAGES DE COUCHE.

        /!\ ELLE EXISTAIT DEJA, MAIS SEULEMENT AU PAS DE CONTROLE - c'est-a-dire
        APRES le declenchement de la note. Consequence mesuree par
        `mes_impact.exe` : ATTAQUE, DECROISSANCE, TENUE, RELACHEMENT et LARGEUR
        UNISSON rendaient **exactement 0,000 dB** sur six profils. Zero exact,
        pas « peu » - la signature du piege 38 : le code n'etait pas atteint.
        Ces reglages sont lus une fois, au declenchement ; la modulation
        arrivait un pas trop tard, pour toujours.
        Cinq entrees du menu ne faisaient donc rien - et « velocite -> attaque »,
        le geste de clavier le plus banal qui soit, etait du nombre. */
    void calculeCouchesModulees (const ReglagesPatch& p, const double* v) noexcept
    {
        for (auto& a : apports_) a.zero();

        // ⭐ LA COPIE MODULEE. On ne la refait que pour les couches qui ont
        // effectivement une destination generique cablee.
        bool besoin[kNbCouches] = { false, false, false, false };
        for (const auto& r : p.routages)
        {
            if (r.source <= 0 || r.source > kNbSources) continue;
            if (r.dest < 8 || r.dest >= kNbDestinations) continue;
            const int c0 = (r.couche <= 0) ? 0 : r.couche - 1;
            const int c1 = (r.couche <= 0) ? kNbCouches - 1 : r.couche - 1;
            for (int c = c0; c <= c1 && c < kNbCouches; ++c)
                if (c >= 0) besoin[c] = true;
        }
        for (int c = 0; c < kNbCouches; ++c)
        {
            modulee_[c] = besoin[c];
            if (besoin[c]) couchesMod_[c] = p.couche[c];
        }

        for (const auto& r : p.routages)
        {
            if (r.source <= 0 || r.source > kNbSources || r.dest <= 0) continue;
            // /!\ Les destinations globales (les effets) ne passent PAS par
            // ici : il n'y a qu'une reverbe pour tout l'instrument.
            if (r.dest >= kNbDestinations) continue;
            const double x = v[r.source - 1] * r.dose;
            const int c0 = (r.couche <= 0) ? 0 : r.couche - 1;
            const int c1 = (r.couche <= 0) ? kNbCouches - 1 : r.couche - 1;
            for (int c = c0; c <= c1; ++c)
            {
                if (c < 0 || c >= kNbCouches) continue;
                if (r.dest < 8)
                {
                    auto& a = apports_[c];
                    switch ((Dest) r.dest)
                    {
                        case Dest::Hauteur:      a.hauteur      += x * 24.0;  break;
                        case Dest::Coupure:      a.coupure      += x * 4.0;   break;
                        case Dest::Niveau:       a.niveau       += x;         break;
                        case Dest::Desaccord:    a.desaccord    += x * 50.0;  break;
                        case Dest::Pan:          a.pan          += x;         break;
                        case Dest::CoupureGrain: a.coupureGrain += x * 2.0;   break;
                        case Dest::Morph:        a.morph        += x;         break;
                        default: break;
                    }
                }
                else if (modulee_[c])
                {
                    appliqueDestination (r.dest, x, couchesMod_[c]);
                }
            }
        }
    }

    void pasDeControle (const ReglagesPatch& p) noexcept
    {
        double v[kNbSources];
        litSources (p, v, true);

        // ---- le glide, au taux de controle
        // ⭐ A VITESSE CONSTANTE : `p.glide` secondes pour parcourir une octave.
        if (hauteurLissee_ != (double) note_)
        {
            if (p.glide <= 1e-4) hauteurLissee_ = (double) note_;
            else
            {
                const double pas = (12.0 / p.glide)
                                     * ((double) kPasControle / std::max (1.0, fs_));
                const double d = (double) note_ - hauteurLissee_;
                hauteurLissee_ += (std::abs (d) <= pas)
                                     ? d : (d > 0.0 ? pas : -pas);
            }
        }
        for (int i = 0; i < kNbCouches; ++i)
            deriveVoix_[i] = derives_[i].suivant (p.instab);

        calculeCouchesModulees (p, v);
        majHauteur (p);
    }

    bool libre() const noexcept { return libre_; }
    /** Vrai tant que la touche est tenue (avant le relachement). */
    bool tenue() const noexcept { return tenue_; }
    int  note()  const noexcept { return note_; }
    /** La hauteur reellement jouee, en numero de note fractionnaire. */
    double hauteurJouee() const noexcept { return hauteurLissee_; }
    /** L'aftertouch POLYPHONIQUE : une pression par touche. */
    void posePression (double x) noexcept { pression_ = x; }
    long age()   const noexcept { return age_; }

    inline void suivant (const ReglagesPatch& p, double wowCents,
                         double& g, double& d) noexcept
    {
        g = d = 0.0;
        if (libre_) return;
        wowCents_ = wowCents;
        ++age_;
        if (--compteur_ <= 0)
        {
            pasDeControle (p);
            for (int i = 0; i < kNbCouches; ++i)
                gainDerive_[i] = std::pow (10.0, deriveVoix_[i].dB / 20.0);
            compteur_ = kPasControle;
        }
        bool vivant = false;
        for (int i = 0; i < kNbCouches; ++i)
        {
            if (! actives_[i]) continue;
            double cg = 0.0, cd = 0.0;
            const ReglagesCouche& rc = modulee_[i] ? couchesMod_[i] : p.couche[i];
            couches_[i].suivant (rc, roms_[i], tailles_[i], cg, cd,
                                 apports_[i].niveau + gainDerive_[i] - 1.0,
                                 apports_[i].pan);
            // ⭐⭐ CHAQUE COUCHE GARDE SON BUS. Elles etaient sommees ici, et
            // c'est ce qui interdisait un rack d'effets par couche : apres la
            // somme, plus rien ne distingue la couche A de la couche C.
            //
            // /!\ Le bus est un ACCUMULATEUR, pas une affectation : plusieurs
            // VOIX ecrivent dans le meme bus de couche. Ecraser au lieu
            // d'ajouter ne laisserait sonner que la derniere note.
            if (bus_ != nullptr) { bus_[i * 2] += cg; bus_[i * 2 + 1] += cd; }
            g += cg; d += cd;
            vivant = vivant || couches_[i].active();
        }
        if (p.bruitActif)
        {
            const double b = bruit_.suivant (p.bruitNiveau);
            // le bruit est au centre : sa couleur est mesuree, pas sa position.
            g += b; d += b;
            vivant = vivant || bruit_.active();
        }
        if (! vivant) libre_ = true;
    }

private:
    Couche couches_[kNbCouches];
    CoucheBruit bruit_;
    Modulateur mods_[kNbMods];
    Derive     derives_[kNbCouches];
    ApportDerive deriveVoix_[kNbCouches];
    double     gainDerive_[kNbCouches] = { 1.0, 1.0, 1.0, 1.0 };
    double     wowCents_ = 0.0;
    ApportMod  apports_[kNbCouches];
    /** ⭐ Les reglages de couche APRES modulation generique. Ils ne sont a jour
        que si `modulee_[c]` est vrai - sinon la voix lit les reglages du patch
        directement, sans rien copier. */
    ReglagesCouche couchesMod_[kNbCouches];
    bool modulee_[kNbCouches] = { false, false, false, false };
    int compteur_ = 0;
    bool actives_[kNbCouches] = { false, false, false, false };
    /** Le gain de fondu de zone, fige au declenchement : une note ne doit pas
        changer de couche pendant qu'elle sonne. */
    double gainsZone_[kNbCouches] = { 1.0, 1.0, 1.0, 1.0 };
    /** Ou cette voix verse ses couches, quand on veut les separer. Pose par
        le Moteur avant chaque appel ; nul = on ne veut que la somme. */
    double* bus_ = nullptr;
    const double* roms_[kNbCouches] = { nullptr, nullptr, nullptr, nullptr };
    int tailles_[kNbCouches] = { 0, 0, 0, 0 };
    int  note_ = -1, velo_ = 0;
    /** La hauteur REELLEMENT jouee. Elle rejoint `note_` a la vitesse du
        glide - et vaut `note_` exactement quand le glide est a zero. */
    double hauteurLissee_ = -1.0;
    double pression_ = 0.0;        // aftertouch polyphonique, 0 si absent
    double fs_ = 44100.0;
    long age_ = 0;
    bool libre_ = true;
    bool enExtinction_ = false;
    bool tenue_ = false;
};

/** Le pool de voix, avec le vol. */
class Moteur
{
public:
    void prepare (double fs, int nVoix = kVoixMax) noexcept
    {
        voix_.resize ((size_t) std::max (1, std::min (nVoix, kVoixMax)));
        for (size_t i = 0; i < voix_.size(); ++i) voix_[i].prepare (fs, (int) i);
        // UNE horloge pour toute la machine : le wow est commun par nature.
        horl_.prepare (fs / (double) kPasControle);
        compteurH_ = 0;
    }

    /** ⭐⭐ LES TETES DE LECTURE - une par note qui sonne.

        /!\ APPELEE DEPUIS LE FIL AUDIO, une fois par bloc, jamais par
        echantillon. Elle ne fait que LIRE : aucune allocation, aucun verrou.
        L'editeur, lui, relit l'instantane quand il redessine - deux fils qui
        se croisent sur des flottants, donc au pire une tete qui saute d'un
        pixel. C'est le bon prix : verrouiller le fil audio pour dessiner une
        barre serait le mauvais.

        Rend le nombre de tetes ecrites dans `sortie`. */
    int positions (const ReglagesPatch& p, int couche, float* sortie,
                   int maxi) const noexcept
    {
        int n = 0;
        for (const auto& v : voix_)
        {
            if (n >= maxi) break;
            const double x = v.position (p, couche);
            if (x >= 0.0) sortie[n++] = (float) x;
        }
        return n;
    }

    void toutCouper() noexcept
    {
        for (auto& v : voix_) v.coupe();
        nPile_ = 0;
        // /!\ ON OUBLIE AUSSI LE DEPART DU GLIDE. Sans ca, la premiere note
        // apres un panic glisserait depuis une note jouee avant l'arret.
        derniereNote_ = -1.0;
    }

    void noteOn (const ReglagesPatch& p, int note, int velo,
                 const double* const* roms, const int* tailles) noexcept
    {
        const int limite = std::max (1, std::min (p.polyphonie, (int) voix_.size()));

        if (p.mode != ModeVoix::Poly)
        {
            // ⭐ PILE DE NOTES, comme sur les machines monophoniques : priorite
            // a la DERNIERE touche, et quand on la relache on redescend sur
            // celle qu'on tient encore. Sans pile, lacher une touche coupe le
            // son alors qu'un doigt est toujours pose - ce qui rend le jeu lie
            // impossible.
            empile (note, velo);

            // ⭐⭐ « SONNE ENCORE » N'EST PAS « EST TENUE », ET C'EST LE BUG QUE
            // L'UTILISATEUR A ENTENDU : « une note jouee apres une autre devient
            // plus faible, et une fois que la premiere est finie elle redevient
            // forte ».
            //
            // On testait si la voix SONNAIT. Une note relachee sonne encore
            // pendant tout son relachement : rejouer pendant ce temps-la
            // enchainait donc en legato sur une queue qui s'eteint - la
            // nouvelle note heritait du niveau residuel au lieu de repartir.
            // Mesure : 0,26 fois le niveau de la premiere en legato, 0,84 en
            // mono. Ce qu'il faut regarder, c'est si UNE TOUCHE EST ENCORE
            // ENFONCEE - c'est ca, jouer lie.
            const bool sonnait = voix_[0].tenue() && ! voix_[0].libre()
                                                  && ! voix_[0].enExtinction();
            if (! sonnait)
            {
                // ⭐ MEME EN MONO, ON GLISSE DEPUIS LA NOTE PRECEDENTE. Un
                // portamento qui ne marcherait qu'en legato obligerait a tenir
                // la touche pour l'entendre - or sur les machines mono, le
                // glide agit aussi quand on detache.
                const double dep = voix_[0].hauteurJouee();
                voix_[0].coupe();
                voix_[0].declenche (p, note, velo, roms, tailles, dep);
            }
            else if (p.mode == ModeVoix::Legato)
            {
                // /!\ L'ancienne version faisait `return` : elle gardait
                // l'ANCIENNE hauteur. Le legato ne faisait donc rien du tout.
                voix_[0].glisse (p, note);
            }
            else
            {
                // /!\ Mono : PAS de coupe() + declenche(). Ca repartait de zero
                // en phase ET en amplitude, donc un clic a chaque note. On
                // relance l'enveloppe en gardant le son en cours.
                voix_[0].relance (p, note, velo);
            }
            return;
        }

        // Voix qui TIENNENT une note (celles qui s'eteignent ne comptent pas).
        int nTenues = 0, plusVieille = -1;
        for (auto& v : voix_)
            if (! v.libre() && ! v.enExtinction())
            {
                ++nTenues;
                if (plusVieille < 0 || v.age() > voix_[(size_t) plusVieille].age())
                    plusVieille = (int) (&v - voix_.data());
            }

        // ⭐ ORDRE DE VOL, ET C'EST L'ORDRE DES MACHINES : on prend d'abord ce
        // qui est DEJA en train de mourir, et seulement ensuite ce qui sonne.
        //
        //   1. une voix libre
        //   2. une voix RELACHEE (la touche n'est plus tenue) - la plus vieille
        //   3. seulement en dernier recours, une voix TENUE - la plus vieille
        //
        // /!\ L'ancienne regle prenait directement la plus vieille TENUE. Sur
        // une nappe a deux doigts plus une melodie, c'est la nappe qui se
        // faisait manger a chaque note - alors que les notes en train de
        // s'eteindre, elles, occupaient le pool sans que personne ne les tienne.
        // Ce n'est pas la regle d'epoque : les machines conservent la note
        // tenue et reprennent celles qu'on a lachees.
        //
        // ⭐ Et la voix volee REND SA PLACE au lieu d'etre coupee net : elle
        // s'eteint en quelques millisecondes. Couper net creait une
        // discontinuite pleine echelle a chaque vol.
        if (nTenues >= limite)
        {
            int cible = plusVieilleRelachee();
            if (cible < 0) cible = plusVieille;
            if (cible >= 0)
            {
                voix_[(size_t) cible].rendLaPlace();
                ++volsEffectues_;
            }
        }

        int cible = -1;
        for (size_t i = 0; i < voix_.size(); ++i)
            if (voix_[i].libre()) { cible = (int) i; break; }
        if (cible < 0)   // pool sature : la plus vieille en extinction saute
        {
            cible = 0;
            for (size_t i = 1; i < voix_.size(); ++i)
                if (voix_[i].age() > voix_[(size_t) cible].age()) cible = (int) i;
            voix_[(size_t) cible].coupe();
        }
        // /!\ EN POLY, LE DEPART EST LA DERNIERE NOTE JOUEE, pas celle de la
        // voix reutilisee. Reprendre la hauteur de la voix qu'on recycle ferait
        // partir chaque note d'un endroit imprevisible - ce serait du hasard,
        // pas un portamento. Toutes les machines polyphoniques a glide font
        // ainsi : le glide y est un geste de MELODIE.
        voix_[(size_t) cible].declenche (p, note, velo, roms, tailles,
                                         derniereNote_);
        derniereNote_ = (double) note;
    }

    /** ⭐ L'AFTERTOUCH, LES DEUX FORMES. La pression de CANAL vit dans le
        patch (elle vaut pour tout l'instrument) ; la pression POLYPHONIQUE se
        pose sur la voix qui tient cette note-la. */
    void posePressionPoly (int note, double x) noexcept
    {
        for (auto& v : voix_)
            if (! v.libre() && v.note() == note) v.posePression (x);
    }

    /** La voix relachee la plus ancienne, ou -1. */
    int plusVieilleRelachee() const noexcept
    {
        int meilleur = -1;
        for (size_t i = 0; i < voix_.size(); ++i)
        {
            const auto& v = voix_[i];
            if (v.libre() || v.enExtinction() || v.tenue()) continue;
            if (meilleur < 0 || v.age() > voix_[(size_t) meilleur].age())
                meilleur = (int) i;
        }
        return meilleur;
    }

    void noteOff (const ReglagesPatch& p, int note) noexcept
    {
        if (p.mode != ModeVoix::Poly)
        {
            depile (note);
            if (nPile_ > 0)
            {
                // il reste un doigt pose : on y revient au lieu de se taire
                if (p.mode == ModeVoix::Legato) voix_[0].glisse (p, pile_[nPile_ - 1]);
                else                            voix_[0].relance (p, pile_[nPile_ - 1], 100);
            }
            else
            {
                voix_[0].relache();
            }
            return;
        }
        for (auto& v : voix_)
            if (! v.libre() && v.note() == note) v.relache();
    }

    inline void suivant (const ReglagesPatch& p, double& g, double& d) noexcept
    {
        g = d = 0.0;
        // L'horloge maitre avance au taux de controle, pour tout l'instrument.
        if (--compteurH_ <= 0) { wow_ = horl_.suivant (p.instab); compteurH_ = kPasControle; }
        for (auto& v : voix_)
        {
            double vg = 0.0, vd = 0.0;
            v.suivant (p, wow_, vg, vd);
            g += vg; d += vd;
        }
    }

    /** ⭐⭐ LA MEME CHOSE, MAIS COUCHE PAR COUCHE.

        `bus` recoit 2 x kNbCouches valeurs : gauche et droite de chaque couche.
        C'est ce qui permet un rack d'effets par tranche, comme sur une console.

        /!\ LE TOTAL RESTE RENDU, et ce n'est pas une commodite : l'appelant ne
        doit PAS avoir a resommer lui-meme. Deux additions ecrites a deux
        endroits finissent toujours par diverger d'un facteur de gain ou d'un
        panoramique, et le defaut ne s'entend qu'en comparant les deux chemins.

        /!\ Le tableau est remis a zero ICI, une fois par echantillon. Le
        laisser a l'appelant marche jusqu'au jour ou l'on ajoute un chemin qui
        oublie de le faire - et le son grossit alors sans fin. */
    inline void suivantParCouche (const ReglagesPatch& p, double* bus,
                                  double& g, double& d) noexcept
    {
        for (int i = 0; i < 2 * kNbCouches; ++i) bus[i] = 0.0;
        g = d = 0.0;
        if (--compteurH_ <= 0) { wow_ = horl_.suivant (p.instab); compteurH_ = kPasControle; }
        for (auto& v : voix_)
        {
            double vg = 0.0, vd = 0.0;
            v.poseBus (bus);
            v.suivant (p, wow_, vg, vd);
            v.poseBus (nullptr);
            g += vg; d += vd;
        }
    }

    /** A appeler une fois par bloc : les notes qui tiennent suivent la
        transposition et l'accord fin au lieu de rester figees. */
    void majHauteurs (const ReglagesPatch& p) noexcept
    {
        for (auto& v : voix_) v.majHauteur (p);
    }

    /** Vrai si UNE touche au moins est encore enfoncee.

        Sert au banc de modulation global : ses enveloppes se relachent quand la
        derniere touche se leve, pas quand une note quelconque se termine. */
    bool uneNoteTenue() const noexcept
    {
        for (const auto& v : voix_)
            if (! v.libre() && ! v.enExtinction() && v.tenue()) return true;
        return false;
    }

    /** Vrai si une voix TIENT encore cette note (touche pas relachee). */
    bool tientLaNote (int note) const noexcept
    {
        for (const auto& v : voix_)
            if (! v.libre() && ! v.enExtinction() && v.tenue() && v.note() == note)
                return true;
        return false;
    }

    int nbActives() const noexcept
    {
        int n = 0;
        for (const auto& v : voix_) if (! v.libre() && ! v.enExtinction()) ++n;
        return n;
    }
    int nbTotal() const noexcept
    {
        int n = 0;
        for (const auto& v : voix_) if (! v.libre()) ++n;
        return n;
    }
    long volsEffectues() const noexcept { return volsEffectues_; }
    double wowCourant() const noexcept { return wow_; }

private:
    void empile (int note, int) noexcept
    {
        depile (note);
        if (nPile_ < 16) pile_[nPile_++] = note;
    }
    void depile (int note) noexcept
    {
        int j = 0;
        for (int i = 0; i < nPile_; ++i) if (pile_[i] != note) pile_[j++] = pile_[i];
        nPile_ = j;
    }

    std::vector<Voix> voix_;
    int pile_[16] = {};
    int nPile_ = 0;
    /** La derniere note jouee, point de depart du glide. -1 = aucune. */
    double derniereNote_ = -1.0;
    Horlogerie horl_;
    double wow_ = 0.0;
    int compteurH_ = 0;
    long volsEffectues_ = 0;
};

} // namespace romance
