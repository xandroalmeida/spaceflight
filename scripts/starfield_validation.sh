#!/usr/bin/env bash
# The starfield validation harness (docs/validation/starfield-debug.md): ten
# stages, each measuring the renderer's own frame against core/. The artefacts
# land in docs/validation/starfield.
#
#   scripts/starfield_validation.sh [--seed-references]
#
# Seeding the references is a deliberate act, never a side effect of a run: a
# suite that quietly adopts whatever it just rendered as the thing to compare
# against cannot fail. Seed them when the physics has been checked by the other
# nine stages, and say so in the commit.
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/gpu_validation.sh" starfield "$@"
