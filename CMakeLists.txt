cmake_minimum_required(VERSION 3.22)
project(MaybeDuck VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

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
    COPY_PLUGIN_AFTER_BUILD  FALSE
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
)

target_compile_definitions(MaybeDuck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
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
