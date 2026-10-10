cmake_minimum_required(VERSION 3.22)
project(MooDuck VERSION 1.21 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

# Windows CI fix: JUCE's juceaide bootstrap re-invokes CMake in a fresh build tree
# that does NOT inherit our -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded, so it
# compiles with /Zi + a shared vc140.pdb. Under parallel Ninja AND sccache (our
# compiler launcher, which only supports /Z7-style embedded debug info, never the
# separate-PDB /Zi) the parallel cl.exe race on that PDB -> "fatal error C1041".
# /FS alone can't help because under sccache the PDB shouldn't exist at all.
# Fix: force C7/embedded debug info into the bootstrap via _CL_, whose options cl
# appends AFTER the command line, so /Z7 overrides the bootstrap's /Zi -> no PDB,
# no race, and the objects stay sccache-cacheable. Set before FetchContent so it
# is live during the bootstrap's configure-time execute_process (inherits env).
# MSVC-only; the main build already uses Embedded, so this is a no-op there.
if(MSVC)
    set(ENV{_CL_} "/Z7")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Plugin identity codes mirror Builds/LinuxMakefile output so the CMake-built
# binary keeps the same VST3 UID / AU subtype as the local Projucer build.
# ManufacturerCode 0x4d616e75 = 'Manu', PluginCode 0x49366e67 = 'I6ng'.
juce_add_plugin(MooDuck
    PRODUCT_NAME             "MooDuck"
    COMPANY_NAME             "mvzn"
    BUNDLE_ID                "com.mvzn.MooDuck"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE              I6ng
    FORMATS                  VST3 AU Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_CATEGORIES          "Fx" "Dynamics"
    AU_MAIN_TYPE             "kAudioUnitType_Effect"
)

juce_generate_juce_header(MooDuck)

juce_add_binary_data(MooDuckBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES
        Resources/fonts/IBMPlexSansCondensed-Regular.ttf
        Resources/fonts/IBMPlexSansCondensed-Medium.ttf
        Resources/sounds/moo.wav
        Resources/sounds/quack.wav
)

target_sources(MooDuck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MooDuckLookAndFeel.cpp
    Source/LevelMeterComponent.cpp
    Source/ThemeState.cpp
)

target_compile_definitions(MooDuck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_include_directories(MooDuck PRIVATE
    Source/vendor
)

target_link_libraries(MooDuck
    PRIVATE
        MooDuckBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Host-level regression tests. A console app that links the plugin's shared code
# and drives the REAL MooDuckAudioProcessor through the host API to assert the
# latency / PDC contract (which the headless tests/runtests.cpp can't reach,
# since it deliberately drops juce_audio_processors / the plugin client).
#   cmake --build build-cmake --target MooDuckLatencyTest
#   ctest --test-dir build-cmake -R latency --output-on-failure
# Disable with -DMOODUCK_BUILD_TESTS=OFF.
# ---------------------------------------------------------------------------
option(MOODUCK_BUILD_TESTS "Build MooDuck host-level regression tests" ON)
if(MOODUCK_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MooDuckLatencyTest PRODUCT_NAME "MooDuckLatencyTest")
    juce_generate_juce_header(MooDuckLatencyTest)

    target_sources(MooDuckLatencyTest PRIVATE tests/latency_test.cpp)

    target_include_directories(MooDuckLatencyTest PRIVATE Source Source/vendor)

    target_compile_definitions(MooDuckLatencyTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    # We need the plugin's compiled objects (the real processor + editor), so we link the
    # shared-code ARCHIVE BY FILE rather than the MooDuck target. Linking the target would
    # drag in its INTERFACE definitions — including JUCE_STANDALONE_APPLICATION=
    # JucePlugin_Build_Standalone, which collides with the JUCE_STANDALONE_APPLICATION=1 that
    # juce_add_console_app sets (both evaluate to 1, but the differing tokens warn as a macro
    # redefinition). Linking the file gives us the objects without the interface. The JUCE
    # modules are compiled into THIS target via the juce:: links; the archive's own module
    # copies are pulled on demand only for the plugin-specific objects (no duplicate symbols).
    add_dependencies(MooDuckLatencyTest MooDuck)
    target_link_libraries(MooDuckLatencyTest PRIVATE
        $<TARGET_FILE:MooDuck>
        MooDuckBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME latency COMMAND MooDuckLatencyTest)
endif()
