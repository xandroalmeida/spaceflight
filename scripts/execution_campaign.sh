#!/usr/bin/env bash
# The execution campaign: planner + autopilot + finite burns, scored apart.
#
# Milestone 6.2 sections 19, 20 and 21.
#
# ---------------------------------------------------------------------------
# Processes, not threads
#
# CSPICE is a Fortran translation with global state, so every ephemeris call in
# the core goes through one mutex.  Threads inside `lunar-campaign` do not share
# the work, they queue for it -- 7.5 s per epoch on two threads against 29 s on
# six.  Separate processes have separate SPICE and scale properly, which is the
# same reason scripts/lunar_campaign.sh is shaped this way.
#
# ---------------------------------------------------------------------------
# Why this is so much slower than the planning campaign
#
# A FINITE_BURN epoch costs about 1.7 s.  An AUTOPILOT epoch costs about two
# minutes, and the factor of seventy is not waste: under AUTOPILOT the two
# differential correctors invert a map that has the attitude controller and
# twelve RCS thrusters inside it, so every one of the few hundred probe flights
# per epoch integrates a quaternion and an angular velocity alongside the orbit,
# with an error controller that has to resolve both.
#
# That cost is the measurement.  Correcting an IMPULSIVE map and then flying an
# autopilot is what a planner does when it wants cheap numbers, and it produces a
# departure velocity that was never corrected for the trajectory the ship
# actually flies.
#
#   scripts/execution_campaign.sh [epochs] [jobs] [output.csv] [extra args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAMPAIGN="${ROOT}/build/bin/lunar-campaign"
SCENARIO="${ROOT}/tests/scenarios/lunar-intercept.json"

EPOCHS="${1:-30}"
JOBS="${2:-8}"
OUTPUT="${3:-${ROOT}/docs/validation/mission-execution-campaign.csv}"
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
    # day at a time from there, so the blocks tile the span with no overlap.
    date=$(python3 -c "
import datetime as dt
print((dt.datetime(2026, 1, 1) + dt.timedelta(days=${start})).strftime('%Y-%m-%d %H:%M:%S TDB'))")
    "${CAMPAIGN}" "${SCENARIO}" --execution-campaign --epochs "${count}" --date "${date}" \
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
cat "${WORK}"/block_*.log > "${OUTPUT%.csv}.log"

rows=$(( $(wc -l < "${OUTPUT}") - 1 ))
echo "cases       : ${rows}"
echo "output      : ${OUTPUT}"
echo "logs        : ${OUTPUT%.csv}.log"
echo

# The three questions of section 19, counted separately.  Collapsing them is what
# made Milestone 6 unable to say whether the planner or the engine was at fault.
python3 - "${OUTPUT}" <<'PY'
import csv, sys
from collections import Counter, defaultdict

rows = list(csv.DictReader(open(sys.argv[1])))
if not rows:
    print("no rows")
    raise SystemExit(1)

def tally(subset):
    return (len(subset),
            sum(r["planning"] == "PASS" for r in subset),
            sum(r["execution"] == "PASS" for r in subset),
            sum(r["final_orbit"] == "PASS" for r in subset))

groups = defaultdict(list)
for r in rows:
    key = (r["parking_altitude_km"], r["parking_inclination_deg"], r["fuel_scenario"])
    groups[key].append(r)

print(f'{"configuration":<42}{"cases":>8}{"planning":>11}{"execution":>11}{"orbit":>8}')
for key in sorted(groups):
    n, p, e, o = tally(groups[key])
    label = f"{float(key[0]):.0f} km / {float(key[1]):.1f} deg / {key[2]}"
    print(f"{label:<42}{n:>8}{p:>11}{e:>11}{o:>8}")
n, p, e, o = tally(rows)
print(f'{"TOTAL":<42}{n:>8}{p:>11}{e:>11}{o:>8}')

print()
print("verdicts")
for verdict, count in Counter(r["verdict"] for r in rows).most_common():
    print(f"  {verdict:<28}{count:>6}")

print()
print("failure reasons")
for reason, count in Counter(
        r["planning_failure"] for r in rows if r["planning"] != "PASS").most_common():
    print(f"  planning   {reason:<34}{count:>6}")
for reason, count in Counter(
        r["execution_failure"] for r in rows
        if r["planning"] == "PASS" and r["final_orbit"] != "PASS").most_common():
    print(f"  execution  {reason:<34}{count:>6}")
PY

exit ${status}
