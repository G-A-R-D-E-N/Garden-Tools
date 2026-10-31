#!/usr/bin/env bash
set -euo pipefail

paths=(
  BGS/tools/tests/fixtures/vanilla/Meshes
  BGS/tools/symrm/samples
  Radicle/testdata/FW_HeatherSurvivalPatch.esp
  Radicle/testdata/SlowTime.esp
)

[ -n "${TOKEN:-}" ] || { echo "::error::GT_PUBLIC_CI_TOKEN is not set"; exit 1; }
source=$(git log -1 --format=%B | sed -n 's/^Source-Commit: \([0-9a-f]\{40\}\)$/\1/p')
[ -n "$source" ] || { echo "::error::no Source-Commit trailer"; exit 1; }

clone="$RUNNER_TEMP/private-fixtures"
git init -q "$clone"
git -C "$clone" remote add origin "https://x-access-token:${TOKEN}@github.com/G-A-R-D-E-N/GardenTools.git"
git -C "$clone" sparse-checkout set --no-cone "${paths[@]}"
git -C "$clone" fetch -q --depth 1 --filter=blob:none origin "$source"
git -C "$clone" checkout -q FETCH_HEAD
tar -C "$clone" -cf - "${paths[@]}" | tar -xf -
rm -rf "$clone"
echo "restored ${#paths[@]} private fixture paths from ${source:0:8}"
