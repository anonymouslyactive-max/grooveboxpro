#pragma once
#include "Actions.h"

// Sample editor in the style of the original: Record / Edit / Slice / Zone
namespace gb
{
// ---------------------------------------------------------------------------------------------
// Big stereo waveform with seconds ruler, S / E play-range markers, selection, loop and slices
class WaveformView : public juce::Component
{
public:
    WaveformView() = default;
    SamplePtr sample;
    double viewStart = 0.0, viewEnd = 1.0;   // normalised zoom window
    float start = 0.0f, end = 1.0f;          // play range (normalised)
    int selStart = -1, selEnd = -1;          // selection in samples
    bool loopOn = false; float loopStart = 0.0f, loopEnd = 1.0f;
    std::vector<float> slices; bool showSlices = false;
    float playPos = -1.0f;
    float ampZoom = 1.0f;                    // vertical zoom
    std::function<void (float, float)> onRegionChanged;
    std::function<void()> onDragStart, onSelectionChanged;

    static constexpr int rulerH = 18, scrollH = 10;

    void setSample (SamplePtr s)
    {
        if (s == sample) return;
        sample = s;
        viewStart = 0; viewEnd = 1; selStart = selEnd = -1;
        repaint();
    }
    int length() const { return sample ? sample->buffer.getNumSamples() : 0; }
    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedTop (rulerH).withTrimmedBottom (scrollH); }
    float normToX (double n) const { auto w = waveArea(); return (float) (w.getX() + (n - viewStart) / (viewEnd - viewStart) * w.getWidth()); }
    double xToNorm (float x) const { auto w = waveArea(); return viewStart + (x - w.getX()) / (double) w.getWidth() * (viewEnd - viewStart); }

    void paint (juce::Graphics& g) override
    {
        auto wa = waveArea();
        g.fillAll (col::editorBg);
        // ruler
        g.setColour (col::bg);
        g.fillRect (0, 0, getWidth(), rulerH);
        if (sample == nullptr)
        {
            g.setColour (col::textDark);
            g.setFont (font (13.0f));
            g.drawText ("No sample on the focused sound - record one, or drag a sample from the browser onto a sound", wa, juce::Justification::centred);
            return;
        }
        const double secs = length() / sample->sampleRate;
        const double vs = viewStart * secs, ve = viewEnd * secs;
        double step = 0.001;
        for (double s : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
            if ((wa.getWidth() / ((ve - vs) / s)) >= 60) { step = s; break; } else step = s;
        g.setFont (font (11.5f));
        for (double t = std::ceil (vs / step) * step; t <= ve + 1e-9; t += step)
        {
            float x = normToX (t / secs);
            g.setColour (col::textDim);
            g.fillRect (x, (float) rulerH - 7, 1.0f, 7.0f);
            g.drawText (juce::String (t, step < 0.01 ? 3 : 2) + "s", (int) x + 4, 1, 60, rulerH - 3, juce::Justification::centredLeft);
            for (int k = 1; k < 5; ++k) { float sx = normToX ((t + step * k / 5.0) / secs); g.fillRect (sx, (float) rulerH - 3, 1.0f, 3.0f); }
        }
        // background: play range lighter
        g.setColour (juce::Colour (0xff262626));
        g.fillRect (wa);
        float sx = normToX (start), ex = normToX (end);
        g.setColour (juce::Colour (0xff303030));
        g.fillRect (juce::Rectangle<float> (sx, (float) wa.getY(), ex - sx, (float) wa.getHeight()).getIntersection (wa.toFloat()));
        // selection
        if (selStart >= 0 && selEnd > selStart)
        {
            float a = normToX ((double) selStart / length()), b = normToX ((double) selEnd / length());
            g.setColour (juce::Colour (0xff4a4a4a));
            g.fillRect (juce::Rectangle<float> (a, (float) wa.getY(), b - a, (float) wa.getHeight()).getIntersection (wa.toFloat()));
        }
        // loop
        if (loopOn)
        {
            float a = normToX (loopStart), b = normToX (loopEnd);
            g.setColour (col::orange.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (a, (float) wa.getY(), b - a, (float) wa.getHeight()).getIntersection (wa.toFloat()));
            g.setColour (col::orange);
            g.fillRect (a, (float) wa.getY(), juce::jmax (1.0f, b - a), 2.0f);
        }
        // channels
        auto& buf = sample->buffer;
        int chs = juce::jmin (2, buf.getNumChannels());
        int total = length();
        for (int c = 0; c < 2; ++c)
        {
            auto lane = juce::Rectangle<float> ((float) wa.getX(), wa.getY() + c * wa.getHeight() / 2.0f, (float) wa.getWidth(), wa.getHeight() / 2.0f).reduced (0, 3);
            g.setColour (juce::Colour (0xff555555));
            g.fillRect (lane.getX(), lane.getCentreY(), lane.getWidth(), 1.0f);
            int ch = juce::jmin (c, chs - 1);
            const float* d = buf.getReadPointer (ch);
            double sppx = (viewEnd - viewStart) * total / wa.getWidth();
            juce::Path p;
            bool started = false;
            for (int x = 0; x < wa.getWidth(); ++x)
            {
                double n0 = viewStart * total + x * sppx;
                int a = juce::jlimit (0, total - 1, (int) n0), b = juce::jlimit (a + 1, total, (int) (n0 + sppx) + 1);
                float mn = d[a], mx = d[a];
                for (int i = a; i < b; i += juce::jmax (1, (b - a) / 64)) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
                float hh = lane.getHeight() * 0.5f * ampZoom;
                float y0 = juce::jlimit (lane.getY(), lane.getBottom(), lane.getCentreY() - mx * hh), y1 = juce::jlimit (lane.getY(), lane.getBottom(), lane.getCentreY() - mn * hh);
                float fx = (float) (wa.getX() + x);
                if (sppx <= 2.0)
                {
                    float y = juce::jlimit (lane.getY(), lane.getBottom(), lane.getCentreY() - d[a] * hh);
                    if (! started) { p.startNewSubPath (fx, y); started = true; } else p.lineTo (fx, y);
                }
                else
                {
                    if (! started) { p.startNewSubPath (fx, y0); started = true; }
                    p.lineTo (fx, y0); p.lineTo (fx, y1);
                }
            }
            g.setColour (col::orange);
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }
        // slices
        if (showSlices)
        {
            g.setFont (font (10.5f));
            for (size_t i = 0; i < slices.size(); ++i)
            {
                float x = normToX (slices[i]);
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.fillRect (x, (float) wa.getY(), 1.0f, (float) wa.getHeight());
                g.fillRect (x, (float) wa.getY(), 16.0f, 14.0f);
                g.setColour (juce::Colours::black);
                g.drawText (juce::String ((int) i + 1), (int) x, wa.getY(), 16, 14, juce::Justification::centred);
            }
        }
        // play head
        if (playPos >= 0) { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (normToX (playPos), (float) wa.getY(), 1.0f, (float) wa.getHeight()); }
        // S / E markers
        auto badge = [&] (float x, const juce::String& t, bool rightAlign)
        {
            auto r = juce::Rectangle<float> (rightAlign ? x - 15.0f : x, (float) wa.getBottom() - 15.0f, 14.0f, 14.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (r);
            g.setColour (juce::Colours::black);
            g.setFont (font (10.0f, true));
            g.drawText (t, r, juce::Justification::centred);
        };
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillRect (sx, (float) wa.getY(), 1.0f, (float) wa.getHeight());
        g.fillRect (ex - 1, (float) wa.getY(), 1.0f, (float) wa.getHeight());
        badge (sx, "S", false);
        badge (ex, "E", true);
        // scrollbar
        auto sb = juce::Rectangle<float> (4.0f, (float) getHeight() - scrollH + 3, (float) getWidth() - 8, 4.0f);
        g.setColour (col::button);
        g.fillRoundedRectangle (sb, 2.0f);
        g.setColour (col::textDark);
        g.fillRoundedRectangle (sb.withX (sb.getX() + (float) viewStart * sb.getWidth()).withWidth (juce::jmax (12.0f, (float) (viewEnd - viewStart) * sb.getWidth())), 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        auto wa = waveArea();
        drag = 0;
        if (e.y >= getHeight() - scrollH) { drag = 4; dragView = viewStart; return; }
        float sx = normToX (start), ex = normToX (end);
        if (showSlices && e.mods.isPopupMenu())
        {
            double f = xToNorm ((float) e.x);
            int best = -1; double bd = 0.01 * (viewEnd - viewStart);
            for (size_t i = 0; i < slices.size(); ++i) if (std::abs (slices[i] - f) < bd) { bd = std::abs (slices[i] - f); best = (int) i; }
            if (best > 0) { slices.erase (slices.begin() + best); repaint(); }
            return;
        }
        if (showSlices && e.mods.isShiftDown()) { slices.push_back ((float) xToNorm ((float) e.x)); std::sort (slices.begin(), slices.end()); repaint(); return; }
        if (std::abs (e.x - sx) < 9 && (e.y > wa.getBottom() - 22 || e.mods.isAltDown())) { drag = 1; if (onDragStart) onDragStart(); return; }
        if (std::abs (e.x - ex) < 9 && (e.y > wa.getBottom() - 22 || e.mods.isAltDown())) { drag = 2; if (onDragStart) onDragStart(); return; }
        drag = 3;
        anchor = juce::jlimit (0, length(), (int) (xToNorm ((float) e.x) * length()));
        selStart = selEnd = -1;
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        double f = juce::jlimit (0.0, 1.0, xToNorm ((float) e.x));
        if (drag == 1) { start = (float) juce::jmin (f, (double) end - 0.0005); if (onRegionChanged) onRegionChanged (start, end); }
        else if (drag == 2) { end = (float) juce::jmax (f, (double) start + 0.0005); if (onRegionChanged) onRegionChanged (start, end); }
        else if (drag == 3)
        {
            int p = (int) (f * length());
            selStart = juce::jmin (anchor, p); selEnd = juce::jmax (anchor, p);
            if (selEnd - selStart < 2) selStart = selEnd = -1;
            if (onSelectionChanged) onSelectionChanged();
        }
        else if (drag == 4)
        {
            double w = viewEnd - viewStart;
            viewStart = juce::jlimit (0.0, 1.0 - w, dragView + e.getDistanceFromDragStartX() / (double) getWidth());
            viewEnd = viewStart + w;
        }
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { selStart = selEnd = -1; viewStart = 0; viewEnd = 1; if (onSelectionChanged) onSelectionChanged(); repaint(); }
    // Ctrl+wheel = zoom horizontally, Alt+wheel = zoom vertically (amplitude), wheel = scroll
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (e.mods.isAltDown() || (e.mods.isCommandDown() && e.mods.isShiftDown())) zoomV (d > 0 ? 1 : -1);
        else if (e.mods.isCommandDown()) zoomH (d > 0 ? 0.8 : 1.25, xToNorm ((float) e.x));
        else
        {
            double width = viewEnd - viewStart;
            viewStart = juce::jlimit (0.0, 1.0 - width, viewStart - d * width * 0.25);
            viewEnd = viewStart + width;
            repaint();
        }
    }
    void zoomH (double widthFactor, double at = -1)
    {
        double width = viewEnd - viewStart;
        if (at < 0) at = (viewStart + viewEnd) * 0.5;
        double nw = juce::jlimit (0.0005, 1.0, width * widthFactor);
        double frac = (at - viewStart) / width;
        viewStart = juce::jlimit (0.0, 1.0 - nw, at - frac * nw);
        viewEnd = viewStart + nw;
        repaint();
    }
    void zoomV (int dir) { ampZoom = juce::jlimit (0.25f, 32.0f, ampZoom * (dir > 0 ? 1.25f : 0.8f)); repaint(); }
    void zoomReset() { viewStart = 0; viewEnd = 1; ampZoom = 1; repaint(); }
private:
    int drag = 0, anchor = 0; double dragView = 0;
};

// ---------------------------------------------------------------------------------------------
// Caption + number that can be dragged or typed (PLAY RANGE START etc.)
class ValueField : public juce::Component
{
public:
    ValueField (const juce::String& c) : caption (c) {}
    juce::String caption, text;
    std::function<void (double delta, bool fine)> onDrag;
    std::function<void (juce::String)> onType;
    std::function<void()> onClick, onDragStart;
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::textDim);
        g.setFont (font (10.0f, true));
        g.drawText (caption, getLocalBounds().removeFromTop (14), juce::Justification::centredRight);
        g.setColour (isMouseOver() ? juce::Colours::white : col::text);
        g.setFont (font (12.5f, true));
        g.drawText (text, getLocalBounds().withTrimmedTop (14), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent&) override { lastY = 0; moved = false; if (onDrag && onDragStart) onDragStart(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! onDrag) return;
        int dy = e.getDistanceFromDragStartY();
        if (std::abs (dy - lastY) >= 2) { onDrag ((lastY - dy) / 2.0, e.mods.isShiftDown()); lastY = dy; moved = true; }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! moved && e.getNumberOfClicks() == 1 && onClick) onClick(); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! onType) return;
        actions::askText (caption, text, [this] (juce::String t) { if (onType) onType (t); });
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    int lastY = 0; bool moved = false;
};

// Tool button with drawn glyph + label (Truncate, Norm., Reverse ...)
class ToolButton : public juce::Button
{
public:
    ToolButton (const juce::String& n, int glyph) : Button (n), id (glyph) {}
    int id;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto c = over ? juce::Colours::white : col::text;
        if (down) c = c.darker (0.2f);
        if (! isEnabled()) c = c.withAlpha (0.4f);
        auto r = getLocalBounds().toFloat();
        auto ir = r.removeFromTop (r.getHeight() - 18).withSizeKeepingCentre (24, 24);
        g.setColour (c);
        drawGlyph (g, ir, c);
        g.setFont (font (12.0f));
        g.drawText (getButtonText(), r, juce::Justification::centredTop);
    }
private:
    void drawGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        juce::Path p;
        auto L = r.getX(), T = r.getY(), W = r.getWidth(), H = r.getHeight(), cx = r.getCentreX(), cy = r.getCentreY();
        juce::PathStrokeType st (1.5f);
        switch (id)
        {
            case 0: // truncate (crop)
                p.startNewSubPath (L + 6, T); p.lineTo (L + 6, T + H - 6); p.lineTo (L + W, T + H - 6);
                p.startNewSubPath (L, T + 6); p.lineTo (L + W - 6, T + 6); p.lineTo (L + W - 6, T + H);
                g.strokePath (p, st); break;
            case 1: // normalize
                g.drawRect (r.reduced (1), 1.5f);
                for (int i = 0; i < 5; ++i) { float h = H * (0.3f + 0.12f * (i % 3)); g.fillRect (L + 5 + i * 3.2f, cy - h / 2, 1.5f, h); }
                g.fillRect (L, T + 3, W, 1.5f); g.fillRect (L, T + H - 4.5f, W, 1.5f); break;
            case 2: // reverse
                p.startNewSubPath (L + 2, cy - 4); p.lineTo (L + W - 2, cy - 4); p.startNewSubPath (L + W - 6, cy - 8); p.lineTo (L + W - 2, cy - 4); p.lineTo (L + W - 6, cy);
                p.startNewSubPath (L + W - 2, cy + 4); p.lineTo (L + 2, cy + 4); p.startNewSubPath (L + 6, cy); p.lineTo (L + 2, cy + 4); p.lineTo (L + 6, cy + 8);
                g.strokePath (p, st); break;
            case 3: p.addTriangle (L, cy + 6, L + W, cy + 6, L + W, cy - 4); g.fillPath (p); break;   // fade in
            case 4: p.addTriangle (L, cy - 4, L, cy + 6, L + W, cy + 6); g.fillPath (p); break;       // fade out
            case 5: // correct (DC)
                g.fillRect (L, cy, W, 1.5f);
                for (int i = 0; i < 6; ++i) { float h = (i % 2 ? 0.5f : 0.9f) * H * 0.5f; g.fillRect (L + 3 + i * 3.5f, cy - h, 1.5f, h * 2); }
                break;
            case 6: // silence
                g.drawEllipse (r.reduced (2), 1.5f);
                p.startNewSubPath (cx - 4, cy - 4); p.lineTo (cx + 4, cy + 4); p.startNewSubPath (cx + 4, cy - 4); p.lineTo (cx - 4, cy + 4);
                g.strokePath (p, st); break;
            case 7: // cut
                g.drawEllipse (L + 2, T + H - 9, 7, 7, 1.5f); g.drawEllipse (L + W - 9, T + H - 9, 7, 7, 1.5f);
                p.startNewSubPath (L + 7, T + H - 8); p.lineTo (L + W - 4, T + 2); p.startNewSubPath (L + W - 7, T + H - 8); p.lineTo (L + 4, T + 2);
                g.strokePath (p, st); break;
            case 8: // copy
                g.drawRect (L + 2, T + 6, W - 10, H - 8, 1.5f); g.drawRect (L + 8, T + 1, W - 10, H - 8, 1.5f); break;
            case 9: // paste
                g.drawRect (L + 3, T + 3, W - 6, H - 4, 1.5f); g.fillRect (cx - 4, T, 8.0f, 5.0f);
                p.startNewSubPath (cx, cy - 3); p.lineTo (cx, cy + 6); p.startNewSubPath (cx - 3, cy + 3); p.lineTo (cx, cy + 6); p.lineTo (cx + 3, cy + 3);
                g.strokePath (p, st); break;
            case 10: // duplicate
                g.drawRect (L + 2, T + 6, W - 10, H - 8, 1.5f); g.drawRect (L + 8, T + 1, W - 10, H - 8, 1.5f);
                g.fillRect (L + 14, T + 7, 1.5f, 7.0f); g.fillRect (L + 11, T + 10, 7.0f, 1.5f); break;
            case 11: // stretch
                g.drawRect (L + 5, T + 5, W - 10, H - 10, 1.5f);
                p.startNewSubPath (L, cy); p.lineTo (L + W, cy); p.startNewSubPath (L + 3, cy - 3); p.lineTo (L, cy); p.lineTo (L + 3, cy + 3);
                p.startNewSubPath (L + W - 3, cy - 3); p.lineTo (L + W, cy); p.lineTo (L + W - 3, cy + 3);
                g.strokePath (p, st); break;
            default: // stems
                for (int row = 0; row < 3; ++row)
                    for (int i = 0; i < 6; ++i) { float h = 2.0f + (float) ((i * 7 + row * 3) % 5); g.fillRect (L + 2 + i * 3.6f, T + 4 + row * 7.0f - h / 2 + 2, 1.5f, h); }
                break;
        }
        juce::ignoreUnused (c);
    }
};

// ---------------------------------------------------------------------------------------------
class SamplingView : public juce::Component, private juce::Timer
{
public:
    SamplingView (Engine& e, Ui& u) : engine (e), ui (u)
    {
        const char* tabs[] = { "Record", "Edit", "Slice", "Zone" };
        for (int i = 0; i < 4; ++i)
        {
            auto* b = tabBtns.add (new FlatButton (tabs[i], 12.5f));
            b->onClick = [this, i] { ui.samplingTab = i; update(); };
            addAndMakeVisible (b);
        }
        startBtn.style = cancelBtn.style = FlatButton::boxed;
        addAndMakeVisible (startBtn); addAndMakeVisible (cancelBtn);
        startBtn.onClick = [this]
        {
            if (engine.recState == Engine::RecState::idle) engine.startSampling();
            else if (engine.recState == Engine::RecState::recording) { engine.recState = Engine::RecState::done; }
            else engine.cancelSampling();
        };
        cancelBtn.onClick = [this] { engine.cancelSampling(); };

        auto mk = [this] (const juce::String& cap, juce::StringArray choices, std::function<int()> get, std::function<void (int)> set)
        {
            auto* p = recPickers.add (new ValuePicker (cap));
            p->choices = choices; p->orange = false; p->compact = true; p->index = get();
            p->onChange = set;
            addAndMakeVisible (p);
            return p;
        };
        mk ("MODE", { "Detect", "Sync", "Loop", "Auto" }, [this] { return engine.rec.mode; }, [this] (int v) { engine.rec.mode = v; update(); });
        mk ("LENGTH", Engine::recLengths(), [this] { return engine.rec.length; }, [this] (int v) { engine.rec.length = v; });
        mk ("SOURCE", { "Ext. Ster.", "Ext. Mono", "Internal" }, [this] { return engine.rec.source; }, [this] (int v) { engine.rec.source = v; rebuildInputChoices(); });
        mk ("INPUT", {}, [this] { return engine.rec.input; }, [this] (int v) { engine.rec.input = v; });
        mk ("MONITOR", { "Off", "On" }, [this] { return engine.rec.monitor ? 1 : 0; }, [this] (int v) { engine.rec.monitor = v == 1; });
        rebuildInputChoices();
        addAndMakeVisible (threshold);
        threshold.label = "Threshold";
        threshold.value = 0.3f;
        threshold.onChange = [this] (float v) { engine.rec.threshold = juce::Decibels::decibelsToGain (-60.0f + v * 60.0f); };
        threshold.textFn = [] (float v) { return juce::String (-60.0f + v * 60.0f, 1) + " dB"; };

        addChildComponent (wave);
        wave.onDragStart = [this] { engine.snapshot ("Sample Region"); };
        wave.onRegionChanged = [this] (float s, float en) { if (auto* sp = sampler()) sp->setRegion (s, en); updateFields(); };
        wave.onSelectionChanged = [this] { updateFields(); };

        const char* tools[] = { "Truncate", "Norm.", "Reverse", "Fade In", "Fade Out", "Correct", "Silence", "Cut", "Copy", "Paste", "Dupl.", "Stretch", "Stems" };
        const char* tips[] = { "Keep only the play range (S to E)", "Normalize the selection (or the whole sample)", "Reverse the selection (or the whole sample)",
                               "Fade in over the selection", "Fade out over the selection", "Remove DC offset", "Silence the selection", "Cut the selection",
                               "Copy the selection", "Paste at the selection start", "Duplicate the selection", "Time-stretch the selection without changing pitch",
                               "Split into Low / Mid / High stems on the next empty pads" };
        for (int i = 0; i < 13; ++i)
        {
            auto* b = toolBtns.add (new ToolButton (tools[i], i));
            b->setTooltip (tips[i]);
            juce::String name (tools[i]);
            b->onClick = [this, name] { editOp (name); };
            addChildComponent (b);
        }

        // value fields
        auto field = [this] (const juce::String& cap) { auto* f = fields.add (new ValueField (cap)); addChildComponent (f); return f; };
        fPlayS = field ("START"); fPlayE = field ("END"); fSelS = field ("START"); fSelE = field ("END");
        fLoopOn = field ("ACTIVE"); fLoopS = field ("START"); fLoopE = field ("END"); fXfade = field ("X-FADE");
        auto samplesSetter = [this] (int which)
        {
            return [this, which] (double d, bool fine)
            {
                auto* sp = sampler(); if (sp == nullptr || wave.length() <= 0) return;
                double len = wave.length(), step = fine ? 1.0 : juce::jmax (1.0, len / 1000.0);
                auto pct = [len] (double smp) { return (float) juce::jlimit (0.0, 100.0, smp / len * 100.0); };
                double ps = sp->value (SamplerProcessor::start) / 100.0 * len, pe = sp->value (SamplerProcessor::end) / 100.0 * len;
                double ls = sp->value (SamplerProcessor::loopStart) / 100.0 * len, le = sp->value (SamplerProcessor::loopEnd) / 100.0 * len;
                if (which == 0) sp->setValue (SamplerProcessor::start, pct (juce::jlimit (0.0, pe - 1, ps + d * step)));
                if (which == 1) sp->setValue (SamplerProcessor::end, pct (juce::jlimit (ps + 1, len, pe + d * step)));
                if (which == 2) sp->setValue (SamplerProcessor::loopStart, pct (juce::jlimit (0.0, le - 1, ls + d * step)));
                if (which == 3) sp->setValue (SamplerProcessor::loopEnd, pct (juce::jlimit (ls + 1, len, le + d * step)));
                if (which == 4) sp->setValue (SamplerProcessor::xfade, (float) juce::jlimit (0.0, 100.0, sp->value (SamplerProcessor::xfade) + d * (fine ? 0.1 : 1.0)));
                updateFields();
            };
        };
        auto typer = [this] (int which)
        {
            return [this, which] (juce::String t)
            {
                auto* sp = sampler(); if (sp == nullptr || wave.length() <= 0) return;
                engine.snapshot ("Sample Range");
                double len = wave.length(), val = t.getDoubleValue();
                auto pct = [len] (double smp) { return (float) juce::jlimit (0.0, 100.0, smp / len * 100.0); };
                if (which == 0) sp->setValue (SamplerProcessor::start, pct (val));
                if (which == 1) sp->setValue (SamplerProcessor::end, pct (val));
                if (which == 2) sp->setValue (SamplerProcessor::loopStart, pct (val));
                if (which == 3) sp->setValue (SamplerProcessor::loopEnd, pct (val));
                if (which == 4) { double loopLen = (sp->value (SamplerProcessor::loopEnd) - sp->value (SamplerProcessor::loopStart)) / 100.0 * len; sp->setValue (SamplerProcessor::xfade, (float) juce::jlimit (0.0, 100.0, val / juce::jmax (1.0, loopLen * 0.5) * 100.0)); }
                if (which == 5) wave.selStart = juce::jlimit (0, wave.length(), (int) val);
                if (which == 6) wave.selEnd = juce::jlimit (0, wave.length(), (int) val);
                updateFields(); wave.repaint();
            };
        };
        ValueField* adj[] = { fPlayS, fPlayE, fLoopS, fLoopE, fXfade };
        for (int i = 0; i < 5; ++i) { adj[i]->onDrag = samplesSetter (i); adj[i]->onType = typer (i); adj[i]->onDragStart = [this] { engine.snapshot ("Sample Range"); }; }
        fSelS->onType = typer (5); fSelE->onType = typer (6);
        fLoopOn->onClick = [this] { if (auto* sp = sampler()) { engine.snapshot ("Loop"); sp->setValue (SamplerProcessor::loopP, sp->value (SamplerProcessor::loopP) > 0.5f ? 0.0f : 1.0f); updateFields(); } };

        // slice controls
        auto* sm = recPickers.add (new ValuePicker ("SLICE MODE"));
        sm->choices = { "Grid", "Detect" }; sm->orange = false; sm->compact = true;
        sm->onChange = [this] (int) { computeSlices(); update(); };
        addChildComponent (sm);
        sliceMode = sm;
        auto* sc = recPickers.add (new ValuePicker ("SLICES"));
        sc->choices = { "4", "8", "16", "32" }; sc->index = 2; sc->orange = false; sc->compact = true;
        sc->onChange = [this] (int) { computeSlices(); };
        addChildComponent (sc);
        sliceCount = sc;
        addChildComponent (sensitivity);
        sensitivity.label = "Sensitivity"; sensitivity.value = 0.5f;
        sensitivity.onChange = [this] (float) { computeSlices(); };
        for (auto* t : { "Apply to Pads", "Apply to Pattern" })
        {
            auto* b = sliceBtns.add (new FlatButton (t, 12.0f));
            b->style = FlatButton::boxed;
            juce::String name (t);
            b->onClick = [this, name] { if (name == "Apply to Pads") slicesToPads(); else slicesToPattern(); };
            addChildComponent (b);
        }

        // zone fields
        fRoot = field ("ROOT"); fKeyLo = field ("LOW KEY"); fKeyHi = field ("HIGH KEY"); fTune = field ("TUNE"); fGain = field ("GAIN"); fPan = field ("PAN");
        auto zoneDrag = [this] (int what)
        {
            return [this, what] (double d, bool fine)
            {
                auto* G = engine.project().group (engine.focusedGroup);
                auto* sp = sampler();
                if (G == nullptr) return;
                auto& snd = *G->sounds[(size_t) G->focusedSound];
                int di = d > 0 ? 1 : -1;
                if (what == 0) { snd.rootKey = juce::jlimit (0, 127, snd.rootKey + di); if (sp) sp->rootKey = snd.rootKey; }
                else if (sp == nullptr) return;
                else if (what == 1) sp->setValue (SamplerProcessor::keyLo, juce::jlimit (0.0f, sp->value (SamplerProcessor::keyHi), sp->value (SamplerProcessor::keyLo) + di));
                else if (what == 2) sp->setValue (SamplerProcessor::keyHi, juce::jlimit (sp->value (SamplerProcessor::keyLo), 127.0f, sp->value (SamplerProcessor::keyHi) + di));
                else if (what == 3) sp->setValue (SamplerProcessor::tune, juce::jlimit (-36.0f, 36.0f, sp->value (SamplerProcessor::tune) + (fine ? 0.01f : 1.0f) * (float) d));
                else if (what == 4) sp->setValue (SamplerProcessor::zoneGain, juce::jlimit (-36.0f, 12.0f, sp->value (SamplerProcessor::zoneGain) + (fine ? 0.1f : 0.5f) * (float) d));
                else if (what == 5) sp->setValue (SamplerProcessor::zonePan, juce::jlimit (-100.0f, 100.0f, sp->value (SamplerProcessor::zonePan) + (fine ? 1.0f : 2.0f) * (float) d));
                updateFields(); repaint();
            };
        };
        ValueField* zf[] = { fRoot, fKeyLo, fKeyHi, fTune, fGain, fPan };
        for (int i = 0; i < 6; ++i) { zf[i]->onDrag = zoneDrag (i); zf[i]->onDragStart = [this] { engine.snapshot ("Zone"); }; }

        update();
        startTimerHz (20);
    }

    void zoomH (bool in) { wave.zoomH (in ? 0.8 : 1.25); }
    void zoomV (int dir) { wave.zoomV (dir); }
    void zoomReset() { wave.zoomReset(); }

    void update()
    {
        for (int i = 0; i < 4; ++i) { tabBtns[i]->setToggleState (ui.samplingTab == i, juce::dontSendNotification); tabBtns[i]->onColour = juce::Colours::white; }
        bool rec = ui.samplingTab == 0, edit = ui.samplingTab == 1, slice = ui.samplingTab == 2, zone = ui.samplingTab == 3;
        startBtn.setVisible (rec); cancelBtn.setVisible (rec);
        for (int i = 0; i < 5; ++i) recPickers[i]->setVisible (rec);
        threshold.setVisible (rec && engine.rec.mode == 0);
        wave.setVisible (edit || slice);
        wave.showSlices = slice;
        for (auto* b : toolBtns) b->setVisible (edit);
        for (auto* f : { fPlayS, fPlayE, fSelS, fSelE, fLoopOn, fLoopS, fLoopE, fXfade }) f->setVisible (edit);
        for (auto* f : { fRoot, fKeyLo, fKeyHi, fTune, fGain, fPan }) f->setVisible (zone);
        for (auto* b : sliceBtns) b->setVisible (slice);
        sliceMode->setVisible (slice); sliceCount->setVisible (slice); sensitivity.setVisible (slice && sliceMode->index == 1);
        if (slice) computeSlices();
        updateFields();
        resized();
        repaint();
    }

    static constexpr int tabsH = 22, headH = 20, toolsH = 62, valuesH = 52;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::editorBg);
        g.setColour (col::bg);
        g.fillRect (0, 0, getWidth(), tabsH);
        g.setColour (juce::Colours::black);
        g.fillRect (0, tabsH - 1, getWidth(), 1);
        if (ui.samplingTab == 1 || ui.samplingTab == 2 || ui.samplingTab == 3)
        {
            // header: play triangle + sample name, length on the right
            auto* sp = sampler();
            auto smp = sp ? sp->getSample() : nullptr;
            auto hr = juce::Rectangle<int> (0, tabsH, getWidth(), headH);
            g.setColour (col::bg);
            g.fillRect (hr);
            juce::Path tri; tri.addTriangle (12.0f, (float) hr.getY() + 6, 12.0f, (float) hr.getY() + 14, 18.0f, (float) hr.getCentreY());
            g.setColour (col::orange);
            g.fillPath (tri);
            g.setFont (font (12.5f));
            g.drawText (smp ? smp->file.getFileNameWithoutExtension() : juce::String ("No sample"), 26, hr.getY(), getWidth() / 2, headH, juce::Justification::centredLeft);
            if (smp)
            {
                g.setColour (col::textDim);
                g.drawText (juce::String (smp->buffer.getNumSamples() / smp->sampleRate, 2) + "s", getWidth() - 110, hr.getY(), 82, headH, juce::Justification::centredRight);
                icons::draw (g, icons::eye, { (float) getWidth() - 24, (float) hr.getY() + 3, 14.0f, 14.0f }, col::textDim);
            }
        }
        if (ui.samplingTab == 1)
        {
            auto tr = toolsRect();
            g.setColour (col::bg);
            g.fillRect (tr);
            g.setColour (juce::Colour (0xff1a1a1a));
            g.fillRect (valuesRect());
            g.setColour (juce::Colours::black);
            g.fillRect (0, tr.getY(), getWidth(), 1);
            g.fillRect (0, valuesRect().getY(), getWidth(), 1);
            g.setColour (col::text);
            g.setFont (font (12.0f));
            auto vr = valuesRect();
            g.drawText ("PLAY RANGE", 6, vr.getY() + 3, 120, 16, juce::Justification::centredLeft);
            g.drawText ("SELECTION RANGE", 139, vr.getY() + 3, 140, 16, juce::Justification::centredLeft);
            g.drawText ("LOOP", 272, vr.getY() + 3, 80, 16, juce::Justification::centredLeft);
        }
        if (ui.samplingTab == 2)
        {
            g.setColour (col::bg);
            g.fillRect (toolsRect().getUnion (valuesRect()));
            g.setColour (col::textDark);
            g.setFont (font (11.0f));
            g.drawText ("Shift-click the waveform to add a slice, right-click a slice to remove it", 520, toolsRect().getY() + 10, 500, 16, juce::Justification::centredLeft);
        }
        if (ui.samplingTab == 3) paintZone (g);
        if (ui.samplingTab != 0) return;
        // ---- Record page
        auto st = engine.recState.load();
        juce::String msg = st == Engine::RecState::idle ? "Press Start to record your input source"
                         : st == Engine::RecState::waiting ? (engine.rec.mode == 0 ? "Waiting for signal above threshold..." : engine.rec.mode == 3 ? "Waiting for transport to start..." : "Waiting for the next bar...")
                         : st == Engine::RecState::recording ? "Recording..." : "Processing...";
        g.setColour (st == Engine::RecState::recording ? col::orange : col::textDark);
        g.setFont (font (13.0f));
        g.drawText (msg, getLocalBounds().withTrimmedBottom (80), juce::Justification::centred);
        auto bottom = getHeight() - 62;
        g.setColour (col::bg);
        g.fillRect (0, bottom, getWidth(), 62);
        g.setColour (juce::Colours::black);
        g.fillRect (0, bottom, getWidth(), 1);
        g.setColour (col::textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("RECORDING", 6, bottom + 4, 100, 14, juce::Justification::centredLeft);
        g.drawText ("INPUT", 170, bottom + 4, 100, 14, juce::Justification::centredLeft);
        g.drawText ("MONITOR", 338, bottom + 4, 100, 14, juce::Justification::centredLeft);
        auto mr = juce::Rectangle<int> (24, bottom - 64, 100, 18);
        float l = engine.inLevelL, r = engine.inLevelR;
        auto bar = [&] (juce::Rectangle<int> b, float v)
        {
            g.setColour (col::button); g.fillRect (b);
            g.setColour (v > 0.95f ? juce::Colours::red : juce::Colours::white);
            g.fillRect (b.withWidth ((int) (b.getWidth() * levelToNorm (v))));
        };
        bar (mr.removeFromTop (6), l); mr.removeFromTop (2); bar (mr.removeFromTop (6), r);
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x88\x9e  50  40  30  20  10  0"), 20, bottom - 46, 130, 12, juce::Justification::centredLeft);
        if (engine.rec.mode == 0)
        {
            float tx = 24 + 100 * levelToNorm (engine.rec.threshold);
            g.setColour (col::orange);
            g.fillRect (tx, (float) bottom - 66, 1.0f, 22.0f);
        }
    }

    juce::Rectangle<int> valuesRect() const { return { 0, getHeight() - valuesH, getWidth(), valuesH }; }
    juce::Rectangle<int> toolsRect() const { return { 0, getHeight() - valuesH - toolsH, getWidth(), toolsH }; }

    void resized() override
    {
        int x = 6;
        for (int i = 0; i < 4; ++i)
        {
            int w = (int) font (12.5f).getStringWidthFloat (tabBtns[i]->getButtonText()) + 20;
            tabBtns[i]->setBounds (x, 1, w, tabsH - 2); x += w;
        }
        auto bottom = getHeight() - 62;
        startBtn.setBounds (150, bottom - 70, 62, 22);
        cancelBtn.setBounds (150, bottom - 44, 62, 22);
        threshold.setBounds (230, bottom - 78, 64, 66);
        const int xs[] = { 20, 90, 170, 250, 338 };
        for (int i = 0; i < 5; ++i) recPickers[i]->setBounds (xs[i], bottom + 20, 80, 38);

        int waveTop = tabsH + headH;
        int waveBottom = ui.samplingTab == 3 ? getHeight() - 70 : toolsRect().getY();
        wave.setBounds (0, waveTop, getWidth(), waveBottom - waveTop);
        auto tr = toolsRect();
        int bx = 4;
        for (auto* b : toolBtns) { b->setBounds (bx, tr.getY() + 6, 56, tr.getHeight() - 10); bx += 56; }
        auto vr = valuesRect();
        const int fx[] = { 12, 77, 143, 210, 277, 344, 410, 475 };
        ValueField* vf[] = { fPlayS, fPlayE, fSelS, fSelE, fLoopOn, fLoopS, fLoopE, fXfade };
        for (int i = 0; i < 8; ++i) vf[i]->setBounds (fx[i], vr.getY() + 18, 50, 32);
        sliceMode->setBounds (12, tr.getY() + 10, 80, 38);
        sliceCount->setBounds (100, tr.getY() + 10, 60, 38);
        sensitivity.setBounds (170, tr.getY() + 4, 60, 56);
        bx = 250;
        for (auto* b : sliceBtns) { b->setBounds (bx, tr.getY() + 18, 120, 24); bx += 126; }
        ValueField* zf[] = { fRoot, fKeyLo, fKeyHi, fTune, fGain, fPan };
        for (int i = 0; i < 6; ++i) zf[i]->setBounds (20 + i * 80, getHeight() - 52, 64, 36);
    }

private:
    Engine& engine;
    Ui& ui;
    juce::OwnedArray<FlatButton> tabBtns, sliceBtns;
    juce::OwnedArray<ToolButton> toolBtns;
    juce::OwnedArray<ValueField> fields;
    ValueField *fPlayS, *fPlayE, *fSelS, *fSelE, *fLoopOn, *fLoopS, *fLoopE, *fXfade;
    ValueField *fRoot, *fKeyLo, *fKeyHi, *fTune, *fGain, *fPan;
    FlatButton startBtn { "Start" }, cancelBtn { "Cancel" };
    juce::OwnedArray<ValuePicker> recPickers;
    ValuePicker* sliceMode = nullptr; ValuePicker* sliceCount = nullptr;
    Knob threshold, sensitivity;
    WaveformView wave;
    static juce::AudioBuffer<float>& audioClipboard() { static juce::AudioBuffer<float> b; return b; }

    SamplerProcessor* sampler()
    {
        auto* g = engine.project().group (engine.focusedGroup);
        return g ? engine.samplerOf (*g->sounds[(size_t) g->focusedSound]) : nullptr;
    }

    void rebuildInputChoices()
    {
        juce::StringArray a;
        if (engine.rec.source == 0) for (int i = 1; i <= 16; ++i) a.add ("In " + juce::String (i) + " L+R");
        else if (engine.rec.source == 1) for (int i = 1; i <= 16; ++i) { a.add ("In " + juce::String (i) + " L"); a.add ("In " + juce::String (i) + " R"); }
        else a.add ("Master");
        recPickers[3]->choices = a;
        recPickers[3]->setIndex (juce::jmin (engine.rec.input, a.size() - 1));
        engine.rec.input = recPickers[3]->index;
    }

    void updateFields()
    {
        auto* sp = sampler();
        int len = wave.length();
        auto smpStr = [] (double v) { return juce::String ((juce::int64) std::llround (v)); };
        if (sp == nullptr || len <= 0)
        {
            for (auto* f : fields) f->text = "-";
        }
        else
        {
            double ls = sp->value (SamplerProcessor::loopStart) / 100.0 * len, le = sp->value (SamplerProcessor::loopEnd) / 100.0 * len;
            fPlayS->text = smpStr (sp->value (SamplerProcessor::start) / 100.0 * len);
            fPlayE->text = smpStr (sp->value (SamplerProcessor::end) / 100.0 * len);
            fSelS->text = smpStr (wave.selStart >= 0 ? wave.selStart : 0);
            fSelE->text = smpStr (wave.selStart >= 0 ? wave.selEnd : len);
            fLoopOn->text = sp->value (SamplerProcessor::loopP) > 0.5f ? "On" : "Off";
            fLoopS->text = smpStr (ls);
            fLoopE->text = smpStr (le);
            fXfade->text = smpStr ((le - ls) * 0.5 * sp->value (SamplerProcessor::xfade) / 100.0);
            auto* G = engine.project().group (engine.focusedGroup);
            fRoot->text = G ? noteName (G->sounds[(size_t) G->focusedSound]->rootKey) : "-";
            fKeyLo->text = noteName ((int) sp->value (SamplerProcessor::keyLo));
            fKeyHi->text = noteName ((int) sp->value (SamplerProcessor::keyHi));
            fTune->text = juce::String (sp->value (SamplerProcessor::tune), 2) + " st";
            fGain->text = juce::String (sp->value (SamplerProcessor::zoneGain), 1) + " dB";
            auto pan = juce::roundToInt (sp->value (SamplerProcessor::zonePan));
            fPan->text = pan == 0 ? "C" : (pan < 0 ? "L" + juce::String (-pan) : "R" + juce::String (pan));
        }
        for (auto* f : fields) f->repaint();
    }

    void paintZone (juce::Graphics& g)
    {
        // keyboard map with the zone, like the original Zone page
        auto* sp = sampler();
        auto* G = engine.project().group (engine.focusedGroup);
        auto area = juce::Rectangle<int> (8, tabsH + headH + 8, getWidth() - 16, getHeight() - tabsH - headH - 90);
        g.setColour (col::editorBg.brighter (0.03f));
        g.fillRect (area);
        auto kb = area.removeFromBottom (36);
        float kw = kb.getWidth() / 128.0f;
        int whites = 0; for (int n = 0; n < 128; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
        float ww = kb.getWidth() / (float) whites;
        g.setColour (juce::Colour (0xffd8d8d8));
        g.fillRect (kb);
        g.setColour (juce::Colour (0xff6a6a6a));
        for (int i = 1; i < whites; ++i) g.fillRect (kb.getX() + i * ww, (float) kb.getY(), 1.0f, (float) kb.getHeight());
        int wi = 0;
        for (int n = 0; n < 128; ++n)
        {
            if (juce::MidiMessage::isMidiNoteBlack (n)) { g.setColour (juce::Colour (0xff1a1a1a)); g.fillRect (kb.getX() + wi * ww - ww * 0.32f, (float) kb.getY(), ww * 0.64f, kb.getHeight() * 0.6f); }
            else ++wi;
            if (n % 12 == 0) { g.setColour (col::textDim); g.setFont (font (10.0f)); g.drawText (noteName (n), (int) (kb.getX() + n * kw), kb.getY() - 14, 30, 12, juce::Justification::centredLeft); }
        }
        if (sp == nullptr) return;
        int lo = (int) sp->value (SamplerProcessor::keyLo), hi = (int) sp->value (SamplerProcessor::keyHi);
        auto zr = juce::Rectangle<float> (area.getX() + lo * kw, (float) area.getY() + 10, (hi - lo + 1) * kw, (float) area.getHeight() - 30);
        g.setColour (col::orange.withAlpha (0.35f));
        g.fillRect (zr);
        g.setColour (col::orange);
        g.drawRect (zr, 1.5f);
        g.setFont (font (12.5f));
        g.setColour (juce::Colours::white);
        if (auto smp = sp->getSample()) g.drawText (smp->file.getFileNameWithoutExtension(), zr.reduced (6).toNearestInt(), juce::Justification::topLeft, true);
        if (G)
        {
            int root = G->sounds[(size_t) G->focusedSound]->rootKey;
            g.setColour (juce::Colours::white);
            g.fillRect (kb.getX() + root * kw, (float) kb.getY() - 4, kw, 4.0f);
        }
    }

    void timerCallback() override
    {
        auto st = engine.recState.load();
        startBtn.setButtonText (st == Engine::RecState::idle ? "Start" : st == Engine::RecState::recording ? "Stop" : "Cancel");
        if (st == Engine::RecState::done)
        {
            engine.finishSampling();
            ui.samplingTab = 1;
            update();
            ui.changed();
        }
        if (ui.samplingTab == 0) repaint();
        auto* sp = sampler();
        auto smp = sp ? sp->getSample() : nullptr;
        if (smp != wave.sample) { wave.setSample (smp); updateFields(); repaint(); }
        if (sp)
        {
            float s = sp->value (SamplerProcessor::start) / 100.0f, e = sp->value (SamplerProcessor::end) / 100.0f;
            bool lo = sp->value (SamplerProcessor::loopP) > 0.5f;
            float ls = sp->value (SamplerProcessor::loopStart) / 100.0f, le = sp->value (SamplerProcessor::loopEnd) / 100.0f;
            if (! wave.isMouseButtonDown() && (std::abs (s - wave.start) > 1e-5f || std::abs (e - wave.end) > 1e-5f || lo != wave.loopOn || ls != wave.loopStart || le != wave.loopEnd))
            { wave.start = s; wave.end = e; wave.loopOn = lo; wave.loopStart = ls; wave.loopEnd = le; wave.repaint(); updateFields(); }
            float pp = sp->playPos;
            if (pp != wave.playPos) { wave.playPos = pp; wave.repaint(); }
        }
        threshold.setVisible (ui.samplingTab == 0 && engine.rec.mode == 0);
    }

    // ------------------------------------------------------------------ destructive edits
    void writeAndLoad (const juce::AudioBuffer<float>& b, double sr, const juce::String& srcName, const juce::String& op, bool keepRange, float s = 0.0f, float e = 1.0f)
    {
        auto* g = engine.project().group (engine.focusedGroup);
        if (g == nullptr || b.getNumSamples() <= 0) return;
        auto folder = Settings::userFolder().getChildFile ("Recordings").getChildFile ("Edits");
        folder.createDirectory();
        auto f = folder.getNonexistentChildFile (srcName + " " + op.removeCharacters ("."), ".wav");
        juce::WavAudioFormat wav;
        if (auto os = f.createOutputStream())
        {
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) b.getNumChannels(), 24, {}, 0));
            if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        }
        engine.snapshot ("Sample " + op);
        auto& snd = *g->sounds[(size_t) g->focusedSound];
        auto name = snd.name;
        auto* sp = engine.samplerOf (snd);
        float ls = sp ? sp->value (SamplerProcessor::loopStart) : 0.0f, le = sp ? sp->value (SamplerProcessor::loopEnd) : 100.0f;
        engine.loadSampleIntoSound (snd, f);
        snd.name = name;
        if (auto* np = engine.samplerOf (snd))
        {
            np->setRegion (keepRange ? s : 0.0f, keepRange ? e : 1.0f);
            if (keepRange) { np->setValue (SamplerProcessor::loopStart, ls); np->setValue (SamplerProcessor::loopEnd, le); }
            else { np->setValue (SamplerProcessor::loopStart, 0.0f); np->setValue (SamplerProcessor::loopEnd, 100.0f); }
        }
        ui.changed();
    }

    void editOp (const juce::String& op)
    {
        auto* sp = sampler();
        if (sp == nullptr || sp->getSample() == nullptr) return;
        auto src = sp->getSample();
        const int len = src->buffer.getNumSamples(), chs = src->buffer.getNumChannels();
        bool hasSel = wave.selStart >= 0 && wave.selEnd > wave.selStart;
        int a = hasSel ? wave.selStart : 0, z = hasSel ? wave.selEnd : len;
        a = juce::jlimit (0, len, a); z = juce::jlimit (a, len, z);
        auto baseName = src->file.getFileNameWithoutExtension();
        juce::AudioBuffer<float> b;
        b.makeCopyOf (src->buffer);
        float s = wave.start, e = wave.end;

        if (op == "Truncate")
        {
            int ps = (int) (s * len), pe = juce::jmax (ps + 1, (int) (e * len));
            juce::AudioBuffer<float> t (chs, pe - ps);
            for (int c = 0; c < chs; ++c) t.copyFrom (c, 0, src->buffer, c, ps, pe - ps);
            writeAndLoad (t, src->sampleRate, baseName, op, false);
            return;
        }
        if (op == "Norm.") { float m = 0; for (int c = 0; c < chs; ++c) m = juce::jmax (m, b.getMagnitude (c, a, z - a)); if (m > 1e-5f) for (int c = 0; c < chs; ++c) b.applyGain (c, a, z - a, 0.99f / m); }
        else if (op == "Reverse") for (int c = 0; c < chs; ++c) b.reverse (c, a, z - a);
        else if (op == "Fade In") for (int c = 0; c < chs; ++c) b.applyGainRamp (c, a, z - a, 0.0f, 1.0f);
        else if (op == "Fade Out") for (int c = 0; c < chs; ++c) b.applyGainRamp (c, a, z - a, 1.0f, 0.0f);
        else if (op == "Correct")
            for (int c = 0; c < chs; ++c)
            {
                double sum = 0; auto* d = b.getWritePointer (c);
                for (int i = a; i < z; ++i) sum += d[i];
                float dc = (float) (sum / juce::jmax (1, z - a));
                for (int i = a; i < z; ++i) d[i] -= dc;
            }
        else if (op == "Silence") { if (! hasSel) return; b.clear (a, z - a); }
        else if (op == "Copy") { if (! hasSel) return; auto& cb = audioClipboard(); cb.setSize (chs, z - a); for (int c = 0; c < chs; ++c) cb.copyFrom (c, 0, b, c, a, z - a); return; }
        else if (op == "Cut" || op == "Dupl." || op == "Paste")
        {
            juce::AudioBuffer<float> out;
            if (op == "Cut")
            {
                if (! hasSel) return;
                auto& cb = audioClipboard(); cb.setSize (chs, z - a); for (int c = 0; c < chs; ++c) cb.copyFrom (c, 0, b, c, a, z - a);
                out.setSize (chs, juce::jmax (1, len - (z - a)));
                for (int c = 0; c < chs; ++c) { out.copyFrom (c, 0, b, c, 0, a); if (len - z > 0) out.copyFrom (c, a, b, c, z, len - z); }
            }
            else
            {
                juce::AudioBuffer<float> ins;
                int at = a;
                if (op == "Dupl.") { if (! hasSel) return; ins.setSize (chs, z - a); for (int c = 0; c < chs; ++c) ins.copyFrom (c, 0, b, c, a, z - a); at = z; }
                else { if (audioClipboard().getNumSamples() == 0) return; ins.makeCopyOf (audioClipboard()); if (hasSel) { /* replace selection */ juce::AudioBuffer<float> t (chs, len - (z - a)); for (int c = 0; c < chs; ++c) { t.copyFrom (c, 0, b, c, 0, a); if (len - z > 0) t.copyFrom (c, a, b, c, z, len - z); } b.makeCopyOf (t); } }
                int cur = b.getNumSamples();
                out.setSize (chs, cur + ins.getNumSamples());
                for (int c = 0; c < chs; ++c)
                {
                    out.copyFrom (c, 0, b, c, 0, at);
                    out.copyFrom (c, at, ins, juce::jmin (c, ins.getNumChannels() - 1), 0, ins.getNumSamples());
                    if (cur - at > 0) out.copyFrom (c, at + ins.getNumSamples(), b, c, at, cur - at);
                }
            }
            writeAndLoad (out, src->sampleRate, baseName, op, false);
            wave.selStart = wave.selEnd = -1;
            return;
        }
        else if (op == "Stretch") { stretchDialog (src, a, z); return; }
        else if (op == "Stems") { makeStems (src); return; }
        writeAndLoad (b, src->sampleRate, baseName, op, true, s, e);
    }

    // overlap-add time stretch (keeps pitch)
    static juce::AudioBuffer<float> olaStretch (const juce::AudioBuffer<float>& in, double factor, double sr)
    {
        const int win = (int) (sr * 0.06), hopOut = win / 4;
        const double hopIn = hopOut / factor;
        int outLen = (int) (in.getNumSamples() * factor);
        juce::AudioBuffer<float> out (in.getNumChannels(), juce::jmax (1, outLen + win));
        out.clear();
        juce::AudioBuffer<float> norm (1, out.getNumSamples()); norm.clear();
        std::vector<float> w ((size_t) win);
        for (int i = 0; i < win; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (win - 1));
        for (int k = 0; ; ++k)
        {
            int o = k * hopOut, ip = (int) (k * hopIn);
            if (o >= outLen) break;
            for (int i = 0; i < win; ++i)
            {
                int si = ip + i; if (si >= in.getNumSamples()) break;
                for (int c = 0; c < in.getNumChannels(); ++c) out.addSample (c, o + i, in.getSample (c, si) * w[(size_t) i]);
                norm.addSample (0, o + i, w[(size_t) i]);
            }
        }
        for (int i = 0; i < out.getNumSamples(); ++i) { float n = norm.getSample (0, i); if (n > 0.05f) for (int c = 0; c < out.getNumChannels(); ++c) out.setSample (c, i, out.getSample (c, i) / n); }
        out.setSize (out.getNumChannels(), juce::jmax (1, outLen), true);
        return out;
    }

    void stretchDialog (SamplePtr src, int a, int z)
    {
        auto* aw = new juce::AlertWindow ("Stretch", "Time-stretch the selection (or the whole sample) without changing pitch", juce::MessageBoxIconType::NoIcon);
        double secs = (z - a) / src->sampleRate;
        double beats = secs * engine.project().bpm / 60.0;
        aw->addComboBox ("mode", { "Fit to 1 bar", "Fit to 2 bars", "Fit to 4 bars", "Fit to nearest bar count", "50 %", "75 %", "125 %", "150 %", "200 %" }, "Length");
        aw->getComboBoxComponent ("mode")->setSelectedItemIndex (3);
        aw->addButton ("Stretch", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, src, a, z, beats] (int r)
        {
            if (r != 1) return;
            int m = aw->getComboBoxComponent ("mode")->getSelectedItemIndex();
            double barBeats = 4.0 * engine.project().sigNum / engine.project().sigDen;
            double factor = 1.0;
            if (m <= 2) factor = (barBeats * (1 << m)) / juce::jmax (0.01, beats);
            else if (m == 3) factor = (barBeats * juce::jmax (1.0, std::round (beats / barBeats))) / juce::jmax (0.01, beats);
            else { const double f[] = { 0.5, 0.75, 1.25, 1.5, 2.0 }; factor = f[m - 4]; }
            int chs = src->buffer.getNumChannels(), len = src->buffer.getNumSamples();
            juce::AudioBuffer<float> part (chs, z - a);
            for (int c = 0; c < chs; ++c) part.copyFrom (c, 0, src->buffer, c, a, z - a);
            auto st = olaStretch (part, factor, src->sampleRate);
            juce::AudioBuffer<float> out (chs, a + st.getNumSamples() + (len - z));
            for (int c = 0; c < chs; ++c)
            {
                out.copyFrom (c, 0, src->buffer, c, 0, a);
                out.copyFrom (c, a, st, c, 0, st.getNumSamples());
                if (len - z > 0) out.copyFrom (c, a + st.getNumSamples(), src->buffer, c, z, len - z);
            }
            writeAndLoad (out, src->sampleRate, src->file.getFileNameWithoutExtension(), "Stretch", false);
        }), true);
    }

    // frequency-band "stems": Low / Mid / High onto the next empty pads
    void makeStems (SamplePtr src)
    {
        auto* g = engine.project().group (engine.focusedGroup);
        if (g == nullptr) return;
        engine.snapshot ("Stems");
        const char* names[] = { "Low", "Mid", "High" };
        int pad = g->focusedSound + 1;
        for (int k = 0; k < 3; ++k)
        {
            juce::AudioBuffer<float> b; b.makeCopyOf (src->buffer);
            juce::dsp::ProcessSpec spec { src->sampleRate, (juce::uint32) b.getNumSamples(), (juce::uint32) b.getNumChannels() };
            juce::dsp::LinkwitzRileyFilter<float> f1, f2;
            f1.prepare (spec); f2.prepare (spec);
            juce::dsp::AudioBlock<float> blk (b);
            juce::dsp::ProcessContextReplacing<float> ctx (blk);
            if (k == 0) { f1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); f1.setCutoffFrequency (200.0f); f1.process (ctx); }
            if (k == 1) { f1.setType (juce::dsp::LinkwitzRileyFilterType::highpass); f1.setCutoffFrequency (200.0f); f1.process (ctx); f2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); f2.setCutoffFrequency (3000.0f); f2.process (ctx); }
            if (k == 2) { f1.setType (juce::dsp::LinkwitzRileyFilterType::highpass); f1.setCutoffFrequency (3000.0f); f1.process (ctx); }
            while (pad < numPads && ! g->sounds[(size_t) pad]->slots.empty()) ++pad;
            if (pad >= numPads) break;
            auto folder = Settings::userFolder().getChildFile ("Recordings").getChildFile ("Edits");
            folder.createDirectory();
            auto f = folder.getNonexistentChildFile (src->file.getFileNameWithoutExtension() + " " + names[k], ".wav");
            juce::WavAudioFormat wav;
            if (auto os = f.createOutputStream())
            {
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), src->sampleRate, (unsigned int) b.getNumChannels(), 24, {}, 0));
                if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
            }
            engine.loadSampleIntoSound (*g->sounds[(size_t) pad], f);
            ++pad;
        }
        ui.changed();
    }

    // ------------------------------------------------------------------ slicing
    void computeSlices()
    {
        wave.slices.clear();
        auto* sp = sampler();
        if (sp == nullptr || sp->getSample() == nullptr) { wave.repaint(); return; }
        auto& b = sp->getSample()->buffer;
        float s = wave.start, e = wave.end;
        if (sliceMode->index == 0)
        {
            int n = 4 << sliceCount->index;
            for (int i = 0; i < n; ++i) wave.slices.push_back (s + (e - s) * i / n);
        }
        else
        {
            int hop = 512, a = (int) (s * b.getNumSamples()), z = (int) (e * b.getNumSamples());
            float prev = 0; int lastHit = -100000;
            float thr = 1.5f + (1.0f - sensitivity.value) * 6.0f;
            wave.slices.push_back (s);
            for (int i = a; i + hop < z; i += hop)
            {
                float en = 0;
                for (int c = 0; c < b.getNumChannels(); ++c) en = juce::jmax (en, b.getRMSLevel (c, i, hop));
                if (en > prev * thr && en > 0.02f && i - lastHit > (int) (0.06 * sp->getSample()->sampleRate)) { wave.slices.push_back ((float) i / b.getNumSamples()); lastHit = i; }
                prev = prev * 0.7f + en * 0.3f;
                if ((int) wave.slices.size() >= 64) break;
            }
        }
        wave.repaint();
    }

    void slicesToPads()
    {
        auto* g = engine.project().group (engine.focusedGroup);
        auto* sp = sampler();
        if (g == nullptr || sp == nullptr || wave.slices.empty()) return;
        engine.snapshot ("Slice to Pads");
        auto smp = sp->getSample();
        int startPad = g->focusedSound;
        auto bounds = wave.slices; bounds.push_back (wave.end);
        for (size_t i = 0; i + 1 < bounds.size() && (int) i + startPad < numPads; ++i)
        {
            auto& s = *g->sounds[(size_t) startPad + i];
            engine.loadSampleIntoSound (s, smp->file);
            if (auto* p = engine.samplerOf (s)) { p->setSample (smp); p->setRegion (bounds[i], bounds[i + 1]); }
            s.name = smp->file.getFileNameWithoutExtension().substring (0, 10) + " " + juce::String ((int) i + 1);
        }
        ui.changed();
    }

    void slicesToPattern()
    {
        slicesToPads();
        auto* g = engine.project().group (engine.focusedGroup);
        auto* sp = sampler();
        if (g == nullptr || sp == nullptr) return;
        auto smp = sp->getSample();
        double secs = smp->buffer.getNumSamples() * (wave.end - wave.start) / smp->sampleRate;
        auto lenTicks = (juce::int64) (secs * engine.project().bpm / 60.0 * PPQ);
        auto bar = engine.project().ticksPerBar();
        auto patLen = juce::jmax (bar, ((lenTicks + bar / 2) / bar) * bar);
        auto* p = engine.newPatternFor (*g, patLen);
        const juce::ScopedLock sl (engine.lock);
        p->name = "Slices";
        auto bounds = wave.slices; bounds.push_back (wave.end);
        int startPad = g->focusedSound;
        for (size_t i = 0; i + 1 < bounds.size() && (int) i + startPad < numPads; ++i)
        {
            NoteEvent n; n.sound = startPad + (int) i;
            n.start = (juce::int64) ((bounds[i] - wave.start) / (wave.end - wave.start) * patLen);
            n.length = std::max<juce::int64> (PPQ / 16, (juce::int64) ((bounds[i + 1] - bounds[i]) / (wave.end - wave.start) * patLen));
            p->notes.push_back (n);
        }
        int pi = (int) g->patterns.size() - 1;
        if (engine.songMode) { juce::int64 st = 0; for (auto& c : g->clips) st = juce::jmax (st, c.start + c.length); g->clips.push_back ({ st, patLen, pi }); engine.setEditClip (engine.focusedGroup, (int) g->clips.size() - 1); }
        else { engine.ensureSceneSlots(); engine.project().scenes[(size_t) engine.project().currentScene].patternForGroup[(size_t) engine.focusedGroup] = pi; }
        ui.changed();
    }
};
} // namespace gb
