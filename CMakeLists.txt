cmake_minimum_required(VERSION 3.22)
project(SSL9000Console VERSION 0.2.0 LANGUAGES CXX)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

function(add_ssl9000_plugin target product plugin_code source_file)
  juce_add_plugin(${target}
    COMPANY_NAME "Independent Audio Tools"
    BUNDLE_ID "audio.independent.${target}"
    PLUGIN_MANUFACTURER_CODE IATl
    PLUGIN_CODE ${plugin_code}
    PRODUCT_NAME "${product}"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(${target} PRIVATE ${source_file})
  target_include_directories(${target} PRIVATE Source/Shared)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_dsp)
  target_compile_definitions(${target} PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endfunction()

add_ssl9000_plugin(SSL9000Channel "SSL9000 Channel" S9Ch Source/Channel/PluginProcessor.cpp)
add_ssl9000_plugin(SSL9000Master "SSL9000 Master" S9Ms Source/Master/PluginProcessor.cpp)
