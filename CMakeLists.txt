cmake_minimum_required(VERSION 3.22)

project(AudioPluginSuite VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginCommon.cmake")

set(SUITE_PLUGINS
    Physicalize
    Fossil
    SessionMate
    Tactile
    HarmonyHUD
    Bricolage
    VowelMorph
)

foreach(_plugin IN LISTS SUITE_PLUGINS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${_plugin}/CMakeLists.txt")
        message(STATUS "PluginSuite: adding plugin ${_plugin}")
        add_subdirectory("plugins/${_plugin}")
    endif()
endforeach()
