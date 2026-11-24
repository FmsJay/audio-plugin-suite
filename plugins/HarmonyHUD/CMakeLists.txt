cmake_minimum_required(VERSION 3.22)
project(HarmonyHUD VERSION 1.0.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PluginCommon.cmake")

juce_add_plugin(HarmonyHUD
    COMPANY_NAME "PortableWorkstation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE PwSt
    PLUGIN_CODE Hhud
    FORMATS VST3 Standalone
    PRODUCT_NAME "HarmonyHUD"
)

target_sources(HarmonyHUD PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_include_directories(HarmonyHUD PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(HarmonyHUD
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
    TARGET HarmonyHUD
    CLAP_ID "com.portableworkstation.harmonyhud"
    CLAP_FEATURES analyzer utility
)

# Standalone console test target
add_executable(HarmonyHUDTests
    tests/HarmonyHUDTests.cpp
)

target_link_libraries(HarmonyHUDTests PRIVATE
    HarmonyHUD
    SuiteShared
)

target_compile_definitions(HarmonyHUDTests PRIVATE
    JUCE_STANDALONE_APPLICATION=0
)

if(MSVC)
    target_compile_options(HarmonyHUDTests PRIVATE /bigobj /utf-8)
endif()
