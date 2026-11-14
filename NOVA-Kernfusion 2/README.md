# NOVA Kernfusion

**Kernfusion** ist das Mastering-Plugin von NOVA: Ein-Klick-Mastering mit Assistent,
eine komplette Mastering-Kette für Profis und ein Industrielautstärke-Check für Spotify & Co.
Für **Mac (VST3 + AU)** und **Windows (VST3)**.

## Was drin ist

- **Mini-Meter** oben im Fenster, immer sichtbar: scrollende Waveform des Masters in
  **Rot, Blau oder Spektral** (Bass rot, Höhen blau), dahinter grau das Original.
  Daneben Kurzzeit- und integrierte Lautheit, True Peak und Limiter-Reduktion.
- **Ein Klick:** Song abspielen, „Ein Klick mastern“ drücken. Kernfusion hört ca. 20 Sekunden
  mit und stellt alle Module ein (EQ Richtung Klangziel, Multiband-Kleber, Stereobild,
  Exciter, Maximizer auf die Ziel-Lautheit). Danach mit sieben Reglern nachregeln:
  **Lautheit, Bass, Wärme, Punch, Breite, Luft, Glanz**. Die Lautheit bleibt beim Drehen auf dem Ziel.
- **Klangziele:** Genre-Modelle (Pop, Club/EDM, Hip-Hop, Rock, Akustisch), **eigene Referenztracks**
  (einfach ins Fenster ziehen) oder **Zielkurven aus NOVA Leitstern** (gemeinsamer Ordner).
- **Profi:** Pre-EQ und Post-EQ mit ziehbaren Punkten, Multiband-Dynamik (3 Bänder),
  Exciter (Band/Röhre/Warm, 2-fach überabgetastet), Imager mit Goniometer,
  Maximizer (True-Peak-Limiter mit Lookahead). Alle Parameter sind in der DAW automatisierbar.
- **Industrielautstärke:** misst den Master nach ITU-R BS.1770 und zeigt, ob er „Industrie-laut“,
  „laut genug“ oder „zu leise“ ist. Für Spotify, Apple Music, YouTube, Amazon, TIDAL und Deezer
  steht da, was die Plattform mit dem Song macht – mit **„Anhören“** hörst du ihn so,
  wie die Plattform ihn abspielen würde.
- **Vorher / Nachher** pegelgleich, damit „lauter“ nicht automatisch „besser“ klingt.

> Vor dem Export „Vorher / Nachher“ und die Plattform-Vorschau ausschalten –
> sonst landen sie im Bounce. Kernfusion zeigt oben einen gelben Hinweis, solange eins aktiv ist.

## Fertige Plugins herunterladen

GitHub baut die Plugins bei jeder Änderung automatisch (siehe `WORKFLOW-fuer-GitHub.txt`):

1. Im Repo oben auf **Actions** klicken.
2. Den obersten Lauf **„NOVA Kernfusion bauen“** mit grünem Haken öffnen.
3. Ganz unten bei **Artifacts**: **NOVA-Kernfusion-Mac** bzw. **NOVA-Kernfusion-Windows** laden.
4. Installation: `packaging/INSTALLATION-Mac.txt` bzw. `packaging/INSTALLATION-Windows.txt`.

Für feste Versionen einen Tag `v0.1.0` anlegen – dann landen die Dateien zusätzlich unter **Releases**.

## Selbst bauen (optional)

Mac: Xcode Command Line Tools (`xcode-select --install`) und CMake.
Windows: Visual Studio 2022 (mit „Desktopentwicklung mit C++“) und CMake.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Kernfusion_VST3
```

Auf dem Mac zusätzlich `Kernfusion_AU`. JUCE 9.0.4 wird beim ersten Mal automatisch geladen.
Tests: `-DMS_BUILD_TESTS=ON` und das Ziel `KernfusionTests` bauen und starten.

## Projektaufbau

```
NOVA-Kernfusion/
├── WORKFLOW-fuer-GitHub.txt      Build-Anleitung für GitHub (Inhalt nach .github/workflows/build.yml)
├── CMakeLists.txt                Gesamtprojekt (lädt JUCE)
├── modules/ms_core/              Analyse-Kern, identisch mit NOVA Leitstern
├── plugins/Kernfusion/
│   ├── Source/dsp/               DSP-Kette: EQ, Dynamik, Exciter, Imager, Maximizer, Lautheitsmessung
│   ├── Source/Assistant.*        Ein-Klick-Assistent und Klangziele
│   ├── Source/Engine.*           Echtzeit-Teil: Meter, Waveform, Vorher/Nachher, Vorschau
│   ├── Source/ui/                Oberfläche
│   └── Resources/fonts/          Barlow Semi Condensed + IBM Plex Mono (SIL Open Font License)
├── scripts/                      Installer (Mac) und ZIP (Windows) bauen
├── packaging/                    Installationsanleitungen, Windows-Installationshilfe
├── tests/                        Tests (DSP, Assistent, Plugin) + Tests des Web-Prototyps
└── web/                          Browser-Prototyp (gleiche Mathematik)
```

## Wie gemessen und geregelt wird

- **Lautheit** nach ITU-R BS.1770 / EBU R128 (K-Gewichtung, 400-ms-Blöcke, absolutes und relatives Gate),
  **True Peak** mit 4-facher Überabtastung.
- **Maximizer:** 1,5 ms Lookahead auf True Peak, Ausgang garantiert unter dem Ceiling (Standard −1,0 dBTP).
- **Multiband:** Linkwitz-Riley-Weichen 4. Ordnung mit Phasenausgleich – summieren flach.
- **Assistent:** misst Lautheit und Spektrum der Aufnahme, korrigiert behutsam (max. ±4 dB) Richtung Klangziel,
  setzt die Kompressor-Schwellen aus den Bandpegeln und misst den Maximizer iterativ auf die Ziel-Lautheit ein.
  Ist das Ziel nur mit Verzerrung erreichbar, sagt er das.
- Plattform-Ziele: Spotify offiziell (Normal −14, Laut −11, Leise −19 LUFS; True Peak unter −1 dBTP,
  bei Mastern über −14 LUFS besser −2 dBTP). Übrige Plattformen: verbreitete Richtwerte.

## Lizenz-Hinweise

Das Projekt nutzt das JUCE-Framework (AGPLv3 oder kommerzielle JUCE-9-Lizenz,
https://juce.com/legal/juce-9-licence/). Für den eigenen Gebrauch unproblematisch; vor Verkauf oder Weitergabe
prüfen, ob eine kommerzielle JUCE-Lizenz nötig ist. Die eingebetteten Schriften stehen unter der
SIL Open Font License (Lizenztexte in `plugins/Kernfusion/Resources/fonts`).
