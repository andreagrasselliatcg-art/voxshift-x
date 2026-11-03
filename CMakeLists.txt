cmake_minimum_required(VERSION 3.22)

# Must be set before project(): lowest target supported by current Xcode.
# Covers Logic Pro X 10.4+ and every Ableton Live that runs on macOS 10.13 or later.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(VoxShiftX VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

# One plugin "flavour" per call. They all share the same source code.
function(voxshift_plugin target)
    cmake_parse_arguments(ARG "" "CODE;NAME;BUNDLE;AU_TYPE;MIDI" "FORMATS" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME                "Tune Talks"
        BUNDLE_ID                   ${ARG_BUNDLE}
        PLUGIN_MANUFACTURER_CODE    Tntk
        PLUGIN_CODE                 ${ARG_CODE}
        PRODUCT_NAME                "${ARG_NAME}"
        DESCRIPTION                 "Pitch and formant shifter"
        FORMATS                     ${ARG_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            ${ARG_MIDI}
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx "Pitch Shift"
        AU_MAIN_TYPE                ${ARG_AU_TYPE}
        AU_SANDBOX_SAFE             TRUE
    )

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
    )

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endfunction()

# VST3 (Ableton Live 10.1+, Windows and Mac) + Standalone: accepts MIDI notes.
voxshift_plugin(VoxShiftX
    CODE Vsfx NAME "VoxShift - X" BUNDLE com.tunetalks.voxshiftx
    AU_TYPE kAudioUnitType_Effect MIDI TRUE
    FORMATS VST3 Standalone)

# Audio Unit effect: insert on any audio track in every Logic Pro / Ableton Live for Mac.
voxshift_plugin(VoxShiftXAU
    CODE Vsfx NAME "VoxShift - X" BUNDLE com.tunetalks.voxshiftx.au
    AU_TYPE kAudioUnitType_Effect MIDI FALSE
    FORMATS AU)

# Audio Unit MIDI-controlled effect: play the pitch from MIDI notes in Logic.
voxshift_plugin(VoxShiftXMidiAU
    CODE Vsfm NAME "VoxShift - X MIDI" BUNDLE com.tunetalks.voxshiftx.midi
    AU_TYPE kAudioUnitType_MusicEffect MIDI TRUE
    FORMATS AU)
