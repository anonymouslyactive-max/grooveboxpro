#include "InternalProcessors.h"

// Built-in effects matching the "Internal" menu.
namespace gb
{
namespace
{
using juce::dsp::ProcessSpec;

inline float dbg (float db) { return juce::Decibels::decibelsToGain (db); }

struct Fx : InternalProcessor
{
    using InternalProcessor::InternalProcessor;
    ProcessSpec spec() const { return { sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 }; }
    void mixDry (juce::AudioBuffer<float>& wet, const juce::AudioBuffer<float>& dry, float mix)
    {
        for (int c = 0; c < wet.getNumChannels(); ++c)
        {
            auto* w = wet.getWritePointer (c);
            auto* d = dry.getReadPointer (juce::jmin (c, dry.getNumChannels() - 1));
            for (int i = 0; i < wet.getNumSamples(); ++i) w[i] = d[i] + (w[i] - d[i]) * mix;
        }
    }
    juce::AudioBuffer<float> dry;
    void keepDry (const juce::AudioBuffer<float>& b) { dry.makeCopyOf (b, true); }
};

// ----------------------------------------------------------------------------------------- dynamics
struct Compressor : Fx
{
    juce::dsp::Compressor<float> c;
    Compressor() : Fx ("Compressor", false)
    {
        addFloat ("thr", "Threshold", -60.0f, 0.0f, -18.0f, "dB");
        addFloat ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f);
        addFloat ("att", "Attack", 0.1f, 200.0f, 10.0f, "ms", 20.0f);
        addFloat ("rel", "Release", 5.0f, 2000.0f, 150.0f, "ms", 200.0f);
        addFloat ("gain", "Makeup", 0.0f, 24.0f, 0.0f, "dB");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { c.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        c.setThreshold (v (0)); c.setRatio (v (1)); c.setAttack (v (2)); c.setRelease (v (3));
        juce::dsp::AudioBlock<float> blk (b);
        c.process (juce::dsp::ProcessContextReplacing<float> (blk));
        b.applyGain (dbg (v (4)));
        mixDry (b, dry, v (5) / 100.0f);
    }
};

struct Gate : Fx
{
    juce::dsp::NoiseGate<float> gt;
    Gate() : Fx ("Gate", false)
    {
        addFloat ("thr", "Threshold", -80.0f, 0.0f, -40.0f, "dB");
        addFloat ("ratio", "Ratio", 1.0f, 50.0f, 10.0f, ":1", 8.0f);
        addFloat ("att", "Attack", 0.1f, 100.0f, 1.0f, "ms", 10.0f);
        addFloat ("rel", "Release", 5.0f, 2000.0f, 100.0f, "ms", 200.0f);
    }
    void prepare() override { gt.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        gt.setThreshold (v (0)); gt.setRatio (v (1)); gt.setAttack (v (2)); gt.setRelease (v (3));
        juce::dsp::AudioBlock<float> blk (b);
        gt.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
};

struct TransientMaster : Fx
{
    float fast = 0, slow = 0, sus = 0;
    TransientMaster() : Fx ("Transient Master", false)
    {
        addFloat ("attack", "Attack", -100.0f, 100.0f, 0.0f, "%");
        addFloat ("sustain", "Sustain", -100.0f, 100.0f, 0.0f, "%");
        addFloat ("gain", "Gain", -24.0f, 24.0f, 0.0f, "dB");
    }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const float a = v (0) / 100.0f, s = v (1) / 100.0f, g = dbg (v (2));
        const float fc = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
        const float sc = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
        const float rc = 1.0f - std::exp (-1.0f / (0.3f * (float) sampleRate));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float x = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) x = juce::jmax (x, std::abs (b.getSample (c, i)));
            fast += (x - fast) * fc;
            slow += (x - slow) * sc;
            sus += (x - sus) * rc;
            float trans = juce::jmax (0.0f, fast - slow) / (slow + 1e-4f);
            float tail = juce::jmax (0.0f, sus - fast) / (sus + 1e-4f);
            float gain = juce::jlimit (0.05f, 8.0f, 1.0f + a * juce::jmin (trans, 4.0f) + s * tail * 2.0f) * g;
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, b.getSample (c, i) * gain);
        }
    }
};

struct Limiter : Fx
{
    juce::dsp::Limiter<float> lim;
    bool maximizer;
    explicit Limiter (bool maxi) : Fx (maxi ? "Maximizer" : "Limiter", false), maximizer (maxi)
    {
        if (maxi) addFloat ("gain", "Gain", 0.0f, 24.0f, 6.0f, "dB");
        else      addFloat ("thr", "Threshold", -24.0f, 0.0f, 0.0f, "dB");
        addFloat ("ceil", "Ceiling", -12.0f, 0.0f, -0.3f, "dB");
        addFloat ("rel", "Release", 1.0f, 1000.0f, 80.0f, "ms", 100.0f);
    }
    void prepare() override { lim.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        float pre = maximizer ? dbg (v (0)) : dbg (-v (0));
        b.applyGain (pre);
        lim.setThreshold (v (1));
        lim.setRelease (v (2));
        juce::dsp::AudioBlock<float> blk (b);
        lim.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
};

// ----------------------------------------------------------------------------------------- tone
struct EQ : Fx
{
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lc, lo, mid, hi;
    EQ() : Fx ("EQ", false)
    {
        addFloat ("lc", "Low Cut", 10.0f, 1000.0f, 10.0f, "Hz", 100.0f);
        addFloat ("lf", "Low Freq", 30.0f, 500.0f, 100.0f, "Hz", 120.0f);
        addFloat ("lg", "Low Gain", -18.0f, 18.0f, 0.0f, "dB");
        addFloat ("mf", "Mid Freq", 200.0f, 8000.0f, 1000.0f, "Hz", 1000.0f);
        addFloat ("mg", "Mid Gain", -18.0f, 18.0f, 0.0f, "dB");
        addFloat ("mq", "Mid Q", 0.2f, 8.0f, 0.8f, "", 1.0f);
        addFloat ("hf", "High Freq", 1000.0f, 18000.0f, 8000.0f, "Hz", 5000.0f);
        addFloat ("hg", "High Gain", -18.0f, 18.0f, 0.0f, "dB");
    }
    void prepare() override { for (auto* f : { &lc, &lo, &mid, &hi }) f->prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        using C = juce::dsp::IIR::Coefficients<float>;
        *lc.state  = *C::makeHighPass (sampleRate, v (0));
        *lo.state  = *C::makeLowShelf (sampleRate, v (1), 0.7f, dbg (v (2)));
        *mid.state = *C::makePeakFilter (sampleRate, v (3), v (5), dbg (v (4)));
        *hi.state  = *C::makeHighShelf (sampleRate, v (6), 0.7f, dbg (v (7)));
        juce::dsp::AudioBlock<float> blk (b);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        if (v (0) > 11.0f) lc.process (ctx);
        lo.process (ctx); mid.process (ctx); hi.process (ctx);
    }
};

struct Filter : Fx
{
    juce::dsp::StateVariableTPTFilter<float> f;
    Filter() : Fx ("Filter", false)
    {
        addChoice ("type", "Type", { "Low Pass", "Band Pass", "High Pass" }, 0);
        addFloat ("cut", "Cutoff", 20.0f, 20000.0f, 2000.0f, "Hz", 1000.0f);
        addFloat ("res", "Resonance", 0.0f, 100.0f, 20.0f, "%");
        addFloat ("drive", "Drive", 0.0f, 100.0f, 0.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { f.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        const int t = (int) v (0);
        f.setType (t == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass : t == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass : juce::dsp::StateVariableTPTFilterType::highpass);
        f.setCutoffFrequency (v (1));
        f.setResonance (0.7f + v (2) / 100.0f * 9.0f);
        float drive = 1.0f + v (3) / 100.0f * 10.0f;
        if (drive > 1.01f) for (int c = 0; c < b.getNumChannels(); ++c) { auto* d = b.getWritePointer (c); for (int i = 0; i < b.getNumSamples(); ++i) d[i] = std::tanh (d[i] * drive) / std::tanh (drive); }
        juce::dsp::AudioBlock<float> blk (b);
        f.process (juce::dsp::ProcessContextReplacing<float> (blk));
        mixDry (b, dry, v (4) / 100.0f);
    }
};

struct Cabinet : Fx
{
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> hp, lp, pk1, pk2;
    Cabinet() : Fx ("Cabinet", false)
    {
        addChoice ("cab", "Cabinet", { "1x12 Combo", "2x12 Open", "4x12 Closed", "Bass 8x10" }, 2);
        addFloat ("size", "Size", 0.0f, 100.0f, 50.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { for (auto* f : { &hp, &lp, &pk1, &pk2 }) f->prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        using C = juce::dsp::IIR::Coefficients<float>;
        const int cab = (int) v (0);
        const float sz = v (1) / 100.0f;
        const float hpF[] = { 110, 90, 75, 40 }, lpF[] = { 4200, 5200, 3800, 3000 }, p1[] = { 1800, 2200, 1500, 800 }, p2[] = { 120, 140, 110, 70 };
        *hp.state = *C::makeHighPass (sampleRate, hpF[cab] * (1.5f - sz));
        *lp.state = *C::makeLowPass (sampleRate, lpF[cab] * (1.3f - sz * 0.5f), 0.9f);
        *pk1.state = *C::makePeakFilter (sampleRate, p1[cab], 1.2f, dbg (5.0f));
        *pk2.state = *C::makePeakFilter (sampleRate, p2[cab] * (1.2f - sz * 0.4f), 1.0f, dbg (3.0f + sz * 3.0f));
        juce::dsp::AudioBlock<float> blk (b);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        hp.process (ctx); lp.process (ctx); pk1.process (ctx); pk2.process (ctx);
        mixDry (b, dry, v (2) / 100.0f);
    }
};

// ----------------------------------------------------------------------------------------- modulation
struct ChorusFx : Fx
{
    juce::dsp::Chorus<float> ch;
    bool flanger;
    explicit ChorusFx (bool fl) : Fx (fl ? "Flanger" : "Chorus", false), flanger (fl)
    {
        addFloat ("rate", "Rate", 0.01f, 10.0f, fl ? 0.2f : 0.8f, "Hz", 1.0f);
        addFloat ("depth", "Depth", 0.0f, 100.0f, fl ? 70.0f : 35.0f, "%");
        addFloat ("delay", "Delay", 1.0f, 30.0f, fl ? 2.0f : 8.0f, "ms");
        addFloat ("fb", "Feedback", -95.0f, 95.0f, fl ? 60.0f : 0.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%");
    }
    void prepare() override { ch.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        ch.setRate (v (0)); ch.setDepth (v (1) / 100.0f); ch.setCentreDelay (v (2));
        ch.setFeedback (v (3) / 100.0f); ch.setMix (v (4) / 100.0f);
        juce::dsp::AudioBlock<float> blk (b);
        ch.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
};

struct PhaserFx : Fx
{
    juce::dsp::Phaser<float> ph;
    PhaserFx() : Fx ("Phaser", false)
    {
        addFloat ("rate", "Rate", 0.01f, 10.0f, 0.5f, "Hz", 1.0f);
        addFloat ("depth", "Depth", 0.0f, 100.0f, 60.0f, "%");
        addFloat ("freq", "Frequency", 100.0f, 5000.0f, 800.0f, "Hz", 800.0f);
        addFloat ("fb", "Feedback", -95.0f, 95.0f, 40.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%");
    }
    void prepare() override { ph.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        ph.setRate (v (0)); ph.setDepth (v (1) / 100.0f); ph.setCentreFrequency (v (2)); ph.setFeedback (v (3) / 100.0f); ph.setMix (v (4) / 100.0f);
        juce::dsp::AudioBlock<float> blk (b);
        ph.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
};

// Delay-line helper
struct DelayLine
{
    std::vector<float> d; int w = 0;
    void init (int n) { d.assign ((size_t) juce::jmax (16, n), 0.0f); w = 0; }
    void push (float x) { d[(size_t) w] = x; w = (w + 1) % (int) d.size(); }
    float read (double delay) const
    {
        double r = w - 1 - delay;
        while (r < 0) r += (double) d.size();
        int i0 = (int) r; float f = (float) (r - i0);
        int i1 = (i0 + 1) % (int) d.size();
        return d[(size_t) i0] * (1 - f) + d[(size_t) i1] * f;
    }
};

struct FMFx : Fx
{
    DelayLine dl[2]; double ph = 0;
    FMFx() : Fx ("FM", false)
    {
        addFloat ("freq", "Frequency", 0.1f, 2000.0f, 40.0f, "Hz", 100.0f);
        addFloat ("amt", "Amount", 0.0f, 100.0f, 30.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { for (auto& d : dl) d.init ((int) (sampleRate * 0.02)); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const float depth = v (1) / 100.0f * (float) sampleRate * 0.004f, mix = v (2) / 100.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            ph += v (0) / sampleRate; ph -= std::floor (ph);
            double m = (1.0 + std::sin (ph * juce::MathConstants<double>::twoPi)) * depth + 1.0;
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                float x = b.getSample (c, i);
                dl[c].push (x);
                b.setSample (c, i, x + (dl[c].read (m) - x) * mix);
            }
        }
    }
};

// Hilbert-transform frequency shifter (Olli Niemitalo allpass pairs)
struct FreqShifter : Fx
{
    struct AP { float a; float x1 = 0, x2 = 0, y1 = 0, y2 = 0; float run (float x) { float y = a * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
    struct Hilbert
    {
        std::array<AP, 4> p1 { AP { 0.6923878f * 0.6923878f }, AP { 0.9360654322959f * 0.9360654322959f }, AP { 0.9882295226860f * 0.9882295226860f }, AP { 0.9987488452737f * 0.9987488452737f } };
        std::array<AP, 4> p2 { AP { 0.4021921162426f * 0.4021921162426f }, AP { 0.8561710882420f * 0.8561710882420f }, AP { 0.9722909545651f * 0.9722909545651f }, AP { 0.9952884791278f * 0.9952884791278f } };
        float delayed = 0;
        void run (float x, float& re, float& im)
        {
            float a = x, b = x;
            for (auto& p : p1) a = p.run (a);
            for (auto& p : p2) b = p.run (b);
            re = delayed; delayed = a; im = b;
        }
    };
    std::array<Hilbert, 2> h; double ph = 0;
    FreqShifter() : Fx ("Freq Shifter", false)
    {
        addFloat ("shift", "Shift", -2000.0f, 2000.0f, 100.0f, "Hz");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const float mix = v (1) / 100.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            ph += v (0) / sampleRate; ph -= std::floor (ph);
            float c = std::cos ((float) (ph * juce::MathConstants<double>::twoPi)), s = std::sin ((float) (ph * juce::MathConstants<double>::twoPi));
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                float x = b.getSample (ch, i), re, im;
                h[(size_t) ch].run (x, re, im);
                float y = re * c - im * s;
                b.setSample (ch, i, x + (y - x) * mix);
            }
        }
    }
};

// ----------------------------------------------------------------------------------------- space
struct ReverbFx : Fx
{
    juce::Reverb rv; DelayLine pre[2];
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowCut;
    float defSize;
    ReverbFx (const juce::String& n, float size, float damp, float predelay) : Fx (n, false), defSize (size)
    {
        addFloat ("size", "Size", 0.0f, 100.0f, size, "%");
        addFloat ("damp", "Damping", 0.0f, 100.0f, damp, "%");
        addFloat ("width", "Width", 0.0f, 100.0f, 100.0f, "%");
        addFloat ("pre", "Pre-Delay", 0.0f, 200.0f, predelay, "ms");
        addFloat ("lowcut", "Low Cut", 20.0f, 1000.0f, 20.0f, "Hz", 150.0f);
        addFloat ("freeze", "Freeze", 0.0f, 100.0f, 0.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%");
    }
    void prepare() override { rv.setSampleRate (sampleRate); for (auto& p : pre) p.init ((int) (sampleRate * 0.25)); lowCut.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        juce::Reverb::Parameters p;
        p.roomSize = v (0) / 100.0f; p.damping = v (1) / 100.0f; p.width = v (2) / 100.0f;
        p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = v (5) / 100.0f;
        rv.setParameters (p);
        const double pd = v (3) / 1000.0 * sampleRate;
        if (pd > 1)
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i) { pre[c].push (b.getSample (c, i)); b.setSample (c, i, pre[c].read (pd)); }
        if (b.getNumChannels() > 1) rv.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        else rv.processMono (b.getWritePointer (0), b.getNumSamples());
        if (v (4) > 21.0f)
        {
            *lowCut.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, v (4));
            juce::dsp::AudioBlock<float> blk (b);
            lowCut.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
        mixDry (b, dry, v (6) / 100.0f);
    }
};

struct IceFx : Fx
{
    juce::Reverb rv; std::array<juce::dsp::StateVariableTPTFilter<float>, 3> bp;
    IceFx() : Fx ("Ice", false)
    {
        addFloat ("color", "Color", 200.0f, 8000.0f, 1500.0f, "Hz", 1500.0f);
        addFloat ("size", "Size", 0.0f, 100.0f, 80.0f, "%");
        addFloat ("res", "Resonance", 0.0f, 100.0f, 60.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 35.0f, "%");
    }
    void prepare() override { rv.setSampleRate (sampleRate); for (auto& f : bp) { f.prepare (spec()); f.setType (juce::dsp::StateVariableTPTFilterType::bandpass); } }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        juce::Reverb::Parameters p; p.roomSize = v (1) / 100.0f; p.damping = 0.2f; p.wetLevel = 1; p.dryLevel = 0; p.width = 1;
        rv.setParameters (p);
        juce::AudioBuffer<float> tmp; tmp.makeCopyOf (b);
        b.clear();
        const float ratios[] = { 1.0f, 1.5f, 2.25f };
        for (size_t k = 0; k < bp.size(); ++k)
        {
            juce::AudioBuffer<float> band; band.makeCopyOf (tmp);
            bp[k].setCutoffFrequency (juce::jmin (18000.0f, v (0) * ratios[k]));
            bp[k].setResonance (1.0f + v (2) / 100.0f * 10.0f);
            juce::dsp::AudioBlock<float> blk (band);
            bp[k].process (juce::dsp::ProcessContextReplacing<float> (blk));
            for (int c = 0; c < b.getNumChannels(); ++c) b.addFrom (c, 0, band, c, 0, b.getNumSamples(), 0.6f);
        }
        if (b.getNumChannels() > 1) rv.processStereo (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        mixDry (b, dry, v (3) / 100.0f);
    }
};

// ----------------------------------------------------------------------------------------- delays
inline double syncedDelaySeconds (int idx)
{
    // 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2, 1/1
    const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, 11, idx)] * 60.0 / juce::jmax (20.0, hostBpm.load());
}
const juce::StringArray syncChoices { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" };

struct BeatDelay : Fx
{
    DelayLine dl[2]; float lpS[2] { 0, 0 };
    BeatDelay() : Fx ("Beat Delay", false)
    {
        addChoice ("time", "Time", syncChoices, 5);
        addFloat ("fb", "Feedback", 0.0f, 100.0f, 40.0f, "%");
        addFloat ("tone", "Tone", 500.0f, 18000.0f, 6000.0f, "Hz", 3000.0f);
        addChoice ("pp", "Ping Pong", { "Off", "On" }, 1);
        addFloat ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%");
    }
    void prepare() override { for (auto& d : dl) d.init ((int) (sampleRate * 6.0)); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const double t = juce::jmin (5.9, syncedDelaySeconds ((int) v (0))) * sampleRate;
        const float fb = v (1) / 100.0f * 0.95f, mix = v (4) / 100.0f;
        const float lc = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * v (2) / (float) sampleRate);
        const bool pp = v (3) > 0.5f && b.getNumChannels() > 1;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float inL = b.getSample (0, i), inR = b.getNumChannels() > 1 ? b.getSample (1, i) : inL;
            float dL = dl[0].read (t), dR = dl[1].read (t);
            lpS[0] += (dL - lpS[0]) * lc; lpS[1] += (dR - lpS[1]) * lc;
            if (pp) { dl[0].push ((inL + inR) * 0.5f + lpS[1] * fb); dl[1].push (lpS[0] * fb); }
            else    { dl[0].push (inL + lpS[0] * fb); dl[1].push (inR + lpS[1] * fb); }
            b.setSample (0, i, inL + lpS[0] * mix);
            if (b.getNumChannels() > 1) b.setSample (1, i, inR + lpS[1] * mix);
        }
    }
};

// Granular pitch shifting delay: two crossfading read heads
struct GrainDelay : Fx
{
    DelayLine dl[2]; double grainPhase = 0; bool stretch;
    explicit GrainDelay (bool str) : Fx (str ? "Grain Stretch" : "Grain Delay", false), stretch (str)
    {
        if (! str) { addChoice ("time", "Time", syncChoices, 5); addFloat ("fb", "Feedback", 0.0f, 100.0f, 30.0f, "%"); }
        else       { addFloat ("speed", "Speed", 10.0f, 200.0f, 100.0f, "%"); }
        addFloat ("pitch", "Pitch", -24.0f, 24.0f, str ? 0.0f : 12.0f, "st");
        addFloat ("grain", "Grain", 10.0f, 200.0f, 60.0f, "ms");
        addFloat ("mix", "Mix", 0.0f, 100.0f, str ? 100.0f : 35.0f, "%");
    }
    void prepare() override { for (auto& d : dl) d.init ((int) (sampleRate * 6.0)); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        int k = 0;
        double base = 0; float fb = 0; double speed = 1.0;
        if (! stretch) { base = juce::jmin (5.0, syncedDelaySeconds ((int) v (k++))) * sampleRate; fb = v (k++) / 100.0f * 0.9f; }
        else speed = v (k++) / 100.0;
        const double ratio = std::pow (2.0, v (k++) / 12.0) * speed;
        const double grain = v (k++) / 1000.0 * sampleRate;
        const float mix = v (k) / 100.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            grainPhase += (1.0 - ratio) / grain;
            grainPhase -= std::floor (grainPhase);
            double ph2 = std::fmod (grainPhase + 0.5, 1.0);
            float w1 = std::sin ((float) (grainPhase * juce::MathConstants<double>::pi)), w2 = std::sin ((float) (ph2 * juce::MathConstants<double>::pi));
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                float x = b.getSample (c, i);
                float y = dl[c].read (base + grainPhase * grain + 1) * w1 + dl[c].read (base + ph2 * grain + 1) * w2;
                dl[c].push (x + y * fb);
                b.setSample (c, i, x + (y - x) * mix);
            }
        }
    }
};

struct Resochord : Fx
{
    std::array<DelayLine, 4> combs[2]; std::array<float, 4> lp[2] {};
    Resochord() : Fx ("Resochord", false)
    {
        addChoice ("chord", "Chord", { "Major", "Minor", "Sus4", "Maj7", "Min7", "Fifths" }, 1);
        addChoice ("root", "Root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9);
        addFloat ("decay", "Decay", 0.0f, 99.0f, 85.0f, "%");
        addFloat ("bright", "Brightness", 0.0f, 100.0f, 50.0f, "%");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 40.0f, "%");
    }
    void prepare() override { for (auto& ch : combs) for (auto& d : ch) d.init ((int) (sampleRate / 20.0)); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        static const int chords[6][4] = { { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 7, 12, 19 } };
        const int ch = (int) v (0), root = (int) v (1);
        const float fb = v (2) / 100.0f, br = 0.2f + v (3) / 100.0f * 0.8f, mix = v (4) / 100.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                float x = b.getSample (c, i), y = 0;
                for (int k = 0; k < 4; ++k)
                {
                    double f = juce::MidiMessage::getMidiNoteInHertz (48 + root + chords[ch][k]);
                    float d = combs[c][(size_t) k].read (sampleRate / f - 1.0);
                    lp[c][(size_t) k] += (d - lp[c][(size_t) k]) * br;
                    combs[c][(size_t) k].push (x * 0.25f + lp[c][(size_t) k] * fb);
                    y += d;
                }
                b.setSample (c, i, x + (y - x) * mix);
            }
    }
};

// ----------------------------------------------------------------------------------------- drive
struct Distortion : Fx
{
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> tone;
    Distortion() : Fx ("Distortion", false)
    {
        addFloat ("drive", "Drive", 0.0f, 100.0f, 40.0f, "%");
        addFloat ("tone", "Tone", 500.0f, 16000.0f, 5000.0f, "Hz", 3000.0f);
        addFloat ("out", "Output", -24.0f, 6.0f, -6.0f, "dB");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { tone.prepare (spec()); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        float drive = 1.0f + v (0) / 100.0f * 50.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) { auto* d = b.getWritePointer (c); for (int i = 0; i < b.getNumSamples(); ++i) { float x = d[i] * drive; d[i] = x / (1.0f + std::abs (x)); } }
        *tone.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, v (1));
        juce::dsp::AudioBlock<float> blk (b);
        tone.process (juce::dsp::ProcessContextReplacing<float> (blk));
        b.applyGain (dbg (v (2)));
        mixDry (b, dry, v (3) / 100.0f);
    }
};

struct Lofi : Fx
{
    float hold[2] { 0, 0 }; double cnt = 0; juce::dsp::StateVariableTPTFilter<float> f;
    Lofi() : Fx ("Lofi", false)
    {
        addFloat ("bits", "Bits", 2.0f, 16.0f, 10.0f, "bit");
        addFloat ("rate", "Sample Rate", 1000.0f, 44100.0f, 16000.0f, "Hz", 12000.0f);
        addFloat ("filter", "Filter", 500.0f, 20000.0f, 9000.0f, "Hz", 4000.0f);
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void prepare() override { f.prepare (spec()); f.setType (juce::dsp::StateVariableTPTFilterType::lowpass); }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        const float steps = std::pow (2.0f, v (0) - 1.0f);
        const double step = sampleRate / v (1);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            cnt += 1.0;
            bool take = cnt >= step;
            if (take) cnt -= step;
            for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
            {
                if (take) hold[c] = std::round (b.getSample (c, i) * steps) / steps;
                b.setSample (c, i, hold[c]);
            }
        }
        f.setCutoffFrequency (v (2));
        juce::dsp::AudioBlock<float> blk (b);
        f.process (juce::dsp::ProcessContextReplacing<float> (blk));
        mixDry (b, dry, v (3) / 100.0f);
    }
};

struct Saturator : Fx
{
    Saturator() : Fx ("Saturator", false)
    {
        addChoice ("mode", "Mode", { "Tape", "Tube", "Hard" }, 0);
        addFloat ("drive", "Drive", 0.0f, 24.0f, 6.0f, "dB");
        addFloat ("out", "Output", -24.0f, 6.0f, -3.0f, "dB");
        addFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
    }
    void process (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        keepDry (b);
        const int m = (int) v (0); const float drive = dbg (v (1)), out = dbg (v (2));
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i] * drive;
                if (m == 0) x = std::tanh (x);
                else if (m == 1) x = x >= 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x)) * 0.8f;
                else x = juce::jlimit (-1.0f, 1.0f, x);
                d[i] = x * out;
            }
        }
        mixDry (b, dry, v (3) / 100.0f);
    }
};
} // namespace

std::unique_ptr<InternalProcessor> createInternalEffect (const juce::String& n)
{
    if (n == "Compressor") return std::make_unique<Compressor>();
    if (n == "Gate") return std::make_unique<Gate>();
    if (n == "Transient Master") return std::make_unique<TransientMaster>();
    if (n == "Limiter") return std::make_unique<Limiter> (false);
    if (n == "Maximizer") return std::make_unique<Limiter> (true);
    if (n == "EQ") return std::make_unique<EQ>();
    if (n == "Filter") return std::make_unique<Filter>();
    if (n == "Cabinet") return std::make_unique<Cabinet>();
    if (n == "Chorus") return std::make_unique<ChorusFx> (false);
    if (n == "Flanger") return std::make_unique<ChorusFx> (true);
    if (n == "FM") return std::make_unique<FMFx>();
    if (n == "Freq Shifter") return std::make_unique<FreqShifter>();
    if (n == "Phaser") return std::make_unique<PhaserFx>();
    if (n == "Ice") return std::make_unique<IceFx>();
    if (n == "Metaverb") return std::make_unique<ReverbFx> ("Metaverb", 85.0f, 30.0f, 0.0f);
    if (n == "Reflex") return std::make_unique<ReverbFx> ("Reflex", 25.0f, 60.0f, 8.0f);
    if (n == "Reverb (Legacy)") return std::make_unique<ReverbFx> ("Reverb (Legacy)", 60.0f, 50.0f, 0.0f);
    if (n == "Reverb") return std::make_unique<ReverbFx> ("Reverb", 70.0f, 40.0f, 20.0f);
    if (n == "Beat Delay") return std::make_unique<BeatDelay>();
    if (n == "Grain Delay") return std::make_unique<GrainDelay> (false);
    if (n == "Grain Stretch") return std::make_unique<GrainDelay> (true);
    if (n == "Resochord") return std::make_unique<Resochord>();
    if (n == "Distortion") return std::make_unique<Distortion>();
    if (n == "Lofi") return std::make_unique<Lofi>();
    if (n == "Saturator") return std::make_unique<Saturator>();
    return {};
}
} // namespace gb
