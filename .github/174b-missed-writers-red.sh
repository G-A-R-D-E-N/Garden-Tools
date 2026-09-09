#!/usr/bin/env bash
set -euo pipefail

base=3a096d6

git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-missed-red
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-missed-red
test -f /tmp/ponytail-174b-missed-red/AGENTS.md

python3 - <<'PY'
from pathlib import Path

# Masters reorder
path = Path("PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp")
text = path.read_text()
name = 'TEST_CASE("Masters reorder writes immediately and preserves logical FormLinks")'
start = text.index(name)
end = text.index('TEST_CASE("', start + len(name))
block = text[start:end]
block = block.replace(
    '    const auto reorder = textResult(\n',
    '    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(patch));\n\n'
    '    const auto reorder = textResult(\n', 1)
anchor = '    const auto reloaded = fo4::read_plugin_file(patch);'
block = block.replace(
    anchor,
    '    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patch)) == beforeSave);\n'
    '    CHECK(reorder.find("SavePlugin") != std::string::npos);\n'
    '    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());\n'
    + anchor, 1)
text = text[:start] + block + text[end:]
path.write_text(text)

# Reference remap
path = Path("PluginEditTool/native/tests/unit/RadiclePluginReferenceRemapTests.cpp")
text = path.read_text()
name = 'TEST_CASE("Native ChangeReferencingRecords copies winners adds target master and remaps links")'
start = text.index(name)
end = text.index('TEST_CASE("', start + len(name))
block = text[start:end]
block = block.replace(
    '    const auto applied = backend.changeReferencingRecords(\n',
    '    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(patchPath));\n\n'
    '    const auto applied = backend.changeReferencingRecords(\n', 1)
anchor = '    const auto patch = fo4::read_plugin_file(patchPath);'
block = block.replace(
    anchor,
    '    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patchPath)) == beforeSave);\n'
    '    CHECK(applied->get<std::string>().find("SavePlugin") != std::string::npos);\n'
    '    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());\n\n'
    + anchor, 1)
text = text[:start] + block + text[end:]
path.write_text(text)

# Renumber
path = Path("PluginEditTool/native/tests/unit/RadiclePluginRenumberTests.cpp")
text = path.read_text()
name = 'TEST_CASE("Native RenumberPluginFormIds vacates overlapping targets before rekeying dependents")'
start = text.index(name)
block = text[start:]
block = block.replace(
    '    const auto applied = backend.renumberPluginFormIds(\n',
    '    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(path));\n\n'
    '    const auto applied = backend.renumberPluginFormIds(\n', 1)
anchor = '    const auto disk = fo4::read_plugin_file(path);'
block = block.replace(
    anchor,
    '    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == beforeSave);\n'
    '    CHECK(applied->get<std::string>().find("SavePlugin") != std::string::npos);\n'
    '    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());\n\n'
    + anchor, 1)
text = text[:start] + block
path.write_text(text)
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="$(printf '%s\n' \
  PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginReferenceRemapTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginRenumberTests.cpp | sort)"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-missed-red -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-missed-red --target fo4ide_test_plugin_backend
binary=build/fo4ide-174b-missed-red/bin/fo4ide_test_plugin_backend

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

prove_red "Masters reorder writes immediately and preserves logical FormLinks" masters
prove_red "Native ChangeReferencingRecords copies winners adds target master and remaps links" remap
prove_red "Native RenumberPluginFormIds vacates overlapping targets before rekeying dependents" renumber

git rm .github/workflows/174b-core-writers.yml .github/174b-missed-writers-red.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginMastersTests.cpp \
        PluginEditTool/native/tests/unit/RadiclePluginReferenceRemapTests.cpp \
        PluginEditTool/native/tests/unit/RadiclePluginRenumberTests.cpp
git commit -m "test: prove remaining deferred writer contracts"
git push origin HEAD:work/174b-deferred-mutations
