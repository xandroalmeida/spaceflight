#!/usr/bin/env bash
# The Earth-to-Moon campaign, split across processes.
#
# Processes and not threads, and that is measured rather than assumed: CSPICE is
# a Fortran translation with global state, so every ephemeris call in the core
# goes through one mutex.  Threads inside `lunar-campaign` do not share the work,
# they queue for it -- 7.5 s per epoch on two threads against 29 s on six.
# Separate processes have separate SPICE and scale properly.
#
#   scripts/lunar_campaign.sh [epochs] [jobs] [output.csv] [extra args...]
#
# Each worker takes a contiguous block of epochs, writes its own CSV, and the
# blocks are concatenated at the end in epoch order.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAMPAIGN="${ROOT}/build/bin/lunar-campaign"
SCENARIO="${ROOT}/tests/scenarios/lunar-intercept.json"

EPOCHS="${1:-365}"
JOBS="${2:-8}"
OUTPUT="${3:-${ROOT}/docs/validation/lunar-navigation-campaign-v2.csv}"
shift 3 2>/dev/null || shift $# || true

if [[ ! -x "${CAMPAIGN}" ]]; then
    echo "lunar-campaign is not built. Run:" >&2
    echo "  cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

block=$(( (EPOCHS + JOBS - 1) / JOBS ))
pids=()
for (( i = 0; i < JOBS; ++i )); do
    start=$(( i * block ))
    (( start >= EPOCHS )) && break
    count=$(( block ))
    (( start + count > EPOCHS )) && count=$(( EPOCHS - start ))
    # --date is the first epoch of this worker's block; the tool then walks one
    # day at a time from there, so the blocks tile the year with no overlap.
    date=$(python3 -c "
import datetime as dt
print((dt.datetime(2026, 1, 1) + dt.timedelta(days=${start})).strftime('%Y-%m-%d %H:%M:%S TDB'))")
    "${CAMPAIGN}" "${SCENARIO}" --epochs "${count}" --date "${date}" \
        --csv "${WORK}/block_$(printf '%03d' "${i}").csv" "$@" \
        > "${WORK}/block_$(printf '%03d' "${i}").log" 2>&1 &
    pids+=($!)
done

status=0
for pid in "${pids[@]}"; do
    wait "${pid}" || status=$?
done

head -1 "${WORK}"/block_000.csv > "${OUTPUT}"
for block_csv in "${WORK}"/block_*.csv; do
    tail -n +2 "${block_csv}" >> "${OUTPUT}"
done

echo "epochs      : ${EPOCHS}"
echo "workers     : ${#pids[@]}"
echo "rows        : $(( $(wc -l < "${OUTPUT}") - 1 ))"
echo "successes   : $(grep -c ',SUCCESS,' "${OUTPUT}" || true)"
echo "output      : ${OUTPUT}"
cat "${WORK}"/block_*.log | grep -E '^  \[' | grep -c 'FAILURE' | \
    xargs -I{} echo "failures    : {}"

# The per-worker summaries, so the taxonomy counts survive the concatenation.
echo
echo "failure reasons:"
awk -F',' 'NR > 1 && $0 !~ /,SUCCESS,/ { for (i = 1; i <= NF; ++i) if ($i == "FAILURE") print $(i+1) }' \
    "${OUTPUT}" | sort | uniq -c | sort -rn

exit ${status}
