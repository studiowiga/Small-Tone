#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
// Géométrie du visuel (pixels de background.png : 1520 x 1916, même format que Holyverb)
namespace art
{
constexpr float W = 1520.0f, H = 1916.0f;
struct Circle { float x, y, r; };
constexpr Circle knob      { 759.7f, 401.1f, 164.4f };    // potard RATE (zone cliquable)
constexpr float  indexR0 = 82.2f, indexR1 = 143.9f, indexW = 9.6f;   // index blanc du potard
constexpr float  knobSweep = 2.356194f;                   // ±135° (graduations de la sérigraphie)
constexpr Circle foot      { 760.0f, 1418.5f, 89.9f };    // footswitch
constexpr Circle status    { 1073.0f, 410.9f, 36.0f };    // LED STATUS
const juce::Rectangle<float> glowRate   { 489.0f, 131.0f, 540.0f, 539.0f };
const juce::Rectangle<float> glowFoot   { 580.0f, 1238.0f, 359.0f, 360.0f };
const juce::Rectangle<float> glowStatus { 990.0f, 328.0f, 165.0f, 165.0f };
const juce::Rectangle<float> labelSync  { 1107.0f, 366.0f, 201.0f, 89.0f };   // inscription SYNC (image avec marge)
const juce::Rectangle<float> hitSync    { 1130.0f, 385.0f, 155.0f, 50.0f };    // zone cliquable du mot SYNC
const juce::Rectangle<float> toggle     { 364.0f, 338.0f, 152.0f, 142.0f };
const juce::Rectangle<float> labelDin   { 389.0f, 276.0f, 92.0f, 48.0f };     // "DIN."
const juce::Rectangle<float> labelDepth { 314.0f, 508.0f, 160.0f, 52.0f };    // "DEPTH"
constexpr float statusLineX = 758.5f;  // centre de la ligne d'état
constexpr float statusLineY = 1797.9f;    // ligne "USA" d'origine (centre)
constexpr float statusCapH  = 23.2f;      // hauteur des capitales de "USA"
}

//==============================================================================
class SmallToneEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit SmallToneEditor (SmallToneProcessor&);
    ~SmallToneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void useSoftwareRenderer();
    float scale() const { return (float) getWidth() / art::W; }
    juce::Point<float> toArt (juce::Point<float> p) const { return p / scale(); }
    juce::Rectangle<float> toWin (juce::Rectangle<float> r) const { const float s = scale(); return { r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s }; }
    enum class Hit { none, knob, toggle, din, depth, foot, led };
    Hit hitAt (juce::Point<float> artPos) const;

    void setParam (const juce::String& id, float plain);
    void setRate (float r01);
    void flash (const juce::String& text, double seconds = 1.6);
    juce::String statusText() const;
    float statusAlpha() const;
    static constexpr double kStatusFade = 0.6;   // fondu de sortie de la ligne d'état (s)
    juce::String syncText() const;
    juce::String rateText() const;
    void showContextMenu();
    void setWindowHeight (int h);
    void askUserPresetName (int slot);
    void rebuildScaledImages();
    void paintIndex (juce::Graphics&) const;
    void paintStatusLine (juce::Graphics&) const;

    SmallToneProcessor& proc;
    juce::Image background, glowRate, glowFoot, glowStatus, toggleUp, toggleDown, toggleMid, labelSync, labelSyncGlow;
    juce::Image sBackground, sGlowRate, sGlowFoot, sGlowStatus, sToggleUp, sToggleDown, sToggleMid, sLabelSync, sLabelSyncGlow;
    juce::Typeface::Ptr labelFace;
    float capRatio = 0.72f, capTopRatio = -0.72f;    // capitales : hauteur et haut (y) / hauteur de police

    float ringLevel = 1.0f, ledLevel = 1.0f, footLevel = 1.0f;
    bool footDown = false;
    Hit dragging = Hit::none;
    float dragStartRate = 0.0f;
    juce::Point<float> dragStart;
    juce::String flashText;
    double flashUntil = 0.0;
    juce::String lastStatus;
    float lastRate = -1.0f; bool lastDeep = false, lastOn = false;
    int lastLearnShown = -1;
    bool shownDeep = true;          // position du levier affichée
    double toggleMidUntil = 0.0;
    float ledFlash = 0.0f; bool ledArmed = false;
    float syncGlow = 0.0f;           // halo du mot SYNC quand la synchro est active
    juce::String lastPresetShown;    // dernier nom de preset affiché

    // dernier état réellement dessiné : on ne redessine une zone que si elle a changé
    float drawnRate = -1.0f, drawnRing = -1.0f, drawnLed = -1.0f, drawnSync = -1.0f, drawnFoot = -1.0f, drawnStatusAlpha = -1.0f;
    bool drawnDeep = false, drawnToggleMoving = false, drawnFootDown = false;
    juce::String drawnStatus;

    std::unique_ptr<juce::AlertWindow> nameDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmallToneEditor)
};
