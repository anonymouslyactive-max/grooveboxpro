#pragma once
#include "Theme.h"
#include "Engine.h"

namespace gb
{
// Shared GUI state
struct Ui : juce::ChangeBroadcaster
{
    int channelTarget = 2;      // 0 master, 1 group, 2 sound
    int channelMode = 0;        // 0 plug-ins, 1 properties
    int propPage = 1;           // 0 input, 1 output, 2 groove, 3 macro
    int propTab = 0;
    int selectedSlot = 0;
    int paramPage = 0;
    int editorMode = 0;         // 0 pattern, 1 sampling
    int samplingTab = 0;        // 0 record, 1 edit, 2 slice
    bool padView = false;
    bool keyboardView = false;  // piano roll for focused sound
    bool pencil = false;
    bool showBrowser = false, showMixer = false;
    bool channelCollapsed = false, editorCollapsed = false;
    int arrGridBars = 1;
    int laneMode = 0;           // 0 velocity, 1 probability, 2.. automation lanes
    double pxPerTick = 0.0;     // editor zoom (0 = auto)
    double scrollTick = 0.0;
    double arrPxPerTick = 0.0;
    double arrScrollTick = 0.0;
    // vertical zoom / scroll (0 = automatic size)
    int drumRowH = 0, drumScroll = 0;   // pattern editor sound rows
    int keyH = 0;                       // piano roll key height
    int laneH = 88;                     // velocity / automation lane height
    int arrRowH = 24, arrScrollY = 0;   // song view group rows
    int ideasSceneW = 100;              // ideas view scene column width
    int mixerStripW = 0, mixerScroll = 0;
    bool autoWrite = false, stepMode = false;
    int stripMode = 0;          // touch strip: 0 pitch, 1 mod, 2 perform, 3 notes
    int stepPage = 0;
    void changed() { sendChangeMessage(); }
};

// ---------------------------------------------------------------------------------------------
inline void showMenu (juce::PopupMenu& m, juce::Component* target, std::function<void (int)> cb)
{
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withStandardItemHeight (22),
                     [cb] (int r) { if (r != 0 && cb) cb (r); });
}

inline void showMenuAt (juce::PopupMenu& m, juce::Point<int> screenPos, std::function<void (int)> cb)
{
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }).withStandardItemHeight (22),
                     [cb] (int r) { if (r != 0 && cb) cb (r); });
}

// ---------------------------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    IconButton (icons::Id i, const juce::String& tip = {}) : Button (tip), icon (i) { setTooltip (tip); }
    icons::Id icon;
    juce::Colour on = juce::Colours::white, off = col::textDim;
    bool drawBox = false;
    float iconScale = 0.62f;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (drawBox || (getToggleState() && getClickingTogglesState() && boxWhenOn))
        {
            g.setColour (getToggleState() ? col::buttonHi : col::button);
            g.fillRoundedRectangle (r.reduced (1), 2.0f);
        }
        auto c = getToggleState() ? on : off;
        if (over) c = c.brighter (0.3f);
        if (down) c = c.darker (0.2f);
        if (! isEnabled()) c = c.withAlpha (0.35f);
        auto s = juce::jmin (r.getWidth(), r.getHeight()) * iconScale;
        icons::draw (g, icon, r.withSizeKeepingCentre (s, s), c);
    }
    bool boxWhenOn = false;
};

// Flat text button / tab
class FlatButton : public juce::Button
{
public:
    explicit FlatButton (const juce::String& t, float fh = 12.5f) : Button (t), fontH (fh) {}
    enum Style { plain, tab, boxed, menuItem };
    Style style = plain;
    juce::Colour onColour = juce::Colours::white;
    icons::Id icon = icons::plus; bool hasIcon = false;
    juce::Justification just = juce::Justification::centred;
    float fontH;
    bool bold = false;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        bool on = getToggleState();
        if (style == boxed)
        {
            g.setColour (on ? col::buttonHi : (over ? col::button.brighter (0.05f) : col::button));
            g.fillRoundedRectangle (r.reduced (0.5f), 2.0f);
        }
        else if (style == menuItem && (on || over))
        {
            g.setColour (on ? col::buttonHi : col::row);
            g.fillRoundedRectangle (r, 2.0f);
        }
        auto c = on ? onColour : (over ? col::text : col::textDim);
        if (style == boxed) c = on ? juce::Colours::white : col::text;
        if (down) c = c.darker (0.2f);
        if (! isEnabled()) c = c.withAlpha (0.4f);
        g.setColour (c);
        g.setFont (font (fontH, bold));
        auto tr = r.reduced (style == menuItem ? 6.0f : 2.0f, 0);
        if (hasIcon)
        {
            auto ir = tr.removeFromLeft (fontH + 6);
            icons::draw (g, icon, ir.withSizeKeepingCentre (fontH, fontH), c);
            tr.removeFromLeft (4);
            g.drawText (getButtonText(), tr, juce::Justification::centredLeft);
        }
        else g.drawText (getButtonText(), tr, just);
        if (style == tab && on)
        {
            g.setColour (juce::Colours::white);
            auto w = font (fontH, bold).getStringWidthFloat (getButtonText());
            float x = just == juce::Justification::centredLeft ? 2.0f : (r.getWidth() - w) * 0.5f;
            g.fillRect (x, r.getBottom() - 2.0f, w, 1.5f);
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Arc knob in the original style: white value arc on a dark ring, label underneath.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (const juce::String& lbl = {}) : label (lbl) {}
    juce::String label;
    float value = 0.5f, defaultValue = 0.5f;   // normalised
    bool bipolar = false;
    bool orangeArc = false;
    std::function<void (float)> onChange;
    std::function<void()> onDragStart;
    std::function<juce::String (float)> textFn;
    std::function<void()> onRightClick;

    void setValue (float v, bool notify = false)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - value) < 1e-6f) return;
        value = v; repaint();
        if (notify && onChange) onChange (value);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto lab = r.removeFromBottom (18.0f);
        auto d = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
        auto kr = r.withSizeKeepingCentre (d, d);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        auto cx = kr.getCentreX(), cy = kr.getCentreY(), rad = d * 0.5f - 2.0f;
        juce::Path track; track.addCentredArc (cx, cy, rad, rad, 0, a0, a1, true);
        g.setColour (col::knobTrack);
        g.strokePath (track, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float av = a0 + (a1 - a0) * value;
        float from = bipolar ? (a0 + a1) * 0.5f : a0;
        juce::Path arc; arc.addCentredArc (cx, cy, rad, rad, 0, juce::jmin (from, av), juce::jmax (from, av), true);
        g.setColour (orangeArc ? col::orange : juce::Colours::white);
        if (std::abs (av - from) > 0.001f) g.strokePath (arc, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // pointer
        juce::Point<float> p1 (cx + std::sin (av) * rad * 0.35f, cy - std::cos (av) * rad * 0.35f), p2 (cx + std::sin (av) * rad * 0.8f, cy - std::cos (av) * rad * 0.8f);
        g.setColour (juce::Colours::white);
        g.drawLine ({ p1, p2 }, 2.0f);
        g.setColour (dragging || isMouseOver() || orangeArc ? col::text : col::textDim);
        g.setFont (font (12.0f));
        auto txt = (dragging && textFn) ? textFn (value) : label;
        g.drawFittedText (txt, lab.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        dragStartValue = value; dragging = true;
        if (onDragStart) onDragStart();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        float sens = e.mods.isShiftDown() ? 0.0015f : 0.006f;
        setValue (dragStartValue - (float) e.getDistanceFromDragStartY() * sens + (float) e.getDistanceFromDragStartX() * sens * 0.3f, true);
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDragStart) onDragStart(); setValue (defaultValue, true); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (onDragStart) onDragStart();
        setValue (value + w.deltaY * 0.05f, true);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    float dragStartValue = 0; bool dragging = false;
};

// ---------------------------------------------------------------------------------------------
// Orange value with small up/down arrows and a caption below (e.g. "Natural / Distribution")
class ValuePicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValuePicker (const juce::String& cap = {}) : caption (cap) {}
    juce::String caption;
    juce::StringArray choices;
    int index = 0;
    bool orange = true;
    std::function<void (int)> onChange;
    std::function<void()> onBeforeChange;
    std::function<void (juce::PopupMenu&)> customMenu;   // optional: build your own menu (ids = index + 1)

    void setIndex (int i, bool notify = false)
    {
        i = juce::jlimit (0, juce::jmax (0, choices.size() - 1), i);
        if (i == index && ! notify) return;
        index = i; repaint();
        if (notify && onChange) onChange (index);
    }
    bool compact = false;   // caption above, plain value below (sampling page style)
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (compact)
        {
            g.setColour (col::textDim);
            g.setFont (font (10.0f, true));
            g.drawText (caption, r.removeFromTop (16.0f), juce::Justification::centred);
            g.setColour (isMouseOver() ? juce::Colours::white : col::text);
            g.setFont (font (12.5f));
            g.drawText (choices[index], r.removeFromTop (18.0f), juce::Justification::centred);
            return;
        }
        auto cap = caption.isNotEmpty() ? r.removeFromBottom (18.0f) : juce::Rectangle<float>();
        auto c = orange ? col::orange : col::text;
        if (isMouseOver()) c = c.brighter (0.2f);
        g.setColour (c.withAlpha (0.9f));
        auto mid = r.getCentre();
        juce::Path up, dn;
        up.addTriangle (mid.x - 3.5f, mid.y - 11, mid.x + 3.5f, mid.y - 11, mid.x, mid.y - 15);
        dn.addTriangle (mid.x - 3.5f, mid.y + 11, mid.x + 3.5f, mid.y + 11, mid.x, mid.y + 15);
        g.setColour (col::text);
        g.fillPath (up); g.fillPath (dn);
        g.setColour (c);
        g.setFont (font (13.5f));
        g.drawFittedText (choices[index], r.toNearestInt(), juce::Justification::centred, 1);
        if (caption.isNotEmpty())
        {
            g.setColour (col::text);
            g.setFont (font (12.0f));
            g.drawFittedText (caption, cap.toNearestInt(), juce::Justification::centred, 1);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { dragY = e.y; moved = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (std::abs (e.y - dragY) > 12)
        {
            if (onBeforeChange) onBeforeChange();
            setIndex (index + (e.y < dragY ? -1 : 1), true);
            dragY = e.y; moved = true;
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (moved) return;
        if (compact)
        {
            juce::PopupMenu m;
            for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == index);
            showMenu (m, this, [this] (int r) { if (onBeforeChange) onBeforeChange(); setIndex (r - 1, true); });
            return;
        }
        auto mid = getHeight() / 2 - (caption.isNotEmpty() ? 9 : 0);
        if (e.y < mid - 9 && e.x > getWidth() / 2 - 12 && e.x < getWidth() / 2 + 12) { if (onBeforeChange) onBeforeChange(); setIndex (index - 1, true); return; }
        if (e.y > mid + 9 && e.y < mid + 18 && e.x > getWidth() / 2 - 12 && e.x < getWidth() / 2 + 12) { if (onBeforeChange) onBeforeChange(); setIndex (index + 1, true); return; }
        juce::PopupMenu m;
        if (customMenu) customMenu (m);
        else for (int i = 0; i < choices.size(); ++i) m.addItem (i + 1, choices[i], true, i == index);
        showMenu (m, this, [this] (int r) { if (onBeforeChange) onBeforeChange(); setIndex (r - 1, true); });
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (onBeforeChange) onBeforeChange();
        setIndex (index + (w.deltaY > 0 ? -1 : 1), true);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    int dragY = 0; bool moved = false;
};

// Small power-style toggle (e.g. "Invert", "Thru", "Live")
class ToggleIcon : public juce::Component
{
public:
    ToggleIcon (const juce::String& cap) : caption (cap) {}
    juce::String caption; bool on = false;
    std::function<void (bool)> onChange;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cap = r.removeFromBottom (18.0f);
        icons::draw (g, icons::power, r.withSizeKeepingCentre (14, 14), on ? col::orange : col::textDark);
        g.setColour (col::text); g.setFont (font (12.0f));
        g.drawFittedText (caption, cap.toNearestInt(), juce::Justification::centred, 1);
    }
    void mouseUp (const juce::MouseEvent&) override { on = ! on; repaint(); if (onChange) onChange (on); }
};

inline juce::String gainToText (float lin)
{
    if (lin <= 0.0001f) return "-inf dB";
    return juce::String (juce::Decibels::gainToDecibels (lin), 1) + " dB";
}

// Level knob mapping: normalised 0..1 <-> linear gain (-inf .. +6 dB), 0 dB at 0.75
inline float levelToNorm (float lin) { if (lin <= 0.0001f) return 0.0f; auto db = juce::Decibels::gainToDecibels (lin); return juce::jlimit (0.0f, 1.0f, db >= -48.0f ? 0.75f + db / 64.0f : 0.0f); }
inline float normToLevel (float n) { if (n <= 0.001f) return 0.0f; return juce::Decibels::decibelsToGain ((n - 0.75f) * 64.0f); }

// Builds Dest picker choices: None, (Group), Master, Ext. 1..16
inline juce::StringArray destChoices (bool sound)
{
    juce::StringArray a { "None" };
    if (sound) a.add ("Group");
    a.add ("Master");
    for (int i = 1; i <= 16; ++i) a.add ("Ext. " + juce::String (i));
    return a;
}
inline int destToIndex (int d, bool sound) { if (d == destNone) return 0; if (sound) return d; return d == destGroup ? 1 : d - 1; }
inline int indexToDest (int i, bool sound) { if (i == 0) return destNone; return sound ? i : i + 1; }
} // namespace gb
