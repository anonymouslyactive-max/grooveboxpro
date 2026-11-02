#pragma once
#include "Model.h"
#include "InternalProcessors.h"

namespace gb
{
// Persistent application settings (Preferences)
class Settings
{
public:
    Settings();
    juce::PropertiesFile& props() { return *appProps.getUserSettings(); }
    int getInt (const juce::String& k, int def) { return props().getIntValue (k, def); }
    double getDouble (const juce::String& k, double def) { return props().getDoubleValue (k, def); }
    bool getBool (const juce::String& k, bool def) { return props().getBoolValue (k, def); }
    juce::String getString (const juce::String& k, const juce::String& def = {}) { return props().getValue (k, def); }
    void set (const juce::String& k, const juce::var& v) { props().setValue (k, v); props().saveIfNeeded(); }

    juce::StringArray getList (const juce::String& k) { return juce::StringArray::fromLines (getString (k)); }
    void setList (const juce::String& k, const juce::StringArray& a) { set (k, a.joinIntoString ("\n")); }

    static juce::File userFolder();   // Documents/Groovebox Pro
private:
    juce::ApplicationProperties appProps;
};

// MIDI-learnable controller actions (Preferences > Hardware)
struct LearnAction { juce::String id, label; int kind = 0; int defaultCC = -1; };   // kind: 0 button, 1 knob (absolute), 2 encoder
const std::vector<LearnAction>& learnActions();

class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback,
               public juce::ChangeBroadcaster,
               private juce::Timer
{
public:
    Engine();
    ~Engine() override;

    // ---- state
    juce::CriticalSection lock;                 // guards project structure
    Project& project() { return *proj; }
    Settings settings;
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::File pluginListFile() const;
    void savePluginList();

    std::atomic<int> focusedGroup { 0 };
    std::atomic<bool> songMode { true };        // Song view vs Ideas view playback
    std::atomic<bool> keyboardMode { false };   // pads play focused sound chromatically

    // ---- transport
    std::atomic<bool> playing { false }, recording { false }, metronomeOn { false }, followOn { false };
    std::atomic<bool> countingIn { false }, engineOn { true }, syncOn { false };
    std::atomic<double> position { 0.0 };       // ticks
    std::atomic<double> masterVolume { 1.0 };
    std::atomic<int> stepGrid { 4 };            // 1/16 (index into gridChoices)
    void play();
    void stop();
    void togglePlay() { if (playing) stop(); else play(); }
    void restart();
    void toggleRecord();
    void setTempo (double bpm);
    void tapTempo();
    double displayLoopLength();
    static const juce::StringArray& gridChoices();
    static int gridTicks (int idx);

    // ---- pads / live input from GUI (mouse, computer keyboard)
    void triggerPad (int group, int sound, float velocity, bool noteOn, int pitch = -1);
    void previewFile (const juce::File& f);    // browser audition
    void stopPreview();
    std::atomic<float> previewGain { 0.8f };

    // ---- chains
    using Chain = std::vector<std::unique_ptr<Slot>>;
    std::unique_ptr<Slot> createSlot (const juce::String& type, const juce::String& pluginId, juce::String& error);
    void insertSlot (Chain& chain, int index, std::unique_ptr<Slot> s);
    void removeSlot (Chain& chain, int index);
    void moveSlot (Chain& chain, int from, int to);
    void loadSampleIntoSound (Sound& s, const juce::File& f);
    SamplerProcessor* samplerOf (Sound& s);
    juce::String describeSlot (const Slot& s) const;
    void openPluginEditor (Slot& s);
    void closeEditorsFor (const Chain& c);

    // ---- editing helpers
    Pattern* editPattern (int group);           // the pattern currently shown in the editor for a group
    int editClip (int group) const;              // selected clip index in Song mode
    void setEditClip (int group, int clip);
    void addGroup();
    void ensureSceneSlots();
    Pattern* newPatternFor (Group& g, juce::int64 length);
    void selectSound (int group, int sound);
    juce::int64 defaultPatternLength();

    // ---- project I/O & undo
    juce::ValueTree toValueTree (bool forUndo);
    std::unique_ptr<Project> fromValueTree (const juce::ValueTree& v, Project* reuse = nullptr);
    void newProject();
    bool loadProject (const juce::File& f);
    bool saveProject (const juce::File& f, bool withSamples);
    void embedSamples (juce::ValueTree v, const juce::File& folder);
    bool syncSections();
    juce::int64 sceneLength (int scene);
    juce::ValueTree saveGroup (int g);
    void loadGroupInto (int g, const juce::ValueTree& v);
    juce::ValueTree saveSound (int g, int s);
    void loadSoundInto (int g, int s, const juce::ValueTree& v);
    void snapshot (const juce::String& what);
    void restoreSnapshot (const juce::ValueTree& tree);
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    juce::String undoName() const { return undoStack.empty() ? juce::String() : undoStack.back().first; }
    juce::String redoName() const { return redoStack.empty() ? juce::String() : redoStack.back().first; }
    juce::File autoBackupFile() const;
    bool dirty = false;

    // ---- sampling (audio recording)
    struct RecSettings { int mode = 1; int length = 0; int source = 0; int input = 0; bool monitor = false; float threshold = 0.1f; };
    RecSettings rec;
    enum class RecState { idle, waiting, recording, done };
    std::atomic<RecState> recState { RecState::idle };
    std::atomic<float> inLevelL { 0 }, inLevelR { 0 };
    void startSampling();
    void cancelSampling();
    juce::File finishSampling();                  // writes WAV and loads into focused sound; called from GUI
    static const juce::StringArray& recLengths();

    // ---- export
    struct ExportOptions { int range = 0; int bars = 4; int stems = 0; int bitDepth = 24; bool normalize = false; double tail = 2.0; juce::File file; };
    bool exportAudio (const ExportOptions& o, std::function<bool (float)> progress, juce::String& error);
    std::atomic<bool> exporting { false };       // set while exportAudio runs (on the export thread)

    // ---- MIDI
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void refreshMidiInputs();
    juce::StringArray midiInputNames() const;
    std::atomic<int> learnTarget { -1 };          // index into learnActions() while learning
    std::atomic<bool> learnSequential { false };  // guided "Learn All"
    juce::String learnKey (int action);           // stored mapping or the FelonE Beats template default
    void rebuildLearnCache();
    void sendLive (const juce::MidiMessage& m) { InMsg im; im.msg = m; im.device = -1; pushMidi (im); }

    // ---- Maschine+ controller state
    std::atomic<int> padFunction { 0 };           // non-zero: pads run a function (Shift / Mute / Solo ... held) instead of playing
    std::function<void (int pad, float velocity)> onPadFunction;
    std::atomic<bool> noteRepeat { false }, fixedVelOn { false }, chordMode { false };
    std::atomic<int> repeatRate { 4 };            // index into gridChoices
    std::atomic<int> keyboardOffset { 0 };        // Semitone / Octave +/- in keyboard mode
    std::atomic<juce::uint32> heldPads { 0 };
    std::array<std::atomic<float>, numPads> heldVel {};
    std::function<void (int action, float value)> onLearnedAction;  // GUI dispatch (message thread)
    std::atomic<int> lastMidiNote { -1 };

    // ---- recorded notes travel audio->GUI through this fifo
    struct RecNote { int group, sound, pitch; juce::int64 tick; float vel; bool on; };

    // ---- audio callback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    double getSampleRate() const { return sampleRate; }
    float cpu() { return (float) deviceManager.getCpuUsage(); }

    // cached preferences (read by the audio / MIDI threads)
    void reloadSettingsCache();
    std::atomic<int> padBase { 36 }, padChannel { 0 }, velCurve { 0 }, defaultMidiMode { 0 }, inputQuantize { 1 };
    std::atomic<int> countInBars { 1 }, cueDest { 0 }, syncMode { 0 }, fixedVelocity { 100 };
    std::atomic<float> metroVolume { 0.7f };
    std::atomic<bool> autoMetroRec { true };
    float applyVelocityCurve (float v) const;

    std::function<void()> onProjectReplaced;
    std::function<void (const juce::String&)> onStatus;

private:
    std::unique_ptr<Project> proj;
    double sampleRate = 44100.0;
    int blockSize = 512;
    juce::Random rng;

    // buffers
    juce::AudioBuffer<float> soundBuf, groupBuf, masterBuf, aux1Buf, aux2Buf, cueBuf, scratch, inputCopy;
    std::array<juce::AudioBuffer<float>, 16> extBufs;
    std::vector<juce::MidiBuffer> soundMidi;     // [group*16 + sound]
    std::vector<juce::MidiBuffer> groupMidi;
    juce::MidiBuffer emptyMidi;

    // live MIDI fifo
    struct InMsg { juce::MidiMessage msg; int device = -1; int group = -1, sound = -1; };
    juce::AbstractFifo midiFifo { 1024 };
    std::vector<InMsg> midiQueue { 1024 };
    void pushMidi (const InMsg&);
    juce::CriticalSection fifoWriteLock;
    juce::StringArray openedInputs;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;

    juce::AbstractFifo recFifo { 512 };
    std::vector<RecNote> recQueue { 512 };
    struct OpenRecNote { Pattern* pattern = nullptr; int index = -1; juce::int64 onTick = 0; };
    std::map<std::tuple<int, int, int>, OpenRecNote> openRecNotes;   // GUI side

    std::unique_ptr<juce::MidiOutput> clockOut;
    double clockPhase = 0;
    std::vector<double> midiClockTimes; double lastClockTime = 0;

    std::vector<std::pair<juce::String, juce::ValueTree>> undoStack, redoStack;
    std::map<int, int> clipSelection;

    std::atomic<bool> allNotesOffPending { false };
    std::map<juce::String, int> learnCache; juce::CriticalSection learnCacheLock;
    double repeatClock = 0; double lastRepeatTick = -1;
    void processNoteRepeat (int n);
    std::vector<int> padNotes (int pad, int root) const;
    double countInRemaining = 0;   // ticks
    double countInTotal = 0, countInAtBlockStart = 0; bool blockCountIn = false;
    double metroPhase = 0; float metroEnv = 0; float metroFreq = 1000;
    SamplePtr metroDown, metroUp; double metroSamplePos = -1; bool metroSampleDown = false;

    // sampling buffers
    juce::AudioBuffer<float> recBuffer; int recWritePos = 0; int recTargetLen = 0; double recStartTick = 0;

    // preview player
    SamplePtr preview; double previewPos = -1; double previewInc = 1; juce::CriticalSection previewLock;

    std::vector<double> tapTimes;
    std::vector<std::unique_ptr<juce::DocumentWindow>> editorWindows;

    void timerCallback() override;
    void processBlockInternal (const float* const* in, int numIn, int n);
    void scheduleSequencer (int n);
    void scheduleGroup (int gi, Group& g, double from, double to, double samplesPerTick, int n);
    void scheduleGroupPass (int gi, Group& g, double from, double to, double samplesPerTick, int n);
    void emitPattern (int gi, Group& g, const Pattern& p, double base, double from, double to, double spt, int n, juce::int64 clipEnd);
    void noteToSound (int gi, int si, int offset, float vel, int pitch, bool on, bool allowLink = true);
    void handleLiveMidi (const InMsg& m, int offset);
    void routeNote (int device, const juce::MidiMessage& m, int offset);
    void processChain (Chain& chain, juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, int n);
    void route (int dest, const juce::AudioBuffer<float>& src, float gain, float pan, int n, bool isSound);
    void addMetronome (juce::AudioBuffer<float>& out, int n, double fromTick, double spt);
    void applyAutomation (int gi, Group& g, double tick);
    void prepareAll();
    void allocateBuffers (double sr, int bs);
    void prepareChain (Chain& c);
    void handleLearn (const juce::MidiMessage& m);
    void sendClock (int n);
    std::unique_ptr<Project> createDefaultProject();
    void replaceProject (std::unique_ptr<Project> p);
    void finishReplace (std::unique_ptr<Project> old);
    float swingShift (const Group& g, const Sound& s, juce::int64 start) const;

    // per-block variables
    int emitPass = 1;              // emitPattern: 0 = note-offs only, 1 = note-ons only
    double blockStartTick = 0;
    bool anyGroupSolo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
} // namespace gb
