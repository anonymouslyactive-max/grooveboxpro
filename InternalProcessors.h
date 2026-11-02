#pragma once
#include <JuceHeader.h>

namespace gb
{
extern std::atomic<double> hostBpm;

// Shared, immutable sample data (shared between sounds after slicing / undo)
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::File file;
};
using SamplePtr = std::shared_ptr<const SampleData>;

// Loads (and caches) audio files.
class SampleCache
{
public:
    static SampleCache& get() { static SampleCache c; return c; }
    SamplePtr load (const juce::File& f);
    juce::AudioFormatManager& formats() { return fm; }
private:
    SampleCache() { fm.registerBasicFormats(); }
    juce::AudioFormatManager fm;
    juce::CriticalSection lock;
    std::map<juce::String, std::weak_ptr<const SampleData>> cache;
};

// Base for all built-in processors: parameters + simple state.
class InternalProcessor : public juce::AudioProcessor
{
public:
    InternalProcessor (const juce::String& n, bool instrument);

    // Optional page layout for the channel panel (8 knob slots per page, like the hardware)
    struct UiItem { int slot, param; };
    struct UiSection { int slot; juce::String caption; };
    struct UiPage { juce::String name; std::vector<UiSection> sections; std::vector<UiItem> items; };
    virtual std::vector<UiPage> uiPages() const { return {}; }
    virtual int uiLayoutKey() const { return 0; }   // changes when the page layout depends on a parameter

    const juce::String getName() const override { return name; }
    void prepareToPlay (double sr, int bs) override { sampleRate = sr; blockSize = bs; prepare(); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override { process (b, m); }
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return instrument; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    bool isInstrument() const { return instrument; }
    std::vector<juce::RangedAudioParameter*> params;

protected:
    virtual void prepare() {}
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual void saveExtra (juce::XmlElement&) {}
    virtual void loadExtra (const juce::XmlElement&) {}

    int addFloat (const juce::String& id, const juce::String& label, float mn, float mx, float def, const juce::String& suffix = {}, float skewCentre = 0.0f);
    int addChoice (const juce::String& id, const juce::String& label, const juce::StringArray& choices, int def);
    float v (int i) const;   // current (denormalised) value

    juce::String name;
    bool instrument;
    double sampleRate = 44100.0;
    int blockSize = 512;
};

// ------------------------------------------------------------------------------------------
class SamplerProcessor : public InternalProcessor
{
public:
    SamplerProcessor();
    void setSample (SamplePtr s);
    SamplePtr getSample() const { return sample; }
    void setRegion (float startNorm, float endNorm);
    int rootKey = 60;
    enum P { tune, volume, start, end, mode, attack, hold, decay, sustain, release, reverse, loopP, cutoff, reso, velSens,
             polyphony, pitchbend, engine, comp, drive, srRed, bits, ftype, eqLow, eqMid, eqHigh,
             menvA, menvH, menvD, menvPitch, menvCutoff, menvDrive, menvPan,
             lfoType, lfoSpeed, lfoPhase, lfoSync, lfoPitch, lfoCutoff, lfoDrive, lfoPan,
             velStart, velDecay, velCutoff, mwStart, mwCutoff, mwLfo, mwPan,
             loopStart, loopEnd, xfade, keyLo, keyHi, zoneGain, zonePan, numP };
    float value (int i) const { return v (i); }
    void setValue (int i, float denorm) { params[(size_t) i]->setValueNotifyingHost (params[(size_t) i]->convertTo0to1 (denorm)); }
    std::atomic<float> playPos { -1.0f };   // normalised play position of the newest voice (for waveform display)
    std::vector<UiPage> uiPages() const override;
    int uiLayoutKey() const override { return (int) v (mode) * 10 + (int) v (ftype); }
protected:
    void prepare() override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void saveExtra (juce::XmlElement&) override;
    void loadExtra (const juce::XmlElement&) override;
private:
    struct Voice
    {
        bool active = false; double pos = 0, inc = 1; float gain = 1, vel = 1; int note = 60;
        float env = 0; double stageTime = 0; bool released = false; float chokeFade = 1; bool choking = false;
        double lfoPhase = 0; float decayScale = 1, cutoffVel = 0;
        juce::dsp::StateVariableTPTFilter<float> filt;
    };
    std::array<Voice, 16> voices;
    SamplePtr sample;
    float bend = 0, modWheel = 0;
    double freeLfo = 0;
    float compEnv = 0, holdL = 0, holdR = 0; double srCount = 0;
    float engL = 0, engR = 0, engCount = 0, engHoldL = 0, engHoldR = 0;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> eqLo, eqMi, eqHi, engineLp;
    int lastEngine = -1;
    void startVoice (int note, float vel);
    float lfoValue (double phase, int type);
    juce::Random rng;
    float randomLfo = 0; int lastLfoCycle = -1;
};

class DrumSynthProcessor : public InternalProcessor
{
public:
    enum Model { kick, snare, hihat, tom, perc };
    explicit DrumSynthProcessor (Model m);
    Model model;
protected:
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    struct Voice { bool active = false; double t = 0; double phase = 0, phase2 = 0; float vel = 1; float hp = 0, lp = 0, bp1 = 0, bp2 = 0; };
    std::array<Voice, 4> voices;
    juce::Random rng;
    float renderVoice (Voice&);
};

class BassSynthProcessor : public InternalProcessor
{
public:
    BassSynthProcessor();
protected:
    void prepare() override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    double phase = 0, subPhase = 0, freq = 55, targetFreq = 55; float env = 0, ampEnv = 0; bool gate = false; int curNote = -1;
    std::vector<int> held;
    juce::dsp::StateVariableTPTFilter<float> filt;
};

class PolySynthProcessor : public InternalProcessor
{
public:
    PolySynthProcessor();
protected:
    void prepare() override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    struct Voice { bool active = false; int note = 0; double p1 = 0, p2 = 0, freq = 440; float vel = 1; juce::ADSR adsr; juce::dsp::StateVariableTPTFilter<float> f; };
    std::array<Voice, 10> voices;
};

// Generic effect created from a name (see InternalFx.cpp)
std::unique_ptr<InternalProcessor> createInternalEffect (const juce::String& name);

// Factory for everything listed in the Internal menu
std::unique_ptr<juce::AudioProcessor> createInternal (const juce::String& name);
bool isInternalInstrument (const juce::String& name);

// Menu layout of the "Internal" sub-menu (instruments then effect sections)
struct InternalMenuSection { juce::StringArray items; };
const std::vector<InternalMenuSection>& internalMenu();
const juce::StringArray& drumSynthModels();
} // namespace gb
