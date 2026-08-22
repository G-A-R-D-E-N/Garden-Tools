#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Linux wrapper for headless Ghidra queries against the named FO4 1.10.155
# project. Mirrors Scripts/run_query_155.ps1 (the Windows wrapper): same
# interface, Linux paths, plus the symlink + owner-rewrite + restore dance that
# Symbols/GHIDRA_PROJECT.md ("Opening this project on Linux") documents.
#
# Why the dance:
#   1. analyzeHeadless rejects '&' in the <project_location> argument, so the
#      project (and scripts) are symlinked to /tmp paths without '&' first.
#   2. The project was created on Windows as user "Game", so project.prp's OWNER
#      is temporarily rewritten to the current user, then restored on exit via a
#      trap (runs on normal exit and on INT/TERM; only SIGKILL skips it).
#
# Usage:
#   ./run_query_155.sh "PlayerCharacter::Update" "TESObjectREFR"
#   ./run_query_155.sh "decompile:PlayerCharacter::Update"
#   ./run_query_155.sh -script Stats.java
#   ./run_query_155.sh -script DumpEnum.java BUTTON_INDEX
#
# Options (all mirror run_query_155.ps1):
#   -script S     GhidraScript to run      (default: Query.java)
#   -ghidra D     Ghidra install dir       (default: $RE_ROOT/Ghidra/ghidra_12.1_PUBLIC)
#   -projLoc D    project dir              (default: .../Projects/fwgh_proj_1.10.155)
#   -projName N   program name             (default: Fallout4_155)
#   -h|--help     this help
#
# Always runs -noanalysis: the DB is fully analyzed; never re-run auto-analysis.
# -----------------------------------------------------------------------------
set -uo pipefail

RE_ROOT="${RE_ROOT:-/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering}"
GHIDRA_HOME="${GHIDRA_HOME:-$RE_ROOT/Ghidra/ghidra_12.1_PUBLIC}"
PROJ_LOC="${PROJ_LOC:-$RE_ROOT/Projects/fwgh_proj_1.10.155}"
PROJ_NAME="${PROJ_NAME:-Fallout4_155}"
SCRIPTS_DIR="$RE_ROOT/Scripts"
SCRIPT="Query.java"

usage() {
    echo "Usage: $0 [-script S] [-ghidra D] [-projLoc D] [-projName N] [term ...]"
    echo "  Linux wrapper for the 1.10.155 named project project (mirror of run_query_155.ps1)."
    echo "  Defaults: script=Query.java, projLoc=.../fwgh_proj_1.10.155, projName=Fallout4_155."
    echo "  Terms: plain word = substring search, 'decompile:Name' = decompile, 'addr:0xRVA[,decompile]' = resolve."
    echo "  Use -script Stats.java for the naming/health diagnostic."
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
        -*)        echo "run_query_155.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)         terms+=("$1"); shift ;;
    esac
done

GH="$GHIDRA_HOME/support/analyzeHeadless"
PROJ_LINK="/tmp/fwgh155_proj"
SCRIPTS_LINK="/tmp/fwgh_scripts"

[[ -x "$GH" ]] || { echo "run_query_155.sh: analyzeHeadless not found/executable: $GH" >&2; exit 1; }
[[ -f "$PROJ_LOC/$PROJ_NAME.gpr" ]] || { echo "run_query_155.sh: project not found: $PROJ_LOC/$PROJ_NAME.gpr" >&2; exit 1; }

PRP="$PROJ_LINK/$PROJ_NAME.rep/project.prp"
PRP_BAK="$(mktemp)" || { echo "run_query_155.sh: could not create temp file" >&2; exit 1; }

# Always restore the owner and drop the 155-specific symlink/lock, even on failure or
# interrupt. (/tmp/fwgh_scripts is shared with the 1.10.163 workflow, so leave it in place.)
cleanup() {
    [[ -f "$PRP_BAK" && -f "$PRP" ]] && cp -p "$PRP_BAK" "$PRP" 2>/dev/null
    rm -f "$PRP_BAK" "$PROJ_LOC/$PROJ_NAME.lock" "$PROJ_LINK"
}
trap cleanup EXIT

# 1. Symlink project + scripts to clean paths (no '&' in <project_location>).
ln -sfn "$PROJ_LOC" "$PROJ_LINK"
ln -sfn "$SCRIPTS_DIR" "$SCRIPTS_LINK"

# 2. Drop a stale lock left by a killed/interrupted run.
rm -f "$PROJ_LINK/$PROJ_NAME.lock"

# 3. Rewrite OWNER Game -> current user (backup kept for restore in cleanup).
cp -p "$PRP" "$PRP_BAK"
sed -i "s/VALUE=\"Game\"/VALUE=\"$(id -un)\"/" "$PRP"

echo "run_query_155.sh: running $SCRIPT ${terms[*]} ..." >&2

# 4. Read-only headless query.
OUT="$(mktemp)" || { echo "run_query_155.sh: could not create temp file" >&2; exit 1; }
"$GH" "$PROJ_LINK" "$PROJ_NAME" -process -recursive -noanalysis \
    -scriptPath "$SCRIPTS_LINK" -postScript "$SCRIPT" "${terms[@]}" \
    > "$OUT" 2>&1
STATUS=$?

# 5. Print just the script's own output (same filtering as run_query_155.ps1).
tag="$(basename "$SCRIPT")"
if grep -F "$tag>" "$OUT" >/dev/null 2>&1; then
    tag_pat="$(printf '%s' "$tag" | sed 's/[.[\*^$+?]/\\&/g')"
    sed -n "s/.*${tag_pat}> //; s/ (GhidraScript).*//p" "$OUT"
else
    echo "run_query_155.sh: warning: no script output captured; tail of log:" >&2
    tail -n 15 "$OUT" >&2
fi
rm -f "$OUT"

exit "$STATUS"
