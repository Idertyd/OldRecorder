cmake_minimum_required(VERSION 3.22)
project(OldRecorder VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(OldRecorder
    COMPANY_NAME "OldRecorder"
    PLUGIN_MANUFACTURER_CODE Mydr
    PLUGIN_CODE Oldr
    FORMATS VST3
    PRODUCT_NAME "Old Recorder"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(OldRecorder PRIVATE Source/Plugin.cpp)

target_compile_definitions(OldRecorder PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OldRecorder
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
