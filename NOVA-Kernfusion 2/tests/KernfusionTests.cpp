/*  Tests für NOVA Kernfusion.

    KernfusionTests                       → alle Tests
    KernfusionTests --snapshot <ordner>   → zusätzlich Screenshots aller Ansichten
*/

#include <ms_core/ms_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>
#include <thread>

namespace
{
    int failures = 0;
    constexpr double fs = 48000.0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what.toRawUTF8() << std::endl;
        if (! ok) ++failures;
    }

    juce::String f2 (double v, int d = 2) { return juce::String (v, d); }

    std::vector<float> sine (double freq, double amp, double seconds, double phase = 0.0)
    {
        std::vector<float> d ((size_t) (fs * seconds));
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / fs + phase));
        return d;
    }

    std::vector<float> pink (double seconds, uint32_t seed, float gain)
    {
        std::vector<float> d ((size_t) (fs * seconds));
        uint32_t s = seed;
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (auto& v : d)
        {
            s = s * 1664525u + 1013904223u;
            const float w = (float) ((double) s / 4294967296.0 * 2.0 - 1.0);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.969f * b2 + w * 0.153852f;
            b3 = 0.8665f * b3 + w * 0.3104856f; b4 = 0.55f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.016898f;
            v = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f * gain;
            b6 = w * 0.115926f;
        }
        return d;
    }

    void shelf (std::vector<float>& d, bool low, double f, double db)
    {
        kf::Biquad bq;
        bq.c = kf::makeCoeffs (low ? kf::FilterType::lowshelf : kf::FilterType::highshelf, fs, f, 0.71, db);
        for (auto& v : d) v = (float) bq.L (v);
    }

    /** „Musik-ähnliches“ Testsignal: rosa Rauschen mit Klangfarbe und Kick alle 0,5 s. */
    void song (std::vector<float>& L, std::vector<float>& R, double seconds, double lowDb, double highDb, float gain)
    {
        L = pink (seconds, 11, gain); R = pink (seconds, 12, gain);
        for (auto* d : { &L, &R }) { shelf (*d, true, 100, lowDb); shelf (*d, false, 6000, highDb); }
        const int kick = (int) (0.12 * fs), step = (int) (0.5 * fs);
        for (int p = 0; p + kick < (int) L.size(); p += step)
            for (int i = 0; i < kick; ++i)
            {
                const double t = i / fs;
                const float k = (float) (std::sin (2 * juce::MathConstants<double>::pi * 55 * t) * std::exp (-t * 30) * gain * 1.2);
                L[(size_t) (p + i)] += k; R[(size_t) (p + i)] += k;
            }
    }

    double rmsDb (const std::vector<float>& d, size_t from = 0)
    {
        double s = 0;
        for (size_t i = from; i < d.size(); ++i) s += (double) d[i] * d[i];
        return 10.0 * std::log10 (s / (double) (d.size() - from) + 1e-30);
    }

    void render (const kf::ChainParams& p, const std::vector<float>& L, const std::vector<float>& R, std::vector<float>& oL, std::vector<float>& oR)
    {
        kf::renderOffline (p, fs, L.data(), R.data(), (int) L.size(), oL, oR);
    }

    kf::ChainParams neutral()
    {
        auto p = kf::ChainParams::defaults();
        p.max.on = false;
        return p;
    }

    //==============================================================================
    void testLoudness()
    {
        std::cout << "\nLautheit (ITU-R BS.1770)\n";
        auto s = sine (1000, 1.0, 10);
        auto m = kf::measure (s.data(), s.data(), (int) s.size(), fs);
        check (std::abs (m.integrated) < 0.1, "1 kHz, 0 dBFS stereo -> " + f2 (m.integrated) + " LUFS (Soll 0,0)");

        auto q = sine (1000, std::pow (10.0, -23.0 / 20.0), 10);
        auto mq = kf::measure (q.data(), q.data(), (int) q.size(), fs);
        check (std::abs (mq.integrated + 23.0) < 0.1, "1 kHz, -23 dBFS stereo -> " + f2 (mq.integrated) + " LUFS (Soll -23,0)");

        auto ip = sine (fs / 4, 1.0, 2, juce::MathConstants<double>::pi / 4);
        auto mi = kf::measure (ip.data(), ip.data(), (int) ip.size(), fs);
        check (std::abs (mi.samplePeakDb + 3.01) < 0.05 && std::abs (mi.truePeakDb) < 0.3,
               "Intersample-Peak: Sample " + f2 (mi.samplePeakDb) + " dB, True Peak " + f2 (mi.truePeakDb) + " dBTP (Soll ~0)");

        // 44,1 kHz: K-Gewichtung muss sich anpassen
        kf::LoudnessMeter lm;
        lm.prepare (44100.0);
        for (int i = 0; i < 44100 * 10; ++i) { const double v = std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / 44100.0); lm.push (v, v); }
        check (std::abs (lm.integrated()) < 0.1, "1 kHz bei 44,1 kHz -> " + f2 (lm.integrated()) + " LUFS");
    }

    void testNeutral()
    {
        std::cout << "\nNeutralstellung ist transparent (inkl. Ueberabtastung)\n";
        for (double f : { 40.0, 1000.0, 12000.0 })
        {
            auto s = sine (f, 0.25, 1.0);
            std::vector<float> oL, oR;
            render (neutral(), s, s, oL, oR);
            const double d = rmsDb (oL, (size_t) fs / 2) - rmsDb (s, (size_t) fs / 2);
            check (std::abs (d) < 0.1, juce::String (f, 0) + " Hz: " + f2 (d, 3) + " dB");
        }

        // Latenz wird korrekt herausgerechnet: Impuls bleibt an seiner Stelle
        std::vector<float> imp ((size_t) fs, 0.0f), oL, oR;
        imp[10000] = 0.5f;
        auto p = neutral();
        render (p, imp, imp, oL, oR);
        const auto peak = (size_t) std::distance (oL.begin(), std::max_element (oL.begin(), oL.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
        check (std::abs ((int) peak - 10000) <= 2, "Latenzausgleich: Impuls bei " + juce::String ((int) peak) + " (Soll 10000)");
    }

    void testCrossover()
    {
        std::cout << "\nFrequenzweiche summiert flach\n";
        kf::Crossover3 xo;
        xo.set (fs, 120, 2500);
        double worst = 0;
        for (double f : { 30.0, 80.0, 120.0, 200.0, 1000.0, 2500.0, 5000.0, 15000.0 })
        {
            auto s = sine (f, 0.5, 0.5);
            std::vector<float> out (s.size());
            double b[3];
            for (size_t i = 0; i < s.size(); ++i) { xo.L (s[i], b); out[i] = (float) (b[0] + b[1] + b[2]); }
            worst = std::max (worst, std::abs (rmsDb (out, s.size() / 2) - rmsDb (s, s.size() / 2)));
        }
        check (worst < 0.05, "groesste Abweichung 30 Hz-15 kHz: " + f2 (worst, 3) + " dB");
    }

    void testDynamicsImagerExciter()
    {
        std::cout << "\nKompressor, Imager, Exciter\n";
        {
            auto p = neutral();
            p.dyn.bands[1] = { -20.0f, 4.0f, 0.0f };
            auto s = sine (1000, std::pow (10.0, -10.0 / 20.0), 2);
            std::vector<float> oL, oR;
            render (p, s, s, oL, oR);
            const double d = rmsDb (oL, (size_t) fs) - rmsDb (s, (size_t) fs);
            check (std::abs (-d - 5.0) < 0.7, "Kompressor: Reduktion " + f2 (-d) + " dB (Web-Prototyp: 4,93 dB)");
        }
        {
            auto p = neutral();
            p.img.width = { 0.0f, 0.0f, 0.0f };
            auto L = pink (1, 1, 0.5f), R = pink (1, 2, 0.5f);
            std::vector<float> oL, oR;
            render (p, L, R, oL, oR);
            float diff = 0;
            for (size_t i = (size_t) fs / 4; i < oL.size(); ++i) diff = std::max (diff, std::abs (oL[i] - oR[i]));
            check (diff < 1e-5f, "Imager: Breite 0 -> Mono (L-R max. " + juce::String (diff, 7) + ")");
        }
        {
            auto p = neutral();
            p.exc.amount = { 0.0f, 0.0f, 0.6f };
            auto s = sine (5000, 0.3, 1);
            std::vector<float> oL, oR;
            render (p, s, s, oL, oR);
            auto goertzel = [] (const std::vector<float>& d, double f)
            {
                const double k = 2 * std::cos (2 * juce::MathConstants<double>::pi * f / fs);
                double s1 = 0, s2 = 0;
                for (size_t i = (size_t) fs / 2; i < d.size(); ++i) { const double s0 = d[i] + k * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - k * s1 * s2);
            };
            const double h3 = 20 * std::log10 (goertzel (oL, 15000) / goertzel (oL, 5000));
            check (h3 > -45, "Exciter: 3. Oberton " + f2 (h3, 1) + " dB unter dem Grundton");
        }
    }

    void testMaximizer()
    {
        std::cout << "\nMaximizer (True-Peak-Limiter)\n";
        struct Case { const char* name; std::vector<float> L, R; };
        std::vector<Case> cases;
        cases.push_back ({ "rosa Rauschen", pink (10, 3, 0.9f), pink (10, 4, 0.9f) });
        auto ip = sine (fs / 4, 0.9, 5, juce::MathConstants<double>::pi / 4);
        cases.push_back ({ "Sinus mit Intersample-Peaks", ip, ip });

        for (auto& c : cases)
        {
            auto p = kf::ChainParams::defaults();
            p.dyn.on = false;
            p.max = { true, 12.0f, -1.0f, 100.0f };
            std::vector<float> oL, oR;
            render (p, c.L, c.R, oL, oR);
            kf::TruePeakDetector dl, dr;
            double tp = 0;
            for (size_t i = 0; i < oL.size(); ++i) { const double v = std::max (dl.push (oL[i]), dr.push (oR[i])); if (i > (size_t) fs) tp = std::max (tp, v); }
            check (kf::gainToDb (tp) <= -0.95, juce::String (c.name) + ": +12 dB -> True Peak " + f2 (kf::gainToDb (tp)) + " dBTP (Grenze -1,0)");
        }

        // ausgeschaltet: gleiche Latenz, Pegel unverändert
        auto p = neutral();
        kf::Chain a, b;
        a.prepare (fs, 512); b.prepare (fs, 512);
        p.max.on = true; a.set (p);
        p.max.on = false; b.set (p);
        check (a.latency() == b.latency() && a.latency() > 0, "Latenz konstant (an/aus): " + juce::String (a.latency()) + " Samples");
    }

    void testSpeed()
    {
        std::cout << "\nRechenzeit\n";
        auto p = kf::ChainParams::defaults();
        p.pre.bands[1].gain = 2; p.post.bands[4].gain = 1.5f;
        for (auto& b : p.dyn.bands) { b.threshold = -24; b.ratio = 2; }
        p.exc.amount[2] = 0.3f; p.img.width[2] = 1.2f; p.max.gain = 6;
        auto L = pink (20, 7, 0.4f), R = pink (20, 8, 0.4f);
        std::vector<float> oL, oR;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (p, L, R, oL, oR);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 20000.0 / 8.0, "20 s Audio mit allen Modulen in " + f2 (ms / 1000.0) + " s (" + f2 (20000.0 / ms, 0) + "x Echtzeit)");
    }

    void testAssistant()
    {
        std::cout << "\nEin-Klick-Assistent\n";
        std::vector<float> L, R;
        song (L, R, 20, 1.0, -4.0, 0.15f);
        std::vector<float> dL, dR;
        song (dL, dR, 20, 1.0, -14.0, 0.15f);   // sehr dumpfer Mix
        const auto in = kf::measure (L.data(), R.data(), (int) L.size(), fs);

        for (float target : { -11.0f, -14.0f })
        {
            auto res = kf::AssistantJob::runFull (L, R, fs, target, kf::tone::makePreset ("club"));
            std::vector<float> oL, oR;
            render (res.params, L, R, oL, oR);
            const auto m = kf::measure (oL.data(), oR.data(), (int) oL.size(), fs);
            check (res.error.isEmpty() && std::abs (m.integrated - target) < 0.6,
                   "Ziel " + f2 (target, 1) + " LUFS: Eingang " + f2 (in.integrated, 1) + " -> Master " + f2 (m.integrated) + " LUFS, Maximizer +" + f2 (res.params.max.gain, 1) + " dB");
            check (m.truePeakDb <= -0.9, "True Peak " + f2 (m.truePeakDb) + " dBTP");
            check (res.notes.size() == 5, juce::String ((int) res.notes.size()) + " Hinweise fuer die Oberflaeche");
        }

        // Klangziel wirkt: dumpfer Mix gegen helles Ziel -> Höhen werden angehoben
        auto res = kf::AssistantJob::runFull (dL, dR, fs, -11.0f, kf::tone::makePreset ("club"));
        check (res.params.pre.bands[4].gain > 0.5f, "dumpfer Mix -> Hoehen-Shelf " + f2 (res.params.pre.bands[4].gain, 1) + " dB");

        auto preset = kf::tone::makePreset ("hiphop");
        check (preset.isValid() && preset.bands[10].p10 < preset.bands[10].p50 && preset.bands[10].p50 < preset.bands[10].p90, "Genre-Klangmodell ist eine gueltige Zielkurve");
        check (ms::TargetCurve::fromJson (preset.toJson()).has_value(), "Klangmodell als Leitstern-JSON speicherbar");
    }

    void feed (KernfusionProcessor& proc, const std::vector<float>& L, const std::vector<float>& R, int block = 512)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos + (size_t) block <= L.size(); pos += (size_t) block)
        {
            buf.copyFrom (0, 0, L.data() + pos, block);
            buf.copyFrom (1, 0, R.data() + pos, block);
            proc.processBlock (buf, midi);
            if ((pos / (size_t) block) % 40 == 0) std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    }

    void testProcessor()
    {
        std::cout << "\nPlugin: Ein Klick im Prozessor, Zustand speichern\n";
        KernfusionProcessor proc;
        proc.setPlayConfigDetails (2, 2, fs, 512);
        proc.prepareToPlay (fs, 512);
        kf::params::setValue (proc.apvts, kf::params::target, -9.0f);
        proc.poll();

        std::vector<float> L, R;
        song (L, R, 24, 2.0, -3.0, 0.15f);
        proc.oneClick();
        check (proc.getPhase() == KernfusionProcessor::Phase::listening, "Ein Klick -> hoert mit");
        feed (proc, L, R);
        check (proc.hasCapture(), "nach 20 s Musik ist die Aufnahme fertig");

        for (int i = 0; i < 600 && (proc.getPhase() != KernfusionProcessor::Phase::idle || proc.isAssistantBusy()); ++i)
        {
            proc.poll();
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        proc.poll();
        check (proc.getNotes().size() == 5, "Assistent fertig, Einstellungen uebernommen");
        check (kf::params::getValue (proc.apvts, "max_gain") > 3.0f, "Maximizer-Gain gesetzt: +" + f2 (kf::params::getValue (proc.apvts, "max_gain"), 1) + " dB");

        // Master abspielen und messen
        proc.engine.resetRequested = true;
        feed (proc, L, R);
        const float I = proc.engine.meters.outIntegrated.load(), tp = proc.engine.meters.outTruePeak.load();
        check (std::abs (I + 9.0f) < 1.0f, "Live gemessen: " + f2 (I) + " LUFS (Ziel -9)");
        check (tp <= -0.9f, "Live True Peak " + f2 (tp) + " dBTP");

        // Audio bei ausgeschalteten Modulen unverändert (bis auf Latenz)
        // Zustand
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        KernfusionProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (kf::params::getValue (restored.apvts, "max_gain") - kf::params::getValue (proc.apvts, "max_gain")) < 0.01f
               && std::abs (kf::params::getValue (restored.apvts, kf::params::target) + 9.0f) < 0.01f, "Parameter werden mit dem Projekt gespeichert");
        check (restored.getNotes().size() == 5 && restored.getToneKey() == proc.getToneKey(), "Hinweise und Klangziel werden gespeichert");

        juce::AudioProcessor::BusesLayout mono, surround;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        surround.inputBuses.add (juce::AudioChannelSet::create5point1()); surround.outputBuses.add (juce::AudioChannelSet::create5point1());
        check (proc.isBusesLayoutSupported (mono) && ! proc.isBusesLayoutSupported (surround), "Mono und Stereo erlaubt, Surround nicht");
    }

    void snapshots (const juce::File& folder)
    {
        std::cout << "\nScreenshots\n";
        folder.createDirectory();
        KernfusionProcessor proc;
        proc.setPlayConfigDetails (2, 2, fs, 512);
        proc.prepareToPlay (fs, 512);
        kf::params::setValue (proc.apvts, kf::params::target, -11.0f);
        proc.poll();

        std::vector<float> L, R;
        song (L, R, 24, 1.5, -5.0, 0.15f);
        proc.oneClick();
        feed (proc, L, R);
        for (int i = 0; i < 600 && (proc.getPhase() != KernfusionProcessor::Phase::idle || proc.isAssistantBusy()); ++i) { proc.poll(); std::this_thread::sleep_for (std::chrono::milliseconds (20)); }
        proc.poll();

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* e = dynamic_cast<KernfusionEditor*> (ed.get());
        proc.engine.resetRequested = true;

        // live abspielen, damit Waveform, Meter und Klangbalance gefüllt sind
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos + 512 <= L.size(); pos += 512)
        {
            buf.copyFrom (0, 0, L.data() + pos, 512);
            buf.copyFrom (1, 0, R.data() + pos, 512);
            proc.processBlock (buf, midi);
            if ((pos / 512) % 3 == 0) e->refresh();
            if ((pos / 512) % 40 == 0) std::this_thread::sleep_for (std::chrono::milliseconds (3));
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (300));

        const char* names[] = { "1-ein-klick.png", "2-profi.png", "3-industrielautstaerke.png" };
        for (int t = 0; t < 3; ++t)
        {
            e->showTab (t);
            for (int i = 0; i < 3; ++i) e->refresh();
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto f = folder.getChildFile (names[t]);
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat png;
            check (out.openedOk() && png.writeImageToStream (img, out), f.getFullPathName());
        }
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String snapFolder;
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--snapshot") snapFolder = argv[i + 1];

    testLoudness();
    testNeutral();
    testCrossover();
    testDynamicsImagerExciter();
    testMaximizer();
    testSpeed();
    testAssistant();
    testProcessor();

    if (snapFolder.isNotEmpty())
        snapshots (juce::File (snapFolder));

    std::cout << (failures == 0 ? "\nAlle Tests bestanden\n" : "\n" + std::to_string (failures) + " Test(s) fehlgeschlagen\n");
    return failures == 0 ? 0 : 1;
}
