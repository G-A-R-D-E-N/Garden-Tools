#!/usr/bin/env bash
# Dev-only oracle harness. Out of tree by design: nothing here belongs in the BGS repo.
#
# It converts stock Fallout 4 animation .hkx files to the 4-byte pointer layout using the reference
# content tools under Wine, then grounds the BGS pointer-layout walker against that output with
# `symrm ground`. Every pointer fixup the reference writes must land on a walker-predicted offset.
#
# Key discovery (2026-08-15): the standalone filter manager does NOT need a full install or .NET.
# Extract the tools with `7z x <installer> -o<dir> "HavokContentTools/*"`, install only the VC++ 2010
# runtime (`winetricks -q vcrun2010`), and point the exe at its own dll via HAVOK_TOOLS_ROOT_X64.
# Behaviour files still crash the x64 filter manager; only animation files pass this way.
set -u

PREFIX="${HAVOK_WINE_PREFIX:-$HOME/.wine-havok}"
TOOLS="${HAVOK_TOOLS_DIR:-$HOME/.havok-oracle/HavokContentTools}"
WORK="${HAVOK_WORK:-$HOME/.havok-oracle/work}"
SYMRM="${SYMRM_PROJECT:-/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/BehaviourGraphStudio/tools/symrm}"
HKO="$WORK/to32.hko"

export WINEPREFIX="$PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="winedbg.exe=d"
export HAVOK_TOOLS_ROOT_X64="Z:${TOOLS//\//\\}"      # unix path -> wine Z: drive, backslashed

mkdir -p "$WORK"

if [ ! -f "$HKO" ]; then
  cat > "$HKO" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<hkoptions>
	<hkobject class="hctConfigurationSetData">
		<hkparam name="filterManagerVersion">65536</hkparam>
		<hkparam name="activeConfiguration">0</hkparam>
	</hkobject>
	<hkobject class="hctConfigurationData">
		<hkparam name="configurationName">To32</hkparam>
		<hkparam name="numFilters">1</hkparam>
	</hkobject>
	<hkobject name="Write to Platform" class="hctFilterData">
		<hkparam name="id">2876798309</hkparam>
		<hkparam name="ver">65792</hkparam>
		<hkparam name="hasOptions">true</hkparam>
	</hkobject>
	<hkobject name="Write to Platform" class="hctPlatformWriterOptions">
		<hkparam name="filename">out32.hkx</hkparam>
		<hkparam name="tagfile">false</hkparam>
		<hkparam name="preset">MSVC_WIN32</hkparam>
		<hkparam name="bytesInPointer">4</hkparam>
		<hkparam name="littleEndian">1</hkparam>
		<hkparam name="reusePaddingOptimized">0</hkparam>
		<hkparam name="emptyBaseClassOptimized">1</hkparam>
		<hkparam name="removeMetadata">false</hkparam>
		<hkparam name="userTag">0</hkparam>
		<hkparam name="saveEnvironmentData">false</hkparam>
	</hkobject>
</hkoptions>
EOF
fi

fails=0
for IN in "$@"; do
  rm -f "$WORK/out32.hkx"
  ( cd "$WORK" && timeout 120 xvfb-run -a wine "$TOOLS/hctStandAloneFilterManager.exe" -s "$HKO" "$IN" ) >/dev/null 2>&1
  if [ -f "$WORK/out32.hkx" ]; then
    out=$(dotnet run --project "$SYMRM" --no-build -- ground "$WORK/out32.hkx" 2>&1 \
          | grep -E 'reference layout|did not predict')
    echo "$(basename "$IN"):"
    echo "$out" | sed 's/^/  /'
    echo "$out" | grep -q 'did not predict: 0' || fails=$((fails+1))
  else
    echo "$(basename "$IN"): CONVERSION FAILED (behaviour files crash the x64 filter manager)"
    fails=$((fails+1))
  fi
done

echo "----"
echo "$fails file(s) failed grounding"
exit $(( fails == 0 ? 0 : 1 ))
