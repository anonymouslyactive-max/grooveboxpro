#include "MainComponent.h"
#include "SelfTest.h"

class GrooveboxProApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Groovebox Pro"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        engine = std::make_unique<gb::Engine>();
        engine->reloadSettingsCache();
        if (commandLine.contains ("--selftest"))
        {
            auto dir = juce::File (commandLine.fromFirstOccurrenceOf ("--selftest", false, false).trim().unquoted());
            if (dir == juce::File()) dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GrooveboxProTest");
            int r = gb::runSelfTest (*engine, dir);
            setApplicationReturnValue (r);
            quit();
            return;
        }
        window = std::make_unique<MainWindow> (*engine);

        // open a project passed on the command line (double-clicked .gbp), or the last one
        auto arg = commandLine.unquoted().trim();
        if (arg.endsWithIgnoreCase (".gbp") && juce::File (arg).existsAsFile()) engine->loadProject (juce::File (arg));
        else if (engine->settings.getBool ("reloadLast", false))
        {
            juce::File last (engine->settings.getString ("lastProject"));
            if (last.existsAsFile()) engine->loadProject (last);
        }
        engine->metronomeOn = engine->settings.getBool ("metroEnabled", false);
        if (commandLine.contains ("--inject"))   // development: replay FelonE Beats template messages
        {
            struct Step { int ms; juce::MidiMessage m; };
            auto cc = [] (int n, int v) { return juce::MidiMessage::controllerEvent (1, n, v); };
            auto on = [] (int n) { return juce::MidiMessage::noteOn (1, n, (juce::uint8) 100); };
            auto off = [] (int n) { return juce::MidiMessage::noteOff (1, n); };
            std::vector<Step> steps { { 300, cc (82, 127) }, { 50, cc (82, 0) },              // KEYBOARD
                                      { 300, cc (81, 127) }, { 50, cc (81, 0) },              // PAD MODE
                                      { 200, on (48) }, { 100, off (48) },                    // pad 1
                                      { 200, cc (92, 127) }, { 100, on (49) }, { 50, off (49) }, { 100, cc (92, 0) },   // MUTE + pad 2
                                      { 200, cc (83, 127) }, { 50, cc (83, 0) },              // STEP on
                                      { 200, on (48) }, { 50, off (48) }, { 100, on (52) }, { 50, off (52) }, { 100, on (56) }, { 50, off (56) }, { 100, on (60) }, { 50, off (60) },
                                      { 200, cc (83, 127) }, { 50, cc (83, 0) },              // STEP off
                                      { 200, cc (35, 127) }, { 50, cc (35, 0) },              // PLUG-IN
                                      { 200, cc (38, 127) }, { 50, cc (38, 0) },              // BROWSER
                                      { 200, cc (8, 1) }, { 100, cc (8, 1) },                 // encoder
                                      { 200, cc (45, 127) }, { 50, cc (8, 1) }, { 50, cc (8, 1) }, { 50, cc (45, 0) },  // SWING + encoder
                                      { 300, cc (37, 127) }, { 50, cc (37, 0) } };           // MIXER
            if (engine->deviceManager.getCurrentAudioDevice() == nullptr)
            {
                struct Pump : juce::Timer { gb::Engine* e; void timerCallback() override { juce::AudioIODeviceCallbackContext c; e->audioDeviceIOCallbackWithContext (nullptr, 0, nullptr, 0, 512, c); } };
                static Pump pump; pump.e = engine.get(); pump.startTimer (11);
            }
            int t = 2000;
            for (auto& st : steps)
            {
                t += st.ms;
                auto msg = st.m;
                juce::Timer::callAfterDelay (t, [this, msg] { engine->handleIncomingMidiMessage (nullptr, msg); });
            }
        }
    }

    void shutdown() override
    {
        window.reset();
        engine.reset();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr) window->content->confirmDiscard ([] { quit(); });
        else quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        auto arg = commandLine.unquoted().trim();
        if (arg.endsWithIgnoreCase (".gbp") && window) window->content->confirmDiscard ([this, arg] { engine->loadProject (juce::File (arg)); });
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (gb::Engine& e) : DocumentWindow ("Groovebox Pro", gb::col::header, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new gb::MainComponent (e);
            setContentOwned (content, true);
            setMenuBar (content, 22);
            content->onFullscreen = [this]
            {
                bool fs = ! isFullScreen();
                setFullScreen (fs);
            };
            content->onQuit = [] { JUCEApplication::getInstance()->systemRequestedQuit(); };
            content->onTitle = [this] (const juce::String& t) { setName (t + " - Groovebox Pro"); };
            setResizable (true, false);
            setResizeLimits (1100, 700, 10000, 10000);
            centreWithSize (1600, 960);
            setVisible (true);
           #if JUCE_WINDOWS
            setFullScreen (true);   // maximised, like the original
           #endif
        }
        ~MainWindow() override { setMenuBar (nullptr); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
        gb::MainComponent* content = nullptr;
    };

private:
    std::unique_ptr<gb::Engine> engine;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (GrooveboxProApp)
