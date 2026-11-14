juce_add_console_app (KernfusionTests PRODUCT_NAME "KernfusionTests")

target_sources (KernfusionTests PRIVATE KernfusionTests.cpp ${KF_PLUGIN_SOURCES})
target_include_directories (KernfusionTests PRIVATE ../plugins/Kernfusion/Source)

target_compile_definitions (KernfusionTests PRIVATE
    ${MS_COMMON_DEFINITIONS}
    "JucePlugin_Name=\"NOVA Kernfusion\"")

target_link_libraries (KernfusionTests
    PRIVATE
        KernfusionFonts
        ms_core
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
