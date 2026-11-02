#pragma once
#include <JuceHeader.h>

// Groovebox Pro look: dark greys with the orange accent, matched to the reference screenshots.
namespace gb
{
namespace col
{
    const juce::Colour header      { 0xff181818 };
    const juce::Colour bg          { 0xff212121 };
    const juce::Colour panel       { 0xff242424 };
    const juce::Colour editorBg    { 0xff1c1c1c };
    const juce::Colour row         { 0xff2b2b2b };
    const juce::Colour rowAlt      { 0xff262626 };
    const juce::Colour rowSel      { 0xff333333 };
    const juce::Colour ruler       { 0xff323232 };
    const juce::Colour rulerDark   { 0xff2a2a2a };
    const juce::Colour line        { 0xff141414 };
    const juce::Colour gridLine    { 0xff2c2c2c };
    const juce::Colour gridBar     { 0xff4a4a4a };
    const juce::Colour orange      { 0xffff7442 };
    const juce::Colour orangeClip  { 0xffff8a60 };
    const juce::Colour orangeDim   { 0xffb05a36 };
    const juce::Colour text        { 0xffe6e6e6 };
    const juce::Colour textDim     { 0xff9a9a9a };
    const juce::Colour textDark    { 0xff6a6a6a };
    const juce::Colour button      { 0xff3a3a3a };
    const juce::Colour buttonHi    { 0xff454545 };
    const juce::Colour menuBg      { 0xff2e2e2e };
    const juce::Colour knobTrack   { 0xff3c3c3c };
    const juce::Colour pianoWhite  { 0xffd9a58f };
    const juce::Colour pianoBlack  { 0xff5a3a2c };
    const juce::Colour meterGreen  { 0xff6fcf6f };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

// Colour palette used for groups / sounds (Maschine-style 16 colours).
inline juce::Colour paletteColour (int i)
{
    static const juce::uint32 c[] = { 0xffff7442, 0xffff5252, 0xffff4f9a, 0xffd65cff, 0xff9a6bff, 0xff5c7cff,
                                      0xff3fa9ff, 0xff2fd0e0, 0xff2fd9a4, 0xff4cd964, 0xff9bd94c, 0xffd9d94c,
                                      0xffffcf3f, 0xffffa43f, 0xffc0c0c0, 0xff8a8a8a };
    return juce::Colour (c[(i % 16 + 16) % 16]);
}

// ---------------------------------------------------------------------------------------------
// Vector icons drawn to match the original toolbar glyphs.
namespace icons
{
    enum Id { dots, library, mixer, rewind, play, stop, record, loop, metronome, follow, keyboard, power,
              arranger, knob, plug, chevronDown, chevronUp, listIcon, patternIcon, waveIcon, palette, ccIcon,
              pencil, grid, speaker, padsIcon, search, star, eye, plus, close, folder, file, audioFile,
              projectIcon, groupIcon, soundIcon, instrumentIcon, fxIcon, loopIcon, oneshotIcon, userIcon,
              headphones, cpuIcon, logoMark, arrowUpDown, input, output, groove, macro, refresh, info, triangleDown };

    inline void draw (juce::Graphics& g, Id id, juce::Rectangle<float> r, juce::Colour c)
    {
        using namespace juce;
        g.setColour (c);
        auto cx = r.getCentreX(), cy = r.getCentreY();
        auto s = jmin (r.getWidth(), r.getHeight());
        auto sq = Rectangle<float> (s, s).withCentre ({ cx, cy });
        Path p;
        switch (id)
        {
            case dots:
                for (int i = -1; i <= 1; ++i) g.fillEllipse (cx + i * s * 0.3f - s * 0.08f, cy - s * 0.08f, s * 0.16f, s * 0.16f);
                break;
            case library:
                g.fillRect (sq.getX() + s * 0.15f, sq.getY() + s * 0.15f, s * 0.1f, s * 0.7f);
                g.fillRect (sq.getX() + s * 0.38f, sq.getY() + s * 0.15f, s * 0.1f, s * 0.7f);
                p.startNewSubPath (sq.getX() + s * 0.62f, sq.getY() + s * 0.17f);
                p.lineTo (sq.getX() + s * 0.85f, sq.getY() + s * 0.83f);
                g.strokePath (p, PathStrokeType (s * 0.1f));
                break;
            case mixer:
                for (int i = 0; i < 3; ++i)
                {
                    float x = sq.getX() + s * (0.2f + i * 0.3f);
                    g.fillRect (x - s * 0.04f, sq.getY() + s * 0.12f, s * 0.08f, s * 0.76f);
                    float ky = sq.getY() + s * (i == 1 ? 0.62f : 0.32f);
                    g.fillRect (x - s * 0.13f, ky, s * 0.26f, s * 0.1f);
                }
                break;
            case rewind:
                g.fillRect (sq.getX() + s * 0.18f, sq.getY() + s * 0.2f, s * 0.1f, s * 0.6f);
                p.addTriangle (sq.getX() + s * 0.82f, sq.getY() + s * 0.2f, sq.getX() + s * 0.82f, sq.getY() + s * 0.8f, sq.getX() + s * 0.3f, cy);
                g.fillPath (p);
                break;
            case play:
                p.addTriangle (sq.getX() + s * 0.25f, sq.getY() + s * 0.18f, sq.getX() + s * 0.25f, sq.getY() + s * 0.82f, sq.getX() + s * 0.82f, cy);
                g.fillPath (p);
                break;
            case stop:
                g.fillRect (sq.reduced (s * 0.22f));
                break;
            case record:
                g.fillEllipse (sq.reduced (s * 0.2f));
                break;
            case loop:
            {
                auto rr = sq.reduced (s * 0.08f, s * 0.25f);
                g.drawRoundedRectangle (rr, rr.getHeight() * 0.5f, s * 0.09f);
                p.addTriangle (rr.getCentreX() - s * 0.02f, rr.getY() - s * 0.12f, rr.getCentreX() - s * 0.02f, rr.getY() + s * 0.12f, rr.getCentreX() + s * 0.16f, rr.getY());
                g.fillPath (p);
                break;
            }
            case metronome:
                p.addTriangle (sq.getX() + s * 0.3f, sq.getY() + s * 0.15f, sq.getX() + s * 0.7f, sq.getY() + s * 0.15f, sq.getX() + s * 0.85f, sq.getBottom() - s * 0.12f);
                p.lineTo (sq.getX() + s * 0.15f, sq.getBottom() - s * 0.12f);
                p.closeSubPath();
                g.fillPath (p);
                g.setColour (Colours::black.withAlpha (0.6f));
                g.drawLine (cx, sq.getBottom() - s * 0.2f, sq.getX() + s * 0.72f, sq.getY() + s * 0.25f, s * 0.08f);
                break;
            case follow:
                g.drawLine (sq.getX() + s * 0.05f, cy, sq.getX() + s * 0.5f, cy, s * 0.08f);
                p.addTriangle (sq.getX() + s * 0.42f, cy - s * 0.15f, sq.getX() + s * 0.42f, cy + s * 0.15f, sq.getX() + s * 0.62f, cy);
                g.fillPath (p);
                for (int i = 0; i < 3; ++i) g.fillRect (sq.getX() + s * (0.7f + i * 0.11f), cy - s * 0.03f, s * 0.06f, s * 0.06f);
                break;
            case keyboard:
            {
                auto kr = sq.reduced (s * 0.05f, s * 0.18f);
                g.fillRoundedRectangle (kr, s * 0.08f);
                g.setColour (Colours::black.withAlpha (0.7f));
                for (int row = 0; row < 3; ++row)
                    for (int i = 0; i < 5; ++i)
                        g.fillRect (kr.getX() + kr.getWidth() * (0.1f + i * 0.17f), kr.getY() + kr.getHeight() * (0.18f + row * 0.26f), kr.getWidth() * 0.09f, kr.getHeight() * 0.13f);
                break;
            }
            case power:
                p.addCentredArc (cx, cy + s * 0.04f, s * 0.32f, s * 0.32f, 0, 0.6f, MathConstants<float>::twoPi - 0.6f, true);
                g.strokePath (p, PathStrokeType (s * 0.1f, PathStrokeType::curved, PathStrokeType::rounded));
                g.drawLine (cx, sq.getY() + s * 0.12f, cx, cy, s * 0.1f);
                break;
            case arranger:
                g.fillRect (sq.getX() + s * 0.1f, sq.getY() + s * 0.3f, s * 0.8f, s * 0.08f);
                g.fillRect (sq.getX() + s * 0.1f, sq.getY() + s * 0.62f, s * 0.8f, s * 0.08f);
                g.fillRect (sq.getX() + s * 0.32f, sq.getY() + s * 0.12f, s * 0.08f, s * 0.36f);
                g.fillRect (sq.getX() + s * 0.6f, sq.getY() + s * 0.5f, s * 0.08f, s * 0.38f);
                break;
            case knob:
                g.drawEllipse (sq.reduced (s * 0.14f), s * 0.1f);
                g.drawLine (cx, cy, cx + s * 0.2f, cy - s * 0.2f, s * 0.1f);
                break;
            case plug:
                g.fillRoundedRectangle (sq.getX() + s * 0.28f, sq.getY() + s * 0.25f, s * 0.5f, s * 0.5f, s * 0.15f);
                g.fillRect (sq.getX() + s * 0.1f, cy - s * 0.04f, s * 0.2f, s * 0.08f);
                g.fillRect (sq.getX() + s * 0.75f, sq.getY() + s * 0.33f, s * 0.15f, s * 0.07f);
                g.fillRect (sq.getX() + s * 0.75f, sq.getY() + s * 0.6f, s * 0.15f, s * 0.07f);
                break;
            case chevronDown:
            case chevronUp:
            {
                float d = id == chevronDown ? 1.0f : -1.0f;
                p.startNewSubPath (cx - s * 0.25f, cy - d * s * 0.12f);
                p.lineTo (cx, cy + d * s * 0.12f);
                p.lineTo (cx + s * 0.25f, cy - d * s * 0.12f);
                g.strokePath (p, PathStrokeType (s * 0.1f));
                break;
            }
            case triangleDown:
                p.addTriangle (cx - s * 0.2f, cy - s * 0.1f, cx + s * 0.2f, cy - s * 0.1f, cx, cy + s * 0.12f);
                g.fillPath (p);
                break;
            case listIcon:
                for (int i = 0; i < 4; ++i)
                    g.fillRect (sq.getX() + s * 0.12f, sq.getY() + s * (0.17f + i * 0.2f), s * (i % 2 ? 0.55f : 0.76f), s * 0.09f);
                break;
            case patternIcon:
                g.fillRoundedRectangle (sq.reduced (s * 0.14f), s * 0.06f);
                g.setColour (Colours::black.withAlpha (0.8f));
                for (int i = 0; i < 3; ++i) g.fillRect (sq.getX() + s * 0.24f, sq.getY() + s * (0.28f + i * 0.17f), s * 0.52f, s * 0.07f);
                break;
            case waveIcon:
                for (int i = 0; i < 7; ++i)
                {
                    float h = s * (0.2f + 0.6f * std::abs (std::sin (i * 1.3f)));
                    g.fillRect (sq.getX() + s * (0.1f + i * 0.12f), cy - h * 0.5f, s * 0.07f, h);
                }
                break;
            case palette:
                g.drawEllipse (sq.reduced (s * 0.12f), s * 0.09f);
                for (int i = 0; i < 3; ++i)
                {
                    float a = -2.4f + i * 1.0f;
                    g.fillEllipse (cx + std::cos (a) * s * 0.2f - s * 0.06f, cy + std::sin (a) * s * 0.2f - s * 0.06f, s * 0.12f, s * 0.12f);
                }
                break;
            case ccIcon:
                g.fillRect (sq.getX() + s * 0.15f, sq.getY() + s * 0.2f, s * 0.08f, s * 0.6f);
                g.fillRect (sq.getX() + s * 0.77f, sq.getY() + s * 0.2f, s * 0.08f, s * 0.6f);
                g.fillRect (sq.getX() + s * 0.46f, sq.getY() + s * 0.45f, s * 0.08f, s * 0.35f);
                g.fillEllipse (sq.getX() + s * 0.42f, sq.getY() + s * 0.25f, s * 0.16f, s * 0.16f);
                break;
            case pencil:
                p.startNewSubPath (sq.getX() + s * 0.2f, sq.getBottom() - s * 0.2f);
                p.lineTo (sq.getRight() - s * 0.2f, sq.getY() + s * 0.2f);
                g.strokePath (p, PathStrokeType (s * 0.14f));
                break;
            case grid:
                for (int i = 0; i < 2; ++i)
                {
                    g.fillRect (sq.getX() + s * (0.32f + i * 0.3f), sq.getY() + s * 0.15f, s * 0.08f, s * 0.7f);
                    g.fillRect (sq.getX() + s * 0.15f, sq.getY() + s * (0.32f + i * 0.3f), s * 0.7f, s * 0.08f);
                }
                break;
            case speaker:
                p.startNewSubPath (sq.getX() + s * 0.15f, sq.getY() + s * 0.38f);
                p.lineTo (sq.getX() + s * 0.35f, sq.getY() + s * 0.38f);
                p.lineTo (sq.getX() + s * 0.6f, sq.getY() + s * 0.15f);
                p.lineTo (sq.getX() + s * 0.6f, sq.getY() + s * 0.85f);
                p.lineTo (sq.getX() + s * 0.35f, sq.getY() + s * 0.62f);
                p.lineTo (sq.getX() + s * 0.15f, sq.getY() + s * 0.62f);
                p.closeSubPath();
                g.fillPath (p);
                break;
            case padsIcon:
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                        g.fillRect (sq.getX() + s * (0.12f + x * 0.2f), sq.getY() + s * (0.12f + y * 0.2f), s * 0.12f, s * 0.12f);
                break;
            case search:
                g.drawEllipse (sq.getX() + s * 0.32f, sq.getY() + s * 0.12f, s * 0.5f, s * 0.5f, s * 0.1f);
                g.drawLine (sq.getX() + s * 0.4f, sq.getY() + s * 0.55f, sq.getX() + s * 0.15f, sq.getY() + s * 0.85f, s * 0.12f);
                break;
            case star:
                p.addStar ({ cx, cy }, 5, s * 0.18f, s * 0.42f, -MathConstants<float>::halfPi * 0.0f);
                g.fillPath (p);
                break;
            case eye:
                p.addEllipse (sq.reduced (s * 0.08f, s * 0.26f));
                g.fillPath (p);
                g.setColour (Colours::black);
                g.fillEllipse (sq.reduced (s * 0.36f));
                break;
            case plus:
                g.fillRect (cx - s * 0.05f, sq.getY() + s * 0.18f, s * 0.1f, s * 0.64f);
                g.fillRect (sq.getX() + s * 0.18f, cy - s * 0.05f, s * 0.64f, s * 0.1f);
                break;
            case close:
                g.drawLine (sq.getX() + s * 0.25f, sq.getY() + s * 0.25f, sq.getRight() - s * 0.25f, sq.getBottom() - s * 0.25f, s * 0.08f);
                g.drawLine (sq.getRight() - s * 0.25f, sq.getY() + s * 0.25f, sq.getX() + s * 0.25f, sq.getBottom() - s * 0.25f, s * 0.08f);
                break;
            case folder:
                p.startNewSubPath (sq.getX() + s * 0.08f, sq.getY() + s * 0.2f);
                p.lineTo (sq.getX() + s * 0.4f, sq.getY() + s * 0.2f);
                p.lineTo (sq.getX() + s * 0.5f, sq.getY() + s * 0.3f);
                p.lineTo (sq.getRight() - s * 0.08f, sq.getY() + s * 0.3f);
                p.lineTo (sq.getRight() - s * 0.08f, sq.getBottom() - s * 0.18f);
                p.lineTo (sq.getX() + s * 0.08f, sq.getBottom() - s * 0.18f);
                p.closeSubPath();
                g.fillPath (p);
                break;
            case file: case audioFile:
                g.drawRect (sq.reduced (s * 0.22f, s * 0.12f), s * 0.08f);
                if (id == audioFile)
                    for (int i = 0; i < 3; ++i) g.fillRect (sq.getX() + s * (0.36f + i * 0.1f), cy - s * (0.08f + (i % 2) * 0.08f), s * 0.05f, s * (0.16f + (i % 2) * 0.16f));
                break;
            case projectIcon:
                g.fillRect (sq.getX() + s * 0.15f, sq.getY() + s * 0.2f, s * 0.7f, s * 0.1f);
                g.fillRect (sq.getX() + s * 0.2f, sq.getY() + s * 0.36f, s * 0.6f, s * 0.44f);
                break;
            case groupIcon:
                for (int i = 0; i < 4; ++i) g.fillRect (sq.getX() + s * (0.1f + i * 0.22f), cy - s * 0.08f, s * 0.15f, s * 0.16f);
                break;
            case soundIcon:
                for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x)
                    g.fillRect (sq.getX() + s * (0.16f + x * 0.25f), sq.getY() + s * (0.16f + y * 0.25f), s * 0.17f, s * 0.17f);
                break;
            case instrumentIcon:
                g.fillRect (sq.reduced (s * 0.14f));
                g.setColour (Colours::black);
                for (int i = 0; i < 3; ++i) g.fillRect (sq.getX() + s * (0.3f + i * 0.17f), sq.getY() + s * 0.14f, s * 0.07f, s * 0.42f);
                break;
            case fxIcon:
                g.fillRect (sq.reduced (s * 0.14f));
                g.setColour (Colours::black);
                g.setFont (font (s * 0.42f, true));
                g.drawText ("FX", sq, Justification::centred);
                break;
            case loopIcon:
                p.startNewSubPath (sq.getX() + s * 0.2f, cy);
                p.lineTo (sq.getX() + s * 0.2f, sq.getY() + s * 0.3f);
                p.lineTo (sq.getRight() - s * 0.25f, sq.getY() + s * 0.3f);
                p.startNewSubPath (sq.getRight() - s * 0.2f, cy);
                p.lineTo (sq.getRight() - s * 0.2f, sq.getBottom() - s * 0.3f);
                p.lineTo (sq.getX() + s * 0.25f, sq.getBottom() - s * 0.3f);
                g.strokePath (p, PathStrokeType (s * 0.09f));
                break;
            case oneshotIcon: case cpuIcon:
                for (int i = 0; i < 6; ++i)
                {
                    float h = s * (0.15f + 0.55f * std::abs (std::cos (i * 0.9f)));
                    g.fillRect (sq.getX() + s * (0.12f + i * 0.13f), cy - h * 0.5f, s * 0.06f, h);
                }
                break;
            case userIcon:
                g.fillRoundedRectangle (sq.reduced (s * 0.12f), s * 0.08f);
                g.setColour (Colours::black);
                g.fillEllipse (cx - s * 0.13f, sq.getY() + s * 0.24f, s * 0.26f, s * 0.26f);
                g.fillRoundedRectangle (cx - s * 0.22f, sq.getY() + s * 0.55f, s * 0.44f, s * 0.2f, s * 0.1f);
                break;
            case headphones:
                p.addCentredArc (cx, cy + s * 0.05f, s * 0.3f, s * 0.3f, 0, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
                g.strokePath (p, PathStrokeType (s * 0.09f));
                g.fillRect (cx - s * 0.36f, cy + s * 0.05f, s * 0.14f, s * 0.26f);
                g.fillRect (cx + s * 0.22f, cy + s * 0.05f, s * 0.14f, s * 0.26f);
                break;
            case logoMark:
                p.startNewSubPath (sq.getX() + s * 0.12f, sq.getBottom() - s * 0.2f);
                p.lineTo (sq.getX() + s * 0.12f, sq.getY() + s * 0.2f);
                p.lineTo (sq.getRight() - s * 0.12f, sq.getBottom() - s * 0.2f);
                p.lineTo (sq.getRight() - s * 0.12f, sq.getY() + s * 0.2f);
                g.strokePath (p, PathStrokeType (s * 0.1f));
                break;
            case arrowUpDown:
                p.addTriangle (cx - s * 0.12f, cy - s * 0.12f, cx + s * 0.12f, cy - s * 0.12f, cx, cy - s * 0.3f);
                p.addTriangle (cx - s * 0.12f, cy + s * 0.12f, cx + s * 0.12f, cy + s * 0.12f, cx, cy + s * 0.3f);
                g.fillPath (p);
                break;
            case input: case output:
                g.drawEllipse (sq.reduced (s * 0.18f), s * 0.09f);
                p.addTriangle (cx - s * 0.12f, cy - s * 0.12f, cx - s * 0.12f, cy + s * 0.12f, cx + s * 0.12f, cy);
                if (id == input) p.applyTransform (AffineTransform::rotation (MathConstants<float>::pi, cx, cy));
                g.fillPath (p);
                break;
            case groove:
                g.fillRect (cx - s * 0.04f, sq.getY() + s * 0.2f, s * 0.08f, s * 0.6f);
                g.fillRect (sq.getX() + s * 0.2f, cy - s * 0.04f, s * 0.6f, s * 0.08f);
                break;
            case macro:
                g.drawEllipse (sq.reduced (s * 0.18f), s * 0.09f);
                g.drawLine (cx, cy, cx + s * 0.15f, cy - s * 0.1f, s * 0.09f);
                break;
            case refresh:
                p.addCentredArc (cx, cy, s * 0.3f, s * 0.3f, 0, 0.5f, MathConstants<float>::twoPi - 0.3f, true);
                g.strokePath (p, PathStrokeType (s * 0.09f));
                break;
            case info:
                g.fillEllipse (sq.reduced (s * 0.12f));
                g.setColour (Colours::black);
                g.fillRect (cx - s * 0.05f, cy - s * 0.08f, s * 0.1f, s * 0.3f);
                g.fillEllipse (cx - s * 0.06f, cy - s * 0.27f, s * 0.12f, s * 0.12f);
                break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        using namespace juce;
        setColourScheme ({ col::bg, col::header, col::panel, col::textDim, col::text, col::orange,
                           Colours::white, col::buttonHi, col::text });
        setColour (PopupMenu::backgroundColourId, col::menuBg);
        setColour (PopupMenu::textColourId, col::text);
        setColour (PopupMenu::highlightedBackgroundColourId, col::buttonHi);
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (TextButton::buttonColourId, col::button);
        setColour (TextButton::buttonOnColourId, col::buttonHi);
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (ComboBox::backgroundColourId, col::row);
        setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        setColour (ComboBox::textColourId, col::text);
        setColour (ComboBox::arrowColourId, col::textDim);
        setColour (TextEditor::backgroundColourId, col::header);
        setColour (TextEditor::outlineColourId, col::row);
        setColour (TextEditor::focusedOutlineColourId, col::orange);
        setColour (TextEditor::textColourId, col::text);
        setColour (TextEditor::highlightColourId, col::orangeDim);
        setColour (Label::textColourId, col::text);
        setColour (ListBox::backgroundColourId, col::header);
        setColour (ScrollBar::thumbColourId, col::buttonHi);
        setColour (Slider::thumbColourId, Colours::white);
        setColour (Slider::trackColourId, col::textDim);
        setColour (Slider::backgroundColourId, col::knobTrack);
        setColour (ToggleButton::textColourId, col::text);
        setColour (ToggleButton::tickColourId, col::text);
        setColour (ToggleButton::tickDisabledColourId, col::textDim);
        setColour (ResizableWindow::backgroundColourId, col::bg);
        setColour (DocumentWindow::textColourId, col::text);
        setColour (AlertWindow::backgroundColourId, col::menuBg);
        setColour (AlertWindow::textColourId, col::text);
        setColour (AlertWindow::outlineColourId, col::buttonHi);
        setColour (TooltipWindow::backgroundColourId, col::menuBg);
        setColour (TooltipWindow::textColourId, col::text);
        setColour (TreeView::backgroundColourId, col::header);
        setColour (CaretComponent::caretColourId, col::orange);
    }

    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (13.0f, h * 0.6f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc, bool over, bool down) override
    {
        auto c = b.getToggleState() ? col::buttonHi : bgc;
        if (down) c = c.brighter (0.15f); else if (over) c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 2.0f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (col::menuBg);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRect (0, 0, w, h);
    }

    void drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool, bool, bool) override
    {
        juce::Rectangle<float> r (x, y, w, h);
        r = r.withSizeKeepingCentre (13, 13);
        g.setColour (col::textDim);
        g.drawRect (r, 1.0f);
        if (ticked)
        {
            juce::Path p;
            p.startNewSubPath (r.getX() + 3, r.getCentreY());
            p.lineTo (r.getX() + 5.5f, r.getBottom() - 3.5f);
            p.lineTo (r.getRight() - 2.5f, r.getY() + 3);
            g.setColour (col::text);
            g.strokePath (p, juce::PathStrokeType (1.6f));
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            auto cy = (float) y + h * 0.5f;
            g.setColour (col::knobTrack);
            g.fillRect ((float) x, cy - 1.5f, (float) w, 3.0f);
            g.setColour (col::textDim);
            g.fillRect ((float) x, cy - 1.5f, pos - x, 3.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (pos - 2.0f, cy - 7.0f, 4.0f, 14.0f);
            return;
        }
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos, int thumbSize, bool over, bool) override
    {
        g.setColour (col::header);
        g.fillRect (x, y, w, h);
        g.setColour (over ? col::textDark : col::buttonHi);
        if (vertical) g.fillRoundedRectangle ((float) x + 2, (float) thumbPos, (float) w - 4, (float) thumbSize, 2.0f);
        else          g.fillRoundedRectangle ((float) thumbPos, (float) y + 2, (float) thumbSize, (float) h - 4, 2.0f);
    }

    void drawDocumentWindowTitleBar (juce::DocumentWindow& w, juce::Graphics& g, int width, int height, int, int, const juce::Image*, bool) override
    {
        g.fillAll (col::header);
        g.setColour (col::textDim);
        g.setFont (font (13.0f));
        g.drawText (w.getName(), 10, 0, width - 120, height, juce::Justification::centredLeft);
    }

    // Menu bar (File / Edit / View / Controller / Help)
    void drawMenuBarBackground (juce::Graphics& g, int, int, bool, juce::MenuBarComponent&) override { g.fillAll (juce::Colour (0xfff3f3f3)); }
    void drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text, bool over, bool open, bool, juce::MenuBarComponent&) override
    {
        if (over || open) { g.setColour (juce::Colour (0xffd8e6f5)); g.fillRect (0, 0, w, h); }
        g.setColour (juce::Colours::black);
        g.setFont (font (13.0f));
        g.drawText (text, 0, 0, w, h, juce::Justification::centred);
    }
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override { return font (13.0f); }
};
} // namespace gb
