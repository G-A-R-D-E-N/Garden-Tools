#!/usr/bin/env bash
set -euo pipefail

base=19948098

git cat-file -e "${base}^{commit}"
changed="$(git diff --name-only "${base}..HEAD" | sort)"
expected="$(printf '%s\n' '.github/174b-quest-green.sh' '.github/workflows/174b-core-writers.yml' | sort)"
test "$changed" = "$expected"

rm -rf /tmp/ponytail-174b-quest-red-more
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-quest-red-more
test -f /tmp/ponytail-174b-quest-red-more/AGENTS.md

python3 - <<'PY'
from pathlib import Path
import re

path = Path("PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp")
text = path.read_text()

tests = [
    "Native SetQuestStages preserves opaque INDX bytes",
    "Native SetQuestAliases preserves stages and tail when writing conditions",
    "Native SetQuestAliases writes keywords items packages spells factions and linked aliases",
    "Native SetQuestAliases writes multi-entry collections and preserves tail",
    "Native SetQuestStages writes per-log-entry CTDA and CIS strings",
    "Native SetQuestAliases binds mixed item COED to the preceding CNTO",
    "Native SetQuestStages replaces a lossless existing CTDA run",
    "Native SetQuestStages accepts alias-index GetDistance and RunOn Reference FormKeys",
    "Native SetQuestStages remaps VMAD Stage by unique body not array position",
    "Native SetQuestStages ties CIS strings to String/Event parameter slots",
]

for name in tests:
    marker = f'TEST_CASE("{name}")'
    start = text.find(marker)
    if start == -1:
        raise SystemExit(f"missing test: {name}")
    next_test = text.find('TEST_CASE("', start + len(marker))
    end = len(text) if next_test == -1 else next_test
    block = text[start:end]
    path_var = "patchPath" if "const auto patchPath =" in block else "path"
    selected = None
    for match in re.finditer(r'    const auto ([A-Za-z_][A-Za-z0-9_]*) = backend\.invokeBackend\(\n', block):
        result_var = match.group(1)
        require_marker = f'    REQUIRE({result_var}.has_value());\n'
        require_pos = block.find(require_marker, match.end())
        if require_pos != -1:
            selected = (match, result_var, require_marker, require_pos)
            break
    if selected is None:
        raise SystemExit(f"missing successful backend invocation in {name}")
    match, result_var, require_marker, require_pos = selected
    invoke_pos = match.start()
    before = f'    const auto beforeSave = readBytes({path_var});\n'
    block = block[:invoke_pos] + before + block[invoke_pos:]
    require_pos += len(before)
    after_pos = require_pos + len(require_marker)
    save = (
        f'    CHECK(readBytes({path_var}) == beforeSave);\n'
        '    REQUIRE(backend.invoke(\n'
        '        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());\n'
    )
    block = block[:after_pos] + save + block[after_pos:]
    text = text[:start] + block + text[end:]

path.write_text(text)
PY

git diff --check
test "$(git diff --name-only | tr -d '\r')" = "PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp"

cmake -S PluginEditTool/native -B build/fo4ide-174b-quest-red-more -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-quest-red-more --target fo4ide_test_quest_authoring
binary=build/fo4ide-174b-quest-red-more/bin/fo4ide_test_quest_authoring

prove_red() {
    local filter="$1"
    local slug="$2"
    set +e
    "$binary" "$filter" > "/tmp/${slug}.txt" 2>&1
    local status=$?
    set -e
    cat "/tmp/${slug}.txt"
    test "$status" -ne 0
    grep -F 'beforeSave' "/tmp/${slug}.txt"
    grep -F 'FAILED:' "/tmp/${slug}.txt"
}

prove_red "Native SetQuestStages preserves opaque INDX bytes" q1
prove_red "Native SetQuestAliases preserves stages and tail when writing conditions" q2
prove_red "Native SetQuestAliases writes keywords items packages spells factions and linked aliases" q3
prove_red "Native SetQuestAliases writes multi-entry collections and preserves tail" q4
prove_red "Native SetQuestStages writes per-log-entry CTDA and CIS strings" q5
prove_red "Native SetQuestAliases binds mixed item COED to the preceding CNTO" q6
prove_red "Native SetQuestStages replaces a lossless existing CTDA run" q7
prove_red "Native SetQuestStages accepts alias-index GetDistance and RunOn Reference FormKeys" q8
prove_red "Native SetQuestStages remaps VMAD Stage by unique body not array position" q9
prove_red "Native SetQuestStages ties CIS strings to String/Event parameter slots" q10

git rm .github/workflows/174b-core-writers.yml .github/174b-quest-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp
git commit -m "test: prove remaining deferred quest contracts"
git push origin HEAD:work/174b-deferred-mutations
