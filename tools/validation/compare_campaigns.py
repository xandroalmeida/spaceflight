#!/usr/bin/env python3
"""Compare two lunar-campaign CSVs, epoch by epoch.

Milestone 6.2 section 7 asks the campaign to be re-run through the public
planner interface and says:

    "Qualquer divergência em relação à campanha anterior deve ser explicada."

This is the instrument that finds the divergences worth explaining.  It matches
rows on `requested_epoch_tdb_s`, compares the columns that describe the CHOSEN
trajectory and the ACHIEVED orbit, and reports the distribution of each
difference rather than a pass/fail -- because "identical" and "different" are
both possible correct answers, and which one it is has to be read rather than
asserted.

    tools/validation/compare_campaigns.py before.csv after.csv

Columns that are expected to differ are not compared and are listed below with
the reason.  Wall time is the obvious one: it is a property of the machine.
"""

import csv
import math
import sys
from collections import Counter

# What is compared, and what a difference in it would MEAN.
COMPARED = [
    ("departure_epoch_tdb_s", "s", "a different departure was chosen"),
    ("time_of_flight_s", "s", "a different transfer was chosen"),
    ("lambert_branch", None, "a different Lambert branch was chosen"),
    ("departure_delta_v_ms", "m/s", "the corrected injection differs"),
    ("correction_magnitude_ms", "m/s", "the corrector used different authority"),
    ("bplane_target_t_m", "m", "the flyby was aimed somewhere else"),
    ("bplane_target_r_m", "m", "the flyby was aimed somewhere else"),
    ("required_capture_dv_ms", "m/s", "the capture burn differs"),
    ("actual_periapsis_m", "m", "the flown flyby differs"),
    ("post_burn_ecc", "", "the captured orbit differs in shape"),
    ("post_burn_periapsis_alt_m", "m", "the captured orbit differs in size"),
    ("post_burn_apoapsis_alt_m", "m", "the captured orbit differs in size"),
    ("post_burn_inclination_deg", "deg", "the captured orbit differs in plane"),
    ("propellant_used_kg", "kg", "the mission costs differently"),
]

# Not compared, with the reason stated rather than left to be noticed.
IGNORED = {
    "wall_time_s": "a property of the machine, not of the physics",
    "propagations": "bookkeeping; changes with any search-order change",
    "integrator_steps": "bookkeeping",
    "detail": "free text",
}


def load(path):
    with open(path, newline="") as handle:
        return {row["requested_epoch_tdb_s"]: row for row in csv.DictReader(handle)}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    before, after = load(sys.argv[1]), load(sys.argv[2])
    shared = sorted(set(before) & set(after), key=float)

    print(f"before : {len(before)} rows  ({sys.argv[1]})")
    print(f"after  : {len(after)} rows  ({sys.argv[2]})")
    print(f"shared : {len(shared)} epochs")
    only_before = set(before) - set(after)
    only_after = set(after) - set(before)
    if only_before:
        print(f"  only in before: {len(only_before)}")
    if only_after:
        print(f"  only in after : {len(only_after)}")
    if not shared:
        return 1

    # Outcome first: a campaign that captures a different NUMBER of epochs has
    # diverged in the only way that matters, whatever the columns say.
    outcome = Counter()
    for key in shared:
        outcome[(before[key]["result"], after[key]["result"])] += 1
    print("\noutcome (before -> after)")
    for (was, now), count in sorted(outcome.items()):
        marker = "  " if was == now else "!!"
        print(f"{marker}  {was:>8} -> {now:<8} {count:>6}")

    print(f"\n{'column':<30}{'identical':>11}{'differ':>9}{'max |diff|':>16}  meaning")
    diverged = False
    for column, unit, meaning in COMPARED:
        if column not in next(iter(before.values())) or column not in next(iter(after.values())):
            print(f"{column:<30}{'(absent)':>11}")
            continue
        identical = 0
        differing = 0
        worst = 0.0
        for key in shared:
            was, now = before[key][column], after[key][column]
            if unit is None:
                if was == now:
                    identical += 1
                else:
                    differing += 1
                continue
            try:
                a, b = float(was), float(now)
            except ValueError:
                identical += was == now
                differing += was != now
                continue
            if math.isnan(a) and math.isnan(b):
                identical += 1
                continue
            # Bit-identical, not "close": the two runs are the same arithmetic on
            # the same inputs, so anything else is a real change and the size of
            # it is what has to be explained.
            if a == b:
                identical += 1
            else:
                differing += 1
                worst = max(worst, abs(a - b))
        if differing:
            diverged = True
        suffix = f"{worst:.6g} {unit}" if unit else ""
        print(f"{column:<30}{identical:>11}{differing:>9}{suffix:>16}  {meaning}")

    print("\nnot compared")
    for column, why in IGNORED.items():
        print(f"  {column:<24}{why}")

    return 1 if diverged else 0


if __name__ == "__main__":
    sys.exit(main())
