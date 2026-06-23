cmake_minimum_required(VERSION 3.22)

project(TubeEmulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------------------
# Pull JUCE in automatically. No manual install / Projucer required.
# Pin a tag so builds are reproducible; bump it when you want a newer JUCE.
# ----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ----------------------------------------------------------------------------
# The plugin target. FORMATS AU builds the Audio Unit that GarageBand loads.
# Standalone gives you a runnable app for quick iteration outside a DAW.
# ----------------------------------------------------------------------------
juce_add_plugin(TubeEmulator
    COMPANY_NAME              "AndrewCordivari"
    PLUGIN_MANUFACTURER_CODE  Acrd          # 4 chars, exactly one uppercase
    PLUGIN_CODE               Tbem          # 4 chars, unique per plugin
    FORMATS                   AU Standalone
    PRODUCT_NAME              "Tube Emulator"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE          # installs the .component so GB sees it
)

juce_generate_juce_header(TubeEmulator)

target_sources(TubeEmulator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(TubeEmulator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(TubeEmulator
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
