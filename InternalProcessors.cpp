#include "InternalProcessors.h"

namespace gb
{
extern juce::File projectSampleFolder;
std::atomic<double> hostBpm { 120.0 };

SamplePtr SampleCache::load (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    auto key = f.getFullPathName();
    if (auto it = cache.find (key); it != cache.end())
        if (auto s = it->second.lock())
            return s;

    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0)
        return {};

    auto d = std::make_shared<SampleData>();
    auto len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) r->sampleRate * 60 * 20);
    d->buffer.setSize (juce::jmax (1, juce::jmin (2, (int) r->numChannels)), len);
    r->read (&d->buffer, 0, len, 0, true, d->buffer.getNumChannels() > 1);
    d->sampleRate = r->sampleRate;
    d->file = f;
    cache[key] = d;
    return d;
}

// ------------------------------------------------------------------------------------------
InternalProcessor::InternalProcessor (const juce::String& n, bool inst)
    : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      name (n), instrument (inst)
{
}

int InternalProcessor::addFloat (const juce::String& id, const juce::String& label, float mn, float mx, float def, const juce::String& suffix, float skewCentre)
{
    juce::NormalisableRange<float> r (mn, mx);
    if (skewCentre > mn && skewCentre < mx) r.setSkewForCentre (skewCentre);
    auto* p = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, label, r, def,
                                             juce::AudioParameterFloatAttributes().withLabel (suffix)
                                                 .withStringFromValueFunction ([suffix] (float x, int)
                                                 {
                                                     auto a = std::abs (x);
                                                     auto s = a >= 1000 ? juce::String (x / 1000.0f, 1) + "k" : a >= 100 ? juce::String (juce::roundToInt (x)) : juce::String (x, a >= 10 ? 1 : 2);
                                                     return s + (suffix.isEmpty() ? "" : " " + suffix);
                                                 }));
    addParameter (p);
    params.push_back (p);
    return (int) params.size() - 1;
}

int InternalProcessor::addChoice (const juce::String& id, const juce::String& label, const juce::StringArray& choices, int def)
{
    auto* p = new juce::AudioParameterChoice (juce::ParameterID { id, 1 }, label, choices, def);
    addParameter (p);
    params.push_back (p);
    return (int) params.size() - 1;
}

float InternalProcessor::v (int i) const
{
    auto* p = params[(size_t) i];
    return p->convertFrom0to1 (p->getValue());
}

void InternalProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    juce::XmlElement x ("STATE");
    for (auto* p : params) x.setAttribute (p->paramID, p->getValue());
    saveExtra (x);
    copyXmlToBinary (x, mb);
}

void InternalProcessor::setStateInformation (const void* d, int n)
{
    if (auto x = getXmlFromBinary (d, n))
    {
        for (auto* p : params)
            if (x->hasAttribute (p->paramID))
                p->setValueNotifyingHost ((float) x->getDoubleAttribute (p->paramID));
        loadExtra (*x);
    }
}

// ------------------------------------------------------------------------------------------
SamplerProcessor::SamplerProcessor() : InternalProcessor ("Sampler", true)
{
    // the original parameters (ids kept for old projects)
    addFloat ("tune", "Tune", -36.0f, 36.0f, 0.0f, "st");
    addFloat ("volume", "Volume", -48.0f, 12.0f, 0.0f, "dB");
    addFloat ("start", "Start", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("end", "End", 0.0f, 100.0f, 100.0f, "%");
    addChoice ("mode", "Type", { "One-shot", "AHD", "ADSR" }, 0);
    addFloat ("attack", "Attack", 0.0f, 2000.0f, 0.0f, "ms", 100.0f);
    addFloat ("hold", "Hold", 0.0f, 5000.0f, 5000.0f, "ms", 500.0f);
    addFloat ("decay", "Decay", 1.0f, 5000.0f, 500.0f, "ms", 400.0f);
    addFloat ("sustain", "Sustain", 0.0f, 100.0f, 100.0f, "%");
    addFloat ("release", "Release", 1.0f, 5000.0f, 50.0f, "ms", 300.0f);
    addChoice ("reverse", "Reverse", { "Off", "On" }, 0);
    addChoice ("loop", "Loop", { "Off", "On" }, 0);
    addFloat ("cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f, "Hz", 1000.0f);
    addFloat ("reso", "Resonance", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("velsens", "Volume", 0.0f, 100.0f, 70.0f, "%");          // Velocity -> Volume
    // Voice settings / engine
    addChoice ("poly", "Polyphony", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" }, 7);
    addFloat ("pbend", "Pitchbend", 0.0f, 24.0f, 2.0f, "st");
    addChoice ("engine", "Mode", { "Standard", "MP60", "S1200", "S1200 L", "S1200 LM", "S1200 HM", "S1200 H" }, 0);
    // FX / filter
    addFloat ("comp", "Comp", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("drive", "Drive", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("srred", "SR", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("bits", "Bits", 0.0f, 100.0f, 0.0f, "%");
    addChoice ("ftype", "Filter", { "Off", "LP2", "BP2", "HP2", "EQ" }, 0);
    addFloat ("eqlow", "Low", -18.0f, 18.0f, 0.0f, "dB");
    addFloat ("eqmid", "Mid", -18.0f, 18.0f, 0.0f, "dB");
    addFloat ("eqhigh", "High", -18.0f, 18.0f, 0.0f, "dB");
    // modulation envelope
    addFloat ("menva", "Attack", 0.0f, 2000.0f, 0.0f, "ms", 100.0f);
    addFloat ("menvh", "Hold", 0.0f, 2000.0f, 50.0f, "ms", 200.0f);
    addFloat ("menvd", "Decay", 1.0f, 5000.0f, 300.0f, "ms", 400.0f);
    addFloat ("menvpitch", "Pitch", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("menvcut", "Cutoff", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("menvdrive", "Drive", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("menvpan", "Pan", -100.0f, 100.0f, 0.0f, "%");
    // LFO
    addChoice ("lfotype", "Type", { "Sine", "Tri", "Rect", "Saw", "Random" }, 0);
    addFloat ("lfospeed", "Speed", 0.05f, 40.0f, 4.0f, "Hz", 3.0f);
    addFloat ("lfophase", "Phase", 0.0f, 360.0f, 0.0f, "deg");
    addChoice ("lfosync", "Sync", { "Free", "Retrig", "Lock" }, 0);
    addFloat ("lfopitch", "Pitch", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("lfocut", "Cutoff", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("lfodrive", "Drive", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("lfopan", "Pan", -100.0f, 100.0f, 0.0f, "%");
    // velocity / modwheel destinations
    addFloat ("velstart", "Start", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("veldecay", "Decay", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("velcut", "Cutoff", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("mwstart", "Start", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("mwcut", "Cutoff", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("mwlfo", "LFO Depth", -100.0f, 100.0f, 0.0f, "%");
    addFloat ("mwpan", "Pan", -100.0f, 100.0f, 0.0f, "%");
    // loop / zone (edited in the sample editor)
    addFloat ("loopstart", "Loop Start", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("loopend", "Loop End", 0.0f, 100.0f, 100.0f, "%");
    addFloat ("xfade", "X-Fade", 0.0f, 100.0f, 0.0f, "%");
    addFloat ("keylo", "Key Low", 0.0f, 127.0f, 0.0f);
    addFloat ("keyhi", "Key High", 0.0f, 127.0f, 127.0f);
    addFloat ("zonegain", "Gain", -36.0f, 12.0f, 0.0f, "dB");
    addFloat ("zonepan", "Pan", -100.0f, 100.0f, 0.0f, "%");
}

std::vector<InternalProcessor::UiPage> SamplerProcessor::uiPages() const
{
    std::vector<UiPage> p;
    p.push_back ({ "Voice Settings / Engine", { { 0, "Voice Settings" }, { 4, "Engine" } }, { { 0, polyphony }, { 3, pitchbend }, { 4, engine } } });
    UiPage pe { "Pitch / Envelope", { { 0, "Pitch / Gate" } }, { { 0, tune }, { 1, start }, { 2, reverse }, { 3, mode } } };
    int md = (int) v (mode);
    if (md == 1) { pe.sections.push_back ({ 4, "Envelope" }); pe.items.insert (pe.items.end(), { { 4, attack }, { 5, hold }, { 6, decay } }); }
    if (md == 2) { pe.sections.push_back ({ 4, "Envelope" }); pe.items.insert (pe.items.end(), { { 4, attack }, { 5, decay }, { 6, sustain }, { 7, release } }); }
    p.push_back (pe);
    UiPage fx { "FX / Filter", { { 0, "FX" }, { 4, "Filter" } }, { { 0, comp }, { 1, drive }, { 2, srRed }, { 3, bits }, { 4, ftype } } };
    int ft = (int) v (ftype);
    if (ft >= 1 && ft <= 3) fx.items.insert (fx.items.end(), { { 5, cutoff }, { 6, reso } });
    if (ft == 4) fx.items.insert (fx.items.end(), { { 5, eqLow }, { 6, eqMid }, { 7, eqHigh } });
    p.push_back (fx);
    p.push_back ({ "Modulation", { { 0, "Modulation Envelope" }, { 4, "Destination" } },
                   { { 0, menvA }, { 1, menvH }, { 2, menvD }, { 4, menvPitch }, { 5, menvCutoff }, { 6, menvDrive }, { 7, menvPan } } });
    p.push_back ({ "LFO", { { 0, "LFO" }, { 4, "Destination" } },
                   { { 0, lfoType }, { 1, lfoSpeed }, { 2, lfoPhase }, { 3, lfoSync }, { 4, lfoPitch }, { 5, lfoCutoff }, { 6, lfoDrive }, { 7, lfoPan } } });
    p.push_back ({ "Velocity / Modwheel", { { 0, "Velocity Destination" }, { 4, "Modwheel Destination" } },
                   { { 0, velStart }, { 1, velDecay }, { 2, velCutoff }, { 3, velSens }, { 4, mwStart }, { 5, mwCutoff }, { 6, mwLfo }, { 7, mwPan } } });
    return p;
}

void SamplerProcessor::setSample (SamplePtr s)
{
    const juce::ScopedLock sl (getCallbackLock());
    sample = std::move (s);
    for (auto& vc : voices) vc.active = false;
}

void SamplerProcessor::setRegion (float s, float e)
{
    setValue (start, s * 100.0f);
    setValue (end, e * 100.0f);
}

void SamplerProcessor::prepare()
{
    for (auto& vc : voices)
    {
        vc.filt.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 });
        vc.filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        vc.active = false;
    }
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, blockSize), 2 };
    for (auto* f : { &eqLo, &eqMi, &eqHi, &engineLp }) f->prepare (spec);
    lastEngine = -1;
}

float SamplerProcessor::lfoValue (double ph, int type)
{
    ph -= std::floor (ph);
    switch (type)
    {
        case 0: return std::sin ((float) (ph * juce::MathConstants<double>::twoPi));
        case 1: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
        case 2: return ph < 0.5 ? 1.0f : -1.0f;
        case 3: return (float) (2.0 * ph - 1.0);
        default: return randomLfo;
    }
}

void SamplerProcessor::startVoice (int note, float vel)
{
    if (sample == nullptr) return;
    if (note < (int) v (keyLo) || note > (int) v (keyHi)) return;
    const int maxVoices = juce::jlimit (1, (int) voices.size(), (int) v (polyphony) + 1);
    int active = 0;
    for (auto& vc : voices) active += vc.active ? 1 : 0;
    Voice* best = nullptr;
    if (active < maxVoices) for (auto& vc : voices) if (! vc.active) { best = &vc; break; }
    if (best == nullptr)
    {
        // steal the oldest voice
        for (auto& vc : voices) if (vc.active && (best == nullptr || vc.stageTime > best->stageTime)) best = &vc;
        if (best == nullptr) best = &voices[0];
    }
    auto& vc = *best;
    auto len = (double) sample->buffer.getNumSamples();
    bool rev = v (reverse) > 0.5f;
    double s0 = len * juce::jlimit (0.0f, 1.0f, v (start) / 100.0f);
    double s1 = len * juce::jlimit (0.0f, 1.0f, v (end) / 100.0f);
    if (s1 <= s0 + 1) s1 = juce::jmin (len, s0 + 2);
    // velocity / modwheel -> start offset
    double startMod = (v (velStart) / 100.0f) * vel * 0.5 + (v (mwStart) / 100.0f) * modWheel * 0.5;
    double off = juce::jlimit (0.0, 0.95, std::abs (startMod)) * (s1 - s0);
    vc.active = true;
    vc.note = note;
    vc.vel = vel;
    vc.pos = rev ? s1 - 1 - off : s0 + off;
    vc.inc = std::pow (2.0, (note - rootKey + (double) v (tune)) / 12.0) * sample->sampleRate / sampleRate;
    float sens = v (velSens) / 100.0f;
    vc.gain = juce::Decibels::decibelsToGain (v (volume) + v (zoneGain)) * (1.0f - sens + sens * vel);
    vc.decayScale = juce::jmax (0.05f, 1.0f + v (velDecay) / 100.0f * (vel - 0.5f) * 2.0f);
    vc.cutoffVel = v (velCutoff) / 100.0f * vel;
    vc.env = 0; vc.stageTime = 0; vc.released = false; vc.choking = false; vc.chokeFade = 1.0f;
    vc.lfoPhase = v (lfoPhase) / 360.0;
    vc.filt.reset();
}

void SamplerProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    buf.clear();
    if (sample == nullptr) { playPos = -1.0f; return; }

    const int n = buf.getNumSamples();
    auto& src = sample->buffer;
    const int srcCh = src.getNumChannels();
    const double len = src.getNumSamples();
    const bool rev = v (reverse) > 0.5f, loop = v (loopP) > 0.5f;
    const int md = (int) v (mode);
    const double s0 = len * juce::jlimit (0.0f, 1.0f, v (start) / 100.0f);
    double s1 = len * juce::jlimit (0.0f, 1.0f, v (end) / 100.0f);
    if (s1 <= s0 + 1) s1 = juce::jmin (len, s0 + 2);
    double l0 = juce::jlimit (s0, s1 - 1, len * v (loopStart) / 100.0), l1 = juce::jlimit (l0 + 1, s1, len * v (loopEnd) / 100.0);
    const double xf = (l1 - l0) * 0.5 * v (xfade) / 100.0;
    const double msToS = sampleRate / 1000.0;
    const float att = v (attack), hld = v (hold), dec = v (decay), sus = v (sustain) / 100.0f, rel = v (release);
    const int ft = (int) v (ftype);
    const float baseCut = v (cutoff), res = 0.7f + v (reso) / 100.0f * 9.0f;
    const float ma = v (menvA), mh = v (menvH), mdc = v (menvD);
    const float mPitch = v (menvPitch) / 100.0f, mCut = v (menvCutoff) / 100.0f, mDrive = v (menvDrive) / 100.0f, mPan = v (menvPan) / 100.0f;
    const int lt = (int) v (lfoType), lsync = (int) v (lfoSync);
    double lspeed = v (lfoSpeed);
    if (lsync == 2) { const double divs[] = { 4, 2, 1, 0.5, 0.25, 0.125 }; int k = juce::jlimit (0, 5, (int) (lspeed / 7.0)); lspeed = hostBpm.load() / 60.0 / divs[k]; }
    const float lDepth = juce::jlimit (0.0f, 2.0f, 1.0f + v (mwLfo) / 100.0f * modWheel);
    const float lPitch = v (lfoPitch) / 100.0f * lDepth, lCut = v (lfoCutoff) / 100.0f * lDepth, lDrive = v (lfoDrive) / 100.0f * lDepth, lPan = v (lfoPan) / 100.0f * lDepth;
    const float mwCutAmt = v (mwCutoff) / 100.0f * modWheel, mwPanAmt = v (mwPan) / 100.0f * modWheel;
    const float baseDrive = v (drive) / 100.0f;
    const float zp = v (zonePan) / 100.0f;
    const double bendSemis = bend * v (pitchbend);

    auto it = midi.begin();
    for (int i = 0; i < n; ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= i)
        {
            auto m = (*it).getMessage();
            if (m.isNoteOn()) startVoice (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) { for (auto& vc : voices) if (vc.active && vc.note == m.getNoteNumber()) vc.released = true; }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& vc : voices) if (vc.active) vc.choking = true; }
            else if (m.isPitchWheel()) bend = (m.getPitchWheelValue() - 8192) / 8192.0f;
            else if (m.isController() && m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
            ++it;
        }
        // free-running LFO (+ random S&H)
        freeLfo += lspeed / sampleRate;
        int cyc = (int) std::floor (freeLfo);
        if (cyc != lastLfoCycle) { lastLfoCycle = cyc; randomLfo = rng.nextFloat() * 2.0f - 1.0f; }

        for (auto& vc : voices)
        {
            if (! vc.active) continue;
            const double t = vc.stageTime;
            // amp envelope
            float e = 1.0f;
            if (md == 0) e = att > 0 ? (float) juce::jmin (1.0, t / (att * msToS)) : 1.0f;
            else
            {
                double a = att * msToS, h = hld * msToS, d = dec * msToS * vc.decayScale;
                if (md == 1)
                {
                    if (t < a) e = (float) (t / a);
                    else if (t < a + h) e = 1.0f;
                    else e = (float) juce::jmax (0.0, 1.0 - (t - a - h) / d);
                    if (vc.released) { vc.env = juce::jmax (0.0f, vc.env - (float) (1.0 / (rel * msToS))); e = juce::jmin (e, vc.env); }
                    else vc.env = e;
                    if (t > a + h + d) { vc.active = false; continue; }
                }
                else
                {
                    if (! vc.released)
                    {
                        if (t < a) e = (float) (t / a);
                        else if (t < a + d) e = (float) (1.0 - (1.0 - sus) * (t - a) / d);
                        else e = sus;
                        vc.env = e;
                    }
                    else
                    {
                        vc.env -= (float) (1.0 / (rel * msToS));
                        if (vc.env <= 0) { vc.active = false; continue; }
                        e = vc.env;
                    }
                }
            }
            if (vc.choking) { vc.chokeFade -= (float) (1.0 / (0.005 * sampleRate)); if (vc.chokeFade <= 0) { vc.active = false; continue; } e *= vc.chokeFade; }

            // modulation envelope (AHD)
            double mA = ma * msToS, mH = mh * msToS, mD = mdc * msToS;
            float menv = t < mA ? (float) (t / juce::jmax (1.0, mA)) : t < mA + mH ? 1.0f : (float) juce::jmax (0.0, 1.0 - (t - mA - mH) / juce::jmax (1.0, mD));
            // LFO
            double ph = lsync == 0 ? freeLfo + v (lfoPhase) / 360.0 : vc.lfoPhase;
            if (lsync != 0) vc.lfoPhase += lspeed / sampleRate;
            float lfo = lfoValue (ph, lt);

            double semis = mPitch * 24.0 * menv + lPitch * 12.0 * lfo + bendSemis;
            double inc = vc.inc * (semis != 0.0 ? std::pow (2.0, semis / 12.0) : 1.0);

            // read sample (linear interpolation, loop with crossfade)
            auto readAt = [&] (double p, int ch) -> float
            {
                auto ip = (int) p; auto fr = (float) (p - ip);
                ip = juce::jlimit (0, (int) len - 1, ip);
                int ip2 = juce::jlimit (0, (int) len - 1, ip + 1);
                int c = juce::jmin (ch, srcCh - 1);
                return src.getSample (c, ip) * (1 - fr) + src.getSample (c, ip2) * fr;
            };
            float l = readAt (vc.pos, 0), r = readAt (vc.pos, 1);
            if (loop && xf > 1 && ! rev && vc.pos > l1 - xf && vc.pos < l1)
            {
                float w = (float) ((vc.pos - (l1 - xf)) / xf);
                double p2 = vc.pos - (l1 - l0);
                l = l * (1 - w) + readAt (p2, 0) * w;
                r = r * (1 - w) + readAt (p2, 1) * w;
            }

            // drive
            float drv = juce::jlimit (0.0f, 1.0f, baseDrive + mDrive * menv + lDrive * lfo * 0.5f);
            if (drv > 0.001f) { float g = 1.0f + drv * 12.0f; l = std::tanh (l * g) / std::tanh (g); r = std::tanh (r * g) / std::tanh (g); }
            // per-voice filter
            if (ft >= 1 && ft <= 3)
            {
                float oct = mCut * 6.0f * menv + lCut * 3.0f * lfo + vc.cutoffVel * 4.0f + mwCutAmt * 4.0f;
                vc.filt.setType (ft == 1 ? juce::dsp::StateVariableTPTFilterType::lowpass : ft == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass : juce::dsp::StateVariableTPTFilterType::highpass);
                vc.filt.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, baseCut * std::pow (2.0f, oct)));
                vc.filt.setResonance (res);
                l = vc.filt.processSample (0, l);
                r = vc.filt.processSample (1, r);
            }
            // pan
            float pan = juce::jlimit (-1.0f, 1.0f, zp + mPan * menv + lPan * lfo + mwPanAmt);
            float g = vc.gain * e;
            buf.addSample (0, i, l * g * juce::jmin (1.0f, 1.0f - pan));
            buf.addSample (1, i, r * g * juce::jmin (1.0f, 1.0f + pan));

            vc.stageTime += 1;
            vc.pos += rev ? -inc : inc;
            if (! rev)
            {
                if (loop && vc.pos >= l1) vc.pos = l0 + (vc.pos - l1) + (xf > 1 ? xf : 0.0);
                else if (vc.pos >= s1) vc.active = false;
            }
            else
            {
                if (loop && vc.pos < l0) vc.pos = l1 - (l0 - vc.pos);
                else if (vc.pos < s0) vc.active = false;
            }
        }
    }

    // FX: comp, SR, bits
    const float compAmt = v (comp) / 100.0f, srAmt = v (srRed) / 100.0f, bitAmt = v (bits) / 100.0f;
    const double srStep = srAmt > 0.001f ? sampleRate / juce::jmax (1000.0, 44100.0 * std::pow (0.05, (double) srAmt)) : 1.0;
    const float steps = bitAmt > 0.001f ? std::pow (2.0f, 16.0f - bitAmt * 14.0f) : 0.0f;
    if (compAmt > 0.001f || srAmt > 0.001f || bitAmt > 0.001f)
        for (int i = 0; i < n; ++i)
        {
            float l = buf.getSample (0, i), r = buf.getSample (1, i);
            if (compAmt > 0.001f)
            {
                float lvl = juce::jmax (std::abs (l), std::abs (r));
                compEnv = lvl > compEnv ? compEnv + (lvl - compEnv) * 0.3f : compEnv * 0.9995f;
                float thr = juce::Decibels::decibelsToGain (-compAmt * 30.0f);
                float gr = compEnv > thr ? std::pow (thr / compEnv, 0.75f) : 1.0f;
                float makeup = 1.0f + compAmt * 2.0f;
                l *= gr * makeup; r *= gr * makeup;
            }
            if (srAmt > 0.001f) { srCount += 1.0; if (srCount >= srStep) { srCount -= srStep; holdL = l; holdR = r; } l = holdL; r = holdR; }
            if (steps > 0) { l = std::round (l * steps) / steps; r = std::round (r * steps) / steps; }
            buf.setSample (0, i, l); buf.setSample (1, i, r);
        }

    // EQ filter mode
    if (ft == 4)
    {
        using C = juce::dsp::IIR::Coefficients<float>;
        *eqLo.state = *C::makeLowShelf (sampleRate, 200.0, 0.7f, juce::Decibels::decibelsToGain (v (eqLow)));
        *eqMi.state = *C::makePeakFilter (sampleRate, 1500.0, 0.8f, juce::Decibels::decibelsToGain (v (eqMid)));
        *eqHi.state = *C::makeHighShelf (sampleRate, 6000.0, 0.7f, juce::Decibels::decibelsToGain (v (eqHigh)));
        juce::dsp::AudioBlock<float> blk (buf);
        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        eqLo.process (ctx); eqMi.process (ctx); eqHi.process (ctx);
    }

    // vintage engines: MP60 / S1200 (+ output filter variants)
    const int eng = (int) v (engine);
    if (eng > 0)
    {
        const double rates[] = { 0, 40000, 26040, 26040, 26040, 26040, 26040 };
        const float bitsArr[] = { 0, 16, 12, 12, 12, 12, 12 };
        const float lpf[] = { 0, 16000, 12000, 4500, 7500, 10500, 0 };
        if (eng != lastEngine && lpf[eng] > 0)
            *engineLp.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, juce::jmin ((double) lpf[eng], sampleRate * 0.45), 0.7f);
        lastEngine = eng;
        double step = sampleRate / rates[eng];
        float q = std::pow (2.0f, bitsArr[eng] - 1);
        for (int i = 0; i < n; ++i)
        {
            engCount += 1.0f;
            if (engCount >= step) { engCount -= (float) step; engHoldL = std::round (buf.getSample (0, i) * q) / q; engHoldR = std::round (buf.getSample (1, i) * q) / q; }
            float l = engHoldL, r = engHoldR;
            if (eng == 1) { l = std::tanh (l * 1.2f) / 1.2f * 1.05f; r = std::tanh (r * 1.2f) / 1.2f * 1.05f; }   // MP60 warmth
            buf.setSample (0, i, l); buf.setSample (1, i, r);
        }
        if (lpf[eng] > 0)
        {
            juce::dsp::AudioBlock<float> blk (buf);
            engineLp.process (juce::dsp::ProcessContextReplacing<float> (blk));
        }
    }

    // newest voice position for display
    float pp = -1.0f; double best = 1e18;
    for (auto& vc : voices) if (vc.active && vc.stageTime < best) { best = vc.stageTime; pp = (float) (vc.pos / len); }
    playPos = pp;
}

void SamplerProcessor::saveExtra (juce::XmlElement& x)
{
    if (sample != nullptr) x.setAttribute ("file", sample->file.getFullPathName());
    x.setAttribute ("root", rootKey);
}

void SamplerProcessor::loadExtra (const juce::XmlElement& x)
{
    rootKey = x.getIntAttribute ("root", rootKey);
    // projects from before the filter-type switch: a lowered cutoff meant "low pass on"
    if (! x.hasAttribute ("ftype") && v (cutoff) < 19900.0f) setValue (ftype, 1.0f);
    juce::File f (x.getStringAttribute ("file"));
    if (! f.existsAsFile() && projectSampleFolder.isDirectory()) f = projectSampleFolder.getChildFile (f.getFileName());
    if (f.existsAsFile()) setSample (SampleCache::get().load (f));
}

// ------------------------------------------------------------------------------------------
const juce::StringArray& drumSynthModels() { static juce::StringArray a { "Kick", "Snare", "Hi-hat", "Tom", "Percussion" }; return a; }

DrumSynthProcessor::DrumSynthProcessor (Model m) : InternalProcessor ("Drum Synth", true), model (m)
{
    name = drumSynthModels()[(int) m];
    const float tunes[] = { 50, 180, 8000, 110, 600 };
    addFloat ("tune", "Tune", m == hihat ? 2000.0f : 20.0f, m == hihat ? 16000.0f : 1200.0f, tunes[(int) m], "Hz", m == hihat ? 6000.0f : 150.0f);
    addFloat ("decay", "Decay", 10.0f, 3000.0f, m == kick ? 450.0f : m == hihat ? 90.0f : 250.0f, "ms", 300.0f);
    addFloat ("tone", "Tone", 0.0f, 100.0f, 50.0f, "%");
    addFloat ("punch", m == snare ? "Snap" : m == hihat ? "Metal" : "Punch", 0.0f, 100.0f, 50.0f, "%");
    addFloat ("pitchenv", "Pitch Env", 0.0f, 100.0f, m == kick ? 60.0f : 30.0f, "%");
    addFloat ("level", "Level", -48.0f, 12.0f, 0.0f, "dB");
}

float DrumSynthProcessor::renderVoice (Voice& vc)
{
    const float sr = (float) sampleRate;
    const float t = (float) (vc.t / sampleRate);
    const float tune = v (0), dec = v (1) / 1000.0f, tone = v (2) / 100.0f, punch = v (3) / 100.0f, penv = v (4) / 100.0f;
    float amp = std::exp (-t / (dec * 0.35f));
    if (amp < 0.0005f && t > 0.01f) { vc.active = false; return 0; }
    float out = 0;
    auto noise = rng.nextFloat() * 2.0f - 1.0f;
    switch (model)
    {
        case kick:
        {
            float f = tune * (1.0f + penv * 6.0f * std::exp (-t / 0.03f));
            vc.phase += f / sr;
            out = std::sin ((float) (vc.phase * juce::MathConstants<double>::twoPi));
            out = std::tanh (out * (1.0f + tone * 3.0f));
            out += punch * noise * std::exp (-t / 0.003f) * 0.6f;
            break;
        }
        case snare:
        {
            float f = tune * (1.0f + penv * 2.0f * std::exp (-t / 0.02f));
            vc.phase += f / sr;
            float body = std::sin ((float) (vc.phase * juce::MathConstants<double>::twoPi)) * std::exp (-t / 0.05f);
            vc.hp = 0.9f * vc.hp + 0.1f * noise;
            float n = noise - vc.hp * (1.0f - tone);
            out = body * (1.0f - punch * 0.6f) + n * (0.4f + punch * 0.8f);
            break;
        }
        case hihat:
        {
            float metal = 0;
            const float ratios[] = { 1.0f, 1.342f, 1.2312f, 1.6532f, 1.9523f, 2.1523f };
            vc.phase += tune / sr * 0.25f;
            for (auto r : ratios) metal += (std::fmod ((float) vc.phase * r, 1.0f) < 0.5f ? 1.0f : -1.0f);
            float src = metal / 6.0f * punch + noise * (1.0f - punch * 0.7f);
            vc.hp = vc.hp + (src - vc.hp) * (0.2f + tone * 0.6f);
            out = (src - vc.hp) * 1.5f;
            break;
        }
        case tom:
        {
            float f = tune * (1.0f + penv * 1.5f * std::exp (-t / 0.05f));
            vc.phase += f / sr;
            out = std::sin ((float) (vc.phase * juce::MathConstants<double>::twoPi)) + tone * 0.3f * noise * std::exp (-t / 0.01f);
            break;
        }
        case perc:
        {
            vc.phase2 += tune * (1.5f + punch * 3.0f) / sr;
            float mod = std::sin ((float) (vc.phase2 * juce::MathConstants<double>::twoPi)) * (tone * 4.0f) * std::exp (-t / 0.03f);
            vc.phase += tune * (1.0f + penv * std::exp (-t / 0.02f)) / sr;
            out = std::sin ((float) (vc.phase * juce::MathConstants<double>::twoPi) + mod);
            break;
        }
    }
    vc.t += 1;
    return out * amp * vc.vel;
}

void DrumSynthProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    buf.clear();
    const auto gain = juce::Decibels::decibelsToGain (v (5));
    auto it = midi.begin();
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= i)
        {
            auto m = (*it).getMessage();
            if (m.isNoteOn())
            {
                Voice* vc = &voices[0];
                for (auto& x : voices) if (! x.active) { vc = &x; break; }
                *vc = Voice();
                vc->active = true; vc->vel = m.getFloatVelocity();
            }
            else if (m.isAllNotesOff()) for (auto& x : voices) x.active = false;
            ++it;
        }
        float s = 0;
        for (auto& vc : voices) if (vc.active) s += renderVoice (vc);
        s *= gain;
        buf.setSample (0, i, s);
        buf.setSample (1, i, s);
    }
}

// ------------------------------------------------------------------------------------------
BassSynthProcessor::BassSynthProcessor() : InternalProcessor ("Bass Synth", true)
{
    addChoice ("wave", "Wave", { "Saw", "Square", "Sine" }, 0);
    addFloat ("tune", "Tune", -24.0f, 24.0f, 0.0f, "st");
    addFloat ("cutoff", "Cutoff", 30.0f, 12000.0f, 600.0f, "Hz", 800.0f);
    addFloat ("reso", "Resonance", 0.0f, 100.0f, 30.0f, "%");
    addFloat ("envamt", "Env Amount", 0.0f, 100.0f, 50.0f, "%");
    addFloat ("decay", "Decay", 10.0f, 3000.0f, 300.0f, "ms", 300.0f);
    addFloat ("glide", "Glide", 0.0f, 500.0f, 0.0f, "ms", 100.0f);
    addFloat ("sub", "Sub", 0.0f, 100.0f, 40.0f, "%");
    addFloat ("drive", "Drive", 0.0f, 100.0f, 10.0f, "%");
    addFloat ("level", "Level", -48.0f, 12.0f, -6.0f, "dB");
}

void BassSynthProcessor::prepare()
{
    filt.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, blockSize), 1 });
    filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void BassSynthProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    buf.clear();
    const int wave = (int) v (0);
    const float cut = v (2), res = 0.7f + v (3) / 100.0f * 8.0f, envAmt = v (4) / 100.0f;
    const float decCoef = std::exp (-1.0f / (v (5) / 1000.0f * (float) sampleRate * 0.3f));
    const float glide = v (6) / 1000.0f;
    const float glideCoef = glide > 0 ? std::exp (-1.0f / (glide * (float) sampleRate * 0.3f)) : 0.0f;
    const float sub = v (7) / 100.0f, drive = 1.0f + v (8) / 100.0f * 8.0f, lvl = juce::Decibels::decibelsToGain (v (9));
    auto it = midi.begin();
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= i)
        {
            auto m = (*it).getMessage();
            if (m.isNoteOn())
            {
                held.push_back (m.getNoteNumber());
                targetFreq = juce::MidiMessage::getMidiNoteInHertz (m.getNoteNumber()) * std::pow (2.0, v (1) / 12.0) / 4.0; // bass register
                if (! gate || glide <= 0) freq = targetFreq;
                gate = true; env = 1.0f; curNote = m.getNoteNumber();
            }
            else if (m.isNoteOff())
            {
                held.erase (std::remove (held.begin(), held.end(), m.getNoteNumber()), held.end());
                if (held.empty()) gate = false;
                else targetFreq = juce::MidiMessage::getMidiNoteInHertz (held.back()) * std::pow (2.0, v (1) / 12.0) / 4.0;
            }
            else if (m.isAllNotesOff()) { held.clear(); gate = false; }
            ++it;
        }
        freq = glideCoef > 0 ? targetFreq + (freq - targetFreq) * glideCoef : targetFreq;
        ampEnv += ((gate ? 1.0f : 0.0f) - ampEnv) * (gate ? 0.01f : 0.002f);
        env *= decCoef;
        phase += freq / sampleRate; phase -= std::floor (phase);
        subPhase += freq * 0.5 / sampleRate; subPhase -= std::floor (subPhase);
        float o = wave == 0 ? (float) (2.0 * phase - 1.0) : wave == 1 ? (phase < 0.5 ? 1.0f : -1.0f) : std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
        o += sub * std::sin ((float) (subPhase * juce::MathConstants<double>::twoPi));
        filt.setCutoffFrequency (juce::jlimit (20.0f, 18000.0f, cut * (1.0f + envAmt * 8.0f * env)));
        filt.setResonance (res);
        o = filt.processSample (0, o);
        o = std::tanh (o * drive) / std::tanh (drive) * ampEnv * lvl;
        buf.setSample (0, i, o);
        buf.setSample (1, i, o);
    }
}

// ------------------------------------------------------------------------------------------
PolySynthProcessor::PolySynthProcessor() : InternalProcessor ("Poly Synth", true)
{
    addChoice ("wave", "Wave", { "Saw", "Square", "Triangle" }, 0);
    addFloat ("detune", "Detune", 0.0f, 50.0f, 8.0f, "ct");
    addFloat ("cutoff", "Cutoff", 50.0f, 18000.0f, 3000.0f, "Hz", 1500.0f);
    addFloat ("reso", "Resonance", 0.0f, 100.0f, 15.0f, "%");
    addFloat ("attack", "Attack", 1.0f, 4000.0f, 10.0f, "ms", 300.0f);
    addFloat ("decay", "Decay", 1.0f, 4000.0f, 400.0f, "ms", 400.0f);
    addFloat ("sustain", "Sustain", 0.0f, 100.0f, 70.0f, "%");
    addFloat ("release", "Release", 1.0f, 6000.0f, 500.0f, "ms", 500.0f);
    addFloat ("level", "Level", -48.0f, 12.0f, -9.0f, "dB");
}

void PolySynthProcessor::prepare()
{
    for (auto& vc : voices)
    {
        vc.adsr.setSampleRate (sampleRate);
        vc.f.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, blockSize), 1 });
        vc.f.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        vc.active = false;
    }
}

void PolySynthProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    buf.clear();
    const int wave = (int) v (0);
    const double det = std::pow (2.0, v (1) / 1200.0);
    juce::ADSR::Parameters ap { v (4) / 1000.0f, v (5) / 1000.0f, v (6) / 100.0f, v (7) / 1000.0f };
    const float cut = v (2), res = 0.7f + v (3) / 100.0f * 6.0f, lvl = juce::Decibels::decibelsToGain (v (8));
    auto osc = [wave] (double p)
    {
        if (wave == 0) return (float) (2.0 * p - 1.0);
        if (wave == 1) return p < 0.5 ? 1.0f : -1.0f;
        return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    };
    auto it = midi.begin();
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        while (it != midi.end() && (*it).samplePosition <= i)
        {
            auto m = (*it).getMessage();
            if (m.isNoteOn())
            {
                Voice* vc = nullptr;
                for (auto& x : voices) if (! x.active) { vc = &x; break; }
                if (vc == nullptr) vc = &voices[0];
                vc->active = true; vc->note = m.getNoteNumber(); vc->vel = m.getFloatVelocity();
                vc->freq = juce::MidiMessage::getMidiNoteInHertz (vc->note);
                vc->adsr.setParameters (ap); vc->adsr.reset(); vc->adsr.noteOn(); vc->f.reset();
            }
            else if (m.isNoteOff()) { for (auto& x : voices) if (x.active && x.note == m.getNoteNumber()) x.adsr.noteOff(); }
            else if (m.isAllNotesOff()) for (auto& x : voices) x.adsr.noteOff();
            ++it;
        }
        float s = 0;
        for (auto& vc : voices)
        {
            if (! vc.active) continue;
            if (! vc.adsr.isActive()) { vc.active = false; continue; }
            vc.p1 += vc.freq / sampleRate; vc.p1 -= std::floor (vc.p1);
            vc.p2 += vc.freq * det / sampleRate; vc.p2 -= std::floor (vc.p2);
            float o = (osc (vc.p1) + osc (vc.p2)) * 0.5f;
            vc.f.setCutoffFrequency (cut); vc.f.setResonance (res);
            s += vc.f.processSample (0, o) * vc.adsr.getNextSample() * vc.vel;
        }
        s *= lvl;
        buf.setSample (0, i, s);
        buf.setSample (1, i, s);
    }
}

// ------------------------------------------------------------------------------------------
const std::vector<InternalMenuSection>& internalMenu()
{
    static std::vector<InternalMenuSection> m {
        { { "Audio", "Sampler", "Drum Synth", "Bass Synth", "Poly Synth" } },
        { { "Compressor", "Gate", "Transient Master", "Limiter", "Maximizer" } },
        { { "EQ", "Filter", "Cabinet" } },
        { { "Chorus", "Flanger", "FM", "Freq Shifter", "Phaser" } },
        { { "Ice", "Metaverb", "Reflex", "Reverb (Legacy)", "Reverb" } },
        { { "Beat Delay", "Grain Delay", "Grain Stretch", "Resochord" } },
        { { "Distortion", "Lofi", "Saturator" } } };
    return m;
}

bool isInternalInstrument (const juce::String& n)
{
    return n == "Audio" || n == "Sampler" || n == "Bass Synth" || n == "Poly Synth" || drumSynthModels().contains (n) || n.startsWith ("Drum Synth");
}

std::unique_ptr<juce::AudioProcessor> createInternal (const juce::String& n)
{
    if (n == "Sampler" || n == "Audio")
    {
        auto s = std::make_unique<SamplerProcessor>();
        if (n == "Audio") s->params[SamplerProcessor::loopP]->setValueNotifyingHost (1.0f);
        return s;
    }
    if (n == "Bass Synth") return std::make_unique<BassSynthProcessor>();
    if (n == "Poly Synth") return std::make_unique<PolySynthProcessor>();
    auto idx = drumSynthModels().indexOf (n);
    if (n == "Drum Synth") idx = 0;
    if (idx >= 0) return std::make_unique<DrumSynthProcessor> ((DrumSynthProcessor::Model) idx);
    return createInternalEffect (n);
}
} // namespace gb
