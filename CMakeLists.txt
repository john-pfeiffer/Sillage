cmake_minimum_required(VERSION 3.24)

project(Sillage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build universal binaries on macOS (Intel + Apple Silicon)
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Sillage
    COMPANY_NAME "Elan Vital Studios"
    COMPANY_WEBSITE "https://elanvitalstudios.com"
    BUNDLE_ID com.elanvitalstudios.sillage
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Evsy
    PLUGIN_CODE Slge
    FORMATS VST3 AU Standalone
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect
    PRODUCT_NAME "Sillage")

set(SILLAGE_SOURCES
    src/CurveEditor.cpp
    src/FeedbackPath.cpp
    src/GrainEngine.cpp
    src/LifetimeCurves.cpp
    src/MainPage.cpp
    src/ModPanel.cpp
    src/Modulation.cpp
    src/Modulators.cpp
    src/OnsetDetector.cpp
    src/Parameters.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Randomize.cpp
    src/SectionPage.cpp
    src/SillageLookAndFeel.cpp
    src/Types.cpp
    src/Visuals.cpp
    src/Wake.cpp)

target_sources(Sillage PRIVATE ${SILLAGE_SOURCES})
target_include_directories(Sillage PRIVATE src)

target_compile_definitions(Sillage PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Sillage
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Tests (headless render harness) ----------------------------------------
enable_testing()

juce_add_console_app(SillageTests PRODUCT_NAME "SillageTests")
target_sources(SillageTests PRIVATE
    tests/SillageTests.cpp
    ${SILLAGE_SOURCES})
target_include_directories(SillageTests PRIVATE src)
target_compile_definitions(SillageTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(SillageTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME SillageTests COMMAND SillageTests)

# ---- UI snapshot tool (development only) ------------------------------------
# Renders every page of the editor to PNG without a host, so a layout change can
# be looked at: run it under xvfb-run on a headless machine.
option(SILLAGE_BUILD_SNAPSHOT_TOOL "Build the editor snapshot tool" OFF)
if(SILLAGE_BUILD_SNAPSHOT_TOOL)
    juce_add_console_app(SillageSnapshot PRODUCT_NAME "SillageSnapshot")
    target_sources(SillageSnapshot PRIVATE
        tools/Snapshot.cpp
        ${SILLAGE_SOURCES})
    target_include_directories(SillageSnapshot PRIVATE src)
    target_compile_definitions(SillageSnapshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SillageSnapshot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

# ---- Measurement probe (development only) -----------------------------------
# Renders a phrase through the processor and prints levels, tail lengths and
# Randomize's yield: what the Types, the Decay model and Randomize were tuned
# against. See the usage notes at the top of tools/Probe.cpp.
option(SILLAGE_BUILD_PROBE "Build the measurement probe" OFF)
if(SILLAGE_BUILD_PROBE)
    juce_add_console_app(SillageProbe PRODUCT_NAME "SillageProbe")
    target_sources(SillageProbe PRIVATE
        tools/Probe.cpp
        ${SILLAGE_SOURCES})
    target_include_directories(SillageProbe PRIVATE src)
    target_compile_definitions(SillageProbe PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(SillageProbe PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
