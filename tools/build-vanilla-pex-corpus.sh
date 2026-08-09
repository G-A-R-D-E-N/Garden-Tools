#!/usr/bin/env bash
#
# Builds the vanilla (.psc, .pex) corpus the Creation Kit differential compares against.
#
# The shipped compiled scripts are not loose on disk: they live inside "Fallout4 - Misc.ba2".
# This extracts them with the tool's own BA2 reader, repairs the paths, and stages the vanilla
# sources beside them in the layout PapyrusDifferentialTests pairs on:
#
#     <out>/scripts/X.pex   with   <out>/scripts/Source/X.psc
#
# Usage:
#   tools/build-vanilla-pex-corpus.sh <FalloutDataDir> <VanillaSourceDir> <OutDir>
#
# Then, and note the release flag, because the shipped binaries are release builds with DebugOnly
# and BetaOnly stripped. Without it every script that calls Debug.Trace reports a difference that is
# not a compiler defect, which measured about eleven points on this corpus:
#
#   FO4RE_PEX_RELEASE=1 \
#   FO4RE_PEX_CORPUS=<OutDir> \
#   FO4RE_PSC_ROOTS=<VanillaSourceDir> \
#     dotnet test FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj \
#       --filter "FullyQualifiedName~PapyrusDifferentialTests"
#
set -euo pipefail

DATA=${1:-}
SOURCES=${2:-}
OUT=${3:-}

if [[ -z $DATA || -z $SOURCES || -z $OUT ]]; then
    echo "usage: $0 <FalloutDataDir> <VanillaSourceDir> <OutDir>" >&2
    exit 2
fi

BA2="$DATA/Fallout4 - Misc.ba2"
[[ -f $BA2 ]] || { echo "no archive at $BA2" >&2; exit 1; }
[[ -d $SOURCES ]] || { echo "no source tree at $SOURCES" >&2; exit 1; }

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SERVER="$HERE/FO4RecordEditor.Server/bin/Release/net9.0/FO4RecordEditor.Server.dll"
[[ -f $SERVER ]] || {
    echo "build the server first: dotnet build FO4RecordEditor.Server/FO4RecordEditor.Server.csproj -c Release" >&2
    exit 1
}

rm -rf "$OUT"
mkdir -p "$OUT"

PORT=${FO4RE_CORPUS_PORT:-44987}
dotnet "$SERVER" --host 127.0.0.1 --port "$PORT" --headless >"$OUT/.server.log" 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT

for _ in $(seq 1 60); do
    curl -sf --max-time 2 "http://127.0.0.1:$PORT/api/health" >/dev/null && break
    sleep 1
done
curl -sf --max-time 2 "http://127.0.0.1:$PORT/api/health" >/dev/null || { echo "server did not start" >&2; exit 1; }

# "contains" rather than a glob: the entry paths are Windows style, and a glob does not cross the
# backslash, so "*.pex" matches nothing.
echo "extracting..."
jq -n --arg a "$BA2" --arg o "$OUT" \
    '{target:"archive",method:"ExtractAll",args:[$a,$o,".pex",20000,"contains"]}' \
    | curl -sf --max-time 900 -X POST "http://127.0.0.1:$PORT/rpc" \
        -H 'Content-Type: application/json' --data @- \
    | sed 's/\\"/"/g'

# The extractor writes the archive's Windows paths as literal file names on Linux, backslashes and
# all, so "scripts\Foo.pex" is one file rather than a directory and a file. Split them.
echo "repairing paths..."
python3 - "$OUT" <<'PY'
import os, shutil, sys
root = sys.argv[1]
moved = 0
for name in list(os.listdir(root)):
    if "\\" not in name:
        continue
    src = os.path.join(root, name)
    if not os.path.isfile(src):
        continue
    dst = os.path.join(root, *name.split("\\"))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.move(src, dst)
    moved += 1
print(f"  split {moved} paths")
PY

# The archive spells the folder both ways, and on a case-sensitive filesystem that is two trees.
if [[ -d "$OUT/Scripts" && -d "$OUT/scripts" ]]; then
    cp -rn "$OUT/Scripts/." "$OUT/scripts/"
    rm -rf "$OUT/Scripts"
elif [[ -d "$OUT/Scripts" ]]; then
    mv "$OUT/Scripts" "$OUT/scripts"
fi

echo "staging sources..."
cp -r "$SOURCES" "$OUT/scripts/Source"

echo
echo "corpus at $OUT"
echo "  .pex: $(find "$OUT/scripts" -name '*.pex' | wc -l)"
echo "  .psc: $(find "$OUT/scripts/Source" -name '*.psc' | wc -l)"
