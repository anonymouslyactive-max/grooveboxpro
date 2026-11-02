#pragma once
#include "Actions.h"

namespace gb
{
// Top section: Song view (clip timeline per group) and Ideas view (scenes x groups)
class ArrangerPanel : public juce::Component, private juce::Timer
{
public:
    ArrangerPanel (Engine& e, Ui& u) : engine (e), ui (u)
    {
        addAndMakeVisible (viewBtn);
        viewBtn.onClick = [this] { engine.songMode = ! engine.songMode; ui.changed(); };
        viewBtn.setTooltip ("Switch Ideas / Song view (Ctrl+1 / Ctrl+2)");
        startTimerHz (30);
    }

    static constexpr int iconW = 36, listW = 165, rulerH = 18, markerH = 34, footerH = 24;
    int rowH() const { return juce::jlimit (16, 160, ui.arrRowH); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::header);
        g.fillRect (0, 0, iconW, getHeight());
        g.setColour (juce::Colours::black);
        g.fillRect (iconW - 1, 0, 1, getHeight());
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        if (engine.songMode) paintSong (g); else paintIdeas (g);
    }

    void resized() override { viewBtn.setBounds (4, 4, 28, 26); }

    void mouseDown (const juce::MouseEvent& e) override { if (engine.songMode) songMouseDown (e); else ideasMouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (engine.songMode) songMouseDrag (e); else ideasMouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! engine.songMode) ideasMouseUp (e); drag = DragNone; band = {}; dragOrig.clear(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (engine.songMode) songDoubleClick (e); else ideasDoubleClick (e); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! engine.songMode) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
        if (e.y >= rulerH && e.y < groupsTop() && e.x >= timelineX())
        {
            setMouseCursor (sectionEdgeAt ((float) e.x) != nullptr ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            return;
        }
        auto hit = hitClip (e.position);
        setMouseCursor (hit.second == 2 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    // wheel = scroll up/down, Shift+wheel = left/right, Ctrl+wheel = zoom horizontally, Alt+wheel = zoom vertically
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        float dy = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : 0.0f;
        bool vZoom = e.mods.isAltDown() || (e.mods.isCommandDown() && e.mods.isShiftDown());
        if (vZoom) { zoomVertical (dy > 0 ? 1 : -1); return; }
        if (! engine.songMode)
        {
            if (e.mods.isCommandDown()) zoomHorizontal (dy > 0 ? 1.15 : 1.0 / 1.15, (float) e.x);
            return;
        }
        if (e.mods.isCommandDown()) zoomHorizontal (dy > 0 ? 1.15 : 1.0 / 1.15, (float) e.x);
        else if (! e.mods.isShiftDown() && w.deltaX == 0 && maxScrollY() > 0 && e.x >= iconW)
            ui.arrScrollY = juce::jlimit (0, maxScrollY(), arrScrollY() - (int) (dy * 120.0f));
        else ui.arrScrollTick = juce::jmax (0.0, ui.arrScrollTick - ((w.deltaX != 0 ? w.deltaX : w.deltaY)) * 400.0 / pxPerTick() * 0.1);
        repaint();
    }

    void zoomHorizontal (double factor, float anchorX = -1)
    {
        if (! engine.songMode) { ui.ideasSceneW = juce::jlimit (50, 400, (int) std::round (sceneW() * factor)); repaint(); return; }
        if (anchorX < timelineX()) anchorX = (float) timelineX();
        double t = xToTick (anchorX);
        ui.arrPxPerTick = juce::jlimit (0.0005, 1.0, pxPerTick() * factor);
        ui.arrScrollTick = juce::jmax (0.0, t - (anchorX - timelineX()) / ui.arrPxPerTick);
        repaint();
    }
    void zoomVertical (int dir) { ui.arrRowH = juce::jlimit (16, 160, rowH() + dir * juce::jmax (2, rowH() / 6)); ui.arrScrollY = arrScrollY(); repaint(); }
    void zoomReset() { ui.arrPxPerTick = 0; ui.arrScrollTick = 0; ui.arrRowH = 24; ui.arrScrollY = 0; ui.ideasSceneW = 100; repaint(); }

private:
    Engine& engine;
    Ui& ui;
    IconButton viewBtn { icons::arranger, "Ideas / Song" };
    enum Drag { DragNone, DragMove, DragResize, DragPosition, DragScroll, DragLoop, DragBand, DragZoom, DragCells, DragSection };
    Drag drag = DragNone;
    int dragGroup = -1, dragClip = -1;
    juce::int64 dragClipStart = 0, dragClipLen = 0;
    double dragTick0 = 0, dragZoom0 = 0;
    int selSection = -1;                                              // id of the selected song section
    juce::int64 secDragStart = 0, secDragLen = 0;
    std::set<std::pair<int, int>> selClips, bandBaseClips;            // song view: (group, clip)
    std::set<std::pair<int, int>> selCells, bandBaseCells;            // ideas view: (scene, group)
    std::map<std::pair<int, int>, std::pair<juce::int64, juce::int64>> dragOrig;
    juce::Point<float> bandStart;
    juce::Rectangle<float> band;
    int cellDragScene = -1, cellDragGroup = -1; bool cellMoved = false;

    int timelineX() const { return iconW + listW; }
    double pxPerTick() const
    {
        if (ui.arrPxPerTick > 0) return ui.arrPxPerTick;
        return (getWidth() - timelineX()) / (24.0 * engine.project().ticksPerBar());   // 24 bars visible like the original
    }
    float tickToX (double t) const { return (float) (timelineX() + (t - ui.arrScrollTick) * pxPerTick()); }
    double xToTick (float x) const { return ui.arrScrollTick + (x - timelineX()) / pxPerTick(); }
    int groupsTop() const { return rulerH + markerH; }
    int groupsY() const { return groupsTop() - arrScrollY(); }
    int maxScrollY() const { return juce::jmax (0, ((int) engine.project().groups.size() + 1) * rowH() - (getHeight() - footerH - groupsTop())); }
    int arrScrollY() const { return juce::jlimit (0, maxScrollY(), ui.arrScrollY); }
    juce::int64 snap (double t) const { auto q = (juce::int64) ui.arrGridBars * engine.project().ticksPerBar(); return (juce::int64) std::floor (t / (double) q) * q; }

    // --------------------------------------------------------------------------- Song view
    void paintSong (juce::Graphics& g)
    {
        auto& P = engine.project();
        const juce::ScopedLock sl (engine.lock);
        int tx = timelineX();
        int bottom = getHeight() - footerH;
        // list column
        g.setColour (col::bg);
        g.fillRect (iconW, 0, listW, getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (tx - 1, 0, 1, getHeight());
        g.setColour (col::text);
        g.setFont (font (15.0f));
        g.drawText (P.name + (engine.dirty ? " *" : ""), iconW + 8, 0, listW - 30, rulerH + markerH - 4, juce::Justification::centredLeft);
        icons::draw (g, icons::triangleDown, { (float) tx - 22, 10.0f, 14.0f, 14.0f }, col::textDim);

        // ruler
        g.setColour (col::ruler);
        g.fillRect (tx, 0, getWidth() - tx, rulerH);
        g.setColour (col::rulerDark);
        g.fillRect (tx, rulerH, getWidth() - tx, markerH);
        auto bar = P.ticksPerBar();
        double ppt = pxPerTick();
        int barStep = juce::jmax (1, (int) std::ceil (40.0 / (bar * ppt)));
        auto firstBar = (juce::int64) std::floor (ui.arrScrollTick / bar);
        // loop range (song length) highlight in the ruler
        auto songLen = P.songLength();
        if (songLen > 0)
        {
            g.setColour (P.loopOn ? col::buttonHi : col::button);
            g.fillRect (juce::Rectangle<float> (tickToX (0), 0.0f, (float) (songLen * ppt), (float) rulerH).getIntersection ({ (float) tx, 0.0f, (float) getWidth(), (float) rulerH }));
        }
        for (auto b = firstBar; ; ++b)
        {
            float x = tickToX ((double) (b * bar));
            if (x > getWidth()) break;
            if (x < tx - 1) continue;
            g.setColour (col::gridBar.withAlpha (0.6f));
            g.fillRect (x, 0.0f, 1.0f, (float) rulerH);
            g.setColour (col::gridLine);
            for (float yy = (float) rulerH; yy < bottom; yy += 4) g.fillRect (x, yy, 1.0f, 2.0f);
            if (b % barStep == 0)
            {
                g.setColour (col::textDim);
                g.setFont (font (11.0f));
                g.drawText (juce::String (b + 1), (int) x + 4, 0, 40, rulerH, juce::Justification::centredLeft);
            }
        }
        // sections: the bar above the clips. Each one plays an Ideas scene; + adds a new one at the end
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (tx, rulerH, getWidth() - tx, markerH);
            for (auto& se : P.sections)
            {
                auto r = juce::Rectangle<float> (tickToX ((double) se.start), (float) rulerH + 1, (float) (se.length * ppt) - 1.0f, (float) markerH - 2);
                bool sel = se.id == selSection;
                g.setColour (sel ? juce::Colour (0xff4c4c4c) : juce::Colour (0xff383838));
                g.fillRect (r);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRect (r.getRight(), r.getY(), 1.0f, r.getHeight());
                if (juce::isPositiveAndBelow (se.scene, (int) P.scenes.size()))
                {
                    g.setColour (sel ? juce::Colours::white : col::text);
                    g.setFont (font (12.0f));
                    g.drawText (P.scenes[(size_t) se.scene].name, r.withLeft (juce::jmax (r.getX(), (float) tx)).reduced (6, 0).removeFromTop (18.0f), juce::Justification::centredLeft, true);
                }
            }
            auto pr = plusRect();
            icons::draw (g, icons::plus, pr, col::textDim);
        }

        // group rows
        g.saveState();
        g.reduceClipRegion (iconW, groupsTop(), getWidth() - iconW, bottom - groupsTop());
        int y = groupsY();
        int fg = engine.focusedGroup;
        for (int gi = 0; gi < (int) P.groups.size(); ++gi)
        {
            auto& G = *P.groups[(size_t) gi];
            bool foc = gi == fg;
            g.setColour (foc ? col::row : col::rowAlt);
            g.fillRect (iconW, y, getWidth() - iconW, rowH());
            g.setColour (G.colour);
            g.fillRect (iconW, y, 32, rowH());
            g.setColour (juce::Colours::white);
            g.setFont (font (12.0f));
            g.drawText (groupLetterName (gi), iconW, y, 32, rowH(), juce::Justification::centred);
            g.setColour (G.mute ? col::textDark : col::text);
            g.drawText (G.name, iconW + 40, y, listW - 44, rowH(), juce::Justification::centredLeft);
            if (G.solo) { g.setColour (col::orange); g.drawText ("S", tx - 18, y, 14, rowH(), juce::Justification::centred); }
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (iconW, y + rowH() - 1, getWidth() - iconW, 1);

            g.saveState();
            g.reduceClipRegion (tx, y, getWidth() - tx, rowH());
            for (int ci = 0; ci < (int) G.clips.size(); ++ci)
            {
                auto& c = G.clips[(size_t) ci];
                auto r = juce::Rectangle<float> (tickToX ((double) c.start), (float) y + 1, (float) (c.length * ppt), (float) rowH() - 2);
                auto* pat = G.pattern (c.pattern);
                auto colour = pat != nullptr ? pat->colour : G.colour;
                g.setColour (colour.interpolatedWith (col::orangeClip, colour == col::orange ? 1.0f : 0.0f));
                g.fillRoundedRectangle (r, 2.0f);
                // mini note preview
                if (pat != nullptr && pat->length > 0)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.25f));
                    for (juce::int64 k = 0; k * pat->length < c.length; ++k)
                        for (auto& n : pat->notes)
                        {
                            auto t = k * pat->length + n.start;
                            if (t >= c.length) continue;
                            g.fillRect ((float) (r.getX() + t * ppt), r.getY() + 4 + (n.sound % 16) * (r.getHeight() - 8) / 16.0f, juce::jmax (1.0f, (float) (n.length * ppt)), 1.0f);
                        }
                }
                bool sel = (foc && engine.editClip (gi) == ci) || selClips.count ({ gi, ci }) > 0;
                if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.5f); }
                g.setColour (juce::Colour (0xff2a1408));
                g.setFont (font (12.0f));
                g.drawText (pat != nullptr ? pat->name : "Clip " + juce::String (ci + 1), r.withLeft (juce::jmax (r.getX(), (float) tx)).reduced (5, 0), juce::Justification::centredLeft, true);
            }
            g.restoreState();
            y += rowH();
        }
        // add group
        icons::draw (g, icons::plus, { (float) iconW + 6, (float) y + 4, 14.0f, 14.0f }, col::textDark);
        // rubber band
        if (! band.isEmpty()) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect (band); g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRect (band, 1.0f); }
        g.restoreState();

        // playhead
        if (engine.playing || engine.position > 0)
        {
            double pos = engine.position;
            float x = tickToX (pos);
            if (x >= tx)
            {
                g.setColour (juce::Colours::white);
                g.fillRect (x, 0.0f, 1.0f, (float) bottom);
            }
        }
        else { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect ((float) tx, 0.0f, 1.0f, (float) bottom); }

        // footer
        g.setColour (col::bg);
        g.fillRect (iconW, bottom, getWidth() - iconW, footerH);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (iconW, bottom, getWidth() - iconW, 1);
        icons::draw (g, icons::grid, { (float) iconW + 8, (float) bottom + 5, 13.0f, 13.0f }, col::text);
        g.setColour (col::text);
        g.setFont (font (12.0f, true));
        g.drawText (juce::String (ui.arrGridBars) + (ui.arrGridBars == 1 ? " Bar" : " Bars"), iconW + 26, bottom, 80, footerH, juce::Justification::centredLeft);
        // scrollbar
        auto total = juce::jmax ((double) P.songLength() + 8 * bar, 32.0 * bar);
        auto sr = juce::Rectangle<float> ((float) tx + 4, (float) bottom + 9, (float) getWidth() - tx - 30, 5.0f);
        g.setColour (col::button);
        g.fillRoundedRectangle (sr, 2.5f);
        double vis = (getWidth() - tx) / ppt;
        auto thumb = sr.withX (sr.getX() + (float) (ui.arrScrollTick / total) * sr.getWidth()).withWidth (juce::jmax (12.0f, (float) (vis / total) * sr.getWidth()));
        g.setColour (col::textDark);
        g.fillRoundedRectangle (thumb.getIntersection (sr), 2.5f);
    }

    std::pair<int, int> hitClip (juce::Point<float> p)   // returns (clip, part) part: 1 body, 2 right edge
    {
        int gi = (int) ((p.y - groupsY()) / rowH());
        auto* G = engine.project().group (gi);
        if (G == nullptr || p.y < groupsTop() || p.y < groupsY() || p.x < timelineX() || p.y >= getHeight() - footerH) return { -1, 0 };
        for (int ci = (int) G->clips.size() - 1; ci >= 0; --ci)
        {
            auto& c = G->clips[(size_t) ci];
            float x0 = tickToX ((double) c.start), x1 = tickToX ((double) (c.start + c.length));
            if (p.x >= x0 && p.x <= x1) return { ci, p.x > x1 - 6 ? 2 : 1 };
        }
        return { -1, 0 };
    }

    void songMouseDown (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        int tx = timelineX();
        int bottom = getHeight() - footerH;
        if (e.x >= iconW && e.x < tx && e.y < groupsY())
        {
            juce::PopupMenu m;
            m.addItem (1, "Rename Project...");
            m.addItem (2, "Save");
            m.addItem (3, "Save As...");
            showMenuAt (m, juce::Desktop::getMousePosition(), [this] (int r)
            {
                if (r == 1) actions::askText ("Rename Project", engine.project().name, [this] (juce::String t) { engine.project().name = t; repaint(); });
                else if (onSave) onSave (r == 3);
            });
            return;
        }
        if (e.y >= bottom)
        {
            if (e.x < tx)
            {
                juce::PopupMenu m;
                for (int b : { 1, 2, 4, 8, 16 }) m.addItem (b, juce::String (b) + (b == 1 ? " Bar" : " Bars"), true, ui.arrGridBars == b);
                showMenuAt (m, juce::Desktop::getMousePosition(), [this] (int r) { ui.arrGridBars = r; repaint(); });
            }
            else { drag = DragScroll; dragTick0 = ui.arrScrollTick; }
            return;
        }
        if (e.x < tx)
        {
            int gi = (e.y - groupsY()) / rowH();
            if (e.y >= groupsTop() && gi < (int) P.groups.size())
            {
                engine.focusedGroup = gi;
                if (e.mods.isPopupMenu()) actions::groupMenu (engine, ui, gi, this);
                ui.changed();
            }
            else if (e.y >= groupsTop() && gi == (int) P.groups.size() && e.x < iconW + 26)
            {
                engine.snapshot ("Add Group");
                engine.addGroup();
                ui.changed();
            }
            return;
        }
        if (e.y < rulerH)
        {
            drag = DragPosition;
            engine.position = juce::jmax (0.0, (double) snap (xToTick ((float) e.x)));
            return;
        }
        if (e.y < groupsTop())
        {
            sectionMouseDown (e);
            return;
        }
        int gi = (e.y - groupsY()) / rowH();
        auto* G = P.group (gi);
        auto hit = hitClip (e.position);
        selSection = -1;
        if (G == nullptr || (hit.first < 0 && ! ui.pencil && ! e.mods.isPopupMenu()))
        {
            // empty space: drag = rubber band selection (Shift / Ctrl add to it)
            if (! (e.mods.isShiftDown() || e.mods.isCommandDown())) selClips.clear();
            bandBaseClips = selClips;
            drag = DragBand; bandStart = e.position; band = {};
            if (G != nullptr) { engine.focusedGroup = gi; ui.changed(); }
            repaint();
            return;
        }
        if (G == nullptr) return;
        engine.focusedGroup = gi;
        if (e.mods.isPopupMenu())
        {
            if (hit.first >= 0) clipMenu (gi, hit.first);
            ui.changed();
            return;
        }
        if (hit.first >= 0)
        {
            std::pair<int, int> key { gi, hit.first };
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                if (selClips.count (key)) selClips.erase (key); else selClips.insert (key);
                if (e.mods.isShiftDown()) { ui.changed(); return; }
            }
            else if (selClips.count (key) == 0) { selClips.clear(); selClips.insert (key); }
            selClips.insert (key);
            engine.setEditClip (gi, hit.first);
            engine.snapshot (hit.second == 2 ? "Resize Clip" : "Move Clip");
            if ((e.mods.isAltDown() || e.mods.isCommandDown()) && hit.second == 1)
            {
                // Alt / Ctrl-drag copies the selected clips; the copies are dragged
                const juce::ScopedLock sl (engine.lock);
                std::set<std::pair<int, int>> copies;
                for (auto& k : selClips)
                    if (auto* g2 = P.group (k.first))
                        if (juce::isPositiveAndBelow (k.second, (int) g2->clips.size()))
                        {
                            g2->clips.push_back (g2->clips[(size_t) k.second]);
                            copies.insert ({ k.first, (int) g2->clips.size() - 1 });
                            if (k == key) hit.first = (int) g2->clips.size() - 1;
                        }
                selClips = copies;
                engine.setEditClip (gi, hit.first);
            }
            drag = hit.second == 2 ? DragResize : DragMove;
            dragGroup = gi; dragClip = hit.first;
            dragClipStart = G->clips[(size_t) hit.first].start; dragClipLen = G->clips[(size_t) hit.first].length;
            dragOrig.clear();
            for (auto& k : selClips)
                if (auto* g2 = P.group (k.first))
                    if (juce::isPositiveAndBelow (k.second, (int) g2->clips.size())) dragOrig[k] = { g2->clips[(size_t) k.second].start, g2->clips[(size_t) k.second].length };
            dragTick0 = xToTick ((float) e.x);
        }
        else if (ui.pencil)
        {
            selClips.clear();
            createClipAt (gi, (float) e.x);
        }
        ui.changed();
    }

    void songMouseDrag (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        auto q = (juce::int64) ui.arrGridBars * P.ticksPerBar();
        if (drag == DragSection)
        {
            // drag the end of a section to make it longer / shorter (later sections move along)
            auto* se = P.section (selSection);
            if (se == nullptr) return;
            auto q = (juce::int64) ui.arrGridBars * P.ticksPerBar();
            auto end = std::max<juce::int64> (se->start + q, (juce::int64) std::round ((xToTick ((float) e.x) - (double) se->start) / (double) q) * q + se->start);
            setSectionLength (*se, end - se->start);
            repaint();
            return;
        }
        if (drag == DragPosition && std::abs (e.getDistanceFromDragStartY()) > 6)
        {
            drag = DragZoom; dragZoom0 = pxPerTick(); dragTick0 = xToTick ((float) e.getMouseDownX());
        }
        if (drag == DragZoom)
        {
            // drag down on the ruler = zoom in, up = zoom out
            ui.arrPxPerTick = juce::jlimit (0.0005, 1.0, dragZoom0 * std::pow (1.01, (double) e.getDistanceFromDragStartY()));
            ui.arrScrollTick = juce::jmax (0.0, dragTick0 - (e.getMouseDownX() - timelineX()) / ui.arrPxPerTick - e.getDistanceFromDragStartX() / ui.arrPxPerTick);
            repaint();
            return;
        }
        if (drag == DragPosition) { engine.position = juce::jmax (0.0, (double) snap (xToTick ((float) e.x))); return; }
        if (drag == DragBand)
        {
            band = juce::Rectangle<float> (bandStart, e.position);
            selClips = bandBaseClips;
            for (int gi = 0; gi < (int) P.groups.size(); ++gi)
            {
                float y = (float) (groupsY() + gi * rowH());
                for (int ci = 0; ci < (int) P.groups[(size_t) gi]->clips.size(); ++ci)
                {
                    auto& c = P.groups[(size_t) gi]->clips[(size_t) ci];
                    juce::Rectangle<float> r (tickToX ((double) c.start), y, (float) (c.length * pxPerTick()), (float) rowH());
                    if (r.intersects (band)) selClips.insert ({ gi, ci });
                }
            }
            repaint();
            return;
        }
        if (drag == DragScroll)
        {
            auto total = juce::jmax ((double) P.songLength() + 8 * P.ticksPerBar(), 32.0 * P.ticksPerBar());
            ui.arrScrollTick = juce::jlimit (0.0, total, dragTick0 + e.getDistanceFromDragStartX() / (double) (getWidth() - timelineX()) * total);
            repaint();
            return;
        }
        auto* G = P.group (dragGroup);
        if (G == nullptr || ! juce::isPositiveAndBelow (dragClip, (int) G->clips.size())) return;
        double dt = xToTick ((float) e.x) - dragTick0;
        auto dq = (juce::int64) std::round (dt / (double) q) * q;
        const juce::ScopedLock sl (engine.lock);
        auto& c = G->clips[(size_t) dragClip];
        if (dragOrig.empty())
        {
            if (drag == DragMove) c.start = std::max<juce::int64> (0, dragClipStart + dq);
            else if (drag == DragResize) c.length = std::max<juce::int64> (q, dragClipLen + dq);
        }
        else
        {
            // move / resize every selected clip together
            juce::int64 minStart = dragClipStart;
            for (auto& [k, v] : dragOrig) minStart = std::min (minStart, v.first);
            if (drag == DragMove) dq = std::max<juce::int64> (dq, -minStart);
            for (auto& [k, v] : dragOrig)
                if (auto* g2 = P.group (k.first))
                    if (juce::isPositiveAndBelow (k.second, (int) g2->clips.size()))
                    {
                        auto& cc = g2->clips[(size_t) k.second];
                        if (drag == DragMove) cc.start = v.first + dq;
                        else if (drag == DragResize) cc.length = std::max<juce::int64> (q, v.second + dq);
                    }
        }
        repaint();
    }

    // ------------------------------------------------------------------ song sections
    juce::int64 sectionsEnd() const { juce::int64 e = 0; for (auto& se : engine.project().sections) e = std::max (e, se.start + se.length); return e; }
    juce::Rectangle<float> plusRect() const { return { tickToX ((double) sectionsEnd()) + (engine.project().sections.empty() ? 6.0f : 14.0f), (float) rulerH + 10.0f, 14.0f, 14.0f }; }
    Section* sectionEdgeAt (float x)
    {
        Section* best = nullptr; float bd = 7.0f;
        for (auto& se : engine.project().sections)
        {
            float d = std::abs (x - tickToX ((double) (se.start + se.length)));
            if (d <= bd) { bd = d; best = &se; }
        }
        return best;
    }
    juce::int64 gridLen() const { return (juce::int64) ui.arrGridBars * engine.project().ticksPerBar(); }

    // change a section's length; sections after it move along so nothing overlaps
    void setSectionLength (Section& se, juce::int64 len)
    {
        auto& P = engine.project();
        len = std::max<juce::int64> (1, len);
        auto oldEnd = se.start + se.length, delta = len - se.length;
        if (delta == 0) return;
        const juce::ScopedLock sl (engine.lock);
        for (auto& o : P.sections) if (o.id != se.id && o.start >= oldEnd) o.start = std::max<juce::int64> (0, o.start + delta);
        se.length = len;
    }
    Section& insertSection (juce::int64 at, juce::int64 len, int scene)
    {
        auto& P = engine.project();
        const juce::ScopedLock sl (engine.lock);
        for (auto& o : P.sections) if (o.start >= at) o.start += len;
        Section se; se.id = P.nextSectionId++; se.start = at; se.length = len; se.scene = scene;
        P.sections.push_back (se);
        std::sort (P.sections.begin(), P.sections.end(), [] (const Section& a, const Section& b) { return a.start < b.start; });
        selSection = se.id;
        return *P.section (se.id);
    }

    void sectionMouseDown (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        // right edge (a few pixels either side of it) = resize
        if (auto* edge = sectionEdgeAt ((float) e.x); edge != nullptr && ! e.mods.isPopupMenu())
        {
            selSection = edge->id;
            engine.snapshot ("Section Length");
            drag = DragSection;
            ui.changed();
            return;
        }
        if (plusRect().expanded (5.0f, 10.0f).contains (e.position) && ! e.mods.isPopupMenu())
        {
            // + : new section at the end; keep dragging to stretch it over more bars
            engine.snapshot ("Create Section");
            auto& se = insertSection (sectionsEnd(), gridLen(), -1);
            drag = DragSection; secDragStart = se.start; secDragLen = se.length;
            ui.changed();
            return;
        }
        Section* hit = nullptr;
        for (auto& se : P.sections) if (e.x >= tickToX ((double) se.start) && e.x < tickToX ((double) (se.start + se.length))) hit = &se;
        if (hit == nullptr) { selSection = -1; repaint(); return; }
        selSection = hit->id;
        if (hit->scene >= 0) P.currentScene = hit->scene;
        if (e.mods.isPopupMenu()) { sectionMenu (hit->id); ui.changed(); return; }
        if (e.getNumberOfClicks() >= 2) engine.position = (double) hit->start;
        ui.changed();
    }

    void sectionMenu (int id)
    {
        auto& P = engine.project();
        auto* se = P.section (id);
        if (se == nullptr) return;
        juce::PopupMenu m, sel;
        sel.addItem (100, "None", true, se->scene < 0);
        for (int i = 0; i < (int) P.scenes.size(); ++i) sel.addItem (101 + i, P.scenes[(size_t) i].name, true, se->scene == i);
        m.addItem (1, "Create");
        m.addSubMenu ("Select", sel);
        m.addSeparator();
        m.addItem (2, "Auto Length", se->scene >= 0);
        m.addItem (3, "Duplicate");
        m.addItem (4, "Convert to Clip", se->scene >= 0);
        m.addItem (5, "Delete");
        showMenuAt (m, juce::Desktop::getMousePosition(), [this, id] (int r)
        {
            auto& P = engine.project();
            auto* se = P.section (id);
            if (se == nullptr) return;
            if (r == 1) { engine.snapshot ("Create Section"); insertSection (se->start + se->length, gridLen(), -1); }
            else if (r >= 100)
            {
                engine.snapshot ("Section Scene");
                int sc = r - 101;
                { const juce::ScopedLock sl (engine.lock); se->scene = sc; }
                if (sc >= 0) { P.currentScene = sc; P.scenes[(size_t) sc].placed = true; }
            }
            else if (r == 2) { engine.snapshot ("Auto Length"); setSectionLength (*se, engine.sceneLength (se->scene)); }
            else if (r == 3) { engine.snapshot ("Duplicate Section"); auto copy = *se; insertSection (copy.start + copy.length, copy.length, copy.scene); }
            else if (r == 4)
            {
                // the scene's patterns become ordinary clips; the section is left empty
                engine.snapshot ("Convert to Clip");
                engine.syncSections();
                const juce::ScopedLock sl (engine.lock);
                for (auto& g : P.groups) for (auto& c : g->clips) if (c.section == id) c.section = -1;
                se->scene = -1;
            }
            else if (r == 5) deleteSection (id);
            ui.changed();
        });
    }

public:
    void deleteSection (int id)
    {
        auto& P = engine.project();
        auto* se = P.section (id);
        if (se == nullptr) return;
        engine.snapshot ("Delete Section");
        const juce::ScopedLock sl (engine.lock);
        auto end = se->start + se->length, len = se->length;
        for (auto& g : P.groups) g->clips.erase (std::remove_if (g->clips.begin(), g->clips.end(), [id] (const Clip& c) { return c.section == id; }), g->clips.end());
        P.sections.erase (std::remove_if (P.sections.begin(), P.sections.end(), [id] (const Section& x) { return x.id == id; }), P.sections.end());
        for (auto& o : P.sections) if (o.start >= end) o.start -= len;   // close the gap
        if (selSection == id) selSection = -1;
        ui.changed();
    }
private:

    void createClipAt (int gi, float x)
    {
        auto& P = engine.project();
        auto* G = P.group (gi);
        if (G == nullptr) return;
        engine.snapshot ("New Clip");
        auto len = (juce::int64) ui.arrGridBars * P.ticksPerBar();
        engine.newPatternFor (*G, len);
        const juce::ScopedLock sl (engine.lock);
        G->clips.push_back ({ std::max<juce::int64> (0, snap (xToTick (x))), len, (int) G->patterns.size() - 1 });
        engine.setEditClip (gi, (int) G->clips.size() - 1);
        drag = DragResize; dragGroup = gi; dragClip = (int) G->clips.size() - 1;
        dragClipStart = G->clips.back().start; dragClipLen = len; dragTick0 = xToTick (x);
        ui.changed();
    }

    void songDoubleClick (const juce::MouseEvent& e)
    {
        auto hit = hitClip (e.position);
        int gi = (e.y - groupsY()) / rowH();
        if (e.x >= timelineX() && e.y >= groupsY() && e.y < getHeight() - footerH && engine.project().group (gi) != nullptr)
        {
            if (hit.first >= 0) { engine.setEditClip (gi, hit.first); ui.changed(); }
            else createClipAt (gi, (float) e.x);   // double-click on empty space creates a clip
        }
        else if (e.x >= iconW && e.x < timelineX() && engine.project().group (gi) != nullptr && e.y >= groupsY())
            actions::askText ("Rename Group", engine.project().group (gi)->name, [this, gi] (juce::String t) { if (auto* g2 = engine.project().group (gi)) { engine.snapshot ("Rename Group"); g2->name = t; ui.changed(); } });
    }

public:
    // Delete key in the arranger
    void deleteSelectedClip()
    {
        if (! engine.songMode) { deleteSelectedCells(); return; }
        if (selSection >= 0 && selClips.empty()) { deleteSection (selSection); return; }
        if (selClips.size() > 1)
        {
            engine.snapshot ("Delete Clips");
            const juce::ScopedLock sl (engine.lock);
            for (auto it = selClips.rbegin(); it != selClips.rend(); ++it)   // highest index first keeps indices valid
                if (auto* g2 = engine.project().group (it->first))
                    if (juce::isPositiveAndBelow (it->second, (int) g2->clips.size())) g2->clips.erase (g2->clips.begin() + it->second);
            selClips.clear();
            for (auto& g2 : engine.project().groups) juce::ignoreUnused (g2);
            engine.setEditClip (engine.focusedGroup, 0);
            ui.changed();
            return;
        }
        selClips.clear();
        int gi = engine.focusedGroup;
        auto* G = engine.project().group (gi);
        int ci = engine.editClip (gi);
        if (G == nullptr || ! juce::isPositiveAndBelow (ci, (int) G->clips.size())) return;
        engine.snapshot ("Delete Clip");
        const juce::ScopedLock sl (engine.lock);
        G->clips.erase (G->clips.begin() + ci);
        engine.setEditClip (gi, juce::jmax (0, ci - 1));
        ui.changed();
    }
private:

    void clipMenu (int gi, int ci)
    {
        auto* G = engine.project().group (gi);
        juce::PopupMenu m, pats;
        for (int i = 0; i < (int) G->patterns.size(); ++i) pats.addItem (100 + i, G->patterns[(size_t) i]->name, true, G->clips[(size_t) ci].pattern == i);
        m.addItem (1, "Duplicate (Ctrl+D)");
        m.addItem (2, "Make Unique");
        m.addSubMenu ("Pattern", pats);
        m.addItem (3, "Rename Pattern...");
        m.addSubMenu ("Colour", actions::colourMenu (200, G->colour));
        m.addSeparator();
        m.addItem (4, "Delete");
        showMenuAt (m, juce::Desktop::getMousePosition(), [this, gi, ci] (int r)
        {
            auto* G = engine.project().group (gi);
            if (G == nullptr || ! juce::isPositiveAndBelow (ci, (int) G->clips.size())) return;
            engine.snapshot ("Clip Edit");
            if (r == 1) duplicateClip (gi, ci);
            else if (r == 2)
            {
                auto* src = G->pattern (G->clips[(size_t) ci].pattern);
                if (src == nullptr) return;
                auto* np = engine.newPatternFor (*G, src->length);
                const juce::ScopedLock sl (engine.lock);
                np->notes = src->notes; np->lanes = src->lanes;
                G->clips[(size_t) ci].pattern = (int) G->patterns.size() - 1;
            }
            else if (r >= 100 && r < 200) { const juce::ScopedLock sl (engine.lock); G->clips[(size_t) ci].pattern = r - 100; }
            else if (r >= 200 && r < 216) { if (auto* p = G->pattern (G->clips[(size_t) ci].pattern)) p->colour = paletteColour (r - 200); }
            else if (r == 3)
            {
                if (auto* p = G->pattern (G->clips[(size_t) ci].pattern))
                {
                    const int pi = G->clips[(size_t) ci].pattern;   // re-resolve later: a Pattern* may dangle after undo
                    actions::askText ("Rename Pattern", p->name, [this, gi, pi] (juce::String t) { if (auto* g2 = engine.project().group (gi)) if (auto* p2 = g2->pattern (pi)) { p2->name = t; ui.changed(); } });
                }
            }
            else if (r == 4) { const juce::ScopedLock sl (engine.lock); G->clips.erase (G->clips.begin() + ci); }
            ui.changed();
        });
    }

public:
    std::function<void (bool saveAs)> onSave;

    // Ctrl+D: duplicates all selected clips (placed after the selection) or the focused clip
    void duplicateSelection()
    {
        int gi = engine.focusedGroup;
        if (! engine.songMode) return;
        if (selClips.size() <= 1) { duplicateClip (gi, engine.editClip (gi)); return; }
        auto& P = engine.project();
        juce::int64 a = std::numeric_limits<juce::int64>::max(), z = 0;
        for (auto& k : selClips) if (auto* g2 = P.group (k.first)) if (juce::isPositiveAndBelow (k.second, (int) g2->clips.size())) { auto& c = g2->clips[(size_t) k.second]; a = std::min (a, c.start); z = std::max (z, c.start + c.length); }
        if (z <= a) return;
        const juce::ScopedLock sl (engine.lock);
        std::set<std::pair<int, int>> copies;
        for (auto& k : selClips)
            if (auto* g2 = P.group (k.first))
                if (juce::isPositiveAndBelow (k.second, (int) g2->clips.size()))
                {
                    auto c = g2->clips[(size_t) k.second]; c.start += z - a;
                    g2->clips.push_back (c);
                    copies.insert ({ k.first, (int) g2->clips.size() - 1 });
                }
        selClips = copies;
    }
    void selectAllClips()
    {
        auto& P = engine.project();
        selClips.clear();
        for (int gi = 0; gi < (int) P.groups.size(); ++gi) for (int ci = 0; ci < (int) P.groups[(size_t) gi]->clips.size(); ++ci) selClips.insert ({ gi, ci });
        repaint();
    }
    void clearSelection() { selClips.clear(); selCells.clear(); selSection = -1; repaint(); }

    void duplicateClip (int gi, int ci)
    {
        auto* G = engine.project().group (gi);
        if (G == nullptr || ! juce::isPositiveAndBelow (ci, (int) G->clips.size())) return;
        const juce::ScopedLock sl (engine.lock);
        auto c = G->clips[(size_t) ci];
        juce::int64 end = c.start + c.length;
        for (auto& o : G->clips) if (o.start >= end && o.start < end + c.length) end = o.start + o.length;
        c.start = end;
        G->clips.push_back (c);
        engine.setEditClip (gi, (int) G->clips.size() - 1);
    }

private:
    // --------------------------------------------------------------------------- Ideas view
    static constexpr int sceneHeaderH = 22, groupTileH = 40;
    int sceneW() const { return juce::jlimit (50, 400, ui.ideasSceneW); }
    int cellH() const { return juce::jmax (12, rowH() - 4); }

    void paintIdeas (juce::Graphics& g)
    {
        auto& P = engine.project();
        const juce::ScopedLock sl (engine.lock);
        engine.ensureSceneSlots();
        int x0 = iconW;
        int tilesY = getHeight() - groupTileH - 4;
        // scene headers
        for (int s = 0; s < (int) P.scenes.size(); ++s)
        {
            int x = x0 + s * sceneW();
            bool cur = s == P.currentScene;
            g.setColour (cur ? col::rowSel : col::bg);
            g.fillRect (x, 0, sceneW() - 1, tilesY - 4);
            g.setColour (cur ? juce::Colour (0xfff0d040) : col::textDim);
            g.setFont (font (12.0f));
            g.drawText (P.scenes[(size_t) s].name, x, 0, sceneW(), sceneHeaderH, juce::Justification::centred);
            // pattern cells
            for (int gi = 0; gi < (int) P.groups.size(); ++gi)
            {
                auto r = juce::Rectangle<int> (x + 2, sceneHeaderH + 4 + gi * cellH(), sceneW() - 5, cellH() - 2);
                if (r.getBottom() > tilesY - 4) continue;
                bool cellSel = selCells.count ({ s, gi }) > 0;
                int pi = P.scenes[(size_t) s].patternFor (gi);
                if (auto* pat = P.groups[(size_t) gi]->pattern (pi))
                {
                    g.setColour (pat->colour == col::orange ? col::orangeClip : pat->colour);
                    g.fillRect (r);
                    g.setColour (juce::Colour (0xff2a1408));
                    g.drawText (pat->name, r.reduced (4, 0), juce::Justification::centredLeft, true);
                    if (cellSel) { g.setColour (juce::Colours::white); g.drawRect (r, 2); }
                }
                else
                {
                    g.setColour (col::rowAlt.darker (0.1f));
                    g.fillRect (r);
                }
            }
            // grid lines in the scene column
            g.setColour (col::gridLine);
            for (int yy = sceneHeaderH; yy < tilesY - 4; yy += 4) g.fillRect (x + sceneW() - 1, yy, 1, 2);
        }
        icons::draw (g, icons::plus, { (float) (x0 + (int) P.scenes.size() * sceneW() + 6), 4.0f, 14.0f, 14.0f }, col::textDim);

        // playhead progress across the current scene
        if (engine.playing)
        {
            auto L = engine.displayLoopLength();
            float frac = (float) (std::fmod (engine.position.load(), L) / L);
            int x = x0 + P.currentScene * sceneW();
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillRect ((float) x, (float) sceneHeaderH - 2, frac * (sceneW() - 1), 2.0f);
        }

        // group tiles
        int fg = engine.focusedGroup;
        for (int gi = 0; gi < (int) P.groups.size(); ++gi)
        {
            auto r = juce::Rectangle<int> (x0 + gi * sceneW(), tilesY, sceneW() - 2, groupTileH);
            auto& G = *P.groups[(size_t) gi];
            g.setColour (gi == fg ? G.colour : G.colour.withAlpha (0.55f));
            g.fillRect (r);
            g.setColour (juce::Colours::white);
            g.setFont (font (12.0f, true));
            g.drawText (groupLetterName (gi), r.reduced (4, 2).removeFromTop (16), juce::Justification::centredLeft);
            g.setFont (font (11.0f));
            g.drawText (G.name, r.reduced (4, 2).removeFromBottom (16), juce::Justification::centredLeft);
            if (G.mute) { g.setColour (juce::Colours::black.withAlpha (0.4f)); g.fillRect (r); }
        }
        icons::draw (g, icons::plus, { (float) (x0 + (int) P.groups.size() * sceneW() + 6), (float) tilesY + 12, 14.0f, 14.0f }, col::textDim);
    }

    void ideasMouseDown (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        int x0 = iconW;
        int tilesY = getHeight() - groupTileH - 4;
        if (e.x < x0) return;
        int col = (e.x - x0) / sceneW();
        if (e.y >= tilesY)
        {
            if (col < (int) P.groups.size())
            {
                engine.focusedGroup = col;
                if (e.mods.isPopupMenu()) actions::groupMenu (engine, ui, col, this);
            }
            else if (col == (int) P.groups.size()) { engine.snapshot ("Add Group"); engine.addGroup(); }
            ui.changed();
            return;
        }
        if (col == (int) P.scenes.size() && e.y < sceneHeaderH)
        {
            engine.snapshot ("Add Scene");
            const juce::ScopedLock sl (engine.lock);
            P.scenes.push_back ({ "Scene " + juce::String (P.scenes.size() + 1), std::vector<int> (P.groups.size(), -1) });
            P.currentScene = (int) P.scenes.size() - 1;
            ui.changed();
            return;
        }
        if (col >= (int) P.scenes.size()) return;
        if (e.y < sceneHeaderH)
        {
            P.currentScene = col;
            if (e.mods.isPopupMenu()) sceneMenu (col);
            ui.changed();
            return;
        }
        int gi = (e.y - sceneHeaderH - 4) / cellH();
        if (gi >= (int) P.groups.size() || P.scenes[(size_t) col].patternFor (gi) < 0)
        {
            // empty: rubber band selection of pattern cells
            if (gi < (int) P.groups.size() && gi >= 0) { P.currentScene = col; engine.focusedGroup = gi; }
            if (! (e.mods.isShiftDown() || e.mods.isCommandDown())) selCells.clear();
            bandBaseCells = selCells;
            if (e.mods.isPopupMenu() && gi < (int) P.groups.size()) { selCells.clear(); cellMenu (col, gi); }
            else { drag = DragBand; bandStart = e.position; band = {}; }
            ui.changed();
            return;
        }
        P.currentScene = col;
        engine.focusedGroup = gi;
        std::pair<int, int> key { col, gi };
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (selCells.count (key)) selCells.erase (key); else selCells.insert (key); ui.changed(); return; }
        if (selCells.count (key) == 0) { selCells.clear(); selCells.insert (key); }
        if (e.mods.isPopupMenu()) { cellMenu (col, gi); ui.changed(); return; }
        drag = DragCells; cellDragScene = col; cellDragGroup = gi; cellMoved = false;
        ui.changed();
    }

    void ideasMouseDrag (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        if (drag == DragBand)
        {
            band = juce::Rectangle<float> (bandStart, e.position);
            selCells = bandBaseCells;
            for (int sc = 0; sc < (int) P.scenes.size(); ++sc)
                for (int gi = 0; gi < (int) P.groups.size(); ++gi)
                {
                    juce::Rectangle<float> r ((float) (iconW + sc * sceneW() + 2), (float) (sceneHeaderH + 4 + gi * cellH()), (float) sceneW() - 5, (float) cellH() - 2);
                    if (P.scenes[(size_t) sc].patternFor (gi) >= 0 && r.intersects (band)) selCells.insert ({ sc, gi });
                }
            repaint();
            return;
        }
        if (drag != DragCells) return;
        int col = juce::jlimit (0, (int) P.scenes.size() - 1, (int) std::floor ((e.x - iconW) / (double) sceneW()));
        int ds = col - cellDragScene;
        if (ds == 0) return;
        // move (or Alt / Ctrl: copy) the selected patterns to other scenes
        bool copy = e.mods.isAltDown() || e.mods.isCommandDown();
        for (auto& k : selCells) if (k.first + ds < 0 || k.first + ds >= (int) P.scenes.size()) return;
        if (! cellMoved) engine.snapshot (copy ? "Copy Patterns" : "Move Patterns");
        cellMoved = true;
        const juce::ScopedLock sl (engine.lock);
        engine.ensureSceneSlots();
        std::vector<std::tuple<int, int, int>> moves;
        for (auto& k : selCells) moves.push_back ({ k.first, k.second, P.scenes[(size_t) k.first].patternFor (k.second) });
        if (! copy) for (auto& [sc, gi, pi] : moves) P.scenes[(size_t) sc].patternForGroup[(size_t) gi] = -1;
        std::set<std::pair<int, int>> moved;
        for (auto& [sc, gi, pi] : moves) { P.scenes[(size_t) (sc + ds)].patternForGroup[(size_t) gi] = pi; moved.insert ({ sc + ds, gi }); }
        selCells = moved;
        cellDragScene = col;
        P.currentScene = col;
        repaint();
    }

    void ideasMouseUp (const juce::MouseEvent& e)
    {
        if (drag == DragCells && ! cellMoved && ! e.mods.isAnyModifierKeyDown() && e.getDistanceFromDragStart() < 4 && selCells.size() <= 1)
            cellMenu (cellDragScene, cellDragGroup);
        if (drag == DragCells && cellMoved) ui.changed();
        if (drag == DragBand && e.getDistanceFromDragStart() < 4 && ! e.mods.isAnyModifierKeyDown())
        {
            // simple click on an empty cell: pattern menu (New Pattern etc.)
            int col = (int) std::floor ((bandStart.x - iconW) / (double) sceneW());
            int gi = (int) ((bandStart.y - sceneHeaderH - 4) / cellH());
            if (juce::isPositiveAndBelow (col, (int) engine.project().scenes.size()) && juce::isPositiveAndBelow (gi, (int) engine.project().groups.size()) && bandStart.y >= sceneHeaderH)
                cellMenu (col, gi);
        }
    }

    void deleteSelectedCells()
    {
        auto& P = engine.project();
        if (selCells.empty()) return;
        engine.snapshot ("Remove Patterns");
        const juce::ScopedLock sl (engine.lock);
        engine.ensureSceneSlots();
        for (auto& k : selCells)
            if (juce::isPositiveAndBelow (k.first, (int) P.scenes.size()) && juce::isPositiveAndBelow (k.second, (int) P.scenes[(size_t) k.first].patternForGroup.size()))
                P.scenes[(size_t) k.first].patternForGroup[(size_t) k.second] = -1;
        selCells.clear();
        ui.changed();
    }

    void cellMenu (int col, int gi)
    {
        auto& P = engine.project();
        auto* G = P.group (gi);
        if (G == nullptr || ! juce::isPositiveAndBelow (col, (int) P.scenes.size())) return;
        juce::PopupMenu m;
        int cur = P.scenes[(size_t) col].patternFor (gi);
        for (int i = 0; i < (int) G->patterns.size(); ++i) m.addItem (100 + i, G->patterns[(size_t) i]->name, true, i == cur);
        m.addSeparator();
        m.addItem (1, "New Pattern");
        m.addItem (2, "Duplicate Pattern", cur >= 0);
        m.addItem (3, "Rename Pattern...", cur >= 0);
        m.addItem (4, "Remove from Scene", cur >= 0);
        m.addItem (5, "Delete Pattern", cur >= 0);
        showMenuAt (m, juce::Desktop::getMousePosition(), [this, col, gi, cur] (int r)
        {
            auto& P = engine.project();
            auto* G = P.group (gi);
            if (G == nullptr || ! juce::isPositiveAndBelow (col, (int) P.scenes.size())
                || gi >= (int) P.scenes[(size_t) col].patternForGroup.size()) return;
            if (r >= 100 && r - 100 >= (int) G->patterns.size()) return;
            if (r >= 2 && r <= 5 && ! juce::isPositiveAndBelow (cur, (int) G->patterns.size())) return;
            engine.snapshot ("Scene Pattern");
            if (r >= 100) { const juce::ScopedLock sl (engine.lock); P.scenes[(size_t) col].patternForGroup[(size_t) gi] = r - 100; }
            else if (r == 1 || r == 2)
            {
                auto* src = G->pattern (cur);
                auto* np = engine.newPatternFor (*G, src && r == 2 ? src->length : engine.defaultPatternLength());
                const juce::ScopedLock sl (engine.lock);
                if (r == 2 && src) { np->notes = src->notes; np->lanes = src->lanes; }
                P.scenes[(size_t) col].patternForGroup[(size_t) gi] = (int) G->patterns.size() - 1;
            }
            else if (r == 3) { if (auto* p = G->pattern (cur)) actions::askText ("Rename Pattern", p->name, [this, gi, cur] (juce::String t) { if (auto* g2 = engine.project().group (gi)) if (auto* p2 = g2->pattern (cur)) { p2->name = t; ui.changed(); } }); }
            else if (r == 4) { const juce::ScopedLock sl (engine.lock); P.scenes[(size_t) col].patternForGroup[(size_t) gi] = -1; }
            else if (r == 5)
            {
                const juce::ScopedLock sl (engine.lock);
                G->patterns.erase (G->patterns.begin() + cur);
                for (auto& sc : P.scenes)
                    if (gi < (int) sc.patternForGroup.size()) { auto& v = sc.patternForGroup[(size_t) gi]; if (v == cur) v = -1; else if (v > cur) --v; }
                G->clips.erase (std::remove_if (G->clips.begin(), G->clips.end(), [cur] (auto& c) { return c.pattern == cur; }), G->clips.end());
                for (auto& c : G->clips) if (c.pattern > cur) --c.pattern;
            }
            ui.changed();
        });
        ui.changed();
    }

    void ideasDoubleClick (const juce::MouseEvent& e)
    {
        auto& P = engine.project();
        int col = (e.x - iconW) / sceneW();
        if (e.y < sceneHeaderH && juce::isPositiveAndBelow (col, (int) P.scenes.size()))
            actions::askText ("Rename Scene", P.scenes[(size_t) col].name, [this, col] (juce::String t) { auto& sc = engine.project().scenes; if (juce::isPositiveAndBelow (col, (int) sc.size())) { sc[(size_t) col].name = t; repaint(); } });
    }

    void sceneMenu (int s)
    {
        juce::PopupMenu m;
        m.addItem (1, "Rename...");
        m.addItem (2, "Duplicate");
        m.addItem (3, "Delete", engine.project().scenes.size() > 1);
        m.addItem (4, "Add Section to Song");
        showMenuAt (m, juce::Desktop::getMousePosition(), [this, s] (int r)
        {
            auto& P = engine.project();
            if (! juce::isPositiveAndBelow (s, (int) P.scenes.size())) return;
            if (r == 1) { actions::askText ("Rename Scene", P.scenes[(size_t) s].name, [this, s] (juce::String t) { auto& sc = engine.project().scenes; if (juce::isPositiveAndBelow (s, (int) sc.size())) { sc[(size_t) s].name = t; repaint(); } }); return; }
            engine.snapshot ("Scene Edit");
            const juce::ScopedLock sl (engine.lock);
            if (r == 2)
            {
                auto c = P.scenes[(size_t) s]; c.name += " Copy"; c.placed = false;
                P.scenes.insert (P.scenes.begin() + s + 1, c); P.currentScene = s + 1;
                for (auto& se : P.sections) if (se.scene > s) ++se.scene;
            }
            else if (r == 3)
            {
                P.scenes.erase (P.scenes.begin() + s); P.currentScene = juce::jlimit (0, (int) P.scenes.size() - 1, P.currentScene);
                for (auto& se : P.sections) { if (se.scene == s) se.scene = -1; else if (se.scene > s) --se.scene; }
            }
            else if (r == 4)
            {
                // add a section that plays this scene at the end of the song
                Section se; se.id = P.nextSectionId++; se.start = P.songLength(); se.length = engine.sceneLength (s); se.scene = s;
                P.sections.push_back (se);
                P.scenes[(size_t) s].placed = true;
            }
            ui.changed();
        });
    }

    void timerCallback() override
    {
        if ((! isMouseButtonDownAnywhere() || drag == DragSection) && engine.syncSections()) ui.changed();   // keep the editor's clip strip in step
        if (engine.playing && engine.songMode && engine.followOn)
        {
            double pos = engine.position;
            double vis = (getWidth() - timelineX()) / pxPerTick();
            if (pos < ui.arrScrollTick || pos > ui.arrScrollTick + vis * 0.95)
                ui.arrScrollTick = juce::jmax (0.0, pos - vis * 0.05);
        }
        viewBtn.setToggleState (! engine.songMode, juce::dontSendNotification);
        repaint();
    }
};
} // namespace gb
