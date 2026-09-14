#!/usr/bin/env python3
"""Retired.  The campaign is now tools/lunar-campaign.

This script drove `orbit-cli intercept` and read its PROSE with regular
expressions:

    if "NOT captured:" in text:
        return "lunar insertion remained hyperbolic"

which made the campaign's verdict depend on the wording of a print statement,
and limited what could be recorded to what the CLI happened to print.  Milestone
6.1 asks for forty instrumented quantities per epoch and a classification on
every failure; neither survives a screen scrape.

    cmake --build build -j
    ./build/bin/lunar-campaign tests/scenarios/lunar-intercept.json --epochs 100 \
        --csv docs/validation/lunar-navigation-campaign-v2.csv

    scripts/lunar_campaign.sh 365 8      # the same, across processes

See docs/validation/lunar-navigation-hardening.md.
"""

import sys

print(__doc__, file=sys.stderr)
sys.exit(2)
