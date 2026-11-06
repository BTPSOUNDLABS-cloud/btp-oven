#pragma once

#include <JuceHeader.h>

/**
    Portable on-disk locations used by every engine.

    Kept in a header-only unit (no ACE-Step / ONNX dependencies) so the
    standalone smoke-test executables can share the exact same rules as the
    plugin without linking the whole AI engine.

    Layout:
        %APPDATA%\BtP Oven\             config + models + training
        %APPDATA%\BtP Oven\Models\      portable model root
*/
namespace btppaths
{
    inline juce::File getConfigDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("BtP Oven");
    }

    inline juce::File getModelsRoot()  { return getConfigDir().getChildFile ("Models"); }
    inline juce::File getTrainingDir() { return getConfigDir().getChildFile ("Training"); }

    /** Folder next to the running binary, for installs that ship Models beside
        the plugin instead of in %APPDATA%. */
    inline juce::File getAppModelsDir()
    {
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                 .getParentDirectory().getChildFile ("Models");
    }
}
