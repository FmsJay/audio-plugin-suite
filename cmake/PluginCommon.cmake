include_guard(GLOBAL)

cmake_minimum_required(VERSION 3.22)

# Resolve suite root regardless of whether invoked from root or plugins/<Name>
get_filename_component(PLUGIN_SUITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep third-party builds lean and collision-free
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_COPY_PLUGIN_AFTER_BUILD OFF CACHE BOOL "" FORCE)
set(CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Add JUCE 8 (isolated per build directory under _deps to prevent parallel collisions)
if(NOT TARGET juce::juce_core)
    if(NOT EXISTS "${PLUGIN_SUITE_ROOT}/third_party/JUCE/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at ${PLUGIN_SUITE_ROOT}/third_party/JUCE")
    endif()
    add_subdirectory(
        "${PLUGIN_SUITE_ROOT}/third_party/JUCE"
        "${CMAKE_BINARY_DIR}/_deps/JUCE"
        EXCLUDE_FROM_ALL
    )
endif()

# Add clap-juce-extensions
if(NOT TARGET clap_juce_extensions)
    if(EXISTS "${PLUGIN_SUITE_ROOT}/third_party/clap-juce-extensions/CMakeLists.txt")
        add_subdirectory(
            "${PLUGIN_SUITE_ROOT}/third_party/clap-juce-extensions"
            "${CMAKE_BINARY_DIR}/_deps/clap-juce-extensions"
            EXCLUDE_FROM_ALL
        )
    else()
        message(WARNING "clap-juce-extensions not found at ${PLUGIN_SUITE_ROOT}/third_party/clap-juce-extensions; CLAP targets will be skipped.")
    endif()
endif()

# Shared header-only DSP + UI utility library
if(NOT TARGET SuiteShared)
    add_library(SuiteShared INTERFACE)
    target_include_directories(SuiteShared INTERFACE
        "${PLUGIN_SUITE_ROOT}/shared/include"
    )
    target_compile_features(SuiteShared INTERFACE cxx_std_20)
    target_compile_definitions(SuiteShared INTERFACE
        NOMINMAX
        _USE_MATH_DEFINES
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        target_compile_options(SuiteShared INTERFACE /bigobj /utf-8)
    endif()
endif()

# Configure a JUCE plugin target with suite-wide C++20/MSVC/JUCE flags + optional CLAP wrapper
function(suite_configure_plugin)
    set(options NO_CLAP)
    set(oneValueArgs TARGET CLAP_ID)
    set(multiValueArgs CLAP_FEATURES)
    cmake_parse_arguments(SCP "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SCP_TARGET)
        message(FATAL_ERROR "suite_configure_plugin: TARGET argument is required")
    endif()

    target_compile_features(${SCP_TARGET} PUBLIC cxx_std_20)
    target_compile_definitions(${SCP_TARGET} PUBLIC
        NOMINMAX
        _USE_MATH_DEFINES
        _CRT_SECURE_NO_WARNINGS
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    if(MSVC)
        target_compile_options(${SCP_TARGET} PUBLIC /bigobj /utf-8)
    endif()

    target_link_libraries(${SCP_TARGET} PUBLIC SuiteShared)

    if(NOT SCP_NO_CLAP AND SCP_CLAP_ID)
        if(COMMAND clap_juce_extensions_plugin)
            if(SCP_CLAP_FEATURES)
                clap_juce_extensions_plugin(
                    TARGET ${SCP_TARGET}
                    CLAP_ID "${SCP_CLAP_ID}"
                    CLAP_FEATURES ${SCP_CLAP_FEATURES}
                )
            else()
                clap_juce_extensions_plugin(
                    TARGET ${SCP_TARGET}
                    CLAP_ID "${SCP_CLAP_ID}"
                    CLAP_FEATURES audio-effect
                )
            endif()

            string(REPLACE " " "_" _clap_target "${SCP_TARGET}_CLAP")
            if(TARGET "${_clap_target}" AND MSVC)
                target_compile_options("${_clap_target}" PRIVATE /bigobj /utf-8)
            endif()
        else()
            message(WARNING "clap_juce_extensions_plugin command unavailable; skipping CLAP target for ${SCP_TARGET}")
        endif()
    endif()
endfunction()
