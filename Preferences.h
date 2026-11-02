#pragma once
#include "Actions.h"

namespace gb
{
// Background VST3 scanner
class PluginScanner : public juce::Thread
{
public:
    PluginScanner (Engine& e) : Thread ("Plug-in Scan"), engine (e) {}
    ~PluginScanner() override { stopThread (10000); }
    std::atomic<float> progress { 0 };
    juce::String current;
    juce::CriticalSection nameLock;
    std::function<void()> onFinished;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    void run() override
    {
        for (auto* fmt : engine.formatManager.getFormats())
        {
            if (fmt->getName() != "VST3") continue;
            juce::FileSearchPath path (engine.settings.getString ("vst3Paths", fmt->getDefaultLocationsToSearch().toString()));
            auto pedal = engine.pluginListFile().getSiblingFile ("ScanInProgress.txt");
            juce::PluginDirectoryScanner scanner (engine.knownPlugins, *fmt, path, true, pedal, false);
            juce::String name;
            for (;;)
            {
                if (threadShouldExit()) break;
                { const juce::ScopedLock sl (nameLock); current = scanner.getNextPluginFileThatWillBeScanned(); }
                if (! scanner.scanNextFile (true, name)) break;
                progress = scanner.getProgress();
            }
            for (auto& f : scanner.getFailedFiles()) engine.knownPlugins.addToBlacklist (f);
        }
        progress = 1.0f;
        auto* eng = &engine; auto flag = alive;
        juce::MessageManager::callAsync ([eng, flag, this] { eng->savePluginList(); if (*flag && onFinished) onFinished(); });
    }
private:
    Engine& engine;
};

class PreferencesPanel : public juce::Component, private juce::Timer, public juce::ListBoxModel
{
public:
    PreferencesPanel (Engine& e, Ui& u, int page) : engine (e), ui (u), scanner (e)
    {
        const char* names[] = { "General", "Audio", "MIDI", "Default", "Library", "Plug-ins", "Hardware", "Colors", "Data" };
        for (int i = 0; i < 9; ++i)
        {
            auto* b = nav.add (new FlatButton (names[i], 13.0f));
            b->just = juce::Justification::centredRight;
            b->onClick = [this, i] { showPage (i); };
            addAndMakeVisible (b);
        }
        closeBtn.style = FlatButton::boxed;
        closeBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeBtn);
        list.setModel (this);
        list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, col::header);
        scanner.onFinished = [this] { scanning = false; refreshList(); };
        showPage (page);
        startTimerHz (10);
    }
    ~PreferencesPanel() override { *scanner.alive = false; scanner.onFinished = nullptr; scanner.stopThread (8000); }

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff2a2a2a));
        g.setColour (col::text);
        g.setFont (font (16.0f));
        g.drawText ("Preferences", 0, 8, getWidth(), 30, juce::Justification::centred);
        g.setColour (juce::Colours::white);
        g.fillRect (0, 0, getWidth(), 3);
        g.setColour (col::textDim);
        g.setFont (font (11.5f));
        for (auto& [r, t, bold] : labels)
        {
            g.setColour (bold ? col::text : col::textDim);
            g.setFont (font (bold ? 12.5f : 11.5f, bold));
            g.drawText (t, r, juce::Justification::centredLeft, true);
            if (bold) { g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRect (r.getX(), r.getBottom(), contentW(), 1); }
        }
        if (page == 5 && scanning)
        {
            g.setColour (col::orange);
            g.drawText ("Scanning: " + juce::File (currentScanName()).getFileName(), contentX(), getHeight() - 70, contentW(), 16, juce::Justification::centredLeft);
            g.fillRect (contentX(), getHeight() - 52, (int) (contentW() * scanner.progress.load()), 3);
        }
    }

    void resized() override
    {
        for (int i = 0; i < nav.size(); ++i) nav[i]->setBounds (8, 48 + i * 26, 92, 22);
        closeBtn.setBounds (getWidth() - 74, getHeight() - 34, 60, 22);
        if (getLocalBounds() != builtFor && ! building) { buildPage(); return; }
        for (auto& [c, r] : places) c->setBounds (r);
    }
    juce::Rectangle<int> builtFor;
    bool building = false;

    // ListBoxModel for plug-ins / library folders / MIDI devices / learn list
    int getNumRows() override { return listRows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (sel) { g.setColour (col::buttonHi); g.fillRect (0, 0, w, h); }
        bool checked = listChecks[row];
        int x = 6;
        if (listHasChecks)
        {
            juce::Rectangle<float> cb (6.0f, (h - 12) / 2.0f, 12.0f, 12.0f);
            getLookAndFeel().drawTickBox (g, *this, cb.getX(), cb.getY(), cb.getWidth(), cb.getHeight(), checked, true, false, false);
            x = 24;
        }
        g.setColour (col::text);
        g.setFont (font (12.0f));
        g.drawText (listRows[row], x, 0, w - x - 4, h, juce::Justification::centredLeft, true);
        if (listRight.size() > row)
        {
            g.setColour (learnRow == row ? col::orange : col::textDim);
            g.drawText (learnRow == row ? juce::String ("Move a control...") : listRight[row], w - 170, 0, 164, h, juce::Justification::centredRight);
        }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (listHasChecks && e.x < 24) { toggleListRow (row); return; }
        if (page == 6) { learnRow = row; engine.learnTarget = row; list.repaint(); }
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (listHasChecks) toggleListRow (row); }

private:
    Engine& engine;
    Ui& ui;
    PluginScanner scanner;
    bool scanning = false;
    juce::OwnedArray<FlatButton> nav;
    FlatButton closeBtn { "Close" };
    int page = 0, subTab = 0;
    juce::OwnedArray<juce::Component> owned;
    template <typename T> T* own (T* c) { owned.add (c); return c; }
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> places;
    std::vector<std::tuple<juce::Rectangle<int>, juce::String, bool>> labels;
    juce::ListBox list;
    juce::StringArray listRows, listRight, listIds;
    juce::Array<bool> listChecks;
    bool listHasChecks = false;
    int learnRow = -1;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    int contentX() const { return 112; }
    int contentW() const { return getWidth() - contentX() - 16; }

    juce::String currentScanName() { const juce::ScopedLock sl (scanner.nameLock); return scanner.current; }

    void timerCallback() override
    {
        if (page == 5 && scanning) repaint();
        if (page == 6 && engine.learnSequential && engine.learnTarget >= 0 && engine.learnTarget != learnRow)
        {
            learnRow = engine.learnTarget; refreshLearnList(); list.selectRow (learnRow); list.scrollToEnsureRowIsOnscreen (learnRow); list.repaint();
        }
        else if (page == 6 && learnRow >= 0 && engine.learnTarget == -1 && ! engine.learnSequential) { learnRow = -1; buildPage(); }
        checkPadLearn();
    }

    void showPage (int p)
    {
        page = p; subTab = 0;
        for (int i = 0; i < nav.size(); ++i) { nav[i]->setToggleState (i == page, juce::dontSendNotification); nav[i]->onColour = juce::Colours::white; }
        buildPage();
    }

    void clear()
    {
        for (auto& [c, r] : places) removeChildComponent (c);
        places.clear(); labels.clear(); owned.clear();
        deviceSelector.reset();
        removeChildComponent (&list);
        listRows.clear(); listRight.clear(); listIds.clear(); listChecks.clear(); listHasChecks = false;
        engine.learnTarget = -1;
        engine.learnSequential = false;
    }

    int y = 0;
    void section (const juce::String& t) { y += 6; labels.push_back ({ { contentX(), y, contentW(), 18 }, t, true }); y += 24; }
    void rowLabel (const juce::String& t) { labels.push_back ({ { contentX(), y, 200, 22 }, t, false }); }

    void addCheck (const juce::String& label, const juce::String& key, bool def, std::function<void()> after = {})
    {
        rowLabel (label);
        auto* t = own (new juce::ToggleButton());
        t->setToggleState (engine.settings.getBool (key, def), juce::dontSendNotification);
        t->onClick = [this, t, key, after] { engine.settings.set (key, t->getToggleState()); engine.reloadSettingsCache(); if (after) after(); };
        place (t, { contentX() + contentW() - 24, y, 24, 22 });
        y += 24;
    }
    void addCombo (const juce::String& label, const juce::String& key, const juce::StringArray& items, int def, std::function<void (int)> after = {})
    {
        rowLabel (label);
        auto* c = own (new juce::ComboBox());
        c->addItemList (items, 1);
        c->setSelectedItemIndex (juce::jlimit (0, items.size() - 1, engine.settings.getInt (key, def)), juce::dontSendNotification);
        c->onChange = [this, c, key, after] { engine.settings.set (key, c->getSelectedItemIndex()); engine.reloadSettingsCache(); if (after) after (c->getSelectedItemIndex()); };
        place (c, { contentX() + contentW() - 170, y, 170, 22 });
        y += 26;
    }
    void addSlider (const juce::String& label, const juce::String& key, double mn, double mx, double def, double step = 0.01)
    {
        rowLabel (label);
        auto* s = own (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
        s->setRange (mn, mx, step);
        s->setValue (engine.settings.getDouble (key, def), juce::dontSendNotification);
        s->onValueChange = [this, s, key] { engine.settings.set (key, s->getValue()); engine.reloadSettingsCache(); };
        place (s, { contentX() + contentW() - 170, y, 170, 22 });
        y += 26;
    }
    void addFileField (const juce::String& label, const juce::String& key, const juce::String& pattern)
    {
        rowLabel (label);
        auto* b = own (new FlatButton (engine.settings.getString (key).isEmpty() ? juce::String ("(none)") : juce::File (engine.settings.getString (key)).getFileName(), 11.5f));
        b->style = FlatButton::boxed;
        b->just = juce::Justification::centredLeft;
        b->onClick = [this, key, pattern]
        {
            juce::PopupMenu m; m.addItem (1, "Choose..."); m.addItem (2, "Clear");
            showMenu (m, this, [this, key, pattern] (int r)
            {
                if (r == 2) { engine.settings.set (key, ""); engine.reloadSettingsCache(); buildPage(); return; }
                actions::chooseFile ("Choose file", Settings::userFolder(), pattern, false, [this, key] (juce::File f) { engine.settings.set (key, f.getFullPathName()); engine.reloadSettingsCache(); buildPage(); });
            });
        };
        place (b, { contentX() + contentW() - 170, y, 170, 22 });
        y += 26;
    }
    FlatButton* addButton (const juce::String& text, juce::Rectangle<int> r, std::function<void()> fn, bool boxed = true)
    {
        auto* b = own (new FlatButton (text, 12.0f));
        b->style = boxed ? FlatButton::boxed : FlatButton::tab;
        b->onClick = fn;
        place (b, r);
        return b;
    }
    void place (juce::Component* c, juce::Rectangle<int> r) { addAndMakeVisible (c); places.push_back ({ c, r }); c->setBounds (r); }
    void showList (int top, int bottom, bool checks)
    {
        listHasChecks = checks;
        place (&list, { contentX(), top, contentW(), bottom - top });
        list.updateContent();
        list.repaint();
    }

    void toggleListRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, listRows.size())) return;
        listChecks.set (row, ! listChecks[row]);
        if (page == 2)
        {
            auto key = subTab == 0 ? "midiInputs" : "midiOutputs";
            juce::StringArray en;
            for (int i = 0; i < listRows.size(); ++i) if (listChecks[i]) en.add (listRows[i]);
            engine.settings.setList (key, en);
            engine.refreshMidiInputs();
        }
        else if (page == 5)
        {
            auto dis = engine.settings.getList ("disabledPlugins");
            if (listChecks[row]) dis.removeString (listIds[row]); else dis.addIfNotAlreadyThere (listIds[row]);
            engine.settings.setList ("disabledPlugins", dis);
        }
        list.repaint();
    }

    void refreshList() { if (page == 5) buildPage(); }

    void buildPage()
    {
        if (getWidth() <= 0) return;
        const juce::ScopedValueSetter<bool> svs (building, true);
        builtFor = getLocalBounds();
        clear();
        y = 44;
        switch (page)
        {
            case 0:
                section ("Projects");
                addCheck ("Reload Last Project", "reloadLast", false);
                addCheck ("Automatic Backup", "autoBackup", true);
                section ("Recording Audio");
                addCheck ("Prefer Project Folder", "recPreferProject", true);
                section ("Metronome");
                addCheck ("Enabled", "metroEnabled", false, [this] { engine.metronomeOn = engine.settings.getBool ("metroEnabled", false); });
                addCheck ("Auto-Enable when Recording", "metroAutoRecord", true);
                addSlider ("Volume", "metroVolume", 0.0, 1.0, 0.7);
                addCombo ("Count-in Length", "countIn", { "Off", "1 Bar", "2 Bars", "4 Bars" }, 1);
                section ("Link");
                rowLabel ("Ableton Link is not available - use MIDI clock sync (MIDI page)");
                y += 24;
                section ("Input");
                addCombo ("Quantize", "inputQuantize", { "None", "Record" }, 1);
                break;
            case 1:
            {
                section ("Interface");
                deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.deviceManager, 0, 32, 0, 32, false, false, true, false);
                deviceSelector->setItemHeight (22);
                place (deviceSelector.get(), { contentX(), y, contentW(), getHeight() - y - 50 });
                break;
            }
            case 2:
            {
                section ("Sync");
                addCombo ("Mode", "syncMode", { "Off", "Send MIDI Clock", "Receive MIDI Clock" }, 0);
                {
                    rowLabel ("Clock Output");
                    auto* c = own (new juce::ComboBox());
                    c->addItem ("None", 1);
                    auto outs = juce::MidiOutput::getAvailableDevices();
                    for (int i = 0; i < outs.size(); ++i) c->addItem (outs[i].name, i + 2);
                    c->setText (engine.settings.getString ("clockOutput", "None"), juce::dontSendNotification);
                    c->onChange = [this, c] { engine.settings.set ("clockOutput", c->getText()); engine.refreshMidiInputs(); };
                    place (c, { contentX() + contentW() - 170, y, 170, 22 });
                    y += 26;
                }
                section ("Devices");
                addButton ("Input", { contentX(), y, 60, 20 }, [this] { subTab = 0; buildPageKeepTab(); }, false)->setToggleState (subTab == 0, juce::dontSendNotification);
                y += 26;
                auto enabled = engine.settings.getList ("midiInputs");
                bool first = ! engine.settings.props().containsKey ("midiInputs");
                for (auto& d : juce::MidiInput::getAvailableDevices()) { listRows.add (d.name); listChecks.add (first || enabled.contains (d.name)); }
                if (listRows.isEmpty()) rowLabel ("No MIDI inputs found. Connect your Maschine+ and set it to MIDI mode.");
                showList (y, getHeight() - 50, true);
                break;
            }
            case 3:
                section ("Project");
                addFileField ("Standalone Template", "standaloneTemplate", "*.gbp");
                section ("Metronome");
                addFileField ("Down Beat Sample", "metroDown", actions::audioWildcard());
                addFileField ("Up Beat Sample", "metroUp", actions::audioWildcard());
                section ("Scene / Section");
                addCombo ("Duplicate", "duplicateMode", { "Scene and Patterns", "Scene only" }, 0);
                addCheck ("Link when duplicating Sections", "linkDuplicate", true);
                section ("Pattern");
                addCombo ("Length (Ideas view)", "patternLengthBarsIdx", { "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }, 2,
                          [this] (int i) { engine.settings.set ("patternLengthBars", 1 << i); });
                addCheck ("Grow Pattern while Recording", "growPattern", false);
                section ("Sound");
                addCombo ("Default MIDI Input Mode", "defaultMidiInputMode", { "Focus", "None", "All" }, 0);
                addSlider ("Default Tempo", "defaultTempo", 60, 200, 120, 1);
                break;
            case 4:
            {
                section ("Library Folders");
                rowLabel ("Folders scanned for Loops / One-shots / Sounds / Groups");
                y += 24;
                for (auto& f : engine.settings.getList ("libraryFolders")) listRows.add (f);
                showList (y, getHeight() - 90, false);
                addButton ("Add Folder...", { contentX(), getHeight() - 80, 110, 22 }, [this]
                {
                    auto fc = std::make_shared<juce::FileChooser> ("Add Library Folder", juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
                    actions::chooserHolder() = fc;
                    fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c)
                    {
                        if (c.getResult().isDirectory()) { auto l = engine.settings.getList ("libraryFolders"); l.addIfNotAlreadyThere (c.getResult().getFullPathName()); engine.settings.setList ("libraryFolders", l); buildPage(); }
                    });
                });
                addButton ("Remove", { contentX() + 118, getHeight() - 80, 80, 22 }, [this]
                {
                    int r = list.getSelectedRow();
                    auto l = engine.settings.getList ("libraryFolders");
                    if (juce::isPositiveAndBelow (r, l.size())) { l.remove (r); engine.settings.setList ("libraryFolders", l); buildPage(); }
                });
                break;
            }
            case 5:
            {
                addButton ("Manager", { contentX(), y, 70, 20 }, [this] { subTab = 0; buildPageKeepTab(); }, false)->setToggleState (subTab == 0, juce::dontSendNotification);
                addButton ("Locations", { contentX() + 76, y, 76, 20 }, [this] { subTab = 1; buildPageKeepTab(); }, false)->setToggleState (subTab == 1, juce::dontSendNotification);
                y += 28;
                if (subTab == 0)
                {
                    auto dis = engine.settings.getList ("disabledPlugins");
                    auto types = engine.knownPlugins.getTypes();
                    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
                    for (auto& d : types) { listRows.add (d.name + " (" + d.pluginFormatName + ")"); listIds.add (d.createIdentifierString()); listChecks.add (! dis.contains (d.createIdentifierString())); listRight.add (d.manufacturerName); }
                    showList (y, getHeight() - 110, true);
                    int by = getHeight() - 100;
                    y = by + 28;
                    addCheck ("Use VST3 Plugins", "useVST3", true);
                    addButton (scanning ? "Scanning..." : "Rescan", { getWidth() - 100, by, 80, 22 }, [this]
                    {
                        if (scanning) return;
                        scanning = true; scanner.progress = 0;
                        scanner.startThread();
                        buildPage();
                    });
                    addButton ("Clear List", { getWidth() - 190, by, 80, 22 }, [this] { engine.knownPlugins.clear(); engine.knownPlugins.clearBlacklistedFiles(); engine.savePluginList(); buildPage(); });
                }
                else
                {
                    juce::FileSearchPath path (engine.settings.getString ("vst3Paths", juce::VST3PluginFormat().getDefaultLocationsToSearch().toString()));
                    for (int i = 0; i < path.getNumPaths(); ++i) listRows.add (path.getRawString (i));
                    showList (y, getHeight() - 90, false);
                    addButton ("Add Folder...", { contentX(), getHeight() - 80, 110, 22 }, [this]
                    {
                        auto fc = std::make_shared<juce::FileChooser> ("Add VST3 Folder", juce::File ("C:\\Program Files\\Common Files\\VST3"));
                        actions::chooserHolder() = fc;
                        fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c)
                        {
                            if (! c.getResult().isDirectory()) return;
                            juce::FileSearchPath p (engine.settings.getString ("vst3Paths", juce::VST3PluginFormat().getDefaultLocationsToSearch().toString()));
                            p.addIfNotAlreadyThere (c.getResult());
                            engine.settings.set ("vst3Paths", p.toString());
                            buildPageKeepTab();
                        });
                    });
                    addButton ("Remove", { contentX() + 118, getHeight() - 80, 80, 22 }, [this]
                    {
                        juce::FileSearchPath p (engine.settings.getString ("vst3Paths", juce::VST3PluginFormat().getDefaultLocationsToSearch().toString()));
                        int r = list.getSelectedRow();
                        if (juce::isPositiveAndBelow (r, p.getNumPaths())) { p.remove (r); engine.settings.set ("vst3Paths", p.toString()); buildPageKeepTab(); }
                    });
                }
                break;
            }
            case 6:
            {
                section ("Controller");
                rowLabel ("Maschine+ in MIDI mode (Controller Editor template)");
                y += 24;
                section ("Pads");
                {
                    rowLabel ("Pad 1 Note (pads 1-16 follow)");
                    auto* c = own (new juce::ComboBox());
                    for (int n = 0; n <= 112; ++n) c->addItem (noteName (n) + " (" + juce::String (n) + ")", n + 1);
                    c->setSelectedId (engine.settings.getInt ("padBaseNote", 48) + 1, juce::dontSendNotification);
                    c->onChange = [this, c] { engine.settings.set ("padBaseNote", c->getSelectedId() - 1); engine.reloadSettingsCache(); };
                    place (c, { contentX() + contentW() - 170, y, 170, 22 });
                    y += 26;
                    auto* learnPad = addButton ("Learn from Pad 1", { contentX() + contentW() - 170, y, 170, 22 }, [this]
                    {
                        engine.lastMidiNote = -1;
                        padLearn = true;
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Pad Learn", "Hit pad 1 (bottom-left) on your Maschine+ now, then close this message.");
                    });
                    juce::ignoreUnused (learnPad);
                    y += 26;
                }
                {
                    juce::StringArray ch { "All" };
                    for (int i = 1; i <= 16; ++i) ch.add (juce::String (i));
                    addCombo ("Pad MIDI Channel", "padChannel", ch, 0);
                }
                addCombo ("Velocity Scaling", "velocityScaling", { "Linear", "Soft (Log)", "Hard (Exp)", "Fixed" }, 0);
                addSlider ("Fixed Velocity", "fixedVelocity", 1, 127, 100, 1);
                {
                    rowLabel ("Light Feedback Output");
                    auto* c = own (new juce::ComboBox());
                    c->addItem ("None", 1);
                    auto outs = juce::MidiOutput::getAvailableDevices();
                    for (int i = 0; i < outs.size(); ++i) c->addItem (outs[i].name, i + 2);
                    c->setText (engine.settings.getString ("feedbackOutput", "None"), juce::dontSendNotification);
                    c->onChange = [this, c] { engine.settings.set ("feedbackOutput", c->getText()); };
                    place (c, { contentX() + contentW() - 170, y, 170, 22 });
                    y += 26;
                }
                section ("Controller Map (FelonE Beats template loaded - click a row and press / turn a control to change it)");
                auto& acts = learnActions();
                for (int i = 0; i < (int) acts.size(); ++i)
                {
                    listRows.add (acts[(size_t) i].label);
                    auto k = engine.learnKey (i);
                    listRight.add (k.isEmpty() ? juce::String ("-") : k.replace ("cc:0:", "CC ").replace ("note:0:", "Note ").replace ("cc:", "CC ch").replace ("note:", "Note ch").replace (":", " #"));
                }
                showList (y, getHeight() - 80, false);
                addButton ("Clear Selected", { contentX(), getHeight() - 70, 110, 22 }, [this]
                {
                    int r = list.getSelectedRow();
                    if (juce::isPositiveAndBelow (r, (int) learnActions().size())) { engine.settings.set ("learn2_" + learnActions()[(size_t) r].id, ""); engine.rebuildLearnCache(); buildPage(); }
                });
                addButton ("Learn All (guided)", { contentX() + 116, getHeight() - 70, 130, 22 }, [this]
                {
                    engine.learnSequential = true;
                    engine.learnTarget = 0; learnRow = 0;
                    list.selectRow (0);
                    list.repaint();
                });
                addButton ("Skip", { contentX() + 252, getHeight() - 70, 50, 22 }, [this]
                {
                    if (! engine.learnSequential) return;
                    int t = juce::jmax (0, learnRow) + 1;
                    if (t >= (int) learnActions().size()) { engine.learnSequential = false; engine.learnTarget = -1; learnRow = -1; buildPage(); return; }
                    learnRow = t; engine.learnTarget = t; list.selectRow (t); list.repaint();
                });
                addButton ("Reset to Template", { contentX() + 308, getHeight() - 70, 124, 22 }, [this]
                {
                    for (auto& a : learnActions()) engine.settings.props().removeValue ("learn2_" + a.id);
                    engine.settings.props().saveIfNeeded();
                    engine.settings.set ("padBaseNote", 48);
                    engine.reloadSettingsCache();
                    buildPage();
                });
                break;
            }
            case 7:
                section ("Colors");
                addCombo ("Group", "groupColours", { "Auto", "Orange" }, 0);
                addCombo ("Sound", "soundColours", { "Use Group Colour", "Auto" }, 0);
                addCheck ("Load with Colors", "loadWithColours", true);
                break;
            case 8:
                section ("Usage Data Tracking");
                rowLabel ("Groovebox Pro does not collect or send any usage data.");
                y += 24;
                section ("Folders");
                rowLabel ("User folder: " + Settings::userFolder().getFullPathName());
                y += 24;
                addButton ("Open User Folder", { contentX(), y, 140, 22 }, [] { Settings::userFolder().revealToUser(); });
                y += 30;
                addButton ("Open Settings Folder", { contentX(), y, 140, 22 }, [this] { engine.pluginListFile().getParentDirectory().revealToUser(); });
                break;
        }
        resized();
        repaint();
    }
    void buildPageKeepTab() { buildPage(); }
    void refreshLearnList()
    {
        listRight.clear();
        for (int i = 0; i < (int) learnActions().size(); ++i)
        {
            auto k = engine.learnKey (i);
            listRight.add (k.isEmpty() ? juce::String ("-") : k.replace ("cc:0:", "CC ").replace ("note:0:", "Note ").replace ("cc:", "CC ch").replace ("note:", "Note ch").replace (":", " #"));
        }
    }

    bool padLearn = false;
    void visibilityChanged() override {}
public:
    void checkPadLearn()
    {
        if (padLearn && engine.lastMidiNote >= 0)
        {
            engine.settings.set ("padBaseNote", engine.lastMidiNote.load());
            engine.reloadSettingsCache();
            padLearn = false;
            if (page == 6) buildPage();
        }
    }
};
} // namespace gb
