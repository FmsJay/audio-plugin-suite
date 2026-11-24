cmake_minimum_required(VERSION 3.22)
project(Fossil VERSION 1.0.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PluginCommon.cmake")

juce_add_plugin(Fossil
    COMPANY_NAME "PortableWorkstation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE PwSt
    PLUGIN_CODE Foss
    FORMATS VST3 Standalone
    PRODUCT_NAME "Fossil"
)

target_sources(Fossil PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_include_directories(Fossil PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(Fossil
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

suite_configure_plugin(
    TARGET Fossil
    CLAP_ID "com.portableworkstation.fossil"
    CLAP_FEATURES audio-effect granular reverb
)

# Standalone console test target
add_executable(FossilTests
    tests/FossilTests.cpp
)

target_link_libraries(FossilTests PRIVATE
    Fossil
    SuiteShared
)

target_compile_definitions(FossilTests PRIVATE
    JUCE_STANDALONE_APPLICATION=0
)

if(MSVC)
    target_compile_options(FossilTests PRIVATE /bigobj /utf-8)
endif()
