#!/usr/bin/env bash
set -euo pipefail

base=33eef92

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-wording-red
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-wording-red
test -f /tmp/ponytail-174b-wording-red/AGENTS.md

python3 - <<'PY'
from pathlib import Path

# Components message contract
path = Path("PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp")
text = path.read_text()
old = '''    CHECK(result->get<std::string>() ==
          "Set 1 component(s) on 000A00:Patch.esp in Patch.esp. Written to disk.");
'''
new = '''    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);
'''
if text.count(old) != 1:
    raise SystemExit("component wording anchor mismatch")
path.write_text(text.replace(old, new, 1))

# Conditions message contract
path = Path("PluginEditTool/native/tests/unit/ConditionBackendTests.cpp")
text = path.read_text()
old = '    CHECK(message->get<std::string>().find("Set 2 condition(s)") == 0);\n'
new = old + '    CHECK(message->get<std::string>().find("Written to disk") == std::string::npos);\n' + \
      '    CHECK(message->get<std::string>().find("SavePlugin") != std::string::npos);\n'
if text.count(old) != 1:
    raise SystemExit("conditions wording anchor mismatch")
path.write_text(text.replace(old, new, 1))

# List removal message contract
path = Path("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp")
text = path.read_text()
old = '''    CHECK(result->get<std::string>() ==
          "Removed 1 entr(y/ies) matching 000800:Patch.esp from ITEMS (now 0) on "
          "000811:Patch.esp. Written to disk.");
'''
new = '''    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);
'''
if text.count(old) != 1:
    raise SystemExit("list wording anchor mismatch")
path.write_text(text.replace(old, new, 1))
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="$(printf '%s\n' \
  PluginEditTool/native/tests/unit/ConditionBackendTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp | sort)"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-wording-red -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-wording-red --target fo4ide_test_plugin_backend
binary=build/fo4ide-174b-wording-red/bin/fo4ide_test_plugin_backend

prove_red() {
  local filter="$1"
  local slug="$2"
  set +e
  "$binary" "$filter" > "/tmp/${slug}.txt" 2>&1
  local status=$?
  set -e
  cat "/tmp/${slug}.txt"
  test "$status" -ne 0
  grep -F 'SavePlugin' "/tmp/${slug}.txt"
  grep -F 'FAILED:' "/tmp/${slug}.txt"
}

prove_red "PluginEditTool set_components writes FVPA and no CDIX on a COBJ" components
prove_red "condition backend: SetConditions replaces the list and round-trips" conditions
prove_red "PluginEditTool remove_list_item matches the field name case-insensitively" list

git rm .github/workflows/174b-core-writers.yml .github/174b-wording-red.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/ConditionBackendTests.cpp \
        PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp \
        PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp
git commit -m "test: prove deferred persistence wording"
git push origin HEAD:work/174b-deferred-mutations
