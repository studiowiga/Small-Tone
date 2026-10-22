#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

static Image loadImg (const void* d, int n) { return ImageFileFormat::loadFrom (d, (size_t) n); }

//==============================================================================
SmallToneEditor::SmallToneEditor (SmallToneProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    background = loadImg (BinaryData::background_jpg, BinaryData::background_jpgSize);
    glowRate   = loadImg (BinaryData::glow_rate_png, BinaryData::glow_rate_pngSize);
    glowFoot   = loadImg (BinaryData::glow_foot_png, BinaryData::glow_foot_pngSize);
    glowStatus = loadImg (BinaryData::glow_status_png, BinaryData::glow_status_pngSize);
    toggleUp   = loadImg (BinaryData::toggle_up_png, BinaryData::toggle_up_pngSize);
    toggleDown = loadImg (BinaryData::toggle_down_png, BinaryData::toggle_down_pngSize);
    toggleMid  = loadImg (BinaryData::toggle_mid_png, BinaryData::toggle_mid_pngSize);
    labelFace  = Typeface::createSystemTypefaceFor (BinaryData::SmallToneSans_ttf, BinaryData::SmallToneSans_ttfSize);

    {   // encre réelle du "H" : hauteur et position des capitales, pour coller au "USA" d'origine
        GlyphArrangement ga;
        ga.addLineOfText (Font (FontOptions (labelFace).withHeight (100.0f)), "H", 0, 0);
        Path h; ga.createPath (h);
        const auto b = h.getBounds();
        if (b.getHeight() > 1.0f) { capRatio = b.getHeight() / 100.0f; capTopRatio = b.getY() / 100.0f; }
    }

    // taille : identique à Holyverb, jamais plus petite que la taille par défaut
    const double aspect = (double) art::W / (double) art::H;
    const int h0 = jlimit (stui::heightDefault, stui::heightMax, proc.uiHeight.load());   // lu avant setResizeLimits (qui appelle resized)
    setResizable (true, true);
    setResizeLimits (roundToInt (stui::heightDefault * aspect), stui::heightDefault,
                     roundToInt (stui::heightMax * aspect), stui::heightMax);
    getConstrainer()->setFixedAspectRatio (aspect);
    setSize (roundToInt (h0 * aspect), h0);

    ringLevel = ledLevel = footLevel = proc.isOn() ? 1.0f : 0.0f;
    shownDeep = proc.isDeep();
    lastPresetShown = proc.presetName;      // pas de message à l'ouverture
    startTimerHz (60);
}

SmallToneEditor::~SmallToneEditor() { stopTimer(); }

//==============================================================================
void SmallToneEditor::resized()
{
    const int minW = roundToInt (stui::heightDefault * (double) art::W / (double) art::H);
    if (getHeight() < stui::heightDefault || getWidth() < minW) { setSize (minW, stui::heightDefault); return; }
    proc.uiHeight.store (getHeight());
    rebuildScaledImages();
}

void SmallToneEditor::setWindowHeight (int h)
{
    h = jlimit (stui::heightDefault, stui::heightMax, h);
    setSize (roundToInt ((double) h * art::W / art::H), h);
}

void SmallToneEditor::rebuildScaledImages()
{
    // images pré-redimensionnées une seule fois (affichage fluide à 60 i/s)
    const float s = scale();
    auto rs = [s] (const Image& im) -> Image
    {
        if (! im.isValid()) return {};
        const int w = jmax (1, roundToInt (im.getWidth() * s)), h = jmax (1, roundToInt (im.getHeight() * s));
        return im.rescaled (w, h, Graphics::highResamplingQuality);
    };
    if (background.isValid()) sBackground = background.rescaled (getWidth(), getHeight(), Graphics::highResamplingQuality);
    sGlowRate = rs (glowRate); sGlowFoot = rs (glowFoot); sGlowStatus = rs (glowStatus);
    sToggleUp = rs (toggleUp); sToggleDown = rs (toggleDown); sToggleMid = rs (toggleMid);
}

//==============================================================================
String SmallToneEditor::rateText() const
{
    const float r = proc.getRate();
    if (proc.apvts.getRawParameterValue ("sync")->load() > 0.5f)
        return "SYNC " + SmallToneProcessor::syncDivisionName (SmallToneProcessor::syncDivisionIndex (r));
    const double hz = smalltone::Engine::rateHz (r);
    return String (hz, hz < 1.0 ? 2 : 1) + " HZ";
}

String SmallToneEditor::syncText() const
{
    return SmallToneProcessor::syncDivisionName (SmallToneProcessor::syncDivisionIndex (proc.getRate()))
           + "  @ " + String (proc.hostBpm.load(), 0) + " BPM";
}

String SmallToneEditor::statusText() const
{
    // la ligne d'état n'apparaît que pendant un réglage, puis s'efface (voir statusAlpha)
    if (proc.learnTarget.load() >= 0) return "MIDI LEARN...";
    if (Time::getMillisecondCounterHiRes() * 0.001 < flashUntil + kStatusFade) return flashText;
    return {};
}

float SmallToneEditor::statusAlpha() const
{
    if (proc.learnTarget.load() >= 0) return 1.0f;
    const double left = flashUntil + kStatusFade - Time::getMillisecondCounterHiRes() * 0.001;
    return (float) jlimit (0.0, 1.0, left / kStatusFade);
}

void SmallToneEditor::flash (const String& t, double seconds)
{
    flashText = t;
    flashUntil = Time::getMillisecondCounterHiRes() * 0.001 + seconds;
    repaint();
}

void SmallToneEditor::setParam (const String& id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void SmallToneEditor::setRate (float r)
{
    if (auto* p = proc.apvts.getParameter ("rate"))
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, r));
    proc.presetName = {};
    flash ("RATE " + rateText());
}

//==============================================================================
void SmallToneEditor::timerCallback()
{
    const bool on = proc.isOn();
    const bool follow = proc.apvts.getRawParameterValue ("ringlfo")->load() > 0.5f;
    const float lfo = proc.lfoVisual.load();
    const float ringTarget = on ? (follow ? 0.42f + 0.58f * lfo : 1.0f) : 0.0f;
    const float k = 0.35f;   // LED : allumage rapide mais pas instantané
    ringLevel += (ringTarget - ringLevel) * k;
    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    float ledTarget = on ? 1.0f : 0.0f;
    if (sync)
    {
        // synchro tempo : la LED STATUS clignote à chaque cycle du LFO (donc en mesure avec le DAW)
        if (lfo > 0.96f && ! ledArmed) { ledFlash = 1.0f; ledArmed = true; }
        if (lfo < 0.5f) ledArmed = false;
        ledFlash *= 0.84f;
        ledTarget = on ? 0.22f + 0.78f * ledFlash : 0.55f * ledFlash;
    }
    ledLevel  += (ledTarget - ledLevel) * (sync ? 0.6f : k);
    // preset changé par le DAW / MIDI : nom affiché brièvement
    if (proc.presetName != lastPresetShown)
    {
        lastPresetShown = proc.presetName;
        if (lastPresetShown.isNotEmpty()) flash (lastPresetShown.toUpperCase());
    }
    footLevel += ((on ? 1.0f : 0.0f) - footLevel) * k;

    // switch : à chaque changement (souris, MIDI, automation) le levier passe par le point mort
    if (proc.isDeep() != shownDeep)
    {
        shownDeep = proc.isDeep();
    lastPresetShown = proc.presetName;      // pas de message à l'ouverture
        toggleMidUntil = Time::getMillisecondCounterHiRes() * 0.001 + 0.045;
    }

    const int learned = proc.lastLearned.exchange (-1);
    if (learned >= 0)
    {
        const int cc = learned == 0 ? proc.ccFoot.load() : learned == 1 ? proc.ccRate.load() : proc.ccDepth.load();
        flash (String (learned == 0 ? "FOOTSWITCH" : learned == 1 ? "RATE" : "DEPTH") + " = CC " + String (cc), 2.0);
    }

    // on ne redessine que ce qui bouge (potard, LED, switch, footswitch, ligne d'état)
    repaint (toWin (art::glowRate).toNearestInt().expanded (2));
    repaint (toWin (art::glowStatus).toNearestInt().expanded (2));
    repaint (toWin (art::glowFoot).toNearestInt().expanded (2));
    repaint (toWin (art::toggle).toNearestInt().expanded (2));
    repaint (toWin ({ 160.0f, art::statusLineY - 40.0f, art::W - 320.0f, 80.0f }).toNearestInt());
}

//==============================================================================
void SmallToneEditor::paint (Graphics& g)
{
    g.fillAll (Colour (7, 7, 7));
    if (sBackground.isValid()) g.drawImageAt (sBackground, 0, 0);
    const float s = scale();

    auto drawOverlay = [&] (const Image& im, Rectangle<float> r, float alpha)
    {
        if (alpha <= 0.003f || ! im.isValid()) return;
        g.setOpacity (jlimit (0.0f, 1.0f, alpha));
        g.drawImageAt (im, roundToInt (r.getX() * s), roundToInt (r.getY() * s));
        g.setOpacity (1.0f);
    };

    // switch DEPTH : levier en haut = "DIN.", en bas = "DEPTH"
    {
        const bool moving = Time::getMillisecondCounterHiRes() * 0.001 < toggleMidUntil;
        const auto& im = moving ? sToggleMid : (shownDeep ? sToggleDown : sToggleUp);
        if (im.isValid()) g.drawImageAt (im, roundToInt (art::toggle.getX() * s), roundToInt (art::toggle.getY() * s));
    }

    // lumières (calques photo allumés, fondus selon l'état)
    drawOverlay (sGlowRate, art::glowRate, ringLevel);
    drawOverlay (sGlowStatus, art::glowStatus, ledLevel);
    drawOverlay (sGlowFoot, art::glowFoot, footLevel);

    // footswitch enfoncé
    if (footDown)
    {
        const float r = art::foot.r * 0.62f * s;
        const Point<float> c (art::foot.x * s, art::foot.y * s + 1.5f * s);
        ColourGradient sh (Colours::black.withAlpha (0.35f), c.x, c.y - r, Colours::black.withAlpha (0.12f), c.x, c.y + r, false);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }

    paintIndex (g);
    paintStatusLine (g);
}

void SmallToneEditor::paintIndex (Graphics& g) const
{
    // index blanc sérigraphié sur le chapeau : seul l'index tourne, l'éclairage du métal reste fixe
    const float s = scale();
    const float a = (proc.getRate() - 0.5f) * 2.0f * art::knobSweep;
    const Point<float> c (art::knob.x * s, art::knob.y * s);
    const float w = art::indexW * s;
    Path bar;
    bar.addRoundedRectangle (-w * 0.5f, -art::indexR1 * s, w, (art::indexR1 - art::indexR0) * s, w * 0.45f);
    const auto tr = AffineTransform::rotation (a).translated (c);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillPath (bar, tr.translated (0.6f * s, 1.4f * s));
    ColourGradient ink (Colour (246, 246, 248), c.x - 40 * s, c.y - 140 * s, Colour (200, 200, 206), c.x + 60 * s, c.y + 120 * s, false);
    g.setGradientFill (ink);
    g.fillPath (bar, tr);
}

void SmallToneEditor::paintStatusLine (Graphics& g) const
{
    // même police et même taille que le "USA" de la sérigraphie d'origine (à la place du "USA")
    const float s = scale();
    const String t = statusText();
    if (t.isEmpty()) return;
    float fontH = art::statusCapH / capRatio * s;
    Font f (FontOptions (labelFace).withHeight (fontH));
    f.setExtraKerningFactor (0.04f);
    GlyphArrangement ga;
    ga.addLineOfText (f, t, 0, 0);
    Path p; ga.createPath (p);
    auto bb = p.getBounds();
    const float maxW = 900.0f * s;
    if (bb.getWidth() > maxW)
    {
        fontH *= maxW / bb.getWidth();
        f = f.withHeight (fontH);
        ga.clear(); ga.addLineOfText (f, t, 0, 0);
        p.clear(); ga.createPath (p);
        bb = p.getBounds();
    }
    const float capTop = art::statusLineY * s - art::statusCapH * 0.5f * s;
    const float dx = art::statusLineX * s - bb.getCentreX();
    const float dy = capTop - capTopRatio * fontH;
    g.setColour (Colour (218, 218, 222).withAlpha (0.94f * statusAlpha()));
    g.fillPath (p, AffineTransform::translation (dx, dy));
}

//==============================================================================
SmallToneEditor::Hit SmallToneEditor::hitAt (Point<float> a) const
{
    if (a.getDistanceFrom ({ art::knob.x, art::knob.y }) <= art::knob.r) return Hit::knob;
    if (art::toggle.contains (a)) return Hit::toggle;
    if (art::labelDin.contains (a)) return Hit::din;
    if (art::labelDepth.contains (a)) return Hit::depth;
    if (a.getDistanceFrom ({ art::foot.x, art::foot.y }) <= art::foot.r * 1.12f) return Hit::foot;
    if (a.getDistanceFrom ({ art::status.x, art::status.y }) <= art::status.r * 1.6f) return Hit::led;
    return Hit::none;
}

void SmallToneEditor::mouseMove (const MouseEvent& e)
{
    const auto h = hitAt (toArt (e.position));
    setMouseCursor (h == Hit::none ? MouseCursor::NormalCursor
                  : h == Hit::knob ? MouseCursor::UpDownResizeCursor : MouseCursor::PointingHandCursor);
}

void SmallToneEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    const auto h = hitAt (toArt (e.position));
    dragging = Hit::none;
    switch (h)
    {
        case Hit::knob:
            dragging = Hit::knob; dragStart = e.position; dragStartRate = proc.getRate();
            if (auto* p = proc.apvts.getParameter ("rate")) p->beginChangeGesture();
            flash ("RATE " + rateText());
            break;
        case Hit::toggle:
        case Hit::din:
        case Hit::depth:
        {
            const bool deep = h == Hit::toggle ? ! proc.isDeep() : (h == Hit::depth);
            setParam ("depth", deep ? 1.0f : 0.0f);
            proc.presetName = {};
            flash (deep ? "DEPTH : DEEP" : "DIN. : SHALLOW");
            break;
        }
        case Hit::foot:
            footDown = true;
            setParam ("on", proc.isOn() ? 0.0f : 1.0f);
            flash (proc.isOn() ? "ON" : "BYPASS", 1.0);
            break;
        case Hit::led:
        {
            // LED STATUS = bouton de synchro tempo avec le DAW
            const bool s = proc.apvts.getRawParameterValue ("sync")->load() < 0.5f;
            setParam ("sync", s ? 1.0f : 0.0f);
            flash (s ? "TEMPO SYNC  -  " + syncText() : "TEMPO SYNC OFF  -  " + rateText(), 2.2);
            break;
        }
        case Hit::none: break;
    }
    repaint();
}

void SmallToneEditor::mouseDrag (const MouseEvent& e)
{
    if (dragging != Hit::knob) return;
    const auto d = e.position - dragStart;
    const float range = 260.0f * scale() * (e.mods.isShiftDown() ? 5.0f : 1.0f);
    setRate (dragStartRate + (d.x - d.y) / range);
}

void SmallToneEditor::mouseUp (const MouseEvent&)
{
    if (dragging == Hit::knob)
        if (auto* p = proc.apvts.getParameter ("rate")) p->endChangeGesture();
    dragging = Hit::none;
    if (footDown) { footDown = false; repaint(); }
}

void SmallToneEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (hitAt (toArt (e.position)) == Hit::knob) setRate (0.5f);
}

void SmallToneEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (hitAt (toArt (e.position)) != Hit::knob) return;
    const float step = (std::abs (w.deltaY) > 1.0e-6f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    setRate (proc.getRate() + step * (e.mods.isShiftDown() ? 0.02f : 0.08f));
}

//==============================================================================
void SmallToneEditor::askUserPresetName (int slot)
{
    nameDialog = std::make_unique<AlertWindow> ("Save user preset " + String (slot), "Preset name:", MessageBoxIconType::NoIcon, this);
    nameDialog->addTextEditor ("name", proc.presetName.isNotEmpty() ? proc.presetName : "User " + String (slot));
    nameDialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    nameDialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    nameDialog->enterModalState (true, ModalCallbackFunction::create ([this, slot] (int r)
    {
        if (r == 1 && nameDialog != nullptr)
        {
            auto n = nameDialog->getTextEditorContents ("name").trim();
            if (n.isEmpty()) n = "User " + String (slot);
            proc.saveUserPreset (slot, n);
            flash ("SAVED : " + n.toUpperCase(), 2.0);
        }
        nameDialog.reset();
    }), false);
}

void SmallToneEditor::showContextMenu()
{
    PopupMenu factory;
    const auto& fp = SmallToneProcessor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i)
        factory.addItem (100 + i, fp[(size_t) i].name, true, proc.presetName == fp[(size_t) i].name);

    PopupMenu load, save;
    for (int s = 1; s <= 9; ++s)
    {
        const auto n = proc.userPresetName (s);
        load.addItem (200 + s, "User " + String (s) + (n.isNotEmpty() ? "  -  " + n : String ("  (empty)")), n.isNotEmpty());
        save.addItem (300 + s, "User " + String (s) + (n.isNotEmpty() ? "  -  " + n + "  (overwrite)" : String()));
    }

    PopupMenu size;
    const int h = getHeight();
    size.addItem (401, "Default (" + String (stui::heightDefault) + " px)", true, std::abs (h - stui::heightDefault) < 8);
    size.addItem (402, "60 %", true, std::abs (h - stui::height60) < 8);
    size.addItem (403, "75 %", true, std::abs (h - stui::height75) < 8);
    size.addItem (404, "100 %", true, std::abs (h - stui::height100) < 8);

    auto flag = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; };
    PopupMenu opt;
    opt.addItem (501, "Rate synced to host tempo (" + String (proc.hostBpm.load(), 1) + " BPM)  -  or click the STATUS LED", true, flag ("sync"));
    opt.addItem (502, "Stereo spread (right channel LFO inverted)", true, flag ("stereo"));
    opt.addItem (503, "BBD hiss (analog noise floor)", true, flag ("noise"));
    opt.addItem (504, "Rate ring pulses with the LFO", true, flag ("ringlfo"));

    PopupMenu midi, chan;
    const int ch = (int) proc.apvts.getRawParameterValue ("midichan")->load();
    chan.addItem (600, "Omni", true, ch == 0);
    for (int c = 1; c <= 16; ++c) chan.addItem (600 + c, String (c), true, ch == c);
    midi.addSubMenu ("Channel", chan);
    midi.addSeparator();
    midi.addItem (701, "Learn: Footswitch (CC " + String (proc.ccFoot.load()) + ")");
    midi.addItem (702, "Learn: Rate (CC " + String (proc.ccRate.load()) + ")");
    midi.addItem (703, "Learn: Depth switch (CC " + String (proc.ccDepth.load()) + ")");
    midi.addItem (704, "Reset MIDI assignments (CC 80-82)");
    midi.addSeparator();
    const bool mom = flag ("ccmomentary");
    midi.addItem (710, "Footswitch: latching pedal (127 = on, 0 = bypass)", true, ! mom);
    midi.addItem (711, "Footswitch: momentary pedal (each press toggles)", true, mom);
    midi.addItem (-1, "Program Change n = factory preset n+1", false, false);

    PopupMenu m;
    m.addSectionHeader ("Small Tone v" JucePlugin_VersionString " - STUDIO WIGA");
    m.addSubMenu ("Factory presets", factory);
    m.addSubMenu ("Load user preset", load);
    m.addSubMenu ("Save user preset", save);
    m.addSeparator();
    m.addSubMenu ("Window size", size);
    m.addSubMenu ("Options", opt);
    m.addSubMenu ("MIDI", midi);

    m.showMenuAsync (PopupMenu::Options().withMousePosition().withTargetComponent (this), [this, flag] (int r)
    {
        auto toggleFlag = [this] (const char* id) { setParam (id, proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); };
        if (r >= 100 && r < 100 + (int) SmallToneProcessor::factoryPresets().size())
        {
            proc.setCurrentProgram (r - 100);
            proc.updateHostDisplay (AudioProcessor::ChangeDetails().withProgramChanged (true));
        }
        else if (r > 200 && r < 210) { if (proc.loadUserPreset (r - 200)) flash ("LOADED : " + proc.presetName.toUpperCase()); }
        else if (r > 300 && r < 310) askUserPresetName (r - 300);
        else if (r == 401) setWindowHeight (stui::heightDefault);
        else if (r == 402) setWindowHeight (stui::height60);
        else if (r == 403) setWindowHeight (stui::height75);
        else if (r == 404) setWindowHeight (stui::height100);
        else if (r == 501) { toggleFlag ("sync"); flash (flag ("sync") ? "TEMPO SYNC  -  " + syncText() : "TEMPO SYNC OFF  -  " + rateText(), 2.2); }
        else if (r == 502) { toggleFlag ("stereo"); flash (flag ("stereo") ? "STEREO SPREAD" : "MONO (ORIGINAL)"); }
        else if (r == 503) toggleFlag ("noise");
        else if (r == 504) toggleFlag ("ringlfo");
        else if (r >= 600 && r <= 616) setParam ("midichan", (float) (r - 600));
        else if (r >= 701 && r <= 703) proc.learnTarget.store (r - 701);
        else if (r == 704) { proc.ccFoot = 80; proc.ccRate = 81; proc.ccDepth = 82; flash ("CC 80 / 81 / 82"); }
        else if (r == 710 || r == 711) setParam ("ccmomentary", r == 711 ? 1.0f : 0.0f);
        repaint();
    });
}
