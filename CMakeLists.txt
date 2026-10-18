cmake_minimum_required(VERSION 3.22)
project(TriOsc VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TriOsc
    COMPANY_NAME "TriOscAudio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Tosc
    PLUGIN_CODE Tos1
    FORMATS VST3
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Ladds Cool Oscillator")

target_sources(TriOsc PRIVATE TriOsc.cpp TriOsc.h)

target_compile_definitions(TriOsc PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TriOsc
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
