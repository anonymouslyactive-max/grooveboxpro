#pragma once
#include "Actions.h"

namespace gb
{
// Background indexer for the Library tab
class LibraryIndex : public juce::Thread, public juce::ChangeBroadcaster
{
public:
    struct Entry { juce::File file; int category = 0; juce::String type; double seconds = 0; };
    enum Cat { projects, groups, sounds, instruments, effects, loops, oneshots };

    explicit LibraryIndex (Settings& s) : Thread ("Library Index"), settings (s) {}
    ~LibraryIndex() override { stopThread (4000); }

    juce::StringArray folders()
    {
        auto f = settings.getList ("libraryFolders");
        auto u = Settings::userFolder();
        for (auto sub : { "Samples", "Recordings" }) if (! f.contains (u.getChildFile (sub).getFullPathName())) f.add (u.getChildFile (sub).getFullPathName());
        return f;
    }

    void rescan() { stopThread (2000); startThread(); }

    std::vector<Entry> snapshot() { const juce::ScopedLock sl (lock); return entries; }
    std::atomic<bool> scanning { false };

private:
    Settings& settings;
    juce::CriticalSection lock;
    std::vector<Entry> entries;

    void run() override
    {
        scanning = true;
        std::vector<Entry> out;
        auto u = Settings::userFolder();
        // Iterate lazily so threadShouldExit() is polled per file: File::findChildFiles (recursive) can run for
        // minutes on a big drive, and stopThread() then forcibly kills the thread (TerminateThread on Windows).
        auto forEachFile = [this] (const juce::File& dir, const juce::String& pattern, const std::function<void (const juce::File&)>& fn)
        {
            for (const auto& entry : juce::RangedDirectoryIterator (dir, true, pattern, juce::File::findFiles))
            {
                if (threadShouldExit()) return false;
                fn (entry.getFile());
            }
            return ! threadShouldExit();
        };
        auto addAll = [&] (const juce::File& dir, const juce::String& pattern, int cat)
        {
            forEachFile (dir, pattern, [&] (const juce::File& f) { out.push_back ({ f, cat, f.getParentDirectory().getFileName(), 0 }); });
        };
        addAll (u.getChildFile ("Projects"), "*.gbp", projects);
        for (auto& r : settings.getList ("recentProjects"))
        {
            juce::File f (r);
            bool dup = false;
            for (auto& e : out) if (e.file == f) dup = true;
            if (! dup && f.existsAsFile()) out.push_back ({ f, projects, "Recent", 0 });
        }
        addAll (u.getChildFile ("Groups"), "*.gbg", groups);
        addAll (u.getChildFile ("Sounds"), "*.gbs", sounds);
        auto& fm = SampleCache::get().formats();
        for (auto& folder : folders())
        {
            juce::File dir (folder);
            if (! dir.isDirectory()) continue;
            bool completed = forEachFile (dir, actions::audioWildcard(), [&] (const juce::File& f)
            {
                double secs = 0;
                if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (f) }) secs = (double) r->lengthInSamples / juce::jmax (1.0, r->sampleRate);
                auto name = f.getFileNameWithoutExtension().toLowerCase();
                bool loop = name.contains ("loop") || name.contains ("bpm") || secs >= 1.6;
                auto type = f.getParentDirectory() == dir ? juce::String ("Other") : f.getParentDirectory().getFileName();
                out.push_back ({ f, loop ? loops : oneshots, type, secs });
            });
            if (! completed) { scanning = false; return; }
            addAll (dir, "*.gbg", groups);
            addAll (dir, "*.gbs", sounds);
        }
        if (threadShouldExit()) { scanning = false; return; }
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.file.getFileName().compareNatural (b.file.getFileName()) < 0; });
        { const juce::ScopedLock sl (lock); entries = std::move (out); }
        scanning = false;
        sendChangeMessage();
    }
};

class BrowserPanel : public juce::Component, public juce::ListBoxModel, public juce::DragAndDropContainer,
                     private juce::ChangeListener, private juce::TextEditor::Listener
{
public:
    BrowserPanel (Engine& e, Ui& u) : engine (e), ui (u), index (e.settings)
    {
        libTab.style = filesTab.style = FlatButton::tab;
        libTab.fontH = filesTab.fontH = 13.5f;
        libTab.onClick = [this] { filesMode = false; refresh(); };
        filesTab.onClick = [this] { filesMode = true; refresh(); };
        addAndMakeVisible (libTab); addAndMakeVisible (filesTab);
        const icons::Id catIcons[] = { icons::projectIcon, icons::groupIcon, icons::soundIcon, icons::instrumentIcon, icons::fxIcon, icons::loopIcon, icons::oneshotIcon };
        const char* tips[] = { "Projects", "Groups", "Sounds", "Instruments", "Effects", "Loops", "One-shots" };
        for (int i = 0; i < 7; ++i)
        {
            auto* b = catBtns.add (new IconButton (catIcons[i], tips[i]));
            b->onClick = [this, i] { category = i; typeFilter.clear(); refresh(); };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (userBtn);
        userBtn.on = col::orange;
        userBtn.onClick = [this] { userOnly = ! userOnly; refresh(); };
        userBtn.setTooltip ("Only show favourites / user content");
        addAndMakeVisible (eyeBtn);
        eyeBtn.on = juce::Colours::white;
        eyeBtn.setToggleState (true, juce::dontSendNotification);
        eyeBtn.onClick = [this] { previewOn = ! previewOn; eyeBtn.setToggleState (previewOn, juce::dontSendNotification); if (! previewOn) engine.stopPreview(); };
        eyeBtn.setTooltip ("Prehear samples");
        addAndMakeVisible (search);
        search.setTextToShowWhenEmpty ("", col::textDark);
        search.setColour (juce::TextEditor::backgroundColourId, col::header);
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setIndents (26, 6);
        search.addListener (this);
        addAndMakeVisible (clearBtn); addAndMakeVisible (starBtn);
        clearBtn.onClick = [this] { search.clear(); refresh(); };
        starBtn.on = col::orange;
        starBtn.onClick = [this] { favOnly = ! favOnly; starBtn.setToggleState (favOnly, juce::dontSendNotification); refresh(); };
        starBtn.setTooltip ("Favourites");
        addAndMakeVisible (typesBtn);
        typesBtn.just = juce::Justification::centredLeft;
        typesBtn.onClick = [this] { typesMenu(); };
        addAndMakeVisible (updateBtn);
        updateBtn.onClick = [this] { index.rescan(); };
        updateBtn.setTooltip ("Rescan library folders");

        addAndMakeVisible (list);
        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, col::header);
        list.setMultipleSelectionEnabled (false);

        addAndMakeVisible (importBtn);
        importBtn.style = FlatButton::plain;
        importBtn.onClick = [this]
        {
            auto f = engine.settings.getList ("libraryFolders");
            if (! f.contains (currentDir.getFullPathName())) { f.add (currentDir.getFullPathName()); engine.settings.setList ("libraryFolders", f); }
            index.rescan();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Library", "Added to the library:\n" + currentDir.getFullPathName());
        };
        importBtn.setTooltip ("Add this folder to the Library");
        addAndMakeVisible (upBtn);
        upBtn.onClick = [this] { if (currentDir.getParentDirectory() != currentDir) { currentDir = currentDir.getParentDirectory(); refresh(); } };
        addAndMakeVisible (volume);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0, 1);
        volume.setValue (0.8);
        volume.onValueChange = [this] { engine.previewGain = (float) volume.getValue(); };
        volume.setTooltip ("Prehear volume");

        currentDir = juce::File (engine.settings.getString ("browserDir", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getFullPathName()));
        if (! currentDir.isDirectory()) currentDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        index.addChangeListener (this);
        index.rescan();
        refresh();
    }
    ~BrowserPanel() override { index.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::header);
        g.setColour (juce::Colours::black);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
        if (! filesMode)
        {
            g.setColour (col::text);
            g.setFont (font (19.0f));
            const char* names[] = { "All Projects", "All Groups", "All Sounds", "All Instruments", "All Effects", "All Loops", "All One-shots" };
            g.drawText (names[category], 16, 82, getWidth() - 60, 32, juce::Justification::centredLeft);
        }
        else
        {
            // drive / favourite buttons and path
            g.setColour (col::textDim);
            g.setFont (font (11.5f));
            int x = 12, y = 34;
            for (auto& [label, f] : quickLinks())
            {
                int w = (int) font (11.5f).getStringWidthFloat (label) + 12;
                if (x + w > getWidth() - 8) { x = 12; y += 18; }
                g.setColour (f == currentDir ? juce::Colours::white : col::textDim);
                g.drawText (label, x, y, w, 16, juce::Justification::centredLeft);
                x += w;
            }
            g.setColour (col::text);
            g.setFont (font (12.0f));
            g.drawText (currentDir.getFullPathName(), 34, 112, getWidth() - 46, 18, juce::Justification::centredLeft, true);
        }
        icons::draw (g, icons::search, { (float) search.getX() + 6, (float) search.getY() + 5, 14.0f, 14.0f }, col::textDim);
        g.setColour (col::textDark);
        g.fillRect (search.getX(), search.getBottom(), search.getWidth() + 40, 1);
        if (index.scanning) { g.setColour (col::textDark); g.setFont (font (10.0f)); g.drawText ("Scanning...", getWidth() - 90, getHeight() - 22, 80, 16, juce::Justification::centredRight); }
        // footer
        g.setColour (juce::Colours::black);
        g.fillRect (0, getHeight() - 26, getWidth(), 1);
        icons::draw (g, icons::speaker, { 30.0f, (float) getHeight() - 21, 14.0f, 14.0f }, col::textDim);
    }

    void resized() override
    {
        libTab.setBounds (12, 6, 56, 24);
        filesTab.setBounds (72, 6, 48, 24);
        int x = 12;
        for (auto* b : catBtns) { b->setBounds (x, 40, 28, 28); b->setVisible (! filesMode); x += 30; }
        userBtn.setBounds (getWidth() - 40, 40, 28, 28);
        userBtn.setVisible (! filesMode);
        eyeBtn.setBounds (getWidth() - 40, 84, 26, 26);
        eyeBtn.setVisible (! filesMode);
        upBtn.setBounds (8, 110, 22, 22);
        upBtn.setVisible (filesMode);
        int sy = filesMode ? 138 : 132;
        search.setBounds (12, sy, getWidth() - 70, 24);
        clearBtn.setBounds (getWidth() - 56, sy + 2, 20, 20);
        starBtn.setBounds (getWidth() - 34, sy + 2, 20, 20);
        typesBtn.setBounds (12, sy + 30, 140, 18);
        updateBtn.setBounds (getWidth() - 70, sy + 30, 60, 18);
        bool showTypes = ! filesMode && category >= LibraryIndex::loops;
        typesBtn.setVisible (showTypes); updateBtn.setVisible (! filesMode);
        int ly = sy + (showTypes || ! filesMode ? 52 : 30);
        list.setBounds (8, ly, getWidth() - 16, getHeight() - ly - 30);
        volume.setBounds (48, getHeight() - 22, 70, 18);
        importBtn.setBounds (getWidth() - 80, getHeight() - 24, 70, 22);
        importBtn.setVisible (filesMode);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! filesMode || e.y < 32 || e.y > 108) return;
        int x = 12, y = 34;
        for (auto& [label, f] : quickLinks())
        {
            int w = (int) font (11.5f).getStringWidthFloat (label) + 12;
            if (x + w > getWidth() - 8) { x = 12; y += 18; }
            if (juce::Rectangle<int> (x, y, w, 16).contains (e.getPosition()))
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu m; m.addItem (1, "Remove from Favourites");
                    auto file = f;
                    showMenu (m, this, [this, file] (int) { auto fav = engine.settings.getList ("favFolders"); fav.removeString (file.getFullPathName()); engine.settings.setList ("favFolders", fav); repaint(); });
                }
                else { currentDir = f; refresh(); }
                return;
            }
            x += w;
        }
    }

    // ---------------------------------------------------------------- ListBoxModel
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        auto& r = rows[(size_t) row];
        if (selected) { g.setColour (col::buttonHi); g.fillRect (0, 0, w, h); }
        int x = 6;
        if (filesMode)
        {
            icons::draw (g, r.isDir ? icons::folder : (r.audio ? icons::audioFile : icons::file), { 6.0f, 5.0f, 14.0f, 14.0f }, r.isDir ? col::textDim : col::text);
            x = 26;
        }
        g.setColour (selected ? juce::Colours::white : col::text);
        g.setFont (font (13.0f));
        g.drawText (r.label, x, 0, w - x - 24, h, juce::Justification::centredLeft, true);
        if (isFavourite (r.file)) icons::draw (g, icons::star, { (float) w - 18, 6.0f, 12.0f, 12.0f }, col::orange);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        auto r = rows[(size_t) row];
        if (e.mods.isPopupMenu()) { itemMenu (r); return; }
        if (r.audio && previewOn && ! r.isDir) { engine.previewFile (r.file); }
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        activate (rows[(size_t) row]);
    }
    void returnKeyPressed (int row) override { if (juce::isPositiveAndBelow (row, (int) rows.size())) activate (rows[(size_t) row]); }
    void selectedRowsChanged (int row) override
    {
        if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].audio && previewOn && juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown() == false)
            engine.previewFile (rows[(size_t) row].file);
    }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& sel) override
    {
        if (sel.size() == 0) return {};
        auto& r = rows[(size_t) sel[0]];
        if (r.isDir || r.pluginId.isNotEmpty() || r.internalName.isNotEmpty()) return {};
        return "file:" + r.file.getFullPathName();
    }

    // ---- Maschine+ 4-D encoder
    void moveSelection (int d)
    {
        if (rows.empty()) return;
        int r = juce::jlimit (0, (int) rows.size() - 1, (list.getSelectedRow() < 0 ? -1 : list.getSelectedRow()) + d);
        list.selectRow (r);
        list.scrollToEnsureRowIsOnscreen (r);
        if (rows[(size_t) r].audio && previewOn) engine.previewFile (rows[(size_t) r].file);
    }
    void activateSelected() { int r = list.getSelectedRow(); if (juce::isPositiveAndBelow (r, (int) rows.size())) activate (rows[(size_t) r]); }
    void nextCategory (int d)
    {
        if (filesMode) { if (d < 0 && currentDir.getParentDirectory() != currentDir) { currentDir = currentDir.getParentDirectory(); refresh(); } else if (d > 0) activateSelected(); return; }
        category = (category + d + 7) % 7; typeFilter.clear(); refresh();
    }
    juce::String selectedName() { int r = list.getSelectedRow(); return juce::isPositiveAndBelow (r, (int) rows.size()) ? rows[(size_t) r].label : juce::String(); }

private:
    Engine& engine;
    Ui& ui;
    LibraryIndex index;
    FlatButton libTab { "Library" }, filesTab { "Files" }, typesBtn { juce::CharPointer_UTF8 ("\xe2\x96\xb8  TYPES"), 11.0f }, updateBtn { "Update", 11.0f }, importBtn { "Import", 12.0f };
    juce::OwnedArray<IconButton> catBtns;
    IconButton userBtn { icons::userIcon }, eyeBtn { icons::eye }, clearBtn { icons::close }, starBtn { icons::star }, upBtn { icons::chevronUp, "Up one folder" };
    juce::TextEditor search;
    juce::ListBox list;
    juce::Slider volume;
    bool filesMode = false, previewOn = true, favOnly = false, userOnly = false;
    int category = 0;
    juce::String typeFilter;
    juce::File currentDir;

    struct Row { juce::String label; juce::File file; bool isDir = false, audio = false; juce::String pluginId, internalName; bool instrument = false; };
    std::vector<Row> rows;

    std::vector<std::pair<juce::String, juce::File>> quickLinks()
    {
        std::vector<std::pair<juce::String, juce::File>> l;
        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots (roots);
        for (auto& r : roots) l.push_back ({ r.getFullPathName().trimCharactersAtEnd ("\\/"), r });
        l.push_back ({ "Desktop", juce::File::getSpecialLocation (juce::File::userDesktopDirectory) });
        l.push_back ({ juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFileName(), juce::File::getSpecialLocation (juce::File::userHomeDirectory) });
        l.push_back ({ "Groovebox Pro", Settings::userFolder() });
        for (auto& f : engine.settings.getList ("favFolders")) if (juce::File (f).isDirectory()) l.push_back ({ juce::File (f).getFileName(), juce::File (f) });
        return l;
    }

    bool isFavourite (const juce::File& f) { return engine.settings.getList ("favourites").contains (f.getFullPathName()); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void textEditorTextChanged (juce::TextEditor&) override { refresh(); }

    void refresh()
    {
        libTab.setToggleState (! filesMode, juce::dontSendNotification);
        filesTab.setToggleState (filesMode, juce::dontSendNotification);
        for (int i = 0; i < catBtns.size(); ++i) catBtns[i]->setToggleState (i == category, juce::dontSendNotification);
        userBtn.setToggleState (userOnly, juce::dontSendNotification);
        typesBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8  TYPES")) + (typeFilter.isNotEmpty() ? ": " + typeFilter : juce::String()));
        rows.clear();
        auto q = search.getText().trim().toLowerCase();
        auto favs = engine.settings.getList ("favourites");
        auto& fm = SampleCache::get().formats();
        if (filesMode)
        {
            engine.settings.set ("browserDir", currentDir.getFullPathName());
            for (auto& d : currentDir.findChildFiles (juce::File::findDirectories, false))
                if (! d.isHidden() && (q.isEmpty() || d.getFileName().toLowerCase().contains (q))) rows.push_back ({ d.getFileName(), d, true });
            for (auto& f : currentDir.findChildFiles (juce::File::findFiles, false))
            {
                auto ext = f.getFileExtension().toLowerCase();
                bool audio = fm.findFormatForFileExtension (ext) != nullptr;
                bool known = audio || ext == ".gbp" || ext == ".gbg" || ext == ".gbs" || ext == ".mid" || ext == ".midi" || ext == ".gbpreset";
                if (! known || f.isHidden()) continue;
                if (q.isNotEmpty() && ! f.getFileName().toLowerCase().contains (q)) continue;
                if (favOnly && ! favs.contains (f.getFullPathName())) continue;
                rows.push_back ({ f.getFileName(), f, false, audio });
            }
            std::stable_sort (rows.begin(), rows.end(), [] (auto& a, auto& b) { if (a.isDir != b.isDir) return a.isDir; return a.label.compareNatural (b.label) < 0; });
        }
        else if (category == LibraryIndex::instruments || category == LibraryIndex::effects)
        {
            bool inst = category == LibraryIndex::instruments;
            for (auto& sec : internalMenu())
                for (auto& n : sec.items)
                {
                    if (isInternalInstrument (n) != inst) continue;
                    if (n == "Drum Synth") { for (auto& d : drumSynthModels()) addInternalRow ("Drum Synth: " + d, d, inst, q); continue; }
                    addInternalRow (n, n, inst, q);
                }
            if (! userOnly)
                for (auto& d : engine.knownPlugins.getTypes())
                {
                    if (d.isInstrument != inst) continue;
                    auto label = d.name + "  (" + d.manufacturerName + ")";
                    if (q.isNotEmpty() && ! label.toLowerCase().contains (q)) continue;
                    Row r; r.label = label; r.pluginId = d.createIdentifierString(); r.instrument = inst;
                    rows.push_back (r);
                }
        }
        else
        {
            for (auto& e : index.snapshot())
            {
                if (e.category != category) continue;
                if (typeFilter.isNotEmpty() && e.type != typeFilter) continue;
                if (q.isNotEmpty() && ! e.file.getFileName().toLowerCase().contains (q)) continue;
                if ((favOnly || userOnly) && ! favs.contains (e.file.getFullPathName())) continue;
                rows.push_back ({ e.file.getFileNameWithoutExtension(), e.file, false, category >= LibraryIndex::loops });
            }
        }
        list.updateContent();
        list.repaint();
        resized();
        repaint();
    }

    void addInternalRow (const juce::String& label, const juce::String& name, bool inst, const juce::String& q)
    {
        if (q.isNotEmpty() && ! label.toLowerCase().contains (q)) return;
        Row r; r.label = label; r.internalName = name; r.instrument = inst;
        rows.push_back (r);
    }

    void typesMenu()
    {
        juce::StringArray types;
        for (auto& e : index.snapshot()) if (e.category == category) types.addIfNotAlreadyThere (e.type);
        types.sort (true);
        juce::PopupMenu m;
        m.addItem (1, "All Types", true, typeFilter.isEmpty());
        for (int i = 0; i < types.size(); ++i) m.addItem (100 + i, types[i], true, types[i] == typeFilter);
        showMenu (m, &typesBtn, [this, types] (int r) { typeFilter = r == 1 ? juce::String() : types[r - 100]; refresh(); });
    }

    void activate (const Row& r)
    {
        if (r.isDir) { currentDir = r.file; search.clear(); refresh(); return; }
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        auto& s = *G->sounds[(size_t) G->focusedSound];
        if (r.pluginId.isNotEmpty() || r.internalName.isNotEmpty())
        {
            juce::String err;
            auto slot = engine.createSlot (r.pluginId.isNotEmpty() ? "VST3" : r.internalName, r.pluginId, err);
            if (slot == nullptr) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groovebox Pro", err); return; }
            engine.snapshot ("Load " + slot->displayName);
            if (r.instrument)
            {
                // replace the existing instrument in slot 1
                if (! s.slots.empty() && s.slots[0]->isInstrument) engine.removeSlot (s.slots, 0);
                engine.insertSlot (s.slots, 0, std::move (slot));
                if (s.name.startsWith ("Sound ")) s.name = r.internalName.isNotEmpty() ? r.internalName : r.label.upToFirstOccurrenceOf ("  (", false, false);
            }
            else engine.insertSlot (s.slots, (int) s.slots.size(), std::move (slot));
            ui.channelTarget = 2; ui.channelMode = 0; ui.selectedSlot = r.instrument ? 0 : (int) s.slots.size() - 1;
            ui.changed();
            return;
        }
        if (r.file.getFileExtension().equalsIgnoreCase (".gbpreset"))
        {
            if (auto x = juce::XmlDocument::parse (r.file))
            {
                juce::String err;
                auto slot = engine.createSlot (x->getStringAttribute ("type"), x->getStringAttribute ("id"), err);
                if (slot == nullptr) return;
                juce::MemoryBlock mb; mb.fromBase64Encoding (x->getStringAttribute ("state"));
                if (mb.getSize() > 0) slot->proc->setStateInformation (mb.getData(), (int) mb.getSize());
                engine.snapshot ("Load Preset");
                engine.insertSlot (s.slots, (int) s.slots.size(), std::move (slot));
                ui.changed();
            }
            return;
        }
        engine.stopPreview();
        actions::loadFileFromBrowser (engine, ui, r.file, engine.focusedGroup, G->focusedSound);
        if (r.audio) { int next = juce::jmin (numPads - 1, G->focusedSound + (G->sounds[(size_t) juce::jmin (15, G->focusedSound + 1)]->slots.empty() ? 1 : 0)); juce::ignoreUnused (next); }
    }

    void itemMenu (const Row& r)
    {
        juce::PopupMenu m;
        bool fav = isFavourite (r.file);
        m.addItem (1, "Load");
        if (! r.isDir && r.pluginId.isEmpty() && r.internalName.isEmpty()) m.addItem (2, fav ? "Remove from Favourites" : "Add to Favourites");
        if (r.isDir) m.addItem (3, "Add Folder to Favourites");
        if (r.isDir) m.addItem (4, "Add Folder to Library");
        if (! r.isDir && r.file.existsAsFile()) m.addItem (5, "Show in Explorer");
        if (r.audio) m.addItem (6, "Load into next empty pad");
        auto row = r;
        showMenu (m, this, [this, row] (int res)
        {
            if (res == 1) activate (row);
            else if (res == 2)
            {
                auto favs = engine.settings.getList ("favourites");
                if (favs.contains (row.file.getFullPathName())) favs.removeString (row.file.getFullPathName()); else favs.add (row.file.getFullPathName());
                engine.settings.setList ("favourites", favs);
                list.repaint();
            }
            else if (res == 3) { auto f = engine.settings.getList ("favFolders"); f.addIfNotAlreadyThere (row.file.getFullPathName()); engine.settings.setList ("favFolders", f); repaint(); }
            else if (res == 4) { auto f = engine.settings.getList ("libraryFolders"); f.addIfNotAlreadyThere (row.file.getFullPathName()); engine.settings.setList ("libraryFolders", f); index.rescan(); }
            else if (res == 5) row.file.revealToUser();
            else if (res == 6)
            {
                auto* G = engine.project().group (engine.focusedGroup);
                for (int i = 0; i < numPads && G; ++i)
                    if (G->sounds[(size_t) i]->slots.empty()) { actions::loadFileFromBrowser (engine, ui, row.file, engine.focusedGroup, i); engine.selectSound (engine.focusedGroup, i); break; }
                ui.changed();
            }
        });
    }
};
} // namespace gb
