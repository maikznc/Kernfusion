# Packt das gebaute Windows-VST3 mit Installationshilfe als ZIP nach dist/.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$version = (Select-String -Path CMakeLists.txt -Pattern 'project \(NOVAKernfusion VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$name = "NOVA Kernfusion"
$folder = "NOVA-Kernfusion-$version-Windows"
$vst3 = "build/plugins/Kernfusion/Kernfusion_artefacts/Release/VST3/$name.vst3"

if (-not (Test-Path $vst3)) { throw "Fehlt: $vst3 (erst bauen)" }

Remove-Item -Recurse -Force dist, zip -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force dist, "zip/$folder" | Out-Null

Copy-Item -Recurse $vst3 "zip/$folder/"
Copy-Item packaging/INSTALLATION-Windows.txt "zip/$folder/"
Copy-Item packaging/Installieren-Windows.bat "zip/$folder/Installieren.bat"

Compress-Archive -Path "zip/$folder" -DestinationPath "dist/$folder.zip" -Force
Copy-Item packaging/INSTALLATION-Windows.txt dist/
Remove-Item -Recurse -Force zip
Get-ChildItem dist
