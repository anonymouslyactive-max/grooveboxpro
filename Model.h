#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <atomic>
#include <functional>
#include <algorithm>
#include <tuple>
#include <limits>
#include <iostream>

namespace gb
{
constexpr int PPQ = 960;
constexpr int numPads = 16;

// Routing destinations used by Output / Aux pages: 0 None, 1 Group (sounds only), 2 Master, 3.. Ext 1..16
enum Dest { destNone = 0, destGroup = 1, destMaster = 2, destExt1 = 3 };

struct NoteEvent
{
    int sound = 0;
    juce::int64 start = 0;     // ticks
    juce::int64 length = 240;  // ticks
    float velocity = 0.8f;     // 0..1
    float probability = 1.0f;  // 0..1
    int pitch = -1;            // -1 = sound root key
    bool selected = false;
};

// Step automation for a parameter of a sound (one value per grid step, NaN = no automation)
struct AutoLane
{
    int sound = 0;
    juce::String target;       // "level", "pan", "tune" or "slot:<index>:<paramIndex>"
    juce::String name;
    std::map<int, float> steps; // step index (at 1/16) -> normalised value
};

struct Pattern
{
    juce::String name { "Pattern 1" };
    juce::int64 length = PPQ * 4;  // ticks
    std::vector<NoteEvent> notes;
    std::vector<AutoLane> lanes;
    juce::Colour colour;
};

// A plugin / instrument slot in a chain
struct Slot
{
    juce::String type;            // internal name ("Sampler", "Compressor", ...) or "VST3"
    juce::String pluginId;        // for VST3: PluginDescription::createIdentifierString()
    juce::String displayName;
    bool bypass = false;
    bool isInstrument = false;
    std::unique_ptr<juce::AudioProcessor> proc;
    juce::MemoryBlock pendingState; // used while loading
};

struct Groove
{
    float swing = 0.0f;   // 0..1
    int cycle = 1;        // 0 = 1/16, 1 = 1/8, 2 = 1/4
    bool invert = false;
};

struct Routing
{
    int outDest = destGroup;
    float level = 1.0f;     // linear gain (0..2)
    float pan = 0.0f;       // -1..1
    bool cue = false;
    int aux1Dest = destNone, aux2Dest = destNone;
    float aux1Level = 0.0f, aux2Level = 0.0f;
    bool aux1Post = true, aux2Post = true;
    int audioIn = 0;        // 0 None, 1..16 In n L+R, 17.. mono
    int midiSource = 0;     // 0 Default, 1 Focus, 2 None, 3 All, 4.. device index
    int midiChannel = 0;    // 0 = All
    bool midiThru = true;
    int keyMode = 0;        // groups: 0 Off, 1 Drumkit, 2 Manual
};

struct Macro
{
    juce::String name;
    int slot = -1, param = -1;  // target
    float value = 0.5f;
};

struct Sound
{
    juce::String name;
    juce::Colour colour;
    std::vector<std::unique_ptr<Slot>> slots;
    Routing routing;
    Groove groove;
    bool mute = false, solo = false;
    int rootKey = 60;           // C3
    int triggerNote = -1;       // incoming MIDI note that plays this pad in Drumkit mode (-1 = default)
    int chokeGroup = 0, chokeMode = 0;   // mode: 0 Send, 1 Receive, 2 Send+Receive
    int linkGroup = 0, linkMode = 0;
    int focusMode = 0;          // 0 On Trigger, 1 Pinned
    std::array<Macro, 8> macros;
    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<int> lastTrigger { 0 };   // incremented when triggered (GUI flashes the pad)
    bool hasContent() const { return ! slots.empty(); }
};

struct Clip
{
    juce::int64 start = 0;      // ticks
    juce::int64 length = PPQ * 4;
    int pattern = 0;
    int section = -1;           // id of the song section that generated this clip (-1 = a normal clip)
};

// Song view section (the bar above the clips): a stretch of the song that plays an Ideas scene
struct Section
{
    int id = 0;
    juce::int64 start = 0, length = 0;
    int scene = -1;             // -1 = None
    std::vector<juce::int64> sig;   // what the generated clips were built from (not saved)
};

struct Group
{
    juce::String name;
    juce::Colour colour;
    std::array<std::unique_ptr<Sound>, numPads> sounds;
    std::vector<std::unique_ptr<Pattern>> patterns;
    std::vector<Clip> clips;    // Song view arrangement for this group
    std::vector<std::unique_ptr<Slot>> slots;
    Routing routing;
    Groove groove;
    bool mute = false, solo = false;
    int focusedSound = 0;
    int currentPattern = 0;
    juce::File file;            // where the group was last opened from / saved to (not stored in projects)
    std::array<Macro, 8> macros;
    std::atomic<float> meterL { 0 }, meterR { 0 };

    Group()
    {
        routing.outDest = destMaster;
        for (int i = 0; i < numPads; ++i)
        {
            sounds[(size_t) i] = std::make_unique<Sound>();
            sounds[(size_t) i]->name = "Sound " + juce::String (i + 1);
        }
    }
    Pattern* pattern (int i) { return juce::isPositiveAndBelow (i, (int) patterns.size()) ? patterns[(size_t) i].get() : nullptr; }
};

struct Scene
{
    juce::String name;
    std::vector<int> patternForGroup;   // -1 = empty
    bool placed = false;                // has had a song section made for it automatically
    int patternFor (int g) const { return juce::isPositiveAndBelow (g, (int) patternForGroup.size()) ? patternForGroup[(size_t) g] : -1; }
};

struct Project
{
    juce::String name { "New Project" };
    juce::File file;
    double bpm = 120.0;
    float swing = 0.0f;
    int sigNum = 4, sigDen = 4;
    std::vector<std::unique_ptr<Group>> groups;
    std::vector<Scene> scenes;
    int currentScene = 0;
    std::vector<std::unique_ptr<Slot>> masterSlots;
    Routing master;
    Groove masterGroove;
    std::array<Macro, 8> masterMacros;
    juce::int64 loopStart = 0, loopEnd = PPQ * 4 * 4;
    bool loopOn = false;
    std::atomic<float> meterL { 0 }, meterR { 0 };

    Project() { master.outDest = destExt1; }

    juce::int64 ticksPerBar() const { return (juce::int64) PPQ * 4 * sigNum / sigDen; }
    Group* group (int i) { return juce::isPositiveAndBelow (i, (int) groups.size()) ? groups[(size_t) i].get() : nullptr; }

    juce::int64 songLength() const
    {
        juce::int64 e = 0;
        for (auto& g : groups) for (auto& c : g->clips) e = juce::jmax (e, c.start + c.length);
        for (auto& s : sections) e = juce::jmax (e, s.start + s.length);
        return e;
    }
    std::vector<Section> sections;
    int nextSectionId = 1;
    Section* section (int id) { for (auto& s : sections) if (s.id == id) return &s; return nullptr; }
};

inline juce::String groupLetterName (int index)
{
    // A1..H1, A2.. like the original naming
    return juce::String::charToString ((juce::juce_wchar) ('A' + index % 8)) + juce::String (index / 8 + 1);
}

inline juce::String destName (int d, bool isSound)
{
    if (d == destNone) return "None";
    if (d == destGroup) return isSound ? "Group" : "Master";
    if (d == destMaster) return "Master";
    return "Ext. " + juce::String (d - destExt1 + 1);
}

inline juce::String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[(n % 12 + 12) % 12]) + juce::String (n / 12 - 2);   // 60 = C3 (Maschine convention)
}

inline juce::String formatTicks (juce::int64 t, const Project& p, bool position)
{
    auto bar = p.ticksPerBar();
    auto beatT = (juce::int64) PPQ * 4 / p.sigDen;
    auto b = t / bar, rem = t % bar;
    auto beat = rem / beatT, six = (rem % beatT) / (PPQ / 4);
    if (position) return juce::String (b + 1) + ":" + juce::String (beat + 1) + ":" + juce::String (six + 1);
    return juce::String (b) + ":" + juce::String (beat) + ":" + juce::String (six);
}
} // namespace gb
