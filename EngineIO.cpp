#include "Engine.h"
#include "Theme.h"

// Project save/load, undo, sampling and audio export.
namespace gb
{
extern juce::File projectSampleFolder;

namespace
{
    void writeRouting (juce::ValueTree& v, const Routing& r)
    {
        v.setProperty ("out", r.outDest, nullptr);
        v.setProperty ("level", r.level, nullptr);
        v.setProperty ("pan", r.pan, nullptr);
        v.setProperty ("cue", r.cue, nullptr);
        v.setProperty ("a1", r.aux1Dest, nullptr); v.setProperty ("a1l", r.aux1Level, nullptr); v.setProperty ("a1p", r.aux1Post, nullptr);
        v.setProperty ("a2", r.aux2Dest, nullptr); v.setProperty ("a2l", r.aux2Level, nullptr); v.setProperty ("a2p", r.aux2Post, nullptr);
        v.setProperty ("ain", r.audioIn, nullptr);
        v.setProperty ("msrc", r.midiSource, nullptr); v.setProperty ("mch", r.midiChannel, nullptr); v.setProperty ("thru", r.midiThru, nullptr);
        v.setProperty ("keymode", r.keyMode, nullptr);
    }
    void readRouting (const juce::ValueTree& v, Routing& r)
    {
        r.outDest = v.getProperty ("out", r.outDest);
        r.level = v.getProperty ("level", r.level);
        r.pan = v.getProperty ("pan", r.pan);
        r.cue = v.getProperty ("cue", r.cue);
        r.aux1Dest = v.getProperty ("a1", r.aux1Dest); r.aux1Level = v.getProperty ("a1l", r.aux1Level); r.aux1Post = v.getProperty ("a1p", r.aux1Post);
        r.aux2Dest = v.getProperty ("a2", r.aux2Dest); r.aux2Level = v.getProperty ("a2l", r.aux2Level); r.aux2Post = v.getProperty ("a2p", r.aux2Post);
        r.audioIn = v.getProperty ("ain", r.audioIn);
        r.midiSource = v.getProperty ("msrc", r.midiSource); r.midiChannel = v.getProperty ("mch", r.midiChannel); r.midiThru = v.getProperty ("thru", r.midiThru);
        r.keyMode = v.getProperty ("keymode", r.keyMode);
    }
    void writeGroove (juce::ValueTree& v, const Groove& g) { v.setProperty ("swing", g.swing, nullptr); v.setProperty ("cycle", g.cycle, nullptr); v.setProperty ("invert", g.invert, nullptr); }
    void readGroove (const juce::ValueTree& v, Groove& g) { g.swing = v.getProperty ("swing", 0.0f); g.cycle = v.getProperty ("cycle", 1); g.invert = v.getProperty ("invert", false); }

    juce::ValueTree writeMacros (const std::array<Macro, 8>& m)
    {
        juce::ValueTree v ("MACROS");
        for (auto& x : m)
        {
            juce::ValueTree c ("M");
            c.setProperty ("name", x.name, nullptr); c.setProperty ("slot", x.slot, nullptr); c.setProperty ("param", x.param, nullptr); c.setProperty ("value", x.value, nullptr);
            v.appendChild (c, nullptr);
        }
        return v;
    }
    void readMacros (const juce::ValueTree& v, std::array<Macro, 8>& m)
    {
        for (int i = 0; i < juce::jmin (8, v.getNumChildren()); ++i)
        {
            auto c = v.getChild (i);
            m[(size_t) i].name = c["name"].toString(); m[(size_t) i].slot = c.getProperty ("slot", -1); m[(size_t) i].param = c.getProperty ("param", -1); m[(size_t) i].value = c.getProperty ("value", 0.5f);
        }
    }
}

static juce::ValueTree writeChain (const std::vector<std::unique_ptr<Slot>>& chain, bool forUndo)
{
    juce::ValueTree v ("SLOTS");
    for (auto& s : chain)
    {
        juce::ValueTree c ("SLOT");
        c.setProperty ("type", s->type, nullptr);
        c.setProperty ("id", s->pluginId, nullptr);
        c.setProperty ("name", s->displayName, nullptr);
        c.setProperty ("bypass", s->bypass, nullptr);
        c.setProperty ("inst", s->isInstrument, nullptr);
        if (s->proc != nullptr && ! (forUndo && s->type == "VST3"))
        {
            juce::MemoryBlock mb;
            s->proc->getStateInformation (mb);
            c.setProperty ("state", mb.toBase64Encoding(), nullptr);
        }
        v.appendChild (c, nullptr);
    }
    return v;
}

static juce::ValueTree writeSound (const Sound& s, int idx, bool forUndo)
{
    juce::ValueTree v ("SOUND");
    v.setProperty ("idx", idx, nullptr);
    v.setProperty ("name", s.name, nullptr);
    v.setProperty ("colour", s.colour.toString(), nullptr);
    v.setProperty ("mute", s.mute, nullptr); v.setProperty ("solo", s.solo, nullptr);
    v.setProperty ("root", s.rootKey, nullptr); v.setProperty ("trig", s.triggerNote, nullptr);
    v.setProperty ("choke", s.chokeGroup, nullptr); v.setProperty ("chokeMode", s.chokeMode, nullptr);
    v.setProperty ("link", s.linkGroup, nullptr); v.setProperty ("linkMode", s.linkMode, nullptr);
    v.setProperty ("focus", s.focusMode, nullptr);
    writeRouting (v, s.routing);
    juce::ValueTree gr ("GROOVE"); writeGroove (gr, s.groove); v.appendChild (gr, nullptr);
    v.appendChild (writeChain (s.slots, forUndo), nullptr);
    v.appendChild (writeMacros (s.macros), nullptr);
    return v;
}

static juce::ValueTree writePattern (const Pattern& p)
{
    juce::ValueTree v ("PATTERN");
    v.setProperty ("name", p.name, nullptr);
    v.setProperty ("length", (juce::int64) p.length, nullptr);
    v.setProperty ("colour", p.colour.toString(), nullptr);
    juce::String notes;
    for (auto& n : p.notes)
        notes << n.sound << "," << n.start << "," << n.length << "," << juce::String (n.velocity, 4) << "," << juce::String (n.probability, 3) << "," << n.pitch << ";";
    v.setProperty ("notes", notes, nullptr);
    for (auto& l : p.lanes)
    {
        juce::ValueTree lv ("LANE");
        lv.setProperty ("sound", l.sound, nullptr); lv.setProperty ("target", l.target, nullptr); lv.setProperty ("name", l.name, nullptr);
        juce::String st;
        for (auto& [k, val] : l.steps) st << k << ":" << juce::String (val, 4) << ";";
        lv.setProperty ("steps", st, nullptr);
        v.appendChild (lv, nullptr);
    }
    return v;
}

static std::unique_ptr<Pattern> readPattern (const juce::ValueTree& v)
{
    auto p = std::make_unique<Pattern>();
    p->name = v["name"].toString();
    p->length = std::max<juce::int64> (PPQ / 4, (juce::int64) v.getProperty ("length", PPQ * 4));   // 0 => division by zero (% length)
    p->colour = juce::Colour::fromString (v.getProperty ("colour", "ffff7442").toString());
    for (auto& tok : juce::StringArray::fromTokens (v["notes"].toString(), ";", ""))
    {
        auto f = juce::StringArray::fromTokens (tok, ",", "");
        if (f.size() < 5) continue;
        NoteEvent n;
        n.sound = f[0].getIntValue(); n.start = f[1].getLargeIntValue(); n.length = f[2].getLargeIntValue();
        n.velocity = f[3].getFloatValue(); n.probability = f[4].getFloatValue(); n.pitch = f.size() > 5 ? f[5].getIntValue() : -1;
        if (! juce::isPositiveAndBelow (n.sound, numPads)) continue;   // GUI indexes sounds[n.sound] directly
        n.start = std::max<juce::int64> (0, n.start);
        n.length = std::max<juce::int64> (1, n.length);
        n.pitch = juce::jlimit (-1, 127, n.pitch);
        p->notes.push_back (n);
    }
    for (auto lv : v)
    {
        if (! lv.hasType ("LANE")) continue;
        AutoLane l; l.sound = lv["sound"]; l.target = lv["target"].toString(); l.name = lv["name"].toString();
        for (auto& tok : juce::StringArray::fromTokens (lv["steps"].toString(), ";", ""))
            if (tok.contains (":")) l.steps[tok.upToFirstOccurrenceOf (":", false, false).getIntValue()] = tok.fromFirstOccurrenceOf (":", false, false).getFloatValue();
        p->lanes.push_back (l);
    }
    return p;
}

static juce::ValueTree writeGroup (const Group& g, bool forUndo)
{
    juce::ValueTree v ("GROUP");
    v.setProperty ("name", g.name, nullptr);
    v.setProperty ("colour", g.colour.toString(), nullptr);
    v.setProperty ("mute", g.mute, nullptr); v.setProperty ("solo", g.solo, nullptr);
    v.setProperty ("focused", g.focusedSound, nullptr);
    v.setProperty ("current", g.currentPattern, nullptr);
    writeRouting (v, g.routing);
    juce::ValueTree gr ("GROOVE"); writeGroove (gr, g.groove); v.appendChild (gr, nullptr);
    v.appendChild (writeChain (g.slots, forUndo), nullptr);
    v.appendChild (writeMacros (g.macros), nullptr);
    for (int i = 0; i < numPads; ++i) v.appendChild (writeSound (*g.sounds[(size_t) i], i, forUndo), nullptr);
    for (auto& p : g.patterns) v.appendChild (writePattern (*p), nullptr);
    for (auto& c : g.clips)
    {
        juce::ValueTree cv ("CLIP");
        cv.setProperty ("start", (juce::int64) c.start, nullptr); cv.setProperty ("length", (juce::int64) c.length, nullptr); cv.setProperty ("pattern", c.pattern, nullptr);
        if (c.section >= 0) cv.setProperty ("sec", c.section, nullptr);
        v.appendChild (cv, nullptr);
    }
    return v;
}

juce::ValueTree Engine::toValueTree (bool forUndo)
{
    const juce::ScopedLock sl (lock);
    auto& P = *proj;
    juce::ValueTree v ("GROOVEBOXPRO");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("name", P.name, nullptr);
    v.setProperty ("bpm", P.bpm, nullptr);
    v.setProperty ("swing", P.swing, nullptr);
    v.setProperty ("sigNum", P.sigNum, nullptr); v.setProperty ("sigDen", P.sigDen, nullptr);
    v.setProperty ("scene", P.currentScene, nullptr);
    v.setProperty ("loopOn", P.loopOn, nullptr);
    v.setProperty ("songMode", songMode.load(), nullptr);
    v.setProperty ("focusedGroup", focusedGroup.load(), nullptr);
    juce::ValueTree m ("MASTER");
    writeRouting (m, P.master);
    m.appendChild (writeChain (P.masterSlots, forUndo), nullptr);
    m.appendChild (writeMacros (P.masterMacros), nullptr);
    v.appendChild (m, nullptr);
    for (auto& g : P.groups) v.appendChild (writeGroup (*g, forUndo), nullptr);
    for (auto& sc : P.scenes)
    {
        juce::ValueTree s ("SCENE");
        s.setProperty ("name", sc.name, nullptr);
        juce::StringArray a; for (auto i : sc.patternForGroup) a.add (juce::String (i));
        s.setProperty ("patterns", a.joinIntoString (","), nullptr);
        s.setProperty ("placed", sc.placed, nullptr);
        v.appendChild (s, nullptr);
    }
    for (auto& se : P.sections)
    {
        juce::ValueTree s ("SECTION");
        s.setProperty ("id", se.id, nullptr);
        s.setProperty ("start", (juce::int64) se.start, nullptr);
        s.setProperty ("length", (juce::int64) se.length, nullptr);
        s.setProperty ("scene", se.scene, nullptr);
        v.appendChild (s, nullptr);
    }
    return v;
}

// Re-uses slot instances from `reuse` (same position, same type) so undo keeps plug-ins alive.
static void readChain (Engine& e, const juce::ValueTree& v, std::vector<std::unique_ptr<Slot>>& out, std::vector<std::unique_ptr<Slot>>* reuse, juce::StringArray& errors)
{
    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        auto c = v.getChild (i);
        auto type = c["type"].toString();
        auto id = c["id"].toString();
        std::unique_ptr<Slot> slot;
        if (reuse != nullptr && i < (int) reuse->size() && (*reuse)[(size_t) i] != nullptr
            && (*reuse)[(size_t) i]->type == type && (*reuse)[(size_t) i]->pluginId == id)
            slot = std::move ((*reuse)[(size_t) i]);
        else
        {
            juce::String err;
            slot = e.createSlot (type, id, err);
            if (slot == nullptr) { errors.add (err); continue; }
        }
        slot->bypass = c["bypass"];
        if (c["name"].toString().isNotEmpty()) slot->displayName = c["name"].toString();
        auto st = c["state"].toString();
        if (st.isNotEmpty() && slot->proc != nullptr)
        {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (st);
            slot->proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        out.push_back (std::move (slot));
    }
}

static void readSound (Engine& e, const juce::ValueTree& v, Sound& s, Sound* reuse, juce::StringArray& errors)
{
    s.name = v["name"].toString();
    s.colour = juce::Colour::fromString (v.getProperty ("colour", "ffff7442").toString());
    s.mute = v["mute"]; s.solo = v["solo"];
    s.rootKey = v.getProperty ("root", 60); s.triggerNote = v.getProperty ("trig", -1);
    s.chokeGroup = v.getProperty ("choke", 0); s.chokeMode = v.getProperty ("chokeMode", 0);
    s.linkGroup = v.getProperty ("link", 0); s.linkMode = v.getProperty ("linkMode", 0);
    s.focusMode = v.getProperty ("focus", 0);
    readRouting (v, s.routing);
    readGroove (v.getChildWithName ("GROOVE"), s.groove);
    readChain (e, v.getChildWithName ("SLOTS"), s.slots, reuse ? &reuse->slots : nullptr, errors);
    readMacros (v.getChildWithName ("MACROS"), s.macros);
}

static void readGroup (Engine& e, const juce::ValueTree& v, Group& g, Group* reuse, juce::StringArray& errors)
{
    g.name = v["name"].toString();
    g.colour = juce::Colour::fromString (v.getProperty ("colour", "ffff7442").toString());
    g.mute = v["mute"]; g.solo = v["solo"];
    g.focusedSound = juce::jlimit (0, numPads - 1, (int) v.getProperty ("focused", 0));   // used as sounds[] index everywhere
    g.currentPattern = v.getProperty ("current", 0);
    readRouting (v, g.routing);
    readGroove (v.getChildWithName ("GROOVE"), g.groove);
    readChain (e, v.getChildWithName ("SLOTS"), g.slots, reuse ? &reuse->slots : nullptr, errors);
    readMacros (v.getChildWithName ("MACROS"), g.macros);
    for (auto c : v)
    {
        if (c.hasType ("SOUND"))
        {
            int idx = c["idx"];
            if (juce::isPositiveAndBelow (idx, numPads))
            {
                g.sounds[(size_t) idx] = std::make_unique<Sound>();
                readSound (e, c, *g.sounds[(size_t) idx], reuse ? reuse->sounds[(size_t) idx].get() : nullptr, errors);
            }
        }
        else if (c.hasType ("PATTERN")) g.patterns.push_back (readPattern (c));
        else if (c.hasType ("CLIP")) g.clips.push_back ({ std::max<juce::int64> (0, (juce::int64) c["start"]), std::max<juce::int64> (1, (juce::int64) c["length"]), (int) c["pattern"], (int) c.getProperty ("sec", -1) });
    }
}

std::vector<juce::int64> sectionSignature (Project& P, const Section& se);

std::unique_ptr<Project> Engine::fromValueTree (const juce::ValueTree& v, Project* reuse)
{
    // bail out before readChain() moves any slot out of `reuse` (the live project the audio thread is using)
    if (! v.hasType ("GROOVEBOXPRO") || ! v.getChildWithName ("GROUP").isValid()) return {};
    auto p = std::make_unique<Project>();
    juce::StringArray errors;
    p->name = v.getProperty ("name", "New Project").toString();
    p->bpm = juce::jlimit (20.0, 300.0, (double) v.getProperty ("bpm", 120.0));
    p->swing = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("swing", 0.0f));
    p->sigNum = juce::jlimit (1, 32, (int) v.getProperty ("sigNum", 4));
    p->sigDen = juce::jlimit (1, 32, (int) v.getProperty ("sigDen", 4));   // 0 => division by zero in ticksPerBar()
    p->currentScene = v.getProperty ("scene", 0);
    p->loopOn = v.getProperty ("loopOn", false);
    auto m = v.getChildWithName ("MASTER");
    readRouting (m, p->master);
    readChain (*this, m.getChildWithName ("SLOTS"), p->masterSlots, reuse ? &reuse->masterSlots : nullptr, errors);
    readMacros (m.getChildWithName ("MACROS"), p->masterMacros);
    int gi = 0;
    for (auto c : v)
    {
        if (c.hasType ("GROUP"))
        {
            auto g = std::make_unique<Group>();
            readGroup (*this, c, *g, reuse ? reuse->group (gi) : nullptr, errors);
            p->groups.push_back (std::move (g));
            ++gi;
        }
        else if (c.hasType ("SCENE"))
        {
            Scene sc; sc.name = c["name"].toString();
            for (auto& t : juce::StringArray::fromTokens (c["patterns"].toString(), ",", "")) sc.patternForGroup.push_back (t.getIntValue());
            sc.placed = c.getProperty ("placed", false);
            p->scenes.push_back (sc);
        }
        else if (c.hasType ("SECTION"))
        {
            Section se;
            se.id = c.getProperty ("id", p->nextSectionId);
            se.start = std::max<juce::int64> (0, (juce::int64) c.getProperty ("start", 0));
            se.length = std::max<juce::int64> (1, (juce::int64) c.getProperty ("length", 1));
            se.scene = c.getProperty ("scene", -1);
            p->nextSectionId = std::max (p->nextSectionId, se.id + 1);
            p->sections.push_back (se);
        }
    }
    if (p->groups.empty()) return {};
    if (p->scenes.empty()) p->scenes.push_back ({ "Scene 1", {} });
    for (auto& sc : p->scenes) while (sc.patternForGroup.size() < p->groups.size()) sc.patternForGroup.push_back (-1);
    // loaded sections are already in sync with their clips
    for (auto& se : p->sections) se.sig = sectionSignature (*p, se);
    // older projects: scenes that already appear in an arrangement count as placed
    if (p->sections.empty() && p->songLength() > 0) for (auto& sc : p->scenes) sc.placed = true;
    p->currentScene = juce::jlimit (0, (int) p->scenes.size() - 1, p->currentScene);   // used as scenes[] index unchecked
    if (errors.size() > 0 && onStatus) onStatus (errors.joinIntoString ("\n"));
    return p;
}

void Engine::replaceProject (std::unique_ptr<Project> p)
{
    {
        const juce::ScopedLock sl (lock);
        std::swap (proj, p);
        prepareAll();
        focusedGroup = juce::jlimit (0, (int) proj->groups.size() - 1, focusedGroup.load());
        hostBpm = proj->bpm;
    }
    finishReplace (std::move (p));
}

void Engine::finishReplace (std::unique_ptr<Project> old)
{
    // close editors whose processors are gone, then destroy the old project
    std::set<juce::String> live;
    auto addChain = [&] (Chain& c) { for (auto& s : c) if (s) live.insert (juce::String::toHexString ((juce::pointer_sized_int) s->proc.get())); };
    addChain (proj->masterSlots);
    for (auto& g : proj->groups) { addChain (g->slots); for (auto& s : g->sounds) addChain (s->slots); }
    editorWindows.erase (std::remove_if (editorWindows.begin(), editorWindows.end(), [&] (auto& w) { return live.count (w->getProperties()["proc"].toString()) == 0; }), editorWindows.end());
    old.reset();
    clipSelection.clear();
    openRecNotes.clear();
    sendChangeMessage();
    if (onProjectReplaced) onProjectReplaced();
}

void Engine::restoreSnapshot (const juce::ValueTree& tree)
{
    std::unique_ptr<Project> np;
    {
        const juce::ScopedLock sl (lock);
        np = fromValueTree (tree, proj.get());   // re-uses plug-in instances from the live project
        if (np == nullptr) return;
        np->file = proj->file;
        std::swap (proj, np);
        prepareAll();
        songMode = (bool) tree.getProperty ("songMode", true);
        focusedGroup = juce::jlimit (0, (int) proj->groups.size() - 1, (int) tree.getProperty ("focusedGroup", 0));
        hostBpm = proj->bpm;
    }
    finishReplace (std::move (np));
}

bool Engine::loadProject (const juce::File& f)
{
    if (exporting) return false;   // the export thread is rendering the current project
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    auto v = juce::ValueTree::fromXml (*xml);
    projectSampleFolder = f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " Samples");
    auto p = fromValueTree (v);
    projectSampleFolder = juce::File();
    if (p == nullptr) return false;
    stop(); stop();
    p->file = f;
    p->name = f.getFileNameWithoutExtension();
    songMode = (bool) v.getProperty ("songMode", true);
    focusedGroup = (int) v.getProperty ("focusedGroup", 0);
    replaceProject (std::move (p));
    undoStack.clear(); redoStack.clear();
    dirty = false;
    auto recent = settings.getList ("recentProjects");
    recent.removeString (f.getFullPathName());
    recent.insert (0, f.getFullPathName());
    while (recent.size() > 12) recent.remove (12);
    settings.setList ("recentProjects", recent);
    settings.set ("lastProject", f.getFullPathName());
    return true;
}

// copy every sample used by Sampler / Audio slots into 'folder' and point the saved state at the copies
void Engine::embedSamples (juce::ValueTree v, const juce::File& folder)
{
    folder.createDirectory();
    // rewrite sampler file paths inside the slot states
    std::function<void (juce::ValueTree)> walk = [&] (juce::ValueTree t)
    {
        if (t.hasType ("SLOT") && (t["type"].toString() == "Sampler" || t["type"].toString() == "Audio"))
        {
            juce::MemoryBlock mb; mb.fromBase64Encoding (t["state"].toString());
            if (auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
            {
                juce::File src (x->getStringAttribute ("file"));
                if (src.existsAsFile())
                {
                    auto dst = folder.getChildFile (src.getFileName());
                    if (dst != src && ! dst.existsAsFile()) src.copyFileTo (dst);
                    x->setAttribute ("file", dst.getFullPathName());
                    juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary (*x, out);
                    t.setProperty ("state", out.toBase64Encoding(), nullptr);
                }
            }
        }
        for (auto c : t) walk (c);
    };
    walk (v);
}

// What a section's clips are built from: scene, position, length and the scene's patterns (+ their lengths)
std::vector<juce::int64> sectionSignature (Project& P, const Section& se)
{
    std::vector<juce::int64> v { se.scene, se.start, se.length };
    if (juce::isPositiveAndBelow (se.scene, (int) P.scenes.size()))
        for (int gi = 0; gi < (int) P.groups.size(); ++gi)
        {
            int pi = P.scenes[(size_t) se.scene].patternFor (gi);
            v.push_back (pi);
            if (auto* pat = P.groups[(size_t) gi]->pattern (pi)) v.push_back (pat->length);
        }
    return v;
}

juce::int64 Engine::sceneLength (int scene)
{
    auto& P = *proj;
    juce::int64 len = 0;
    if (juce::isPositiveAndBelow (scene, (int) P.scenes.size()))
        for (int gi = 0; gi < (int) P.groups.size(); ++gi)
            if (auto* pat = P.groups[(size_t) gi]->pattern (P.scenes[(size_t) scene].patternFor (gi))) len = std::max<juce::int64> (len, pat->length);
    return len > 0 ? len : P.ticksPerBar();
}

// Song sections play Ideas scenes: each section owns one clip per group (the scene's pattern, looped over the
// section). Whenever a section or its scene changes, its clips are rebuilt. A scene that gets its first pattern
// and is not used by any section gets a section of its own at the end of the song.
bool Engine::syncSections()
{
    auto& P = *proj;
    bool any = false;
    for (int si = 0; si < (int) P.scenes.size(); ++si)
    {
        auto& sc = P.scenes[(size_t) si];
        if (sc.placed) continue;
        bool has = false;
        for (int gi = 0; gi < (int) P.groups.size(); ++gi) if (P.groups[(size_t) gi]->pattern (sc.patternFor (gi)) != nullptr) has = true;
        if (! has) continue;
        sc.placed = true;
        bool used = false;
        for (auto& se : P.sections) if (se.scene == si) used = true;
        if (used) continue;
        const juce::ScopedLock sl (lock);
        Section se; se.id = P.nextSectionId++; se.start = P.songLength(); se.length = sceneLength (si); se.scene = si;
        P.sections.push_back (se);
        any = true;
    }
    for (auto& se : P.sections)
    {
        auto sig = sectionSignature (P, se);
        if (sig == se.sig) continue;
        const juce::ScopedLock sl (lock);
        for (auto& g : P.groups) g->clips.erase (std::remove_if (g->clips.begin(), g->clips.end(), [&] (const Clip& c) { return c.section == se.id; }), g->clips.end());
        if (juce::isPositiveAndBelow (se.scene, (int) P.scenes.size()))
            for (int gi = 0; gi < (int) P.groups.size(); ++gi)
            {
                int pi = P.scenes[(size_t) se.scene].patternFor (gi);
                if (P.groups[(size_t) gi]->pattern (pi) != nullptr) P.groups[(size_t) gi]->clips.push_back ({ se.start, se.length, pi, se.id });
            }
        se.sig = sig;
        any = true;
    }
    // clips whose section was deleted
    for (auto& g : P.groups)
        for (auto& c : g->clips)
            if (c.section >= 0 && P.section (c.section) == nullptr) { const juce::ScopedLock sl (lock); g->clips.erase (std::remove_if (g->clips.begin(), g->clips.end(), [&] (const Clip& x) { return x.section >= 0 && P.section (x.section) == nullptr; }), g->clips.end()); any = true; break; }
    if (any) dirty = true;
    return any;
}

bool Engine::saveProject (const juce::File& f, bool withSamples)
{
    auto v = toValueTree (false);
    if (withSamples)
    {
        embedSamples (v, f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " Samples"));
    }
    auto xml = v.createXml();
    if (xml == nullptr || ! xml->writeTo (f)) return false;
    {
        const juce::ScopedLock sl (lock);
        proj->file = f;
        proj->name = f.getFileNameWithoutExtension();
    }
    dirty = false;
    auto recent = settings.getList ("recentProjects");
    recent.removeString (f.getFullPathName());
    recent.insert (0, f.getFullPathName());
    while (recent.size() > 12) recent.remove (12);
    settings.setList ("recentProjects", recent);
    settings.set ("lastProject", f.getFullPathName());
    sendChangeMessage();
    return true;
}

juce::ValueTree Engine::saveGroup (int gi)
{
    const juce::ScopedLock sl (lock);
    if (auto* g = proj->group (gi)) return writeGroup (*g, false);
    return {};
}

void Engine::loadGroupInto (int gi, const juce::ValueTree& v)
{
    if (! v.hasType ("GROUP")) return;
    auto g = std::make_unique<Group>();
    juce::StringArray errors;
    readGroup (*this, v, *g, nullptr, errors);
    std::unique_ptr<Group> old;
    {
        const juce::ScopedLock sl (lock);
        if (gi >= (int) proj->groups.size()) { proj->groups.push_back (std::move (g)); gi = (int) proj->groups.size() - 1; }
        else { old = std::move (proj->groups[(size_t) gi]); proj->groups[(size_t) gi] = std::move (g); }
        prepareAll();
        ensureSceneSlots();
    }
    if (old != nullptr) { closeEditorsFor (old->slots); for (auto& s : old->sounds) closeEditorsFor (s->slots); }
    focusedGroup = gi;
    sendChangeMessage();
}

juce::ValueTree Engine::saveSound (int gi, int si)
{
    const juce::ScopedLock sl (lock);
    if (! juce::isPositiveAndBelow (si, numPads)) return {};
    if (auto* g = proj->group (gi)) return writeSound (*g->sounds[(size_t) si], si, false);
    return {};
}

void Engine::loadSoundInto (int gi, int si, const juce::ValueTree& v)
{
    if (! v.hasType ("SOUND") || ! juce::isPositiveAndBelow (si, numPads)) return;
    auto s = std::make_unique<Sound>();
    juce::StringArray errors;
    readSound (*this, v, *s, nullptr, errors);
    std::unique_ptr<Sound> old;
    {
        const juce::ScopedLock sl (lock);
        auto* g = proj->group (gi);
        if (g == nullptr) return;
        old = std::move (g->sounds[(size_t) si]);
        g->sounds[(size_t) si] = std::move (s);
        prepareAll();
    }
    if (old) closeEditorsFor (old->slots);
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
void Engine::snapshot (const juce::String& what)
{
    undoStack.push_back ({ what, toValueTree (true) });
    if (undoStack.size() > 80) undoStack.erase (undoStack.begin());
    redoStack.clear();
    dirty = true;
}

void Engine::undo()
{
    if (undoStack.empty() || exporting) return;
    auto [name, tree] = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ name, toValueTree (true) });
    restoreSnapshot (tree);
    if (onStatus) onStatus ("Undo: " + name);
}

void Engine::redo()
{
    if (redoStack.empty() || exporting) return;
    auto [name, tree] = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ name, toValueTree (true) });
    restoreSnapshot (tree);
    if (onStatus) onStatus ("Redo: " + name);
}

// ---------------------------------------------------------------------------------------------
// Sampling
void Engine::startSampling()
{
    {
        const juce::ScopedLock sl (lock);
        recBuffer.setSize (2, (int) (sampleRate * (rec.length < 5 ? juce::jmax (8.0, (1 << rec.length) * proj->ticksPerBar() / (double) PPQ * 60.0 / proj->bpm + 1.0) : 180.0)));
        recBuffer.clear();
        recWritePos = 0;
    }
    recState = RecState::waiting;
    if ((rec.mode == 1 || rec.mode == 2) && ! playing) play();
}

void Engine::cancelSampling() { recState = RecState::idle; }

juce::File Engine::finishSampling()
{
    juce::AudioBuffer<float> take;
    {
        const juce::ScopedLock sl (lock);
        int len = recState == RecState::done || rec.mode != 2 ? recWritePos : recTargetLen;
        if (len <= 0) { recState = RecState::idle; return {}; }
        take.setSize (2, len);
        for (int c = 0; c < 2; ++c) take.copyFrom (c, 0, recBuffer, c, 0, len);
        recState = RecState::idle;
    }
    auto folder = Settings::userFolder().getChildFile ("Recordings");
    auto f = folder.getNonexistentChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".wav");
    juce::WavAudioFormat wav;
    if (auto os = f.createOutputStream())
    {
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, 2, 24, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (take, 0, take.getNumSamples()); }
    }
    auto* g = proj->group (focusedGroup);
    if (g != nullptr && f.existsAsFile())
    {
        snapshot ("Sampling");
        loadSampleIntoSound (*g->sounds[(size_t) g->focusedSound], f);
        sendChangeMessage();
    }
    return f;
}

// ---------------------------------------------------------------------------------------------
// Export
bool Engine::exportAudio (const ExportOptions& o, std::function<bool (float)> progress, juce::String& error)
{
    // Runs on the ExportJob thread while the message thread stays live: block project replacement
    // (undo / redo / load, also reachable via MIDI-learned actions) for the duration.
    exporting = true;
    struct ExportFlagReset { std::atomic<bool>& f; ~ExportFlagReset() { f = false; } } exportFlagReset { exporting };
    deviceManager.removeAudioCallback (this);
    bool savedPlaying = playing, savedRec = recording, savedMetro = metronomeOn;
    double savedPos = position;
    recording = false; metronomeOn = false; countingIn = false;

    struct Pass { juce::String name; int group = -1, sound = -1; };
    std::vector<Pass> passes;
    if (o.stems == 0) passes.push_back ({ "", -1, -1 });
    else if (o.stems == 1) for (int gi = 0; gi < (int) proj->groups.size(); ++gi) passes.push_back ({ proj->groups[(size_t) gi]->name, gi, -1 });
    else
    {
        int fg = juce::jlimit (0, (int) proj->groups.size() - 1, focusedGroup.load());
        for (int si = 0; si < numPads; ++si)
            if (! proj->groups[(size_t) fg]->sounds[(size_t) si]->slots.empty()) passes.push_back ({ proj->groups[(size_t) fg]->sounds[(size_t) si]->name, fg, si });
    }

    bool savedSong = songMode;
    double lengthTicks;
    if (o.range == 0 && proj->songLength() > 0) { songMode = true; lengthTicks = (double) proj->songLength(); }
    else if (o.range == 0) { songMode = false; lengthTicks = displayLoopLength(); }
    else { songMode = o.range == 2; lengthTicks = (double) o.bars * proj->ticksPerBar(); if (o.range == 1) songMode = false; }
    bool savedLoop = proj->loopOn; proj->loopOn = false;

    const double spt = sampleRate * 60.0 / (proj->bpm * PPQ);
    const int total = (int) (lengthTicks * spt + o.tail * sampleRate);
    bool ok = true;
    for (size_t pi = 0; pi < passes.size() && ok; ++pi)
    {
        auto& pass = passes[pi];
        // mute everything except the stem (under the lock: the GUI thread reads / toggles these meanwhile)
        std::vector<std::pair<bool*, bool>> savedMutes;
        {
            const juce::ScopedLock sl (lock);
            for (int gi = 0; gi < (int) proj->groups.size(); ++gi)
            {
                auto& g = *proj->groups[(size_t) gi];
                savedMutes.push_back ({ &g.mute, g.mute });
                if (pass.group >= 0 && gi != pass.group) g.mute = true;
                for (int si = 0; si < numPads; ++si)
                {
                    auto& s = *g.sounds[(size_t) si];
                    savedMutes.push_back ({ &s.mute, s.mute });
                    if (pass.sound >= 0 && gi == pass.group && si != pass.sound) s.mute = true;
                }
            }
        }
        {
            const juce::ScopedLock sl (lock);
            auto resetChain = [] (Chain& c) { for (auto& s : c) if (s->proc) s->proc->reset(); };
            resetChain (proj->masterSlots);
            for (auto& g : proj->groups) { resetChain (g->slots); for (auto& s : g->sounds) resetChain (s->slots); }
        }
        juce::AudioBuffer<float> out (2, juce::jmax (1, total));
        out.clear();
        position = 0.0; playing = true;
        int pos = 0;
        while (pos < total)
        {
            int n = juce::jmin (blockSize, total - pos);
            if ((double) pos > lengthTicks * spt) playing = false;
            {
                const juce::ScopedLock sl (lock);
                processBlockInternal (nullptr, 0, n);
                out.copyFrom (0, pos, masterBuf, 0, 0, n);
                out.copyFrom (1, pos, masterBuf, 1, 0, n);
            }
            pos += n;
            if (progress && ! progress (((float) pi + (float) pos / (float) total) / (float) passes.size())) { ok = false; error = "Cancelled"; break; }
        }
        playing = false;
        {
            const juce::ScopedLock sl (lock);
            for (auto& [ptr, val] : savedMutes) *ptr = val;
        }
        if (! ok) break;

        if (o.normalize) { auto mx = out.getMagnitude (0, out.getNumSamples()); if (mx > 0.0001f) out.applyGain (0.98f / mx); }
        auto file = pass.name.isEmpty() ? o.file : o.file.getSiblingFile (o.file.getFileNameWithoutExtension() + " - " + juce::File::createLegalFileName (pass.name) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        auto os = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (os ? wav.createWriterFor (os.get(), sampleRate, 2, o.bitDepth, {}, 0) : nullptr);
        if (w == nullptr) { ok = false; error = "Could not write " + file.getFullPathName(); break; }
        os.release();
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }

    proj->loopOn = savedLoop;
    songMode = savedSong;
    position = savedPos; playing = false; recording = savedRec; metronomeOn = savedMetro;
    juce::ignoreUnused (savedPlaying);
    deviceManager.addAudioCallback (this);
    return ok;
}
} // namespace gb
