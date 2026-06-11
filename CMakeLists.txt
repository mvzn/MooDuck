cmake_minimum_required(VERSION 3.22)
project(MaybeDuck VERSION 1.21 LANGUAGES C CXX)

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
juce_add_plugin(MaybeDuck
    PRODUCT_NAME             "MaybeDuck"
    COMPANY_NAME             "yourcompany"
    BUNDLE_ID                "com.yourcompany.MaybeDuck"
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

juce_generate_juce_header(MaybeDuck)

juce_add_binary_data(MaybeDuckBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES
        Resources/fonts/IBMPlexSansCondensed-Regular.ttf
        Resources/fonts/IBMPlexSansCondensed-Medium.ttf
)

target_sources(MaybeDuck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MaybeDuckLookAndFeel.cpp
    Source/LevelMeterComponent.cpp
    Source/ThemeState.cpp
)

target_compile_definitions(MaybeDuck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_include_directories(MaybeDuck PRIVATE
    Source/vendor
)

target_link_libraries(MaybeDuck
    PRIVATE
        MaybeDuckBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Host-level regression tests. A console app that links the plugin's shared code
# and drives the REAL MaybeDuckAudioProcessor through the host API to assert the
# latency / PDC contract (which the headless tests/runtests.cpp can't reach,
# since it deliberately drops juce_audio_processors / the plugin client).
#   cmake --build build-cmake --target MaybeDuckLatencyTest
#   ctest --test-dir build-cmake -R latency --output-on-failure
# Disable with -DMAYBEDUCK_BUILD_TESTS=OFF.
# ---------------------------------------------------------------------------
option(MAYBEDUCK_BUILD_TESTS "Build MaybeDuck host-level regression tests" ON)
if(MAYBEDUCK_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MaybeDuckLatencyTest PRODUCT_NAME "MaybeDuckLatencyTest")
    juce_generate_juce_header(MaybeDuckLatencyTest)

    target_sources(MaybeDuckLatencyTest PRIVATE tests/latency_test.cpp)

    target_include_directories(MaybeDuckLatencyTest PRIVATE Source Source/vendor)

    target_compile_definitions(MaybeDuckLatencyTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    # Link the shared-code lib (the real processor + editor objects) and the same JUCE
    # modules the plugin uses. The module sources compile into this target; the shared-code
    # archive is pulled on demand only for the plugin-specific objects, so there are no
    # duplicate JUCE symbols.
    target_link_libraries(MaybeDuckLatencyTest PRIVATE
        MaybeDuck
        MaybeDuckBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME latency COMMAND MaybeDuckLatencyTest)
endif()
