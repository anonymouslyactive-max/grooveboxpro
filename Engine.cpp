#include "Engine.h"
#include "Theme.h"

namespace gb
{
juce::File projectSampleFolder;   // used by Sampler::loadExtra to resolve moved projects

// =============================================================================================
Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Groovebox Pro";
    o.filenameSuffix = ".settings";
    o.folderName = "Groovebox Pro";
    o.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (o);
}

juce::File Settings::userFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Groovebox Pro");
    for (auto sub : { "Projects", "Samples", "Recordings", "Groups", "Sounds", "Exports", "Backups" })
        f.getChildFile (sub).createDirectory();
    return f;
}

const std::vector<LearnAction>& learnActions()
{
    // Defaults = the "FelonE Beats" Controller Editor template (Maschine+ MIDI mode)
    static std::vector<LearnAction> a {
        { "channel", "CHANNEL (Midi)", 0, 34 }, { "plugin", "PLUG-IN (Instance)", 0, 35 }, { "ideas", "IDEAS (Song)", 0, 36 }, { "mixer", "MIXER", 0, 37 },
        { "browser", "BROWSER (+Plug-in)", 0, 38 }, { "sampling", "SAMPLING", 0, 39 }, { "pageLeft", "Page Left", 0, -1 }, { "pageRight", "Page Right", 0, -1 },
        { "file", "FILE (Save)", 0, 40 }, { "settings", "SETTINGS", 0, 41 }, { "auto", "AUTO (write automation)", 0, 42 }, { "macro", "MACRO (Set)", 0, 43 },
        { "disp1", "Display Button 1", 0, 22 }, { "disp2", "Display Button 2", 0, 23 }, { "disp3", "Display Button 3", 0, 24 }, { "disp4", "Display Button 4", 0, 25 },
        { "disp5", "Display Button 5", 0, 26 }, { "disp6", "Display Button 6", 0, 27 }, { "disp7", "Display Button 7", 0, 28 }, { "disp8", "Display Button 8", 0, 29 },
        { "knob1", "Knob 1", 1, 70 }, { "knob2", "Knob 2", 1, 71 }, { "knob3", "Knob 3", 1, 72 }, { "knob4", "Knob 4", 1, 73 },
        { "knob5", "Knob 5", 1, 74 }, { "knob6", "Knob 6", 1, 75 }, { "knob7", "Knob 7", 1, 76 }, { "knob8", "Knob 8", 1, 77 },
        { "encoder", "4-D Encoder Turn", 2, 8 }, { "encPush", "4-D Encoder Push", 0, -1 },
        { "encUp", "4-D Encoder Up", 0, 30 }, { "encDown", "4-D Encoder Down", 0, 32 }, { "encLeft", "4-D Encoder Left", 0, 33 }, { "encRight", "4-D Encoder Right", 0, 31 },
        { "volume", "VOLUME (hold + encoder)", 0, 44 }, { "swing", "SWING (hold + encoder)", 0, 45 }, { "tempo", "TEMPO (hold + encoder)", 0, 47 },
        { "noteRepeat", "NOTE REPEAT (hold)", 0, 46 }, { "lock", "LOCK (snapshot)", 0, 48 },
        { "pitch", "PITCH (touch strip)", 0, 49 }, { "mod", "MOD (touch strip)", 0, 50 }, { "perform", "PERFORM (touch strip)", 0, 51 }, { "notes", "NOTES (touch strip)", 0, 52 },
        { "strip", "Touch Strip", 1, 1 },
        { "groupA", "Group A", 0, -1 }, { "groupB", "Group B", 0, -1 }, { "groupC", "Group C", 0, -1 }, { "groupD", "Group D", 0, -1 },
        { "groupE", "Group E", 0, -1 }, { "groupF", "Group F", 0, -1 }, { "groupG", "Group G", 0, -1 }, { "groupH", "Group H", 0, -1 },
        { "restart", "RESTART (Loop)", 0, 53 }, { "erase", "ERASE (hold + pad)", 0, 54 }, { "tap", "TAP (Metro)", 0, 55 }, { "follow", "FOLLOW (Grid)", 0, 56 },
        { "play", "PLAY", 0, 57 }, { "record", "REC (Count-in)", 0, 58 }, { "stop", "STOP", 0, 59 }, { "shift", "SHIFT (hold)", 0, -1 },
        { "fixedVel", "FIXED VEL", 0, 80 }, { "padMode", "PAD MODE", 0, 81 }, { "keyboard", "KEYBOARD", 0, 82 }, { "chords", "CHORDS", 0, 84 }, { "step", "STEP", 0, 83 },
        { "scene", "SCENE (hold + pad)", 0, 85 }, { "pattern", "PATTERN (hold + pad)", 0, 86 }, { "events", "EVENTS (hold + pad)", 0, 87 }, { "variation", "VARIATION", 0, 88 },
        { "duplicate", "DUPLICATE (hold + pad)", 0, 89 }, { "select", "SELECT (hold + pad)", 0, 90 }, { "solo", "SOLO (hold + pad)", 0, 91 }, { "mute", "MUTE (hold + pad)", 0, 92 },
        { "undo", "Undo", 0, -1 }, { "redo", "Redo", 0, -1 }, { "masterVol", "Master Volume (CC)", 1, -1 }, { "tempoCC", "Tempo (CC)", 1, -1 } };
    return a;
}

juce::String Engine::learnKey (int action)
{
    auto& acts = learnActions();
    if (! juce::isPositiveAndBelow (action, (int) acts.size())) return {};
    auto k = "learn2_" + acts[(size_t) action].id;
    if (settings.props().containsKey (k)) return settings.getString (k);
    return acts[(size_t) action].defaultCC >= 0 ? "cc:0:" + juce::String (acts[(size_t) action].defaultCC) : juce::String();
}

void Engine::rebuildLearnCache()
{
    std::map<juce::String, int> m;
    for (int i = 0; i < (int) learnActions().size(); ++i)
    {
        auto k = learnKey (i);
        if (k.isNotEmpty()) m[k] = i;
    }
    const juce::ScopedLock sl (learnCacheLock);
    learnCache = std::move (m);
}

const juce::StringArray& Engine::gridChoices()
{
    static juce::StringArray a { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T", "Off" };
    return a;
}

int Engine::gridTicks (int idx)
{
    const int t[] = { PPQ * 4, PPQ * 2, PPQ, PPQ / 2, PPQ / 4, PPQ / 8, PPQ / 16, PPQ / 3, PPQ / 6, PPQ / 12, 1 };
    return t[juce::jlimit (0, 10, idx)];
}

const juce::StringArray& Engine::recLengths()
{
    static juce::StringArray a { "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars", "FREE" };
    return a;
}

// =============================================================================================
Engine::Engine()
{
    juce::addDefaultFormatsToManager (formatManager);
    if (auto xml = juce::XmlDocument::parse (pluginListFile()))
        knownPlugins.recreateFromXml (*xml);

    proj = createDefaultProject();
    allocateBuffers (44100.0, 512);   // valid even before / without an audio device

    auto saved = juce::XmlDocument::parse (settings.getString ("audioDevice"));
    auto err = deviceManager.initialise (16, 16, saved.get(), true);
    if (err.isNotEmpty()) DBG ("Audio init: " << err);
    deviceManager.addAudioCallback (this);
    refreshMidiInputs();

    auto down = juce::File (settings.getString ("metroDown")), up = juce::File (settings.getString ("metroUp"));
    if (down.existsAsFile()) metroDown = SampleCache::get().load (down);
    if (up.existsAsFile()) metroUp = SampleCache::get().load (up);

    startTimerHz (30);
}

Engine::~Engine()
{
    stopTimer();
    editorWindows.clear();
    deviceManager.removeAudioCallback (this);
    for (auto& i : inputs) i->stop();
    inputs.clear();
    if (auto x = deviceManager.createStateXml()) settings.set ("audioDevice", x->toString());
    const juce::ScopedLock sl (lock);
    proj.reset();
}

juce::File Engine::pluginListFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Groovebox Pro").getChildFile ("PluginList.xml");
}

void Engine::savePluginList()
{
    pluginListFile().getParentDirectory().createDirectory();
    if (auto x = knownPlugins.createXml()) x->writeTo (pluginListFile());
}

std::unique_ptr<Project> Engine::createDefaultProject()
{
    auto p = std::make_unique<Project>();
    auto g = std::make_unique<Group>();
    g->name = "Group A1";
    g->colour = col::orange;
    for (auto& s : g->sounds) s->colour = g->colour;
    p->groups.push_back (std::move (g));
    p->scenes.push_back ({ "Scene 1", { -1 } });
    p->bpm = settings.getDouble ("defaultTempo", 120.0);
    return p;
}

void Engine::newProject()
{
    if (exporting) return;
    auto tpl = juce::File (settings.getString ("standaloneTemplate"));
    if (tpl.existsAsFile() && loadProject (tpl))
    {
        const juce::ScopedLock sl (lock);
        proj->file = juce::File();
        proj->name = "New Project";
        return;
    }
    stop();
    replaceProject (createDefaultProject());
    undoStack.clear(); redoStack.clear();
    dirty = false;
}
} // namespace gb

namespace gb
{
void Engine::reloadSettingsCache()
{
    padBase = settings.getInt ("padBaseNote", 48);   // FelonE Beats template: pad 1 = C2
    padChannel = settings.getInt ("padChannel", 0);
    velCurve = settings.getInt ("velocityScaling", 0);
    fixedVelocity = settings.getInt ("fixedVelocity", 100);
    defaultMidiMode = settings.getInt ("defaultMidiInputMode", 0);
    inputQuantize = settings.getInt ("inputQuantize", 1);
    countInBars = settings.getInt ("countIn", 1);
    metroVolume = (float) settings.getDouble ("metroVolume", 0.7);
    autoMetroRec = settings.getBool ("metroAutoRecord", true);
    cueDest = settings.getInt ("cueDest", 0);
    syncMode = settings.getInt ("syncMode", 0);
    rebuildLearnCache();
    auto down = juce::File (settings.getString ("metroDown")), up = juce::File (settings.getString ("metroUp"));
    auto d = down.existsAsFile() ? SampleCache::get().load (down) : SamplePtr();
    auto u = up.existsAsFile() ? SampleCache::get().load (up) : SamplePtr();
    const juce::ScopedLock sl (lock);
    metroDown = d; metroUp = u;
}

float Engine::applyVelocityCurve (float v) const
{
    switch (velCurve.load())
    {
        case 1: return std::pow (v, 0.6f);          // Log (soft)
        case 2: return std::pow (v, 1.8f);          // Exp (hard)
        case 3: return fixedVelocity.load() / 127.0f; // Fixed
        default: return v;                           // Linear
    }
}

// =============================================================================================
// Transport
void Engine::play()
{
    if (playing) return;
    if (recording && countInBars > 0)
    {
        const juce::ScopedLock sl (lock);
        countInRemaining = (double) proj->ticksPerBar() * countInBars;
        countInTotal = countInRemaining;
        countingIn = true;
    }
    playing = true;
    if (clockOut != nullptr && syncMode == 1) clockOut->sendMessageNow (position < 1 ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue());
}

void Engine::stop()
{
    if (! playing)
    {
        position = 0.0;  // second stop returns to start, like the original
        return;
    }
    playing = false;
    countingIn = false;
    recording = false;
    allNotesOffPending = true;
    if (clockOut != nullptr && syncMode == 1) clockOut->sendMessageNow (juce::MidiMessage::midiStop());
}

void Engine::restart()
{
    position = 0.0;
    if (! playing) play();
}

void Engine::toggleRecord()
{
    recording = ! recording;
    if (recording && ! playing) play();
}

void Engine::setTempo (double bpm)
{
    const juce::ScopedLock sl (lock);
    proj->bpm = juce::jlimit (20.0, 300.0, bpm);
    hostBpm = proj->bpm;
}

void Engine::tapTempo()
{
    auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! tapTimes.empty() && now - tapTimes.back() > 2.0) tapTimes.clear();
    tapTimes.push_back (now);
    if (tapTimes.size() > 5) tapTimes.erase (tapTimes.begin());
    if (tapTimes.size() >= 2)
    {
        auto avg = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        setTempo (60.0 / avg);
    }
}

double Engine::displayLoopLength()
{
    if (songMode && proj->songLength() > 0) return (double) proj->songLength();
    juce::int64 L = proj->ticksPerBar();
    auto& sc = proj->scenes[(size_t) juce::jlimit (0, (int) proj->scenes.size() - 1, proj->currentScene)];
    for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
        if (auto* p = proj->groups[(size_t) gi]->pattern (sc.patternFor (gi))) L = juce::jmax (L, p->length);
    return (double) L;
}

void Engine::triggerPad (int group, int sound, float velocity, bool noteOn, int pitch)
{
    InMsg m;
    m.group = group; m.sound = sound;
    m.msg = noteOn ? juce::MidiMessage::noteOn (1, pitch < 0 ? 0 : pitch, velocity) : juce::MidiMessage::noteOff (1, pitch < 0 ? 0 : pitch);
    if (pitch < 0) m.msg.setNoteNumber (0), m.device = -2;   // -2 => use sound root key
    pushMidi (m);
}

void Engine::pushMidi (const InMsg& m)
{
    const juce::ScopedLock sl (fifoWriteLock);
    int s1, n1, s2, n2;
    midiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) midiQueue[(size_t) s1] = m;
    else if (n2 > 0) midiQueue[(size_t) s2] = m;
    midiFifo.finishedWrite (n1 + n2);
}

void Engine::previewFile (const juce::File& f)
{
    auto s = SampleCache::get().load (f);
    const juce::ScopedLock sl (previewLock);
    preview = s;
    previewPos = s != nullptr ? 0.0 : -1.0;
    previewInc = s != nullptr ? s->sampleRate / sampleRate : 1.0;
}

void Engine::stopPreview()
{
    const juce::ScopedLock sl (previewLock);
    previewPos = -1;
}

// =============================================================================================
// Chains
std::unique_ptr<Slot> Engine::createSlot (const juce::String& type, const juce::String& pluginId, juce::String& error)
{
    auto s = std::make_unique<Slot>();
    s->type = type;
    s->pluginId = pluginId;
    if (type == "VST3" || type == "VST" || type == "AU")
    {
        std::unique_ptr<juce::PluginDescription> desc;
        for (auto& d : knownPlugins.getTypes())
            if (d.createIdentifierString() == pluginId || d.fileOrIdentifier == pluginId) { desc = std::make_unique<juce::PluginDescription> (d); break; }
        if (desc == nullptr) { error = "Plug-in not found: " + pluginId + "\nRescan plug-ins in Preferences > Plug-ins."; return {}; }
        auto inst = formatManager.createPluginInstance (*desc, sampleRate, blockSize, error);
        if (inst == nullptr) return {};
        inst->enableAllBuses();
        s->isInstrument = desc->isInstrument;
        s->displayName = desc->name;
        s->proc = std::move (inst);
    }
    else
    {
        s->proc = createInternal (type);
        if (s->proc == nullptr) { error = "Unknown device: " + type; return {}; }
        s->isInstrument = isInternalInstrument (type);
        s->displayName = s->proc->getName();
    }
    s->proc->setRateAndBufferSizeDetails (sampleRate, blockSize);
    s->proc->prepareToPlay (sampleRate, blockSize);
    return s;
}

void Engine::insertSlot (Chain& chain, int index, std::unique_ptr<Slot> s)
{
    const juce::ScopedLock sl (lock);
    index = juce::jlimit (0, (int) chain.size(), index);
    chain.insert (chain.begin() + index, std::move (s));
}

void Engine::removeSlot (Chain& chain, int index)
{
    std::unique_ptr<Slot> old;
    {
        const juce::ScopedLock sl (lock);
        if (! juce::isPositiveAndBelow (index, (int) chain.size())) return;
        old = std::move (chain[(size_t) index]);
        chain.erase (chain.begin() + index);
    }
    editorWindows.erase (std::remove_if (editorWindows.begin(), editorWindows.end(), [&] (auto& w)
    {
        return w->getProperties()["proc"].toString() == juce::String::toHexString ((juce::pointer_sized_int) old->proc.get());
    }), editorWindows.end());
}

void Engine::moveSlot (Chain& chain, int from, int to)
{
    const juce::ScopedLock sl (lock);
    if (! juce::isPositiveAndBelow (from, (int) chain.size())) return;
    to = juce::jlimit (0, (int) chain.size() - 1, to);
    auto s = std::move (chain[(size_t) from]);
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + to, std::move (s));
}

SamplerProcessor* Engine::samplerOf (Sound& s)
{
    for (auto& sl : s.slots)
        if (auto* sp = dynamic_cast<SamplerProcessor*> (sl->proc.get())) return sp;
    return nullptr;
}

void Engine::loadSampleIntoSound (Sound& s, const juce::File& f)
{
    auto data = SampleCache::get().load (f);
    if (data == nullptr) { if (onStatus) onStatus ("Could not load " + f.getFileName()); return; }
    auto* sp = samplerOf (s);
    if (sp == nullptr)
    {
        juce::String err;
        auto slot = createSlot ("Sampler", {}, err);
        if (slot == nullptr) return;
        sp = static_cast<SamplerProcessor*> (slot->proc.get());
        insertSlot (s.slots, 0, std::move (slot));
    }
    sp->rootKey = s.rootKey;
    sp->setSample (data);
    sp->setRegion (0.0f, 1.0f);
    const juce::ScopedLock sl (lock);
    s.name = f.getFileNameWithoutExtension();
}

juce::String Engine::describeSlot (const Slot& s) const
{
    return s.displayName.isNotEmpty() ? s.displayName : s.type;
}

namespace
{
    struct PluginWindow : juce::DocumentWindow
    {
        PluginWindow (juce::AudioProcessor& p, std::function<void (PluginWindow*)> onClose)
            : DocumentWindow (p.getName(), juce::Colour (0xff181818), DocumentWindow::closeButton), closeCb (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            if (auto* ed = p.createEditorIfNeeded()) setContentOwned (ed, true);
            else
            {
                auto* ge = new juce::GenericAudioProcessorEditor (p);
                ge->setSize (420, 400);
                setContentOwned (ge, true);
            }
            if (auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent())) setResizable (ed->isResizable(), false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            setAlwaysOnTop (true);
        }
        void closeButtonPressed() override { if (closeCb) closeCb (this); }
        std::function<void (PluginWindow*)> closeCb;
    };
}

void Engine::openPluginEditor (Slot& s)
{
    if (s.proc == nullptr) return;
    auto key = juce::String::toHexString ((juce::pointer_sized_int) s.proc.get());
    for (auto& w : editorWindows)
        if (w->getProperties()["proc"].toString() == key) { w->toFront (true); return; }
    auto w = std::make_unique<PluginWindow> (*s.proc, [this] (PluginWindow* pw)
    {
        juce::MessageManager::callAsync ([this, pw]
        {
            editorWindows.erase (std::remove_if (editorWindows.begin(), editorWindows.end(), [pw] (auto& x) { return x.get() == pw; }), editorWindows.end());
        });
    });
    w->getProperties().set ("proc", key);
    editorWindows.push_back (std::move (w));
}

void Engine::closeEditorsFor (const Chain& c)
{
    for (auto& s : c)
    {
        auto key = juce::String::toHexString ((juce::pointer_sized_int) s->proc.get());
        editorWindows.erase (std::remove_if (editorWindows.begin(), editorWindows.end(), [&] (auto& w) { return w->getProperties()["proc"].toString() == key; }), editorWindows.end());
    }
}

// =============================================================================================
// Editing helpers
juce::int64 Engine::defaultPatternLength()
{
    return (juce::int64) juce::jmax (1, settings.getInt ("patternLengthBars", 4)) * proj->ticksPerBar();
}

Pattern* Engine::newPatternFor (Group& g, juce::int64 length)
{
    auto p = std::make_unique<Pattern>();
    p->length = length;
    p->colour = g.colour;
    int n = 1;
    for (;;)
    {
        bool used = false;
        for (auto& x : g.patterns) if (x->name == "Pattern " + juce::String (n)) used = true;
        if (! used) break;
        ++n;
    }
    p->name = "Pattern " + juce::String (n);
    const juce::ScopedLock sl (lock);
    g.patterns.push_back (std::move (p));
    return g.patterns.back().get();
}

int Engine::editClip (int group) const
{
    auto it = clipSelection.find (group);
    return it == clipSelection.end() ? 0 : it->second;
}

void Engine::setEditClip (int group, int clip) { clipSelection[group] = clip; }

Pattern* Engine::editPattern (int group)
{
    auto* g = proj->group (group);
    if (g == nullptr) return nullptr;
    if (songMode)
    {
        int c = editClip (group);
        if (juce::isPositiveAndBelow (c, (int) g->clips.size())) return g->pattern (g->clips[(size_t) c].pattern);
        return nullptr;
    }
    ensureSceneSlots();
    auto& sc = proj->scenes[(size_t) juce::jlimit (0, (int) proj->scenes.size() - 1, proj->currentScene)];
    return g->pattern (sc.patternFor (group));
}

void Engine::ensureSceneSlots()
{
    const juce::ScopedLock sl (lock);   // may grow vectors the audio thread reads (recursive lock: safe if already held)
    if (proj->scenes.empty()) proj->scenes.push_back ({ "Scene 1", {} });
    for (auto& sc : proj->scenes)
        while (sc.patternForGroup.size() < proj->groups.size()) sc.patternForGroup.push_back (-1);
}

void Engine::addGroup()
{
    auto g = std::make_unique<Group>();
    int idx = (int) proj->groups.size();
    g->name = "Group " + groupLetterName (idx);
    g->colour = paletteColour (idx * 3);
    for (auto& s : g->sounds) s->colour = g->colour;
    {
        const juce::ScopedLock sl (lock);
        proj->groups.push_back (std::move (g));
        groupMidi.resize (proj->groups.size());
        soundMidi.resize (proj->groups.size() * numPads);
        ensureSceneSlots();
    }
    focusedGroup = idx;
}

void Engine::selectSound (int group, int sound)
{
    if (auto* g = proj->group (group)) g->focusedSound = juce::jlimit (0, numPads - 1, sound);
    focusedGroup = group;
}

// =============================================================================================
// MIDI
void Engine::refreshMidiInputs()
{
    auto enabled = settings.getList ("midiInputs");
    auto devs = juce::MidiInput::getAvailableDevices();
    if (! settings.props().containsKey ("midiInputs"))
        for (auto& d : devs) enabled.add (d.name);   // first run: enable everything (incl. Maschine Plus MIDI)

    for (auto& i : inputs) i->stop();
    inputs.clear();
    openedInputs.clear();
    for (auto& d : devs)
    {
        if (! enabled.contains (d.name)) continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            openedInputs.add (d.name);
            inputs.push_back (std::move (in));
        }
    }
    // start only after openedInputs is complete: the MIDI thread reads it in handleIncomingMidiMessage
    for (auto& i : inputs) i->start();
    clockOut.reset();
    auto outName = settings.getString ("clockOutput");
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == outName) clockOut = juce::MidiOutput::openDevice (d.identifier);
    reloadSettingsCache();
}

juce::StringArray Engine::midiInputNames() const { return openedInputs; }

void Engine::handleLearn (const juce::MidiMessage& m)
{
    juce::String key;
    if (m.isController()) key = "cc:" + juce::String (m.getChannel()) + ":" + juce::String (m.getControllerNumber());
    else if (m.isNoteOn()) key = "note:" + juce::String (m.getChannel()) + ":" + juce::String (m.getNoteNumber());
    else return;
    int t = learnTarget.load();
    auto& acts = learnActions();
    if (! juce::isPositiveAndBelow (t, (int) acts.size())) return;
    learnTarget = learnSequential && t + 1 < (int) acts.size() ? -2 : -1;   // -2: waiting for release before the next one
    juce::MessageManager::callAsync ([this, t, key]
    {
        for (int i = 0; i < (int) learnActions().size(); ++i)
            if (i != t && learnKey (i) == key) settings.set ("learn2_" + learnActions()[(size_t) i].id, "");
        settings.set ("learn2_" + learnActions()[(size_t) t].id, key);
        rebuildLearnCache();
        if (learnSequential)
        {
            if (t + 1 < (int) learnActions().size()) juce::Timer::callAfterDelay (350, [this, t] { if (learnSequential) learnTarget = t + 1; if (onLearnedAction) onLearnedAction (-1, 0.0f); });
            else learnSequential = false;
        }
        if (onLearnedAction) onLearnedAction (-1, 0.0f);
    });
}

void Engine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    int dev = source != nullptr ? openedInputs.indexOf (source->getName()) : -1;

    // --- sync (clock / start / stop / song position)
    if (syncOn || syncMode == 2)
    {
        if (m.isMidiClock())
        {
            auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            if (lastClockTime > 0)
            {
                midiClockTimes.push_back (now - lastClockTime);
                if (midiClockTimes.size() > 48) midiClockTimes.erase (midiClockTimes.begin());
                if (midiClockTimes.size() >= 24)
                {
                    double avg = 0; for (auto d : midiClockTimes) avg += d;
                    avg /= (double) midiClockTimes.size();
                    auto bpm = 60.0 / (avg * 24.0);
                    // proj may be replaced by the GUI at any time: only touch it while holding the lock
                    const juce::ScopedTryLock sl (lock);
                    if (sl.isLocked() && std::abs (bpm - proj->bpm) > 0.05) proj->bpm = juce::jlimit (20.0, 300.0, std::round (bpm * 10.0) / 10.0);
                }
            }
            lastClockTime = now;
            return;
        }
        if (m.isMidiStart()) { position = 0.0; playing = true; return; }
        if (m.isMidiContinue()) { playing = true; return; }
        if (m.isMidiStop()) { playing = false; return; }
        if (m.isSongPositionPointer()) { position = (double) m.getSongPositionPointerMidiBeat() * PPQ / 4.0; return; }
    }

    // --- MMC transport (Maschine MIDI mode transport buttons can send these)
    if (m.isSysEx())
    {
        auto* d = m.getSysExData();
        if (m.getSysExDataSize() >= 4 && d[0] == 0x7f && d[2] == 0x06)
        {
            int cmd = d[3];
            juce::MessageManager::callAsync ([this, cmd]
            {
                if (cmd == 1) stop();
                else if (cmd == 2 || cmd == 3) play();
                else if (cmd == 5) { position = 0.0; }
                else if (cmd == 6) { recording = true; play(); }
                else if (cmd == 7) recording = false;
                else if (cmd == 9) stop();
            });
        }
        return;
    }

    if (learnTarget >= 0 && (m.isController() || m.isNoteOn())) { handleLearn (m); return; }
    if (learnTarget == -2) return;   // guided learn between controls

    // --- learned controller actions (buttons send press + release so they can be held)
    if (m.isController() || m.isNoteOn() || m.isNoteOff())
    {
        int num = m.isController() ? m.getControllerNumber() : m.getNoteNumber();
        juce::String pre = m.isController() ? "cc:" : "note:";
        int action = -1;
        {
            const juce::ScopedLock sl (learnCacheLock);
            auto it = learnCache.find (pre + juce::String (m.getChannel()) + ":" + juce::String (num));
            if (it == learnCache.end()) it = learnCache.find (pre + "0:" + juce::String (num));
            if (it != learnCache.end()) action = it->second;
        }
        if (action >= 0)
        {
            int kind = learnActions()[(size_t) action].kind;
            float val;
            if (kind == 0) val = (m.isNoteOn() || (m.isController() && m.getControllerValue() >= 64)) ? 1.0f : 0.0f;
            else if (kind == 1) val = m.isController() ? m.getControllerValue() / 127.0f : (m.isNoteOn() ? 1.0f : 0.0f);
            else val = (float) (m.isController() ? m.getControllerValue() : 64);   // encoder: raw value, GUI works out the direction
            juce::MessageManager::callAsync ([this, action, val] { if (onLearnedAction) onLearnedAction (action, val); });
            return;
        }
    }

    if (m.isNoteOn()) lastMidiNote = m.getNoteNumber();
    InMsg im; im.msg = m; im.device = dev;
    pushMidi (im);
}

// =============================================================================================
void Engine::timerCallback()
{
    // move notes recorded on the audio thread into patterns
    int s1, n1, s2, n2;
    recFifo.prepareToRead (recFifo.getNumReady(), s1, n1, s2, n2);
    auto handle = [this] (const RecNote& r)
    {
        auto* g = proj->group (r.group);
        if (g == nullptr) return;
        auto key = std::make_tuple (r.group, r.sound, r.pitch);
        const juce::ScopedLock sl (lock);
        if (r.on)
        {
            Pattern* pat = nullptr; juce::int64 local = 0;
            if (songMode)
            {
                for (int ci = 0; ci < (int) g->clips.size(); ++ci)
                {
                    auto& c = g->clips[(size_t) ci];
                    if (r.tick >= c.start && r.tick < c.start + c.length) { pat = g->pattern (c.pattern); if (pat) local = (r.tick - c.start) % pat->length; clipSelection[r.group] = ci; break; }
                }
                if (pat == nullptr)
                {
                    auto bar = proj->ticksPerBar();
                    const juce::ScopedUnlock ul (lock);
                    pat = newPatternFor (*g, bar);
                    const juce::ScopedLock sl2 (lock);
                    g->clips.push_back ({ (r.tick / bar) * bar, bar, (int) g->patterns.size() - 1 });
                    clipSelection[r.group] = (int) g->clips.size() - 1;
                    local = r.tick % bar;
                }
            }
            else
            {
                ensureSceneSlots();
                auto& sc = proj->scenes[(size_t) juce::jlimit (0, (int) proj->scenes.size() - 1, proj->currentScene)];
                pat = g->pattern (sc.patternFor (r.group));
                if (pat == nullptr)
                {
                    const juce::ScopedUnlock ul (lock);
                    pat = newPatternFor (*g, defaultPatternLength());
                    const juce::ScopedLock sl2 (lock);
                    sc.patternForGroup[(size_t) r.group] = (int) g->patterns.size() - 1;
                }
                local = r.tick % pat->length;
            }
            if (inputQuantize > 0)
            {
                auto q = (juce::int64) gridTicks (stepGrid);
                local = ((local + q / 2) / q) * q;
                if (local >= pat->length) local -= pat->length;
            }
            NoteEvent ne; ne.sound = r.sound; ne.start = local; ne.length = gridTicks (stepGrid); ne.velocity = r.vel; ne.pitch = r.pitch;
            pat->notes.push_back (ne);
            openRecNotes[key] = { pat, (int) pat->notes.size() - 1, r.tick };
            dirty = true;
            sendChangeMessage();
        }
        else
        {
            auto it = openRecNotes.find (key);
            if (it == openRecNotes.end()) return;
            auto open = it->second;
            openRecNotes.erase (it);
            // the note-on went into open.pattern (not necessarily the pattern shown now); make sure it still exists
            bool alive = false;
            for (auto& p : g->patterns) if (p.get() == open.pattern) { alive = true; break; }
            if (! alive || ! juce::isPositiveAndBelow (open.index, (int) open.pattern->notes.size())) return;
            auto& ne = open.pattern->notes[(size_t) open.index];
            if (ne.sound != r.sound) return;   // notes were edited meanwhile
            auto len = r.tick - open.onTick;   // real held duration (independent of clip offset / quantize)
            if (len <= 0) len += open.pattern->length;   // transport wrapped (loop)
            ne.length = std::clamp<juce::int64> (len, PPQ / 16, std::max<juce::int64> (PPQ / 16, open.pattern->length));
        }
    };
    for (int i = 0; i < n1; ++i) handle (recQueue[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (recQueue[(size_t) (s2 + i)]);
    recFifo.finishedRead (n1 + n2);

    // auto backup every 5 minutes
    static int backupCounter = 0;
    if (++backupCounter > 30 * 300)
    {
        backupCounter = 0;
        if (dirty && settings.getBool ("autoBackup", true))
            toValueTree (false).createXml()->writeTo (autoBackupFile());
    }
}

juce::File Engine::autoBackupFile() const
{
    return Settings::userFolder().getChildFile ("Backups").getChildFile ("Autosave.gbp");
}

// =============================================================================================
// Audio
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const juce::ScopedLock sl (lock);
    allocateBuffers (d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples());
}

void Engine::allocateBuffers (double sr, int bs)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    blockSize = juce::jmax (16, bs);
    for (auto* b : { &soundBuf, &groupBuf, &masterBuf, &aux1Buf, &aux2Buf, &cueBuf })
        b->setSize (2, blockSize);
    inputCopy.setSize (32, blockSize);
    scratch.setSize (32, blockSize);
    for (auto& e : extBufs) e.setSize (2, blockSize);
    prepareAll();
}

void Engine::audioDeviceStopped() {}

void Engine::prepareChain (Chain& c)
{
    for (auto& s : c)
        if (s->proc != nullptr)
        {
            s->proc->setRateAndBufferSizeDetails (sampleRate, blockSize);
            s->proc->prepareToPlay (sampleRate, blockSize);
        }
}

void Engine::prepareAll()
{
    for (auto& g : proj->groups)
    {
        prepareChain (g->slots);
        for (auto& s : g->sounds) prepareChain (s->slots);
    }
    prepareChain (proj->masterSlots);
    soundMidi.resize (proj->groups.size() * numPads);
    groupMidi.resize (proj->groups.size());
    for (auto& m : soundMidi) m.ensureSize (512);
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    if (! engineOn) return;

    const juce::ScopedLock sl (lock);
    int done = 0;
    while (done < n)
    {
        int chunk = juce::jmin (blockSize, n - done);
        const float* inPtrs[32] = {};
        for (int c = 0; c < juce::jmin (numIn, 32); ++c) inPtrs[c] = in[c] != nullptr ? in[c] + done : nullptr;
        processBlockInternal (inPtrs, juce::jmin (numIn, 32), chunk);

        const float mv = (float) masterVolume.load();
        for (int k = 0; k < 16; ++k)
        {
            int l = k * 2, r = k * 2 + 1;
            if (l >= numOut) break;
            float g = k == juce::jmax (0, proj->master.outDest - destExt1) ? mv : 1.0f;
            if (out[l] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[l] + done, extBufs[(size_t) k].getReadPointer (0), g, chunk);
            if (r < numOut && out[r] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[r] + done, extBufs[(size_t) k].getReadPointer (1), g, chunk);
        }
        done += chunk;
    }
}

void Engine::route (int dest, const juce::AudioBuffer<float>& src, float gain, float pan, int n, bool isSound)
{
    juce::AudioBuffer<float>* target = nullptr;
    if (dest == destNone) return;
    if (dest == destGroup) target = isSound ? &groupBuf : &masterBuf;
    else if (dest == destMaster) target = &masterBuf;
    else target = &extBufs[(size_t) juce::jlimit (0, 15, dest - destExt1)];
    float gl = gain * juce::jmin (1.0f, 1.0f - pan), gr = gain * juce::jmin (1.0f, 1.0f + pan);
    target->addFrom (0, 0, src, 0, 0, n, gl);
    target->addFrom (1, 0, src, src.getNumChannels() > 1 ? 1 : 0, 0, n, gr);
}

void Engine::processChain (Chain& chain, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, int n)
{
    for (auto& slot : chain)
    {
        auto* p = slot->proc.get();
        if (p == nullptr || slot->bypass) continue;
        const juce::ScopedLock pl (p->getCallbackLock());
        if (p->isSuspended()) continue;
        int ins = p->getTotalNumInputChannels(), outs = p->getTotalNumOutputChannels();
        int ch = juce::jmax (2, ins, outs);
        if (ch == 2 && outs == 2)
        {
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            p->processBlock (view, midi);
        }
        else
        {
            ch = juce::jmin (ch, scratch.getNumChannels());
            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), ch, n);
            view.clear();
            for (int c = 0; c < juce::jmin (2, ins); ++c) view.copyFrom (c, 0, buf, c, 0, n);
            if (ins == 1) view.addFrom (0, 0, buf, 1, 0, n), view.applyGain (0, 0, n, 0.5f);
            p->processBlock (view, midi);
            if (outs >= 2) { buf.copyFrom (0, 0, view, 0, 0, n); buf.copyFrom (1, 0, view, 1, 0, n); }
            else if (outs == 1) { buf.copyFrom (0, 0, view, 0, 0, n); buf.copyFrom (1, 0, view, 0, 0, n); }
        }
    }
}

float Engine::swingShift (const Group& g, const Sound& s, juce::int64 start) const
{
    float total = juce::jlimit (0.0f, 1.0f, proj->swing + g.groove.swing + s.groove.swing);
    if (total <= 0.0f) return 0.0f;
    int cycle = s.groove.swing > 0 ? s.groove.cycle : g.groove.swing > 0 ? g.groove.cycle : 0;
    bool invert = s.groove.swing > 0 ? s.groove.invert : g.groove.invert;
    juce::int64 grid = cycle == 0 ? PPQ / 4 : cycle == 1 ? PPQ / 2 : PPQ;
    bool odd = ((start / grid) % 2) == 1;
    if (odd == invert) return 0.0f;
    return total * (float) grid * 0.5f;
}

void Engine::noteToSound (int gi, int si, int offset, float vel, int pitch, bool on, bool allowLink)
{
    auto* g = proj->group (gi);
    if (g == nullptr || ! juce::isPositiveAndBelow (si, numPads)) return;
    auto& s = *g->sounds[(size_t) si];
    size_t idx = (size_t) (gi * numPads + si);
    if (idx >= soundMidi.size()) return;
    int note = juce::jlimit (0, 127, pitch >= 0 ? pitch : s.rootKey);
    if (on)
    {
        if (s.chokeGroup > 0 && s.chokeMode != 1)
            for (int o = 0; o < numPads; ++o)
            {
                auto& os = *g->sounds[(size_t) o];
                if (o != si && os.chokeGroup == s.chokeGroup && os.chokeMode != 0)
                    soundMidi[(size_t) (gi * numPads + o)].addEvent (juce::MidiMessage::allNotesOff (1), offset);
            }
        soundMidi[idx].addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.01f, 1.0f, vel)), offset);
        s.lastTrigger++;
    }
    else
        soundMidi[idx].addEvent (juce::MidiMessage::noteOff (1, note), juce::jmin (offset + 0, 0x7fffffff));

    if (allowLink && s.linkGroup > 0 && s.linkMode != 1)
        for (int o = 0; o < numPads; ++o)
        {
            auto& os = *g->sounds[(size_t) o];
            if (o != si && os.linkGroup == s.linkGroup && os.linkMode != 0)
                noteToSound (gi, o, offset, vel, -1, on, false);
        }
}

void Engine::emitPattern (int gi, Group& g, const Pattern& p, double base, double from, double to, double spt, int n, juce::int64 clipEnd)
{
    for (auto& ne : p.notes)
    {
        if (! juce::isPositiveAndBelow (ne.sound, numPads)) continue;
        auto& s = *g.sounds[(size_t) ne.sound];
        double sw = swingShift (g, s, ne.start);
        double t = base + (double) ne.start + sw;
        // skip notes outside the clip, including ones swing pushed past the end (their note-off would precede the note-on)
        if ((juce::int64) (base + ne.start) >= clipEnd || t >= (double) clipEnd) continue;
        if (emitPass == 1)
        {
            if (t >= from && t < to && (ne.probability >= 1.0f || rng.nextFloat() < ne.probability))
                noteToSound (gi, ne.sound, juce::jlimit (0, n - 1, (int) ((t - from) * spt)), ne.velocity, ne.pitch, true);
            continue;
        }
        double te = juce::jmin ((double) clipEnd, t + (double) std::max<juce::int64> (1, ne.length));
        if (te >= from && te < to)
            noteToSound (gi, ne.sound, juce::jlimit (0, n - 1, (int) ((te - from) * spt)), 0.0f, ne.pitch, false);
    }
}

void Engine::scheduleGroup (int gi, Group& g, double from, double to, double spt, int n)
{
    // all note-offs of the window first, then all note-ons: at an identical sample position a note-off of the
    // previous note (other clip / other pattern loop / other list order) must never cut the new note-on.
    for (emitPass = 0; emitPass < 2; ++emitPass)
        scheduleGroupPass (gi, g, from, to, spt, n);
}

void Engine::scheduleGroupPass (int gi, Group& g, double from, double to, double spt, int n)
{
    if (songMode && proj->songLength() > 0)
    {
        for (auto& c : g.clips)
        {
            auto* p = g.pattern (c.pattern);
            if (p == nullptr || p->length <= 0) continue;
            double cs = (double) c.start, ce = (double) (c.start + c.length);
            if (ce + p->length < from || cs > to) continue;
            juce::int64 kMin = std::max<juce::int64> (0, (juce::int64) std::floor ((from - cs) / (double) p->length) - 1);
            juce::int64 kMax = (juce::int64) std::floor ((to - cs) / (double) p->length);
            for (auto k = kMin; k <= kMax; ++k)
            {
                double base = cs + (double) (k * p->length);
                if (base >= ce) break;
                emitPattern (gi, g, *p, base, from, to, spt, n, c.start + c.length);
            }
        }
    }
    else
    {
        auto& sc = proj->scenes[(size_t) juce::jlimit (0, (int) proj->scenes.size() - 1, proj->currentScene)];
        auto* p = g.pattern (sc.patternFor (gi));
        if (p == nullptr || p->length <= 0) return;
        double L = (double) p->length;
        auto kMin = (juce::int64) std::floor (from / L) - 1, kMax = (juce::int64) std::floor (to / L);
        for (auto k = kMin; k <= kMax; ++k)
            if (k >= 0) emitPattern (gi, g, *p, (double) k * L, from, to, spt, n, std::numeric_limits<juce::int64>::max());
    }
}

void Engine::applyAutomation (int gi, Group& g, double tick)
{
    Pattern* p = nullptr; double local = 0;
    if (songMode && proj->songLength() > 0)
    {
        for (auto& c : g.clips)
            if (tick >= c.start && tick < c.start + c.length) { p = g.pattern (c.pattern); if (p) local = std::fmod (tick - (double) c.start, (double) p->length); break; }
    }
    else
    {
        auto& sc = proj->scenes[(size_t) juce::jlimit (0, (int) proj->scenes.size() - 1, proj->currentScene)];
        p = g.pattern (sc.patternFor (gi));
        if (p) local = std::fmod (tick, (double) p->length);
    }
    if (p == nullptr || p->lanes.empty()) return;
    int step = (int) (local / (PPQ / 4));
    for (auto& lane : p->lanes)
    {
        auto it = lane.steps.find (step);
        if (it == lane.steps.end() || ! juce::isPositiveAndBelow (lane.sound, numPads)) continue;
        auto& s = *g.sounds[(size_t) lane.sound];
        float v = it->second;
        if (lane.target == "level") s.routing.level = v * 2.0f;
        else if (lane.target == "pan") s.routing.pan = v * 2.0f - 1.0f;
        else if (lane.target.startsWith ("slot:"))
        {
            auto parts = juce::StringArray::fromTokens (lane.target, ":", "");
            int si = parts[1].getIntValue(), pi = parts[2].getIntValue();
            if (juce::isPositiveAndBelow (si, (int) s.slots.size()) && s.slots[(size_t) si]->proc)
            {
                auto& ps = s.slots[(size_t) si]->proc->getParameters();
                if (juce::isPositiveAndBelow (pi, ps.size())) ps[pi]->setValue (v);
            }
        }
    }
}

void Engine::scheduleSequencer (int n)
{
    const double spt = sampleRate * 60.0 / (proj->bpm * PPQ);  // samples per tick
    const double ticks = n / spt;
    double from = position.load();
    blockStartTick = from;

    if (countingIn)
    {
        countInRemaining -= ticks;
        if (countInRemaining <= 0) countingIn = false;
        return;
    }

    bool song = songMode && proj->songLength() > 0;
    double to = from + ticks;
    if (song)
    {
        double len = (double) proj->songLength();
        if (from >= len)
        {
            if (proj->loopOn) from = std::fmod (from, len);
            else { playing = false; position = 0.0; return; }
            to = from + ticks;
        }
        if (to > len)
        {
            int split = juce::jlimit (0, n, (int) ((len - from) * spt));
            for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
            {
                applyAutomation (gi, *proj->groups[(size_t) gi], from);
                // + 0.5 tick so note-offs that fall exactly on the song end are still sent
                scheduleGroup (gi, *proj->groups[(size_t) gi], from, len + 0.5, spt, n);
            }
            if (proj->loopOn)
            {
                // second part restarts at tick 0, `split` samples into the block. A window starting at
                // -split/spt maps tick t to sample split + t * spt, and handles note-offs, probability and
                // pattern repeats inside clips exactly like a normal block.
                double rem = to - len;
                for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
                {
                    applyAutomation (gi, *proj->groups[(size_t) gi], 0.0);
                    scheduleGroup (gi, *proj->groups[(size_t) gi], -(double) split / spt, rem, spt, n);
                }
                position = rem;
            }
            else { playing = false; position = 0.0; }
            return;
        }
    }

    for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
    {
        applyAutomation (gi, *proj->groups[(size_t) gi], from);
        scheduleGroup (gi, *proj->groups[(size_t) gi], from, to, spt, n);
    }
    position = to;
}

void Engine::addMetronome (juce::AudioBuffer<float>& out, int n, double fromTick, double spt)
{
    bool active = metronomeOn || blockCountIn || (recording && autoMetroRec);
    if (! active && metroEnv <= 0 && metroSamplePos < 0) return;
    const double beat = (double) PPQ * 4.0 / proj->sigDen;
    const double bar = (double) proj->ticksPerBar();
    for (int i = 0; i < n; ++i)
    {
        if (active && playing)
        {
            double t0 = blockCountIn ? (countInTotal - countInAtBlockStart) + i / spt : fromTick + i / spt;
            double t1 = t0 + 1.0 / spt;
            double nb = std::ceil (t0 / beat) * beat;
            if (nb >= t0 && nb < t1)
            {
                bool down = std::fmod (nb, bar) < 1.0;
                auto smp = down ? metroDown : metroUp;
                if (smp != nullptr) { metroSamplePos = 0; metroSampleDown = down; }
                else { metroEnv = 1.0f; metroFreq = down ? 1600.0f : 1000.0f; metroPhase = 0; }
            }
        }
        float v = 0;
        if (metroEnv > 0.0001f)
        {
            metroPhase += metroFreq / sampleRate;
            v += std::sin ((float) (metroPhase * juce::MathConstants<double>::twoPi)) * metroEnv;
            metroEnv *= 0.9985f;
        }
        else metroEnv = 0;
        if (metroSamplePos >= 0)
        {
            auto smp = metroSampleDown ? metroDown : metroUp;
            if (smp != nullptr && (int) metroSamplePos < smp->buffer.getNumSamples()) { v += smp->buffer.getSample (0, (int) metroSamplePos); metroSamplePos += smp->sampleRate / sampleRate; }
            else metroSamplePos = -1;
        }
        v *= metroVolume.load() * 0.5f;
        out.addSample (0, i, v);
        out.addSample (1, i, v);
    }
}

void Engine::handleLiveMidi (const InMsg& m, int offset)
{
    const auto& msg = m.msg;
    const double spt = sampleRate * 60.0 / (proj->bpm * PPQ);
    auto recordNote = [&] (int gi, int si, int pitch, float vel, bool on)
    {
        if (! recording || ! playing || countingIn) return;
        int s1, n1, s2, n2;
        recFifo.prepareToWrite (1, s1, n1, s2, n2);
        RecNote r { gi, si, pitch, (juce::int64) (position.load() + offset / spt), vel, on };
        if (n1 > 0) recQueue[(size_t) s1] = r; else if (n2 > 0) recQueue[(size_t) s2] = r;
        recFifo.finishedWrite (n1 + n2);
    };

    if (m.group >= 0)   // GUI pad / computer keyboard
    {
        int pitch = m.device == -2 ? -1 : msg.getNoteNumber();
        if (msg.isNoteOn()) { noteToSound (m.group, m.sound, offset, msg.getFloatVelocity(), pitch, true); recordNote (m.group, m.sound, pitch, msg.getFloatVelocity(), true); }
        else if (msg.isNoteOff()) { noteToSound (m.group, m.sound, offset, 0, pitch, false); recordNote (m.group, m.sound, pitch, 0, false); }
        return;
    }
    int fg = juce::jlimit (0, (int) proj->groups.size() - 1, focusedGroup.load());
    auto* G = proj->group (fg);
    if (G == nullptr) return;

    if (msg.isNoteOnOrOff())
    {
        int note = msg.getNoteNumber();
        bool on = msg.isNoteOn();
        float vel = on ? applyVelocityCurve (msg.getFloatVelocity()) : 0.0f;
        int pb = padBase, pc = padChannel;
        // 1) Maschine pads (MIDI mode template)
        if (note >= pb && note < pb + numPads && (pc == 0 || msg.getChannel() == pc))
        {
            int i = note - pb;
            if (padFunction != 0)
            {
                if (on) juce::MessageManager::callAsync ([this, i, vel] { if (onPadFunction) onPadFunction (i, vel); });
                return;
            }
            if (fixedVelOn && on) vel = fixedVelocity.load() / 127.0f;
            if (on) { heldPads |= (1u << i); heldVel[(size_t) i] = vel; }
            else heldPads &= ~(1u << i);
            if (keyboardMode)
            {
                for (int nn : padNotes (i, G->sounds[(size_t) G->focusedSound]->rootKey))
                {
                    noteToSound (fg, G->focusedSound, offset, vel, nn, on);
                    recordNote (fg, G->focusedSound, nn, vel, on);
                }
            }
            else
            {
                noteToSound (fg, i, offset, vel, -1, on);
                recordNote (fg, i, -1, vel, on);
                if (on && G->sounds[(size_t) i]->focusMode == 0) G->focusedSound = i;
            }
            return;
        }
        // 2) Drumkit key mode on the focused group: notes map to sounds by trigger note
        if (G->routing.keyMode == 1)
        {
            for (int si = 0; si < numPads; ++si)
            {
                auto& s = *G->sounds[(size_t) si];
                int trig = s.triggerNote >= 0 ? s.triggerNote : 36 + si;
                if (trig == note) { noteToSound (fg, si, offset, vel, -1, on); recordNote (fg, si, -1, vel, on); return; }
            }
        }
        // 3) Per-sound MIDI input routing (Focus / All / device)
        for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
        {
            auto& g = *proj->groups[(size_t) gi];
            for (int si = 0; si < numPads; ++si)
            {
                auto& s = *g.sounds[(size_t) si];
                if (s.slots.empty()) continue;
                int src = s.routing.midiSource;
                if (src == 0) src = defaultMidiMode == 0 ? 1 : defaultMidiMode == 1 ? 2 : 3;
                bool match = (src == 1 && gi == fg && si == g.focusedSound) || src == 3 || (src >= 4 && src - 4 == m.device);
                if (! match) continue;
                if (s.routing.midiChannel != 0 && s.routing.midiChannel != msg.getChannel()) continue;
                noteToSound (gi, si, offset, vel, note, on);
                recordNote (gi, si, note, vel, on);
            }
        }
        return;
    }
    // CC / pitch bend / aftertouch go to the focused sound
    size_t idx = (size_t) (fg * numPads + G->focusedSound);
    if (idx < soundMidi.size()) soundMidi[idx].addEvent (msg, offset);
}

void Engine::sendClock (int n)
{
    if (clockOut == nullptr || syncMode != 1 || ! playing) return;
    const double clocksPerSample = proj->bpm * 24.0 / 60.0 / sampleRate;
    clockPhase += clocksPerSample * n;
    while (clockPhase >= 1.0) { clockOut->sendMessageNow (juce::MidiMessage::midiClock()); clockPhase -= 1.0; }
}

void Engine::processBlockInternal (const float* const* in, int numIn, int n)
{
    auto& P = *proj;
    hostBpm = P.bpm;
    if (soundMidi.size() < P.groups.size() * numPads) soundMidi.resize (P.groups.size() * numPads);
    if (groupMidi.size() < P.groups.size()) groupMidi.resize (P.groups.size());
    for (auto& m : soundMidi) m.clear();
    for (auto& m : groupMidi) m.clear();
    if (allNotesOffPending.exchange (false))
        for (auto& m : soundMidi) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    for (auto* b : { &masterBuf, &aux1Buf, &aux2Buf, &cueBuf }) b->clear (0, n);
    for (auto& e : extBufs) e.clear (0, n);

    // live input
    {
        int s1, n1, s2, n2;
        midiFifo.prepareToRead (midiFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) handleLiveMidi (midiQueue[(size_t) (s1 + i)], 0);
        for (int i = 0; i < n2; ++i) handleLiveMidi (midiQueue[(size_t) (s2 + i)], 0);
        midiFifo.finishedRead (n1 + n2);
    }

    const double spt = sampleRate * 60.0 / (P.bpm * PPQ);
    double startTick = position.load();
    bool wasPlaying = playing;
    blockCountIn = countingIn; countInAtBlockStart = countInRemaining;
    if (playing) scheduleSequencer (n);
    processNoteRepeat (n);
    sendClock (n);

    // input levels for the sampling page
    auto inChan = [&] (int c) -> const float* { return c < numIn ? in[c] : nullptr; };
    {
        int a = rec.input * 2, b = a + 1;
        if (rec.source == 1) { a = rec.input; b = rec.input; }
        float pl = 0, pr = 0;
        if (auto* l = inChan (a)) pl = juce::FloatVectorOperations::findMaximum (l, n), pl = juce::jmax (pl, -juce::FloatVectorOperations::findMinimum (l, n));
        if (auto* r = inChan (b)) pr = juce::FloatVectorOperations::findMaximum (r, n), pr = juce::jmax (pr, -juce::FloatVectorOperations::findMinimum (r, n));
        inLevelL = juce::jmax (pl, inLevelL * 0.9f);
        inLevelR = juce::jmax (pr, inLevelR * 0.9f);
    }

    // groups & sounds
    anyGroupSolo = false;
    for (auto& g : P.groups) anyGroupSolo |= g->solo;
    for (int gi = 0; gi < (int) P.groups.size(); ++gi)
    {
        auto& g = *P.groups[(size_t) gi];
        groupBuf.clear (0, n);
        bool anySoundSolo = false;
        for (auto& s : g.sounds) anySoundSolo |= s->solo;
        for (int si = 0; si < numPads; ++si)
        {
            auto& s = *g.sounds[(size_t) si];
            if (s.slots.empty() && s.routing.audioIn == 0) { s.meterL = s.meterL * 0.8f; s.meterR = s.meterR * 0.8f; continue; }
            soundBuf.clear (0, n);
            if (s.routing.audioIn > 0)
            {
                int ai = s.routing.audioIn - 1;
                int l = ai < 16 ? ai * 2 : ai - 16, r = ai < 16 ? ai * 2 + 1 : ai - 16;
                if (auto* x = inChan (l)) soundBuf.copyFrom (0, 0, x, n);
                if (auto* x = inChan (r)) soundBuf.copyFrom (1, 0, x, n);
            }
            if (! s.slots.empty())
                if (auto* sp = dynamic_cast<SamplerProcessor*> (s.slots[0]->proc.get())) sp->rootKey = s.rootKey;
            processChain (s.slots, soundBuf, soundMidi[(size_t) (gi * numPads + si)], n);
            bool audible = ! s.mute && (! anySoundSolo || s.solo);
            auto sendAux = [&] (int dest, float lvl)
            {
                if (dest == destNone || lvl <= 0) return;
                route (dest == destGroup ? destMaster : dest, soundBuf, lvl, 0, n, false);
            };
            if (audible && ! s.routing.aux1Post) sendAux (s.routing.aux1Dest, s.routing.aux1Level);
            if (audible && ! s.routing.aux2Post) sendAux (s.routing.aux2Dest, s.routing.aux2Level);
            float gl = s.routing.level * juce::jmin (1.0f, 1.0f - s.routing.pan), gr = s.routing.level * juce::jmin (1.0f, 1.0f + s.routing.pan);
            soundBuf.applyGain (0, 0, n, gl);
            soundBuf.applyGain (1, 0, n, gr);
            float ml = audible ? soundBuf.getMagnitude (0, 0, n) : 0.0f, mr = audible ? soundBuf.getMagnitude (1, 0, n) : 0.0f;
            s.meterL = juce::jmax (ml, s.meterL * 0.85f);
            s.meterR = juce::jmax (mr, s.meterR * 0.85f);
            if (! audible) continue;
            route (s.routing.outDest, soundBuf, 1.0f, 0.0f, n, true);
            if (s.routing.aux1Post) sendAux (s.routing.aux1Dest, s.routing.aux1Level);
            if (s.routing.aux2Post) sendAux (s.routing.aux2Dest, s.routing.aux2Level);
            if (s.routing.cue) { cueBuf.addFrom (0, 0, soundBuf, 0, 0, n); cueBuf.addFrom (1, 0, soundBuf, 1, 0, n); }
        }
        processChain (g.slots, groupBuf, groupMidi[(size_t) gi], n);
        bool gAudible = ! g.mute && (! anyGroupSolo || g.solo);
        float gl = g.routing.level * juce::jmin (1.0f, 1.0f - g.routing.pan), gr = g.routing.level * juce::jmin (1.0f, 1.0f + g.routing.pan);
        groupBuf.applyGain (0, 0, n, gl);
        groupBuf.applyGain (1, 0, n, gr);
        g.meterL = juce::jmax (gAudible ? groupBuf.getMagnitude (0, 0, n) : 0.0f, g.meterL * 0.85f);
        g.meterR = juce::jmax (gAudible ? groupBuf.getMagnitude (1, 0, n) : 0.0f, g.meterR * 0.85f);
        if (! gAudible) continue;
        route (g.routing.outDest == destGroup ? destMaster : g.routing.outDest, groupBuf, 1.0f, 0.0f, n, false);
        if (g.routing.aux1Dest != destNone && g.routing.aux1Level > 0) route (g.routing.aux1Dest == destGroup ? destMaster : g.routing.aux1Dest, groupBuf, g.routing.aux1Level, 0, n, false);
        if (g.routing.aux2Dest != destNone && g.routing.aux2Level > 0) route (g.routing.aux2Dest == destGroup ? destMaster : g.routing.aux2Dest, groupBuf, g.routing.aux2Level, 0, n, false);
        if (g.routing.cue) { cueBuf.addFrom (0, 0, groupBuf, 0, 0, n); cueBuf.addFrom (1, 0, groupBuf, 1, 0, n); }
    }

    // master
    processChain (P.masterSlots, masterBuf, emptyMidi, n);
    emptyMidi.clear();
    {
        float gl = P.master.level * juce::jmin (1.0f, 1.0f - P.master.pan), gr = P.master.level * juce::jmin (1.0f, 1.0f + P.master.pan);
        masterBuf.applyGain (0, 0, n, gl);
        masterBuf.applyGain (1, 0, n, gr);
    }

    // sampling capture (before metronome)
    if (recState == RecState::waiting || recState == RecState::recording)
    {
        auto srcL = [&] (int i) -> float
        {
            if (rec.source == 2) return masterBuf.getSample (0, i);
            auto* l = inChan (rec.source == 1 ? rec.input : rec.input * 2);
            return l ? l[i] : 0.0f;
        };
        auto srcR = [&] (int i) -> float
        {
            if (rec.source == 2) return masterBuf.getSample (1, i);
            auto* r = inChan (rec.source == 1 ? rec.input : rec.input * 2 + 1);
            return r ? r[i] : 0.0f;
        };
        int startAt = 0;
        if (recState == RecState::waiting)
        {
            startAt = -1;
            if (rec.mode == 0)   // Detect
            {
                for (int i = 0; i < n; ++i) if (std::abs (srcL (i)) > rec.threshold || std::abs (srcR (i)) > rec.threshold) { startAt = i; break; }
            }
            else if (rec.mode == 3)   // Auto: with the transport
            {
                if (wasPlaying && ! countingIn) startAt = 0;
            }
            else if (wasPlaying && ! countingIn)   // Sync / Loop: next bar
            {
                double bar = (double) P.ticksPerBar();
                double nb = std::ceil (startTick / bar) * bar;
                double endTick = startTick + n / spt;
                if (startTick < 1.0) nb = 0;
                if (nb >= startTick && nb < endTick) startAt = juce::jlimit (0, n - 1, (int) ((nb - startTick) * spt));
            }
            if (startAt >= 0)
            {
                recState = RecState::recording; recWritePos = 0;
                if (rec.length < 5) recTargetLen = (int) std::min<double> (recBuffer.getNumSamples(), (1 << rec.length) * P.ticksPerBar() * spt);
                else recTargetLen = recBuffer.getNumSamples();
            }
        }
        if (recState == RecState::recording && startAt >= 0)
        {
            for (int i = startAt; i < n; ++i)
            {
                if (recWritePos >= recTargetLen)
                {
                    if (rec.mode == 2) recWritePos = 0;   // Loop: keep overwriting
                    else { recState = RecState::done; break; }
                }
                recBuffer.setSample (0, recWritePos, srcL (i));
                recBuffer.setSample (1, recWritePos, srcR (i));
                ++recWritePos;
            }
        }
        if (rec.monitor && rec.source != 2)
            for (int i = 0; i < n; ++i) { masterBuf.addSample (0, i, srcL (i)); masterBuf.addSample (1, i, srcR (i)); }
    }

    addMetronome (masterBuf, n, startTick, spt);
    P.meterL = juce::jmax (masterBuf.getMagnitude (0, 0, n), P.meterL * 0.85f);
    P.meterR = juce::jmax (masterBuf.getMagnitude (1, 0, n), P.meterR * 0.85f);
    route (P.master.outDest <= destMaster ? destExt1 : P.master.outDest, masterBuf, 1.0f, 0.0f, n, false);
    if (cueDest > 0) route (destExt1 + cueDest - 1, cueBuf, 1.0f, 0.0f, n, false);

    // browser preview
    if (previewPos >= 0)
    {
        const juce::ScopedTryLock tl (previewLock);
        if (tl.isLocked() && preview != nullptr)
        {
            auto& pb = preview->buffer;
            for (int i = 0; i < n && previewPos >= 0; ++i)
            {
                int ip = (int) previewPos;
                if (ip >= pb.getNumSamples()) { previewPos = -1; break; }
                const float pg = previewGain.load();
                extBufs[0].addSample (0, i, pb.getSample (0, ip) * pg);
                extBufs[0].addSample (1, i, pb.getSample (pb.getNumChannels() > 1 ? 1 : 0, ip) * pg);
                previewPos += previewInc;
            }
        }
    }
}
} // namespace gb

namespace gb
{
std::vector<int> Engine::padNotes (int pad, int root) const
{
    int base = juce::jlimit (0, 127, root + pad + keyboardOffset.load());
    if (! chordMode) return { base };
    // diatonic triads of the major scale built on each pad's note
    int deg = ((pad % 12) + 12) % 12;
    bool minor = deg == 2 || deg == 4 || deg == 9 || deg == 1 || deg == 3 || deg == 8 || deg == 10;
    return { base, juce::jmin (127, base + (minor ? 3 : 4)), juce::jmin (127, base + 7) };
}

void Engine::processNoteRepeat (int n)
{
    if (! noteRepeat || heldPads.load() == 0) { lastRepeatTick = -1; return; }
    const double spt = sampleRate * 60.0 / (proj->bpm * PPQ);
    const double step = (double) gridTicks (repeatRate);
    double from = playing && ! countingIn ? blockStartTick : repeatClock;
    double to = from + n / spt;
    repeatClock = to;
    int fg = juce::jlimit (0, (int) proj->groups.size() - 1, focusedGroup.load());
    auto* G = proj->group (fg);
    if (G == nullptr) return;
    for (double t = std::ceil (from / step) * step; t < to; t += step)
    {
        if (t <= lastRepeatTick) continue;
        lastRepeatTick = t;
        int off = juce::jlimit (0, n - 1, (int) ((t - from) * spt));
        auto mask = heldPads.load();
        for (int i = 0; i < numPads; ++i)
        {
            if ((mask & (1u << i)) == 0) continue;
            float vel = heldVel[(size_t) i].load();
            if (keyboardMode)
                for (int nn : padNotes (i, G->sounds[(size_t) G->focusedSound]->rootKey))
                {
                    noteToSound (fg, G->focusedSound, off, 0, nn, false);
                    noteToSound (fg, G->focusedSound, off, vel, nn, true);
                }
            else
            {
                noteToSound (fg, i, off, 0, -1, false);
                noteToSound (fg, i, off, vel, -1, true);
            }
            if (recording && playing && ! countingIn)
            {
                int s1, n1, s2, n2;
                recFifo.prepareToWrite (1, s1, n1, s2, n2);
                RecNote r { fg, keyboardMode ? G->focusedSound : i, keyboardMode ? padNotes (i, G->sounds[(size_t) G->focusedSound]->rootKey)[0] : -1, (juce::int64) t, vel, true };
                if (n1 > 0) recQueue[(size_t) s1] = r; else if (n2 > 0) recQueue[(size_t) s2] = r;
                recFifo.finishedWrite (n1 + n2);
            }
        }
    }
}
} // namespace gb
