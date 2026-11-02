#pragma once
#include "HeaderBar.h"
#include "Arranger.h"
#include "ChannelPanel.h"
#include "EditorPanel.h"
#include "MixerView.h"
#include "Browser.h"
#include "Preferences.h"

namespace gb
{
class ExportJob : public juce::ThreadWithProgressWindow
{
public:
    ExportJob (Engine& e, Engine::ExportOptions o) : ThreadWithProgressWindow ("Exporting audio...", true, true), engine (e), opts (std::move (o)) {}
    void run() override
    {
        ok = engine.exportAudio (opts, [this] (float p) { setProgress (p); return ! threadShouldExit(); }, error);
    }
    void threadComplete (bool) override
    {
        if (onDone) onDone (ok, error);
        delete this;
    }
    std::function<void (bool, juce::String)> onDone;
    Engine& engine;
    Engine::ExportOptions opts;
    bool ok = false;
    juce::String error;
};

class MainComponent : public juce::Component, public juce::MenuBarModel, public juce::DragAndDropContainer,
                      private juce::Timer, private juce::ChangeListener
{
public:
    MainComponent (Engine& e) : engine (e), header (e, ui), arranger (e, ui), channel (e, ui), editor (e, ui), mixer (e, ui), browser (e, ui)
    {
        setLookAndFeel (&laf);
        juce::LookAndFeel::setDefaultLookAndFeel (&laf);
        addAndMakeVisible (header);
        addAndMakeVisible (arranger);
        addAndMakeVisible (channel);
        addAndMakeVisible (editor);
        addChildComponent (mixer);
        addChildComponent (browser);
        header.onMenu = [this] (juce::Component* c) { showMainMenu (c); };
        header.onComputerKeyboard = [this] { toggleComputerKeyboard(); };
        header.onFullscreen = [this] { if (onFullscreen) onFullscreen(); };
        header.onAbout = [this] { about(); };
        arranger.onSave = [this] (bool as) { save (as, false); };
        channel.onOpenPreferences = [this] (int p) { openPreferences (p); };
        engine.onStatus = [this] (const juce::String& s) { juce::MessageManager::callAsync ([this, s] { status (s); }); };
        engine.onProjectReplaced = [this] { ui.changed(); };
        engine.onLearnedAction = [this] (int a, float v) { handleAction (a, v); };
        engine.onPadFunction = [this] (int pad, float vel) { handlePadFunction (pad, vel); };
        channel.onHardwareStatus = [this] (const juce::String& s) { status (s); };
        ui.addChangeListener (this);
        engine.addChangeListener (this);
        setWantsKeyboardFocus (true);
        addKeyListener (&keys);
        keys.owner = this;
        // mixer strips use the same plug-in menus as the channel panel
        mixer.onAddPlugin = [this] (MixerStrip& st, int i, bool replace) { channel.addMenu (i, replace, &st); };
        mixer.onSlotMenu = [this] (MixerStrip& st, int i) { channel.slotMenu (i, &st); };
        mixer.rebuild();
        tooltip = std::make_unique<juce::TooltipWindow> (this, 700);
        startTimerHz (10);
        setSize (1600, 960);
    }

    ~MainComponent() override
    {
        ui.removeChangeListener (this);
        engine.removeChangeListener (this);
        removeKeyListener (&keys);
        prefs.reset();
        actions::chooserHolder().reset();   // don't let a FileChooser be destroyed during static destruction (after JUCE shutdown)
        setLookAndFeel (nullptr);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    std::function<void()> onFullscreen, onQuit;
    std::function<void (const juce::String&)> onTitle;

    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (statusText.isNotEmpty())
        {
            auto r = juce::Rectangle<int> (getWidth() - 420, getHeight() - 44, 400, 30);
            g.setColour (col::menuBg.withAlpha (0.95f));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (col::text);
            g.setFont (font (12.5f));
            g.drawFittedText (statusText, r.reduced (10, 2), juce::Justification::centredLeft, 2);
        }
        if (prefs != nullptr) { g.setColour (juce::Colours::black.withAlpha (0.35f)); }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (36));
        if (ui.showBrowser) browser.setBounds (r.removeFromLeft (330));
        browser.setVisible (ui.showBrowser);
        mixer.setVisible (ui.showMixer);
        arranger.setVisible (! ui.showMixer);
        channel.setVisible (! ui.showMixer);
        editor.setVisible (! ui.showMixer);
        if (ui.showMixer)
        {
            // like the original, the mixer takes over the whole window below the header
            mixer.setBounds (r);
        }
        else
        {
            int arrH = juce::jlimit (150, 320, r.getHeight() * 27 / 100);
            arranger.setBounds (r.removeFromTop (arrH));
            channel.setBounds (r.removeFromTop (ui.channelCollapsed ? 32 : 142));
            editor.setBounds (r);
        }
        if (prefs != nullptr) prefs->setBounds (getLocalBounds().withSizeKeepingCentre (560, juce::jmin (720, getHeight() - 60)));
    }

    // ------------------------------------------------------------------------------------------ menus
    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "View", "Controller", "Help" }; }

    juce::PopupMenu getMenuForIndex (int idx, const juce::String&) override
    {
        juce::PopupMenu m;
        auto cmd = [] (const juce::String& s) { return s; };
        juce::ignoreUnused (cmd);
        if (idx == 0)
        {
            m.addItem (item (101, "New", "Ctrl+N"));
            m.addItem (item (102, "Open...", "Ctrl+O"));
            juce::PopupMenu recent;
            auto rp = engine.settings.getList ("recentProjects");
            for (int i = 0; i < rp.size(); ++i) recent.addItem (1000 + i, juce::File (rp[i]).getFileNameWithoutExtension());
            m.addSubMenu ("Open Recent", recent, rp.size() > 0);
            m.addSeparator();
            m.addItem (item (103, "Save", "Ctrl+S"));
            m.addItem (item (104, "Save As...", "Ctrl+Shift+S"));
            m.addItem (105, "Save Project with Samples...");
            m.addSeparator();
            m.addItem (item (106, "Export Audio...", "Ctrl+E"));
            m.addItem (107, "Export Pattern as MIDI...");
            m.addItem (108, "Import MIDI / Samples...");
            m.addSeparator();
            m.addItem (109, "Manage Plug-ins...");
            m.addItem (110, "Preferences...");
            m.addSeparator();
            m.addItem (111, "Exit");
        }
        else if (idx == 1)
        {
            m.addItem (item (201, "Undo" + (engine.canUndo() ? ": " + engine.undoName() : juce::String()), "Ctrl+Z", engine.canUndo()));
            m.addItem (item (202, "Redo" + (engine.canRedo() ? ": " + engine.redoName() : juce::String()), "Ctrl+Y", engine.canRedo()));
            m.addItem (item (203, "Undo Step: " + engine.undoName(), "Ctrl+Shift+Z", engine.canUndo()));
            m.addItem (item (204, "Redo Step", "Ctrl+Shift+Y", engine.canRedo()));
            m.addSeparator();
            auto kb = item (205, "Computer MIDI Keyboard", "Ctrl+K"); kb.isTicked = computerKeyboard; m.addItem (kb);
            m.addItem (206, "Pencil Mode", true, ui.pencil);
            m.addItem (207, "MIDI Learn...");
            m.addSeparator();
            m.addItem (item (208, "Create new Group", "Ctrl+T"));
            m.addItem (209, "Quantize Pattern", true);
            m.addItem (210, "Clear Pattern", true);
        }
        else if (idx == 2)
        {
            auto b = item (301, "Browser", "F4"); b.isTicked = ui.showBrowser; m.addItem (b);
            m.addSeparator();
            auto i1 = item (302, "Ideas", "Ctrl+1"); i1.isTicked = ! engine.songMode && ! ui.showMixer; m.addItem (i1);
            auto i2 = item (303, "Song", "Ctrl+2"); i2.isTicked = engine.songMode && ! ui.showMixer; m.addItem (i2);
            auto i3 = item (304, "Mixer", "Ctrl+3"); i3.isTicked = ui.showMixer; m.addItem (i3);
            m.addSeparator();
            m.addItem (item (310, "Zoom In (horizontal)", "Ctrl++"));
            m.addItem (item (311, "Zoom Out (horizontal)", "Ctrl+-"));
            m.addItem (item (312, "Zoom In (vertical)", "Ctrl+Shift++"));
            m.addItem (item (313, "Zoom Out (vertical)", "Ctrl+Shift+-"));
            m.addItem (item (314, "Reset Zoom", "Ctrl+0"));
            m.addSeparator();
            m.addItem (item (305, "Fullscreen", "Ctrl+F"));
        }
        else if (idx == 3)
        {
            m.addItem (401, "MASCHINE PLUS (MIDI mode)", true, true);
            m.addSeparator();
            m.addItem (402, "Pad / MIDI Learn Settings...");
            m.addItem (403, "Refresh MIDI Devices");
            m.addItem (404, "Keyboard Mode (pads play focused sound)", true, engine.keyboardMode);
        }
        else
        {
            m.addItem (501, "Keyboard Shortcuts");
            m.addItem (502, "Open User Folder");
            m.addItem (503, "Maschine+ MIDI Setup Guide");
            m.addSeparator();
            m.addItem (504, "About Groovebox Pro");
        }
        return m;
    }

    void menuItemSelected (int id, int) override { handleMenu (id); }

    void handleMenu (int id)
    {
        if (id >= 1000 && id < 1100)
        {
            auto rp = engine.settings.getList ("recentProjects");
            if (juce::isPositiveAndBelow (id - 1000, rp.size())) confirmDiscard ([this, f = juce::File (rp[id - 1000])] { openProject (f); });
            return;
        }
        switch (id)
        {
            case 101: confirmDiscard ([this] { engine.newProject(); ui.changed(); }); break;
            case 102: confirmDiscard ([this] { actions::chooseFile ("Open Project", Settings::userFolder().getChildFile ("Projects"), "*.gbp", false, [this] (juce::File f) { openProject (f); }); }); break;
            case 103: save (false, false); break;
            case 104: save (true, false); break;
            case 105: save (true, true); break;
            case 106: exportDialog(); break;
            case 107:
                if (auto* p = engine.editPattern (engine.focusedGroup))
                    actions::chooseFile ("Export MIDI", Settings::userFolder().getChildFile ("Exports").getChildFile (p->name + ".mid"), "*.mid", true,
                                         [this] (juce::File f) { actions::exportPatternMidi (engine, engine.focusedGroup, f.withFileExtension ("mid")); status ("Exported " + f.getFileName()); });
                break;
            case 108:
                actions::chooseFile ("Import", Settings::userFolder(), "*.mid;*.midi;" + actions::audioWildcard() + ";*.gbg;*.gbs", false,
                                     [this] (juce::File f) { auto* g = engine.project().group (engine.focusedGroup); if (g) actions::loadFileFromBrowser (engine, ui, f, engine.focusedGroup, g->focusedSound); });
                break;
            case 109: openPreferences (5); break;
            case 110: openPreferences (0); break;
            case 111: if (onQuit) onQuit(); break;
            case 201: case 203: engine.undo(); ui.changed(); break;
            case 202: case 204: engine.redo(); ui.changed(); break;
            case 205: toggleComputerKeyboard(); break;
            case 206: ui.pencil = ! ui.pencil; ui.changed(); break;
            case 207: case 402: openPreferences (6); break;
            case 208: engine.snapshot ("Add Group"); engine.addGroup(); ui.changed(); break;
            case 209: editor.getGrid().quantizeSelection (true); break;
            case 210: if (auto* p = engine.editPattern (engine.focusedGroup)) { engine.snapshot ("Clear Pattern"); const juce::ScopedLock sl (engine.lock); p->notes.clear(); ui.changed(); } break;
            case 301: ui.showBrowser = ! ui.showBrowser; ui.changed(); break;
            case 302: engine.songMode = false; ui.showMixer = false; ui.changed(); break;
            case 303: engine.songMode = true; ui.showMixer = false; ui.changed(); break;
            case 304: ui.showMixer = ! ui.showMixer; ui.changed(); break;
            case 305: if (onFullscreen) onFullscreen(); break;
            case 310: zoom (true, 1); break;
            case 311: zoom (true, -1); break;
            case 312: zoom (false, 1); break;
            case 313: zoom (false, -1); break;
            case 314: zoom (true, 0); break;
            case 401: break;
            case 403: engine.refreshMidiInputs(); status ("MIDI inputs: " + engine.midiInputNames().joinIntoString (", ")); break;
            case 404: engine.keyboardMode = ! engine.keyboardMode; ui.changed(); break;
            case 501: shortcuts(); break;
            case 502: Settings::userFolder().revealToUser(); break;
            case 503: midiGuide(); break;
            case 504: about(); break;
            default: break;
        }
    }

    void showMainMenu (juce::Component* c)
    {
        juce::PopupMenu m;
        auto names = getMenuBarNames();
        for (int i = 0; i < names.size(); ++i) m.addSubMenu (names[i], getMenuForIndex (i, names[i]));
        showMenu (m, c, [this] (int r) { handleMenu (r); });
    }

    // ------------------------------------------------------------------------------------------ project
    void openProject (const juce::File& f)
    {
        if (engine.loadProject (f)) { status ("Opened " + f.getFileName()); ui.changed(); }
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groovebox Pro", "Could not open " + f.getFullPathName());
    }

    void save (bool saveAs, bool withSamples, std::function<void()> afterSave = {})
    {
        auto f = engine.project().file;
        if (saveAs || f == juce::File())
        {
            auto start = f != juce::File() ? f : Settings::userFolder().getChildFile ("Projects").getChildFile (engine.project().name + ".gbp");
            actions::chooseFile (withSamples ? "Save Project with Samples" : "Save Project", start, "*.gbp", true, [this, withSamples, afterSave] (juce::File chosen)
            {
                auto target = chosen.withFileExtension ("gbp");
                if (withSamples)
                {
                    // project folder with the project file and its samples
                    auto folder = target.getParentDirectory().getChildFile (target.getFileNameWithoutExtension());
                    folder.createDirectory();
                    target = folder.getChildFile (target.getFileName());
                }
                if (engine.saveProject (target, withSamples)) { status ("Saved " + target.getFileName()); if (afterSave) afterSave(); }
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groovebox Pro", "Could not save " + target.getFullPathName());
                ui.changed();
            });
            return;
        }
        if (engine.saveProject (f, false)) { status ("Saved " + f.getFileName()); if (afterSave) afterSave(); }
        ui.changed();
    }

    void confirmDiscard (std::function<void()> then)
    {
        if (! engine.dirty) { then(); return; }
        auto* aw = new juce::AlertWindow ("Groovebox Pro", "Save changes to \"" + engine.project().name + "\"?", juce::MessageBoxIconType::QuestionIcon);
        aw->addButton ("Save", 1);
        aw->addButton ("Don't Save", 2);
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, then] (int r)
        {
            if (r == 1) save (false, false, then);
            else if (r == 2) then();
        }), true);
    }

    void exportDialog()
    {
        auto* aw = new juce::AlertWindow ("Export Audio", "Render to WAV", juce::MessageBoxIconType::NoIcon);
        aw->addComboBox ("range", { "Song (whole arrangement)", "Current Scene / Pattern loop", "Song from start (N bars)" }, "Range");
        aw->addComboBox ("bars", { "1", "2", "4", "8", "16", "32", "64" }, "Bars (loop / N bars)");
        aw->getComboBoxComponent ("bars")->setSelectedItemIndex (2);
        aw->addComboBox ("stems", { "Master", "Each Group (stems)", "Each Sound of focused Group" }, "Source");
        aw->addComboBox ("bits", { "16 bit", "24 bit", "32 bit float" }, "Bit Depth");
        aw->getComboBoxComponent ("bits")->setSelectedItemIndex (1);
        aw->addComboBox ("norm", { "No", "Yes" }, "Normalize");
        aw->addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r)
        {
            if (r != 1) return;
            Engine::ExportOptions o;
            o.range = aw->getComboBoxComponent ("range")->getSelectedItemIndex();
            o.bars = aw->getComboBoxComponent ("bars")->getText().getIntValue();
            o.stems = aw->getComboBoxComponent ("stems")->getSelectedItemIndex();
            const int bits[] = { 16, 24, 32 };
            o.bitDepth = bits[juce::jmax (0, aw->getComboBoxComponent ("bits")->getSelectedItemIndex())];
            o.normalize = aw->getComboBoxComponent ("norm")->getSelectedItemIndex() == 1;
            actions::chooseFile ("Export Audio", Settings::userFolder().getChildFile ("Exports").getChildFile (engine.project().name + ".wav"), "*.wav", true, [this, o] (juce::File f) mutable
            {
                o.file = f.withFileExtension ("wav");
                engine.stop(); engine.stop();
                auto* job = new ExportJob (engine, o);
                auto file = o.file;
                job->onDone = [this, file] (bool ok, juce::String err)
                {
                    if (ok) { status ("Exported " + file.getFileName()); file.revealToUser(); }
                    else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", err);
                };
                job->launchThread();
            });
        }), true);
    }

    void openPreferences (int page)
    {
        prefs = std::make_unique<PreferencesPanel> (engine, ui, page);
        prefs->onClose = [this] { juce::MessageManager::callAsync ([this] { prefs.reset(); ui.changed(); grabKeyboardFocus(); }); };
        addAndMakeVisible (*prefs);
        resized();
        prefs->toFront (true);
    }

    void about()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Groovebox Pro",
            "Groovebox Pro 1.0\nA groovebox DAW for the Maschine+ in MIDI mode.\nMade for FelonE Beats.\n\nAudio engine: " + juce::String (engine.getSampleRate(), 0) + " Hz");
    }

    void shortcuts()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Keyboard Shortcuts",
            "Space  Play / Stop\nR  Record\nCtrl+N / O / S  New / Open / Save\nCtrl+Shift+S  Save As\nCtrl+E  Export Audio\nCtrl+Z / Ctrl+Y  Undo / Redo\n"
            "Ctrl+K  Computer MIDI Keyboard\nCtrl+T  New Group\nF4  Browser\nCtrl+1 / 2 / 3  Ideas / Song / Mixer\nCtrl+F  Fullscreen\n"
            "P  Pencil mode\nQ  Quantize selected notes\nCtrl+A / C / X / V / D  Select all / Copy / Cut / Paste / Duplicate notes\nDelete  Delete notes / clip\n"
            "Arrows  Nudge notes\nCtrl+Mouse wheel  Zoom\n\nComputer keyboard (Ctrl+K on):\n1 2 3 4 / Q W E R / A S D F / Z X C V = pads 13-16 / 9-12 / 5-8 / 1-4\nIn Keyboard mode: A W S E D F T G Y H U J K = notes, Z / X octave");
    }

    void midiGuide()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Maschine+ MIDI Setup",
            "1. On the Maschine+, switch to MIDI mode (SHIFT + CHANNEL/MIDI) and load your \"FelonE Beats\" Controller Editor template.\n"
            "2. In Preferences > MIDI, tick the Maschine Plus MIDI input.\n"
            "3. In Preferences > Hardware, use \"Learn from Pad 1\" so pads 1-16 line up with Sound 1-16.\n"
            "4. Use the MIDI Learn list to map transport buttons and knobs (Play, Record, Tempo, Volume...).\n"
            "Transport buttons sending MMC work automatically.");
    }

    void status (const juce::String& s) { statusText = s; statusTime = juce::Time::getMillisecondCounter(); repaint(); }

    // ------------------------------------------------------------------------------------------ Maschine+ controller
    std::set<juce::String> held;            // buttons currently held on the controller
    int lastEncoder = -1;
    juce::var lockSnapshot;
    std::vector<NoteEvent> patternClipboard;

    bool isHeld (const juce::String& id) const { return held.count (id) > 0; }

    void updatePadFunction()
    {
        int f = 0;
        const char* order[] = { "shift", "mute", "solo", "select", "erase", "duplicate", "scene", "pattern", "events" };
        for (int i = 0; i < 9; ++i) if (isHeld (order[i])) { f = i + 1; break; }
        if (f == 0 && ui.stepMode) f = 10;
        engine.padFunction = f;
    }

    void handleAction (int a, float v)
    {
        if (a < 0) { if (prefs) prefs->repaint(); return; }
        auto& acts = learnActions();
        if (! juce::isPositiveAndBelow (a, (int) acts.size())) return;
        const auto& act = acts[(size_t) a];
        auto id = act.id;
        auto* G = engine.project().group (engine.focusedGroup);
        bool shift = isHeld ("shift");

        // continuous controls
        if (act.kind == 1)
        {
            if (id.startsWith ("knob")) channel.controlFromHardware (id.getTrailingIntValue() - 1, v, ui.autoWrite);
            else if (id == "masterVol") { engine.masterVolume = normToLevel (v); status ("Master " + gainToText ((float) engine.masterVolume.load())); }
            else if (id == "tempoCC") { engine.setTempo (60.0 + v * 140.0); status ("Tempo " + juce::String (engine.project().bpm, 2)); }
            else if (id == "strip") touchStrip (v);
            return;
        }
        if (act.kind == 2) { encoderTurn (v); return; }

        bool pressed = v > 0.5f;
        if (pressed) held.insert (id); else held.erase (id);
        updatePadFunction();
        if (id == "noteRepeat") { engine.noteRepeat = pressed; return; }
        if (! pressed) { if (id == "notes" || id == "pitch") {} return; }   // everything else acts on press

        if (id == "play") engine.togglePlay();
        else if (id == "stop") engine.stop();
        else if (id == "record") { if (shift) { int ci = engine.countInBars > 0 ? 0 : 1; engine.settings.set ("countIn", ci); engine.reloadSettingsCache(); status (ci ? "Count-in on" : "Count-in off"); } else engine.toggleRecord(); }
        else if (id == "restart") { if (shift) { engine.project().loopOn = ! engine.project().loopOn; status (engine.project().loopOn ? "Loop on" : "Loop off"); } else engine.restart(); }
        else if (id == "tap") { if (shift) { engine.metronomeOn = ! engine.metronomeOn; status (engine.metronomeOn ? "Metronome on" : "Metronome off"); } else { engine.tapTempo(); status ("Tempo " + juce::String (engine.project().bpm, 1)); } }
        else if (id == "follow") { if (shift) { engine.stepGrid = (engine.stepGrid + 1) % 7; status ("Grid " + Engine::gridChoices()[engine.stepGrid]); } else engine.followOn = ! engine.followOn; }
        else if (id == "channel") { ui.showMixer = false; ui.channelMode = 1; ui.channelCollapsed = false; if (shift) { ui.propPage = 0; ui.propTab = 1; } }
        else if (id == "plugin") { ui.showMixer = false; ui.channelMode = 0; ui.channelCollapsed = false; }
        else if (id == "ideas") { engine.songMode = shift ? true : ! engine.songMode; ui.showMixer = false; status (engine.songMode ? "Song view" : "Ideas view"); }
        else if (id == "mixer") ui.showMixer = ! ui.showMixer;
        else if (id == "browser") ui.showBrowser = ! ui.showBrowser;
        else if (id == "sampling") { ui.showMixer = false; ui.editorMode = ui.editorMode == 1 ? 0 : 1; }
        else if (id == "pageLeft" || id == "pageRight") channel.stepPage (id == "pageLeft" ? -1 : 1);
        else if (id == "file") { if (shift) save (false, false); else save (false, false); }
        else if (id == "settings") { if (prefs) prefs.reset(); else openPreferences (0); }
        else if (id == "auto") { ui.autoWrite = ! ui.autoWrite; status (ui.autoWrite ? "AUTO: knob moves write automation" : "AUTO off"); }
        else if (id == "macro") { ui.showMixer = false; ui.channelMode = 1; ui.propPage = 3; }
        else if (id.startsWith ("disp")) channel.pressTab (id.getTrailingIntValue() - 1);
        else if (id == "encPush") { if (ui.showBrowser) browser.activateSelected(); else engine.togglePlay(); }
        else if (id == "encUp" || id == "encDown")
        {
            int d = id == "encUp" ? -1 : 1;
            if (ui.showBrowser) browser.moveSelection (d);
            else if (G) { engine.selectSound (engine.focusedGroup, juce::jlimit (0, 15, G->focusedSound + d)); status (G->sounds[(size_t) G->focusedSound]->name); }
        }
        else if (id == "encLeft" || id == "encRight")
        {
            int d = id == "encLeft" ? -1 : 1;
            if (ui.showBrowser) browser.nextCategory (d);
            else engine.position = juce::jmax (0.0, std::round (engine.position.load() / engine.project().ticksPerBar() + d) * engine.project().ticksPerBar());
        }
        else if (id == "lock")
        {
            if (shift || lockSnapshot.isVoid()) { lockSnapshot = juce::var (engine.toValueTree (true).createXml()->toString()); status ("Lock: snapshot stored"); }
            else if (auto x = juce::XmlDocument::parse (lockSnapshot.toString())) { engine.snapshot ("Lock Recall"); engine.restoreSnapshot (juce::ValueTree::fromXml (*x)); status ("Lock: snapshot recalled"); }
        }
        else if (id == "pitch" || id == "mod" || id == "perform" || id == "notes")
        {
            ui.stripMode = id == "pitch" ? 0 : id == "mod" ? 1 : id == "perform" ? 2 : 3;
            status (juce::String ("Touch strip: ") + act.label.upToFirstOccurrenceOf (" ", false, false));
        }
        else if (id.startsWith ("group"))
        {
            int gi = id.getLastCharacter() - 'A';
            if (gi >= (int) engine.project().groups.size()) { engine.snapshot ("Add Group"); while ((int) engine.project().groups.size() <= gi) engine.addGroup(); }
            auto* g = engine.project().group (gi);
            if (isHeld ("mute") && g) g->mute = ! g->mute;
            else if (isHeld ("solo") && g) g->solo = ! g->solo;
            else engine.focusedGroup = gi;
            if (g) status (g->name);
        }
        else if (id == "fixedVel") { engine.fixedVelOn = ! engine.fixedVelOn; status (engine.fixedVelOn ? "Fixed velocity on" : "Fixed velocity off"); }
        else if (id == "padMode") { ui.keyboardView = false; engine.keyboardMode = false; engine.chordMode = false; ui.stepMode = false; ui.editorMode = 0; status ("Pad mode"); }
        else if (id == "keyboard") { ui.keyboardView = true; engine.keyboardMode = true; engine.chordMode = false; ui.stepMode = false; ui.editorMode = 0; status ("Keyboard mode"); }
        else if (id == "chords") { engine.chordMode = ! engine.chordMode; if (engine.chordMode) { ui.keyboardView = true; engine.keyboardMode = true; } status (engine.chordMode ? "Chords on" : "Chords off"); }
        else if (id == "step") { ui.stepMode = ! ui.stepMode; ui.stepPage = 0; status (ui.stepMode ? "Step mode: pads = steps of the focused sound" : "Step mode off"); }
        else if (id == "variation") { ui.showMixer = false; ui.channelTarget = 2; ui.channelMode = 1; ui.propPage = 2; ui.propTab = 1; }
        else if (id == "duplicate" && shift && G) { int gi = engine.focusedGroup; engine.snapshot ("Duplicate Clip"); arranger.duplicateClip (gi, engine.editClip (gi)); }
        else if (id == "undo") engine.undo();
        else if (id == "redo") engine.redo();
        else if (id == "scene" && shift) { engine.songMode = true; }
        else if (id == "pattern" && shift) { if (auto* p = engine.editPattern (engine.focusedGroup)) { engine.snapshot ("Double Pattern"); const juce::ScopedLock sl (engine.lock); auto n0 = p->notes; for (auto n : n0) { n.start += p->length; p->notes.push_back (n); } p->length *= 2; } }
        updatePadFunction();
        ui.changed();
    }

    void encoderTurn (float raw)
    {
        int r = (int) raw, d;
        if (r >= 1 && r <= 15) d = r;                  // relative (2's complement)
        else if (r >= 113 && r <= 127 && lastEncoder < 0) d = r - 128;
        else if (r == 63 || r == 65) d = r - 64;       // relative (binary offset)
        else { d = lastEncoder < 0 ? 0 : juce::jlimit (-8, 8, r - lastEncoder); if (r == 0 && lastEncoder <= 1) d = -1; if (r == 127 && lastEncoder >= 126) d = 1; }
        lastEncoder = r;
        if (d == 0) return;
        auto* G = engine.project().group (engine.focusedGroup);
        bool shift = isHeld ("shift");
        if (isHeld ("volume")) { engine.masterVolume = normToLevel (juce::jlimit (0.0f, 1.0f, levelToNorm ((float) engine.masterVolume.load()) + d * 0.01f)); status ("Master " + gainToText ((float) engine.masterVolume.load())); }
        else if (isHeld ("swing")) { auto& p = engine.project(); p.swing = juce::jlimit (0.0f, 1.0f, p.swing + d * 0.01f); status ("Swing " + juce::String (p.swing * 100.0f, 0) + " %"); }
        else if (isHeld ("tempo")) { engine.setTempo (engine.project().bpm + d * (shift ? 0.01 : 1.0)); status ("Tempo " + juce::String (engine.project().bpm, 2)); }
        else if (isHeld ("noteRepeat")) { engine.repeatRate = juce::jlimit (0, 9, engine.repeatRate + d); status ("Note repeat " + Engine::gridChoices()[engine.repeatRate]); }
        else if (ui.showBrowser) { browser.moveSelection (d); status (browser.selectedName()); }
        else if (G) { engine.selectSound (engine.focusedGroup, juce::jlimit (0, 15, G->focusedSound + (d > 0 ? 1 : -1))); status (G->sounds[(size_t) G->focusedSound]->name); }
        ui.changed();
    }

    void touchStrip (float v)
    {
        if (ui.stripMode == 0) engine.sendLive (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) (v * 16383))));
        else if (ui.stripMode == 1) engine.sendLive (juce::MidiMessage::controllerEvent (1, 1, (int) (v * 127)));
        else if (ui.stripMode == 2) engine.sendLive (juce::MidiMessage::controllerEvent (1, 74, (int) (v * 127)));
        else if (auto* G = engine.project().group (engine.focusedGroup))
        {
            int note = G->sounds[(size_t) G->focusedSound]->rootKey - 12 + (int) (v * 24);
            if (note != lastStripNote) { engine.triggerPad (engine.focusedGroup, G->focusedSound, 0.8f, true, note); lastStripNote = note; }
        }
    }
    int lastStripNote = -1;

    // pads while SHIFT / MUTE / SOLO / SELECT / ERASE / DUPLICATE / SCENE / PATTERN / EVENTS / STEP are active
    void handlePadFunction (int pad, float)
    {
        int f = engine.padFunction;
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        auto* p = engine.editPattern (engine.focusedGroup);
        auto& snd = *G->sounds[(size_t) pad];
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        int fs = G->focusedSound;
        switch (f)
        {
            case 1: shiftPad (pad); break;
            case 2: snd.mute = ! snd.mute; status (snd.name + (snd.mute ? " muted" : " unmuted")); break;
            case 3: snd.solo = ! snd.solo; status (snd.name + (snd.solo ? " solo" : " unsolo")); break;
            case 4: engine.selectSound (engine.focusedGroup, pad); status (snd.name); break;
            case 5: if (p) { engine.snapshot ("Erase"); const juce::ScopedLock sl (engine.lock); p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(), [pad] (auto& n) { return n.sound == pad; }), p->notes.end()); status ("Erased " + snd.name); } break;
            case 6: if (pad != fs) { engine.snapshot ("Duplicate Sound"); engine.loadSoundInto (engine.focusedGroup, pad, engine.saveSound (engine.focusedGroup, fs)); status ("Copied to pad " + juce::String (pad + 1)); } break;
            case 7:
            {
                auto& P = engine.project();
                if (pad >= (int) P.scenes.size()) { engine.snapshot ("Add Scene"); const juce::ScopedLock sl (engine.lock); P.scenes.push_back ({ "Scene " + juce::String (P.scenes.size() + 1), std::vector<int> (P.groups.size(), -1) }); }
                P.currentScene = juce::jmin (pad, (int) P.scenes.size() - 1);
                engine.songMode = false;
                status (P.scenes[(size_t) P.currentScene].name);
                break;
            }
            case 8:
            {
                if (engine.songMode) { if (pad < (int) G->clips.size()) { engine.setEditClip (engine.focusedGroup, pad); auto* cp = G->pattern (G->clips[(size_t) pad].pattern); status (cp ? cp->name : "Clip " + juce::String (pad + 1)); } break; }
                engine.ensureSceneSlots();
                if (pad >= (int) G->patterns.size()) { engine.snapshot ("New Pattern"); engine.newPatternFor (*G, engine.defaultPatternLength()); }
                const juce::ScopedLock sl (engine.lock);
                engine.project().scenes[(size_t) engine.project().currentScene].patternForGroup[(size_t) engine.focusedGroup] = juce::jmin (pad, (int) G->patterns.size() - 1);
                status (G->patterns[(size_t) juce::jmin (pad, (int) G->patterns.size() - 1)]->name);
                break;
            }
            case 9: if (p) { for (auto& n : p->notes) n.selected = n.sound == pad; engine.selectSound (engine.focusedGroup, pad); status ("Events of " + snd.name + " selected"); } break;
            case 10:   // step sequencer: pads = 16 steps of the focused sound
            {
                auto* pat = editor.getGrid().ensurePattern();
                if (pat == nullptr) break;
                juce::int64 t = (juce::int64) (ui.stepPage * 16 + pad) * q;
                if (t >= pat->length) break;
                engine.snapshot ("Step");
                const juce::ScopedLock sl (engine.lock);
                auto it = std::find_if (pat->notes.begin(), pat->notes.end(), [t, fs] (auto& n) { return n.sound == fs && n.start == t; });
                if (it != pat->notes.end()) pat->notes.erase (it);
                else { NoteEvent n; n.sound = fs; n.start = t; n.length = q; n.velocity = engine.fixedVelOn ? engine.fixedVelocity / 127.0f : 0.8f; pat->notes.push_back (n); }
                break;
            }
            default: break;
        }
        ui.changed();
    }

    void shiftPad (int pad)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        auto* p = engine.editPattern (engine.focusedGroup);
        int fs = G ? G->focusedSound : 0;
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        const char* names[] = { "Undo", "Redo", "Step Undo", "Step Redo", "Quantize", "Quantize 50%", "Nudge <", "Nudge >", "Clear", "Clear Auto", "Copy", "Paste", "Semitone -", "Semitone +", "Octave -", "Octave +" };
        switch (pad)
        {
            case 0: case 2: engine.undo(); break;
            case 1: case 3: engine.redo(); break;
            case 4: case 5:
                if (p)
                {
                    engine.snapshot ("Quantize");
                    const juce::ScopedLock sl (engine.lock);
                    for (auto& n : p->notes) { auto t = ((n.start + q / 2) / q) * q; n.start = (pad == 4 ? t : (n.start + t) / 2) % std::max<juce::int64> (1, p->length); }
                }
                break;
            case 6: case 7:
                if (p)
                {
                    engine.snapshot ("Nudge");
                    const juce::ScopedLock sl (engine.lock);
                    for (auto& n : p->notes) if (n.sound == fs) n.start = ((n.start + (pad == 6 ? -q : q)) % p->length + p->length) % p->length;
                }
                break;
            case 8: if (p) { engine.snapshot ("Clear"); const juce::ScopedLock sl (engine.lock); p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(), [fs] (auto& n) { return n.sound == fs; }), p->notes.end()); } break;
            case 9: if (p) { engine.snapshot ("Clear Auto"); const juce::ScopedLock sl (engine.lock); p->lanes.erase (std::remove_if (p->lanes.begin(), p->lanes.end(), [fs] (auto& l) { return l.sound == fs; }), p->lanes.end()); } break;
            case 10: if (p) patternClipboard = p->notes; break;
            case 11:
                if (! patternClipboard.empty())
                    if (auto* tp = editor.getGrid().ensurePattern())
                    { engine.snapshot ("Paste"); const juce::ScopedLock sl (engine.lock); for (auto n : patternClipboard) if (n.start < tp->length) tp->notes.push_back (n); }
                break;
            case 12: case 13: case 14: case 15:
            {
                int d = pad == 12 ? -1 : pad == 13 ? 1 : pad == 14 ? -12 : 12;
                if (engine.keyboardMode) { engine.keyboardOffset = juce::jlimit (-48, 48, engine.keyboardOffset + d); status ("Keyboard transpose " + juce::String (engine.keyboardOffset.load())); return; }
                if (G)
                    if (auto* sp = engine.samplerOf (*G->sounds[(size_t) fs]))
                    {
                        auto* prm = sp->params[SamplerProcessor::tune];
                        float st = juce::jlimit (-24.0f, 24.0f, prm->convertFrom0to1 (prm->getValue()) + (float) d);
                        prm->setValueNotifyingHost (prm->convertTo0to1 (st));
                        status ("Tune " + juce::String (st, 0) + " st");
                        return;
                    }
                break;
            }
            default: break;
        }
        status (names[pad]);
    }

    // light up the controller buttons (works when the template's LED setting follows MIDI input)
    std::unique_ptr<juce::MidiOutput> feedbackOut;
    juce::String feedbackName;
    std::map<juce::String, int> sentLeds;
    void updateLeds()
    {
        auto name = engine.settings.getString ("feedbackOutput");
        if (name != feedbackName)
        {
            feedbackName = name; feedbackOut.reset(); sentLeds.clear();
            for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name == name) feedbackOut = juce::MidiOutput::openDevice (d.identifier);
        }
        if (feedbackOut == nullptr) return;
        std::map<juce::String, bool> st {
            { "play", engine.playing.load() }, { "record", engine.recording.load() }, { "restart", engine.project().loopOn }, { "tap", engine.metronomeOn.load() },
            { "follow", engine.followOn.load() }, { "mixer", ui.showMixer }, { "browser", ui.showBrowser }, { "ideas", ! engine.songMode.load() },
            { "sampling", ui.editorMode == 1 }, { "channel", ui.channelMode == 1 }, { "plugin", ui.channelMode == 0 }, { "auto", ui.autoWrite },
            { "fixedVel", engine.fixedVelOn.load() }, { "padMode", ! engine.keyboardMode.load() }, { "keyboard", engine.keyboardMode.load() },
            { "chords", engine.chordMode.load() }, { "step", ui.stepMode }, { "noteRepeat", engine.noteRepeat.load() }, { "macro", ui.channelMode == 1 && ui.propPage == 3 },
            { "pitch", ui.stripMode == 0 }, { "mod", ui.stripMode == 1 }, { "perform", ui.stripMode == 2 }, { "notes", ui.stripMode == 3 } };
        for (const char* h : { "mute", "solo", "select", "erase", "duplicate", "scene", "pattern", "events", "shift", "volume", "swing", "tempo" }) st[h] = isHeld (h);
        for (int g = 0; g < 8; ++g) st[juce::String ("group") + (char) ('A' + g)] = engine.focusedGroup == g;
        auto& acts = learnActions();
        for (int i = 0; i < (int) acts.size(); ++i)
        {
            auto it = st.find (acts[(size_t) i].id);
            if (it == st.end()) continue;
            auto key = engine.learnKey (i);
            if (key.isEmpty()) continue;
            int v = it->second ? 127 : 0;
            auto sit = sentLeds.find (key);
            if (sit != sentLeds.end() && sit->second == v) continue;
            sentLeds[key] = v;
            auto parts = juce::StringArray::fromTokens (key, ":", "");
            int ch = juce::jmax (1, parts[1].getIntValue()), num = parts[2].getIntValue();
            feedbackOut->sendMessageNow (parts[0] == "cc" ? juce::MidiMessage::controllerEvent (ch, num, v) : juce::MidiMessage::noteOn (ch, num, (juce::uint8) v));
        }
    }

    // ------------------------------------------------------------------------------------------ keyboard
    void toggleComputerKeyboard() { computerKeyboard = ! computerKeyboard; header.computerKeyboardOn = computerKeyboard; status (computerKeyboard ? "Computer MIDI Keyboard on" : "Computer MIDI Keyboard off"); }

    struct Keys : juce::KeyListener
    {
        MainComponent* owner = nullptr;
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override { return owner->handleKey (k); }
        bool keyStateChanged (bool down, juce::Component*) override { return owner->keyState (down); }
    } keys;

    // zoom the window under the mouse (arranger, pattern editor / sample editor or mixer); dir 0 = reset
    void zoom (bool horizontal, int dir)
    {
        auto mp = juce::Desktop::getMousePosition();
        bool overArranger = arranger.isShowing() && arranger.getScreenBounds().contains (mp);
        bool overMixer = mixer.isShowing() && mixer.getScreenBounds().contains (mp);
        if (overMixer) { if (dir == 0) mixer.zoomReset(); else mixer.zoomStrips (dir); return; }
        if (overArranger)
        {
            if (dir == 0) arranger.zoomReset();
            else if (horizontal) arranger.zoomHorizontal (dir > 0 ? 1.25 : 0.8);
            else arranger.zoomVertical (dir);
            return;
        }
        if (dir == 0) editor.zoomReset();
        else if (horizontal) editor.zoomHorizontal (dir > 0 ? 1.25 : 0.8);
        else editor.zoomVertical (dir);
    }

    bool handleKey (const juce::KeyPress& k)
    {
        auto cmd = k.getModifiers().isCommandDown();
        auto shift = k.getModifiers().isShiftDown();
        int code = k.getKeyCode();
        auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (cmd)
        {
            if (code == 'N') { handleMenu (101); return true; }
            if (code == 'O') { handleMenu (102); return true; }
            if (code == 'S') { handleMenu (shift ? 104 : 103); return true; }
            if (code == 'E') { handleMenu (106); return true; }
            if (code == 'Z') { if (shift) engine.redo(); else engine.undo(); ui.changed(); return true; }
            if (code == 'Y') { engine.redo(); ui.changed(); return true; }
            if (code == 'K') { toggleComputerKeyboard(); return true; }
            if (code == 'T') { handleMenu (208); return true; }
            if (code == 'F') { handleMenu (305); return true; }
            if (code == '1') { handleMenu (302); return true; }
            if (code == '2') { handleMenu (303); return true; }
            if (code == '3') { handleMenu (304); return true; }
            if (code == 'D' && ! editor.getGrid().hasKeyboardFocus (false)) { engine.snapshot ("Duplicate Clip"); arranger.duplicateSelection(); ui.changed(); return true; }
            if (code == 'A' && ! editor.getGrid().hasKeyboardFocus (false) && engine.songMode) { arranger.selectAllClips(); return true; }
            if (code == '=' || code == '+' || code == juce::KeyPress::numberPadAdd) { zoom (! shift, 1); return true; }
            if (code == '-' || code == '_' || code == juce::KeyPress::numberPadSubtract) { zoom (! shift, -1); return true; }
            if (code == '0' || code == juce::KeyPress::numberPad0) { zoom (true, 0); return true; }
            return false;
        }
        if (code == juce::KeyPress::F4Key) { handleMenu (301); return true; }
        if (code == juce::KeyPress::spaceKey) { engine.togglePlay(); return true; }
        if (computerKeyboard)
        {
            int note = keyToPad (ch);
            if (note != -999)
            {
                if (heldKeys.count (ch) == 0)
                {
                    heldKeys[ch] = note;
                    auto* G = engine.project().group (engine.focusedGroup);
                    if (G == nullptr) return true;
                    if (engine.keyboardMode) engine.triggerPad (engine.focusedGroup, G->focusedSound, 0.8f, true, note);
                    else { engine.triggerPad (engine.focusedGroup, note, 0.8f, true); if (G->sounds[(size_t) note]->focusMode == 0) engine.selectSound (engine.focusedGroup, note); }
                }
                return true;
            }
            if (engine.keyboardMode && ch == 'z') { kbdOctave = juce::jmax (-4, kbdOctave - 1); status ("Octave " + juce::String (kbdOctave)); return true; }
            if (engine.keyboardMode && ch == 'x') { kbdOctave = juce::jmin (4, kbdOctave + 1); status ("Octave " + juce::String (kbdOctave)); return true; }
        }
        if (ch == 'r') { engine.toggleRecord(); return true; }
        if (ch == 'p') { ui.pencil = ! ui.pencil; ui.changed(); return true; }
        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && ! editor.getGrid().hasKeyboardFocus (false)) { arranger.deleteSelectedClip(); return true; }
        if (code == juce::KeyPress::escapeKey) { arranger.clearSelection(); return true; }
        return false;
    }

    int keyToPad (juce::juce_wchar ch)
    {
        if (engine.keyboardMode)
        {
            const juce::String keysStr ("awsedftgyhujkolp");
            int i = keysStr.indexOfChar (ch);
            if (i < 0) return -999;
            auto* G = engine.project().group (engine.focusedGroup);
            int root = G ? G->sounds[(size_t) G->focusedSound]->rootKey : 60;
            return juce::jlimit (0, 127, root + i + kbdOctave * 12);
        }
        const juce::String rows[] = { "zxcv", "asdf", "qwer", "1234" };
        for (int r = 0; r < 4; ++r) { int i = rows[r].indexOfChar (ch); if (i >= 0) return r * 4 + i; }
        return -999;
    }

    bool keyState (bool)
    {
        bool any = false;
        for (auto it = heldKeys.begin(); it != heldKeys.end();)
        {
            auto ch = it->first;
            bool down = juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase (ch)) || juce::KeyPress::isKeyCurrentlyDown ((int) ch);
            if (! down)
            {
                auto* G = engine.project().group (engine.focusedGroup);
                if (G)
                {
                    if (engine.keyboardMode) engine.triggerPad (engine.focusedGroup, G->focusedSound, 0, false, it->second);
                    else engine.triggerPad (engine.focusedGroup, it->second, 0, false);
                }
                it = heldKeys.erase (it);
                any = true;
            }
            else ++it;
        }
        return any;
    }

private:
    Engine& engine;
    LookAndFeel laf;
    Ui ui;
    HeaderBar header;
    ArrangerPanel arranger;
    ChannelPanel channel;
    EditorPanel editor;
    MixerView mixer;
    BrowserPanel browser;
    std::unique_ptr<PreferencesPanel> prefs;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    bool computerKeyboard = false;
    int kbdOctave = 0;
    std::map<juce::juce_wchar, int> heldKeys;
    juce::String statusText;
    juce::uint32 statusTime = 0;
    juce::String lastTitle;

    static juce::PopupMenu::Item item (int id, const juce::String& text, const juce::String& shortcut, bool enabled = true)
    {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.shortcutKeyDescription = shortcut;
        i.isEnabled = enabled;
        return i;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        resized();
        repaint();
        menuItemsChanged();
    }

    void timerCallback() override
    {
        updateLeds();
        if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > 3500) { statusText = {}; repaint(); }
        auto title = engine.project().name + (engine.dirty ? " *" : "");
        if (title != lastTitle) { lastTitle = title; if (onTitle) onTitle (title); }
        // keep keyboard shortcuts working in the main window - but only while the main window itself is the active
        // window. A plug-in window (or any other window) that is in front keeps its focus, so typing in a plug-in's
        // text boxes (preset search etc.) works.
        auto* active = juce::TopLevelWindow::getActiveTopLevelWindow();
        bool mainActive = active != nullptr && active == getTopLevelComponent() && juce::Process::isForegroundProcess();
        if (mainActive && ! hasKeyboardFocus (true) && prefs == nullptr && juce::Component::getCurrentlyFocusedComponent() == nullptr) grabKeyboardFocus();
    }
};
} // namespace gb
