#pragma once
#include "Widgets.h"

// Shared menus and edit actions used by several panels (group / sound / pattern menus, rename dialog).
namespace gb::actions
{
inline void askText (const juce::String& title, const juce::String& current, std::function<void (juce::String)> apply)
{
    auto* aw = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("v", current);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, apply] (int r)
    {
        if (r == 1) { auto t = aw->getTextEditorContents ("v").trim(); if (t.isNotEmpty()) apply (t); }
    }), true);
}

inline juce::PopupMenu colourMenu (int baseId, juce::Colour current)
{
    juce::PopupMenu m;
    for (int i = 0; i < 16; ++i)
    {
        juce::PopupMenu::Item it ("Colour " + juce::String (i + 1));
        it.itemID = baseId + i;
        it.colour = paletteColour (i);
        it.isTicked = paletteColour (i) == current;
        m.addItem (it);
    }
    return m;
}

inline std::shared_ptr<juce::FileChooser>& chooserHolder() { static std::shared_ptr<juce::FileChooser> fc; return fc; }

inline void chooseFile (const juce::String& title, const juce::File& start, const juce::String& patterns, bool save, std::function<void (juce::File)> cb)
{
    auto fc = std::make_shared<juce::FileChooser> (title, start, patterns);
    chooserHolder() = fc;
    int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting) : juce::FileBrowserComponent::openMode;
    fc->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles, [cb] (const juce::FileChooser& c)
    {
        auto f = c.getResult();
        if (f != juce::File()) cb (f);
    });
}

inline juce::String audioWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"; }

// ---------------------------------------------------------------------------------------------
inline void loadFileFromBrowser (Engine& engine, Ui& ui, const juce::File& f, int gi, int si);
inline void exportPatternMidi (Engine& engine, int gi, const juce::File& f);
inline juce::ValueTree& groupClipboard() { static juce::ValueTree v; return v; }

inline void writeGroupFile (Engine& engine, int gi, juce::File f, bool withSamples)
{
    auto* g = engine.project().group (gi);
    if (g == nullptr) return;
    f = f.withFileExtension ("gbg");
    auto v = engine.saveGroup (gi);
    if (withSamples) engine.embedSamples (v, f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " Samples"));
    if (auto x = v.createXml()) if (x->writeTo (f)) g->file = f;
}

// Right-click on a group: the same menu as the original
inline void groupMenu (Engine& engine, Ui& ui, int gi, juce::Component* target)
{
    auto* g = engine.project().group (gi);
    if (g == nullptr) return;
    juce::PopupMenu m, batch, inCh, notes;
    m.addItem (1, "Open...");
    m.addItem (2, "Save");
    m.addItem (3, "Save As...");
    m.addItem (4, "Save with Samples...");
    m.addSeparator();
    m.addItem (5, "Import MIDI...");
    m.addItem (6, "Export MIDI...", engine.editPattern (gi) != nullptr);
    batch.addItem (30, "Sounds: MIDI Channels 1-16 (Sound n receives on channel n)");
    batch.addItem (31, "Sounds: All MIDI Channels");
    for (int o = 0; o <= 4; ++o) notes.addItem (40 + o, "Starting at C" + juce::String (o) + " (" + juce::String (24 + o * 12) + ")");
    batch.addSubMenu ("Sounds: Trigger Notes (Drumkit)", notes);
    batch.addItem (32, "Sounds: Default Trigger Notes");
    batch.addSeparator();
    batch.addItem (33, "Key Mode: Off", true, g->routing.keyMode == 0);
    batch.addItem (34, "Key Mode: Drumkit", true, g->routing.keyMode == 1);
    m.addSubMenu ("Group MIDI Batch Setup", batch);
    m.addSeparator();
    m.addSubMenu ("Color", colourMenu (100, g->colour));
    m.addSeparator();
    m.addItem (7, "Cut");
    m.addItem (8, "Copy");
    m.addItem (9, "Paste", groupClipboard().isValid());
    m.addSeparator();
    m.addItem (10, "Rename");
    m.addItem (11, "Delete", engine.project().groups.size() > 1);

    auto deleteGroup = [&engine] (int index)
    {
        std::unique_ptr<Group> old;
        {
            const juce::ScopedLock sl (engine.lock);
            auto& P = engine.project();
            old = std::move (P.groups[(size_t) index]);
            P.groups.erase (P.groups.begin() + index);
            for (auto& sc : P.scenes) if (index < (int) sc.patternForGroup.size()) sc.patternForGroup.erase (sc.patternForGroup.begin() + index);
            engine.focusedGroup = juce::jlimit (0, (int) P.groups.size() - 1, engine.focusedGroup.load());
        }
        engine.closeEditorsFor (old->slots);
        for (auto& s : old->sounds) engine.closeEditorsFor (s->slots);
    };
    auto clearGroup = [&engine] (int index)
    {
        auto* gg = engine.project().group (index);
        juce::ValueTree v ("GROUP");
        v.setProperty ("name", gg->name, nullptr);
        v.setProperty ("colour", gg->colour.toString(), nullptr);
        engine.loadGroupInto (index, v);
        gg = engine.project().group (index);
        for (int si = 0; si < numPads; ++si) { gg->sounds[(size_t) si]->name = "Sound " + juce::String (si + 1); gg->sounds[(size_t) si]->colour = gg->colour; }
    };

    juce::ignoreUnused (target);
    showMenuAt (m, juce::Desktop::getMousePosition(), [&engine, &ui, gi, deleteGroup, clearGroup] (int r)
    {
        auto* g = engine.project().group (gi);
        if (g == nullptr) return;
        auto groupsDir = Settings::userFolder().getChildFile ("Groups");
        groupsDir.createDirectory();
        if (r == 1)
            chooseFile ("Open Group", groupsDir, "*.gbg", false, [&engine, &ui, gi] (juce::File f)
            {
                if (auto x = juce::XmlDocument::parse (f))
                {
                    engine.snapshot ("Open Group");
                    engine.loadGroupInto (gi, juce::ValueTree::fromXml (*x));
                    if (auto* gg = engine.project().group (gi)) gg->file = f;
                    ui.changed();
                }
            });
        else if (r == 2 && g->file != juce::File()) writeGroupFile (engine, gi, g->file, false);
        else if (r == 2 || r == 3 || r == 4)
        {
            bool withSamples = r == 4;
            auto start = g->file != juce::File() ? g->file : groupsDir.getChildFile (g->name + ".gbg");
            chooseFile (withSamples ? "Save Group with Samples" : "Save Group As", start, "*.gbg", true,
                        [&engine, gi, withSamples] (juce::File f) { writeGroupFile (engine, gi, f, withSamples); });
        }
        else if (r == 5)
            chooseFile ("Import MIDI", Settings::userFolder(), "*.mid;*.midi", false, [&engine, &ui, gi] (juce::File f)
            {
                auto* gg = engine.project().group (gi);
                loadFileFromBrowser (engine, ui, f, gi, gg ? gg->focusedSound : 0);
            });
        else if (r == 6)
        {
            auto name = g->name;
            if (auto* p = engine.editPattern (gi)) name += " - " + p->name;
            chooseFile ("Export MIDI", Settings::userFolder().getChildFile (name + ".mid"), "*.mid", true,
                        [&engine, gi] (juce::File f) { exportPatternMidi (engine, gi, f.withFileExtension ("mid")); });
        }
        else if (r >= 30 && r < 50)
        {
            engine.snapshot ("Group MIDI Batch Setup");
            const juce::ScopedLock sl (engine.lock);
            for (int si = 0; si < numPads; ++si)
            {
                auto& s = *g->sounds[(size_t) si];
                if (r == 30) { s.routing.midiSource = 3; s.routing.midiChannel = si + 1; }
                if (r == 31) s.routing.midiChannel = 0;
                if (r == 32) s.triggerNote = -1;
                if (r >= 40) s.triggerNote = 24 + (r - 40) * 12 + si;
            }
            if (r == 33) g->routing.keyMode = 0;
            if (r == 34 || r >= 40) g->routing.keyMode = 1;
        }
        else if (r >= 100 && r < 116)
        {
            engine.snapshot ("Group Colour");
            g->colour = paletteColour (r - 100);
            for (auto& s : g->sounds) s->colour = g->colour;
            for (auto& p : g->patterns) p->colour = g->colour;
        }
        else if (r == 7 || r == 8)
        {
            groupClipboard() = engine.saveGroup (gi).createCopy();
            if (r == 7)
            {
                engine.snapshot ("Cut Group");
                if (engine.project().groups.size() > 1) deleteGroup (gi); else clearGroup (gi);
            }
        }
        else if (r == 9 && groupClipboard().isValid())
        {
            engine.snapshot ("Paste Group");
            engine.loadGroupInto (gi, groupClipboard().createCopy());
        }
        else if (r == 10)
        {
            askText ("Rename Group", g->name, [&engine, &ui, gi] (juce::String t) { if (auto* gg = engine.project().group (gi)) { engine.snapshot ("Rename Group"); gg->name = t; ui.changed(); } });
            return;
        }
        else if (r == 11)
        {
            engine.snapshot ("Delete Group");
            deleteGroup (gi);
        }
        ui.changed();
    });
}

inline void soundMenu (Engine& engine, Ui& ui, int gi, int si, juce::Component* target)
{
    auto* g = engine.project().group (gi);
    if (g == nullptr || ! juce::isPositiveAndBelow (si, numPads)) return;
    auto& s = *g->sounds[(size_t) si];
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addSubMenu ("Colour", colourMenu (100, s.colour));
    m.addSeparator();
    m.addItem (2, "Load Sample...");
    m.addItem (3, "Load Sound...");
    m.addItem (4, "Save Sound As...");
    m.addSeparator();
    m.addItem (5, "Copy");
    m.addItem (6, "Paste", juce::SystemClipboard::getTextFromClipboard().trimStart().startsWith ("<SOUND"));
    m.addItem (7, "Duplicate to next empty pad");
    m.addItem (8, "Clear");
    m.addSeparator();
    m.addItem (9, s.mute ? "Unmute" : "Mute");
    m.addItem (10, s.solo ? "Unsolo" : "Solo");
    m.addItem (11, "Remove notes from pattern");
    showMenu (m, target, [&engine, &ui, gi, si] (int r)
    {
        auto* g = engine.project().group (gi);
        if (g == nullptr) return;
        auto& s = *g->sounds[(size_t) si];
        if (r == 1) askText ("Rename Sound", s.name, [&engine, &ui, gi, si] (juce::String t) { if (auto* gg = engine.project().group (gi)) { engine.snapshot ("Rename Sound"); gg->sounds[(size_t) si]->name = t; ui.changed(); } });
        else if (r >= 100 && r < 116) { engine.snapshot ("Sound Colour"); s.colour = paletteColour (r - 100); }
        else if (r == 2)
            chooseFile ("Load Sample", Settings::userFolder().getChildFile ("Samples"), audioWildcard(), false, [&engine, &ui, gi, si] (juce::File f)
            {
                auto* gg = engine.project().group (gi);   // async: the group may be gone by now
                if (gg == nullptr) return;
                engine.snapshot ("Load Sample");
                engine.loadSampleIntoSound (*gg->sounds[(size_t) si], f);
                ui.changed();
            });
        else if (r == 3)
            chooseFile ("Load Sound", Settings::userFolder().getChildFile ("Sounds"), "*.gbs", false, [&engine, &ui, gi, si] (juce::File f)
            {
                if (auto x = juce::XmlDocument::parse (f)) { engine.snapshot ("Load Sound"); engine.loadSoundInto (gi, si, juce::ValueTree::fromXml (*x)); ui.changed(); }
            });
        else if (r == 4)
            chooseFile ("Save Sound", Settings::userFolder().getChildFile ("Sounds").getChildFile (s.name + ".gbs"), "*.gbs", true, [&engine, gi, si] (juce::File f)
            {
                if (auto x = engine.saveSound (gi, si).createXml()) x->writeTo (f.withFileExtension ("gbs"));
            });
        else if (r == 5) { if (auto x = engine.saveSound (gi, si).createXml()) juce::SystemClipboard::copyTextToClipboard (x->toString()); }
        else if (r == 6)
        {
            if (auto x = juce::XmlDocument::parse (juce::SystemClipboard::getTextFromClipboard()))
            { engine.snapshot ("Paste Sound"); engine.loadSoundInto (gi, si, juce::ValueTree::fromXml (*x)); }
        }
        else if (r == 7)
        {
            for (int t = 0; t < numPads; ++t)
                if (g->sounds[(size_t) t]->slots.empty() && t != si)
                {
                    engine.snapshot ("Duplicate Sound");
                    engine.loadSoundInto (gi, t, engine.saveSound (gi, si));
                    engine.project().group (gi)->sounds[(size_t) t]->name = s.name + " Copy";
                    break;
                }
        }
        else if (r == 8)
        {
            engine.snapshot ("Clear Sound");
            juce::ValueTree v ("SOUND");
            v.setProperty ("name", "Sound " + juce::String (si + 1), nullptr);
            v.setProperty ("colour", g->colour.toString(), nullptr);
            engine.loadSoundInto (gi, si, v);
        }
        else if (r == 9) s.mute = ! s.mute;
        else if (r == 10) s.solo = ! s.solo;
        else if (r == 11)
        {
            if (auto* p = engine.editPattern (gi))
            {
                engine.snapshot ("Remove Notes");
                const juce::ScopedLock sl (engine.lock);
                p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(), [si] (auto& n) { return n.sound == si; }), p->notes.end());
            }
        }
        ui.changed();
    });
}

// Loads a file dropped / double-clicked from the browser into the focused group/sound
inline void loadFileFromBrowser (Engine& engine, Ui& ui, const juce::File& f, int gi, int si)
{
    auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".gbp") { engine.loadProject (f); ui.changed(); return; }
    if (ext == ".gbg") { if (auto x = juce::XmlDocument::parse (f)) { engine.snapshot ("Load Group"); engine.loadGroupInto (gi, juce::ValueTree::fromXml (*x)); } ui.changed(); return; }
    if (ext == ".gbs") { if (auto x = juce::XmlDocument::parse (f)) { engine.snapshot ("Load Sound"); engine.loadSoundInto (gi, si, juce::ValueTree::fromXml (*x)); } ui.changed(); return; }
    if (ext == ".mid" || ext == ".midi")
    {
        juce::FileInputStream in (f);
        juce::MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) return;
        mf.convertTimestampTicksToSeconds();
        auto* g = engine.project().group (gi);
        if (g == nullptr) return;
        engine.snapshot ("Import MIDI");
        auto tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 96;
        juce::ignoreUnused (tpq);
        juce::MidiMessageSequence all;
        for (int t = 0; t < mf.getNumTracks(); ++t) all.addSequence (*mf.getTrack (t), 0.0);
        all.updateMatchedPairs();
        double bpm = engine.project().bpm;
        auto secToTick = [bpm] (double s) { return (juce::int64) (s * bpm / 60.0 * PPQ); };
        juce::int64 end = 0;
        std::vector<NoteEvent> notes;
        for (auto* ev : all)
        {
            if (! ev->message.isNoteOn()) continue;
            int nn = ev->message.getNoteNumber();
            NoteEvent ne;
            int padIdx = nn - engine.padBase.load();
            bool drum = padIdx >= 0 && padIdx < numPads;
            ne.sound = drum ? padIdx : g->focusedSound;
            ne.pitch = drum ? -1 : nn;
            ne.start = secToTick (ev->message.getTimeStamp());
            ne.length = ev->noteOffObject ? std::max<juce::int64> (30, secToTick (ev->noteOffObject->message.getTimeStamp()) - ne.start) : PPQ / 4;
            ne.velocity = ev->message.getFloatVelocity();
            end = juce::jmax (end, ne.start + ne.length);
            notes.push_back (ne);
        }
        auto bar = engine.project().ticksPerBar();
        auto len = std::max<juce::int64> (bar, ((end + bar - 1) / bar) * bar);
        auto* p = engine.newPatternFor (*g, len);
        const juce::ScopedLock sl (engine.lock);
        p->name = f.getFileNameWithoutExtension();
        p->notes = notes;
        int pi = (int) g->patterns.size() - 1;
        if (engine.songMode)
        {
            juce::int64 start = 0;
            for (auto& c : g->clips) start = juce::jmax (start, c.start + c.length);
            g->clips.push_back ({ start, len, pi });
            engine.setEditClip (gi, (int) g->clips.size() - 1);
        }
        else
        {
            engine.ensureSceneSlots();
            engine.project().scenes[(size_t) engine.project().currentScene].patternForGroup[(size_t) gi] = pi;
        }
        ui.changed();
        return;
    }
    if (SampleCache::get().formats().findFormatForFileExtension (ext) != nullptr)
    {
        auto* g = engine.project().group (gi);
        if (g == nullptr) return;
        engine.snapshot ("Load Sample");
        engine.loadSampleIntoSound (*g->sounds[(size_t) si], f);
        ui.changed();
    }
}

// Export the current pattern as a MIDI file
inline void exportPatternMidi (Engine& engine, int gi, const juce::File& f)
{
    auto* g = engine.project().group (gi);
    auto* p = engine.editPattern (gi);
    if (g == nullptr || p == nullptr) return;
    juce::MidiMessageSequence seq;
    for (auto& n : p->notes)
    {
        int note = n.pitch >= 0 ? n.pitch : engine.padBase.load() + n.sound;
        seq.addEvent (juce::MidiMessage::noteOn (1, note, n.velocity), (double) n.start);
        seq.addEvent (juce::MidiMessage::noteOff (1, note), (double) (n.start + n.length));
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (PPQ);
    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / engine.project().bpm)), 0);
    mf.addTrack (meta);
    mf.addTrack (seq);
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.openedOk()) mf.writeTo (out);
}
} // namespace gb::actions
