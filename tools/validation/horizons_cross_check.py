#!/usr/bin/env python3
"""Compare every simulated major-body ephemeris with JPL Horizons/DE441."""

from __future__ import annotations

import argparse
import csv
import math
import re
import subprocess
import time
import urllib.parse
from pathlib import Path


BODIES = [
    ("Mercury", "1"), ("Venus", "2"), ("Earth", "399"), ("Moon", "301"),
    ("Mars Barycenter", "4"), ("Jupiter Barycenter", "5"),
    ("Saturn Barycenter", "6"), ("Uranus Barycenter", "7"),
    ("Neptune Barycenter", "8"),
]
EPOCHS = ["1900-02-17 03:00", "2026-09-13 12:34", "2099-11-05 18:00"]
API = "https://ssd.jpl.nasa.gov/api/horizons.api"
NUMBER = r"[-+0-9.]+(?:[Ee][-+]?\d+)?"


def horizons(command: str, epoch: str) -> tuple[list[float], list[float]]:
    # STOP_TIME must not repeat the time scale: Horizons accepts it only on START_TIME.
    start = epoch + " TDB"
    minute = epoch[:-2] + f"{int(epoch[-2:]) + 1:02d}"
    parameters = {
        "format": "text", "COMMAND": f"'{command}'", "EPHEM_TYPE": "'VECTORS'",
        "CENTER": "'@0'", "START_TIME": f"'{start}'", "STOP_TIME": f"'{minute}'",
        "STEP_SIZE": "'1 m'", "OUT_UNITS": "'KM-S'", "REF_PLANE": "'FRAME'",
        "REF_SYSTEM": "'ICRF'", "VEC_TABLE": "'2'", "CSV_FORMAT": "'YES'",
    }
    url = API + "?" + urllib.parse.urlencode(parameters)
    for attempt in range(5):
        result = subprocess.run(["curl", "--silent", "--show-error", "--fail", url],
                                text=True, capture_output=True, check=False)
        if result.returncode == 0 and "$$SOE" in result.stdout:
            line = result.stdout.split("$$SOE", 1)[1].splitlines()[1]
            fields = [field.strip() for field in line.split(",")]
            values = [float(value) * 1000.0 for value in fields[2:8]]
            return values[:3], values[3:]
        time.sleep(1.0 + attempt)
    raise RuntimeError(f"Horizons failed for command {command} at {epoch}: {result.stderr}")


def local(orbit_cli: Path, name: str, epoch: str) -> tuple[list[float], list[float]]:
    result = subprocess.run([str(orbit_cli), "body", name, "--date", epoch + " TDB"],
                            text=True, capture_output=True, check=True)
    blocks = result.stdout.split("velocity [m/s]", 1)
    position = [float(v) for v in re.findall(rf"^\s+[xyz]\s+({NUMBER}) m$", blocks[0], re.M)]
    velocity = [float(v) for v in re.findall(rf"^\s+[xyz]\s+({NUMBER}) m/s$", blocks[1], re.M)]
    if len(position) != 3 or len(velocity) != 3:
        raise RuntimeError(f"could not parse orbit-cli output for {name} at {epoch}")
    return position, velocity


def norm_delta(left: list[float], right: list[float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--orbit-cli", type=Path, default=Path("build/bin/orbit-cli"))
    parser.add_argument("--csv", type=Path, default=Path("docs/validation/ephemeris-horizons.csv"))
    parser.add_argument("--summary", type=Path,
                        default=Path("docs/validation/ephemeris-horizons.md"))
    args = parser.parse_args()

    rows = []
    for epoch in EPOCHS:
        for name, command in BODIES:
            reference_position, reference_velocity = horizons(command, epoch)
            actual_position, actual_velocity = local(args.orbit_cli, name, epoch)
            rows.append({
                "body": name, "epoch_tdb": epoch + " TDB",
                "position_difference_m": norm_delta(actual_position, reference_position),
                "velocity_difference_ms": norm_delta(actual_velocity, reference_velocity),
                "position_scale_m": math.sqrt(sum(x * x for x in reference_position)),
                "reference": "Horizons DE441, SSB/ICRF, geometric",
                "simulator": "CSPICE de440s, SSB/J2000, geometric",
            })
            time.sleep(0.15)

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    lines = [
        "# Ephemeris cross-check: DE440 vs Horizons/DE441", "",
        "Independent geometric SSB/ICRF states at past, present and future epochs. "
        "The residual includes the real difference between DE440 and DE441; it is not an "
        "integration error. Raw cases are in the adjacent CSV.", "",
        "| Body | max |Δr| [m] | max |Δv| [m/s] | max relative |Δr| |", "|---|---:|---:|---:|",
    ]
    for name, _ in BODIES:
        selected = [row for row in rows if row["body"] == name]
        lines.append(f"| {name} | {max(float(r['position_difference_m']) for r in selected):.6g} | "
                     f"{max(float(r['velocity_difference_ms']) for r in selected):.6g} | "
                     f"{max(float(r['position_difference_m']) / float(r['position_scale_m']) for r in selected):.3e} |")
    lines += ["", "Generated by `tools/validation/horizons_cross_check.py`. No simulator "
              "ephemeris function is used to create the expected values.", ""]
    report = "\n".join(lines)
    args.summary.write_text(report, encoding="utf-8")
    print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
