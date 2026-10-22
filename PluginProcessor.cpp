#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
const std::vector<FactoryPreset>& SmallToneProcessor::factoryPresets()
{
    // position du potard RATE (0 … 1) / switch DEPTH
    static const std::vector<FactoryPreset> p {
        { "Classic Deep",       0.330f, true  },   // ≈ 1.2 Hz, le son Small Clone de référence
        { "Grunge Water",       0.270f, true  },   // ≈ 1 Hz, clean liquide années 90
        { "Slow Ocean",         0.020f, true  },   // ≈ 0.45 Hz, houle lente
        { "Seasick",            0.670f, true  },   // ≈ 3.5 Hz
        { "Rotary Fast",        0.900f, true  },   // ≈ 7 Hz, haut-parleur tournant
        { "Subtle Thickener",   0.110f, false },   // ≈ 0.6 Hz, doublage discret
        { "Shimmer Clean",      0.560f, false },   // ≈ 2.5 Hz
        { "Warble",             0.850f, false },   // ≈ 6 Hz, presque vibrato
        { "Bass Chorus",        0.270f, false },   // ≈ 1 Hz, léger pour la basse
    };
    return p;
}

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout SmallToneProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "rate", 1 }, "Rate",
        NormalisableRange<float> (0.0f, 1.0f), 0.33f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (smalltone::Engine::rateHz (v), 2) + " Hz";
        })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "depth", 1 }, "Depth (DIN. / DEPTH)", true,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("DEPTH") : String ("DIN."); })));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "on", 1 }, "Footswitch (On)", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Rate tempo sync", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "stereo", 1 }, "Stereo spread", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "noise", 1 }, "BBD hiss", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ringlfo", 1 }, "Rate ring follows LFO", true));
    p.push_back (std::make_unique<AudioParameterInt>  (ParameterID { "midichan", 1 }, "MIDI channel (0 = omni)", 0, 16, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ccmomentary", 1 }, "Footswitch CC momentary", false));
    return { p.begin(), p.end() };
}

SmallToneProcessor::SmallToneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SmallToneState", createLayout())
{
    presetName = factoryPresets()[0].name;
}

SmallToneProcessor::~SmallToneProcessor() { cancelPendingUpdate(); }

bool SmallToneProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

void SmallToneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

//==============================================================================
String SmallToneProcessor::syncDivisionName (int i)
{
    static const char* n[] = { "2 BARS", "1 BAR", "1/2", "1/4.", "1/4", "1/4T", "1/8", "1/8T", "1/16" };
    return n[jlimit (0, numSyncDivisions - 1, i)];
}
double SmallToneProcessor::syncDivisionBeats (int i)
{
    static const double b[] = { 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25 };
    return b[jlimit (0, numSyncDivisions - 1, i)];
}
int SmallToneProcessor::syncDivisionIndex (float r) { return jlimit (0, numSyncDivisions - 1, roundToInt (r * (numSyncDivisions - 1))); }

//==============================================================================
void SmallToneProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    for (int c = inCh; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);   // mono -> stéréo

    // ---- MIDI : learn, CC, program change
    const int chan = (int) apvts.getRawParameterValue ("midichan")->load();
    const bool momentary = apvts.getRawParameterValue ("ccmomentary")->load() > 0.5f;
    bool triggerUi = false;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (chan != 0 && msg.getChannel() != chan) continue;
        if (msg.isController())
        {
            const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
            const int lt = learnTarget.load();
            if (lt >= 0)
            {
                (lt == 0 ? ccFoot : lt == 1 ? ccRate : ccDepth).store (cc);
                lastLearned.store (lt); learnTarget.store (-1); triggerUi = true;
                continue;
            }
            if (cc == ccFoot.load())
            {
                const bool high = v >= 64;
                if (momentary) { if (high && ! footCcWasHigh) { pendingFootToggle.fetch_add (1); triggerUi = true; } }
                else { pendingFootSet.store (high ? 1 : 0); triggerUi = true; }
                footCcWasHigh = high;
            }
            if (cc == ccRate.load())  { pendingRate.store (v / 127.0f); triggerUi = true; }
            if (cc == ccDepth.load()) { pendingDepth.store (v >= 64 ? 1 : 0); triggerUi = true; }
        }
        else if (msg.isProgramChange())
        {
            pendingProgram.store (msg.getProgramChangeNumber()); triggerUi = true;
        }
    }
    midi.clear();
    if (triggerUi) triggerAsyncUpdate();

    // ---- tempo
    double bpm = hostBpm.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    hostBpm.store (bpm);

    smalltone::Engine::Settings st;
    st.rate = getRate();
    st.deep = isDeep();
    st.on = isOn();
    st.stereoSpread = apvts.getRawParameterValue ("stereo")->load() > 0.5f;
    st.noise = apvts.getRawParameterValue ("noise")->load() > 0.5f;
    if (apvts.getRawParameterValue ("sync")->load() > 0.5f)
        st.syncHz = jlimit (0.05, 20.0, bpm / 60.0 / syncDivisionBeats (syncDivisionIndex ((float) st.rate)));

    float* io[2] { buffer.getWritePointer (0), buffer.getWritePointer (jmin (1, outCh - 1)) };
    engine.process (io, jmin (2, outCh), n, st);

    lfoVisual.store ((float) engine.lfoNormalised());
    delayVisual.store ((float) engine.delayMs());
}

//==============================================================================
void SmallToneProcessor::setParam (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void SmallToneProcessor::handleAsyncUpdate()
{
    const float r = pendingRate.exchange (-1.0f);
    if (r >= 0.0f) setParam ("rate", r);
    const int d = pendingDepth.exchange (-1);
    if (d >= 0) setParam ("depth", (float) d);
    const int fs = pendingFootSet.exchange (-1);
    if (fs >= 0) setParam ("on", (float) fs);
    const int ft = pendingFootToggle.exchange (0);
    if (ft % 2 == 1) setParam ("on", isOn() ? 0.0f : 1.0f);
    const int pg = pendingProgram.exchange (-1);
    if (pg >= 0 && pg < getNumPrograms()) setCurrentProgram (pg);
}

//==============================================================================
int SmallToneProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const String SmallToneProcessor::getProgramName (int i)
{
    if (isPositiveAndBelow (i, (int) factoryPresets().size())) return factoryPresets()[(size_t) i].name;
    return {};
}

void SmallToneProcessor::setCurrentProgram (int i)
{
    if (! isPositiveAndBelow (i, (int) factoryPresets().size())) return;
    currentProgram = i;
    const auto& p = factoryPresets()[(size_t) i];
    applyPreset (p.rate, p.deep, p.name);
}

void SmallToneProcessor::applyPreset (float rate, bool deep, const String& name)
{
    setParam ("rate", rate);
    setParam ("depth", deep ? 1.0f : 0.0f);
    setParam ("on", 1.0f);
    presetName = name;
}

//==============================================================================
File SmallToneProcessor::userPresetFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
             .getChildFile ("Studio Wiga").getChildFile ("Small Tone").getChildFile ("user_presets.xml");
}

static std::unique_ptr<XmlElement> readUserPresets()
{
    auto f = SmallToneProcessor::userPresetFile();
    if (f.existsAsFile())
        if (auto x = parseXML (f)) if (x->hasTagName ("SmallToneUserPresets")) return x;
    return std::make_unique<XmlElement> ("SmallToneUserPresets");
}

String SmallToneProcessor::userPresetName (int slot) const
{
    auto x = readUserPresets();
    for (auto* e : x->getChildIterator())
        if (e->getIntAttribute ("slot") == slot) return e->getStringAttribute ("name", "User " + String (slot));
    return {};
}

bool SmallToneProcessor::loadUserPreset (int slot)
{
    auto x = readUserPresets();
    for (auto* e : x->getChildIterator())
        if (e->getIntAttribute ("slot") == slot)
        {
            applyPreset ((float) e->getDoubleAttribute ("rate", 0.33), e->getBoolAttribute ("deep", true),
                         e->getStringAttribute ("name", "User " + String (slot)));
            return true;
        }
    return false;
}

void SmallToneProcessor::saveUserPreset (int slot, const String& name)
{
    auto x = readUserPresets();
    for (int i = x->getNumChildElements(); --i >= 0;)
        if (x->getChildElement (i)->getIntAttribute ("slot") == slot) x->removeChildElement (x->getChildElement (i), true);
    auto* e = x->createNewChildElement ("Preset");
    e->setAttribute ("slot", slot);
    e->setAttribute ("name", name);
    e->setAttribute ("rate", (double) getRate());
    e->setAttribute ("deep", isDeep());
    auto f = userPresetFile();
    f.getParentDirectory().createDirectory();
    x->writeTo (f);
    presetName = name;
}

//==============================================================================
void SmallToneProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    xml->setAttribute ("ccFoot", ccFoot.load());
    xml->setAttribute ("ccRate", ccRate.load());
    xml->setAttribute ("ccDepth", ccDepth.load());
    xml->setAttribute ("uiHeight", uiHeight.load());
    xml->setAttribute ("presetName", presetName);
    xml->setAttribute ("program", currentProgram);
    copyXmlToBinary (*xml, dest);
}

void SmallToneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            ccFoot = xml->getIntAttribute ("ccFoot", 80);
            ccRate = xml->getIntAttribute ("ccRate", 81);
            ccDepth = xml->getIntAttribute ("ccDepth", 82);
            uiHeight = jlimit (stui::heightDefault, stui::heightMax, xml->getIntAttribute ("uiHeight", stui::heightDefault));
            presetName = xml->getStringAttribute ("presetName");
            currentProgram = xml->getIntAttribute ("program", 0);
            apvts.replaceState (ValueTree::fromXml (*xml));
        }
    }
}

AudioProcessorEditor* SmallToneProcessor::createEditor() { return new SmallToneEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SmallToneProcessor(); }
