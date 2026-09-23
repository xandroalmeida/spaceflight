#!/usr/bin/env python3
"""Render a trajectory CSV from `orbit-cli propagate` as a standalone HTML page.

Python standard library only, by design: a validation tool that needs matplotlib
installed is a validation tool that does not get run.  The output is one
self-contained file with inline SVG -- no network, no assets, no build step.

    ./build/bin/orbit-cli propagate tests/scenarios/leo-circular.json \
        --samples 600 --csv /tmp/leo.csv
    ./tools/validation/plot_trajectory.py /tmp/leo.csv -o /tmp/leo.html

This draws what the core computed.  It is NOT the renderer: nothing here knows
about cameras, floating origin or the renderer (ADR-0009).  It exists to make numbers
look like a trajectory, which is how a human spots a wrong one.
"""

from __future__ import annotations

import argparse
import csv
import html
import math
import sys
from dataclasses import dataclass
from pathlib import Path

# Mean radii [m] of the bodies a scenario is likely to be centred on.
BODY_RADII = {
    "earth": 6_371_008.0,
    "moon": 1_737_400.0,
    "sun": 695_700_000.0,
    "mars": 3_389_500.0,
    "venus": 6_051_800.0,
    "mercury": 2_439_700.0,
    "jupiter": 69_911_000.0,
}


@dataclass
class Track:
    t: list[float]
    r: list[tuple[float, float, float]]   # position relative to the central body
    v: list[tuple[float, float, float]]   # velocity relative to the central body
    radius: list[float]
    speed: list[float]
    energy: list[float]
    momentum: list[float]


def read_csv(path: Path) -> Track:
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        sys.exit(f"{path}: no rows")

    def column(name: str) -> list[float]:
        if name not in rows[0]:
            sys.exit(f"{path}: missing column '{name}' "
                     f"(was this written by `orbit-cli propagate --csv`?)")
        return [float(row[name]) for row in rows]

    t = column("t_tdb_s")
    t0 = t[0]
    has_velocity = "rel_vx_ms" in rows[0]
    return Track(
        t=[value - t0 for value in t],
        r=list(zip(column("rel_x_m"), column("rel_y_m"), column("rel_z_m"))),
        v=list(zip(column("rel_vx_ms"), column("rel_vy_ms"), column("rel_vz_ms")))
        if has_velocity else [],
        radius=column("radius_m"),
        speed=column("speed_ms"),
        energy=column("energy_j_kg"),
        momentum=column("angular_momentum_m2_s"),
    )


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    return math.sqrt(dot(a, a))


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def orbital_plane_basis(track: Track):
    """Orthonormal basis (e1, e2) spanning the orbital plane.

    Projecting onto the plane the motion actually lies in shows the conic
    undistorted; an XY projection of an inclined orbit shows a squashed one and
    invites the reader to conclude the eccentricity is wrong.
    """
    if track.v:
        h = cross(track.r[0], track.v[0])
    else:
        # Fall back to two distinct radius vectors.
        h = (0.0, 0.0, 0.0)
        for i in range(1, len(track.r)):
            candidate = cross(track.r[0], track.r[i])
            if norm(candidate) > 0.0:
                h = candidate
                break
    if norm(h) == 0.0:
        return (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)

    e1 = scale(track.r[0], 1.0 / norm(track.r[0]))
    n = scale(h, 1.0 / norm(h))
    e2 = cross(n, e1)
    return e1, e2


def format_si(value: float, unit: str) -> str:
    magnitude = abs(value)
    for factor, prefix in ((1e9, "G"), (1e6, "M"), (1e3, "k")):
        if magnitude >= factor:
            return f"{value / factor:.4g} {prefix}{unit}"
    return f"{value:.4g} {unit}"


def polyline(points, **attrs) -> str:
    coords = " ".join(f"{x:.3f},{y:.3f}" for x, y in points)
    extra = " ".join(f'{k.replace("_", "-")}="{v}"' for k, v in attrs.items())
    return f'<polyline points="{coords}" {extra}/>'


def orbit_panel(track: Track, body_radius: float, body_name: str) -> str:
    e1, e2 = orbital_plane_basis(track)
    projected = [(dot(r, e1), dot(r, e2)) for r in track.r]

    extent = max(max(abs(x), abs(y)) for x, y in projected)
    extent = max(extent, body_radius) * 1.12

    size = 520
    centre = size / 2.0
    k = centre / extent

    points = [(centre + x * k, centre - y * k) for x, y in projected]
    body_pixels = body_radius * k

    grid = []
    for fraction in (0.25, 0.5, 0.75, 1.0):
        grid.append(f'<circle cx="{centre}" cy="{centre}" r="{centre * fraction:.2f}" '
                    f'class="grid"/>')

    marker = ""
    if body_pixels >= 1.0:
        marker = (f'<circle cx="{centre}" cy="{centre}" r="{body_pixels:.2f}" class="body"/>')
    else:
        marker = f'<circle cx="{centre}" cy="{centre}" r="2.5" class="body"/>'

    start = points[0]
    end = points[-1]

    return f"""
<svg viewBox="0 0 {size} {size}" class="plot" role="img"
     aria-label="Trajectory projected onto the orbital plane">
  {''.join(grid)}
  <line x1="{centre}" y1="0" x2="{centre}" y2="{size}" class="axis"/>
  <line x1="0" y1="{centre}" x2="{size}" y2="{centre}" class="axis"/>
  {marker}
  {polyline(points, fill="none", class_="track")}
  <circle cx="{start[0]:.2f}" cy="{start[1]:.2f}" r="4" class="start"/>
  <circle cx="{end[0]:.2f}" cy="{end[1]:.2f}" r="4" class="end"/>
  <text x="10" y="20" class="label">scale: full circle = {format_si(extent, 'm')}</text>
  <text x="10" y="{size - 10}" class="label">{html.escape(body_name)} drawn to scale
    (r = {format_si(body_radius, 'm')})</text>
</svg>"""


def series_panel(x_values, y_values, x_label, y_label, colour_class) -> str:
    width, height = 520, 190
    pad_left, pad_bottom, pad_top, pad_right = 62, 28, 14, 12

    x_min, x_max = min(x_values), max(x_values)
    y_min, y_max = min(y_values), max(y_values)
    if x_max == x_min:
        x_max = x_min + 1.0
    if y_max == y_min:
        y_max += abs(y_max) * 1e-12 + 1e-12

    def to_pixels(x, y):
        px = pad_left + (x - x_min) / (x_max - x_min) * (width - pad_left - pad_right)
        py = height - pad_bottom - (y - y_min) / (y_max - y_min) * (height - pad_bottom - pad_top)
        return px, py

    points = [to_pixels(x, y) for x, y in zip(x_values, y_values)]

    ticks = []
    for fraction in (0.0, 0.5, 1.0):
        value = y_min + fraction * (y_max - y_min)
        _, py = to_pixels(x_min, value)
        ticks.append(f'<line x1="{pad_left}" y1="{py:.1f}" x2="{width - pad_right}" '
                     f'y2="{py:.1f}" class="grid"/>'
                     f'<text x="{pad_left - 6}" y="{py + 4:.1f}" class="tick" '
                     f'text-anchor="end">{value:.6g}</text>')

    return f"""
<svg viewBox="0 0 {width} {height}" class="plot" role="img"
     aria-label="{html.escape(y_label)} against {html.escape(x_label)}">
  {''.join(ticks)}
  {polyline(points, fill="none", class_=colour_class)}
  <text x="{pad_left}" y="{height - 8}" class="tick">{html.escape(x_label)}:
    0 .. {x_max:.6g}</text>
  <text x="{pad_left}" y="12" class="tick">{html.escape(y_label)}</text>
</svg>"""


def build_page(track: Track, title: str, body_name: str, body_radius: float) -> str:
    energy_0 = track.energy[0]
    momentum_0 = track.momentum[0]
    energy_drift = [(value - energy_0) / abs(energy_0) for value in track.energy]
    momentum_drift = [(value - momentum_0) / abs(momentum_0) for value in track.momentum]

    duration = track.t[-1]
    stats = [
        ("samples", f"{len(track.t)}"),
        ("duration", f"{duration:.6g} s ({duration / 86400.0:.4g} d)"),
        ("periapsis (sampled)", format_si(min(track.radius), "m")),
        ("apoapsis (sampled)", format_si(max(track.radius), "m")),
        ("speed range", f"{min(track.speed):.6g} .. {max(track.speed):.6g} m/s"),
        ("specific energy drift", f"{energy_drift[-1]:.3e} relative"),
        ("angular momentum drift", f"{momentum_drift[-1]:.3e} relative"),
    ]
    rows = "".join(f"<tr><th>{html.escape(k)}</th><td>{html.escape(v)}</td></tr>"
                   for k, v in stats)

    return f"""<!doctype html>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
  :root {{
    --bg: #0f1216; --panel: #171b21; --ink: #e6e9ee; --muted: #8b95a5;
    --line: #2a313b; --track: #5ec8f2; --body: #3d566e;
    --start: #7ee787; --end: #f2777a; --energy: #d2a8ff; --momentum: #ffab70;
  }}
  body {{ margin: 0; padding: 24px; background: var(--bg); color: var(--ink);
         font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, monospace; }}
  h1 {{ font-size: 16px; font-weight: 600; margin: 0 0 4px; }}
  p.sub {{ color: var(--muted); margin: 0 0 20px; }}
  .grid-2 {{ display: grid; gap: 18px; grid-template-columns: 560px minmax(320px, 1fr); }}
  @media (max-width: 920px) {{ .grid-2 {{ grid-template-columns: 1fr; }} }}
  .card {{ background: var(--panel); border: 1px solid var(--line);
           border-radius: 8px; padding: 14px; }}
  .card h2 {{ font-size: 13px; font-weight: 600; color: var(--muted);
              margin: 0 0 8px; text-transform: uppercase; letter-spacing: .06em; }}
  svg.plot {{ display: block; width: 100%; height: auto; }}
  .grid {{ stroke: var(--line); fill: none; stroke-width: 1; }}
  .axis {{ stroke: var(--line); stroke-width: 1; }}
  .body {{ fill: var(--body); }}
  .track {{ stroke: var(--track); stroke-width: 1.6; }}
  .energy {{ stroke: var(--energy); stroke-width: 1.4; }}
  .momentum {{ stroke: var(--momentum); stroke-width: 1.4; }}
  .radius {{ stroke: var(--track); stroke-width: 1.4; }}
  .start {{ fill: var(--start); }}
  .end {{ fill: var(--end); }}
  .label, .tick {{ fill: var(--muted); font-size: 11px;
                   font-family: ui-monospace, Menlo, monospace; }}
  table {{ border-collapse: collapse; width: 100%; }}
  th {{ text-align: left; font-weight: 400; color: var(--muted);
        padding: 3px 12px 3px 0; white-space: nowrap; }}
  td {{ text-align: right; }}
  .legend {{ color: var(--muted); margin-top: 10px; font-size: 12px; }}
  .legend b {{ font-weight: 400; }}
  .legend .s {{ color: var(--start); }} .legend .e {{ color: var(--end); }}
</style>
<h1>{html.escape(title)}</h1>
<p class="sub">relative to {html.escape(body_name)}, projected onto the orbital plane &middot;
   produced by orbit-cli, drawn by tools/validation/plot_trajectory.py</p>
<div class="grid-2">
  <div class="card">
    <h2>trajectory</h2>
    {orbit_panel(track, body_radius, body_name)}
    <p class="legend"><b class="s">&#9679;</b> start &nbsp; <b class="e">&#9679;</b> end</p>
  </div>
  <div>
    <div class="card"><h2>radius [m] vs time [s]</h2>
      {series_panel(track.t, track.radius, "t", "radius [m]", "radius")}</div>
    <div class="card" style="margin-top:18px"><h2>specific energy drift (relative)</h2>
      {series_panel(track.t, energy_drift, "t", "dE/E", "energy")}</div>
    <div class="card" style="margin-top:18px"><h2>angular momentum drift (relative)</h2>
      {series_panel(track.t, momentum_drift, "t", "dh/h", "momentum")}</div>
    <div class="card" style="margin-top:18px"><h2>summary</h2>
      <table>{rows}</table></div>
  </div>
</div>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="CSV written by orbit-cli propagate --csv")
    parser.add_argument("-o", "--output", type=Path, help="output HTML (default: alongside the CSV)")
    parser.add_argument("--title", default=None)
    parser.add_argument("--body", default="Earth", help="central body name, for the scale marker")
    parser.add_argument("--body-radius-m", type=float, default=None,
                        help="override the central body radius drawn to scale")
    args = parser.parse_args()

    track = read_csv(args.csv)
    radius = args.body_radius_m
    if radius is None:
        radius = BODY_RADII.get(args.body.strip().lower(), 0.0)

    title = args.title or args.csv.stem
    output = args.output or args.csv.with_suffix(".html")
    output.write_text(build_page(track, title, args.body, radius))
    print(f"wrote {output}  ({len(track.t)} samples)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
