cmake_minimum_required(VERSION 3.22)
project(VowelMorph VERSION 1.0.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PluginCommon.cmake")

juce_add_plugin(VowelMorph
    COMPANY_NAME "PortableWorkstation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE PwSt
    PLUGIN_CODE VwMp
    FORMATS VST3 Standalone
    PRODUCT_NAME "VowelMorph"
)

target_sources(VowelMorph PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_include_directories(VowelMorph PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(VowelMorph
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
    TARGET VowelMorph
    CLAP_ID "com.portableworkstation.vowelmorph"
    CLAP_FEATURES audio-effect filter
)

# Standalone console test target
add_executable(VowelMorphTests
    tests/VowelMorphTests.cpp
)

target_link_libraries(VowelMorphTests PRIVATE
    VowelMorph
    SuiteShared
)

target_compile_definitions(VowelMorphTests PRIVATE
    JUCE_STANDALONE_APPLICATION=0
)

if(MSVC)
    target_compile_options(VowelMorphTests PRIVATE /bigobj /utf-8)
endif()
