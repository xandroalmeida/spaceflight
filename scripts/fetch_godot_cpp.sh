#!/usr/bin/env bash
# Downloads godot-cpp, the C++ binding layer for GDExtension, into external/.
#
# Kept out of Git for the same reason as CSPICE: it is a third-party tree that
# belongs to its own project, is pinned by a version we choose deliberately, and
# is large.  See ADR-0002.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external"

# Must match the Godot editor version the project is opened with: godot-cpp
# generates its bindings from the engine's extension_api.json, and a mismatch is
# a link error at best.
VERSION="${GODOT_CPP_VERSION:-godot-4.5-stable}"

if [[ -f "${DEST}/godot-cpp/include/godot_cpp/godot.hpp" ]]; then
    echo "godot-cpp already present at ${DEST}/godot-cpp"
    exit 0
fi

URL="https://github.com/godotengine/godot-cpp/archive/refs/tags/${VERSION}.tar.gz"
mkdir -p "${DEST}"
echo "Downloading ${URL}"
curl -fSL --retry 3 -o "${DEST}/godot-cpp.tar.gz" "${URL}"

echo "Extracting"
tar -xzf "${DEST}/godot-cpp.tar.gz" -C "${DEST}"
rm -rf "${DEST}/godot-cpp"
mv "${DEST}/godot-cpp-${VERSION}" "${DEST}/godot-cpp"
rm -f "${DEST}/godot-cpp.tar.gz"

test -f "${DEST}/godot-cpp/include/godot_cpp/godot.hpp"
echo "godot-cpp ${VERSION} ready at ${DEST}/godot-cpp"
