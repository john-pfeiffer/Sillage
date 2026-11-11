// Renders every page of the Sillage editor to a PNG without a host, so a
// layout change can be looked at on a machine with no DAW — headless under
// xvfb-run. Build with -DSILLAGE_BUILD_SNAPSHOT_TOOL=ON.
//
//   SillageSnapshot [output-dir] [--off] [--play <type>]
//
// With --off every stage switch is turned off first, so the greyed-out state
// of each tab can be checked too. With --play a Type is applied, an LFO is
// routed to Size, and a drum-and-arpeggio phrase is run through the plugin
// while the editor's timers tick, so the pictures that draw from the audio
// (the tail, the meters, the mod markers) have something to show.

#include <cstdio>
#include <cstring>

#include "Modulation.h"
#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Types.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto outDir = juce::File::getCurrentWorkingDirectory();
    bool stagesOff = false;
    juce::String playType;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--off") == 0)
            stagesOff = true;
        else if (std::strcmp (argv[i], "--play") == 0 && i + 1 < argc)
            playType = argv[++i];
        else
            outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]);
    }
    outDir.createDirectory();

    SillageAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    if (stagesOff)
        for (auto* id : { params::id::loopOn, params::id::transientsOn, params::id::ageOn,
                          params::id::rewindOn, params::id::chaosOn, params::id::modOn, params::id::timeSync })
            if (auto* param = processor.apvts.getParameter (id))
                param->setValueNotifyingHost (0.0f);

    if (playType.isNotEmpty())
    {
        for (int t = 0; t < types::kNumTypes; ++t)
            if (playType.equalsIgnoreCase (types::kTypeNames[(size_t) t]))
                types::apply (processor, (types::Type) t);

        const auto set = [&] (const char* id, float plain)
        {
            if (auto* param = processor.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (plain));
        };
        set (params::id::modSource[0], (float) mod::Source::lfo1);
        set (params::id::modDestination[0], (float) mod::Destination::size);
        set (params::id::modAmount[0], 35.0f);
        set (params::id::lfoRate[0], 0.4f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* sillage = dynamic_cast<SillageAudioProcessorEditor*> (editor.get());
    if (sillage == nullptr)
    {
        std::printf ("unexpected editor type\n");
        return 1;
    }

    auto& panel = sillage->getPanel();
    std::printf ("editor %d x %d\n", editor->getWidth(), editor->getHeight());

    if (playType.isNotEmpty())
    {
        // Three and a half seconds of phrase, then a second and a half of
        // tail, with the message loop run between blocks so the editor's
        // timers see it go by.
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        const double notes[8] { 220.0, 261.63, 329.63, 440.0, 392.0, 329.63, 261.63, 293.66 };
        juce::Random noise (7);
        int64_t position = 0;

        for (int frame = 0; frame < 150; ++frame)
        {
            for (int block = 0; block < 3; ++block)
            {
                for (int i = 0; i < 512; ++i, ++position)
                {
                    const auto t = (double) position / 48000.0;
                    double value = 0.0;
                    if (t < 3.5)
                    {
                        const auto kick = std::fmod (t, 0.5);
                        value += 0.5 * std::sin (juce::MathConstants<double>::twoPi * (50.0 * kick + 3.0 * (1.0 - std::exp (-kick / 0.03))))
                                     * std::exp (-kick / 0.12);
                        value += 0.1 * (noise.nextDouble() * 2.0 - 1.0) * std::exp (-std::fmod (t + 0.25, 0.5) / 0.02);
                        const auto note = std::fmod (t, 0.25);
                        value += 0.25 * std::sin (juce::MathConstants<double>::twoPi * notes[(int) (t / 0.25) % 8] * note)
                                      * std::exp (-note / 0.2);
                    }
                    buffer.setSample (0, i, (float) value);
                    buffer.setSample (1, i, (float) value);
                }
                processor.processBlock (buffer, midi);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        }
    }

    for (int tab = 0; tab < panel.getNumTabs(); ++tab)
    {
        panel.showTab (tab);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);

        auto name = panel.getTabName (tab).toLowerCase().replaceCharacters (" &", "--").removeCharacters ("-");
        auto file = outDir.getChildFile ("snapshot-" + juce::String (tab) + "-" + name
                                         + (stagesOff ? "-off" : "")
                                         + (playType.isNotEmpty() ? "-" + playType.toLowerCase() : juce::String()) + ".png");
        file.deleteFile();

        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::printf ("failed to write %s\n", file.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    return 0;
}
