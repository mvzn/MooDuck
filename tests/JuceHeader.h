// Minimal JuceHeader for headless test runner — pulls only the modules used by
// DynamicsCore.h, CrossoverCore.h and FXFunctions.h. Avoids juce_gui_*, audio_devices,
// audio_plugin_client and the rest of the plugin scaffolding.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
