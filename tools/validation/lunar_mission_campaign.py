#!/usr/bin/env python3
"""Run the Earth-to-Moon scientific regression over many departure epochs."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import re
import statistics
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


FLOAT = r"([-+0-9.eE]+)"


def value(pattern: str, text: str, group: int = 1) -> float | None:
    match = re.search(pattern, text, re.MULTILINE)
    return float(match.group(group)) if match else None


def classify_failure(text: str, returncode: int) -> str:
    if "trajectory entered Moon" in text or "INSIDE Moon" in text:
        return "trajectory intersects the lunar surface"
    if "NOT captured:" in text:
        return "lunar insertion remained hyperbolic"
    if returncode != 0:
        if "CAPTURED:" in text:
            return "process returned nonzero after reporting capture"
        return f"process exit {returncode} without a classified result"
    if "Lambert" not in text:
        return "Lambert solution absent"
    if not re.search(r"final miss\s+:\s+" + FLOAT + r" km\s+converged", text):
        return "position corrector did not converge"
    if not re.search(r"status\s+:\s+converged", text):
        return "B-plane corrector did not converge"
    if "CAPTURED:" not in text:
        return "lunar insertion did not produce a closed orbit"
    return ""


def run_case(index: int, epoch: dt.datetime, orbit_cli: Path, scenario: Path,
             timeout: float) -> dict[str, object]:
    epoch_text = epoch.strftime("%Y-%m-%d %H:%M:%S TDB")
    command = [
        str(orbit_cli), "intercept", str(scenario), "--to", "Moon", "--tof", "4.5",
        "--date", epoch_text, "--flyby-altitude-km", "100", "--b-plane-angle", "0",
        "--tolerance-km", "1", "--insert",
    ]
    try:
        completed = subprocess.run(command, text=True, capture_output=True, timeout=timeout,
                                   check=False)
        text = completed.stdout + "\n" + completed.stderr
        reason = classify_failure(text, completed.returncode)
        returncode = completed.returncode
    except subprocess.TimeoutExpired as exc:
        text = (exc.stdout or "") + "\n" + (exc.stderr or "")
        reason = f"timeout after {timeout:g} s"
        returncode = 124

    final_orbit = text.split("orbit about Moon, one revolution after the burn:", 1)
    orbit_text = final_orbit[1] if len(final_orbit) == 2 else ""
    injection = value(r"lambert injection:\s+" + FLOAT + r" s,\s+" + FLOAT +
                      r" kg, engine delta-v\s+" + FLOAT, text, 3)
    fuel_left = value(r"propellant left:\s+" + FLOAT + r" kg", orbit_text)
    closest_altitude = value(r"closest approach\s+:.*?\(altitude\s+" + FLOAT + r" km\)", text)

    return {
        "case": index,
        "epoch_tdb": epoch_text,
        "success": not reason,
        "failure_mode": reason,
        "returncode": returncode,
        "lambert_converged": bool(re.search(r"final miss\s+:\s+" + FLOAT +
                                             r" km\s+converged", text)),
        "b_plane_converged": bool(re.search(r"status\s+:\s+converged", text)),
        "closest_approach_altitude_km": closest_altitude,
        "target_periapsis_error_km": (abs(closest_altitude - 100.0)
                                       if closest_altitude is not None else None),
        "insertion_delta_v_ms": value(r"delta-v needed\s+:\s+" + FLOAT, text),
        "final_eccentricity": value(r"eccentricity\s+:\s+" + FLOAT, orbit_text),
        "final_apoapsis_altitude_km": (
            (value(r"apoapsis\s+:\s+" + FLOAT, orbit_text) or 0.0) - 1737.4
            if re.search(r"apoapsis\s+:\s+" + FLOAT, orbit_text) else None),
        "final_periapsis_altitude_km": (
            (value(r"periapsis\s+:\s+" + FLOAT, orbit_text) or 0.0) - 1737.4
            if re.search(r"periapsis\s+:\s+" + FLOAT, orbit_text) else None),
        "fuel_used_kg": (19000.0 - fuel_left if fuel_left is not None else None),
        "injection_delta_v_ms": (float(injection) if injection is not None else None),
    }


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    position = fraction * (len(ordered) - 1)
    lo = int(position)
    hi = min(lo + 1, len(ordered) - 1)
    weight = position - lo
    return ordered[lo] * (1.0 - weight) + ordered[hi] * weight


def summary(rows: list[dict[str, object]], start: dt.datetime, cadence: float) -> str:
    successful = [row for row in rows if row["success"]]
    lines = [
        "# Earth → Moon validation campaign",
        "",
        f"Generated from {len(rows)} deterministic cases beginning {start:%Y-%m-%d}, "
        f"spaced by {cadence:g} day(s). Each case uses the same spacecraft and parameters; "
        "only the TDB departure epoch changes.",
        "",
        f"Success rate: **{len(successful)}/{len(rows)} "
        f"({100.0 * len(successful) / len(rows):.1f}%)**.",
        f"Lambert/position-corrector convergence: **{sum(bool(r['lambert_converged']) for r in rows)}/{len(rows)}**.  ",
        f"B-plane convergence: **{sum(bool(r['b_plane_converged']) for r in rows)}/{len(rows)}**.",
        "",
        "| Metric | min | median | p95 | max |",
        "|---|---:|---:|---:|---:|",
    ]
    metrics = [
        ("absolute periapsis error [km]", "target_periapsis_error_km"),
        ("insertion delta-v [m/s]", "insertion_delta_v_ms"),
        ("final eccentricity", "final_eccentricity"),
        ("final apoapsis altitude [km]", "final_apoapsis_altitude_km"),
        ("final periapsis altitude [km]", "final_periapsis_altitude_km"),
        ("fuel used [kg]", "fuel_used_kg"),
    ]
    for label, key in metrics:
        values = [float(row[key]) for row in successful if row[key] is not None]
        lines.append(f"| {label} | {min(values):.6g} | {statistics.median(values):.6g} | "
                     f"{percentile(values, .95):.6g} | {max(values):.6g} |" if values else
                     f"| {label} | n/a | n/a | n/a | n/a |")

    failures: dict[str, int] = {}
    for row in rows:
        reason = str(row["failure_mode"])
        if reason:
            failures[reason] = failures.get(reason, 0) + 1
    lines += ["", "## Failure modes", ""]
    if failures:
        lines += [f"- {count} × {reason}" for reason, count in sorted(failures.items())]
    else:
        lines.append("No failures in this campaign.")
    lines += [
        "",
        "The CSV beside this report contains every case. This campaign is a robustness sample, "
        "not evidence outside its epoch range, transfer time, B-plane angle, force model or "
        "spacecraft configuration.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--orbit-cli", type=Path, default=Path("build/bin/orbit-cli"))
    parser.add_argument("--scenario", type=Path, default=Path("tests/scenarios/lunar-intercept.json"))
    parser.add_argument("--start", default="2026-01-01")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--cadence-days", type=float, default=1.0)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--csv", type=Path, default=Path("docs/validation/lunar-mission-campaign.csv"))
    parser.add_argument("--summary", type=Path,
                        default=Path("docs/validation/lunar-mission-campaign.md"))
    args = parser.parse_args()
    if args.count < 100:
        parser.error("Milestone 6 requires at least 100 geometries")

    start = dt.datetime.fromisoformat(args.start)
    epochs = [start + dt.timedelta(days=i * args.cadence_days) for i in range(args.count)]
    rows: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(run_case, i, epoch, args.orbit_cli, args.scenario, args.timeout)
                   for i, epoch in enumerate(epochs)]
        for future in as_completed(futures):
            rows.append(future.result())
    rows.sort(key=lambda row: int(row["case"]))

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    args.summary.write_text(summary(rows, start, args.cadence_days), encoding="utf-8")
    print(summary(rows, start, args.cadence_days))
    print(f"\nCases: {args.csv}\nSummary: {args.summary}")
    return 0 if all(bool(row["success"]) for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
