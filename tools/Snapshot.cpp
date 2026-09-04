// Renders every page of the Sillage editor to a PNG without a host, so a
// layout change can be looked at on a machine with no DAW — headless under
// xvfb-run. Build with -DSILLAGE_BUILD_SNAPSHOT_TOOL=ON.
//
//   SillageSnapshot [output-dir]

#include <cstdio>

#include "PluginEditor.h"
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                 : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    SillageAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* sillage = dynamic_cast<SillageAudioProcessorEditor*> (editor.get());
    if (sillage == nullptr)
    {
        std::printf ("unexpected editor type\n");
        return 1;
    }

    auto& panel = sillage->getPanel();
    std::printf ("editor %d x %d\n", editor->getWidth(), editor->getHeight());

    for (int tab = 0; tab < panel.getNumTabs(); ++tab)
    {
        panel.showTab (tab);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);

        auto name = panel.getTabName (tab).toLowerCase().replaceCharacters (" &", "--").removeCharacters ("-");
        auto file = outDir.getChildFile ("snapshot-" + juce::String (tab) + "-" + name + ".png");
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
