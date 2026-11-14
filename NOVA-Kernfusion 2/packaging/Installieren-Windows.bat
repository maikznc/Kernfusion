@echo off
chcp 65001 >nul
echo.
echo  NOVA Kernfusion wird installiert ...
echo.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo  Bitte mit Rechtsklick "Als Administrator ausfuehren" starten.
    echo.
    pause
    exit /b 1
)

if not exist "%~dp0NOVA Kernfusion.vst3" (
    echo  "NOVA Kernfusion.vst3" nicht gefunden. Erst die ZIP-Datei komplett entpacken.
    echo.
    pause
    exit /b 1
)

if not exist "%CommonProgramFiles%\VST3" mkdir "%CommonProgramFiles%\VST3"
if exist "%CommonProgramFiles%\VST3\NOVA Kernfusion.vst3" rmdir /s /q "%CommonProgramFiles%\VST3\NOVA Kernfusion.vst3"
xcopy /E /I /Y /Q "%~dp0NOVA Kernfusion.vst3" "%CommonProgramFiles%\VST3\NOVA Kernfusion.vst3" >nul

echo  Fertig. Installiert nach: %CommonProgramFiles%\VST3
echo  In Ableton: Einstellungen - Plug-Ins - "Neu scannen".
echo.
pause
