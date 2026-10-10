#!/usr/bin/env bash
# Signs, notarizes and staples the macOS MooDuck bundles, then builds a signed,
# notarized installer .pkg. Run after import-certs.sh, on a macOS runner.
#
#   scripts/ci/sign-macos.sh <artefacts dir> <output dir>
#   e.g. scripts/ci/sign-macos.sh build/MooDuck_artefacts/Release dist
#
# Env (GitHub secrets):
#   APPLE_TEAM_ID    10-char team ID (picks the matching identities)
#   ASC_API_KEY_P8   App Store Connect API key (.p8 contents) for notarytool
#   ASC_KEY_ID       its Key ID
#   ASC_ISSUER_ID    its Issuer ID
#
# Output: MooDuck-<ver>-macOS.pkg, MooDuck-<ver>-macOS-VST3.zip,
#         MooDuck-<ver>-macOS-AU.zip (bundles inside are stapled).
set -euo pipefail

ARTEFACTS="${1:?artefacts dir}"
OUT="${2:?output dir}"

for v in APPLE_TEAM_ID ASC_API_KEY_P8 ASC_KEY_ID ASC_ISSUER_ID; do
    if [[ -z "${!v:-}" ]]; then
        echo "error: secret $v is not set" >&2
        exit 1
    fi
done

VST3="$ARTEFACTS/VST3/MooDuck.vst3"
AU="$ARTEFACTS/AU/MooDuck.component"
for b in "$VST3" "$AU"; do
    [[ -d "$b" ]] || { echo "error: $b not found" >&2; exit 1; }
done

VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$VST3/Contents/Info.plist")"
APP_ID="$(security find-identity -v -p codesigning | grep "Developer ID Application" | grep "($APPLE_TEAM_ID)" | head -1 | sed -E 's/.*"(.*)"/\1/')"
INST_ID="$(security find-identity -v | grep "Developer ID Installer" | grep "($APPLE_TEAM_ID)" | head -1 | sed -E 's/.*"(.*)"/\1/')"
[[ -n "$APP_ID" && -n "$INST_ID" ]] || { echo "error: Developer ID identities for team $APPLE_TEAM_ID not found" >&2; exit 1; }
echo "Version            : $VERSION"
echo "Application signer : $APP_ID"
echo "Installer signer   : $INST_ID"

WORK="$RUNNER_TEMP/macsign"
rm -rf "$WORK" && mkdir -p "$WORK" "$OUT"
KEY="$WORK/AuthKey.p8"
printf '%s' "$ASC_API_KEY_P8" > "$KEY"
trap 'rm -f "$KEY"' EXIT

notarize() {   # <file to submit>
    local log="$WORK/notary-$(basename "$1").json"
    xcrun notarytool submit "$1" --key "$KEY" --key-id "$ASC_KEY_ID" --issuer "$ASC_ISSUER_ID" \
        --wait --timeout 30m --output-format json > "$log" || true
    local status id
    status="$(/usr/bin/plutil -extract status raw -o - "$log" 2>/dev/null || echo unknown)"
    id="$(/usr/bin/plutil -extract id raw -o - "$log" 2>/dev/null || echo "")"
    echo "Notarization of $(basename "$1"): $status ($id)"
    if [[ "$status" != "Accepted" ]]; then
        cat "$log"
        [[ -n "$id" ]] && xcrun notarytool log "$id" --key "$KEY" --key-id "$ASC_KEY_ID" --issuer "$ASC_ISSUER_ID"
        exit 1
    fi
}

# --- 1. Sign the bundles (hardened runtime + secure timestamp; each bundle has a
# single Mach-O, so signing the bundle covers it — no --deep).
for b in "$VST3" "$AU"; do
    codesign --force --timestamp --options runtime --sign "$APP_ID" "$b"
    codesign --verify --strict --verbose=2 "$b"
done

# --- 2. Notarize both bundles in one submission, then staple each.
mkdir -p "$WORK/bundles/stage"
ditto "$VST3" "$WORK/bundles/stage/MooDuck.vst3"
ditto "$AU"   "$WORK/bundles/stage/MooDuck.component"
ditto -c -k "$WORK/bundles/stage" "$WORK/bundles/MooDuck-bundles.zip"
notarize "$WORK/bundles/MooDuck-bundles.zip"
xcrun stapler staple "$VST3"
xcrun stapler staple "$AU"

# --- 3. Distributable zips of the stapled bundles.
ditto -c -k --keepParent "$VST3" "$OUT/MooDuck-$VERSION-macOS-VST3.zip"
ditto -c -k --keepParent "$AU"   "$OUT/MooDuck-$VERSION-macOS-AU.zip"

# --- 4. Installer: one component pkg per format (non-relocatable, so the
# installer never "updates" a stray copy elsewhere on disk instead of
# installing to /Library), combined into a product archive signed with the
# Installer identity.
component_pkg() {   # <bundle> <install location> <identifier> <out pkg>
    local root="$WORK/root-$3"
    mkdir -p "$root"
    ditto "$1" "$root/$(basename "$1")"
    pkgbuild --analyze --root "$root" "$WORK/$3.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$3.plist"
    pkgbuild --root "$root" --component-plist "$WORK/$3.plist" \
        --install-location "$2" --identifier "$3" --version "$VERSION" "$4"
}
component_pkg "$VST3" /Library/Audio/Plug-Ins/VST3       com.mvzn.MooDuck.vst3 "$WORK/MooDuck-VST3.pkg"
component_pkg "$AU"   /Library/Audio/Plug-Ins/Components com.mvzn.MooDuck.au   "$WORK/MooDuck-AU.pkg"

PKG="$OUT/MooDuck-$VERSION-macOS.pkg"
productbuild --sign "$INST_ID" --timestamp \
    --package "$WORK/MooDuck-VST3.pkg" \
    --package "$WORK/MooDuck-AU.pkg" \
    "$PKG"
notarize "$PKG"
xcrun stapler staple "$PKG"

# --- 5. Verify what ships.
for b in "$VST3" "$AU"; do
    codesign -dv --verbose=2 "$b" 2>&1 | grep -E "Authority=Developer ID|flags=|Timestamp="
    xcrun stapler validate "$b"
    spctl -a -vv -t exec "$b" 2>&1 || true   # plug-in bundles may report "rejected (not an app)"; stapler + codesign are authoritative
done
pkgutil --check-signature "$PKG"
spctl -a -vv -t install "$PKG"
xcrun stapler validate "$PKG"

echo "Signed outputs in $OUT:"
ls -l "$OUT"
