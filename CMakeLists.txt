cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# VULKAN AUDIO — NITRO-430 (Compact VCA Dynamics Processor)
# Cross-Platform CMake Build for Windows (VST3/Standalone) & macOS (VST3/AU/Standalone)
# ==============================================================================
project(VulkanAudioNitro430 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Universal Binary (Apple Silicon arm64 + Intel x86_64) automatically on macOS
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS Universal Binary" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Customize Brand, Model & Author Name at compile time if desired:
set(PLUGIN_BRAND_TITLE "VULKAN AUDIO" CACHE STRING "Displayed Brand Name on Faceplate")
set(PLUGIN_MODEL_TITLE "NITRO-430"    CACHE STRING "Displayed Model Name on Faceplate")
set(PLUGIN_AUTHOR_NAME "Mykyta Shchur" CACHE STRING "Author Name in About Window")

# Automatically download JUCE 8.0.4 via CMake FetchContent (no manual SDK install needed)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# On macOS build VST3, AU, and Standalone; on Windows/Linux build VST3 and Standalone
if(APPLE)
    set(NITRO_FORMATS AU VST3 Standalone)
else()
    set(NITRO_FORMATS VST3 Standalone)
endif()

juce_add_plugin(VulkanNitro430
    COMPANY_NAME                "${PLUGIN_BRAND_TITLE}"
    COMPANY_COPYRIGHT           "Copyright (c) 2026 ${PLUGIN_AUTHOR_NAME}"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Vulk
    PLUGIN_CODE                 N430
    FORMATS                     ${NITRO_FORMATS}
    PRODUCT_NAME                "Vulkan Audio NITRO-430"
)

# Generate <JuceHeader.h> so single-file ValleyPeopleDynaMite.cpp compiles out of the box
juce_generate_juce_header(VulkanNitro430)

# Support both root-level ValleyPeopleDynaMite.cpp and src/ValleyPeopleDynaMite.cpp
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ValleyPeopleDynaMite.cpp")
    target_sources(VulkanNitro430 PRIVATE src/ValleyPeopleDynaMite.cpp)
else()
    target_sources(VulkanNitro430 PRIVATE ValleyPeopleDynaMite.cpp)
endif()

target_compile_definitions(VulkanNitro430
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        PLUGIN_BRAND_TITLE="${PLUGIN_BRAND_TITLE}"
        PLUGIN_MODEL_TITLE="${PLUGIN_MODEL_TITLE}"
        PLUGIN_FULL_NAME="${PLUGIN_BRAND_TITLE} ${PLUGIN_MODEL_TITLE}"
        PLUGIN_AUTHOR_NAME="${PLUGIN_AUTHOR_NAME}"
)

target_link_libraries(VulkanNitro430
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
