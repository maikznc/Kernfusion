juce_add_binary_data (KernfusionFonts
    NAMESPACE KernfusionFonts
    SOURCES
        Resources/fonts/BarlowSemiCondensed-Medium.ttf
        Resources/fonts/BarlowSemiCondensed-SemiBold.ttf
        Resources/fonts/BarlowSemiCondensed-Bold.ttf
        Resources/fonts/IBMPlexMono-Medium.ttf
        Resources/fonts/IBMPlexMono-SemiBold.ttf)

juce_add_plugin (Kernfusion
    PRODUCT_NAME                "NOVA Kernfusion"
    COMPANY_NAME                "NOVA"
    BUNDLE_ID                   "com.nova.kernfusion"
    PLUGIN_MANUFACTURER_CODE    Nova
    PLUGIN_CODE                 Kfus
    FORMATS                     ${MS_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Mastering Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${MS_COPY_AFTER_BUILD})

# Quelltexte – werden auch von den Tests benutzt
set (KF_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Dsp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assistant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/MiniMeter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/OneClickView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/ProView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/LoudnessView.cpp
    CACHE INTERNAL "Kernfusion-Quelltexte")

target_sources (Kernfusion PRIVATE ${KF_PLUGIN_SOURCES})
target_include_directories (Kernfusion PRIVATE Source)
target_compile_definitions (Kernfusion PUBLIC ${MS_COMMON_DEFINITIONS})

target_link_libraries (Kernfusion
    PRIVATE
        KernfusionFonts
        ms_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
