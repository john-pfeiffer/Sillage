#!/usr/bin/env bash
# Imports the Developer ID certificates into a throwaway keychain on a CI
# runner, so codesign and productbuild can find them by name.
#
#   SILLAGE_MAC_CERT_P12       base64 of one .p12 holding both the
#                              "Developer ID Application" and the
#                              "Developer ID Installer" certificate + key
#   SILLAGE_MAC_CERT_PASSWORD  the .p12 password
#
# Does nothing when the certificate is not set, so unsigned builds keep
# working. The keychain lives in RUNNER_TEMP and dies with the runner.
set -euo pipefail

if [[ -z "${SILLAGE_MAC_CERT_P12:-}" ]]; then
    echo "SILLAGE_MAC_CERT_P12 not set; skipping certificate import (build will be unsigned)"
    exit 0
fi

TEMP_DIR="${RUNNER_TEMP:-$(mktemp -d)}"
KEYCHAIN="$TEMP_DIR/sillage-signing.keychain-db"
KEYCHAIN_PASSWORD="$(openssl rand -hex 24)"
CERT_FILE="$(mktemp "$TEMP_DIR/cert.XXXXXX")"
trap 'rm -f "$CERT_FILE"' EXIT

echo "$SILLAGE_MAC_CERT_P12" | base64 --decode > "$CERT_FILE"

security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security import "$CERT_FILE" -P "${SILLAGE_MAC_CERT_PASSWORD:-}" -A -t cert -f pkcs12 -k "$KEYCHAIN"

# Let the Apple tools use the key without a UI prompt.
security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" > /dev/null

# Put the new keychain first in the search list, keeping the existing ones.
EXISTING="$(security list-keychains -d user | sed 's/^ *"//; s/"$//')"
# shellcheck disable=SC2086
security list-keychains -d user -s "$KEYCHAIN" $EXISTING

echo "Imported identities:"
security find-identity -v "$KEYCHAIN"
