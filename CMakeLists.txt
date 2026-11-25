cmake_minimum_required(VERSION 3.22)
# must come before project(), or Xcode stamps the runner's own macOS version as the minimum
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")
project(Pressao VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
 set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: nothing extra to install
 add_compile_options(/utf-8)   # sources have "PRESSÃO" in UTF-8
endif()

find_package(JUCE CONFIG QUIET)
if(NOT JUCE_FOUND)
 include(FetchContent)
 FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12)
 FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(pressao_assets SOURCES
 Assets/Fonts/ArchivoBlack-Regular.ttf Assets/Fonts/Inter-Bold.ttf Assets/Fonts/Inter-Regular.ttf)
set_target_properties(pressao_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(PRESSAO_SOURCES
 Source/Presets.cpp Source/Presets.h
 Source/Engine.cpp Source/Engine.h
 Source/Processor.cpp Source/Processor.h
 Source/Editor.cpp Source/Editor.h)

set(PRESSAO_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_DIRECT2D=0 JUCE_VST3_CAN_REPLACE_VST2=0
 JUCE_DISPLAY_SPLASH_SCREEN=0)

set(formats VST3)
if(APPLE)
 list(APPEND formats AU)
endif()

juce_add_plugin(Pressao
 COMPANY_NAME "Audio Makers"
 BUNDLE_ID "com.audiomakers.pressao"
 PLUGIN_MANUFACTURER_CODE AuMk
 PLUGIN_CODE Prss
 ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon-1024.png"
 FORMATS ${formats}
 PRODUCT_NAME "Pressao"
 VST3_CATEGORIES Fx Distortion
 AU_MAIN_TYPE kAudioUnitType_Effect
 IS_SYNTH FALSE
 NEEDS_MIDI_INPUT FALSE
 NEEDS_MIDI_OUTPUT FALSE
 VST3_AUTO_MANIFEST FALSE
 COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(Pressao PRIVATE ${PRESSAO_SOURCES})
target_include_directories(Pressao PRIVATE Source)
target_compile_definitions(Pressao PUBLIC ${PRESSAO_DEFINITIONS})
target_link_libraries(Pressao PRIVATE pressao_assets juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
 PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Tests: audio checks for every preset and knob position
# (+ window snapshots with PRESSAO_SNAPSHOT_DIR, demo WAVs with PRESSAO_DEMO_DIR + PRESSAO_DEMO_INPUT)
juce_add_console_app(pressao_tests PRODUCT_NAME "Pressao Tests")
target_sources(pressao_tests PRIVATE Tests/Tests.cpp ${PRESSAO_SOURCES})
target_include_directories(pressao_tests PRIVATE Source)
target_compile_definitions(pressao_tests PRIVATE ${PRESSAO_DEFINITIONS} JucePlugin_VersionString="1.0.0"
 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(pressao_tests PRIVATE pressao_assets juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
 PUBLIC juce::juce_recommended_config_flags)

enable_testing()
add_test(NAME pressao_audio_tests COMMAND pressao_tests)
