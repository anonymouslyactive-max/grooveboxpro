#pragma once
#include "Actions.h"

namespace gb
{
// Middle section: MASTER / GROUP / SOUND channel with plug-in chain and Input / Output / Groove / Macro pages.
class ChannelPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    ChannelPanel (Engine& e, Ui& u) : engine (e), ui (u)
    {
        for (auto* b : { &propsBtn, &pluginBtn, &collapseBtn }) addAndMakeVisible (b);
        propsBtn.setClickingTogglesState (false);
        propsBtn.onClick = [this] { ui.channelMode = 1; ui.changed(); };
        pluginBtn.onClick = [this] { ui.channelMode = 0; ui.changed(); };
        collapseBtn.onClick = [this] { ui.channelCollapsed = ! ui.channelCollapsed; ui.changed(); };
        propsBtn.setTooltip ("Channel properties");
        pluginBtn.setTooltip ("Plug-ins");
        collapseBtn.setTooltip ("Show / hide");

        const char* tabs[] = { "MASTER", "GROUP", "SOUND" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = targetTabs.add (new FlatButton (tabs[i], 11.0f));
            b->onClick = [this, i] { ui.channelTarget = i; ui.selectedSlot = 0; ui.paramPage = 0; ui.propTab = 0; ui.changed(); };
            addAndMakeVisible (b);
        }
        const char* pages[] = { "Input", "Output", "Groove", "Macro" };
        const icons::Id pageIcons[] = { icons::input, icons::output, icons::groove, icons::macro };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = pageBtns.add (new FlatButton (pages[i], 12.5f));
            b->style = FlatButton::menuItem; b->hasIcon = true; b->icon = pageIcons[i];
            b->just = juce::Justification::centredLeft;
            b->onClick = [this, i] { ui.propPage = i; ui.propTab = 0; ui.changed(); };
            addChildComponent (b);
        }
        addAndMakeVisible (searchBtn);
        searchBtn.on = col::orange; searchBtn.off = col::orange;
        searchBtn.onClick = [this] { ui.showBrowser = true; ui.changed(); };
        searchBtn.setTooltip ("Browse");
        ui.addChangeListener (this);
        engine.addChangeListener (this);
        rebuild();
        startTimerHz (10);
    }
    ~ChannelPanel() override { ui.removeChangeListener (this); engine.removeChangeListener (this); }

    static constexpr int iconW = 36, listW = 165;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::header);
        g.fillRect (0, 0, iconW, getHeight());
        g.setColour (juce::Colours::black);
        g.fillRect (iconW - 1, 0, 1, getHeight());
        g.fillRect (iconW + listW - 1, 0, 1, getHeight());
        g.fillRect (0, getHeight() - 1, getWidth(), 1);

        g.setColour (col::orange);
        g.setFont (font (15.0f));
        g.drawText (channelName(), iconW + 8, 22, listW - 34, 24, juce::Justification::centredLeft);

        auto area = contentArea();
        if (ui.channelMode == 0)
        {
            // slot list
            auto* chain = targetChain();
            int y = slotListY;
            if (chain != nullptr)
            {
                const juce::ScopedLock sl (engine.lock);
                for (int i = 0; i < (int) chain->size(); ++i)
                {
                    auto& s = *(*chain)[(size_t) i];
                    auto r = juce::Rectangle<int> (iconW + 8, y, listW - 16, 22);
                    bool sel = i == ui.selectedSlot;
                    g.setColour (sel ? col::buttonHi : col::row);
                    g.fillRoundedRectangle (r.toFloat(), 2.0f);
                    icons::draw (g, s.isInstrument ? icons::instrumentIcon : icons::fxIcon, r.removeFromLeft (22).toFloat().withSizeKeepingCentre (13, 13), s.bypass ? col::textDark : col::text);
                    icons::draw (g, icons::triangleDown, r.removeFromRight (18).toFloat().withSizeKeepingCentre (12, 12), col::textDim);
                    g.setColour (s.bypass ? col::textDark : col::text);
                    g.setFont (font (12.5f));
                    g.drawText (engine.describeSlot (s), r.withTrimmedRight (2), juce::Justification::centredLeft, true);
                    y += 24;
                }
            }
            auto pr = juce::Rectangle<int> (iconW + 8, y, listW - 16, 22);
            g.setColour (col::buttonHi);
            g.fillRoundedRectangle (pr.toFloat(), 2.0f);
            icons::draw (g, icons::plus, pr.removeFromLeft (24).toFloat().withSizeKeepingCentre (12, 12), juce::Colours::white);

            if (chain == nullptr || chain->empty())
            {
                g.setColour (col::textDark);
                g.setFont (font (12.0f));
                g.drawText ("Click + to add an instrument or effect", area.reduced (20, 0), juce::Justification::centredLeft);
            }
        }
        // section captions for property pages
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        for (auto& [r, t] : captions) g.drawText (t, r, juce::Justification::centredLeft);
    }

    void resized() override
    {
        propsBtn.setBounds (4, 4, 28, 26);
        pluginBtn.setBounds (4, 32, 28, 26);
        collapseBtn.setBounds (4, getHeight() - 28, 28, 24);
        int x = iconW + 8;
        for (int i = 0; i < 3; ++i)
        {
            auto w = i == 0 ? 50 : i == 1 ? 44 : 46;
            targetTabs[i]->setBounds (x, 4, w, 16);
            x += w + 4;
        }
        searchBtn.setBounds (iconW + listW - 26, 23, 20, 20);
        for (int i = 0; i < 4; ++i)
            pageBtns[i]->setBounds (iconW + 6 + (i % 2) * 78, 52 + (i / 2) * 36, 74, 30);
        layoutContent();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (ui.channelMode != 0 || e.x < iconW || e.x > iconW + listW) return;
        auto* chain = targetChain();
        if (chain == nullptr) return;
        int idx = (e.y - slotListY) / 24;
        if (e.y < slotListY) return;
        if (idx < (int) chain->size())
        {
            if (e.x < iconW + 28 && ! e.mods.isPopupMenu())
            {
                engine.snapshot ("Bypass");
                (*chain)[(size_t) idx]->bypass = ! (*chain)[(size_t) idx]->bypass;
                repaint();
                return;
            }
            if (e.mods.isPopupMenu() || e.x > iconW + listW - 30) { ui.selectedSlot = idx; slotMenu (idx); return; }
            if (ui.selectedSlot == idx && e.getNumberOfClicks() >= 2) engine.openPluginEditor (*(*chain)[(size_t) idx]);
            ui.selectedSlot = idx; ui.paramPage = 0;
            dragSlot = idx;
            ui.changed();
        }
        else if (idx == (int) chain->size())
            addMenu ((int) chain->size(), false);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* chain = targetChain();
        if (dragSlot >= 0 && chain != nullptr && e.getDistanceFromDragStartY() != 0)
        {
            int to = juce::jlimit (0, (int) chain->size() - 1, (e.y - slotListY) / 24);
            if (to != dragSlot) { engine.snapshot ("Move Plug-in"); engine.moveSlot (*chain, dragSlot, to); ui.selectedSlot = to; ui.changed(); }
        }
        dragSlot = -1;
    }

    void refresh() { rebuild(); }

    // ---- Maschine+ knobs 1-8 / display buttons 1-8 drive whatever page is on screen
    std::function<void (const juce::String&)> onHardwareStatus;
    void controlFromHardware (int index, float v01, bool autoWrite)
    {
        std::vector<juce::Component*> ctl;
        for (auto& p : places) if (p.c != nullptr && p.c->isVisible() && (dynamic_cast<Knob*> (p.c) || dynamic_cast<ValuePicker*> (p.c))) ctl.push_back (p.c);
        if (! juce::isPositiveAndBelow (index, (int) ctl.size())) return;
        auto now = juce::Time::getMillisecondCounter();
        if (now - lastHwSnapshot > 1200) engine.snapshot ("Knob");
        lastHwSnapshot = now;
        if (auto* k = dynamic_cast<Knob*> (ctl[(size_t) index]))
        {
            k->setValue (v01, true);
            if (onHardwareStatus) onHardwareStatus (k->label + ":  " + (k->textFn ? k->textFn (k->value) : juce::String (juce::roundToInt (k->value * 100)) + " %"));
            int pi = k->getProperties().getWithDefault ("param", -1);
            if (autoWrite && pi >= 0 && engine.playing && ui.channelTarget == 2) writeAutomation (pi, k->value);
        }
        else if (auto* p = dynamic_cast<ValuePicker*> (ctl[(size_t) index]))
        {
            p->setIndex (juce::roundToInt (v01 * (float) juce::jmax (0, p->choices.size() - 1)), true);
            if (onHardwareStatus) onHardwareStatus (p->caption + ":  " + p->choices[p->index]);
        }
    }
    void pressTab (int i) { if (juce::isPositiveAndBelow (i, tabBtns.size())) tabBtns[i]->triggerClick(); }
    void stepPage (int d)
    {
        if (ui.channelMode == 0) { ui.paramPage = juce::jmax (0, ui.paramPage + d); }
        else { ui.propPage = (ui.propPage + d + 4) % 4; ui.propTab = 0; }
        ui.changed();
    }
private:
    juce::uint32 lastHwSnapshot = 0;
    void writeAutomation (int paramIndex, float v)
    {
        auto* G = engine.project().group (engine.focusedGroup);
        auto* p = engine.editPattern (engine.focusedGroup);
        if (G == nullptr || p == nullptr) return;
        juce::String target = "slot:" + juce::String (ui.selectedSlot) + ":" + juce::String (paramIndex);
        double pos = engine.position;
        juce::int64 base = 0;
        if (engine.songMode) { int ci = engine.editClip (engine.focusedGroup); if (juce::isPositiveAndBelow (ci, (int) G->clips.size())) base = G->clips[(size_t) ci].start; }
        auto local = (juce::int64) pos - base;
        if (p->length <= 0) return;
        local = ((local % p->length) + p->length) % p->length;
        int step = (int) (local / (PPQ / 4));
        const juce::ScopedLock sl (engine.lock);
        AutoLane* lane = nullptr;
        for (auto& l : p->lanes) if (l.sound == G->focusedSound && l.target == target) lane = &l;
        if (lane == nullptr)
        {
            AutoLane l; l.sound = G->focusedSound; l.target = target;
            auto* chain = targetChain();
            if (chain && juce::isPositiveAndBelow (ui.selectedSlot, (int) chain->size()) && (*chain)[(size_t) ui.selectedSlot]->proc)
                l.name = (*chain)[(size_t) ui.selectedSlot]->proc->getParameters()[paramIndex]->getName (10);
            p->lanes.push_back (l);
            lane = &p->lanes.back();
        }
        lane->steps[step] = v;
    }
public:

private:
    Engine& engine;
    Ui& ui;
    IconButton propsBtn { icons::knob }, pluginBtn { icons::plug }, collapseBtn { icons::chevronDown }, searchBtn { icons::search };
    juce::OwnedArray<FlatButton> targetTabs, pageBtns, tabBtns, extraBtns;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<ValuePicker> pickers;
    juce::OwnedArray<ToggleIcon> toggles;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
    std::vector<std::function<void()>> syncFns;   // pull values from the model into the controls
    int slotListY = 52;
    int dragSlot = -1;
    int lastKey = 0;

    // groove page state (UI-only, like the original apply-based tools)
    float varVel = 0.3f, varShift = 0.0f, varLen = 0.0f, varAmount = 0.5f; int varSpread = 1; bool varLive = false;
    float genProb = 0.5f, genVel = 0.3f; int genMode = 0, genSpread = 1;
    std::vector<NoteEvent> liveOriginal;
    const Pattern* liveSource = nullptr;   // identity only, never dereferenced

    juce::Rectangle<int> contentArea() const { return { iconW + listW + 1, 0, getWidth() - iconW - listW - 1, getHeight() - 1 }; }

    juce::String channelName()
    {
        auto& P = engine.project();
        if (ui.channelTarget == 0) return P.name;
        auto* g = P.group (engine.focusedGroup);
        if (g == nullptr) return {};
        if (ui.channelTarget == 1) return g->name;
        return g->sounds[(size_t) g->focusedSound]->name;
    }

    Engine::Chain* targetChain()
    {
        auto& P = engine.project();
        if (ui.channelTarget == 0) return &P.masterSlots;
        auto* g = P.group (engine.focusedGroup);
        if (g == nullptr) return nullptr;
        if (ui.channelTarget == 1) return &g->slots;
        return &g->sounds[(size_t) g->focusedSound]->slots;
    }
    Routing* targetRouting()
    {
        auto& P = engine.project();
        if (ui.channelTarget == 0) return &P.master;
        auto* g = P.group (engine.focusedGroup);
        if (g == nullptr) return nullptr;
        if (ui.channelTarget == 1) return &g->routing;
        return &g->sounds[(size_t) g->focusedSound]->routing;
    }
    Groove* targetGroove()
    {
        auto& P = engine.project();
        if (ui.channelTarget == 0) return &P.masterGroove;
        auto* g = P.group (engine.focusedGroup);
        if (g == nullptr) return nullptr;
        if (ui.channelTarget == 1) return &g->groove;
        return &g->sounds[(size_t) g->focusedSound]->groove;
    }
    std::array<Macro, 8>* targetMacros()
    {
        auto& P = engine.project();
        if (ui.channelTarget == 0) return &P.masterMacros;
        auto* g = P.group (engine.focusedGroup);
        if (g == nullptr) return nullptr;
        if (ui.channelTarget == 1) return &g->macros;
        return &g->sounds[(size_t) g->focusedSound]->macros;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }

    int layoutKey()
    {
        auto* chain = targetChain();
        if (ui.channelMode != 0 || chain == nullptr || ! juce::isPositiveAndBelow (ui.selectedSlot, (int) chain->size())) return 0;
        if (auto* ip = dynamic_cast<InternalProcessor*> ((*chain)[(size_t) ui.selectedSlot]->proc.get())) return ip->uiLayoutKey() * 2654435;
        return 0;
    }

    int stateKey()
    {
        auto* g = engine.project().group (engine.focusedGroup);
        auto* chain = targetChain();
        return (int) juce::String (ui.channelTarget).hashCode() ^ (engine.focusedGroup * 131) ^ ((g ? g->focusedSound : 0) * 7919)
             ^ (ui.channelMode * 17) ^ (ui.propPage * 1031) ^ (ui.propTab * 4099) ^ (ui.selectedSlot * 33331) ^ (ui.paramPage * 977)
             ^ ((chain ? (int) chain->size() : 0) * 15485863)
             ^ layoutKey()
             ^ identityKey();
    }

    // Controls built by rebuild() hold raw pointers into the model (Routing*, Groove*, Macro, parameters).
    // Undo / load / paste replace Sound / Group / Slot objects, so the key must change when they do,
    // otherwise timerCallback() would run syncFns on freed memory before the change message arrives.
    int identityKey()
    {
        auto h = [] (const void* p) { auto v = (juce::uint64) (juce::pointer_sized_uint) p; return (int) (v ^ (v >> 32)); };
        int k = h (&engine.project()) ^ (h (targetRouting()) * 31);
        if (auto* chain = targetChain())
            if (juce::isPositiveAndBelow (ui.selectedSlot, (int) chain->size()))
                k ^= h ((*chain)[(size_t) ui.selectedSlot]->proc.get()) * 131;
        return k;
    }

    Slot* selectedSlotPtr()
    {
        auto* chain = targetChain();
        if (chain == nullptr || ! juce::isPositiveAndBelow (ui.selectedSlot, (int) chain->size())) return nullptr;
        return (*chain)[(size_t) ui.selectedSlot].get();
    }

    void timerCallback() override
    {
        if (stateKey() != lastKey) rebuild();
        else for (auto& f : syncFns) f();
    }

    // ------------------------------------------------------------------------------------------
    void clearContent()
    {
        knobs.clear(); pickers.clear(); toggles.clear(); tabBtns.clear(); extraBtns.clear(); captions.clear(); syncFns.clear();
    }

    void rebuild()
    {
        lastKey = stateKey();
        clearContent();
        for (int i = 0; i < 3; ++i) { targetTabs[i]->setToggleState (ui.channelTarget == i, juce::dontSendNotification); targetTabs[i]->onColour = juce::Colours::white; }
        propsBtn.setToggleState (ui.channelMode == 1, juce::dontSendNotification);
        pluginBtn.setToggleState (ui.channelMode == 0, juce::dontSendNotification);
        for (int i = 0; i < 4; ++i) { pageBtns[i]->setVisible (ui.channelMode == 1); pageBtns[i]->setToggleState (ui.propPage == i, juce::dontSendNotification); }
        if (ui.channelMode == 0) buildPluginPage();
        else switch (ui.propPage)
        {
            case 0: buildInputPage(); break;
            case 1: buildOutputPage(); break;
            case 2: buildGroovePage(); break;
            default: buildMacroPage(); break;
        }
        layoutContent();
        repaint();
    }

    // layout helpers: controls are placed by build functions into absolute positions
    struct Place { juce::Component* c; juce::Rectangle<int> r; };
    std::vector<Place> places;
    void place (juce::Component* c, juce::Rectangle<int> r) { addAndMakeVisible (c); places.push_back ({ c, r }); }
    void layoutContent() { for (auto& p : places) if (p.c != nullptr) p.c->setBounds (p.r); }

    FlatButton* addTab (const juce::String& name, int index, int x)
    {
        auto* b = tabBtns.add (new FlatButton (name, 12.5f));
        b->style = FlatButton::tab;
        b->setToggleState (ui.propTab == index, juce::dontSendNotification);
        b->onClick = [this, index] { ui.propTab = index; ui.changed(); };
        place (b, { x, 6, (int) font (12.5f).getStringWidthFloat (name) + 10, 20 });
        return b;
    }

    void caption (const juce::String& t, int x, int y = 28) { captions.push_back ({ { x, y, 160, 14 }, t.toUpperCase() }); }

    Knob* addKnob (const juce::String& label, int x, int y, std::function<float()> get, std::function<void (float)> set, std::function<juce::String (float)> text, const juce::String& undoName, bool bipolar = false)
    {
        auto* k = knobs.add (new Knob (label));
        k->bipolar = bipolar;
        k->defaultValue = bipolar ? 0.5f : k->defaultValue;
        k->setValue (get());
        k->onChange = set;
        k->textFn = text;
        k->onDragStart = [this, undoName] { engine.snapshot (undoName); };
        syncFns.push_back ([k, get] { if (! k->isMouseButtonDown()) k->setValue (get()); });
        place (k, { x, y, 64, 66 });
        return k;
    }

    ValuePicker* addPicker (const juce::String& cap, const juce::StringArray& choices, int x, int y, std::function<int()> get, std::function<void (int)> set, const juce::String& undoName, int w = 80)
    {
        auto* p = pickers.add (new ValuePicker (cap));
        p->choices = choices;
        p->index = get();
        p->onBeforeChange = [this, undoName] { engine.snapshot (undoName); };
        p->onChange = set;
        syncFns.push_back ([p, get] { p->setIndex (get()); });
        place (p, { x, y, w, 66 });
        return p;
    }

    ToggleIcon* addToggle (const juce::String& cap, int x, int y, std::function<bool()> get, std::function<void (bool)> set)
    {
        auto* t = toggles.add (new ToggleIcon (cap));
        t->on = get();
        t->onChange = set;
        syncFns.push_back ([t, get] { if (t->on != get()) { t->on = get(); t->repaint(); } });
        place (t, { x, y + 14, 60, 50 });
        return t;
    }

    FlatButton* addButton (const juce::String& text, juce::Rectangle<int> r, std::function<void()> fn)
    {
        auto* b = extraBtns.add (new FlatButton (text, 12.0f));
        b->style = FlatButton::boxed;
        b->onClick = fn;
        place (b, r);
        return b;
    }

    // ------------------------------------------------------------------------------------------
    void buildPluginPage()
    {
        places.clear();
        auto* chain = targetChain();
        int x0 = iconW + listW + 20;
        if (chain == nullptr || chain->empty()) return;
        ui.selectedSlot = juce::jlimit (0, (int) chain->size() - 1, ui.selectedSlot);
        auto& slot = *(*chain)[(size_t) ui.selectedSlot];
        if (slot.proc == nullptr) return;
        auto& params = slot.proc->getParameters();
        if (auto* ip = dynamic_cast<InternalProcessor*> (slot.proc.get()))
        {
            auto pages = ip->uiPages();
            if (! pages.empty()) { buildPagedPlugin (*ip, pages, x0); return; }
        }
        const int perPage = juce::jmax (4, (getWidth() - x0 - 200) / 78);
        int pages = juce::jmax (1, (params.size() + perPage - 1) / perPage);
        ui.paramPage = juce::jlimit (0, pages - 1, ui.paramPage);
        // page tabs
        int tx = x0;
        for (int p = 0; p < juce::jmin (pages, 12); ++p)
        {
            auto name = p == 0 ? juce::String ("Main") : "Page " + juce::String (p + 1);
            auto* b = tabBtns.add (new FlatButton (name, 12.5f));
            b->style = FlatButton::tab;
            b->setToggleState (p == ui.paramPage, juce::dontSendNotification);
            b->onClick = [this, p] { ui.paramPage = p; ui.changed(); };
            int w = (int) font (12.5f).getStringWidthFloat (name) + 12;
            place (b, { tx, 6, w, 20 });
            tx += w + 6;
        }
        captions.push_back ({ { x0, 28, 300, 14 }, engine.describeSlot (slot).toUpperCase() });
        // edit button for plug-ins with their own GUI
        bool hasGui = slot.type == "VST3";
        // re-resolve the slot on click: a captured Slot& dangles once undo / load replaced it
        addButton (hasGui ? "Open Plug-in Window" : "Generic Editor", { getWidth() - 170, 8, 150, 22 }, [this] { if (auto* sp = selectedSlotPtr()) engine.openPluginEditor (*sp); });
        addButton (slot.bypass ? "Bypassed" : "Active", { getWidth() - 170, 34, 72, 22 }, [this] { if (auto* sp = selectedSlotPtr()) { engine.snapshot ("Bypass"); sp->bypass = ! sp->bypass; ui.changed(); } });
        addButton ("Presets", { getWidth() - 92, 34, 72, 22 }, [this] { slotMenu (ui.selectedSlot); });

        int x = x0;
        for (int i = ui.paramPage * perPage; i < juce::jmin (params.size(), (ui.paramPage + 1) * perPage); ++i)
        {
            auto* prm = params[i];
            auto label = prm->getName (14);
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm))
            {
                addPicker (label, ch->choices, x - 6, 44, [ch] { return ch->getIndex(); },
                           [ch] (int v) { ch->setValueNotifyingHost (ch->convertTo0to1 ((float) v)); }, "Parameter");
            }
            else
            {
                auto* k = addKnob (label, x, 44, [prm] { return prm->getValue(); },
                                   [prm] (float v) { prm->beginChangeGesture(); prm->setValueNotifyingHost (v); prm->endChangeGesture(); },
                                   [prm] (float v) { return prm->getText (v, 12) + (prm->getLabel().isNotEmpty() && ! prm->getText (v, 12).contains (prm->getLabel()) ? " " + prm->getLabel() : juce::String()); },
                                   "Parameter");
                k->defaultValue = prm->getDefaultValue();
                k->getProperties().set ("param", i);
                k->setTooltip (prm->getName (64));
            }
            x += 78;
        }
    }

    // Maschine-style pages: tab per page, section captions, 8 knob slots of 83 px
    void buildPagedPlugin (InternalProcessor& ip, const std::vector<InternalProcessor::UiPage>& pages, int x0)
    {
        ui.paramPage = juce::jlimit (0, (int) pages.size() - 1, ui.paramPage);
        int tx = x0;
        for (int p = 0; p < (int) pages.size(); ++p)
        {
            auto* b = tabBtns.add (new FlatButton (pages[(size_t) p].name, 12.5f));
            b->style = FlatButton::tab;
            b->setToggleState (p == ui.paramPage, juce::dontSendNotification);
            b->onClick = [this, p] { ui.paramPage = p; ui.changed(); };
            int w = (int) font (12.5f).getStringWidthFloat (pages[(size_t) p].name) + 12;
            place (b, { tx, 6, w, 20 });
            tx += w + 6;
        }
        addButton ("Generic Editor", { getWidth() - 140, 8, 120, 22 }, [this] { if (auto* sp = selectedSlotPtr()) engine.openPluginEditor (*sp); });
        auto& page = pages[(size_t) ui.paramPage];
        const int slotW = 83;
        for (auto& sec : page.sections) caption (sec.caption, x0 + sec.slot * slotW);
        auto& params = ip.getParameters();
        for (auto& item : page.items)
        {
            if (! juce::isPositiveAndBelow (item.param, params.size())) continue;
            auto* prm = params[item.param];
            int x = x0 + item.slot * slotW + 2;
            auto label = prm->getName (16);
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm))
            {
                if (ch->choices.size() == 2 && ch->choices[0] == "Off" && ch->choices[1] == "On")
                    addToggle (label, x + 2, 30, [ch] { return ch->getIndex() == 1; }, [this, ch] (bool v) { engine.snapshot ("Parameter"); ch->setValueNotifyingHost (v ? 1.0f : 0.0f); });
                else
                    addPicker (label, ch->choices, x - 8, 44, [ch] { return ch->getIndex(); },
                               [ch] (int v) { ch->setValueNotifyingHost (ch->convertTo0to1 ((float) v)); }, "Parameter");
            }
            else
            {
                bool bip = false;
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) { auto range = rp->getNormalisableRange(); bip = range.start < 0 && range.end > 0; }
                auto* k = addKnob (label, x, 44, [prm] { return prm->getValue(); },
                                   [prm] (float v) { prm->beginChangeGesture(); prm->setValueNotifyingHost (v); prm->endChangeGesture(); },
                                   [prm] (float v) { return prm->getText (v, 12); }, "Parameter", bip);
                k->defaultValue = prm->getDefaultValue();
                k->orangeArc = true;   // Maschine draws value arcs in orange
                k->getProperties().set ("param", item.param);
                k->setTooltip (prm->getName (64));
            }
        }
    }

    juce::StringArray audioInputChoices()
    {
        juce::StringArray a { "None" };
        for (int i = 1; i <= 16; ++i) a.add ("In " + juce::String (i) + " L+R");
        for (int i = 1; i <= 16; ++i) { a.add ("In " + juce::String (i) + " L"); a.add ("In " + juce::String (i) + " R"); }
        return a;
    }

    void buildInputPage()
    {
        places.clear();
        int x0 = iconW + listW + 20;
        auto* r = targetRouting();
        if (r == nullptr) return;
        if (ui.channelTarget == 0)
        {
            addTab ("Sync", 0, x0);
            caption ("MIDI Clock", x0);
            addPicker ("Mode", { "Off", "Send", "Receive" }, x0, 44, [this] { return engine.settings.getInt ("syncMode", 0); },
                       [this] (int v) { engine.settings.set ("syncMode", v); engine.reloadSettingsCache(); }, "Sync");
            return;
        }
        if (ui.channelTarget == 1)
        {
            addTab ("MIDI", 0, x0);
            caption ("Routings", x0);
            addPicker ("Key Mode", { "Off", "Drumkit", "Manual" }, x0, 44, [r] { return r->keyMode; }, [r] (int v) { r->keyMode = v; }, "Key Mode");
            return;
        }
        addTab ("Audio", 0, x0);
        addTab ("MIDI", 1, x0 + 56);
        if (ui.propTab == 0)
        {
            caption ("Audio", x0);
            auto choices = audioInputChoices();
            addPicker ("Source", choices, x0, 44, [r] { return r->audioIn; }, [r] (int v) { r->audioIn = v; }, "Audio Input", 90);
        }
        else
        {
            caption ("MIDI", x0);
            juce::StringArray src { "Default", "Focus", "None", "All" };
            src.addArray (engine.midiInputNames());
            addPicker ("Source", src, x0, 44, [r] { return r->midiSource; }, [r] (int v) { r->midiSource = v; }, "MIDI Input", 120);
            juce::StringArray ch { "All" };
            for (int i = 1; i <= 16; ++i) ch.add (juce::String (i));
            addPicker ("Channel", ch, x0 + 120, 44, [r] { return r->midiChannel; }, [r] (int v) { r->midiChannel = v; }, "MIDI Channel");
            addToggle ("Thru", x0 + 200, 30, [r] { return r->midiThru; }, [r] (bool v) { r->midiThru = v; });
        }
    }

    void buildOutputPage()
    {
        places.clear();
        int x0 = iconW + listW + 20;
        auto* r = targetRouting();
        if (r == nullptr) return;
        bool isSound = ui.channelTarget == 2;
        auto levelText = [] (float v) { return gainToText (normToLevel (v)); };
        auto panText = [] (float v) { auto p = juce::roundToInt ((v - 0.5f) * 200.0f); return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p)); };
        if (ui.channelTarget == 0)
        {
            addTab ("Audio", 0, x0);
            caption ("Audio", x0);
            juce::StringArray ext; for (int i = 1; i <= 16; ++i) ext.add ("Ext. " + juce::String (i));
            addPicker ("Output", ext, x0, 44, [r] { return juce::jmax (0, r->outDest - destExt1); }, [r] (int v) { r->outDest = destExt1 + v; }, "Master Output");
            addKnob ("Level", x0 + 90, 44, [r] { return levelToNorm (r->level); }, [r] (float v) { r->level = normToLevel (v); }, levelText, "Master Level");
            addKnob ("Pan", x0 + 170, 44, [r] { return (r->pan + 1.0f) * 0.5f; }, [r] (float v) { r->pan = v * 2.0f - 1.0f; }, panText, "Master Pan", true);
            caption ("Cue", x0 + 250);
            juce::StringArray cue { "Off" }; for (int i = 1; i <= 16; ++i) cue.add ("Ext. " + juce::String (i));
            addPicker ("Output", cue, x0 + 250, 44, [this] { return engine.cueDest.load(); }, [this] (int v) { engine.settings.set ("cueDest", v); engine.reloadSettingsCache(); }, "Cue Output");
            addKnob ("Level", x0 + 340, 44, [this] { return levelToNorm ((float) engine.settings.getDouble ("cueLevel", 1.0)); }, [this] (float v) { engine.settings.set ("cueLevel", normToLevel (v)); }, levelText, "Cue Level");
            addKnob ("Pan", x0 + 420, 44, [] { return 0.5f; }, [] (float) {}, panText, "Cue Pan", true);
            return;
        }
        addTab ("Audio", 0, x0);
        addTab ("Aux", 1, x0 + 56);
        if (ui.propTab == 0)
        {
            caption ("Audio", x0);
            addPicker ("Dest.", destChoices (isSound), x0, 44, [r, isSound] { return destToIndex (r->outDest, isSound); }, [r, isSound] (int v) { r->outDest = indexToDest (v, isSound); }, "Output");
            addToggle ("Cue", x0 + 80, 30, [r] { return r->cue; }, [r] (bool v) { r->cue = v; });
            addKnob ("Level", x0 + 140, 44, [r] { return levelToNorm (r->level); }, [r] (float v) { r->level = normToLevel (v); }, levelText, "Level")->orangeArc = true;
            addKnob ("Pan", x0 + 220, 44, [r] { return (r->pan + 1.0f) * 0.5f; }, [r] (float v) { r->pan = v * 2.0f - 1.0f; }, panText, "Pan", true);
        }
        else
        {
            juce::StringArray dests = destChoices (false);
            for (int a = 0; a < 2; ++a)
            {
                int x = x0 + a * 250;
                caption (a == 0 ? "Aux 1" : "Aux 2", x);
                // capture plain pointers (by value) instead of references to loop-local reference variables
                auto* dest = a == 0 ? &r->aux1Dest : &r->aux2Dest;
                auto* lvl = a == 0 ? &r->aux1Level : &r->aux2Level;
                auto* post = a == 0 ? &r->aux1Post : &r->aux2Post;
                addPicker ("Dest.", dests, x, 44, [dest] { return destToIndex (*dest, false); }, [dest] (int v) { *dest = indexToDest (v, false); }, "Aux Dest");
                addKnob ("Level", x + 80, 44, [lvl] { return levelToNorm (*lvl); }, [lvl] (float v) { *lvl = normToLevel (v); }, levelText, "Aux Level");
                addPicker ("Order", { "Post", "Pre" }, x + 150, 44, [post] { return *post ? 0 : 1; }, [post] (int v) { *post = v == 0; }, "Aux Order", 60);
            }
        }
    }

    void buildGroovePage()
    {
        places.clear();
        int x0 = iconW + listW + 20;
        auto* gr = targetGroove();
        if (gr == nullptr) return;
        addTab ("Swing", 0, x0);
        if (ui.channelTarget == 2) { addTab ("Variation", 1, x0 + 56); addTab ("Generate", 2, x0 + 132); }
        if (ui.propTab == 0 || ui.channelTarget != 2)
        {
            caption ("Swing", x0);
            if (ui.channelTarget == 0)
                addKnob ("Amount", x0, 44, [this] { return engine.project().swing; }, [this] (float v) { engine.project().swing = v; }, [] (float v) { return juce::String (v * 100.0f, 1) + " %"; }, "Swing");
            else
                addKnob ("Amount", x0, 44, [gr] { return gr->swing; }, [gr] (float v) { gr->swing = v; }, [] (float v) { return juce::String (v * 100.0f, 1) + " %"; }, "Swing");
            addPicker ("Cycle", { "1/16", "1/8", "1/4" }, x0 + 70, 44, [gr] { return gr->cycle; }, [gr] (int v) { gr->cycle = v; }, "Swing Cycle", 60);
            addToggle ("Invert", x0 + 140, 30, [gr] { return gr->invert; }, [gr] (bool v) { gr->invert = v; });
            return;
        }
        juce::StringArray spread { "Even", "Natural", "Skewed" };
        auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        if (ui.propTab == 1)
        {
            caption ("Velocity", x0);
            addKnob ("Amount", x0, 44, [this] { return varVel; }, [this] (float v) { varVel = v; liveApply(); }, pct, "Variation");
            caption ("Time Shift", x0 + 70);
            addKnob ("Step", x0 + 70, 44, [this] { return varShift; }, [this] (float v) { varShift = v; liveApply(); }, pct, "Variation");
            caption ("Note Length", x0 + 140);
            addKnob ("Steps", x0 + 140, 44, [this] { return varLen; }, [this] (float v) { varLen = v; liveApply(); }, pct, "Variation");
            addKnob ("Variation", x0 + 210, 44, [this] { return varAmount; }, [this] (float v) { varAmount = v; liveApply(); }, pct, "Variation");
            caption ("Spread", x0 + 330);
            addPicker ("Distribution", spread, x0 + 320, 44, [this] { return varSpread; }, [this] (int v) { varSpread = v; liveApply(); }, "Variation", 84);
            addToggle ("Live", x0 + 400, 30, [this] { return varLive; }, [this] (bool v) { varLive = v; if (v) captureLive(); });
            addButton ("Apply", { x0 + 480, 54, 52, 22 }, [this] { engine.snapshot ("Variation"); applyVariation (false); });
        }
        else
        {
            caption ("Engine", x0);
            addPicker ("Mode", { "Random", "Euclidean" }, x0 - 6, 44, [this] { return genMode; }, [this] (int v) { genMode = v; }, "Generate", 76);
            caption ("Probability", x0 + 80);
            addKnob ("Amount", x0 + 80, 44, [this] { return genProb; }, [this] (float v) { genProb = v; }, pct, "Generate")->orangeArc = true;
            caption ("Velocity", x0 + 150);
            addKnob ("Amount", x0 + 150, 44, [this] { return genVel; }, [this] (float v) { genVel = v; }, pct, "Generate");
            caption ("Spread", x0 + 330);
            addPicker ("Distribution", spread, x0 + 320, 44, [this] { return genSpread; }, [this] (int v) { genSpread = v; }, "Generate", 84);
            addButton ("Apply", { x0 + 480, 54, 52, 22 }, [this] { engine.snapshot ("Generate"); applyGenerate(); });
        }
    }

    float spreadRandom (juce::Random& rng, int mode)
    {
        if (mode == 0) return rng.nextFloat() * 2.0f - 1.0f;                       // Even
        if (mode == 1) { float s = 0; for (int i = 0; i < 4; ++i) s += rng.nextFloat(); return (s / 2.0f - 1.0f); }  // Natural (bell)
        return std::pow (rng.nextFloat(), 2.0f) * (rng.nextBool() ? 1.0f : -1.0f); // Skewed
    }

    void captureLive()
    {
        auto* g = engine.project().group (engine.focusedGroup);
        auto* p = engine.editPattern (engine.focusedGroup);
        liveOriginal.clear();
        liveSource = p;
        if (g && p) for (auto& n : p->notes) liveOriginal.push_back (n);
    }
    void liveApply() { if (varLive) applyVariation (true); }

    void applyVariation (bool fromOriginal)
    {
        auto* g = engine.project().group (engine.focusedGroup);
        auto* p = engine.editPattern (engine.focusedGroup);
        if (g == nullptr || p == nullptr) return;
        juce::Random rng;
        const juce::ScopedLock sl (engine.lock);
        // never paste the captured notes into a different pattern (focus / clip / scene changed since capture)
        if (fromOriginal && p != liveSource) captureLive();
        if (fromOriginal && ! liveOriginal.empty()) p->notes = liveOriginal;
        auto step = (juce::int64) Engine::gridTicks (engine.stepGrid);
        for (auto& n : p->notes)
        {
            if (n.sound != g->focusedSound || rng.nextFloat() > varAmount) continue;
            n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity + spreadRandom (rng, varSpread) * varVel * 0.6f);
            auto shift = (juce::int64) std::round (spreadRandom (rng, varSpread) * varShift * step);
            n.start = std::clamp<juce::int64> (n.start + shift, 0, p->length - 1);
            n.length = std::max<juce::int64> (PPQ / 32, n.length + (juce::int64) std::round (spreadRandom (rng, varSpread) * varLen * step * 2));
        }
        ui.changed();
    }

    void applyGenerate()
    {
        auto* g = engine.project().group (engine.focusedGroup);
        if (g == nullptr) return;
        auto* p = engine.editPattern (engine.focusedGroup);
        if (p == nullptr)
        {
            p = engine.newPatternFor (*g, engine.songMode ? engine.project().ticksPerBar() : engine.defaultPatternLength());
            const juce::ScopedLock sl (engine.lock);
            int pi = (int) g->patterns.size() - 1;
            if (engine.songMode) { g->clips.push_back ({ 0, p->length, pi }); engine.setEditClip (engine.focusedGroup, (int) g->clips.size() - 1); }
            else { engine.ensureSceneSlots(); engine.project().scenes[(size_t) engine.project().currentScene].patternForGroup[(size_t) engine.focusedGroup] = pi; }
        }
        juce::Random rng;
        auto step = (juce::int64) Engine::gridTicks (engine.stepGrid);
        int steps = (int) (p->length / step);
        int si = g->focusedSound;
        const juce::ScopedLock sl (engine.lock);
        p->notes.erase (std::remove_if (p->notes.begin(), p->notes.end(), [si] (auto& n) { return n.sound == si; }), p->notes.end());
        int pulses = juce::jlimit (0, steps, juce::roundToInt (genProb * steps));
        for (int i = 0; i < steps; ++i)
        {
            bool hit = genMode == 0 ? rng.nextFloat() < genProb : (pulses > 0 && ((i * pulses) % steps) < pulses);
            if (! hit) continue;
            NoteEvent n; n.sound = si; n.start = i * step; n.length = step;
            n.velocity = juce::jlimit (0.05f, 1.0f, 0.8f + spreadRandom (rng, genSpread) * genVel * 0.6f);
            p->notes.push_back (n);
        }
        ui.changed();
    }

    void buildMacroPage()
    {
        places.clear();
        int x0 = iconW + listW + 20;
        addTab ("Page 1", 0, x0);
        auto* macros = targetMacros();
        auto* chain = targetChain();
        if (macros == nullptr || chain == nullptr) return;
        for (int i = 0; i < 8; ++i)
        {
            auto& m = (*macros)[(size_t) i];
            juce::String label = m.name.isNotEmpty() ? m.name : "Macro " + juce::String (i + 1);
            auto getParam = [this, i]() -> juce::AudioProcessorParameter*
            {
                auto* ms = targetMacros(); auto* ch = targetChain();
                if (! ms || ! ch) return nullptr;
                auto& mm = (*ms)[(size_t) i];
                if (! juce::isPositiveAndBelow (mm.slot, (int) ch->size())) return nullptr;
                auto* proc = (*ch)[(size_t) mm.slot]->proc.get();
                if (proc == nullptr || ! juce::isPositiveAndBelow (mm.param, proc->getParameters().size())) return nullptr;
                return proc->getParameters()[mm.param];
            };
            auto macroAt = [this, i]() -> Macro* { auto* ms = targetMacros(); return ms != nullptr ? &(*ms)[(size_t) i] : nullptr; };
            auto* k = addKnob (label, x0 + i * 78, 44, [getParam, macroAt] { if (auto* p = getParam()) return p->getValue(); auto* mm = macroAt(); return mm != nullptr ? mm->value : 0.5f; },
                               [getParam, macroAt] (float v) { if (auto* mm = macroAt()) mm->value = v; if (auto* p = getParam()) p->setValueNotifyingHost (v); },
                               [getParam] (float v) { if (auto* p = getParam()) return p->getText (v, 12); return juce::String (juce::roundToInt (v * 100)) + " %"; }, "Macro");
            k->setTooltip ("Right-click to assign");
            k->onRightClick = [this, i]
            {
                auto* ch = targetChain();
                if (ch == nullptr) return;
                juce::PopupMenu menu;
                menu.addItem (1, "Unassign");
                menu.addItem (2, "Rename...");
                for (int s = 0; s < (int) ch->size(); ++s)
                {
                    juce::PopupMenu sub;
                    if (auto* proc = (*ch)[(size_t) s]->proc.get())
                        for (int p = 0; p < juce::jmin (128, proc->getParameters().size()); ++p) sub.addItem (1000 + s * 1000 + p, proc->getParameters()[p]->getName (40));
                    menu.addSubMenu (engine.describeSlot (*(*ch)[(size_t) s]), sub);
                }
                showMenu (menu, this, [this, i] (int r)
                {
                    auto* ms = targetMacros(); auto* ch2 = targetChain();
                    if (ms == nullptr) return;
                    auto& mm = (*ms)[(size_t) i];
                    engine.snapshot ("Macro Assign");
                    if (r == 1) { mm.slot = -1; mm.param = -1; mm.name = {}; }
                    else if (r == 2) actions::askText ("Macro Name", mm.name, [this, i] (juce::String t) { if (auto* ms2 = targetMacros()) { (*ms2)[(size_t) i].name = t; ui.changed(); } });
                    else if (r >= 1000 && ch2 != nullptr)
                    {
                        mm.slot = r / 1000 - 1; mm.param = r % 1000;
                        if (juce::isPositiveAndBelow (mm.slot, (int) ch2->size()) && (*ch2)[(size_t) mm.slot]->proc)
                            mm.name = (*ch2)[(size_t) mm.slot]->proc->getParameters()[mm.param]->getName (14);
                    }
                    ui.changed();
                });
            };
        }
    }

    // ------------------------------------------------------------------------------------------
    // Plug-in menus (None / Internal / VST3 / VST, Cut / Copy / Paste, Open / Save / Save As / Save As Default)
    juce::String clipboardSlot;   // XML of a copied slot

public:
    void addMenu (int index, bool replace, juce::Component* menuTarget = nullptr)
    {
        juce::PopupMenu m, internal, vst3, vst, vInst, vFx;
        int id = 1;
        std::map<int, juce::String> internalIds;
        for (auto& sec : internalMenu())
        {
            for (auto& name : sec.items)
            {
                if (name == "Drum Synth")
                {
                    juce::PopupMenu ds;
                    for (auto& d : drumSynthModels()) { internalIds[++id] = d; ds.addItem (id, d); }
                    internal.addSubMenu ("Drum Synth", ds);
                    continue;
                }
                internalIds[++id] = name;
                internal.addItem (id, name);
            }
            internal.addSeparator();
        }
        std::map<int, juce::String> pluginIds;
        std::map<juce::String, juce::PopupMenu> instByVendor, fxByVendor;
        auto types = engine.knownPlugins.getTypes();
        std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        int pid = 5000;
        bool useVst3 = engine.settings.getBool ("useVST3", true);
        auto blocked = engine.settings.getList ("disabledPlugins");
        for (auto& d : types)
        {
            if (! useVst3 || blocked.contains (d.createIdentifierString())) continue;
            pluginIds[++pid] = d.createIdentifierString();
            auto vendor = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
            (d.isInstrument ? instByVendor : fxByVendor)[vendor].addItem (pid, d.name);
        }
        for (auto& [v, sub] : instByVendor) vInst.addSubMenu (v, sub);
        for (auto& [v, sub] : fxByVendor) vFx.addSubMenu (v, sub);
        vst3.addSubMenu ("Instruments", vInst, vInst.getNumItems() > 0);
        vst3.addSubMenu ("Effects", vFx, vFx.getNumItems() > 0);
        if (types.isEmpty()) vst3.addItem (4, "Scan for plug-ins... (Preferences > Plug-ins)");
        vst.addItem (-1, "VST2 is not supported - use VST3 versions", false);

        auto* chain = targetChain();
        bool hasSlot = chain != nullptr && juce::isPositiveAndBelow (index, (int) chain->size());
        m.addItem (1, "None", replace && hasSlot);
        m.addSubMenu ("Internal", internal);
        m.addSubMenu ("VST3", vst3);
        m.addSubMenu ("VST", vst);
        m.addSeparator();
        m.addItem (2, "Cut", hasSlot);
        m.addItem (3, "Copy", hasSlot);
        m.addItem (6, "Paste", clipboardSlot.isNotEmpty());
        m.addSeparator();
        m.addItem (7, "Open...");
        m.addItem (8, "Save", hasSlot);
        m.addItem (9, "Save As...", hasSlot);
        m.addItem (10, "Save As Default...", hasSlot);

        auto showFn = [this, menuTarget] (juce::PopupMenu& menu, std::function<void (int)> cb) { if (menuTarget != nullptr) showMenuAt (menu, juce::Desktop::getMousePosition(), cb); else showMenu (menu, this, cb); };
        showFn (m, [this, index, replace, internalIds, pluginIds] (int r)
        {
            auto* chain = targetChain();
            if (chain == nullptr) return;
            bool hasSlot = juce::isPositiveAndBelow (index, (int) chain->size());
            auto doInsert = [this, chain, index, replace, hasSlot] (const juce::String& type, const juce::String& pid)
            {
                juce::String err;
                auto slot = engine.createSlot (type, pid, err);
                if (slot == nullptr) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groovebox Pro", err); return; }
                applyDefaultPreset (*slot);
                engine.snapshot ("Add " + slot->displayName);
                if (replace && hasSlot) engine.removeSlot (*chain, index);
                bool isSampler = dynamic_cast<SamplerProcessor*> (slot->proc.get()) != nullptr;
                const int at = juce::jlimit (0, (int) chain->size(), index);   // same clamp insertSlot applies
                engine.insertSlot (*chain, at, std::move (slot));
                if (isSampler && ui.channelTarget == 2)
                    if (auto* g = engine.project().group (engine.focusedGroup))
                        if (auto* sp = dynamic_cast<SamplerProcessor*> ((*chain)[(size_t) at]->proc.get())) sp->rootKey = g->sounds[(size_t) g->focusedSound]->rootKey;
                ui.selectedSlot = at;
                ui.changed();
            };
            if (auto it = internalIds.find (r); it != internalIds.end()) { doInsert (it->second, {}); return; }
            if (auto it = pluginIds.find (r); it != pluginIds.end()) { doInsert ("VST3", it->second); return; }
            if (r == 1 && hasSlot) { engine.snapshot ("Remove Plug-in"); engine.removeSlot (*chain, index); ui.changed(); }
            else if ((r == 2 || r == 3) && hasSlot)
            {
                clipboardSlot = slotToXml (*(*chain)[(size_t) index]);
                if (r == 2) { engine.snapshot ("Cut Plug-in"); engine.removeSlot (*chain, index); ui.changed(); }
            }
            else if (r == 6) pasteSlot (index);
            else if (r == 7)
                actions::chooseFile ("Open Preset", Settings::userFolder().getChildFile ("Presets"), "*.gbpreset", false, [this, index] (juce::File f) { clipboardSlot = f.loadFileAsString(); pasteSlot (index, true); });
            else if ((r == 8 || r == 9) && hasSlot)
            {
                auto folder = Settings::userFolder().getChildFile ("Presets");
                folder.createDirectory();
                auto& s = *(*chain)[(size_t) index];
                // serialise now: the async chooser outlives any Slot& (undo / project load while it is open)
                auto xml = slotToXml (s);
                actions::chooseFile ("Save Preset", folder.getChildFile (engine.describeSlot (s) + ".gbpreset"), "*.gbpreset", true, [xml] (juce::File f) { f.withFileExtension ("gbpreset").replaceWithText (xml); });
            }
            else if (r == 10 && hasSlot)
            {
                auto& s = *(*chain)[(size_t) index];
                defaultPresetFile (s).getParentDirectory().createDirectory();
                defaultPresetFile (s).replaceWithText (slotToXml (s));
            }
            else if (r == 4) { if (onOpenPreferences) onOpenPreferences (5); }
        });
    }

public:
    std::function<void (int page)> onOpenPreferences;
private:
    juce::File defaultPresetFile (const Slot& s)
    {
        auto key = s.type == "VST3" ? juce::File::createLegalFileName (s.pluginId).substring (0, 80) : s.type;
        return Settings::userFolder().getChildFile ("Presets").getChildFile ("Defaults").getChildFile (key + ".gbpreset");
    }
    void applyDefaultPreset (Slot& s)
    {
        auto f = defaultPresetFile (s);
        if (! f.existsAsFile() || s.proc == nullptr) return;
        if (auto x = juce::XmlDocument::parse (f))
        {
            juce::MemoryBlock mb; mb.fromBase64Encoding (x->getStringAttribute ("state"));
            if (mb.getSize() > 0) s.proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
    }
    juce::String slotToXml (Slot& s)
    {
        juce::XmlElement x ("GBPRESET");
        x.setAttribute ("type", s.type);
        x.setAttribute ("id", s.pluginId);
        x.setAttribute ("name", s.displayName);
        juce::MemoryBlock mb;
        if (s.proc) s.proc->getStateInformation (mb);
        x.setAttribute ("state", mb.toBase64Encoding());
        return x.toString();
    }
    void pasteSlot (int index, bool replace = false)
    {
        auto* chain = targetChain();
        auto x = juce::XmlDocument::parse (clipboardSlot);
        if (chain == nullptr || x == nullptr) return;
        juce::String err;
        auto slot = engine.createSlot (x->getStringAttribute ("type"), x->getStringAttribute ("id"), err);
        if (slot == nullptr) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Groovebox Pro", err); return; }
        juce::MemoryBlock mb; mb.fromBase64Encoding (x->getStringAttribute ("state"));
        if (mb.getSize() > 0) slot->proc->setStateInformation (mb.getData(), (int) mb.getSize());
        engine.snapshot ("Paste Plug-in");
        if (replace && juce::isPositiveAndBelow (index, (int) chain->size())) engine.removeSlot (*chain, index);
        engine.insertSlot (*chain, index, std::move (slot));
        ui.selectedSlot = index;
        ui.changed();
    }

public:
    void slotMenu (int idx, juce::Component* menuTarget = nullptr)
    {
        auto* chain = targetChain();
        if (chain == nullptr || ! juce::isPositiveAndBelow (idx, (int) chain->size())) return;
        juce::PopupMenu m;
        m.addItem (1, "Open Editor");
        m.addItem (2, (*chain)[(size_t) idx]->bypass ? "Enable" : "Bypass");
        m.addItem (3, "Move Up", idx > 0);
        m.addItem (4, "Move Down", idx < (int) chain->size() - 1);
        m.addItem (5, "Replace...");
        m.addItem (6, "Insert Before...");
        m.addItem (7, "Remove");
        juce::Component::SafePointer<juce::Component> tgt (menuTarget);
        auto showFn = [this, menuTarget] (juce::PopupMenu& menu, std::function<void (int)> cb) { if (menuTarget != nullptr) showMenuAt (menu, juce::Desktop::getMousePosition(), cb); else showMenu (menu, this, cb); };
        showFn (m, [this, idx, tgt] (int r)
        {
            auto* chain = targetChain();
            if (chain == nullptr || ! juce::isPositiveAndBelow (idx, (int) chain->size())) return;
            if (r == 1) engine.openPluginEditor (*(*chain)[(size_t) idx]);
            else if (r == 2) { engine.snapshot ("Bypass"); (*chain)[(size_t) idx]->bypass = ! (*chain)[(size_t) idx]->bypass; }
            else if (r == 3) { engine.snapshot ("Move Plug-in"); engine.moveSlot (*chain, idx, idx - 1); ui.selectedSlot = idx - 1; }
            else if (r == 4) { engine.snapshot ("Move Plug-in"); engine.moveSlot (*chain, idx, idx + 1); ui.selectedSlot = idx + 1; }
            else if (r == 5) { addMenu (idx, true, tgt.getComponent()); return; }
            else if (r == 6) { addMenu (idx, false, tgt.getComponent()); return; }
            else if (r == 7) { engine.snapshot ("Remove Plug-in"); engine.removeSlot (*chain, idx); }
            ui.changed();
        });
    }
};
} // namespace gb
