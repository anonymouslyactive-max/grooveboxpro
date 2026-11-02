#pragma once
#include "Actions.h"

namespace gb
{
// The grid where notes are drawn (drum view: 16 sound rows / keyboard view: piano roll of the focused sound)
class NoteGrid : public juce::Component
{
public:
    NoteGrid (Engine& e, Ui& u) : engine (e), ui (u) { setWantsKeyboardFocus (true); }

    int rowH = 24;
    int rowOffset = 0;                     // drum view: first visible sound row (vertical scroll)
    std::function<int()> getTopNote;       // keyboard view: highest visible note
    std::function<double (double)> tickToX;
    std::function<double (double)> xToTick;
    std::function<juce::int64()> regionStart;  // absolute tick where the pattern starts (song clip start)
    std::function<void()> onEdited;

    bool keyboard() const { return ui.keyboardView; }
    int rowsVisible() const { return getHeight() / rowH + 1; }
    int rowToSoundOrNote (int row) const { return keyboard() ? getTopNote() - row : row + rowOffset; }
    int noteRow (const NoteEvent& n, const Group& G) const { return keyboard() ? getTopNote() - (n.pitch >= 0 ? n.pitch : G.sounds[(size_t) n.sound]->rootKey) : n.sound - rowOffset; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::editorBg);
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        const juce::ScopedLock sl (engine.lock);
        auto* pat = engine.editPattern (engine.focusedGroup);
        auto base = regionStart ? regionStart() : 0;
        // rows
        for (int r = 0; r < rowsVisible(); ++r)
        {
            int y = r * rowH;
            bool dark;
            if (keyboard()) { int n = rowToSoundOrNote (r); dark = juce::MidiMessage::isMidiNoteBlack (n); }
            else dark = ((r + rowOffset) % 2) == 1;
            g.setColour (dark ? juce::Colour (0xff1a1a1a) : juce::Colour (0xff1e1e1e));
            if (! keyboard() && r + rowOffset == G->focusedSound) g.setColour (juce::Colour (0xff262626));
            g.fillRect (0, y, getWidth(), rowH);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (0, y + rowH - 1, getWidth(), 1);
        }
        // pattern region shading
        if (pat != nullptr)
        {
            float x0 = (float) tickToX ((double) base), x1 = (float) tickToX ((double) (base + pat->length));
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            if (x0 > 0) g.fillRect (0.0f, 0.0f, x0, (float) getHeight());
            if (x1 < getWidth()) g.fillRect (x1, 0.0f, (float) getWidth() - x1, (float) getHeight());
        }
        // vertical grid
        auto& P = engine.project();
        auto step = (double) Engine::gridTicks (engine.stepGrid);
        if (step < 2) step = PPQ / 4.0;
        double t0 = xToTick (0), t1 = xToTick (getWidth());
        double beat = (double) PPQ * 4 / P.sigDen, bar = (double) P.ticksPerBar();
        double drawStep = step;
        while ((tickToX (drawStep) - tickToX (0)) < 6) drawStep *= 2;
        for (double t = std::floor (t0 / drawStep) * drawStep; t < t1; t += drawStep)
        {
            float x = (float) tickToX (t);
            bool isBar = std::fmod (t, bar) < 0.5, isBeat = std::fmod (t, beat) < 0.5;
            g.setColour (isBar ? col::gridBar : isBeat ? juce::Colour (0xff383838) : juce::Colour (0xff282828));
            g.fillRect (x, 0.0f, 1.0f, (float) getHeight());
        }
        if (pat == nullptr)
        {
            g.setColour (col::textDark);
            g.setFont (font (13.0f));
            g.drawText (engine.songMode ? "Double-click in the arranger or draw here to create a clip" : "Draw notes to create a pattern", getLocalBounds(), juce::Justification::centred);
            return;
        }
        // notes
        for (int pass = 0; pass < 2; ++pass)
            for (juce::int64 k = 0; ; ++k)
            {
                juce::int64 off = base + k * pat->length;
                if (! engine.songMode && k > 0) break;
                if (engine.songMode)
                {
                    int ci = engine.editClip (engine.focusedGroup);
                    if (! juce::isPositiveAndBelow (ci, (int) G->clips.size())) break;
                    auto& c = G->clips[(size_t) ci];
                    if (off >= c.start + c.length) break;
                }
                for (int i = 0; i < (int) pat->notes.size(); ++i)
                {
                    auto& n = pat->notes[(size_t) i];
                    if (keyboard() && n.sound != G->focusedSound) continue;
                    int row = noteRow (n, *G);
                    if (row < 0 || row >= rowsVisible()) continue;
                    auto r = juce::Rectangle<float> ((float) tickToX ((double) (off + n.start)), (float) row * rowH + (keyboard() ? 0.0f : 2.0f), (float) (tickToX ((double) (off + n.start + n.length)) - tickToX ((double) (off + n.start))), (float) rowH - (keyboard() ? 1.0f : 4.0f));
                    if (r.getRight() < 0 || r.getX() > getWidth()) continue;
                    auto c = G->sounds[(size_t) juce::jlimit (0, 15, n.sound)]->colour;
                    if (c == col::orange) c = col::orangeClip;
                    if (pass == 0)
                    {
                        g.setColour (k > 0 ? c.withAlpha (0.4f) : c.withMultipliedBrightness (0.55f + 0.45f * n.velocity));
                        g.fillRect (r.withWidth (juce::jmax (3.0f, r.getWidth() - 1)));
                        if (n.probability < 1.0f) { g.setColour (juce::Colours::black.withAlpha (0.5f * (1.0f - n.probability))); g.fillRect (r); }
                    }
                    else if (n.selected && k == 0)
                    {
                        g.setColour (juce::Colours::white);
                        g.drawRect (r, 1.5f);
                    }
                }
            }
        // rubber band
        if (! band.isEmpty()) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect (band); g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRect (band, 1.0f); }
        // playhead
        if (engine.playing)
        {
            double pos = engine.position;
            if (! engine.songMode) pos = std::fmod (pos, (double) std::max<juce::int64> (1, pat->length));
            float x = (float) tickToX (pos);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (x, 0.0f, 1.0f, (float) getHeight());
        }
    }

    // ---------------------------------------------------------------- editing
    Pattern* ensurePattern()
    {
        auto* p = engine.editPattern (engine.focusedGroup);
        if (p != nullptr) return p;
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return nullptr;
        auto& P = engine.project();
        if (engine.songMode)
        {
            auto len = (juce::int64) ui.arrGridBars * P.ticksPerBar();
            p = engine.newPatternFor (*G, len);
            const juce::ScopedLock sl (engine.lock);
            G->clips.push_back ({ 0, len, (int) G->patterns.size() - 1 });
            engine.setEditClip (engine.focusedGroup, (int) G->clips.size() - 1);
        }
        else
        {
            p = engine.newPatternFor (*G, engine.defaultPatternLength());
            const juce::ScopedLock sl (engine.lock);
            engine.ensureSceneSlots();
            P.scenes[(size_t) P.currentScene].patternForGroup[(size_t) engine.focusedGroup] = (int) G->patterns.size() - 1;
        }
        return p;
    }

    juce::int64 localTick (float x) const
    {
        auto base = regionStart ? regionStart() : 0;
        return (juce::int64) std::floor (xToTick (x)) - base;
    }
    juce::int64 snapDown (juce::int64 t) const
    {
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        return (juce::int64) std::floor ((double) t / (double) q) * q;
    }

    int hitNote (juce::Point<float> p, bool& rightEdge)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (G == nullptr || pat == nullptr) return -1;
        int row = (int) (p.y / rowH);
        auto base = regionStart ? regionStart() : 0;
        for (int i = (int) pat->notes.size() - 1; i >= 0; --i)
        {
            auto& n = pat->notes[(size_t) i];
            if (keyboard() && n.sound != G->focusedSound) continue;
            if (noteRow (n, *G) != row) continue;
            float x0 = (float) tickToX ((double) (base + n.start)), x1 = (float) tickToX ((double) (base + n.start + n.length));
            if (p.x >= x0 && p.x <= juce::jmax (x1, x0 + 4))
            {
                rightEdge = p.x > x1 - 5 && (x1 - x0) > 10;
                return i;
            }
        }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        bool edge = false;
        int hit = hitNote (e.position, edge);
        int row = (int) (e.position.y / rowH);
        int target = rowToSoundOrNote (row);
        auto* pat = engine.editPattern (engine.focusedGroup);
        dragMode = None;

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0) { engine.snapshot ("Delete Note"); const juce::ScopedLock sl (engine.lock); pat->notes.erase (pat->notes.begin() + hit); changed(); }
            else noteContextMenu();
            return;
        }
        if (! keyboard() && juce::isPositiveAndBelow (target, numPads)) { G->focusedSound = target; ui.changed(); }

        if (hit >= 0)
        {
            if (ui.pencil) { engine.snapshot ("Delete Note"); const juce::ScopedLock sl (engine.lock); pat->notes.erase (pat->notes.begin() + hit); dragMode = Erase; changed(); return; }
            auto& n = pat->notes[(size_t) hit];
            if (! n.selected && ! e.mods.isShiftDown()) for (auto& o : pat->notes) o.selected = false;
            if (e.mods.isShiftDown()) n.selected = ! n.selected; else n.selected = true;
            engine.snapshot (edge ? "Resize Notes" : (e.mods.isCommandDown() ? "Copy Notes" : "Move Notes"));
            if (e.mods.isCommandDown() && ! edge)
            {
                // Ctrl+drag copies the selection: the copies are what gets dragged
                const juce::ScopedLock sl (engine.lock);
                std::vector<NoteEvent> copies;
                for (auto& o : pat->notes) if (o.selected) { copies.push_back (o); o.selected = false; }
                for (auto& c : copies) pat->notes.push_back (c);
            }
            dragMode = edge ? Resize : Move;
            dragNote = hit;
            origin.clear();
            for (auto& o : pat->notes) origin.push_back (o);
            dragStartTick = localTick (e.position.x);
            dragStartRow = row;
            // audition
            auto nv = pat->notes[(size_t) hit];
            if (! edge) engine.triggerPad (engine.focusedGroup, nv.sound, nv.velocity, true, nv.pitch);
            auditioned = nv.sound; auditionPitch = nv.pitch;
            changed();
            return;
        }
        if (pat != nullptr && (e.mods.isShiftDown() || ! ui.pencil))
        {
            // drag = rubber band selection, click without dragging = add a note (on mouse up)
            dragMode = e.mods.isShiftDown() ? Band : Pending;
            bandStart = e.position;
            bandAdd = e.mods.isShiftDown() || e.mods.isCommandDown();
            bandBase.clear();
            for (auto& o : pat->notes) bandBase.push_back (o.selected);
            if (dragMode == Band) startBand();
            return;
        }
        addNoteAt (e.position, row);
    }

    void addNoteAt (juce::Point<float> pos, int row)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return;
        int target = rowToSoundOrNote (row);
        if (target >= (keyboard() ? 128 : numPads) || target < 0) return;
        // add a note
        engine.snapshot ("Add Note");
        auto* pat = ensurePattern();
        if (pat == nullptr) return;
        auto t = snapDown (localTick (pos.x));
        if (t < 0 || t >= pat->length) return;
        NoteEvent n;
        n.sound = keyboard() ? G->focusedSound : target;
        n.pitch = keyboard() ? target : -1;
        n.start = t;
        n.length = Engine::gridTicks (engine.stepGrid);
        n.velocity = lastVelocity;
        {
            const juce::ScopedLock sl (engine.lock);
            for (auto& o : pat->notes) o.selected = false;
            n.selected = true;
            pat->notes.push_back (n);
        }
        engine.triggerPad (engine.focusedGroup, n.sound, n.velocity, true, n.pitch);
        auditioned = n.sound; auditionPitch = n.pitch;
        dragMode = ui.pencil ? Paint : Resize;
        dragNote = (int) pat->notes.size() - 1;
        origin.assign (pat->notes.begin(), pat->notes.end());
        dragStartTick = t;
        paintRow = row;
        changed();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* G = engine.project().group (engine.focusedGroup);
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (G == nullptr || pat == nullptr) return;
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        if (dragMode == Pending && e.getDistanceFromDragStart() > 4) { dragMode = Band; startBand(); }
        if (dragMode == Band)
        {
            band = juce::Rectangle<float> (bandStart, e.position);
            auto base = regionStart ? regionStart() : 0;
            const juce::ScopedLock sl (engine.lock);
            for (size_t i = 0; i < pat->notes.size(); ++i)
            {
                auto& n = pat->notes[i];
                bool was = bandAdd && i < bandBase.size() && bandBase[i];
                if (keyboard() && n.sound != G->focusedSound) { n.selected = was; continue; }
                int row = noteRow (n, *G);
                juce::Rectangle<float> r ((float) tickToX ((double) (base + n.start)), (float) row * rowH, juce::jmax (3.0f, (float) (tickToX ((double) (base + n.start + n.length)) - tickToX ((double) (base + n.start)))), (float) rowH);
                n.selected = was || r.intersects (band);
            }
            repaint();
            return;
        }
        if (dragMode == Paint)
        {
            auto t = snapDown (localTick (e.position.x));
            if (t < 0 || t >= pat->length) return;
            int target = rowToSoundOrNote (paintRow);
            const juce::ScopedLock sl (engine.lock);
            for (auto& n : pat->notes) if (n.start == t && (keyboard() ? n.pitch == target : n.sound == target)) return;
            NoteEvent n; n.sound = keyboard() ? G->focusedSound : target; n.pitch = keyboard() ? target : -1; n.start = t; n.length = q; n.velocity = lastVelocity;
            pat->notes.push_back (n);
            changed();
            return;
        }
        if (dragMode == Erase)
        {
            bool edge;
            int hit = hitNote (e.position, edge);
            if (hit >= 0) { const juce::ScopedLock sl (engine.lock); pat->notes.erase (pat->notes.begin() + hit); changed(); }
            return;
        }
        if (dragMode != Move && dragMode != Resize) return;
        auto dt = localTick (e.position.x) - dragStartTick;
        auto dq = e.mods.isAltDown() ? dt : (juce::int64) std::round ((double) dt / (double) q) * q;
        int drow = (int) (e.position.y / rowH) - dragStartRow;
        const juce::ScopedLock sl (engine.lock);
        if (origin.size() != pat->notes.size()) return;
        for (size_t i = 0; i < pat->notes.size(); ++i)
        {
            auto& n = pat->notes[i];
            auto& o = origin[i];
            if (! o.selected) continue;
            if (dragMode == Move)
            {
                n.start = std::clamp<juce::int64> (o.start + dq, 0, pat->length - 1);
                if (keyboard()) n.pitch = juce::jlimit (0, 127, (o.pitch >= 0 ? o.pitch : G->sounds[(size_t) o.sound]->rootKey) - drow);
                else n.sound = juce::jlimit (0, numPads - 1, o.sound + drow);
            }
            else n.length = std::max<juce::int64> (e.mods.isAltDown() ? 10 : q, o.length + dq);
        }
        changed();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragMode == Pending)
        {
            // plain click on empty space: deselect and add a note, like the original
            if (auto* pat = engine.editPattern (engine.focusedGroup)) { const juce::ScopedLock sl (engine.lock); for (auto& o : pat->notes) o.selected = false; }
            dragMode = None;
            addNoteAt (bandStart, (int) (bandStart.y / rowH));
            if (dragMode == Resize || dragMode == Paint) mouseUpFinish();
            dragMode = None;
            return;
        }
        juce::ignoreUnused (e);
        mouseUpFinish();
    }
    void mouseUpFinish()
    {
        if (auditioned >= 0) engine.triggerPad (engine.focusedGroup, auditioned, 0, false, auditionPitch);
        auditioned = -1;
        dragMode = None;
        band = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        bool edge;
        int hit = hitNote (e.position, edge);
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (hit >= 0 && pat != nullptr)
        {
            engine.snapshot ("Delete Note");
            const juce::ScopedLock sl (engine.lock);
            pat->notes.erase (pat->notes.begin() + hit);
            changed();
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (pat == nullptr) return false;
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        auto selected = [pat] { int c = 0; for (auto& n : pat->notes) c += n.selected; return c; };
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            if (selected() == 0) return false;
            engine.snapshot ("Delete Notes");
            const juce::ScopedLock sl (engine.lock);
            pat->notes.erase (std::remove_if (pat->notes.begin(), pat->notes.end(), [] (auto& n) { return n.selected; }), pat->notes.end());
            changed(); return true;
        }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { for (auto& n : pat->notes) n.selected = true; repaint(); return true; }
        if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { copySelection(); return true; }
        if (k == juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0))
        {
            copySelection();
            engine.snapshot ("Cut Notes");
            const juce::ScopedLock sl (engine.lock);
            pat->notes.erase (std::remove_if (pat->notes.begin(), pat->notes.end(), [] (auto& n) { return n.selected; }), pat->notes.end());
            changed(); return true;
        }
        if (k == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0)) { paste(); return true; }
        if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0)) { duplicateSelection(); return true; }
        if (k == juce::KeyPress ('q', 0, 0)) { quantizeSelection(); return true; }
        if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey || k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
        {
            if (selected() == 0) return false;
            engine.snapshot ("Nudge");
            const juce::ScopedLock sl (engine.lock);
            for (auto& n : pat->notes)
            {
                if (! n.selected) continue;
                if (k.getKeyCode() == juce::KeyPress::leftKey) n.start = std::max<juce::int64> (0, n.start - q);
                if (k.getKeyCode() == juce::KeyPress::rightKey) n.start = std::min<juce::int64> (pat->length - 1, n.start + q);
                if (k.getKeyCode() == juce::KeyPress::upKey) { if (keyboard()) n.pitch = juce::jmin (127, (n.pitch < 0 ? 60 : n.pitch) + 1); else n.sound = juce::jmax (0, n.sound - 1); }
                if (k.getKeyCode() == juce::KeyPress::downKey) { if (keyboard()) n.pitch = juce::jmax (0, (n.pitch < 0 ? 60 : n.pitch) - 1); else n.sound = juce::jmin (15, n.sound + 1); }
            }
            changed(); return true;
        }
        return false;
    }

    void quantizeSelection (bool all = false)
    {
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (pat == nullptr) return;
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        engine.snapshot ("Quantize");
        const juce::ScopedLock sl (engine.lock);
        bool any = false; for (auto& n : pat->notes) any |= n.selected;
        for (auto& n : pat->notes)
            if (n.selected || all || ! any) n.start = ((n.start + q / 2) / q) * q % std::max<juce::int64> (1, pat->length);
        changed();
    }

    float lastVelocity = 0.8f;
    void changed() { engine.dirty = true; repaint(); if (onEdited) onEdited(); }

private:
    Engine& engine;
    Ui& ui;
    enum Mode { None, Move, Resize, Paint, Erase, Band, Pending };
    Mode dragMode = None;
    int dragNote = -1, dragStartRow = 0, paintRow = 0;
    juce::int64 dragStartTick = 0;
    std::vector<NoteEvent> origin;
    juce::Point<float> bandStart;
    juce::Rectangle<float> band;
    bool bandAdd = false;
    std::vector<bool> bandBase;
    void startBand() { grabKeyboardFocus(); band = juce::Rectangle<float> (bandStart, bandStart); }
    int auditioned = -1, auditionPitch = -1;
    static std::vector<NoteEvent>& noteClipboard() { static std::vector<NoteEvent> c; return c; }

    void copySelection()
    {
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (pat == nullptr) return;
        noteClipboard().clear();
        juce::int64 first = std::numeric_limits<juce::int64>::max();
        for (auto& n : pat->notes) if (n.selected) first = std::min (first, n.start);
        for (auto& n : pat->notes) if (n.selected) { auto c = n; c.start -= first; noteClipboard().push_back (c); }
    }
    void paste()
    {
        auto* pat = ensurePattern();
        if (pat == nullptr || noteClipboard().empty()) return;
        engine.snapshot ("Paste Notes");
        auto at = engine.playing ? (juce::int64) 0 : (juce::int64) 0;
        juce::int64 last = 0;
        for (auto& n : pat->notes) if (n.selected) last = juce::jmax (last, n.start + n.length);
        at = snapDown (last);
        const juce::ScopedLock sl (engine.lock);
        for (auto& n : pat->notes) n.selected = false;
        for (auto n : noteClipboard()) { n.start = (n.start + at) % pat->length; n.selected = true; pat->notes.push_back (n); }
        changed();
    }
    void duplicateSelection()
    {
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (pat == nullptr) return;
        juce::int64 first = std::numeric_limits<juce::int64>::max(), last = 0;
        for (auto& n : pat->notes) if (n.selected) { first = std::min (first, n.start); last = juce::jmax (last, n.start + n.length); }
        if (first > last) return;
        auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
        auto span = juce::jmax (q, ((last - first + q - 1) / q) * q);
        engine.snapshot ("Duplicate Notes");
        const juce::ScopedLock sl (engine.lock);
        std::vector<NoteEvent> add;
        for (auto& n : pat->notes) if (n.selected) { auto c = n; c.start += span; if (c.start < pat->length) add.push_back (c); n.selected = false; }
        for (auto& n : add) pat->notes.push_back (n);
        changed();
    }

    void noteContextMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Select All (Ctrl+A)");
        m.addItem (2, "Quantize (Q)");
        m.addItem (3, "Quantize 50%");
        m.addItem (4, "Clear Pattern");
        m.addItem (5, "Double Pattern Length");
        m.addItem (6, "Export Pattern as MIDI...");
        showMenu (m, this, [this] (int r)
        {
            auto* pat = engine.editPattern (engine.focusedGroup);
            if (pat == nullptr) return;
            if (r == 1) { for (auto& n : pat->notes) n.selected = true; repaint(); }
            else if (r == 2) quantizeSelection (true);
            else if (r == 3)
            {
                auto q = (juce::int64) Engine::gridTicks (engine.stepGrid);
                engine.snapshot ("Quantize 50%");
                const juce::ScopedLock sl (engine.lock);
                for (auto& n : pat->notes) { auto t = ((n.start + q / 2) / q) * q; n.start = (n.start + t) / 2; }
                changed();
            }
            else if (r == 4) { engine.snapshot ("Clear Pattern"); const juce::ScopedLock sl (engine.lock); pat->notes.clear(); pat->lanes.clear(); changed(); }
            else if (r == 5)
            {
                engine.snapshot ("Double Pattern");
                const juce::ScopedLock sl (engine.lock);
                auto n0 = pat->notes;
                for (auto n : n0) { n.start += pat->length; pat->notes.push_back (n); }
                pat->length *= 2;
                changed();
            }
            else if (r == 6)
                actions::chooseFile ("Export MIDI", Settings::userFolder().getChildFile ("Exports").getChildFile (pat->name + ".mid"), "*.mid", true,
                                     [this] (juce::File f) { actions::exportPatternMidi (engine, engine.focusedGroup, f.withFileExtension ("mid")); });
        });
    }
};

// ---------------------------------------------------------------------------------------------
// Velocity / probability / automation lane
class ModLane : public juce::Component
{
public:
    ModLane (Engine& e, Ui& u) : engine (e), ui (u) {}
    std::function<double (double)> tickToX, xToTick;
    std::function<juce::int64()> regionStart;
    std::function<void()> onEdited;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e1e));
        auto* G = engine.project().group (engine.focusedGroup);
        auto* pat = engine.editPattern (engine.focusedGroup);
        // horizontal guides
        g.setColour (juce::Colour (0xff2a2a2a));
        for (int i = 1; i < 4; ++i) g.fillRect (0, getHeight() * i / 4, getWidth(), 1);
        auto step = (double) PPQ / 4.0;
        for (double t = std::floor (xToTick (0) / step) * step; t < xToTick (getWidth()); t += step)
        {
            bool beat = std::fmod (t, (double) PPQ) < 0.5;
            g.setColour (beat ? juce::Colour (0xff353535) : juce::Colour (0xff262626));
            g.fillRect ((float) tickToX (t), 0.0f, 1.0f, (float) getHeight());
        }
        if (G == nullptr || pat == nullptr) return;
        const juce::ScopedLock sl (engine.lock);
        auto base = regionStart ? regionStart() : 0;
        if (ui.laneMode <= 1)
        {
            for (auto& n : pat->notes)
            {
                if (ui.keyboardView && n.sound != G->focusedSound) continue;
                if (! ui.keyboardView && n.sound != G->focusedSound && ! showAll) continue;
                float v = ui.laneMode == 0 ? n.velocity : n.probability;
                float x = (float) tickToX ((double) (base + n.start));
                float h = v * (getHeight() - 6);
                g.setColour (n.sound == G->focusedSound ? (ui.laneMode == 0 ? col::orangeClip : juce::Colour (0xff6f8fd0)) : col::textDark);
                g.fillRect (x, (float) getHeight() - h, 1.0f, h);
                g.fillRect (x - 2.0f, (float) getHeight() - h - 2.0f, 5.0f, 5.0f);
            }
        }
        else if (auto* lane = currentLane (*pat))
        {
            for (auto& [st, v] : lane->steps)
            {
                float x = (float) tickToX ((double) (base + st * (PPQ / 4)));
                float w = (float) (tickToX ((double) (base + (st + 1) * (PPQ / 4))) - x) - 1.0f;
                float h = v * (getHeight() - 6);
                g.setColour (col::orangeClip.withAlpha (0.8f));
                g.fillRect (x, (float) getHeight() - h, juce::jmax (2.0f, w), h);
            }
        }
    }

    AutoLane* currentLane (Pattern& p)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        if (G == nullptr) return nullptr;
        int idx = ui.laneMode - 2, k = 0;
        for (auto& l : p.lanes) if (l.sound == G->focusedSound) { if (k == idx) return &l; ++k; }
        return nullptr;
    }

    void mouseDown (const juce::MouseEvent& e) override { engine.snapshot (ui.laneMode == 0 ? "Velocity" : ui.laneMode == 1 ? "Probability" : "Automation"); edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }
    bool showAll = false;

private:
    Engine& engine;
    Ui& ui;
    juce::Point<float> last;

    void edit (const juce::MouseEvent& e, bool first)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        auto* pat = engine.editPattern (engine.focusedGroup);
        if (G == nullptr || pat == nullptr) return;
        auto base = regionStart ? regionStart() : 0;
        float v = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - 3) / (getHeight() - 6.0f));
        float xa = first ? e.position.x - 3 : juce::jmin (last.x, e.position.x), xb = first ? e.position.x + 3 : juce::jmax (last.x, e.position.x);
        last = e.position;
        const juce::ScopedLock sl (engine.lock);
        if (ui.laneMode <= 1)
        {
            for (auto& n : pat->notes)
            {
                if (n.sound != G->focusedSound) continue;
                float x = (float) tickToX ((double) (base + n.start));
                if (x >= xa - 3 && x <= xb + 3)
                {
                    if (e.mods.isPopupMenu()) { if (ui.laneMode == 0) n.velocity = 0.8f; else n.probability = 1.0f; }
                    else if (ui.laneMode == 0) n.velocity = juce::jmax (0.01f, v); else n.probability = v;
                }
            }
        }
        else if (auto* lane = currentLane (*pat))
        {
            int s0 = (int) std::floor ((xToTick (xa) - base) / (PPQ / 4)), s1 = (int) std::floor ((xToTick (xb) - base) / (PPQ / 4));
            for (int s = juce::jmax (0, s0); s <= s1 && s < (int) (pat->length / (PPQ / 4)); ++s)
            {
                if (e.mods.isPopupMenu()) lane->steps.erase (s);
                else lane->steps[s] = v;
            }
        }
        repaint();
        if (onEdited) onEdited();
    }
};
} // namespace gb
