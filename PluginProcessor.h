#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SmallToneEngine.h"

namespace stui
{
// tailles de fenêtre identiques à Holyverb (et Freeze Shimmer / Tuna) :
// 100 % = 986 px de haut, ouverture par défaut = 493 px (= taille minimale)
constexpr int heightDefault = 493, height60 = 592, height75 = 740, height100 = 986, heightMax = 1479;
}

struct FactoryPreset { const char* name; float rate; bool deep; };

class SmallToneProcessor : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    SmallToneProcessor();
    ~SmallToneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Small Tone"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.02; }

    // presets usine = programmes VST3 (menu déroulant de presets de la fenêtre du DAW)
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ----- presets
    static const std::vector<FactoryPreset>& factoryPresets();
    juce::String presetName;                 // nom affiché sur la ligne d'état (vide = réglage libre)
    void applyPreset (float rate, bool deep, const juce::String& name);
    // presets utilisateur 1 … 9 (communs à toutes les instances)
    static juce::File userPresetFile();
    juce::String userPresetName (int slot) const;          // "" si vide
    bool loadUserPreset (int slot);
    void saveUserPreset (int slot, const juce::String& name);

    // ----- MIDI
    // v1.2 MIDI Learn : toutes les commandes de la façade (0 footswitch, 1 rate, 2 depth, 3 sync / LED STATUS)
    // numéros par défaut = anciens CC 80 à 82 (+ 83 pour SYNC, comme Phase 90)
    struct LearnDef { const char* key; const char* name; int defaultCC; };
    static constexpr int numLearn = 4;
    static const LearnDef& learnDef (int i);
    std::atomic<int> ccMap[numLearn];         // numéro de CC de chaque commande, -1 = non assigné
    std::atomic<int> learnTarget { -1 };      // commande en attente d'un CC (-1 = pas de learn)
    std::atomic<int> lastLearned { -1 };      // pour l'affichage
    void resetMidiMap();
    std::atomic<int> uiHeight { stui::heightDefault };

    // ----- infos pour l'interface
    std::atomic<float> lfoVisual { 0.5f }, delayVisual { 4.0f };
    std::atomic<double> hostBpm { 120.0 };
    static juce::String syncDivisionName (int i);
    static double syncDivisionBeats (int i);
    static int syncDivisionIndex (float rate01);
    static constexpr int numSyncDivisions = 9;

    bool isOn() const     { return pOn->load (std::memory_order_relaxed) > 0.5f; }
    bool isDeep() const   { return pDepth->load (std::memory_order_relaxed) > 0.5f; }
    float getRate() const { return pRate->load (std::memory_order_relaxed); }

private:
    void timerCallback() override;      // anti-gel v2 : vide la file du MIDI (thread message, 60 Hz)

    // valeurs lues par le thread audio : pointeurs obtenus une fois dans le constructeur
    std::atomic<float>* pRate = nullptr, *pDepth = nullptr, *pOn = nullptr, *pSync = nullptr, *pStereo = nullptr,
                      * pNoise = nullptr, *pMidiChan = nullptr, *pMomentary = nullptr;
    void setParam (const juce::String& id, float plainValue);

    smalltone::Engine engine;
    int currentProgram = 0;

    // changements venant du MIDI (thread audio) appliqués sur le thread de l'interface
    std::atomic<float> pendingRate { -1.0f };
    std::atomic<int> pendingDepth { -1 }, pendingFootToggle { 0 }, pendingFootSet { -1 }, pendingProgram { -1 }, pendingSync { -1 };
    bool footCcWasHigh = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmallToneProcessor)
};
