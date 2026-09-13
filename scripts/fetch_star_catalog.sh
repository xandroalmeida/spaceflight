#!/usr/bin/env bash
# Downloads the Yale Bright Star Catalogue into catalogs/.
#
# Same policy as the SPICE kernels: a catalogue is DATA, versioned by whoever
# publishes it, and is not committed.  See catalogs/MANIFEST.md for provenance.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
C="${ROOT}/catalogs"

# VizieR V/50 is the canonical distribution of Hoffleit & Warren (1991); the
# Harvard mirror carries the identical bytes under a different name and is the
# fallback when CDS is unreachable.
PRIMARY="https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz"
MIRROR="http://tdc-www.harvard.edu/catalogs/bsc5.dat.gz"
README="https://cdsarc.cds.unistra.fr/ftp/V/50/ReadMe"

mkdir -p "${C}"

if [[ -s "${C}/bsc5.dat" ]]; then
    echo "  have bsc5.dat ($(wc -l < "${C}/bsc5.dat" | tr -d ' ') records)"
else
    echo "Fetching the Yale Bright Star Catalogue into ${C}"
    if ! curl -fSL --retry 3 --max-time 120 -o "${C}/bsc5.dat.gz" "${PRIMARY}"; then
        echo "  CDS unreachable, trying the Harvard mirror"
        curl -fSL --retry 3 --max-time 120 -o "${C}/bsc5.dat.gz" "${MIRROR}"
    fi
    gunzip -f "${C}/bsc5.dat.gz"
    echo "  got bsc5.dat ($(wc -l < "${C}/bsc5.dat" | tr -d ' ') records)"
fi

if [[ ! -s "${C}/bsc5.readme" ]]; then
    # The byte-by-byte column description.  Worth keeping next to the data: the
    # parser's column numbers came from it and are unverifiable without it.
    curl -fSL --retry 3 --max-time 60 -o "${C}/bsc5.readme" "${README}" || \
        echo "  (could not fetch the ReadMe; the catalogue itself is what matters)"
fi

echo "Done."
