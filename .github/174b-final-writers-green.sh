#!/usr/bin/env bash
set -euo pipefail

base=4f2bae7

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-wording-align
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-wording-align
test -f /tmp/ponytail-174b-wording-align/AGENTS.md

python3 - <<'PY'
from pathlib import Path

replacements = {
    "PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp": [
        (
            '"Set 2 component(s) on 000900:Patch.esp in Patch.esp. Written to disk."',
            '"Set 2 component(s) on 000900:Patch.esp in Patch.esp. SavePlugin to persist."'
        ),
        (
            '"Skipped 1: 000B00:Patch.esp: WEAP is not a CMPO component. Written to disk."',
            '"Skipped 1: 000B00:Patch.esp: WEAP is not a CMPO component. SavePlugin to persist."'
        ),
        (
            '"Set 0 component(s) on 000900:Patch.esp in Patch.esp. Written to disk."',
            '"Set 0 component(s) on 000900:Patch.esp in Patch.esp. SavePlugin to persist."'
        ),
        (
            '"Set 2 component(s) on 000A00:Patch.esp in Patch.esp. Written to disk."',
            '"Set 2 component(s) on 000A00:Patch.esp in Patch.esp. SavePlugin to persist."'
        ),
        (
            '"Skipped 1: 000C00:Patch.esp: QUST is not a valid recipe item. Written to disk."',
            '"Skipped 1: 000C00:Patch.esp: QUST is not a valid recipe item. SavePlugin to persist."'
        ),
        (
            '"000802:Patch.esp: count must be 1..4294967295. Written to disk."',
            '"000802:Patch.esp: count must be 1..4294967295. SavePlugin to persist."'
        ),
        (
            '"Set 1 component(s) on 000A00:Patch.esp in Patch.esp. Written to disk."',
            '"Set 1 component(s) on 000A00:Patch.esp in Patch.esp. SavePlugin to persist."'
        ),
    ],
    "PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp": [
        (
            '"000811:Patch.esp. Written to disk."',
            '"000811:Patch.esp. SavePlugin to persist."'
        ),
        (
            '"000900:Patch.esp. Written to disk."',
            '"000900:Patch.esp. SavePlugin to persist."'
        ),
    ],
    "PluginEditTool/native/tests/unit/ConditionBackendTests.cpp": [
        (
            'CHECK(text.find("Written to disk.") != std::string::npos);',
            'CHECK(text.find("SavePlugin to persist.") != std::string::npos);'
        ),
    ],
}

for filename, pairs in replacements.items():
    path = Path(filename)
    text = path.read_text()
    for old, new in pairs:
        if text.count(old) != 1:
            raise SystemExit(f"{filename}: expected one occurrence of {old!r}, got {text.count(old)}")
        text = text.replace(old, new, 1)
    path.write_text(text)
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="$(printf '%s\n' \
  PluginEditTool/native/tests/unit/ConditionBackendTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp | sort)"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-wording-align -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-wording-align --target fo4ide_test_plugin_backend
binary=build/fo4ide-174b-wording-align/bin/fo4ide_test_plugin_backend

filters=(
  "PluginEditTool set_components writes CVPA and identity CDIX on a MISC"
  "condition backend: SetConditionsAt replaces one effect scope and round-trips"
  "PluginEditTool set_components skips negative and oversized counts"
  "PluginEditTool set_components accepts broad item types on a COBJ"
  "PluginEditTool set_components skips a non-CMPO component on a MISC"
  "PluginEditTool set_components accepts a COBJ as a recipe component"
  "PluginEditTool remove_list_item drops the last keyword and its KSIZ/KWDA block"
  "PluginEditTool remove_list_item drops the matching FormLink"
  "PluginEditTool set_components skips a non-item component on a COBJ"
  "PluginEditTool set_components clears CVPA and CDIX on an empty list"
)
for filter in "${filters[@]}"; do
  slug=$(printf '%s' "$filter" | tr -cs 'A-Za-z0-9' '_' | cut -c1-80)
  set +e
  "$binary" "$filter" > "/tmp/${slug}.txt" 2>&1
  status=$?
  set -e
  cat "/tmp/${slug}.txt"
  test "$status" -ne 0
  grep -F 'SavePlugin' "/tmp/${slug}.txt"
  grep -F 'FAILED:' "/tmp/${slug}.txt"
done

git rm .github/workflows/174b-core-writers.yml .github/174b-final-writers-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add \
  PluginEditTool/native/tests/unit/ConditionBackendTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp
git commit -m "test: align deferred persistence messages"
git push origin HEAD:work/174b-deferred-mutations
