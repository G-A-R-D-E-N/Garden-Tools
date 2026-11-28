#!/usr/bin/env bash
set -euo pipefail
shopt -s nullglob

DATA=${1:-}
SOURCES=${2:-}
OUT=${3:-}

if [[ -z $DATA || -z $SOURCES || -z $OUT ]]; then
    echo "usage: RADICLE_PARITY_DUMP=<radicle_parity_dump> $0 <FalloutDataDir> <VanillaSourceDir> <OutDir>" >&2
    exit 2
fi

BA2="$DATA/Fallout4 - Misc.ba2"
[[ -f $BA2 ]] || { echo "no archive at $BA2" >&2; exit 1; }
[[ -d $SOURCES ]] || { echo "no source tree at $SOURCES" >&2; exit 1; }

DUMP=${RADICLE_PARITY_DUMP:-}
[[ -n $DUMP && -x $DUMP ]] || {
    echo "set RADICLE_PARITY_DUMP to a built radicle_parity_dump: <build>/radicle_parity_dump after cmake -S Radicle -B <build>, or <build>/bin/radicle_parity_dump after cmake -S PluginEditTool/native -B <build> -DRADICLE_BUILD_TOOLS=ON" >&2
    exit 1
}

merge_dirs() {
    local src=$1 dst=$2 entry name
    for entry in "$src"/*; do
        name=${entry##*/}
        if [[ -d $entry && -d $dst/$name ]]; then
            merge_dirs "$entry" "$dst/$name"
        elif [[ -e $dst/$name ]]; then
            cmp -s "$entry" "$dst/$name" || { echo "conflicting entry $dst/$name vs $entry" >&2; exit 1; }
            rm "$entry"
        else
            mv "$entry" "$dst/$name"
        fi
    done
    rmdir "$src"
}

fold_dirs() {
    local dir=$1 entry name key
    local -A seen=()
    for entry in "$dir"/*/; do
        entry=${entry%/}
        name=${entry##*/}
        if [[ $name == "${name,,}" ]]; then seen[${name,,}]=$entry; fi
    done
    for entry in "$dir"/*/; do
        entry=${entry%/}
        name=${entry##*/}
        key=${name,,}
        if [[ $name == "$key" ]]; then continue; fi
        if [[ -n ${seen[$key]:-} ]]; then
            merge_dirs "$entry" "${seen[$key]}"
        else
            seen[$key]=$entry
        fi
    done
    for key in "${!seen[@]}"; do
        fold_dirs "${seen[$key]}"
    done
}

rm -rf "$OUT"
mkdir -p "$OUT"

echo "extracting..."
"$DUMP" ba2pex "$BA2" "$OUT"

for entry in "$OUT"/*/; do
    entry=${entry%/}
    name=${entry##*/}
    if [[ ${name,,} != scripts || $name == scripts || $entry -ef $OUT/scripts ]]; then continue; fi
    if [[ -d $OUT/scripts ]]; then merge_dirs "$entry" "$OUT/scripts"; else mv "$entry" "$OUT/scripts"; fi
done
[[ -d $OUT/scripts ]] || { echo "archive did not stage $OUT/scripts" >&2; exit 1; }
fold_dirs "$OUT/scripts"

trees=0
for entry in "$OUT"/*/; do
    entry=${entry%/}
    name=${entry##*/}
    if [[ ${name,,} == scripts ]]; then trees=$((trees + 1)); fi
done
(( trees == 1 )) || { echo "expected one scripts tree under $OUT, found $trees" >&2; exit 1; }

echo "staging sources..."
cp -r "$SOURCES" "$OUT/scripts/Source"

echo
echo "corpus at $OUT"
echo "  .pex: $(find "$OUT/scripts" -name '*.pex' | wc -l)"
echo "  .psc: $(find "$OUT/scripts/Source" -name '*.psc' | wc -l)"
