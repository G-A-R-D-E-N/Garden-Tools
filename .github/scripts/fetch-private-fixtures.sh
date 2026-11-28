#!/usr/bin/env bash
set -euo pipefail

paths=(
  Radicle/tests/fixtures/hkx
  BGS/tools/symrm/samples
  Radicle/testdata/FW_HeatherSurvivalPatch.esp
  Radicle/testdata/SlowTime.esp
  Radicle/testdata/previs/oracles
  Radicle/docs/FO4_PARITY.md
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
present=()
for path in "${paths[@]}"; do
  if [ -e "$clone/$path" ]; then present+=("$path"); else echo "::notice::$path is not in ${source:0:8}; skipped"; fi
done
[ ${#present[@]} -gt 0 ] || { echo "::error::no private fixture path exists in ${source:0:8}"; exit 1; }
tar -C "$clone" -cf - "${present[@]}" | tar -xf -
rm -rf "$clone"
echo "restored ${#present[@]} of ${#paths[@]} private fixture paths from ${source:0:8}"
