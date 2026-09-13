#!/usr/bin/env bash
# Downloads and extracts the NAIF CSPICE toolkit into external/cspice.
#
# Platform packages: https://naif.jpl.nasa.gov/naif/toolkit_C.html
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/external"
BASE="https://naif.jpl.nasa.gov/pub/naif/toolkit//C"

if [[ -f "${DEST}/cspice/include/SpiceUsr.h" ]]; then
    echo "CSPICE already present at ${DEST}/cspice"
    exit 0
fi

uname_s="$(uname -s)"
uname_m="$(uname -m)"
case "${uname_s}/${uname_m}" in
    Darwin/arm64)  PKG="MacM1_OSX_clang_64bit" ;;
    Darwin/x86_64) PKG="MacIntel_OSX_AppleC_64bit" ;;
    Linux/x86_64)  PKG="PC_Linux_GCC_64bit" ;;
    Linux/aarch64) PKG="PC_Linux_GCC_64bit" ;;   # source build; verify if it fails
    *) echo "Unsupported platform ${uname_s}/${uname_m}; download manually from ${BASE}" >&2; exit 1 ;;
esac

URL="${BASE}/${PKG}/packages/cspice.tar.Z"
mkdir -p "${DEST}"
echo "Downloading ${URL}"
curl -fSL --retry 3 -o "${DEST}/cspice.tar.Z" "${URL}"

echo "Extracting"
gzip -dc "${DEST}/cspice.tar.Z" > "${DEST}/cspice.tar"
tar -xf "${DEST}/cspice.tar" -C "${DEST}"
rm -f "${DEST}/cspice.tar"

test -f "${DEST}/cspice/include/SpiceUsr.h"
echo "CSPICE ready at ${DEST}/cspice"
