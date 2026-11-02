#pragma once
#include "PatternEditor.h"
#include "SamplingView.h"

namespace gb
{
// Bottom section: group header, sound list / pad view, pattern editor or sampling page, modulation lanes
class EditorPanel : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget,
                    private juce::Timer, private juce::ChangeListener
{
public:
    EditorPanel (Engine& e, Ui& u) : engine (e), ui (u), grid (e, u), lane (e, u), sampling (e, u)
    {
        for (auto* b : { &listBtn, &patternBtn, &waveBtn, &paletteBtn, &ccBtn, &collapseBtn, &speakerBtn, &padsBtn, &pencilBtn })
            addAndMakeVisible (b);
        listBtn.onClick = [this] { ui.editorMode = 0; ui.keyboardView = false; engine.keyboardMode = false; ui.changed(); };
        listBtn.setTooltip ("Pad mode: one row per sound");
        patternBtn.onClick = [this] { ui.editorMode = 0; ui.keyboardView = true; engine.keyboardMode = true; ui.changed(); };
        patternBtn.setTooltip ("Keyboard mode: piano roll for the focused sound (pads play it chromatically)");
        waveBtn.onClick = [this] { ui.editorMode = 1; ui.changed(); };
        waveBtn.setTooltip ("Sampling: record / edit / slice");
        paletteBtn.onClick = [this] { lane.showAll = ! lane.showAll; lane.repaint(); };
        paletteBtn.setTooltip ("Show velocities of all sounds");
        ccBtn.onClick = [this] { addLaneMenu(); };
        ccBtn.setTooltip ("Add a modulation / automation lane");
        collapseBtn.onClick = [this] { ui.editorCollapsed = ! ui.editorCollapsed; ui.changed(); };
        speakerBtn.onClick = [this] { if (auto* g = engine.project().group (engine.focusedGroup)) { g->mute = ! g->mute; repaint(); } };
        speakerBtn.setTooltip ("Mute group");
        padsBtn.onClick = [this] { ui.padView = ! ui.padView; ui.changed(); };
        padsBtn.setTooltip ("Show the 16 pads (Key / Choke / Link / Focus)");
        pencilBtn.onClick = [this] { ui.pencil = ! ui.pencil; ui.changed(); };
        pencilBtn.setTooltip ("Pencil mode: paint / erase notes");
        speakerBtn.on = col::orange;
        padsBtn.on = col::orange;
        pencilBtn.on = col::orange;

        addAndMakeVisible (grid);
        addAndMakeVisible (lane);
        addChildComponent (sampling);
        auto t2x = [this] (double t) { return (t - ui.scrollTick) * pxPerTick(); };
        auto x2t = [this] (double x) { return ui.scrollTick + x / pxPerTick(); };
        grid.tickToX = t2x; grid.xToTick = x2t;
        lane.tickToX = t2x; lane.xToTick = x2t;
        grid.regionStart = [this] { return regionStart(); };
        lane.regionStart = [this] { return regionStart(); };
        grid.getTopNote = [this] { return pianoTopNote(); };
        grid.onEdited = [this] { lane.repaint(); repaintArea(); };
        lane.onEdited = [this] { grid.repaint(); };

        for (auto* t : { "Velocity", "Probability" })
        {
            auto* b = laneBtns.add (new FlatButton (t, 12.5f));
            b->style = FlatButton::menuItem;
            b->just = juce::Justification::centredLeft;
            addAndMakeVisible (b);
        }
        laneBtns[0]->onClick = [this] { ui.laneMode = 0; ui.changed(); };
        laneBtns[1]->onClick = [this] { ui.laneMode = 1; ui.changed(); };
        addAndMakeVisible (addLaneBtn);
        addLaneBtn.onClick = [this] { addLaneMenu(); };
        addLaneBtn.setTooltip ("Add a parameter automation lane for the focused sound");

        ui.addChangeListener (this);
        engine.addChangeListener (this);
        startTimerHz (30);
    }
    ~EditorPanel() override { ui.removeChangeListener (this); engine.removeChangeListener (this); }

    static constexpr int iconW = 36, listW = 165, pianoW = 20, topH = 56, rulerH = 16, footerH = 26;
    int modH() const { return ui.editorCollapsed ? 0 : juce::jlimit (50, juce::jmax (50, getHeight() / 2), ui.laneH); }
    int timelineX() const { return iconW + listW; }
    int rowsTop() const { return topH; }
    int rowsBottom() const { return getHeight() - footerH - modH(); }
    int rowH() const { return ui.keyboardView ? pianoKeyH() : soundRowH(); }
    int autoSoundRowH() const { return juce::jlimit (14, 24, (rowsBottom() - rowsTop()) / numPads); }
    int soundRowH() const { return ui.drumRowH > 0 ? juce::jlimit (10, 80, ui.drumRowH) : autoSoundRowH(); }
    int maxDrumScroll() const { return juce::jmax (0, numPads - (rowsBottom() - rowsTop()) / juce::jmax (1, soundRowH())); }
    int drumScroll() const { return juce::jlimit (0, maxDrumScroll(), ui.drumScroll); }
    int soundAtY (int y) const { return (y - rowsTop()) / juce::jmax (1, soundRowH()) + drumScroll(); }

    double pxPerTick() const
    {
        if (ui.pxPerTick > 0) return ui.pxPerTick;
        return 210.0 / (double) engine.project().ticksPerBar();   // ~210 px per bar like the original
    }

    juce::int64 regionStart()
    {
        if (! engine.songMode) return 0;
        auto* G = engine.project().group (engine.focusedGroup);
        int ci = engine.editClip (engine.focusedGroup);
        if (G == nullptr || ! juce::isPositiveAndBelow (ci, (int) G->clips.size())) return 0;
        return G->clips[(size_t) ci].start;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::header);
        g.fillRect (0, 0, iconW, getHeight());
        g.setColour (juce::Colours::black);
        g.fillRect (iconW - 1, 0, 1, getHeight());
        g.fillRect (timelineX() - 1, 0, 1, getHeight());
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        const juce::ScopedLock sl (engine.lock);

        // group header
        g.setColour (G->colour == col::orange ? col::orange : G->colour);
        g.setFont (font (15.0f));
        g.drawText (G->name, iconW + 8, 2, listW - 30, 26, juce::Justification::centredLeft);
        icons::draw (g, icons::triangleDown, { (float) timelineX() - 22, 8.0f, 14.0f, 14.0f }, col::textDim);

        if (ui.padView) paintPads (g, *G);
        else paintSoundList (g, *G);
        paintPiano (g);
        if (ui.editorMode == 0) { paintRuler (g); paintClipStrip (g, *G); }

        // mod lane captions
        if (! ui.editorCollapsed && ui.editorMode == 0)
        {
            int y = rowsBottom();
            g.setColour (col::bg);
            g.fillRect (iconW, y, listW, modH());
            g.setColour (juce::Colours::black);
            g.fillRect (iconW, y, getWidth() - iconW, 1);
            g.setColour (col::textDark);
            g.setFont (font (10.0f));
            juce::String top = ui.laneMode == 0 ? "127" : "100%", mid = ui.laneMode == 0 ? "63" : "50%";
            g.drawText (top, timelineX() - 34, y + 2, 30, 12, juce::Justification::centredRight);
            g.drawText (mid, timelineX() - 34, y + modH() / 2 - 6, 30, 12, juce::Justification::centredRight);
            g.drawText ("0", timelineX() - 34, y + modH() - 14, 30, 12, juce::Justification::centredRight);
        }
        // footer
        int fy = getHeight() - footerH;
        g.setColour (col::bg);
        g.fillRect (iconW, fy, getWidth() - iconW, footerH);
        g.setColour (juce::Colours::black);
        g.fillRect (iconW, fy, getWidth() - iconW, 1);
        icons::draw (g, icons::grid, { (float) iconW + 8, (float) fy + 6, 13.0f, 13.0f }, col::text);
        g.setColour (col::text);
        g.setFont (font (12.5f, true));
        g.drawText (Engine::gridChoices()[engine.stepGrid], iconW + 26, fy, 60, footerH, juce::Justification::centredLeft);
        // scrollbar
        auto sr = scrollRect();
        g.setColour (col::button);
        g.fillRoundedRectangle (sr, 2.5f);
        double total = totalTicks(), vis = (getWidth() - timelineX()) / pxPerTick();
        g.setColour (col::textDark);
        g.fillRoundedRectangle (sr.withX (sr.getX() + (float) (ui.scrollTick / total) * sr.getWidth()).withWidth (juce::jmax (14.0f, (float) (vis / total) * sr.getWidth())).getIntersection (sr), 2.5f);
    }

    void resized() override
    {
        listBtn.setBounds (4, 2, 28, 24);
        patternBtn.setBounds (4, 28, 28, 24);
        waveBtn.setBounds (4, 62, 28, 24);
        speakerBtn.setBounds (iconW + 4, 30, 20, 20);
        padsBtn.setBounds (iconW + 28, 30, 22, 20);
        int my = rowsBottom();
        paletteBtn.setBounds (4, my + 4, 28, 24);
        ccBtn.setBounds (4, my + 30, 28, 24);
        collapseBtn.setBounds (4, getHeight() - footerH, 28, footerH);
        pencilBtn.setBounds (timelineX() - 26, getHeight() - footerH + 3, 22, 20);
        laneBtns[0]->setBounds (iconW + 6, my + 6, 100, 20);
        laneBtns[1]->setBounds (iconW + 6, my + 28, 100, 20);
        addLaneBtn.setBounds (iconW + 6, my + 52, 22, 22);
        bool showLane = ! ui.editorCollapsed && ui.editorMode == 0;
        for (auto* b : laneBtns) b->setVisible (showLane);
        addLaneBtn.setVisible (showLane);
        paletteBtn.setVisible (showLane); ccBtn.setVisible (showLane);
        grid.setVisible (ui.editorMode == 0);
        lane.setVisible (showLane);
        sampling.setVisible (ui.editorMode == 1);
        grid.rowH = rowH();
        grid.rowOffset = ui.keyboardView ? 0 : drumScroll();
        grid.setBounds (timelineX(), rowsTop(), getWidth() - timelineX(), rowsBottom() - rowsTop());
        lane.setBounds (timelineX(), my + 1, getWidth() - timelineX(), modH() - 1);
        sampling.setBounds (timelineX(), 0, getWidth() - timelineX(), getHeight() - footerH);
    }

    // ---------------------------------------------------------------- mouse on list / pads / ruler / clip strip
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        if (e.x >= iconW && e.x < timelineX() && e.y < 28) { actions::groupMenu (engine, ui, engine.focusedGroup, this); return; }
        if (e.y >= getHeight() - footerH)
        {
            if (e.x >= iconW && e.x < iconW + 90)
            {
                juce::PopupMenu m;
                for (int i = 0; i < Engine::gridChoices().size(); ++i) m.addItem (i + 1, Engine::gridChoices()[i], true, i == engine.stepGrid);
                showMenu (m, this, [this] (int r) { engine.stepGrid = r - 1; ui.changed(); });
            }
            else if (e.x >= timelineX()) { scrollDrag = true; scrollStart = ui.scrollTick; }
            return;
        }
        if (e.x >= iconW && e.x < timelineX() - pianoW && e.y >= rowsTop() && e.y < rowsBottom())
        {
            if (ui.padView) { padMouseDown (e, *G); return; }
            int si = soundAtY (e.y);
            if (! juce::isPositiveAndBelow (si, numPads)) return;
            auto& s = *G->sounds[(size_t) si];
            // mute / solo hot zones at the right of the row
            int rx = timelineX() - pianoW;
            if (e.x > rx - 36 && e.x < rx - 20 && isMouseOverRow (si)) { s.mute = ! s.mute; repaint(); return; }
            if (e.x > rx - 18 && e.x < rx - 2 && isMouseOverRow (si)) { s.solo = ! s.solo; repaint(); return; }
            engine.selectSound (engine.focusedGroup, si);
            if (e.mods.isPopupMenu()) actions::soundMenu (engine, ui, engine.focusedGroup, si, this);
            else if (e.getNumberOfClicks() >= 2) actions::askText ("Rename Sound", s.name, [this, si] (juce::String t) { engine.snapshot ("Rename Sound"); engine.project().group (engine.focusedGroup)->sounds[(size_t) si]->name = t; ui.changed(); });
            else if (e.mods.isAltDown()) { engine.triggerPad (engine.focusedGroup, si, 0.8f, true); auditioning = si; }
            ui.changed();
            return;
        }
        if (e.x >= timelineX() - pianoW && e.x < timelineX() && e.y >= rowsTop() && e.y < rowsBottom())
        {
            // piano strip: audition
            int note = pianoTopNote() - (e.y - rowsTop()) / pianoKeyH();
            engine.triggerPad (engine.focusedGroup, G->focusedSound, 0.8f, true, note);
            auditioning = G->focusedSound; auditionPitch = note;
            return;
        }
        if (ui.editorMode == 0 && e.x >= timelineX() && e.y < rulerH)
        {
            rulerDrag = true; rulerZoom0 = pxPerTick(); rulerScroll0 = ui.scrollTick; rulerTick0 = ui.scrollTick + (e.x - timelineX()) / pxPerTick();
            double t = ui.scrollTick + (e.x - timelineX()) / pxPerTick();
            if (! engine.songMode) t = std::fmod (t, juce::jmax (1.0, engine.displayLoopLength()));
            engine.position = juce::jmax (0.0, std::floor (t / Engine::gridTicks (engine.stepGrid)) * Engine::gridTicks (engine.stepGrid));
            return;
        }
        if (ui.editorMode == 0 && e.x >= timelineX() && e.y >= rulerH && e.y < topH) clipStripMouseDown (e, *G);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (rulerDrag)
        {
            // drag down on the ruler = zoom in, up = zoom out, sideways = scroll
            double z = juce::jlimit (0.005, 2.0, rulerZoom0 * std::pow (1.01, (double) e.getDistanceFromDragStartY()));
            ui.pxPerTick = z;
            ui.scrollTick = juce::jmax (0.0, rulerTick0 - (e.getMouseDownX() - timelineX()) / z - e.getDistanceFromDragStartX() / z);
            resized(); repaintArea();
            return;
        }
        if (scrollDrag)
        {
            ui.scrollTick = juce::jlimit (0.0, totalTicks(), scrollStart + e.getDistanceFromDragStartX() / (double) scrollRect().getWidth() * totalTicks());
            repaintArea();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        scrollDrag = false; rulerDrag = false;
        if (auditioning >= 0) engine.triggerPad (engine.focusedGroup, auditioning, 0, false, auditionPitch);
        auditioning = -1; auditionPitch = -1;
    }

    void mouseMove (const juce::MouseEvent& e) override { hoverRow = soundAtY (e.y); if (e.x >= timelineX() || e.y < rowsTop()) hoverRow = -1; repaint (iconW, rowsTop(), listW, rowsBottom()); }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }

    // Mouse wheel (all timeline windows work the same way):
    //   wheel = scroll up/down, Shift+wheel = scroll left/right,
    //   Ctrl+wheel = zoom horizontally, Alt+wheel (or Ctrl+Shift+wheel) = zoom vertically
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.eventComponent != &grid && e.eventComponent != &lane && e.eventComponent != this) return;
        float dy = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : 0.0f;
        bool vZoom = e.mods.isAltDown() || (e.mods.isCommandDown() && e.mods.isShiftDown());
        auto pos = e.getEventRelativeTo (this).position;
        if (vZoom) { if (pos.y >= rowsBottom() && ! ui.editorCollapsed) zoomLane (dy > 0 ? 1 : -1); else zoomVertical (dy > 0 ? 1 : -1, (int) pos.y); }
        else if (e.mods.isCommandDown()) zoomHorizontal (dy > 0 ? 1.15 : 1.0 / 1.15, pos.x - timelineX());
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            ui.scrollTick = juce::jmax (0.0, ui.scrollTick - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 200.0 / pxPerTick() * 0.25);
        else if (ui.keyboardView) kbScroll = juce::jlimit (-127, 127, kbScroll + (dy > 0 ? 2 : -2));
        else if (maxDrumScroll() > 0) ui.drumScroll = juce::jlimit (0, maxDrumScroll(), drumScroll() + (dy > 0 ? -1 : 1));
        else ui.scrollTick = juce::jmax (0.0, ui.scrollTick - dy * 200.0 / pxPerTick() * 0.25);
        resized();
        repaintArea();
    }

public:
    void zoomHorizontal (double factor, double anchorX = 0)
    {
        if (ui.editorMode == 1) { sampling.zoomH (factor > 1.0); return; }
        double t = ui.scrollTick + anchorX / pxPerTick();
        ui.pxPerTick = juce::jlimit (0.005, 2.0, pxPerTick() * factor);
        ui.scrollTick = juce::jmax (0.0, t - anchorX / ui.pxPerTick);
        resized(); repaintArea();
    }
    void zoomVertical (int dir, int anchorY = -1)
    {
        if (ui.editorMode == 1) { sampling.zoomV (dir); return; }
        if (ui.keyboardView)
        {
            int topBefore = pianoTopNote();
            int kh = pianoKeyH();
            int anchorRow = anchorY >= rowsTop() ? (anchorY - rowsTop()) / kh : 0;
            int anchorNote = topBefore - anchorRow;
            ui.keyH = juce::jlimit (4, 48, kh + dir * juce::jmax (1, kh / 6));
            // keep the note under the mouse in place
            int keys = (rowsBottom() - rowsTop()) / pianoKeyH();
            int newAnchorRow = anchorY >= rowsTop() ? (anchorY - rowsTop()) / pianoKeyH() : 0;
            kbScroll = 0;
            int base = pianoTopNote();
            kbScroll = juce::jlimit (-127, 127, (anchorNote + newAnchorRow) - base);
            juce::ignoreUnused (keys);
        }
        else
        {
            int rh = soundRowH();
            int anchorSound = anchorY >= rowsTop() ? soundAtY (anchorY) : drumScroll();
            ui.drumRowH = juce::jlimit (10, 80, rh + dir * juce::jmax (2, rh / 6));
            int rowsVis = (rowsBottom() - rowsTop()) / soundRowH();
            int anchorRow = anchorY >= rowsTop() ? (anchorY - rowsTop()) / soundRowH() : 0;
            ui.drumScroll = juce::jlimit (0, maxDrumScroll(), anchorSound - juce::jmin (anchorRow, rowsVis - 1));
        }
        resized(); repaintArea();
    }
    void zoomLane (int dir) { ui.laneH = juce::jlimit (50, juce::jmax (50, getHeight() / 2), ui.laneH + dir * 16); resized(); repaintArea(); }
    void zoomReset() { sampling.zoomReset(); ui.pxPerTick = 0; ui.scrollTick = 0; ui.drumRowH = 0; ui.drumScroll = 0; ui.keyH = 0; kbScroll = 0; ui.laneH = 88; resized(); repaintArea(); }
public:

    // drag & drop from Explorer / Browser onto the sound list
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override { dropFiles (files, x, y); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("file:"); }
    void itemDropped (const SourceDetails& d) override { dropFiles ({ d.description.toString().fromFirstOccurrenceOf ("file:", false, false) }, d.localPosition.x, d.localPosition.y); }

    NoteGrid& getGrid() { return grid; }

private:
    Engine& engine;
    Ui& ui;
    IconButton listBtn { icons::listIcon }, patternBtn { icons::patternIcon }, waveBtn { icons::waveIcon };
    IconButton paletteBtn { icons::palette }, ccBtn { icons::ccIcon }, collapseBtn { icons::chevronUp };
    IconButton speakerBtn { icons::speaker }, padsBtn { icons::padsIcon }, pencilBtn { icons::pencil };
    IconButton addLaneBtn { icons::plus };
    juce::OwnedArray<FlatButton> laneBtns;
    NoteGrid grid;
    ModLane lane;
    SamplingView sampling;
    int kbScroll = 0;
    int hoverRow = -1;
    int auditioning = -1, auditionPitch = -1;
    bool scrollDrag = false; double scrollStart = 0;
    bool rulerDrag = false; double rulerZoom0 = 0, rulerScroll0 = 0, rulerTick0 = 0;
    std::array<int, numPads> lastTrig {};
    std::array<float, numPads> padFlash {};
    int lastFocusedSound = -1;
    int lastClipKey = -1;

    bool isMouseOverRow (int si) const { return hoverRow == si; }
    juce::Rectangle<float> scrollRect() const { return { (float) timelineX() + 4, (float) getHeight() - footerH + 10, (float) getWidth() - timelineX() - 30, 5.0f }; }
    double totalTicks()
    {
        auto& P = engine.project();
        double t = 16.0 * P.ticksPerBar();
        if (auto* p = engine.editPattern (engine.focusedGroup)) t = juce::jmax (t, (double) (regionStart() + p->length) + 4.0 * P.ticksPerBar());
        return juce::jmax (t, (double) P.songLength() + 4.0 * P.ticksPerBar());
    }
    void repaintArea() { repaint(); grid.repaint(); lane.repaint(); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        listBtn.setToggleState (ui.editorMode == 0 && ! ui.keyboardView, juce::dontSendNotification);
        patternBtn.setToggleState (ui.editorMode == 0 && ui.keyboardView, juce::dontSendNotification);
        padsBtn.setToggleState (ui.padView, juce::dontSendNotification);
        waveBtn.setToggleState (ui.editorMode == 1, juce::dontSendNotification);
        pencilBtn.setToggleState (ui.pencil, juce::dontSendNotification);
        collapseBtn.icon = ui.editorCollapsed ? icons::chevronDown : icons::chevronUp;
        laneBtns[0]->setToggleState (ui.laneMode == 0, juce::dontSendNotification);
        laneBtns[1]->setToggleState (ui.laneMode == 1, juce::dontSendNotification);
        sampling.update();
        // keep the selected clip in view (song mode)
        if (engine.songMode)
        {
            auto rs = (double) regionStart();
            int key = engine.focusedGroup * 100000 + engine.editClip (engine.focusedGroup);
            double vis = (getWidth() - timelineX()) / pxPerTick();
            if (key != lastClipKey || rs < ui.scrollTick || rs > ui.scrollTick + vis * 0.9) ui.scrollTick = juce::jmax (0.0, rs);
            lastClipKey = key;
        }
        else if (ui.scrollTick > 0 && engine.editPattern (engine.focusedGroup) != nullptr && ui.scrollTick > (double) engine.editPattern (engine.focusedGroup)->length) ui.scrollTick = 0;
        rebuildLaneButtons();
        resized();
        repaintArea();
    }

    void rebuildLaneButtons()
    {
        while (laneBtns.size() > 2) { removeChildComponent (laneBtns.getLast()); laneBtns.removeLast(); }
        auto* G = engine.project().group (engine.focusedGroup);
        auto* p = engine.editPattern (engine.focusedGroup);
        if (G == nullptr || p == nullptr) { if (ui.laneMode > 1) ui.laneMode = 0; return; }
        int k = 0;
        int x = iconW + 110;
        for (auto& l : p->lanes)
        {
            if (l.sound != G->focusedSound) continue;
            int mode = 2 + k;
            auto* b = laneBtns.add (new FlatButton (l.name, 11.5f));
            b->style = FlatButton::menuItem;
            b->setToggleState (ui.laneMode == mode, juce::dontSendNotification);
            b->onClick = [this, mode] { ui.laneMode = mode; ui.changed(); };
            addAndMakeVisible (b);
            b->setBounds (x, rowsBottom() + 6 + (k % 3) * 22, 52, 20);
            if (k % 3 == 2) x += 54;
            ++k;
        }
        if (ui.laneMode >= 2 + k) ui.laneMode = 0;
        for (int i = 2; i < laneBtns.size(); ++i) laneBtns[i]->setVisible (! ui.editorCollapsed && ui.editorMode == 0);
    }

    void addLaneMenu()
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        auto& s = *G->sounds[(size_t) G->focusedSound];
        juce::PopupMenu m;
        m.addItem (1, "Level");
        m.addItem (2, "Pan");
        std::map<int, std::pair<juce::String, juce::String>> ids;
        int id = 100;
        for (int si = 0; si < (int) s.slots.size(); ++si)
        {
            juce::PopupMenu sub;
            if (auto* proc = s.slots[(size_t) si]->proc.get())
                for (int pi = 0; pi < juce::jmin (64, proc->getParameters().size()); ++pi)
                {
                    ids[++id] = { "slot:" + juce::String (si) + ":" + juce::String (pi), proc->getParameters()[pi]->getName (10) };
                    sub.addItem (id, proc->getParameters()[pi]->getName (40));
                }
            m.addSubMenu (engine.describeSlot (*s.slots[(size_t) si]), sub);
        }
        showMenu (m, &addLaneBtn, [this, ids] (int r)
        {
            auto* G = engine.project().group (engine.focusedGroup);
            auto* p = grid.ensurePattern();
            if (G == nullptr || p == nullptr) return;
            AutoLane l; l.sound = G->focusedSound;
            if (r == 1) { l.target = "level"; l.name = "Level"; }
            else if (r == 2) { l.target = "pan"; l.name = "Pan"; }
            else if (auto it = ids.find (r); it != ids.end()) { l.target = it->second.first; l.name = it->second.second; }
            else return;
            engine.snapshot ("Add Lane");
            const juce::ScopedLock sl (engine.lock);
            p->lanes.push_back (l);
            int k = 0; for (auto& x : p->lanes) if (x.sound == G->focusedSound) ++k;
            ui.laneMode = 1 + k;
            ui.changed();
        });
    }

    // ---------------------------------------------------------------- painting helpers
    void paintSoundList (juce::Graphics& g, Group& G)
    {
        int rh = soundRowH();
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (iconW, rowsTop(), listW, rowsBottom() - rowsTop());
        for (int si = drumScroll(); si < numPads; ++si)
        {
            auto& s = *G.sounds[(size_t) si];
            int y = rowsTop() + (si - drumScroll()) * rh;
            if (y >= rowsBottom()) break;
            bool foc = si == G.focusedSound;
            auto r = juce::Rectangle<int> (iconW, y, listW - pianoW, rh);
            g.setColour (foc ? col::rowSel : col::row);
            g.fillRect (r);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
            auto numR = r.removeFromLeft (30);
            float flash = padFlash[(size_t) si];
            if (foc) { g.setColour (col::orange); g.fillRect (numR); }
            else if (flash > 0) { g.setColour (s.colour.withAlpha (flash * 0.6f)); g.fillRect (numR); }
            g.setColour (foc ? juce::Colours::white : col::text);
            g.setFont (font (12.0f));
            g.drawText (juce::String (si + 1), numR, juce::Justification::centred);
            g.setColour (s.colour);
            g.fillRect (r.removeFromLeft (4).reduced (0, 1));
            g.setColour (s.mute ? col::textDark : (foc ? juce::Colours::white : (s.slots.empty() ? col::textDim : col::text)));
            g.setFont (font (12.5f, foc));
            g.drawText (s.name, r.reduced (6, 0).withTrimmedRight (36), juce::Justification::centredLeft, true);
            // mute / solo indicators
            auto ms = r.removeFromRight (36).reduced (0, (rh - 14) / 2);
            if (s.mute || isMouseOverRow (si)) { g.setColour (s.mute ? col::orange : col::textDark); g.setFont (font (10.0f, true)); g.drawText ("M", ms.removeFromLeft (16), juce::Justification::centred); }
            else ms.removeFromLeft (16);
            if (s.solo || isMouseOverRow (si)) { g.setColour (s.solo ? juce::Colour (0xfff0d040) : col::textDark); g.setFont (font (10.0f, true)); g.drawText ("S", ms.removeFromLeft (16), juce::Justification::centred); }
        }
    }

    // vertical mini keyboard next to the sound list (piano roll keys in keyboard view)
    int pianoKeyH() const { return ui.keyH > 0 ? juce::jlimit (4, 48, ui.keyH) : juce::jmax (4, (rowsBottom() - rowsTop()) / 64); }   // ~5 octaves like the original
    int pianoTopNote() const
    {
        auto* G = engine.project().group (engine.focusedGroup);
        int root = G ? G->sounds[(size_t) G->focusedSound]->rootKey : 60;
        int keys = (rowsBottom() - rowsTop()) / pianoKeyH();
        return juce::jlimit (keys, 127, root + keys / 2 + kbScroll);
    }

    void paintPiano (juce::Graphics& g)
    {
        int x = timelineX() - pianoW;
        int kh = pianoKeyH();
        auto* G = engine.project().group (engine.focusedGroup);
        int root = G ? G->sounds[(size_t) G->focusedSound]->rootKey : 60;
        g.setColour (col::pianoWhite);
        g.fillRect (x, rowsTop(), pianoW, rowsBottom() - rowsTop());
        int top = pianoTopNote();
        for (int k = 0; ; ++k)
        {
            int y = rowsTop() + k * kh;
            if (y >= rowsBottom()) break;
            int note = top - k;
            if (note < 0) break;
            if (juce::MidiMessage::isMidiNoteBlack (note)) { g.setColour (col::pianoBlack); g.fillRect (x, y + (kh > 6 ? 2 : 0), pianoW - 8, juce::jmax (2, kh - (kh > 6 ? 4 : 1))); }
            else if (note % 12 == 4 || note % 12 == 11) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (x, y, pianoW, 1); }
            if (note == root) { g.setColour (col::orange); g.fillRect (x, y, 8, juce::jmax (3, kh)); }
            if (note % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillRect (x, y + kh - 1, pianoW, 1);
                g.setColour (juce::Colours::black);
                g.setFont (font (9.5f));
                g.drawText (juce::String (note / 12 - 2), x + 9, y + kh - 12, pianoW - 10, 11, juce::Justification::centredLeft);
            }
        }
    }

    void paintPads (juce::Graphics& g, Group& G)
    {
        int x0 = iconW + 26, y0 = rowsTop() + 4;
        int ps = 25;
        for (int row = 0; row < 4; ++row)
            for (int c = 0; c < 4; ++c)
            {
                int si = (3 - row) * 4 + c;
                auto r = juce::Rectangle<int> (x0 + c * (ps + 2), y0 + row * (ps + 2), ps, ps);
                auto& s = *G.sounds[(size_t) si];
                float flash = padFlash[(size_t) si];
                g.setColour (flash > 0 ? s.colour.withAlpha (0.3f + flash * 0.7f) : (s.slots.empty() ? col::button : col::buttonHi));
                g.fillRect (r);
                if (si == G.focusedSound) { g.setColour (juce::Colours::white); g.drawRect (r, 1); }
                g.setColour (col::textDim);
                g.setFont (font (10.0f));
                g.drawText (juce::String (si + 1), r.reduced (3), juce::Justification::topLeft);
            }
        auto& s = *G.sounds[(size_t) G.focusedSound];
        int y = y0 + 4 * (ps + 2) + 12;
        auto line = [&] (const juce::String& k, const juce::String& v1, const juce::String& v2)
        {
            g.setColour (col::textDim); g.setFont (font (11.5f));
            g.drawText (k, iconW + 16, y, 40, 16, juce::Justification::centredLeft);
            g.setColour (col::text);
            g.drawText (v1, iconW + 58, y, 50, 16, juce::Justification::centredLeft);
            g.drawText (v2, iconW + 106, y, 54, 16, juce::Justification::centredLeft);
            y += 18;
        };
        const juce::StringArray modes { "Send", "Receive", "Both" };
        line ("Key", noteName (s.rootKey), {});
        line ("Choke", s.chokeGroup == 0 ? "None" : juce::String (s.chokeGroup), modes[s.chokeMode]);
        line ("Link", s.linkGroup == 0 ? "None" : juce::String (s.linkGroup), modes[s.linkMode]);
        line ("Focus", s.focusMode == 0 ? "On Trigger" : "Pinned", {});
        line ("Trigger", noteName (s.triggerNote >= 0 ? s.triggerNote : 36 + G.focusedSound), "MIDI");
        g.setColour (engine.keyboardMode ? col::orange : col::textDark);
        g.setFont (font (11.0f));
        g.drawText (engine.keyboardMode ? "KEYBOARD MODE" : "PAD MODE", iconW + 16, y + 4, 140, 14, juce::Justification::centredLeft);
    }

    void padMouseDown (const juce::MouseEvent& e, Group& G)
    {
        int x0 = iconW + 26, y0 = rowsTop() + 4, ps = 25;
        int c = (e.x - x0) / (ps + 2), row = (e.y - y0) / (ps + 2);
        if (e.x >= x0 && c < 4 && e.y >= y0 && row < 4)
        {
            int si = (3 - row) * 4 + c;
            engine.selectSound (engine.focusedGroup, si);
            if (e.mods.isPopupMenu()) { actions::soundMenu (engine, ui, engine.focusedGroup, si, this); return; }
            float vel = juce::jlimit (0.1f, 1.0f, 1.0f - (float) ((e.y - y0) % (ps + 2)) / ps * 0.7f);
            engine.triggerPad (engine.focusedGroup, si, vel, true);
            auditioning = si;
            ui.changed();
            return;
        }
        int y = y0 + 4 * (ps + 2) + 12;
        int line = (e.y - y) / 18;
        if (e.y < y || line > 4) return;
        auto& s = *G.sounds[(size_t) G.focusedSound];
        bool second = e.x >= iconW + 106;
        juce::PopupMenu m;
        if (line == 0) for (int n = 24; n <= 108; ++n) m.addItem (1000 + n, noteName (n), true, n == s.rootKey);
        else if (line == 1 || line == 2)
        {
            int cur = line == 1 ? (second ? s.chokeMode : s.chokeGroup) : (second ? s.linkMode : s.linkGroup);
            if (second) { const char* md[] = { "Send", "Receive", "Both" }; for (int i = 0; i < 3; ++i) m.addItem (2000 + i, md[i], true, i == cur); }
            else { m.addItem (3000, "None", true, cur == 0); for (int i = 1; i <= 16; ++i) m.addItem (3000 + i, juce::String (i), true, i == cur); }
        }
        else if (line == 3) { m.addItem (4000, "On Trigger", true, s.focusMode == 0); m.addItem (4001, "Pinned", true, s.focusMode == 1); }
        else if (line == 4) for (int n = 0; n <= 127; ++n) m.addItem (5000 + n, noteName (n) + " (" + juce::String (n) + ")", true, n == (s.triggerNote >= 0 ? s.triggerNote : 36 + G.focusedSound));
        showMenu (m, this, [this, line, second] (int r)
        {
            auto* G2 = engine.project().group (engine.focusedGroup);
            if (G2 == nullptr) return;
            auto& s2 = *G2->sounds[(size_t) G2->focusedSound];
            engine.snapshot ("Pad Settings");
            if (r >= 5000) s2.triggerNote = r - 5000;
            else if (r >= 4000) s2.focusMode = r - 4000;
            else if (r >= 3000) { if (line == 1) s2.chokeGroup = r - 3000; else s2.linkGroup = r - 3000; }
            else if (r >= 2000) { if (line == 1) s2.chokeMode = r - 2000; else s2.linkMode = r - 2000; }
            else if (r >= 1000) s2.rootKey = r - 1000;
            juce::ignoreUnused (second);
            repaint();
        });
    }

    void paintRuler (juce::Graphics& g)
    {
        int tx = timelineX();
        g.setColour (col::ruler);
        g.fillRect (tx, 0, getWidth() - tx, rulerH);
        auto& P = engine.project();
        double beat = (double) PPQ * 4 / P.sigDen, bar = (double) P.ticksPerBar();
        double t0 = ui.scrollTick, t1 = ui.scrollTick + (getWidth() - tx) / pxPerTick();
        double labelStep = beat;
        while (labelStep * pxPerTick() < 36) labelStep *= 2;
        for (double t = std::floor (t0 / labelStep) * labelStep; t < t1; t += labelStep)
        {
            float x = (float) (tx + (t - ui.scrollTick) * pxPerTick());
            if (x < tx) continue;
            bool isBar = std::fmod (t, bar) < 0.5;
            g.setColour (isBar ? col::textDim : col::textDark);
            g.fillRect (x, isBar ? 0.0f : 8.0f, 1.0f, isBar ? (float) rulerH : 8.0f);
            int b = (int) (t / bar) + 1, bt = (int) (std::fmod (t, bar) / beat) + 1;
            g.setFont (font (11.0f));
            g.setColour (isBar ? col::text : col::textDim);
            g.drawText (isBar ? juce::String (b) : juce::String (b) + "." + juce::String (bt), (int) x + 4, 0, 40, rulerH, juce::Justification::centredLeft);
        }
    }

    void paintClipStrip (juce::Graphics& g, Group& G)
    {
        int tx = timelineX();
        auto strip = juce::Rectangle<int> (tx, rulerH, getWidth() - tx, topH - rulerH);
        g.setColour (col::editorBg);
        g.fillRect (strip);
        // grid ticks in the strip
        double step = PPQ / 4.0;
        g.setColour (juce::Colour (0xff2a2a2a));
        for (double t = std::floor (ui.scrollTick / step) * step; ; t += step)
        {
            float x = (float) (tx + (t - ui.scrollTick) * pxPerTick());
            if (x > getWidth()) break;
            if (x >= tx) g.fillRect (x, (float) strip.getY() + 4, 1.0f, (float) strip.getHeight() - 8);
        }
        auto drawBox = [&] (juce::Rectangle<float> r, const juce::String& text, bool sel, juce::Colour c)
        {
            g.setColour (sel ? (c == col::orange ? col::orangeClip : c) : (c == col::orange ? col::orangeClip : c).withAlpha (0.45f));
            g.fillRoundedRectangle (r, 2.0f);
            if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.5f); }
            g.setColour (juce::Colour (0xff2a1408));
            g.setFont (font (12.5f));
            g.drawText (text, r.reduced (6, 0), juce::Justification::centredLeft, true);
        };
        if (engine.songMode)
        {
            int sel = engine.editClip (engine.focusedGroup);
            for (int ci = 0; ci < (int) G.clips.size(); ++ci)
            {
                auto& c = G.clips[(size_t) ci];
                float x = (float) (tx + ((double) c.start - ui.scrollTick) * pxPerTick());
                auto r = juce::Rectangle<float> (x, (float) strip.getY() + 2, (float) (c.length * pxPerTick()), (float) strip.getHeight() - 4);
                auto* p = G.pattern (c.pattern);
                drawBox (r, p ? p->name : "Clip " + juce::String (ci + 1), ci == sel, p ? p->colour : G.colour);
            }
        }
        else if (auto* p = engine.editPattern (engine.focusedGroup))
        {
            // Ideas view: pattern name in orange like the original
            g.setColour (col::orange);
            g.setFont (font (12.5f));
            g.drawText (p->name, tx + 6, strip.getY(), 300, strip.getHeight(), juce::Justification::centredLeft);
        }
        // start / length info at the right
        if (auto* p = engine.editPattern (engine.focusedGroup))
        {
            auto info = strip.removeFromRight (190).reduced (4, 0);
            g.setColour (col::editorBg.withAlpha (0.85f));
            g.fillRect (info);
            g.setFont (font (10.0f));
            g.setColour (col::textDim);
            g.drawText ("Start", info.getX(), info.getY(), 30, info.getHeight(), juce::Justification::centredLeft);
            g.drawText ("Length", info.getX() + 80, info.getY(), 40, info.getHeight(), juce::Justification::centredLeft);
            g.setFont (font (11.5f, true));
            g.setColour (col::text);
            g.drawText (formatTicks (regionStart(), engine.project(), true), info.getX() + 32, info.getY(), 46, info.getHeight(), juce::Justification::centredLeft);
            g.drawText (formatTicks (p->length, engine.project(), false), info.getX() + 120, info.getY(), 60, info.getHeight(), juce::Justification::centredLeft);
        }
    }

    void clipStripMouseDown (const juce::MouseEvent& e, Group& G)
    {
        int tx = timelineX();
        if (e.x > getWidth() - 110 && engine.editPattern (engine.focusedGroup) != nullptr)
        {
            juce::PopupMenu m;
            for (int b : { 1, 2, 4, 8, 16, 32 }) m.addItem (b, juce::String (b) + (b == 1 ? " Bar" : " Bars"));
            m.addItem (100, "1 Beat"); m.addItem (101, "2 Beats"); m.addItem (102, "3 Beats");
            showMenu (m, this, [this] (int r)
            {
                auto* G2 = engine.project().group (engine.focusedGroup);
                auto* p = engine.editPattern (engine.focusedGroup);
                if (p == nullptr || G2 == nullptr) return;
                engine.snapshot ("Pattern Length");
                auto& P = engine.project();
                auto newLen = r >= 100 ? (juce::int64) (r - 99) * PPQ * 4 / P.sigDen : (juce::int64) r * P.ticksPerBar();
                const juce::ScopedLock sl (engine.lock);
                if (engine.songMode)
                {
                    int ci = engine.editClip (engine.focusedGroup);
                    if (juce::isPositiveAndBelow (ci, (int) G2->clips.size()) && G2->clips[(size_t) ci].length == p->length) G2->clips[(size_t) ci].length = newLen;
                }
                p->length = newLen;
                ui.changed();
            });
            return;
        }
        if (! engine.songMode) return;
        double t = ui.scrollTick + (e.x - tx) / pxPerTick();
        for (int ci = 0; ci < (int) G.clips.size(); ++ci)
        {
            auto& c = G.clips[(size_t) ci];
            if (t >= c.start && t < c.start + c.length) { engine.setEditClip (engine.focusedGroup, ci); ui.changed(); return; }
        }
    }

    void dropFiles (const juce::StringArray& files, int x, int y)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr || files.isEmpty()) return;
        int si = G->focusedSound;
        if (! ui.padView && x < timelineX() && y >= rowsTop()) si = juce::jlimit (0, numPads - 1, soundAtY (y));
        for (auto& f : files)
        {
            actions::loadFileFromBrowser (engine, ui, juce::File (f), engine.focusedGroup, si);
            si = juce::jmin (numPads - 1, si + 1);
        }
        engine.selectSound (engine.focusedGroup, juce::jlimit (0, numPads - 1, si - (files.size() > 1 ? 1 : 0)));
        ui.changed();
    }

    void timerCallback() override
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        bool flashChanged = false;
        for (int i = 0; i < numPads; ++i)
        {
            int t = G->sounds[(size_t) i]->lastTrigger.load();
            if (t != lastTrig[(size_t) i]) { lastTrig[(size_t) i] = t; padFlash[(size_t) i] = 1.0f; flashChanged = true; }
            else if (padFlash[(size_t) i] > 0) { padFlash[(size_t) i] = juce::jmax (0.0f, padFlash[(size_t) i] - 0.12f); flashChanged = true; }
        }
        if (G->focusedSound != lastFocusedSound) { lastFocusedSound = G->focusedSound; ui.changed(); }
        speakerBtn.setToggleState (G->mute, juce::dontSendNotification);
        if (flashChanged) repaint (iconW, rowsTop(), listW, rowsBottom() - rowsTop());
        if (engine.playing)
        {
            // follow playhead
            if (engine.followOn && ui.editorMode == 0)
            {
                double pos = engine.position;
                if (! engine.songMode) if (auto* p = engine.editPattern (engine.focusedGroup)) pos = std::fmod (pos, (double) std::max<juce::int64> (1, p->length));
                double vis = (getWidth() - timelineX()) / pxPerTick();
                if (pos > ui.scrollTick + vis * 0.95 || pos < ui.scrollTick) { ui.scrollTick = juce::jmax (0.0, engine.songMode ? pos - vis * 0.05 : 0.0); repaint(); }
            }
            grid.repaint();
        }
    }
};
} // namespace gb
