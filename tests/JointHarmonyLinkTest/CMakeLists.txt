cmake_minimum_required(VERSION 3.22)

project(JointHarmonyLinkTest VERSION 1.0.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PluginCommon.cmake")

juce_add_console_app(JointHarmonyLinkTest
    PRODUCT_NAME "JointHarmonyLinkTest"
)

target_sources(JointHarmonyLinkTest PRIVATE
    main.cpp
)

target_compile_definitions(JointHarmonyLinkTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_WINRT_MIDI=0
)

target_link_libraries(JointHarmonyLinkTest PRIVATE
    SuiteShared
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_gui_basics
    juce::juce_core
    juce::juce_events
)
