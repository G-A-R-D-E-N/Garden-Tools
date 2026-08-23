#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Linux wrapper for headless Ghidra queries against the FO4 1.10.163 project
# (fwgh_proj_1.10.163_unpacked, Steamless-unpacked exe). Mirrors
# Scripts/run_query.ps1 (the Windows wrapper): same interface, Linux paths.
#
# Unlike the 1.10.155 wrapper (run_query_155.sh), there is NO owner rewrite: this
# project's project.prp already records OWNER="ciusr", so it opens directly. Only
# the '&'-in-<project_location> symlink + stale-lock cleanup are needed -- see
# 01-SETUP.md ("Opening this project on Linux").
#
# Usage:
#   ./run_query_163.sh "HUDCompass" "NavMesh::Activate"
#   ./run_query_163.sh "decompile:NavMesh::Activate"
#   ./run_query_163.sh -script FW_Stats.java
#   ./run_query_163.sh -script DecompileAt.java b7b680
#
# Options (all mirror run_query.ps1):
#   -script S     GhidraScript to run      (default: FW_Query.java)
#   -ghidra D     Ghidra install dir       (default: $RE_ROOT/Ghidra/ghidra_12.1_PUBLIC)
#   -projLoc D    project dir              (default: .../Projects/fwgh_proj_1.10.163_unpacked)
#   -projName N   program name             (default: Fallout4_163_unpacked)
#   -h|--help     this help
#
# Always runs -noanalysis: the DB is fully analyzed; never re-run auto-analysis.
# -----------------------------------------------------------------------------
set -uo pipefail

RE_ROOT="${RE_ROOT:-/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering}"
GHIDRA_HOME="${GHIDRA_HOME:-$RE_ROOT/Ghidra/ghidra_12.1_PUBLIC}"
PROJ_LOC="${PROJ_LOC:-$RE_ROOT/Projects/fwgh_proj_1.10.163_unpacked}"
PROJ_NAME="${PROJ_NAME:-Fallout4_163_unpacked}"
SCRIPTS_DIR="$RE_ROOT/Scripts"
SCRIPT="FW_Query.java"

usage() {
    echo "Usage: $0 [-script S] [-ghidra D] [-projLoc D] [-projName N] [term ...]"
    echo "  Linux wrapper for the 1.10.163_unpacked project (mirror of run_query.ps1)."
    echo "  Defaults: script=FW_Query.java, projLoc=.../fwgh_proj_1.10.163_unpacked, projName=Fallout4_163_unpacked."
    echo "  Terms: plain word = substring search, 'decompile:Name' = decompile, 'addr:0xRVA[,decompile]' = resolve."
    echo "  Use -script FW_Stats.java for the naming/health diagnostic."
}

terms=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -script)   SCRIPT="${2:?missing value for -script}";   shift 2 ;;
        -ghidra)   GHIDRA_HOME="${2:?missing value for -ghidra}"; shift 2 ;;
        -projLoc)  PROJ_LOC="${2:?missing value for -projLoc}";  shift 2 ;;
        -projName) PROJ_NAME="${2:?missing value for -projName}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        --)        shift; terms+=("$@"); break ;;
        -*)        echo "run_query_163.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)         terms+=("$1"); shift ;;
    esac
done

GH="$GHIDRA_HOME/support/analyzeHeadless"
PROJ_LINK="/tmp/fwgh_proj"
SCRIPTS_LINK="/tmp/fwgh_scripts"

[[ -x "$GH" ]] || { echo "run_query_163.sh: analyzeHeadless not found/executable: $GH" >&2; exit 1; }
[[ -f "$PROJ_LOC/$PROJ_NAME.gpr" ]] || { echo "run_query_163.sh: project not found: $PROJ_LOC/$PROJ_NAME.gpr" >&2; exit 1; }

# Drop the 163-specific symlink and stale lock on exit, even on failure/interrupt.
# (/tmp/fwgh_scripts is shared with the 1.10.155 workflow, so leave it in place.)
cleanup() {
    rm -f "$PROJ_LOC/$PROJ_NAME.lock" "$PROJ_LINK"
}
trap cleanup EXIT

# 1. Symlink project + scripts to clean paths (no '&' in <project_location>).
ln -sfn "$PROJ_LOC" "$PROJ_LINK"
ln -sfn "$SCRIPTS_DIR" "$SCRIPTS_LINK"

# 2. Drop a stale lock left by a killed/interrupted run.
rm -f "$PROJ_LINK/$PROJ_NAME.lock"

echo "run_query_163.sh: running $SCRIPT ${terms[*]} ..." >&2

# 3. Read-only headless query.
OUT="$(mktemp)" || { echo "run_query_163.sh: could not create temp file" >&2; exit 1; }
"$GH" "$PROJ_LINK" "$PROJ_NAME" -process -recursive -noanalysis \
    -scriptPath "$SCRIPTS_LINK" -postScript "$SCRIPT" "${terms[@]}" \
    > "$OUT" 2>&1
STATUS=$?

# 4. Print just the script's own output (same filtering as run_query.ps1).
tag="$(basename "$SCRIPT")"
if grep -F "$tag>" "$OUT" >/dev/null 2>&1; then
    tag_pat="$(printf '%s' "$tag" | sed 's/[.[\*^$+?]/\\&/g')"
    sed -n "s/.*${tag_pat}> //; s/ (GhidraScript).*//p" "$OUT"
else
    echo "run_query_163.sh: warning: no script output captured; tail of log:" >&2
    tail -n 15 "$OUT" >&2
fi
rm -f "$OUT"

exit "$STATUS"
