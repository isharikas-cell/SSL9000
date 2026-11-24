cmake_minimum_required(VERSION 3.22)
project(SSL9000Console VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SSL9000Channel
  COMPANY_NAME "Independent Audio Tools"
  BUNDLE_ID "audio.independent.ssl9000channel"
  PLUGIN_MANUFACTURER_CODE IATl
  PLUGIN_CODE S9Ch
  PRODUCT_NAME "SSL9000 Channel"
  FORMATS VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(SSL9000Channel PRIVATE Source/Channel/PluginProcessor.cpp)
target_include_directories(SSL9000Channel PRIVATE Source/Shared)
target_compile_features(SSL9000Channel PRIVATE cxx_std_20)
target_link_libraries(SSL9000Channel PRIVATE juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(SSL9000Channel PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

juce_add_plugin(SSL9000Master
  COMPANY_NAME "Independent Audio Tools"
  BUNDLE_ID "audio.independent.ssl9000master"
  PLUGIN_MANUFACTURER_CODE IATl
  PLUGIN_CODE S9Ms
  PRODUCT_NAME "SSL9000 Master"
  FORMATS VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(SSL9000Master PRIVATE Source/Master/PluginProcessor.cpp)
target_include_directories(SSL9000Master PRIVATE Source/Shared)
target_compile_features(SSL9000Master PRIVATE cxx_std_20)
target_link_libraries(SSL9000Master PRIVATE juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(SSL9000Master PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)


include(CTest)
if(BUILD_TESTING)
  add_executable(ConsoleLinkRingTests Tests/ConsoleLinkRingTests.cpp)
  target_compile_features(ConsoleLinkRingTests PRIVATE cxx_std_20)
  add_test(NAME ConsoleLinkRingTests COMMAND ConsoleLinkRingTests)
endif()
