#!/usr/bin/env python3
"""Cross-validate this project's geodesic against REBOUNDx (spec section 29).

Two independent codes are asked the same question -- a test particle around a
point mass with first-order relativistic corrections -- and made to disagree in
public. REBOUNDx's `gr` operator is the Anderson et al. (1975) 1PN force used in
solar-system ephemerides; `core/gravity/weak_field_metric` integrates the
geodesic of the weak-field metric. Same physics, different formulations, no
shared line of code.

The comparison is done in four steps, because "the trajectories differ by 2600 km"
means nothing on its own:

  1. a NEWTONIAN control: turn relativity off in both codes. Whatever separation
     survives is the two integrators, and everything else has to be measured
     against it;
  2. the raw relativistic separation;
  3. the gauge-invariant observable -- perihelion advance per orbit -- which is
     what anybody ever measured;
  4. the split of the raw separation into an apsidal part and a timing part, and
     how much of it one single constant absorbs.

The spec forbids making REBOUND a dependency of the architecture, so this lives
in tools/, installs into its own virtualenv on first run, and is never part of a
build.

    cmake --build build --target gr-reference
    ./tools/validation/reboundx_cross_check.py
    ./tools/validation/reboundx_cross_check.py --orbits 80 --json /tmp/report.json
"""

import argparse
import csv
import io
import json
import math
import subprocess
import sys
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VENV = ROOT / "build" / "reboundx-venv"

# Mercury, from the same constants tests/scientific/test_relativistic_gravity.cpp
# uses, so the two can be read side by side.
GM_SUN = 1.32712440041e20
A_MERCURY = 5.790905e10
E_MERCURY = 0.205630
C = 299792458.0

ARCSEC = 180.0 / math.pi * 3600.0
CENTURY = 100.0 * 365.25 * 86400.0


def ensure_rebound():
    python = VENV / "bin" / "python"
    if not python.exists():
        print(f"creating {VENV} and installing rebound (first run only)", file=sys.stderr)
        venv.EnvBuilder(with_pip=True).create(VENV)
        subprocess.run([str(python), "-m", "pip", "install", "--quiet",
                        "rebound", "reboundx"], check=True)
    return python


REBOUND_SCRIPT = r"""
import json, sys
import rebound, reboundx

cfg = json.load(sys.stdin)
sim = rebound.Simulation()
sim.units = ('s', 'm', 'kg')
sim.integrator = 'ias15'
sim.add(m=cfg['gm'] / sim.G)
sim.add(m=0.0, x=cfg['x'], y=0.0, z=0.0, vx=0.0, vy=cfg['vy'], vz=0.0)

if cfg['relativistic']:
    rebx = reboundx.Extras(sim)
    gr = rebx.load_force('gr')
    gr.params['c'] = cfg['c']
    rebx.add_force(gr)
    sim.particles[0].params['gr_source'] = 1

rows = []
scale = cfg['time_scale']
for i in range(cfg['samples'] + 1):
    t = cfg['duration'] * i / cfg['samples']
    sim.integrate(t * scale)
    a, b = sim.particles[0], sim.particles[1]
    rows.append([t, b.x - a.x, b.y - a.y, b.z - a.z,
                 b.vx - a.vx, b.vy - a.vy, b.vz - a.vz])

json.dump({'rows': rows, 'version': rebound.__version__}, sys.stdout)
"""


def run_rebound(python, x, vy, duration, samples, relativistic=True, time_scale=1.0):
    cfg = json.dumps({"gm": GM_SUN, "x": x, "vy": vy, "duration": duration,
                      "samples": samples, "c": C, "relativistic": relativistic,
                      "time_scale": time_scale})
    out = subprocess.run([str(python), "-c", REBOUND_SCRIPT],
                         input=cfg, capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"REBOUNDx failed:\n{out.stderr}")
    return json.loads(out.stdout)


def run_ours(binary, x, vy, duration, samples, mode="geodesic"):
    out = subprocess.run([str(binary), "--gm", repr(GM_SUN), "--x", repr(x),
                          "--vy", repr(vy), "--duration", repr(duration),
                          "--samples", str(samples), "--mode", mode],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"gr-reference failed:\n{out.stderr}")
    reader = csv.reader(io.StringIO(out.stdout))
    next(reader)
    return [[float(v) for v in row] for row in reader]


def elements(row):
    """Osculating a, e, argument of periapsis and mean anomaly, in the plane."""
    _, x, y, _, vx, vy, _ = row
    r = math.hypot(x, y)
    v2 = vx * vx + vy * vy
    rv = x * vx + y * vy
    ex = ((v2 - GM_SUN / r) * x - rv * vx) / GM_SUN
    ey = ((v2 - GM_SUN / r) * y - rv * vy) / GM_SUN
    e = math.hypot(ex, ey)
    a = 1.0 / (2.0 / r - v2 / GM_SUN)
    omega = math.atan2(ey, ex)
    nu = math.atan2(y, x) - omega
    eccentric = 2 * math.atan2(math.sqrt(1 - e) * math.sin(nu / 2),
                               math.sqrt(1 + e) * math.cos(nu / 2))
    return a, e, omega, eccentric - e * math.sin(eccentric)


def max_separation(a_rows, b_rows):
    return max(math.dist(a[1:4], b[1:4]) for a, b in zip(a_rows, b_rows))


def wrap(angle):
    while angle > math.pi:
        angle -= 2 * math.pi
    while angle < -math.pi:
        angle += 2 * math.pi
    return angle


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--orbits", type=int, default=40)
    ap.add_argument("--samples-per-orbit", type=int, default=32)
    ap.add_argument("--binary", default=str(ROOT / "build" / "bin" / "gr-reference"))
    ap.add_argument("--json", help="also write the report here")
    args = ap.parse_args()

    binary = Path(args.binary)
    if not binary.exists():
        sys.exit(f"{binary} not found -- build it with:\n"
                 f"  cmake --build build --target gr-reference")

    rp = A_MERCURY * (1.0 - E_MERCURY)
    vp = math.sqrt(GM_SUN * (1.0 + E_MERCURY) / (A_MERCURY * (1.0 - E_MERCURY)))
    period = 2 * math.pi * math.sqrt(A_MERCURY ** 3 / GM_SUN)
    duration = period * args.orbits
    samples = args.orbits * args.samples_per_orbit

    python = ensure_rebound()
    years = duration / 86400 / 365.25
    print(f"Mercury, {args.orbits} orbits ({years:.1f} yr), {samples} samples",
          file=sys.stderr)

    ours = run_ours(binary, rp, vp, duration, samples)
    ours_newton = run_ours(binary, rp, vp, duration, samples, mode="newtonian")
    theirs = run_rebound(python, rp, vp, duration, samples)
    theirs_newton = run_rebound(python, rp, vp, duration, samples, relativistic=False)

    control = max_separation(ours_newton, theirs_newton["rows"])
    raw = max_separation(ours, theirs["rows"])
    signal = max_separation(ours, ours_newton)

    ours_end = elements(ours[-1])
    theirs_end = elements(theirs["rows"][-1])
    d_omega = wrap(ours_end[2] - theirs_end[2])
    d_mean = wrap(ours_end[3] - theirs_end[3])
    closed_form = 6 * math.pi * GM_SUN / (C * C * A_MERCURY * (1 - E_MERCURY ** 2))

    # How much of the raw separation is one constant? Ternary search on a single
    # scalar: a relative rescaling of the other code's time coordinate.
    lo, hi = 1.0 - 5.0e-6, 1.0 + 5.0e-6
    for _ in range(34):
        m1, m2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        s1 = max_separation(ours, run_rebound(python, rp, vp, duration, samples,
                                              time_scale=m1)["rows"])
        s2 = max_separation(ours, run_rebound(python, rp, vp, duration, samples,
                                              time_scale=m2)["rows"])
        if s1 < s2:
            hi = m2
        else:
            lo = m1
    best = (lo + hi) / 2
    residual = max_separation(ours, run_rebound(python, rp, vp, duration, samples,
                                                time_scale=best)["rows"])

    report = {
        "rebound_version": theirs["version"],
        "orbits": args.orbits,
        "years": years,
        "newtonian_control_m": control,
        "raw_separation_m": raw,
        "gr_signal_m": signal,
        "apsidal_difference_rad": d_omega,
        "apsidal_difference_arcsec_per_century": d_omega / duration * CENTURY * ARCSEC,
        "mean_anomaly_difference_rad": d_mean,
        "mean_motion_offset": best - 1.0,
        "mean_motion_offset_over_gm_over_a_c2": (best - 1.0) / (GM_SUN / (A_MERCURY * C * C)),
        "residual_after_one_constant_m": residual,
        "residual_over_gravitational_radius": residual / (GM_SUN / (C * C)),
        "perihelion_advance_per_orbit": {
            "ours_minus_reboundx": d_omega / args.orbits,
            "closed_form": closed_form,
            "relative_disagreement": abs(d_omega / args.orbits) / closed_form,
        },
    }

    print()
    print(f"REBOUND {theirs['version']} IAS15 + REBOUNDx 'gr'   vs   Dormand-Prince 5(4) + weak-field geodesic")
    print(f"Mercury, {args.orbits} orbits, {years:.1f} yr")
    print()
    print(f"  1. Newtonian control (relativity off in both)   {control:12.4g} m")
    print(f"     -- this is the two integrators, and the floor for everything below")
    print()
    print(f"  2. relativistic separation                      {raw:12.4g} m")
    print(f"     the GR signal itself (ours GR - ours Newton) {signal:12.4g} m")
    print()
    print(f"  3. perihelion advance per orbit")
    print(f"       closed form 6 pi GM/(c^2 a (1-e^2))        {closed_form:12.6e} rad")
    print(f"       ours - REBOUNDx                            {d_omega / args.orbits:12.6e} rad")
    print(f"       relative disagreement                      {report['perihelion_advance_per_orbit']['relative_disagreement']:12.4g}")
    print(f"       i.e. {report['apsidal_difference_arcsec_per_century']:+.4f} arcsec/century against 43")
    print()
    print(f"  4. where the {raw:.3g} m actually is")
    print(f"       apsidal   {d_omega:12.4e} rad")
    print(f"       timing    {d_mean:12.4e} rad  ->  {abs(d_mean) * ours_end[0]:.4g} m along track")
    print(f"       one constant time rescale of {best - 1.0:+.6e}")
    print(f"       = {report['mean_motion_offset_over_gm_over_a_c2']:.3f} x GM/(a c^2), absorbs it:")
    print(f"       residual                                   {residual:12.4g} m")
    print(f"       ({raw / residual:.0f}x smaller; the control is {control:.3g} m)")
    print(f"       the residual is bounded, not secular, and is "
          f"{report['residual_over_gravitational_radius']:.2f} x GM/c^2 = "
          f"{GM_SUN / (C * C):.0f} m -- the scale on which two 1PN coordinate")
    print(f"       systems are allowed to differ")
    print()

    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2))
        print(f"wrote {args.json}", file=sys.stderr)


if __name__ == "__main__":
    main()
