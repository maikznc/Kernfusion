#include "PluginEditor.h"

using namespace kf::ui;

KernfusionEditor::KernfusionEditor (KernfusionProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      mini (ui, p.waveColour, [&p] { return kf::params::getValue (p.apvts, "max_ceil"); }),
      oneClick (p, ui), pro (p, ui), loud (p, ui)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (mini);
    mini.onResetMeters = [this]
    {
        proc.engine.resetRequested = true;
        proc.outputAnalyser.resetIntegrated();
        proc.inputAnalyser.resetIntegrated();
    };

    const char* names[] = { "Ein Klick", "Profi", "Industrielautst\xc3\xa4rke" };
    for (int i = 0; i < 3; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (de (names[i]));
        t.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        t.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (t);
    }

    abButton.setButtonText ("Vorher / Nachher");
    abButton.setTooltip (de ("Original zum Vergleich h\xc3\xb6ren \xe2\x80\x93 lautheitsangeglichen, damit lauter nicht automatisch besser klingt"));
    abButton.onClick = [this] { proc.setBypass (! proc.isBypassed()); };
    addAndMakeVisible (abButton);

    addChildComponent (oneClick);
    addChildComponent (pro);
    addChildComponent (loud);
    oneClick.onChooseReferences = [this] { chooseReferences(); };

    setResizable (true, true);
    setResizeLimits (1100, 760, 2000, 1300);
    setSize (1320, 880);
    showTab (proc.lastTab.load());
    refresh();
    startTimerHz (30);
}

KernfusionEditor::~KernfusionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KernfusionEditor::showTab (int t)
{
    tab = juce::jlimit (0, 2, t);
    proc.lastTab = tab;

    for (int i = 0; i < 3; ++i)
        tabs[(size_t) i].setToggleState (i == tab, juce::dontSendNotification);

    oneClick.setVisible (tab == 0);
    pro.setVisible (tab == 1);
    loud.setVisible (tab == 2);
}

void KernfusionEditor::refresh()
{
    auto& e = proc.engine;
    auto& m = e.meters;

    // Waveform
    int got = 0;
    do
    {
        got = e.readColumns (colScratch.data(), (int) colScratch.size());
        for (int i = 0; i < got; ++i)
        {
            ui.cols[(size_t) ui.head] = colScratch[(size_t) i];
            ui.head = (ui.head + 1) % UiState::numCols;
            ui.count = juce::jmin (UiState::numCols, ui.count + 1);
        }
        ui.framesSinceAudio = got > 0 ? 0 : ui.framesSinceAudio + 1;
    } while (got == (int) colScratch.size());

    ui.signal = ui.framesSinceAudio < 15;

    const int pairs = e.readScope (ui.scope.data(), (int) ui.scope.size() / 2);
    if (pairs > 0) ui.scopeCount = pairs;
    else if (! ui.signal) ui.scopeCount = 0;

    ui.outShort = m.outShort; ui.outMomentary = m.outMomentary; ui.outIntegrated = m.outIntegrated;
    ui.outTruePeak = m.outTruePeak; ui.outLra = m.outLra; ui.outMaxShort = m.outMaxShort;
    ui.inIntegrated = m.inIntegrated; ui.measuredSeconds = m.measuredSeconds; ui.correlation = m.correlation;

    ui.limiterGr = e.takeLimiterGr();
    for (int b = 0; b < 3; ++b)
    {
        const float v = e.takeBandGr (b);
        ui.bandGr[(size_t) b] = v > ui.bandGr[(size_t) b] ? v : ui.bandGr[(size_t) b] * 0.85f + v * 0.15f;
    }
    ui.grHistory[(size_t) ui.grHead] = ui.limiterGr;
    ui.grHead = (ui.grHead + 1) % (int) ui.grHistory.size();

    // Warnhinweis im Mini-Meter
    juce::String warn;
    if (proc.getPreview() >= 0) warn = de ("Plattform-Vorschau aktiv \xe2\x80\x93 vor dem Bounce ausschalten");
    else if (proc.isBypassed()) warn = de ("Vorher (Original) \xe2\x80\x93 vor dem Bounce ausschalten");
    mini.setWarning (warn);
    mini.repaint();

    abButton.setToggleState (proc.isBypassed(), juce::dontSendNotification);
    abButton.setButtonText (proc.isBypassed() ? "Vorher (pegelgleich)" : "Vorher / Nachher");

    const auto st = proc.getStatus();
    if (st.serial != lastStatusSerial)
    {
        lastStatusSerial = st.serial;
        statusTime = juce::Time::currentTimeMillis();
    }

    if (tab == 0) oneClick.tick();
    if (tab == 1) pro.tick();
    if (tab == 2) loud.tick();

    repaint (statusArea);
}

void KernfusionEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);

    // Marke
    auto b = brandArea.toFloat();
    g.setGradientFill (plasmaGradient (b.withWidth (200.0f)));
    g.setFont (caps (27.0f, 2, 0.2f));
    g.drawText ("KERNFUSION", b.removeFromTop (30.0f), juce::Justification::bottomLeft);
    g.setColour (C::faint);
    g.setFont (caps (12.5f, 1, 0.12f));
    g.drawText (de ("NOVA \xc2\xb7 MASTERING"), b, juce::Justification::topLeft);

    // Statuszeile
    const auto st = proc.getStatus();
    auto s = statusArea.toFloat();
    const auto phase = proc.getPhase();
    const bool busy = phase != KernfusionProcessor::Phase::idle || proc.isAnalysingReferences();

    if (busy)
    {
        auto bar = s.removeFromLeft (220.0f).withSizeKeepingCentre (220.0f, 4.0f);
        const float pr = proc.isAnalysingReferences() ? proc.getReferenceProgress() : proc.getPhaseProgress();
        g.setColour (C::raised);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (C::plasma);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, pr)), 2.0f);
        s.removeFromLeft (12.0f);
    }

    const bool fresh = juce::Time::currentTimeMillis() - statusTime < (st.kind == 2 ? 12000 : 8000);
    g.setFont (label (14.5f));
    g.setColour (st.kind == 2 ? C::bad : (st.kind == 1 ? C::ok : C::dim));

    if (st.text.isNotEmpty() && (fresh || busy))
        g.drawText (st.text, s, juce::Justification::centredLeft, true);
    else
    {
        g.setColour (C::faint);
        g.drawText (de ("Tipp: Referenztracks oder Leitstern-Zielkurven (.json) einfach ins Fenster ziehen."), s, juce::Justification::centredLeft, true);
    }
}

void KernfusionEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragActive)
        return;

    auto r = getLocalBounds().toFloat().reduced (14.0f);
    g.setColour (C::bg.withAlpha (0.88f));
    g.fillRoundedRectangle (r, 14.0f);
    juce::Path outline, dashed;
    outline.addRoundedRectangle (r, 14.0f);
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (C::plasma);
    g.fillPath (dashed);
    g.setColour (C::fg);
    g.setFont (label (24.0f, 2));
    g.drawText (de ("Loslassen: Referenztracks werden zum Klangziel"), r.withTrimmedBottom (40.0f), juce::Justification::centred);
    g.setColour (C::dim);
    g.setFont (label (16.0f));
    g.drawText (de ("Leitstern-Zielkurven (.json) werden importiert"), r.withTrimmedTop (40.0f), juce::Justification::centred);
}

void KernfusionEditor::resized()
{
    auto r = getLocalBounds();
    mini.setBounds (r.removeFromTop (92));

    auto content = r.reduced (16, 0);
    content.removeFromTop (12);
    headerArea = content.removeFromTop (48);
    auto h = headerArea;
    brandArea = h.removeFromLeft (230);
    h.removeFromLeft (10);
    auto tabRow = h.removeFromLeft (440).withSizeKeepingCentre (440, 40);
    tabs[0].setBounds (tabRow.removeFromLeft (120));
    tabs[1].setBounds (tabRow.removeFromLeft (100));
    tabs[2].setBounds (tabRow);
    abButton.setBounds (h.removeFromRight (190).withSizeKeepingCentre (190, 36));

    content.removeFromTop (12);
    statusArea = content.removeFromBottom (34);
    content.removeFromBottom (4);

    oneClick.setBounds (content);
    pro.setBounds (content);
    loud.setBounds (content);
}

//==============================================================================
bool KernfusionEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (ms::isSupportedAudioFile (juce::File (f)) || juce::File (f).hasFileExtension ("json"))
            return true;
    return false;
}

void KernfusionEditor::fileDragEnter (const juce::StringArray&, int, int) { dragActive = true; repaint(); }
void KernfusionEditor::fileDragExit (const juce::StringArray&)          { dragActive = false; repaint(); }

void KernfusionEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();
    juce::Array<juce::File> audio;

    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension ("json")) { importTarget (file); return; }
        audio.add (file);
    }

    if (! proc.analyseReferences (audio))
        proc.setStatus (de ("Keine unterst\xc3\xbctzten Audiodateien (WAV, AIFF, FLAC, MP3) oder es l\xc3\xa4uft schon eine Analyse."), 2);
}

void KernfusionEditor::chooseReferences()
{
    chooser = std::make_unique<juce::FileChooser> (de ("Referenztracks ausw\xc3\xa4hlen"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory), ms::getSupportedAudioWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& c)
                          {
                              if (! c.getResults().isEmpty() && ! proc.analyseReferences (c.getResults()))
                                  proc.setStatus (de ("Diese Dateien k\xc3\xb6nnen nicht analysiert werden."), 2);
                          });
}

void KernfusionEditor::importTarget (const juce::File& file)
{
    auto curve = ms::TargetCurve::loadFromFile (file);

    if (! curve.has_value())
    {
        proc.setStatus (file.getFileName() + de (" ist keine g\xc3\xbcltige Leitstern-Zielkurve."), 2);
        return;
    }

    // in den gemeinsamen Zielordner übernehmen (dann ist sie auch in Leitstern verfügbar)
    const auto dest = ms::TargetCurve::getUserTargetsFolder().getChildFile (juce::File::createLegalFileName (curve->name.isNotEmpty() ? curve->name : file.getFileNameWithoutExtension()) + ".json");
    if (dest != file)
        curve->saveToFile (dest);

    curve->name = dest.getFileNameWithoutExtension();
    proc.setToneTarget (std::make_shared<const ms::TargetCurve> (std::move (*curve)), "file:" + dest.getFileNameWithoutExtension());
    proc.setStatus (de ("Leitstern-Ziel importiert: ") + dest.getFileNameWithoutExtension(), 1);
}
