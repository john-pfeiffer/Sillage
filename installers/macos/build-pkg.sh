#!/usr/bin/env bash
# Builds the macOS installer package for Sillage.
#
#   installers/macos/build-pkg.sh <version> <artefacts-dir> [out-dir]
#
#   <artefacts-dir> is the JUCE output folder holding VST3/, AU/ and
#   Standalone/ (e.g. build/Sillage_artefacts/Release).
#
# Signing is optional and driven by the environment:
#   SILLAGE_CODESIGN_ID   "Developer ID Application: ..."  signs the bundles
#   SILLAGE_INSTALLER_ID  "Developer ID Installer: ..."    signs the .pkg
# Unsigned packages install fine on a development machine (right-click >
# Open); notarisation for distribution is a separate step in the release
# process.
set -euo pipefail

VERSION="${1:?usage: build-pkg.sh <version> <artefacts-dir> [out-dir]}"
ARTEFACTS="${2:?usage: build-pkg.sh <version> <artefacts-dir> [out-dir]}"
OUT_DIR="${3:-dist}"

VST3="$ARTEFACTS/VST3/Sillage.vst3"
AU="$ARTEFACTS/AU/Sillage.component"
APP="$ARTEFACTS/Standalone/Sillage.app"

for bundle in "$VST3" "$AU" "$APP"; do
    [[ -d "$bundle" ]] || { echo "missing bundle: $bundle" >&2; exit 1; }
done

if [[ -n "${SILLAGE_CODESIGN_ID:-}" ]]; then
    for bundle in "$VST3" "$AU" "$APP"; do
        codesign --force --deep --options runtime --timestamp --sign "$SILLAGE_CODESIGN_ID" "$bundle"
    done
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT_DIR"

pkgbuild --component "$VST3" --install-location /Library/Audio/Plug-Ins/VST3 \
         --identifier com.elanvitalstudios.sillage.vst3 --version "$VERSION" "$WORK/vst3.pkg"
pkgbuild --component "$AU" --install-location /Library/Audio/Plug-Ins/Components \
         --identifier com.elanvitalstudios.sillage.au --version "$VERSION" "$WORK/au.pkg"
pkgbuild --component "$APP" --install-location /Applications \
         --identifier com.elanvitalstudios.sillage.app --version "$VERSION" "$WORK/app.pkg"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>Sillage $VERSION</title>
    <options customize="always" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plugin" description="Installs to /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="com.elanvitalstudios.sillage.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="Installs to /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="com.elanvitalstudios.sillage.au"/>
    </choice>
    <choice id="app" title="Standalone application" description="Installs to /Applications" start_selected="false">
        <pkg-ref id="com.elanvitalstudios.sillage.app"/>
    </choice>
    <pkg-ref id="com.elanvitalstudios.sillage.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="com.elanvitalstudios.sillage.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="com.elanvitalstudios.sillage.app" version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
XML

OUTPUT="$OUT_DIR/Sillage-$VERSION-macos.pkg"
SIGN_ARGS=()
if [[ -n "${SILLAGE_INSTALLER_ID:-}" ]]; then
    SIGN_ARGS=(--sign "$SILLAGE_INSTALLER_ID" --timestamp)
fi

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "${SIGN_ARGS[@]}" "$OUTPUT"
echo "built $OUTPUT"
