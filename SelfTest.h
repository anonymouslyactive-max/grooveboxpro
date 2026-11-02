#pragma once
#include "Engine.h"

// Headless self-test used during development: builds a beat, saves, reloads, undoes and renders it.
namespace gb
{
inline int runSelfTest (Engine& e, const juce::File& outDir)
{
    auto log = [] (const juce::String& s) { std::cout << s << std::endl; };
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { log ((ok ? "PASS  " : "FAIL  ") + what); if (! ok) ++failures; };
    outDir.createDirectory();

    // a test sample (decaying 220 Hz tone)
    auto wavFile = outDir.getChildFile ("tone.wav");
    {
        juce::AudioBuffer<float> b (2, 22050);
        for (int i = 0; i < b.getNumSamples(); ++i) { float v = std::sin (i * 2.0f * 3.14159f * 220.0f / 44100.0f) * std::exp (-i / 6000.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        juce::WavAudioFormat wav;
        wavFile.deleteFile();
        auto os = wavFile.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100, 2, 24, {}, 0));
        os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    auto& P = e.project();
    auto& G = *P.groups[0];
    juce::String err;
    const char* types[] = { "Kick", "Snare", "Hi-hat", "Poly Synth" };
    for (int i = 0; i < 4; ++i)
    {
        auto s = e.createSlot (types[i], {}, err);
        check (s != nullptr, juce::String ("create ") + types[i]);
        if (s) e.insertSlot (G.sounds[(size_t) i]->slots, 0, std::move (s));
    }
    e.loadSampleIntoSound (*G.sounds[4], wavFile);
    check (e.samplerOf (*G.sounds[4]) != nullptr && e.samplerOf (*G.sounds[4])->getSample() != nullptr, "sampler loaded WAV");
    for (auto fx : { "Compressor", "Gate", "Transient Master", "Limiter", "Maximizer", "EQ", "Filter", "Cabinet", "Chorus", "Flanger", "FM", "Freq Shifter",
                     "Phaser", "Ice", "Metaverb", "Reflex", "Reverb (Legacy)", "Reverb", "Beat Delay", "Grain Delay", "Grain Stretch", "Resochord", "Distortion", "Lofi", "Saturator" })
    {
        auto s = e.createSlot (fx, {}, err);
        check (s != nullptr, juce::String ("create FX ") + fx);
        if (s && juce::String (fx) == "Beat Delay") e.insertSlot (G.sounds[1]->slots, 1, std::move (s));
        else if (s && juce::String (fx) == "Compressor") e.insertSlot (G.slots, 0, std::move (s));
        else if (s && juce::String (fx) == "Reverb") e.insertSlot (P.masterSlots, 0, std::move (s));
        else if (s && juce::String (fx) == "Saturator") e.insertSlot (G.sounds[3]->slots, 1, std::move (s));
    }
    G.sounds[2]->chokeGroup = 1; G.sounds[2]->chokeMode = 2;

    // song: 1-bar pattern used by two clips
    e.songMode = true;
    auto* pat = e.newPatternFor (G, P.ticksPerBar());
    for (int st = 0; st < 16; ++st)
    {
        if (st % 4 == 0) pat->notes.push_back ({ 0, st * 240, 240, 1.0f });
        if (st % 8 == 4) pat->notes.push_back ({ 1, st * 240, 240, 0.9f });
        if (st % 2 == 0) pat->notes.push_back ({ 2, st * 240, 120, 0.6f, 0.8f });
    }
    pat->notes.push_back ({ 3, 0, 1920, 0.8f, 1.0f, 60 });
    pat->notes.push_back ({ 3, 1920, 1920, 0.8f, 1.0f, 67 });
    pat->notes.push_back ({ 4, 960, 960, 0.9f });
    G.clips.push_back ({ 0, P.ticksPerBar(), 0 });
    G.clips.push_back ({ P.ticksPerBar(), P.ticksPerBar(), 0 });
    AutoLane lane; lane.sound = 0; lane.target = "level"; lane.name = "Level"; lane.steps[0] = 0.5f; lane.steps[8] = 0.25f;
    pat->lanes.push_back (lane);
    P.swing = 0.2f;
    check (P.songLength() == 2 * P.ticksPerBar(), "song length 2 bars");

    // save + reload
    const auto expectedNotes = pat->notes.size();   // `pat` dies with the old project on reload
    auto projFile = outDir.getChildFile ("SelfTest.gbp");
    check (e.saveProject (projFile, true), "save project with samples");
    check (projFile.getSiblingFile ("SelfTest Samples").getChildFile ("tone.wav").existsAsFile(), "sample copied into project folder");
    check (e.loadProject (projFile), "reload project");
    auto& P2 = e.project();
    check (P2.groups.size() == 1 && P2.groups[0]->clips.size() == 2, "clips restored");
    check (P2.groups[0]->patterns.size() == 1 && P2.groups[0]->patterns[0]->notes.size() == expectedNotes, "notes restored (" + juce::String ((int) P2.groups[0]->patterns[0]->notes.size()) + ")");
    check (P2.groups[0]->sounds[1]->slots.size() == 2, "sound chain restored");
    check (P2.masterSlots.size() == 1, "master chain restored");
    check (e.samplerOf (*P2.groups[0]->sounds[4]) && e.samplerOf (*P2.groups[0]->sounds[4])->getSample() != nullptr, "sampler sample restored");
    check (P2.groups[0]->sounds[2]->chokeGroup == 1, "choke restored");

    // undo / redo keeps plug-in instances
    auto* procBefore = P2.groups[0]->sounds[0]->slots[0]->proc.get();
    e.snapshot ("test");
    P2.groups[0]->sounds[0]->name = "Changed";
    e.undo();
    check (e.project().groups[0]->sounds[0]->name == "Sound 1" || e.project().groups[0]->sounds[0]->name == "Kick" || e.project().groups[0]->sounds[0]->name != "Changed", "undo restores name");
    check (e.project().groups[0]->sounds[0]->slots[0]->proc.get() == procBefore, "undo re-uses processor instance");
    e.redo();
    check (e.project().groups[0]->sounds[0]->name == "Changed", "redo");

    // render the song
    Engine::ExportOptions o; o.range = 0; o.tail = 1.0; o.file = outDir.getChildFile ("song.wav");
    bool ok = e.exportAudio (o, {}, err);
    check (ok && o.file.existsAsFile(), "export song " + err);
    auto analyse = [&] (const juce::File& f, juce::String name)
    {
        auto s = SampleCache::get().load (f);
        if (s == nullptr) { check (false, "read " + name); return; }
        auto& b = s->buffer;
        float peak = b.getMagnitude (0, b.getNumSamples());
        int beats = 0;
        int spb = (int) (44100 * 60.0 / e.project().bpm);
        for (int i = 0; i + spb <= b.getNumSamples(); i += spb) beats += b.getRMSLevel (0, i, spb) > 0.001f;
        log ("      " + name + ": " + juce::String (b.getNumSamples() / 44100.0, 2) + " s, peak " + juce::String (peak, 3) + ", active beats " + juce::String (beats));
        check (peak > 0.05f && peak < 4.0f, name + " has audio");
        check (beats >= 8, name + " sounds on every beat");
    };
    analyse (o.file, "song.wav");

    // stems
    Engine::ExportOptions st = o; st.stems = 2; st.file = outDir.getChildFile ("stem.wav");
    check (e.exportAudio (st, {}, err), "export sound stems");
    check (outDir.getChildFile ("stem - Kick.wav").existsAsFile() || outDir.getNumberOfChildFiles (juce::File::findFiles, "stem - *.wav") >= 4, "stem files written (" + juce::String (outDir.getNumberOfChildFiles (juce::File::findFiles, "stem - *.wav")) + ")");

    // ideas mode: scene loop
    e.songMode = false;
    e.ensureSceneSlots();
    e.project().scenes[0].patternForGroup[0] = 0;
    Engine::ExportOptions id = o; id.range = 1; id.bars = 2; id.file = outDir.getChildFile ("scene.wav");
    check (e.exportAudio (id, {}, err), "export scene loop");
    analyse (id.file, "scene.wav");

    // second group + mute
    e.addGroup();
    check (e.project().groups.size() == 2 && e.project().scenes[0].patternForGroup.size() == 2, "add group extends scenes");

    // MIDI export of the pattern
    log (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)");
    return failures;
}
} // namespace gb
