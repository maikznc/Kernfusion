#!/bin/bash
# Signiert die gebauten Plugins (ad hoc) und packt sie als Installer (.pkg) und ZIP nach dist/.
# Läuft auf macOS (GitHub Actions oder lokal nach dem Bauen).
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION="$(sed -nE 's/^project \(NOVAKernfusion VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)"
NAME="NOVA Kernfusion"
FILE_NAME="NOVA-Kernfusion-${VERSION}-Mac"
ART="build/plugins/Kernfusion/Kernfusion_artefacts/Release"
VST3="$ART/VST3/$NAME.vst3"
AU="$ART/AU/$NAME.component"

for bundle in "$VST3" "$AU"; do
    [ -d "$bundle" ] || { echo "Fehlt: $bundle (erst bauen)"; exit 1; }
done

rm -rf dist pkgroot pkgscripts component.plist zip
mkdir -p dist

# Ad-hoc-Signatur – ohne Signatur laden Apple-Silicon-Macs die Plugins nicht
codesign --force --deep --sign - "$VST3"
codesign --force --deep --sign - "$AU"

# ---- Installer: legt die Plugins in die System-Plugin-Ordner
mkdir -p "pkgroot/Library/Audio/Plug-Ins/VST3" "pkgroot/Library/Audio/Plug-Ins/Components"
ditto "$VST3" "pkgroot/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
ditto "$AU"   "pkgroot/Library/Audio/Plug-Ins/Components/$NAME.component"

# Komponenten-Liste selbst schreiben: immer genau dorthin installieren, ältere Versionen überschreiben
cat > component.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>BundleHasStrictIdentifier</key><true/>
        <key>BundleIsRelocatable</key><false/>
        <key>BundleIsVersionChecked</key><false/>
        <key>BundleOverwriteAction</key><string>upgrade</string>
        <key>RootRelativeBundlePath</key><string>Library/Audio/Plug-Ins/VST3/$NAME.vst3</string>
    </dict>
    <dict>
        <key>BundleHasStrictIdentifier</key><true/>
        <key>BundleIsRelocatable</key><false/>
        <key>BundleIsVersionChecked</key><false/>
        <key>BundleOverwriteAction</key><string>upgrade</string>
        <key>RootRelativeBundlePath</key><string>Library/Audio/Plug-Ins/Components/$NAME.component</string>
    </dict>
</array>
</plist>
EOF

# Nach der Installation den AU-Cache auffrischen, damit Logic/Ableton das AU sofort sehen
mkdir -p pkgscripts
cat > pkgscripts/postinstall <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOF
chmod +x pkgscripts/postinstall

pkgbuild --root pkgroot \
         --component-plist component.plist \
         --scripts pkgscripts \
         --identifier com.nova.kernfusion \
         --version "$VERSION" \
         --install-location / \
         "dist/${FILE_NAME}-Installer.pkg"

# ---- ZIP zum manuellen Kopieren
mkdir -p "zip/${FILE_NAME}"
ditto "$VST3" "zip/${FILE_NAME}/$NAME.vst3"
ditto "$AU"   "zip/${FILE_NAME}/$NAME.component"
cp packaging/INSTALLATION-Mac.txt "zip/${FILE_NAME}/"
ditto -c -k --keepParent "zip/${FILE_NAME}" "dist/${FILE_NAME}-Plugins.zip"
cp packaging/INSTALLATION-Mac.txt dist/

rm -rf pkgroot pkgscripts component.plist zip
ls -la dist
