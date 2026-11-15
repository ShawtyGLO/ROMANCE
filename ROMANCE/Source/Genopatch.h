// =============================================================================
//  Genopatch.h - La resynthese DANS le plugin.
//
//  ⭐ LA RECHERCHE EST DEJA UN ARBRE. A chaque generation, un parent et lambda
//  enfants ; on garde le meilleur, qui devient le parent suivant. L'interface
//  Genopatch n'est donc pas une couche a plaquer par-dessus, c'est une VUE de
//  cette lignee - et c'est pour ca que les deux se rejoignent sans effort.
//
//  /!\ LA RECHERCHE NE TOURNE JAMAIS SUR LE THREAD AUDIO, ni sur le thread
//  message. Une resynthese, c'est des milliers de rendus : elle vit sur son
//  propre fil, et l'interface lit un instantane protege par un verrou. Le
//  thread audio, lui, ne voit RIEN de tout ca - il ne recoit qu'une recette
//  posee, par le chemin qui existe deja (`poseRecette`).
//
//  /!\ ET ELLE EST INTERRUPTIBLE A TOUT MOMENT. L'utilisateur choisit son temps
//  de generation ; une recherche qu'on ne peut pas arreter avant la fin serait
//  inutilisable des la premiere fois qu'il change d'avis.
// =============================================================================
#pragma once

#include <JuceHeader.h>
#include <vector>

#include "Perte.h"
#include "Recherche.h"

namespace romance
{

/** Ce que l'interface a le droit de lire, a tout instant. */
struct Instantane
{
    bool   enCours = false;
    double progression = 0.0;      // 0..1
    double perteCourante = 0.0;
    double perteDepart = 0.0;
    int    evaluations = 0;
    juce::String nomCible;
    std::vector<Generation> lignee;
    Recette meilleure;
    EnvAmpli enveloppe;

    /** ⭐ LE PROFIL COMPLET, et pas seulement la recette. C'est lui que la page
        de resynthese pose dans la couche : moteur, reglages du moteur, filtre,
        unisson, grain. La recette n'en est qu'une tranche - celle qui n'a de
        sens que si le vote de moteur a retenu la ROM. */
    uint8_t profil[kOctetsProfil] = {};
    double f0 = 0.0;                  // la hauteur retenue par le vote d'octave
    /** ⭐ Ce que la recherche fait EN CE MOMENT, pour que le panneau puisse le
        raconter au lieu de rester mort. */
    romance::PhaseRecherche phase = romance::PhaseRecherche::Repos;
    int    moteurEssaye = -1;
    double perteMoteur[4] = { 1e9, 1e9, 1e9, 1e9 };
    double f0Vue = 0.0;

    /** ⭐⭐ LES QUATRE GAGNANTS - un par generateur.

        L'ancienne page montrait jusqu'a 48 GENERATIONS d'une meme lignee. Ce
        n'etait pas un choix, c'etait un journal : chaque generation est
        l'ancetre de la suivante, et la derniere est par construction la
        meilleure de toutes. Retour de l'utilisateur : « aucune n'est montree
        comme etant dans les 4 gagnantes, c'est beaucoup trop de choix ».

        Ici, quatre resultats authentiquement DIFFERENTS - ROM, analogique, FM,
        table d'ondes - chacun etant le meilleur que son generateur sait faire.
        C'est le modele des quatre arbres de Synplant, et nous l'avions deja
        dans nos donnees sans jamais le montrer.

        /!\ `pretsMoteur` dit combien sont TERMINES, pas combien existent. La
        page doit pouvoir afficher le premier des que le premier est fini, sinon
        l'utilisateur attend quatre recherches devant un ecran vide. */
    uint8_t profils[4][kOctetsProfil] = {};
    double  pertes[4] = { 1e9, 1e9, 1e9, 1e9 };
    Recette recettes[4];
    EnvAmpli enveloppes[4];
    int     pretsMoteur = 0;
    /** Celui que la recherche affine EN CE MOMENT (0..3), -1 au repos. */
    int     moteurEnCours = -1;

    bool   aUneCible = false;
};

/** Le moteur de resynthese, sur son propre fil. */
class Genopatch : private juce::Thread
{
public:
    Genopatch() : juce::Thread ("ROMANCE resynthese") {}
    ~Genopatch() override { arrete(); }

    /** ⭐⭐ LE RESEAU, ENFIN BRANCHE.

        /!\ IL NE L'AVAIT JAMAIS ETE. `run()` appelait
        `resynthetise (cible, fs, cfg, r4, rm, rappel)` - sans le dernier
        argument, qui vaut `nullptr` par defaut. Tout le travail
        d'apprentissage - 280 000 exemples, deux phases, une distillation, un
        gain mesure de +0,314 dB sur l'heuristique - n'a jamais atteint le
        produit. Et rien ne le signalait : un argument par defaut ne se voit pas
        sur le site d'appel.

        /!\ NON POSSEDANT ET LU SANS VERROU. Le reseau est charge UNE FOIS a la
        construction du processeur, sur le fil message, et n'est plus jamais
        modifie ; le fil de recherche ne fait que le lire. Le recharger a chaud
        demanderait un verrou - ou, mieux, un echange de pointeur atomique - et
        ce n'est pas un besoin d'aujourd'hui. */
    void poseReseau (const Reseau* r) noexcept { reseau_ = r; }

    /** Charge un son a resynthetiser. Rend faux si le fichier est illisible. */
    bool chargeCible (const juce::File& f)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples < 4096) return false;

        const int n = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * 8.0));
        juce::AudioBuffer<float> tampon ((int) r->numChannels, n);
        r->read (&tampon, 0, n, 0, true, true);

        std::vector<double> x ((size_t) n, 0.0);
        for (int c = 0; c < tampon.getNumChannels(); ++c)
        {
            const float* p = tampon.getReadPointer (c);
            for (int i = 0; i < n; ++i) x[(size_t) i] += (double) p[i];
        }
        const double inv = 1.0 / juce::jmax (1, tampon.getNumChannels());
        for (auto& v : x) v *= inv;

        arrete();
        {
            const juce::ScopedLock v (verrou_);
            cible_ = std::move (x);
            fsCible_ = r->sampleRate;
            etat_.nomCible = f.getFileName();
            etat_.aUneCible = true;
            etat_.lignee.clear();
            etat_.evaluations = 0;
            etat_.progression = 0.0;
            etat_.phase = romance::PhaseRecherche::Repos;
            etat_.moteurEssaye = -1;
            for (auto& p : etat_.perteMoteur) p = 1e9;
        }
        return true;
    }

    /** Lance une recherche neuve depuis l'analyse. */
    void lance (int budget, const ConfigGrain& cfg)
    {
        arrete();
        const juce::ScopedLock v (verrou_);
        if (cible_.empty()) return;
        cfg_ = cfg;
        budget_ = juce::jmax (50, budget);
        depuisNoeud_ = false;
        startThread (juce::Thread::Priority::low);
    }

    /** ⭐ Repart d'un noeud CHOISI A LA MAIN - meme s'il n'est pas le meilleur.
        C'est ce geste qui rend l'outil jouable : on garde une version parce
        qu'elle plait, pas parce qu'elle gagne. */
    void affineDepuis (const Noeud& n, int budget, const ConfigGrain& cfg)
    {
        arrete();
        const juce::ScopedLock v (verrou_);
        if (cible_.empty()) return;
        cfg_ = cfg;
        budget_ = juce::jmax (50, budget);
        noeud_ = n;
        depuisNoeud_ = true;
        startThread (juce::Thread::Priority::low);
    }

    void arrete()
    {
        signalThreadShouldExit();
        // /!\ 3 SECONDES NE SUFFISAIENT PLUS. `stopThread` TUE le fil quand le
        // delai expire ; depuis que la recherche evalue ses enfants en
        // parallele, un fil tue laisse derriere lui des std::thread non joints
        // et le processus tombe. Une generation coute au pire quelques
        // centaines de millisecondes et le rappel est desormais interroge a
        // chaque couvee : 15 s est une marge, pas une attente.
        stopThread (15000);
    }

    Instantane lis() const
    {
        const juce::ScopedLock v (verrou_);
        return etat_;
    }

    bool tourne() const { return isThreadRunning(); }

private:
    /** ⭐ Non possedant : il appartient au processeur, qui vit plus longtemps
        que ce fil. Nul = heuristique seule, ce qui reste un mode valide. */
    const Reseau* reseau_ = nullptr;
    void run() override
    {
        std::vector<double> cible;
        double fs = 44100.0;
        int budget = 1000;
        ConfigGrain cfg;
        bool depuisNoeud = false;
        Noeud noeud;
        {
            const juce::ScopedLock v (verrou_);
            cible = cible_; fs = fsCible_; budget = budget_;
            cfg = cfg_; depuisNoeud = depuisNoeud_; noeud = noeud_;
            etat_.enCours = true;
            etat_.progression = 0.0;
            etat_.lignee.clear();
        }

        ReglagesRecherche reg;
        reg.evaluations = budget;
        reg.collecteLignee = true;
        reg.graine = (unsigned int) juce::Random::getSystemRandom().nextInt (100000);

        auto rappel = [this, budget] (const EtatRecherche& e)
        {
            {
                const juce::ScopedLock v (verrou_);
                etat_.progression = juce::jlimit (0.0, 1.0,
                                        (double) e.evaluationsFaites / (double) budget);
                etat_.phase = e.phase;
                etat_.moteurEssaye = e.moteurEssaye;
                etat_.f0Vue = e.f0Vue;
                for (int i = 0; i < 4; ++i)
                    if (e.perteMoteur[i] < 1e8) etat_.perteMoteur[i] = e.perteMoteur[i];
                // /!\ Pendant l'analyse et les votes il n'y a pas encore de
                // perte a montrer : l'ecrire quand meme afficherait 1e9.
                if (e.phase != romance::PhaseRecherche::Affinage) return ! threadShouldExit();
                etat_.perteCourante = e.perteMeilleure;
                etat_.perteDepart = e.perteDepart;
                etat_.evaluations = e.evaluationsFaites;
                etat_.meilleure = e.meilleure;
                etat_.enveloppe = e.enveloppe;
                for (int i = 0; i < kOctetsProfil; ++i) etat_.profil[i] = e.profil[i];
                etat_.lignee = e.lignee;
            }
            return ! threadShouldExit();
        };

        EtatRecherche st;
        RapportAnalyse rap;
        double f0 = 0.0;
        { const juce::ScopedLock v (verrou_); f0 = etat_.f0; }
        if (f0 <= 0.0) f0 = 220.0;
        if (depuisNoeud)
        {
            // on refabrique l'empreinte de la cible comme le fait resynthetise
            const int onset = trouveOnset (cible);
            const std::vector<double> cale (cible.begin() + onset, cible.end());
            ReglagesRecherche r2 = reg;
            r2.nEchComparaison = juce::jlimit (reg.fenetreMin, reg.fenetreMax,
                                               (int) cale.size());
            std::vector<double> c (cale.begin(),
                cale.begin() + juce::jmin ((size_t) r2.nEchComparaison, cale.size()));
            // /!\ La hauteur ne se redevine PAS ici : elle a ete tranchee par
            // le vote d'octave de la recherche initiale, et rejouer l'analyse
            // pourrait affiner depuis une octave differente de celle qu'on
            // affiche - l'utilisateur verrait sa branche changer de note.
            st = affine (empreinte (c, fs, kPlancherPropre), fs, f0, noeud,
                         r2, rappel);
        }
        else
        {
            // ⭐⭐ QUATRE RECHERCHES COMPLETES, une par generateur.
            //
            // /!\ LE BUDGET EST DIVISE PAR QUATRE, ET CA NE COUTE PRESQUE
            // RIEN. Mesure de l'oracle : huit fois plus d'evaluations
            // n'achetent que 0,27 dB, a peine au-dessus du bruit du banc. Le
            // budget n'est pas ce qui limite cette recherche. Sans cette
            // mesure, diviser par quatre aurait paru inconsequent.
            {
                const juce::ScopedLock v (verrou_);
                etat_.pretsMoteur = 0;
                for (int m = 0; m < 4; ++m) etat_.pertes[m] = 1e9;
            }

            double meilleure = 1e9;
            for (int m = 0; m < 4 && ! threadShouldExit(); ++m)
            {
                ReglagesRecherche r4 = reg;
                r4.moteurImpose = m;
                r4.evaluations = std::max (200, reg.evaluations / 4);
                {
                    const juce::ScopedLock v (verrou_);
                    etat_.moteurEnCours = m;
                }
                RapportAnalyse rm;
                const auto e = resynthetise (cible, fs, cfg, r4, rm, rappel,
                                             reseau_);
                {
                    const juce::ScopedLock v (verrou_);
                    for (int i = 0; i < kOctetsProfil; ++i)
                        etat_.profils[m][i] = e.profil[i];
                    etat_.pertes[m] = e.perteMeilleure;
                    etat_.recettes[m] = e.meilleure;
                    etat_.enveloppes[m] = e.enveloppe;
                    etat_.pretsMoteur = m + 1;
                }
                // ⭐ Le « resultat courant » reste le MEILLEUR des quatre :
                // c'est lui que POSER utilise quand l'utilisateur ne choisit
                // pas explicitement une tranche.
                if (e.perteMeilleure < meilleure)
                {
                    meilleure = e.perteMeilleure;
                    st = e;
                    if (rm.f0 > 0.0) f0 = rm.f0;
                    rap = rm;
                }
            }
            {
                const juce::ScopedLock v (verrou_);
                etat_.moteurEnCours = -1;
            }
        }

        const juce::ScopedLock v (verrou_);
        etat_.enCours = false;
        etat_.phase = romance::PhaseRecherche::Repos;
        etat_.moteurEssaye = -1;
        etat_.meilleure = st.meilleure;
        etat_.enveloppe = st.enveloppe;
        for (int i = 0; i < kOctetsProfil; ++i) etat_.profil[i] = st.profil[i];
        etat_.f0 = f0;
        etat_.perteCourante = st.perteMeilleure;
        etat_.perteDepart = st.perteDepart;
        etat_.lignee = st.lignee;
        etat_.progression = 1.0;
    }

    juce::CriticalSection verrou_;
    std::vector<double> cible_;
    double fsCible_ = 44100.0;
    int budget_ = 1000;
    ConfigGrain cfg_;
    Noeud noeud_;
    bool depuisNoeud_ = false;
    Instantane etat_;
};

} // namespace romance
