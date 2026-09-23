#!/usr/bin/env bash
# Downloads the Godot editor into external/, pinned to the version godot-cpp is
# pinned to.
#
# Why not Homebrew: `brew install --cask godot` tracks the newest engine (4.7.2 at
# the time of writing), while godot-cpp publishes no tag above godot-4.5-stable.
# GDExtension is generally forward compatible inside 4.x -- the extension declares
# compatibility_minimum and Godot honours it -- but "generally" is not what you
# want while verifying that the bridge works at all.  Pin both ends, then relax.
#
# Nothing here touches /Applications: the editor lands in external/, which is
# gitignored and removable with rm -rf.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external"
VERSION="${GODOT_VERSION:-4.5-stable}"

# Only external/: a `godot` on PATH may be any version, and this pins one.
source "${ROOT}/scripts/godot_bin.sh"
EXISTING="$(GODOT_BIN= PATH= godot_bin "${ROOT}")"
if [[ -n "${EXISTING}" ]]; then
    echo "Godot already present: ${EXISTING}"
    exit 0
fi

uname_s="$(uname -s)"
uname_m="$(uname -m)"
case "${uname_s}/${uname_m}" in
    Darwin/*)       ASSET="Godot_v${VERSION}_macos.universal.zip" ;;
    Linux/x86_64)   ASSET="Godot_v${VERSION}_linux.x86_64.zip" ;;
    Linux/aarch64)  ASSET="Godot_v${VERSION}_linux.arm64.zip" ;;
    *) echo "Unsupported platform ${uname_s}/${uname_m}; download from https://godotengine.org/download" >&2; exit 1 ;;
esac

URL="https://github.com/godotengine/godot/releases/download/${VERSION}/${ASSET}"
mkdir -p "${DEST}/godot"
echo "Downloading ${URL}"
curl -fSL --retry 3 -o "${DEST}/godot.zip" "${URL}"
unzip -q -o "${DEST}/godot.zip" -d "${DEST}/godot"
rm -f "${DEST}/godot.zip"

if [[ "${uname_s}" == "Darwin" ]]; then
    # The official build is signed and notarised; clearing the quarantine flag
    # only avoids the first-run prompt when launching from a terminal.
    xattr -dr com.apple.quarantine "${DEST}/godot/Godot.app" 2>/dev/null || true
    echo "Godot ${VERSION} ready: ${DEST}/godot/Godot.app"
else
    chmod +x "${DEST}/godot/"Godot_v*
    echo "Godot ${VERSION} ready in ${DEST}/godot"
fi
