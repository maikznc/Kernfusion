#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/UiState.h"
#include "ui/MiniMeter.h"
#include "ui/OneClickView.h"
#include "ui/ProView.h"
#include "ui/LoudnessView.h"

class KernfusionEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit KernfusionEditor (KernfusionProcessor&);
    ~KernfusionEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    /** Holt neue Daten und zeichnet neu (30 Hz). Öffentlich für Tests/Screenshots. */
    void refresh();
    void showTab (int);

private:
    void timerCallback() override { refresh(); }
    void chooseReferences();
    void importTarget (const juce::File&);

    KernfusionProcessor& proc;
    kf::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    kf::ui::UiState ui;

    kf::ui::MiniMeter mini;
    std::array<juce::TextButton, 3> tabs;
    juce::TextButton abButton;
    kf::ui::OneClickView oneClick;
    kf::ui::ProView pro;
    kf::ui::LoudnessView loud;

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<kf::WaveColumn> colScratch = std::vector<kf::WaveColumn> (2048);
    juce::Rectangle<int> headerArea, statusArea, brandArea;
    int tab = 0;
    bool dragActive = false;
    uint32_t lastStatusSerial = 0;
    juce::int64 statusTime = 0;
};
