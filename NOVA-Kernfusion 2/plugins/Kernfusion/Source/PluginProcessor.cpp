#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String de (const char* s) { return juce::String::fromUTF8 (s); }
}

KernfusionProcessor::KernfusionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KernfusionParams", kf::params::createLayout()),
      reader (apvts)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);

    toneTarget = std::make_shared<const ms::TargetCurve> (kf::tone::makePreset ("balanced"));
    toneKey = "preset:balanced";
    lastTarget = kf::params::getValue (apvts, kf::params::target);

    // Gleiche Startwerte wie die Engine, damit Latenz schon vor prepareToPlay stimmt
    engine.prepare (48000.0, 512);
    setLatencySamples (engine.latency());
    startTimerHz (10);
}

KernfusionProcessor::~KernfusionProcessor()
{
    stopTimer();

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

//==============================================================================
void KernfusionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (32, samplesPerBlock);
    engine.prepare (sampleRate, maxBlock);
    engine.setParams (reader.read());
    paramsDirty = false;
    monoR.assign ((size_t) maxBlock, 0.0f);
    capture.prepare (sampleRate);
    inputAnalyser.prepare (sampleRate);
    outputAnalyser.prepare (sampleRate);
    setLatencySamples (engine.latency());
}

bool KernfusionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void KernfusionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (numIn == 0 || n == 0)
        return;

    if (paramsDirty.exchange (false))
        engine.setParams (reader.read());

    const bool stereo = numIn > 1 && buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;

    inputAnalyser.push (buffer, numIn);

    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        float* l = L + pos;
        float* r = R != nullptr ? R + pos : monoR.data();

        if (R == nullptr)
            std::copy (l, l + len, monoR.data());

        capture.push (l, r, len);
        engine.process (l, r, len);
    }

    outputAnalyser.push (buffer, numIn);
}

juce::AudioProcessorEditor* KernfusionProcessor::createEditor()
{
    return new KernfusionEditor (*this);
}

//==============================================================================
KernfusionProcessor::Phase KernfusionProcessor::getPhase() const noexcept
{
    if (job.isBusy()) return Phase::working;
    if (capture.getState() == kf::Capture::listening) return Phase::listening;
    if (listeningForMaster.load() && capture.getState() == kf::Capture::done) return Phase::working;   // startet gleich
    return Phase::idle;
}

float KernfusionProcessor::getPhaseProgress() const noexcept
{
    switch (getPhase())
    {
        case Phase::listening: return (float) capture.getProgress();
        case Phase::working:   return job.getProgress();
        case Phase::idle:      break;
    }

    return 0.0f;
}

void KernfusionProcessor::oneClick (bool listenAgain)
{
    if (job.isBusy())
        return;

    if (! listenAgain && capture.getState() == kf::Capture::done)
    {
        startJob (kf::AssistantJob::Mode::full);
        return;
    }

    capture.start();
    listeningForMaster = true;
    setStatus (de ("H\xc3\xb6re mit \xe2\x80\xa6 Spiel jetzt den lautesten Teil deines Songs ab (etwa 20 Sekunden)."));
}

void KernfusionProcessor::cancelListening()
{
    listeningForMaster = false;
    capture.clear();
    setStatus (de ("Mith\xc3\xb6ren abgebrochen."));
}

void KernfusionProcessor::requestLoudnessHold()
{
    holdDue = juce::Time::currentTimeMillis() + 400;
}

void KernfusionProcessor::startJob (kf::AssistantJob::Mode mode)
{
    std::vector<float> L, R;
    double fs = 48000;

    if (! capture.copyTo (L, R, fs))
        return;

    const auto target = kf::params::getValue (apvts, kf::params::target);

    if (job.start (mode, std::move (L), std::move (R), fs, reader.read(), target, getToneTarget()))
    {
        if (mode == kf::AssistantJob::Mode::full)
            setStatus (de ("Analysiere und mastere \xe2\x80\xa6"));
    }
}

void KernfusionProcessor::applyResult (const kf::AssistantJob::Result& r)
{
    if (r.error.isNotEmpty())
    {
        setStatus (r.error, 2);
        return;
    }

    calGain = r.calGain;
    calLufs = r.calLufs;

    if (r.mode == kf::AssistantJob::Mode::full)
    {
        kf::params::writeAll (apvts, r.params);
        {
            const std::scoped_lock l (lock);
            notes = r.notes;
        }
        setStatus (r.reachedTarget ? de ("Gemastert. Mit den Reglern feinjustieren, mit \xe2\x80\x9eVorher / Nachher\xe2\x80\x9c vergleichen.")
                                   : de ("Gemastert \xe2\x80\x93 das Lautheitsziel ist mit diesem Mix nur mit starker Verzerrung erreichbar."),
                   r.reachedTarget ? 1 : 2);
    }
    else
    {
        kf::params::setValue (apvts, "max_gain", r.params.max.gain);

        if (! r.reachedTarget)
            setStatus (de ("Ziel-Lautheit nicht ganz erreichbar \xe2\x80\x93 erreicht: ")
                       + juce::String (r.achievedLufs, 1).replaceCharacter ('.', ',') + " LUFS.", 2);
    }

    lastTarget = kf::params::getValue (apvts, kf::params::target);
}

void KernfusionProcessor::timerCallback()
{
    // Mithören fertig → mastern
    if (listeningForMaster.load() && capture.getState() == kf::Capture::done && ! job.isBusy())
    {
        listeningForMaster = false;

        if (capture.getSeconds() >= kf::Capture::minSeconds)
            startJob (kf::AssistantJob::Mode::full);
        else
            setStatus (de ("Zu wenig Musik aufgenommen. Starte die Wiedergabe und klick nochmal auf \xe2\x80\x9e") + "Ein Klick mastern" + de ("\xe2\x80\x9c."), 2);
    }

    if (auto r = job.takeResult())
        applyResult (*r);

    // Ziel-Lautheit geändert (Regler, Auswahl oder Automation)
    const float target = kf::params::getValue (apvts, kf::params::target);

    if (std::abs (target - lastTarget) > 0.01f)
    {
        lastTarget = target;

        if (std::isfinite (calLufs))
            kf::params::setValue (apvts, "max_gain", (float) juce::jlimit (0.0, 30.0, calGain + (target - calLufs)));

        requestLoudnessHold();
    }

    // Lautheit nach Änderungen wieder einmessen
    if (holdDue != 0 && juce::Time::currentTimeMillis() >= holdDue && ! job.isBusy())
    {
        holdDue = 0;
        if (capture.getState() == kf::Capture::done)
            startJob (kf::AssistantJob::Mode::calibrate);
    }

    // Referenztracks fertig analysiert
    std::optional<ms::ReferenceAnalysisJob::Result> ref;
    {
        const std::scoped_lock l (refLock);
        ref = std::move (refResult);
        refResult.reset();
    }

    if (ref.has_value())
    {
        if (ref->curve.has_value())
        {
            const auto name = ref->curve->name;
            setToneTarget (std::make_shared<const ms::TargetCurve> (std::move (*ref->curve)), "ref:" + name);
            setStatus (de ("Klangziel aus Referenzen gebaut: ") + name + de (". \xe2\x80\x9e") + "Ein Klick mastern" + de ("\xe2\x80\x9c wendet es an."), 1);
        }
        else
        {
            setStatus (ref->errors.isEmpty() ? de ("Keine Referenz verwertbar.") : ref->errors.joinIntoString (de (" \xc2\xb7 ")), 2);
        }
    }
}

std::vector<kf::AssistantJob::Note> KernfusionProcessor::getNotes() const
{
    const std::scoped_lock l (lock);
    return notes;
}

//==============================================================================
std::shared_ptr<const ms::TargetCurve> KernfusionProcessor::getToneTarget() const
{
    const std::scoped_lock l (lock);
    return toneTarget;
}

juce::String KernfusionProcessor::getToneKey() const
{
    const std::scoped_lock l (lock);
    return toneKey;
}

void KernfusionProcessor::setToneTarget (std::shared_ptr<const ms::TargetCurve> t, const juce::String& key)
{
    const std::scoped_lock l (lock);
    toneTarget = std::move (t);
    toneKey = key;
}

bool KernfusionProcessor::analyseReferences (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> audio;

    for (const auto& f : files)
        if (ms::isSupportedAudioFile (f))
            audio.add (f);

    if (audio.isEmpty())
        return false;

    const auto name = audio.size() == 1 ? audio.getFirst().getFileNameWithoutExtension() : juce::String (audio.size()) + " Referenzen";
    setStatus (de ("Analysiere Referenzen \xe2\x80\xa6"));

    return refJob.start (audio, name, [this] (ms::ReferenceAnalysisJob::Result r)
    {
        const std::scoped_lock l (refLock);
        refResult = std::move (r);
    });
}

void KernfusionProcessor::setPreview (int platform, float gainDb)
{
    previewPlatform = platform;
    engine.monitorDb = platform >= 0 ? gainDb : 0.0f;
}

KernfusionProcessor::Status KernfusionProcessor::getStatus() const
{
    const std::scoped_lock l (lock);
    return status;
}

void KernfusionProcessor::setStatus (const juce::String& text, int kind)
{
    const std::scoped_lock l (lock);
    status.text = text;
    status.kind = kind;
    ++status.serial;
}

//==============================================================================
void KernfusionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("KernfusionState");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree extra ("Extra");
    extra.setProperty ("waveColour", waveColour.load(), nullptr);
    extra.setProperty ("tab", lastTab.load(), nullptr);
    extra.setProperty ("calGain", calGain, nullptr);
    extra.setProperty ("calLufs", std::isfinite (calLufs) ? calLufs : -1000.0, nullptr);

    {
        const std::scoped_lock l (lock);
        extra.setProperty ("toneKey", toneKey, nullptr);
        if (toneTarget != nullptr)
            extra.setProperty ("toneJson", toneTarget->toJson(), nullptr);

        for (const auto& n : notes)
        {
            juce::ValueTree note ("Note");
            note.setProperty ("label", n.label, nullptr);
            note.setProperty ("text", n.text, nullptr);
            extra.appendChild (note, nullptr);
        }
    }

    root.appendChild (extra, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void KernfusionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("KernfusionState"))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    const auto extra = root.getChildWithName ("Extra");
    if (extra.isValid())
    {
        waveColour = juce::jlimit (0, 2, (int) extra.getProperty ("waveColour", 2));
        lastTab = juce::jlimit (0, 2, (int) extra.getProperty ("tab", 0));
        calGain = (double) extra.getProperty ("calGain", 0.0);
        const double cl = (double) extra.getProperty ("calLufs", -1000.0);
        calLufs = cl > -500.0 ? cl : -HUGE_VAL;

        std::shared_ptr<const ms::TargetCurve> curve;
        if (auto t = ms::TargetCurve::fromJson (extra.getProperty ("toneJson").toString()))
            curve = std::make_shared<const ms::TargetCurve> (std::move (*t));

        std::vector<kf::AssistantJob::Note> loaded;
        for (const auto& c : extra)
            if (c.hasType ("Note"))
                loaded.push_back ({ c.getProperty ("label").toString(), c.getProperty ("text").toString() });

        const std::scoped_lock l (lock);
        if (curve != nullptr)
        {
            toneTarget = curve;
            toneKey = extra.getProperty ("toneKey").toString();
        }
        notes = std::move (loaded);
    }

    lastTarget = kf::params::getValue (apvts, kf::params::target);
    paramsDirty = true;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KernfusionProcessor();
}
