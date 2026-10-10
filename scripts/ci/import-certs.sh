#!/usr/bin/env bash
# Imports the Developer ID certificates into a throwaway keychain on a macOS CI
# runner so codesign / productbuild can use them non-interactively.
#
# Env (GitHub secrets):
#   MACOS_CERT_P12       base64 of a .p12 holding both Developer ID identities
#                        (Application + Installer) with their private keys
#   MACOS_CERT_PASSWORD  the .p12 export password
set -euo pipefail

for v in MACOS_CERT_P12 MACOS_CERT_PASSWORD; do
    if [[ -z "${!v:-}" ]]; then
        echo "error: secret $v is not set" >&2
        exit 1
    fi
done

KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
KEYCHAIN_PWD="$(uuidgen)"
P12="$RUNNER_TEMP/certs.p12"

security create-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"   # don't auto-lock mid-build
security unlock-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN"
# Put it first in the search list (keeping the login keychain) so codesign finds it.
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')

printf '%s' "$MACOS_CERT_P12" | base64 --decode > "$P12"
security import "$P12" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" \
    -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/productsign -T /usr/bin/pkgbuild
rm -f "$P12"
security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PWD" "$KEYCHAIN" > /dev/null

echo "Identities in signing keychain:"
security find-identity -v "$KEYCHAIN"
for kind in "Developer ID Application" "Developer ID Installer"; do
    if ! security find-identity -v "$KEYCHAIN" | grep -q "$kind"; then
        echo "error: no \"$kind\" identity in the .p12" >&2
        exit 1
    fi
done

echo "SIGNING_KEYCHAIN=$KEYCHAIN" >> "$GITHUB_ENV"
