#!/usr/bin/env bash
# Installs an already-built MooDuck plugin to the standard per-OS plugin folders.
# CMake's COPY_PLUGIN_AFTER_BUILD already does this as part of `cmake --build`;
# this script is for reinstalling a build without recompiling, or for generators
# where the copy step doesn't run.
set -euo pipefail

CONFIG="${1:-Release}"
BUILD_DIR="${2:-build-cmake}"
ARTEFACTS="$BUILD_DIR/MooDuck_artefacts/$CONFIG"

if [[ ! -d "$ARTEFACTS" ]]; then
    echo "error: $ARTEFACTS not found — build first: cmake --build $BUILD_DIR --config $CONFIG" >&2
    exit 1
fi

case "$(uname -s)" in
    Darwin)
        VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
        AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
        mkdir -p "$VST3_DEST" "$AU_DEST"
        cp -R "$ARTEFACTS/VST3/MooDuck.vst3" "$VST3_DEST/"
        if [[ -d "$ARTEFACTS/AU/MooDuck.component" ]]; then
            cp -R "$ARTEFACTS/AU/MooDuck.component" "$AU_DEST/"
        fi
        echo "Installed VST3 to $VST3_DEST and AU to $AU_DEST"
        ;;
    Linux)
        VST3_DEST="$HOME/.vst3"
        mkdir -p "$VST3_DEST"
        cp -R "$ARTEFACTS/VST3/MooDuck.vst3" "$VST3_DEST/"
        echo "Installed VST3 to $VST3_DEST"
        ;;
    *)
        echo "error: unsupported platform $(uname -s) — on Windows use scripts/install.ps1" >&2
        exit 1
        ;;
esac
