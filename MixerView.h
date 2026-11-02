#pragma once
#include "Actions.h"

namespace gb
{
// One mixer channel strip (sound, group or master)
class MixerStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Kind { soundStrip, groupStrip, masterStrip };
    MixerStrip (Engine& e, Ui& u, Kind k, int idx) : engine (e), ui (u), kind (k), index (idx) {}

    bool showIO = true, showInserts = true, showAux = true;

    Routing* routing()
    {
        auto& P = engine.project();
        if (kind == masterStrip) return &P.master;
        auto* g = P.group (kind == groupStrip ? index : engine.focusedGroup.load());
        if (g == nullptr) return nullptr;
        return kind == groupStrip ? &g->routing : &g->sounds[(size_t) index]->routing;
    }
    Engine::Chain* chain()
    {
        auto& P = engine.project();
        if (kind == masterStrip) return &P.masterSlots;
        auto* g = P.group (kind == groupStrip ? index : engine.focusedGroup.load());
        if (g == nullptr) return nullptr;
        return kind == groupStrip ? &g->slots : &g->sounds[(size_t) index]->slots;
    }
    bool* mute()
    {
        auto* g = engine.project().group (kind == groupStrip ? index : engine.focusedGroup.load());
        if (kind == masterStrip || g == nullptr) return nullptr;
        return kind == groupStrip ? &g->mute : &g->sounds[(size_t) index]->mute;
    }
    bool* solo()
    {
        auto* g = engine.project().group (kind == groupStrip ? index : engine.focusedGroup.load());
        if (kind == masterStrip || g == nullptr) return nullptr;
        return kind == groupStrip ? &g->solo : &g->sounds[(size_t) index]->solo;
    }
    std::pair<float, float> meter()
    {
        auto& P = engine.project();
        if (kind == masterStrip) return { P.meterL.load(), P.meterR.load() };
        auto* g = P.group (kind == groupStrip ? index : engine.focusedGroup.load());
        if (g == nullptr) return { 0, 0 };
        if (kind == groupStrip) return { g->meterL.load(), g->meterR.load() };
        return { g->sounds[(size_t) index]->meterL.load(), g->sounds[(size_t) index]->meterR.load() };
    }

    static constexpr int headerH = 40, faderH = 150, routeH = 62, slotH = 20;
    std::function<void (MixerStrip&, int index, bool replace)> onAddPlugin;
    std::function<void (MixerStrip&, int index)> onSlotMenu;
    int hoverSlot = -1;
    juce::Rectangle<int> insertArea() const { return { 2, headerH + 4, getWidth() - 4, faderArea().getY() - headerH - 8 }; }
    void focusThis()
    {
        if (kind == soundStrip) { engine.selectSound (engine.focusedGroup, index); ui.channelTarget = 2; }
        else if (kind == groupStrip) { engine.focusedGroup = index; ui.channelTarget = 1; }
        else ui.channelTarget = 0;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        if (showInserts && insertArea().contains (e.getPosition())) h = (e.y - insertArea().getY()) / slotH;
        if (h != hoverSlot) { hoverSlot = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverSlot != -1) { hoverSlot = -1; repaint(); } }

    juce::Rectangle<int> faderArea() const { return { 0, getHeight() - routeH - faderH, getWidth(), faderH - 26 }; }

    void paint (juce::Graphics& g) override
    {
        auto& P = engine.project();
        auto* G = P.group (kind == groupStrip ? index : engine.focusedGroup.load());
        bool focused = kind == soundStrip && G && G->focusedSound == index;
        g.fillAll (focused ? juce::Colour (0xff2c2c2c) : col::bg);
        g.setColour (juce::Colours::black);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
        // header
        juce::String num, name;
        juce::Colour c = col::orange;
        if (kind == masterStrip) { num = "Master"; name = ""; }
        else if (kind == groupStrip) { num = groupLetterName (index); name = G ? G->name : ""; c = G ? G->colour : c; }
        else { num = juce::String (index + 1); name = G ? G->sounds[(size_t) index]->name : ""; c = G ? G->sounds[(size_t) index]->colour : c; }
        if (focused) { g.setColour (col::orange); g.fillRect (0, 0, getWidth() - 1, headerH); }
        g.setColour (focused ? juce::Colours::white : c);
        g.setFont (font (13.0f, true));
        g.drawText (num, 4, 2, getWidth() - 8, 18, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (font (12.0f));
        g.drawText (name, 4, 20, getWidth() - 8, 18, juce::Justification::centredLeft, true);

        // inserts: plug-in slots + "+" to add (click a slot to open it, right-click for options)
        auto ins = insertArea();
        if (showInserts)
            if (auto* ch = chain())
            {
                const juce::ScopedLock sl (engine.lock);
                int y = ins.getY();
                int n = 0;
                for (auto& s : *ch)
                {
                    if (y + slotH > ins.getBottom()) break;
                    auto r = juce::Rectangle<int> (ins.getX(), y, ins.getWidth(), slotH - 2);
                    bool hover = hoverSlot == n;
                    g.setColour (s->bypass ? col::button : (hover ? col::buttonHi.brighter (0.1f) : col::buttonHi));
                    g.fillRoundedRectangle (r.toFloat(), 2.0f);
                    icons::draw (g, icons::power, r.removeFromLeft (14).toFloat().withSizeKeepingCentre (8, 8), s->bypass ? col::textDark : col::orange);
                    g.setColour (s->bypass ? col::textDark : col::text);
                    g.setFont (font (10.5f));
                    g.drawText (engine.describeSlot (*s), r.withTrimmedRight (2), juce::Justification::centredLeft, true);
                    y += slotH; ++n;
                }
                if (y + slotH <= ins.getBottom())
                {
                    auto pr = juce::Rectangle<int> (ins.getX(), y, ins.getWidth(), slotH - 2);
                    if (hoverSlot == n) { g.setColour (col::button); g.fillRoundedRectangle (pr.toFloat(), 2.0f); }
                    icons::draw (g, icons::plus, pr.removeFromLeft (14).toFloat().withSizeKeepingCentre (9, 9), hoverSlot == n ? juce::Colours::white : col::textDim);
                }
            }

        // fader section
        auto fa = faderArea();
        g.setColour (juce::Colour (0xff272727));
        g.fillRect (fa.withHeight (faderH));
        auto r = routing();
        auto [ml, mr] = meter();
        float peak = juce::jmax (ml, mr);
        g.setColour (peak > 0.99f ? juce::Colours::red : col::textDim);
        g.setFont (font (10.0f));
        g.drawText (peak > 0.0001f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) : juce::String ("-inf"), fa.getX(), fa.getY() + 2, fa.getWidth(), 12, juce::Justification::centred);
        // meters
        auto meterR = juce::Rectangle<int> (fa.getRight() - 16, fa.getY() + 18, 10, fa.getHeight() - 24);
        auto drawMeter = [&] (juce::Rectangle<int> m, float v)
        {
            g.setColour (col::header);
            g.fillRect (m);
            float n = levelToNorm (v) / levelToNorm (2.0f);
            g.setColour (v > 0.99f ? juce::Colours::red : col::meterGreen.withAlpha (0.85f));
            g.fillRect (m.withTop (m.getBottom() - (int) (m.getHeight() * juce::jlimit (0.0f, 1.0f, n))));
        };
        drawMeter (meterR.removeFromLeft (4), ml);
        meterR.removeFromLeft (2);
        drawMeter (meterR.removeFromLeft (4), mr);
        // scale + fader
        auto track = juce::Rectangle<int> (fa.getX() + 6, fa.getY() + 18, fa.getWidth() - 28, fa.getHeight() - 24);
        g.setFont (font (9.5f));
        for (auto db : { 6, 0, -10, -20, -30 })
        {
            float y = track.getBottom() - track.getHeight() * levelToNorm (juce::Decibels::decibelsToGain ((float) db)) / levelToNorm (2.0f);
            g.setColour (col::textDark);
            g.drawText (juce::String (std::abs (db)), track.getX(), (int) y - 6, 20, 12, juce::Justification::centredLeft);
        }
        if (r != nullptr)
        {
            float y = track.getBottom() - track.getHeight() * levelToNorm (r->level) / levelToNorm (2.0f);
            g.setColour (col::textDim);
            g.fillRect ((float) track.getX() + 18, y, (float) track.getWidth() - 20, 1.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse ((float) track.getRight() - 6, y - 3.5f, 7.0f, 7.0f);
            // pan
            auto pr = juce::Rectangle<float> ((float) fa.getX() + 6, (float) fa.getBottom() + 2, (float) fa.getWidth() - 12, 4.0f);
            g.setColour (col::header);
            g.fillRect (pr);
            g.setColour (col::textDim);
            float cx = pr.getCentreX(), px = pr.getX() + (r->pan + 1.0f) * 0.5f * pr.getWidth();
            g.fillRect (juce::jmin (cx, px), pr.getY(), std::abs (px - cx) + 1, pr.getHeight());
        }
        // mute / solo / cue row
        auto mrow = juce::Rectangle<int> (0, fa.getBottom() + 8, getWidth(), 16);
        if (auto* m = mute())
        {
            g.setColour (*m ? col::orange : col::text);
            g.setFont (font (11.0f));
            g.drawText ("Mute", mrow.withTrimmedLeft (4).withWidth (32), juce::Justification::centredLeft);
            g.setColour (*solo() ? juce::Colour (0xfff0d040) : col::textDim);
            g.drawText ("Solo", mrow.withTrimmedLeft (36).withWidth (28), juce::Justification::centredLeft);
        }
        if (r != nullptr) icons::draw (g, icons::headphones, { (float) getWidth() - 16, (float) mrow.getY() + 1, 12.0f, 12.0f }, r->cue ? col::orange : col::textDim);

        // routing
        auto rr = juce::Rectangle<int> (3, getHeight() - routeH + 4, getWidth() - 6, 16);
        if (r != nullptr && showIO)
        {
            g.setColour (col::buttonHi);
            g.fillRoundedRectangle (rr.toFloat(), 8.0f);
            g.setColour (col::text);
            g.setFont (font (10.0f, true));
            auto dn = kind == masterStrip ? "EXT. " + juce::String (juce::jmax (1, r->outDest - destExt1 + 1)) : destName (r->outDest, kind == soundStrip).toUpperCase();
            g.drawText (dn, rr.reduced (6, 0), juce::Justification::centredLeft);
        }
        if (r != nullptr && showAux && kind != masterStrip)
            for (int a = 0; a < 2; ++a)
            {
                auto ar = rr.translated (0, 19 * (a + 1));
                g.setColour (col::button);
                g.fillRoundedRectangle (ar.toFloat(), 8.0f);
                float lv = a == 0 ? r->aux1Level : r->aux2Level;
                int d = a == 0 ? r->aux1Dest : r->aux2Dest;
                g.setColour (d == destNone ? col::textDark : col::text);
                g.setFont (font (10.0f, true));
                g.drawText ("AUX " + juce::String (a + 1), ar.reduced (6, 0), juce::Justification::centredLeft);
                // send amount arc
                auto kc = juce::Rectangle<float> ((float) ar.getRight() - 15, (float) ar.getY() + 2, 12.0f, 12.0f);
                juce::Path p; p.addCentredArc (kc.getCentreX(), kc.getCentreY(), 5, 5, 0, -2.4f, -2.4f + 4.8f * levelToNorm (lv), true);
                g.setColour (col::text);
                g.strokePath (p, juce::PathStrokeType (1.5f));
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto r = routing();
        auto fa = faderArea();
        auto* G = engine.project().group (kind == groupStrip ? index : engine.focusedGroup.load());
        dragWhat = 0;
        if (e.y < headerH)
        {
            if (kind == soundStrip && G) { engine.selectSound (engine.focusedGroup, index); ui.channelTarget = 2; }
            else if (kind == groupStrip) { engine.focusedGroup = index; ui.channelTarget = 1; }
            else ui.channelTarget = 0;
            if (e.mods.isPopupMenu())
            {
                if (kind == soundStrip) actions::soundMenu (engine, ui, engine.focusedGroup, index, this);
                else if (kind == groupStrip) actions::groupMenu (engine, ui, index, this);
            }
            ui.changed();
            return;
        }
        if (e.y < fa.getY() && showInserts)
        {
            auto* ch = chain();
            if (ch == nullptr || e.y < insertArea().getY()) return;
            int i = (e.y - insertArea().getY()) / slotH;
            focusThis();
            ui.channelMode = 0;
            if (juce::isPositiveAndBelow (i, (int) ch->size()))
            {
                ui.selectedSlot = i;
                ui.changed();
                if (e.mods.isPopupMenu()) { if (onSlotMenu) onSlotMenu (*this, i); }
                else if (e.x < 16) { engine.snapshot ("Bypass"); (*ch)[(size_t) i]->bypass = ! (*ch)[(size_t) i]->bypass; repaint(); }
                else engine.openPluginEditor (*(*ch)[(size_t) i]);   // view the plug-in
            }
            else if (i == (int) ch->size())
            {
                ui.changed();
                if (onAddPlugin) onAddPlugin (*this, i, false);
            }
            return;
        }
        if (r == nullptr) return;
        if (fa.contains (e.getPosition()))
        {
            engine.snapshot ("Level");
            if (e.getNumberOfClicks() >= 2) { r->level = 1.0f; repaint(); return; }
            dragWhat = 1; dragStart = levelToNorm (r->level);
            return;
        }
        if (e.y >= fa.getBottom() && e.y < fa.getBottom() + 7)
        {
            engine.snapshot ("Pan");
            if (e.getNumberOfClicks() >= 2) { r->pan = 0; repaint(); return; }
            dragWhat = 2; dragStart = r->pan;
            return;
        }
        auto mrowY = fa.getBottom() + 8;
        if (e.y >= mrowY && e.y < mrowY + 16)
        {
            if (e.x > getWidth() - 18) r->cue = ! r->cue;
            else if (auto* m = mute()) { if (e.x < 36) *m = ! *m; else *solo() = ! *solo(); }
            repaint();
            return;
        }
        int ry = getHeight() - routeH + 4;
        int row = (e.y - ry) / 19;
        if (row == 0 && showIO)
        {
            juce::PopupMenu m;
            auto choices = kind == masterStrip ? juce::StringArray() : destChoices (kind == soundStrip);
            if (kind == masterStrip) for (int i = 1; i <= 16; ++i) choices.add ("Ext. " + juce::String (i));
            int cur = kind == masterStrip ? juce::jmax (0, r->outDest - destExt1) : destToIndex (r->outDest, kind == soundStrip);
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == cur);
            showMenu (m, this, [this] (int res)
            {
                auto* rt = routing();
                if (rt == nullptr) return;
                engine.snapshot ("Output");
                rt->outDest = kind == masterStrip ? destExt1 + res - 1 : indexToDest (res - 1, kind == soundStrip);
                repaint();
            });
        }
        else if ((row == 1 || row == 2) && showAux && kind != masterStrip)
        {
            int a = row - 1;
            if (e.x > getWidth() - 22) { engine.snapshot ("Aux Level"); dragWhat = 3 + a; dragStart = levelToNorm (a == 0 ? r->aux1Level : r->aux2Level); return; }
            juce::PopupMenu m;
            auto choices = destChoices (false);
            int cur = destToIndex (a == 0 ? r->aux1Dest : r->aux2Dest, false);
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == cur);
            showMenu (m, this, [this, a] (int res)
            {
                auto* rt = routing();
                if (rt == nullptr) return;
                engine.snapshot ("Aux Dest");
                (a == 0 ? rt->aux1Dest : rt->aux2Dest) = indexToDest (res - 1, false);
                if ((a == 0 ? rt->aux1Level : rt->aux2Level) <= 0) (a == 0 ? rt->aux1Level : rt->aux2Level) = 0.5f;
                repaint();
            });
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = routing();
        if (r == nullptr || dragWhat == 0) return;
        float d = -e.getDistanceFromDragStartY() / (float) faderArea().getHeight() * levelToNorm (2.0f);
        if (dragWhat == 1) r->level = normToLevel (juce::jlimit (0.0f, levelToNorm (2.0f), dragStart + d));
        else if (dragWhat == 2) r->pan = juce::jlimit (-1.0f, 1.0f, dragStart + e.getDistanceFromDragStartX() / 40.0f);
        else (dragWhat == 3 ? r->aux1Level : r->aux2Level) = normToLevel (juce::jlimit (0.0f, 1.0f, dragStart - e.getDistanceFromDragStartY() / 100.0f));
        repaint();
    }

    Engine& engine;
    Ui& ui;
    Kind kind;
    int index;
private:
    int dragWhat = 0; float dragStart = 0;
};

class MixerView : public juce::Component, private juce::Timer
{
public:
    MixerView (Engine& e, Ui& u) : engine (e), ui (u)
    {
        for (auto* b : { &ioBtn, &insBtn, &auxBtn })
        {
            b->setClickingTogglesState (true);
            b->setToggleState (true, juce::dontSendNotification);
            b->onClick = [this] { for (auto* s : strips) { s->showIO = ioBtn.getToggleState(); s->showInserts = insBtn.getToggleState(); s->showAux = auxBtn.getToggleState(); s->repaint(); } };
            addAndMakeVisible (b);
        }
        startTimerHz (25);
    }

    void rebuild()
    {
        strips.clear();
        int ng = (int) engine.project().groups.size();
        for (int i = 0; i < numPads; ++i) strips.add (new MixerStrip (engine, ui, MixerStrip::soundStrip, i));
        strips.add (new MixerStrip (engine, ui, MixerStrip::groupStrip, engine.focusedGroup));
        strips.add (new MixerStrip (engine, ui, MixerStrip::masterStrip, 0));
        for (auto* s : strips)
        {
            s->onAddPlugin = [this] (MixerStrip& st, int i, bool replace) { if (onAddPlugin) onAddPlugin (st, i, replace); };
            s->onSlotMenu = [this] (MixerStrip& st, int i) { if (onSlotMenu) onSlotMenu (st, i); };
            addAndMakeVisible (s);
        }
        lastGroups = ng; lastFocus = engine.focusedGroup;
        resized();
    }

    static constexpr int iconW = 36, groupRowH = 44;
    std::function<void (MixerStrip&, int, bool)> onAddPlugin;
    std::function<void (MixerStrip&, int)> onSlotMenu;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::header);
        g.fillRect (0, 0, iconW, getHeight());
        auto& P = engine.project();
        // group row
        for (int gi = 0; gi < (int) P.groups.size(); ++gi)
        {
            auto& G = *P.groups[(size_t) gi];
            auto r = juce::Rectangle<int> (iconW + 2 + gi * 72, 4, 70, groupRowH - 10);
            bool foc = gi == engine.focusedGroup;
            g.setColour (foc ? G.colour : col::row);
            g.fillRect (r.removeFromBottom (16));
            g.setColour (foc ? G.colour : col::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (groupLetterName (gi), r.reduced (2, 0), juce::Justification::centredLeft);
            g.setColour (foc ? juce::Colours::white : col::text);
            g.setFont (font (11.0f));
            g.drawText (G.name, iconW + 4 + gi * 72, groupRowH - 22, 66, 16, juce::Justification::centredLeft, true);
        }
        icons::draw (g, icons::plus, { (float) (iconW + 8 + (int) P.groups.size() * 72), 12.0f, 14.0f, 14.0f }, col::textDim);
        g.setColour (engine.project().group (engine.focusedGroup) ? engine.project().group (engine.focusedGroup)->colour : col::orange);
        g.fillRect (iconW, groupRowH - 2, getWidth() - iconW, 2);
    }

    void resized() override
    {
        ioBtn.setBounds (2, groupRowH + 6, 32, 18);
        insBtn.setBounds (2, groupRowH + 26, 32, 18);
        auxBtn.setBounds (2, groupRowH + 46, 32, 18);
        int w = stripW();
        ui.mixerScroll = juce::jlimit (0, maxScroll(), ui.mixerScroll);
        int x = iconW + 2 - ui.mixerScroll;
        for (int i = 0; i < strips.size(); ++i)
        {
            if (i == numPads) x += 10;
            strips[i]->setBounds (x, groupRowH + 2, w, getHeight() - groupRowH - 4);
            x += w;
        }
    }

    int autoStripW() const { return juce::jlimit (60, 76, (getWidth() - iconW - 24) / 18); }
    int stripW() const { return ui.mixerStripW > 0 ? juce::jlimit (50, 200, ui.mixerStripW) : autoStripW(); }
    int maxScroll() const { return juce::jmax (0, stripW() * strips.size() + 14 - (getWidth() - iconW - 2)); }
    void zoomStrips (int dir) { ui.mixerStripW = juce::jlimit (50, 200, stripW() + dir * 10); resized(); repaint(); }
    void zoomReset() { ui.mixerStripW = 0; ui.mixerScroll = 0; resized(); repaint(); }

    // Ctrl+wheel = wider / narrower strips, wheel / Shift+wheel = scroll sideways
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (e.mods.isCommandDown() || e.mods.isAltDown()) zoomStrips (d > 0 ? 1 : -1);
        else { ui.mixerScroll = juce::jlimit (0, maxScroll(), ui.mixerScroll - (int) (d * 200.0f)); resized(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y > groupRowH || e.x < iconW) return;
        int gi = (e.x - iconW - 2) / 72;
        auto& P = engine.project();
        if (gi < (int) P.groups.size())
        {
            engine.focusedGroup = gi;
            if (e.mods.isPopupMenu()) actions::groupMenu (engine, ui, gi, this);
        }
        else if (gi == (int) P.groups.size()) { engine.snapshot ("Add Group"); engine.addGroup(); }
        ui.changed();
    }

private:
    Engine& engine;
    Ui& ui;
    juce::OwnedArray<MixerStrip> strips;
    FlatButton ioBtn { "IO", 10.0f }, insBtn { juce::CharPointer_UTF8 ("\xe2\x89\xa1"), 13.0f }, auxBtn { "Aux", 10.0f };
    int lastGroups = -1, lastFocus = -1;
    void timerCallback() override
    {
        if (! isVisible()) return;
        if (lastGroups != (int) engine.project().groups.size() || lastFocus != engine.focusedGroup) { rebuild(); repaint(); }
        for (auto* s : strips) s->repaint();
    }
};
} // namespace gb
