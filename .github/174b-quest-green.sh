#!/usr/bin/env bash
set -euo pipefail

base=a658e759

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-quest-tests
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-quest-tests
test -f /tmp/ponytail-174b-quest-tests/AGENTS.md

python3 - <<'PY'
from pathlib import Path
import re

path = Path("PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp")
text = path.read_text()

tests = [
    "Native logEntry sugar ORs complete and fail into QSDT flags",
    "Native SetQuestAliases rewrites VMAD alias IDs by unique name",
    "Native SetQuestStages rewrites VMAD Stage and leaves alias extras",
    "Native SetQuestStages adds a loaded condition FormLink master",
    "Native SetQuestStages accepts alias-index GetDistance and RunOn Reference FormKeys",
    "Native SetQuestStages ties CIS strings to String/Event parameter slots",
    "Native SetQuestAliases packs linked aliases into one ALLA array",
    "Native SetQuestAliases writes CTDA/CIS before extras",
    "Native SetQuestAliases writes conditions on location aliases",
    "Native SetQuestAliases writes zero, multiple, scalar, and CIS2 conditions",
    "Native SetQuestAliases encodes FormKey compareGlobal and RunOn Reference",
    "Native SetQuestAliases writes remaining reference fill types",
    "Native SetQuestAliases writes remaining location fill types",
    "Native SetQuestAliases writes and reconstructs Create Reference Level",
    "Native SetQuestAliases replaces existing Create Reference without ALCL",
    "Native SetQuestAliases writes remaining members in Mutagen order",
    "Native SetQuestAliases rewrites existing multi-entry collections",
    "Native SetQuestAliases does not allocate reference IDs from collection entries",
]

save = '''    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
'''

for name in tests:
    marker = f'TEST_CASE("{name}")'
    start = text.find(marker)
    if start == -1:
        raise SystemExit(f"missing test: {name}")
    next_test = text.find('TEST_CASE("', start + len(marker))
    end = len(text) if next_test == -1 else next_test
    block = text[start:end]
    matches = list(re.finditer(r'    const auto ([A-Za-z_][A-Za-z0-9_]*) = backend\.invokeBackend\(\n', block))
    offset = 0
    inserted = 0
    for match in matches:
        var = match.group(1)
        assign_start = match.start() + offset
        require = f'    REQUIRE({var}.has_value());\n'
        require_pos = block.find(require, assign_start)
        if require_pos == -1:
            continue
        after = require_pos + len(require)
        following = block[after:after + 500]
        if 'CHECK(readBytes(' in following and '== beforeSave' in following:
            continue
        if following.startswith(save):
            continue
        block = block[:after] + save + block[after:]
        offset += len(save)
        inserted += 1
    if inserted == 0 and 'beforeSave' not in block:
        raise SystemExit(f"no SavePlugin insertion for {name}")
    text = text[:start] + block + text[end:]

path.write_text(text)
PY

git diff --check
test "$(git diff --name-only | tr -d '\r')" = "PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp"

cmake -S PluginEditTool/native -B build/fo4ide-174b-quest-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-quest-tests --target fo4ide_test_quest_authoring
quest=build/fo4ide-174b-quest-tests/bin/fo4ide_test_quest_authoring

"$quest" "Native logEntry sugar ORs complete and fail into QSDT flags"
"$quest" "Native SetQuestAliases rewrites VMAD alias IDs by unique name"
"$quest" "Native SetQuestStages rewrites VMAD Stage and leaves alias extras"
"$quest" "Native SetQuestAliases packs linked aliases into one ALLA array"
"$quest" "Native SetQuestAliases writes remaining reference fill types"
"$quest" "Native SetQuestAliases writes remaining location fill types"

set +e
"$quest" "Native SetQuestAliases replaces reference location and collection aliases" > /tmp/quest-contract-red.txt 2>&1
status=$?
set -e
cat /tmp/quest-contract-red.txt
test "$status" -ne 0
grep -F 'before' /tmp/quest-contract-red.txt
grep -F 'FAILED:' /tmp/quest-contract-red.txt

git rm .github/workflows/174b-core-writers.yml .github/174b-quest-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp
git commit -m "test: align quest persistence assertions"
git push origin HEAD:work/174b-deferred-mutations
