cmake_minimum_required(VERSION 3.22)
project(SessionMate VERSION 1.0.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PluginCommon.cmake")

juce_add_plugin(SessionMate
    COMPANY_NAME "JamesStudio"
    PLUGIN_MANUFACTURER_CODE JmsS
    PLUGIN_CODE SmMt
    FORMATS VST3 Standalone
    PRODUCT_NAME "SessionMate"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(SessionMate PRIVATE
    src/SessionMateProcessor.cpp
    src/ui/SessionMateEditor.cpp
)

target_include_directories(SessionMate PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(SessionMate PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(SessionMate
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

suite_configure_plugin(
    TARGET SessionMate
    CLAP_ID "com.jamesstudio.sessionmate"
    CLAP_FEATURES instrument synthesizer
)

# Comprehensive C++20 Test Suite
juce_add_console_app(SessionMateTests
    PRODUCT_NAME "SessionMateTests"
)

target_sources(SessionMateTests PRIVATE
    tests/SessionMateTests.cpp
    src/SessionMateProcessor.cpp
    src/ui/SessionMateEditor.cpp
)

target_include_directories(SessionMateTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(SessionMateTests PRIVATE cxx_std_20)

target_compile_definitions(SessionMateTests PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    NOMINMAX
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(SessionMateTests PRIVATE /bigobj /utf-8)
endif()

target_link_libraries(SessionMateTests PRIVATE
    SuiteShared
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_dsp
)
