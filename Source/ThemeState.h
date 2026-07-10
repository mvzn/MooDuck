/*
  ==============================================================================

    ThemeState.h

    Process-wide dark-mode toggle, persisted across launches.

    One ThemeState instance lives behind a function-local static (avoids
    global-initialisation-order issues with the JUCE PropertiesFile). Editor
    instances poll generation() each timer tick to detect when another window
    flipped the theme.

    Persistence: PropertiesFile at the platform-appropriate per-user location
      Linux:   ~/.config/MooDuck/MooDuck.settings
      macOS:   ~/Library/Application Support/MooDuck/MooDuck.settings
      Windows: %APPDATA%/MooDuck/MooDuck.settings
    Key: "darkMode" (bool, default false).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

struct ThemeState
{
    // Function-local static — constructed on first call, destroyed at exit.
    static ThemeState& get();

    bool isDark() const noexcept       { return dark.load(); }

    // Flip the bit, persist to disk, bump generation so polling editors notice.
    void toggle();

    // Editors compare this each timer tick to detect another window's toggle.
    int  generation() const noexcept   { return gen.load(); }

private:
    ThemeState();
    ~ThemeState() = default;

    ThemeState (const ThemeState&) = delete;
    ThemeState& operator= (const ThemeState&) = delete;

    std::atomic<bool> dark { false };
    std::atomic<int>  gen  { 0 };
    std::unique_ptr<juce::PropertiesFile> props;
};
