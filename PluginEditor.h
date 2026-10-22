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
constexpr Circle knob      { 763.2f, 407.7f, 163.0f };    // potard RATE (zone cliquable)
constexpr float  indexR0 = 81.5f, indexR1 = 142.7f, indexW = 9.5f;   // index blanc du potard
constexpr float  knobSweep = 2.356194f;                   // ±135° (graduations de la sérigraphie)
constexpr Circle foot      { 767.6f, 1417.9f, 89.2f };    // footswitch
constexpr Circle status    { 1075.3f, 414.1f, 35.7f };    // LED STATUS
const juce::Rectangle<float> glowRate   { 495.0f, 140.0f, 535.0f, 535.0f };
const juce::Rectangle<float> glowFoot   { 589.0f, 1239.0f, 356.0f, 357.0f };
const juce::Rectangle<float> glowStatus { 993.0f, 332.0f, 163.0f, 163.0f };
const juce::Rectangle<float> toggle     { 370.0f, 350.0f, 150.0f, 140.0f };
const juce::Rectangle<float> labelDin   { 395.0f, 288.0f, 92.0f, 48.0f };     // "DIN."
const juce::Rectangle<float> labelDepth { 320.0f, 520.0f, 160.0f, 52.0f };    // "DEPTH"
constexpr float statusLineY = 1789.9f;    // ligne "USA" d'origine (centre)
constexpr float statusCapH  = 23.0f;      // hauteur des capitales de "USA"
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

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    float scale() const { return (float) getWidth() / art::W; }
    juce::Point<float> toArt (juce::Point<float> p) const { return p / scale(); }
    juce::Rectangle<float> toWin (juce::Rectangle<float> r) const { const float s = scale(); return { r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s }; }
    enum class Hit { none, knob, toggle, din, depth, foot };
    Hit hitAt (juce::Point<float> artPos) const;

    void setParam (const juce::String& id, float plain);
    void setRate (float r01);
    void flash (const juce::String& text, double seconds = 1.6);
    juce::String statusText() const;
    juce::String rateText() const;
    void showContextMenu();
    void setWindowHeight (int h);
    void askUserPresetName (int slot);
    void rebuildScaledImages();
    void paintIndex (juce::Graphics&) const;
    void paintStatusLine (juce::Graphics&) const;

    SmallToneProcessor& proc;
    juce::Image background, glowRate, glowFoot, glowStatus, toggleUp, toggleDown;
    juce::Image sBackground, sGlowRate, sGlowFoot, sGlowStatus, sToggleUp, sToggleDown;
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

    std::unique_ptr<juce::AlertWindow> nameDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmallToneEditor)
};
