#!/usr/bin/env bash
# Builds the macOS installer package for Sillage, signing and notarising it
# when the credentials are present.
#
#   installers/macos/build-pkg.sh <version> <artefacts-dir> [out-dir]
#
#   <artefacts-dir> is the JUCE output folder holding VST3/, AU/ and
#   Standalone/ (e.g. build/Sillage_artefacts/Release).
#
# Everything below is optional and driven by the environment; with none of it
# set the result is an unsigned package that installs on a development
# machine (right-click > Open). The certificates must already be in a
# keychain — on CI, installers/macos/import-certificates.sh puts them there.
#
#   SILLAGE_CODESIGN_ID        "Developer ID Application: ..."  signs the bundles
#   SILLAGE_INSTALLER_ID       "Developer ID Installer: ..."    signs the .pkg
#
#   Notarisation runs only for a signed package, with either an App Store
#   Connect API key (preferred):
#   SILLAGE_NOTARY_KEY_P8      base64 of the AuthKey_XXXX.p8 file
#   SILLAGE_NOTARY_KEY_ID      the key id
#   SILLAGE_NOTARY_ISSUER_ID   the issuer id
#   or an Apple ID:
#   SILLAGE_APPLE_ID           the account email
#   SILLAGE_APPLE_TEAM_ID      the 10-character team id
#   SILLAGE_APPLE_APP_PASSWORD an app-specific password
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

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT_DIR"

# ---- Sign the bundles --------------------------------------------------------
if [[ -n "${SILLAGE_CODESIGN_ID:-}" ]]; then
    for bundle in "$VST3" "$AU" "$APP"; do
        echo "signing $bundle"
        codesign --force --options runtime --timestamp --sign "$SILLAGE_CODESIGN_ID" "$bundle"
        codesign --verify --strict --verbose=2 "$bundle"
    done
else
    echo "SILLAGE_CODESIGN_ID not set: bundles are unsigned"
fi

# ---- Component packages ------------------------------------------------------
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

# ---- Product package, signed when possible -----------------------------------
OUTPUT="$OUT_DIR/Sillage-$VERSION-macos.pkg"
SIGN_ARGS=()
if [[ -n "${SILLAGE_INSTALLER_ID:-}" ]]; then
    SIGN_ARGS=(--sign "$SILLAGE_INSTALLER_ID" --timestamp)
fi

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "${SIGN_ARGS[@]}" "$OUTPUT"
echo "built $OUTPUT"

# ---- Notarise and staple -----------------------------------------------------
if [[ ${#SIGN_ARGS[@]} -eq 0 ]]; then
    echo "SILLAGE_INSTALLER_ID not set: package is unsigned and will not be notarised"
    exit 0
fi

NOTARY_ARGS=()
if [[ -n "${SILLAGE_NOTARY_KEY_P8:-}" && -n "${SILLAGE_NOTARY_KEY_ID:-}" && -n "${SILLAGE_NOTARY_ISSUER_ID:-}" ]]; then
    KEY_FILE="$WORK/AuthKey_$SILLAGE_NOTARY_KEY_ID.p8"
    echo "$SILLAGE_NOTARY_KEY_P8" | base64 --decode > "$KEY_FILE"
    NOTARY_ARGS=(--key "$KEY_FILE" --key-id "$SILLAGE_NOTARY_KEY_ID" --issuer "$SILLAGE_NOTARY_ISSUER_ID")
elif [[ -n "${SILLAGE_APPLE_ID:-}" && -n "${SILLAGE_APPLE_TEAM_ID:-}" && -n "${SILLAGE_APPLE_APP_PASSWORD:-}" ]]; then
    NOTARY_ARGS=(--apple-id "$SILLAGE_APPLE_ID" --team-id "$SILLAGE_APPLE_TEAM_ID" --password "$SILLAGE_APPLE_APP_PASSWORD")
fi

if [[ ${#NOTARY_ARGS[@]} -eq 0 ]]; then
    echo "package is signed but no notarisation credentials are set; skipping notarytool"
    exit 0
fi

echo "notarising $OUTPUT"
set +e
RESULT="$(xcrun notarytool submit "$OUTPUT" "${NOTARY_ARGS[@]}" --wait 2>&1)"
STATUS=$?
set -e
echo "$RESULT"

if [[ $STATUS -ne 0 || "$RESULT" != *"status: Accepted"* ]]; then
    SUBMISSION_ID="$(echo "$RESULT" | sed -n 's/^ *id: \([0-9a-fA-F-]*\).*/\1/p' | head -1)"
    if [[ -n "$SUBMISSION_ID" ]]; then
        echo "notarisation log for $SUBMISSION_ID:"
        xcrun notarytool log "$SUBMISSION_ID" "${NOTARY_ARGS[@]}" || true
    fi
    echo "notarisation failed" >&2
    exit 1
fi

xcrun stapler staple "$OUTPUT"
xcrun stapler validate "$OUTPUT"
spctl --assess --type install --verbose=2 "$OUTPUT"
echo "notarised and stapled $OUTPUT"
