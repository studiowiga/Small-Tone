cmake_minimum_required(VERSION 3.22)

# macOS : un seul binaire universel (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(SmallTone VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SMALLTONE_STANDALONE "Compile aussi une application autonome (test sans DAW)" OFF)

# Compilation locale avec un JUCE déjà présent :  cmake -B build -DJUCE_DIR=/chemin/vers/JUCE
if(DEFINED JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    ON)
  FetchContent_MakeAvailable(JUCE)
endif()

if(SMALLTONE_STANDALONE)
  set(SMALLTONE_FORMATS VST3 Standalone)
else()
  set(SMALLTONE_FORMATS VST3)
endif()

# visuel, calques lumineux et police intégrés au plugin (rien à installer à côté)
juce_add_binary_data(SmallToneAssets SOURCES
  background.jpg
  glow_rate.png
  glow_foot.png
  glow_status.png
  toggle_up.png
  toggle_down.png
  toggle_mid.png
  label_sync.png
  label_sync_glow.png
  SmallToneSans.ttf)

juce_add_plugin(SmallTone
  COMPANY_NAME               "Studio Wiga"
  PLUGIN_MANUFACTURER_CODE   Wiga
  PLUGIN_CODE                SmTn
  FORMATS                    ${SMALLTONE_FORMATS}
  VST3_CATEGORIES            Fx Modulation
  BUNDLE_ID                  com.studiowiga.smalltone
  IS_SYNTH                   FALSE
  NEEDS_MIDI_INPUT           TRUE
  NEEDS_MIDI_OUTPUT          FALSE
  IS_MIDI_EFFECT             FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  PRODUCT_NAME               "Small Tone"
  COPY_PLUGIN_AFTER_BUILD    FALSE)

target_sources(SmallTone PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp)

if(MSVC)
  target_compile_options(SmallTone PRIVATE /utf-8)
endif()

target_compile_definitions(SmallTone PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_link_libraries(SmallTone
  PRIVATE
    SmallToneAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
