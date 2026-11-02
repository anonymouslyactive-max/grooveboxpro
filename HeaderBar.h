#pragma once
#include "Widgets.h"

namespace gb
{
// A value in the header that can be dragged / clicked (BPM, swing, signature, position, grid)
class HeaderValue : public juce::Component, public juce::SettableTooltipClient
{
public:
    HeaderValue (const juce::String& cap) : caption (cap) {}
    juce::String caption;
    std::function<juce::String()> getText;
    std::function<void (double delta, bool fine)> onDrag;
    std::function<void()> onClick, onDoubleClick, onDragStart;
    bool highlight = false;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto txt = getText ? getText() : juce::String();
        auto f = font (14.0f, true);
        auto tw = (int) f.getStringWidthFloat (txt) + 2;
        g.setFont (f);
        g.setColour (highlight ? col::orange : col::text);
        g.drawText (txt, r.removeFromLeft (tw), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        g.drawText (caption, r.withTrimmedLeft (5), juce::Justification::centredLeft);
    }
    void mouseDown (const juce::MouseEvent&) override { lastY = 0; dragged = false; if (onDragStart && onDrag) onDragStart(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onDrag) return;
        int dy = e.getDistanceFromDragStartY();
        int step = dy - lastY;
        if (std::abs (step) >= 2) { onDrag (-step / 2.0, e.mods.isShiftDown()); lastY = dy; dragged = true; repaint(); }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! dragged && e.getNumberOfClicks() == 1 && onClick) onClick(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (onDrag) { if (onDragStart) onDragStart(); onDrag (w.deltaY > 0 ? 1.0 : -1.0, e.mods.isShiftDown()); repaint(); } }
private:
    int lastY = 0; bool dragged = false;
};

class HeaderBar : public juce::Component, private juce::Timer
{
public:
    HeaderBar (Engine& e, Ui& u) : engine (e), ui (u)
    {
        for (auto* b : { &dots, &library, &mixerBtn, &rewind, &playBtn, &recBtn, &loopBtn, &metroBtn, &followBtn, &kbdBtn, &winBtn, &powerBtn, &logoBtn })
            addAndMakeVisible (b);
        library.setClickingTogglesState (false);
        recBtn.on = col::orange;
        loopBtn.on = col::orange;
        metroBtn.on = col::orange;
        followBtn.on = col::orange;
        kbdBtn.drawBox = true; kbdBtn.iconScale = 0.75f;
        powerBtn.on = juce::Colours::white;

        dots.onClick = [this] { if (onMenu) onMenu (&dots); };
        library.onClick = [this] { ui.showBrowser = ! ui.showBrowser; ui.changed(); };
        mixerBtn.onClick = [this] { ui.showMixer = ! ui.showMixer; ui.changed(); };
        rewind.onClick = [this] { engine.position = 0.0; };
        playBtn.onClick = [this] { engine.togglePlay(); };
        recBtn.onClick = [this] { engine.toggleRecord(); };
        loopBtn.onClick = [this] { const juce::ScopedLock sl (engine.lock); engine.project().loopOn = ! engine.project().loopOn; };
        metroBtn.onClick = [this] { engine.metronomeOn = ! engine.metronomeOn; };
        metroBtn.setTooltip ("Metronome (right-click: tap tempo / settings)");
        followBtn.onClick = [this] { engine.followOn = ! engine.followOn; };
        followBtn.setTooltip ("Follow playhead");
        kbdBtn.onClick = [this] { if (onComputerKeyboard) onComputerKeyboard(); };
        kbdBtn.setTooltip ("Computer MIDI Keyboard (Ctrl+K)");
        winBtn.onClick = [this] { if (onFullscreen) onFullscreen(); };
        winBtn.setTooltip ("Fullscreen (Ctrl+F)");
        powerBtn.onClick = [this] { engine.engineOn = ! engine.engineOn; };
        powerBtn.setTooltip ("Audio engine on / off");
        logoBtn.onClick = [this] { if (onAbout) onAbout(); };

        addAndMakeVisible (link);
        link.style = FlatButton::plain; link.fontH = 10.0f;
        link.setTooltip ("Ableton Link is not available in Groovebox Pro. Use SYNC to follow external MIDI clock.");
        link.onClick = [this] { link.setToggleState (false, juce::dontSendNotification); };

        bpm.getText = [this] { return juce::String (engine.project().bpm, 2); };
        bpm.onDragStart = [this] { engine.snapshot ("Tempo"); };
        bpm.onDrag = [this] (double d, bool fine) { engine.setTempo (engine.project().bpm + d * (fine ? 0.01 : 1.0)); };
        bpm.onDoubleClick = [this] { editValue ("Tempo (BPM)", juce::String (engine.project().bpm, 2), [this] (juce::String s) { engine.snapshot ("Tempo"); engine.setTempo (s.getDoubleValue()); }); };
        bpm.setTooltip ("Drag to change tempo, double-click to type");

        swing.getText = [this] { return juce::String (engine.project().swing * 100.0f, 1) + " %"; };
        swing.onDragStart = [this] { engine.snapshot ("Swing"); };
        swing.onDrag = [this] (double d, bool fine) { auto& p = engine.project(); p.swing = juce::jlimit (0.0f, 1.0f, p.swing + (float) d * (fine ? 0.001f : 0.01f)); };
        swing.onDoubleClick = [this] { editValue ("Swing (%)", juce::String (engine.project().swing * 100.0f, 1), [this] (juce::String s) { engine.snapshot ("Swing"); engine.project().swing = juce::jlimit (0.0f, 1.0f, s.getFloatValue() / 100.0f); }); };

        sig.getText = [this] { return juce::String (engine.project().sigNum) + " / " + juce::String (engine.project().sigDen); };
        sig.onClick = [this]
        {
            juce::PopupMenu m, num, den;
            for (int i = 1; i <= 16; ++i) num.addItem (100 + i, juce::String (i), true, engine.project().sigNum == i);
            for (int d : { 2, 4, 8, 16 }) den.addItem (200 + d, juce::String (d), true, engine.project().sigDen == d);
            const juce::StringArray presets { "4/4", "3/4", "6/8", "5/4", "7/8", "12/8" };
            for (int i = 0; i < presets.size(); ++i) m.addItem (401 + i, presets[i]);
            m.addSeparator();
            m.addSubMenu ("Numerator", num);
            m.addSubMenu ("Denominator", den);
            showMenu (m, &sig, [this] (int r)
            {
                engine.snapshot ("Signature");
                const juce::ScopedLock sl (engine.lock);
                auto& p = engine.project();
                if (r > 100 && r <= 116) p.sigNum = r - 100;
                else if (r > 200 && r < 300) p.sigDen = r - 200;
                else if (r > 400)
                {
                    const juce::StringArray presets { "4/4", "3/4", "6/8", "5/4", "7/8", "12/8" };
                    auto s = presets[r - 401];
                    p.sigNum = s.getIntValue(); p.sigDen = s.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                }
                ui.changed();
            });
        };
        sig.setTooltip ("Time signature");

        pos.getText = [this]
        {
            auto& p = engine.project();
            auto t = (juce::int64) engine.position.load();
            if (! engine.songMode || p.songLength() == 0) t = (juce::int64) std::fmod (engine.position.load(), juce::jmax (1.0, engine.displayLoopLength()));
            if (showTime)
            {
                double secs = t / (double) PPQ * 60.0 / p.bpm;
                return juce::String ((int) secs / 60) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2) + "." + juce::String ((int) (secs * 10) % 10);
            }
            return formatTicks (t, p, true);
        };
        pos.onClick = [this] { showTime = ! showTime; pos.caption = showTime ? "TIME" : "BARS"; repaint(); };
        pos.setTooltip ("Song position (click: bars / time)");

        grid.getText = [] { return juce::String(); };
        grid.getText = [this] { return Engine::gridChoices()[engine.stepGrid]; };
        grid.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < Engine::gridChoices().size(); ++i) m.addItem (i + 1, Engine::gridChoices()[i], true, i == engine.stepGrid);
            showMenu (m, &grid, [this] (int r) { engine.stepGrid = r - 1; ui.changed(); });
        };
        grid.setTooltip ("Step / record quantize grid");
        sync.getText = [] { return juce::String(); };
        sync.onClick = [this] { engine.syncOn = ! engine.syncOn; };
        sync.setTooltip ("SYNC: follow external MIDI clock");

        for (auto* v : { &bpm, &swing, &sig, &pos, &grid, &sync }) addAndMakeVisible (v);

        addAndMakeVisible (volume);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.0);
        volume.setValue (0.75, juce::dontSendNotification);
        volume.onValueChange = [this] { engine.masterVolume = normToLevel ((float) volume.getValue()); };
        volume.setTooltip ("Master volume");
        volume.setDoubleClickReturnValue (true, 0.75);

        metroBtn.addMouseListener (this, false);
        startTimerHz (20);
    }

    std::function<void (juce::Component*)> onMenu;
    std::function<void()> onComputerKeyboard, onFullscreen, onAbout;
    bool computerKeyboardOn = false;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &metroBtn && e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Metronome", true, engine.metronomeOn);
            m.addItem (2, "Tap Tempo");
            juce::PopupMenu ci;
            for (int i = 0; i <= 4; ++i) ci.addItem (10 + i, i == 0 ? "Off" : juce::String (i) + (i == 1 ? " Bar" : " Bars"), true, engine.countInBars == i);
            m.addSubMenu ("Count-in", ci);
            showMenu (m, &metroBtn, [this] (int r)
            {
                if (r == 1) engine.metronomeOn = ! engine.metronomeOn;
                else if (r == 2) engine.tapTempo();
                else if (r >= 10) { engine.settings.set ("countIn", r - 10); engine.reloadSettingsCache(); }
            });
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::header);
        g.setColour (juce::Colours::black);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        // logo
        g.setColour (juce::Colour (0xffd8d8d8));
        g.setFont (font (17.0f, true).withExtraKerningFactor (0.08f));
        g.drawText ("GROOVEBOX PRO", 12, 0, 175, getHeight(), juce::Justification::centredLeft);
        g.setColour (col::bg.brighter (0.08f));
        for (auto x : separators) g.fillRect (x, 8, 1, getHeight() - 16);
        // labels
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        g.drawText ("SYNC", sync.getBounds(), juce::Justification::centredLeft);
        // cpu meter
        auto cpuR = juce::Rectangle<int> (cpuX, 9, 10, 14);
        float cpu = juce::jlimit (0.0f, 1.0f, engine.cpu());
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (i < (int) std::ceil (cpu * 6) ? (cpu > 0.8f ? col::orange : col::text) : col::buttonHi);
            g.fillRect (cpuR.getX(), cpuR.getBottom() - 2 - i * 2, 10, 1);
        }
        g.setColour (col::textDim);
        g.setFont (font (7.5f, true));
        g.drawText ("CPU", cpuX - 4, 23, 18, 8, juce::Justification::centred);
        // master meter under the volume
        auto mr = volume.getBounds().withHeight (2).withY (volume.getBottom() - 4);
        float lv = juce::jmax (engine.project().meterL.load(), engine.project().meterR.load()) * (float) engine.masterVolume.load();
        g.setColour (col::buttonHi);
        g.fillRect (mr);
        g.setColour (lv > 0.99f ? juce::Colours::red : col::textDim);
        g.fillRect (mr.withWidth ((int) (mr.getWidth() * juce::jlimit (0.0f, 1.0f, levelToNorm (lv)))));
    }

    void resized() override
    {
        int h = getHeight();
        separators.clear();
        separators.push_back (190);
        dots.setBounds (196, 0, 26, h);
        separators.push_back (226);
        library.setBounds (230, 0, 26, h);
        separators.push_back (260);
        mixerBtn.setBounds (264, 0, 28, h);
        separators.push_back (296);

        int x = juce::jmax (310, getWidth() / 2 - 320);
        rewind.setBounds (x, 0, 22, h); x += 32;
        playBtn.setBounds (x, 0, 22, h); x += 32;
        recBtn.setBounds (x, 0, 22, h); x += 30;
        loopBtn.setBounds (x, 0, 26, h); x += 36;
        link.setBounds (x, 0, 34, h); x += 42;
        separators.push_back (x); x += 8;
        metroBtn.setBounds (x, 0, 22, h); x += 30;
        bpm.setBounds (x, 0, 82, h); x += 88;
        separators.push_back (x); x += 8;
        swing.setBounds (x, 0, 86, h); x += 92;
        separators.push_back (x); x += 8;
        sig.setBounds (x, 0, 60, h); x += 66;
        separators.push_back (x); x += 8;
        pos.setBounds (x, 0, 80, h); x += 86;
        separators.push_back (x); x += 6;
        followBtn.setBounds (x, 0, 26, h); x += 32;
        separators.push_back (x); x += 10;
        grid.setBounds (x, 0, 44, h); x += 48;
        sync.setBounds (x, 0, 36, h); x += 40;
        kbdBtn.setBounds (x, 8, 24, h - 16); x += 30;

        int r = getWidth() - 8;
        logoBtn.setBounds (r - 26, 0, 26, h); r -= 32;
        separators.push_back (r); r -= 4;
        powerBtn.setBounds (r - 24, 0, 24, h); r -= 30;
        separators.push_back (r); r -= 8;
        cpuX = r - 12; r -= 20;
        volume.setBounds (r - 62, 6, 62, h - 12); r -= 70;
        separators.push_back (r); r -= 6;
        winBtn.setBounds (r - 24, 0, 24, h);
        separators.push_back (r - 30);
    }

private:
    Engine& engine;
    Ui& ui;
    IconButton dots { icons::dots, "Menu" }, library { icons::library, "Browser (F4)" }, mixerBtn { icons::mixer, "Mixer (Ctrl+3)" };
    IconButton rewind { icons::rewind, "Return to start" }, playBtn { icons::play, "Play / Stop (Space)" }, recBtn { icons::record, "Record (R)" }, loopBtn { icons::loop, "Loop" };
    IconButton metroBtn { icons::metronome, "Metronome" }, followBtn { icons::follow, "Follow" }, kbdBtn { icons::keyboard, "Computer MIDI Keyboard" };
    IconButton winBtn { icons::patternIcon, "Fullscreen" }, powerBtn { icons::power, "Audio Engine" }, logoBtn { icons::logoMark, "About Groovebox Pro" };
    FlatButton link { "LINK" };
    HeaderValue bpm { "BPM" }, swing { "SWING" }, sig { "SIG" }, pos { "BARS" }, grid { "" }, sync { "" };
    juce::Slider volume;
    std::vector<int> separators;
    int cpuX = 0;
    bool showTime = false;

    void editValue (const juce::String& title, const juce::String& current, std::function<void (juce::String)> apply)
    {
        auto* aw = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("v", current);
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, apply] (int r) { if (r == 1) apply (aw->getTextEditorContents ("v")); }), true);
    }

    void timerCallback() override
    {
        playBtn.setToggleState (engine.playing, juce::dontSendNotification);
        playBtn.icon = icons::play;
        playBtn.on = col::text;
        playBtn.off = engine.playing ? juce::Colours::white : col::textDim;
        recBtn.setToggleState (engine.recording, juce::dontSendNotification);
        recBtn.off = engine.countingIn ? col::orange.withAlpha ((juce::Time::getMillisecondCounter() / 250) % 2 ? 1.0f : 0.3f) : col::textDim;
        loopBtn.setToggleState (engine.project().loopOn, juce::dontSendNotification);
        metroBtn.setToggleState (engine.metronomeOn, juce::dontSendNotification);
        followBtn.setToggleState (engine.followOn, juce::dontSendNotification);
        library.setToggleState (ui.showBrowser, juce::dontSendNotification);
        mixerBtn.setToggleState (ui.showMixer, juce::dontSendNotification);
        kbdBtn.setToggleState (computerKeyboardOn, juce::dontSendNotification);
        powerBtn.setToggleState (engine.engineOn, juce::dontSendNotification);
        sync.highlight = engine.syncOn;
        for (auto* c : std::initializer_list<juce::Component*> { &playBtn, &recBtn, &bpm, &swing, &sig, &pos, &grid, &sync, &loopBtn, &metroBtn }) c->repaint();
        repaint (cpuX - 6, 0, 120, getHeight());
        repaint (volume.getBounds().expanded (2));
    }
};
} // namespace gb
