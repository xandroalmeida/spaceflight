#!/usr/bin/env bash
# Downloads the minimal SPICE kernel set into kernels/spice/.
#
# Kernels are data, not code: they are large, versioned by NAIF, and deliberately
# kept out of Git.  See kernels/MANIFEST.md for provenance and checksums.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
K="${ROOT}/kernels/spice"
BASE="https://naif.jpl.nasa.gov/pub/naif/generic_kernels"

FULL_EPHEMERIS="${SPACEFLIGHT_FULL_EPHEMERIS:-0}"

mkdir -p "${K}/lsk" "${K}/pck" "${K}/spk"

fetch() {  # fetch <url> <dest>
    local url="$1" dest="$2"
    if [[ -s "${dest}" ]]; then
        echo "  have $(basename "${dest}")"
        return
    fi
    echo "  get  $(basename "${dest}")"
    curl -fSL --retry 3 -o "${dest}" "${url}"
}

echo "Fetching SPICE kernels into ${K}"
fetch "${BASE}/lsk/naif0012.tls"        "${K}/lsk/naif0012.tls"
fetch "${BASE}/pck/pck00011.tpc"        "${K}/pck/pck00011.tpc"
fetch "${BASE}/pck/gm_de440.tpc"        "${K}/pck/gm_de440.tpc"
fetch "${BASE}/spk/planets/de440s.bsp"  "${K}/spk/de440s.bsp"

# Mars and its two moons.  DE440 stops at the Mars system BARYCENTRE (body 4);
# the planet itself (499) has no segment in it, so without this kernel a Mars
# mission cannot be planned, drawn or captured into -- SPICE answers
# SPKINSUFFDATA and the game has no destination.
#
# The "s" is the short span: 1995-2050 in 64 MB, against 1.2 GB for 1550-2650.
# It also carries segments for the Sun, the Earth and the Earth/Mars
# barycentres, which the loader reaches AFTER de440s.bsp and which therefore
# take precedence.  Measured rather than assumed: the Earth's barycentric
# position at 2026-01-01 is identical to the last printed digit with and without
# this kernel loaded, because MAR099 is fitted to DE440.
fetch "${BASE}/spk/satellites/mar099s.bsp" "${K}/spk/mar099s.bsp"

if [[ "${FULL_EPHEMERIS}" == "1" ]]; then
    # DE440 full span (1550-2650), 114 MB.  de440s (1849-2150) is the default.
    fetch "${BASE}/spk/planets/de440.bsp" "${K}/spk/de440.bsp"
fi

echo "Done."
