/*
  ==============================================================================

    ThemeState.cpp

  ==============================================================================
*/

#include "ThemeState.h"

ThemeState& ThemeState::get()
{
    // Function-local static: thread-safe init (since C++11) and dodges the
    // static-initialisation-order fiasco we'd hit with a file-scope global.
    static ThemeState instance;
    return instance;
}

ThemeState::ThemeState()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "MaybeDuck";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.folderName          = "MaybeDuck";
    opts.commonToAllUsers    = false;

    // Make sure the parent directory exists; otherwise juce::PropertiesFile can
    // silently fall back to writing in the process CWD on Linux.
    opts.getDefaultFile().getParentDirectory().createDirectory();

    props = std::make_unique<juce::PropertiesFile>(opts);
    dark.store(props->getBoolValue("darkMode", false));
}

void ThemeState::toggle()
{
    const bool next = ! dark.load();
    dark.store(next);

    if (props != nullptr)
    {
        props->setValue("darkMode", next);
        props->saveIfNeeded();
    }

    gen.fetch_add(1);
}
