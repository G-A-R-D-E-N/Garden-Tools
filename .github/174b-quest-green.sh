#!/usr/bin/env bash
set -euo pipefail

base=14803594

git cat-file -e "${base}^{commit}"
changed="$(git diff --name-only "${base}..HEAD" | sort)"
expected="$(printf '%s\n' '.github/174b-quest-green.sh' '.github/workflows/174b-core-writers.yml' | sort)"
test "$changed" = "$expected"

rm -rf /tmp/ponytail-174b-backend-tests
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-backend-tests
test -f /tmp/ponytail-174b-backend-tests/AGENTS.md

python3 - <<'PY'
from pathlib import Path

specs = [
    ("PluginEditTool/native/tests/unit/RadiclePluginNestedFormLinkListTests.cpp", "PluginEditTool edits indexed nested RACE subgraph FormLink", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginNestedFormLinkListTests.cpp", "PluginEditTool inserts into empty nested RACE subgraph list", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginNestedFormLinkListTests.cpp", "PluginEditTool removes nested RACE subgraph FormLink", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginExteriorCellTests.cpp", "PluginEditTool creates a new exterior CELL in the WRLD hierarchy", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp", "PluginEditTool set_components accepts a COBJ as a recipe component", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp", "PluginEditTool set_components accepts broad item types on a COBJ", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp", "PluginEditTool set_components skips a non-CMPO component on a MISC", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp", "PluginEditTool set_components writes FVPA and no CDIX on a COBJ", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginRaceCreationTests.cpp", "PluginEditTool creates current-format RACE with required base skeleton", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginArmorCreationTests.cpp", "PluginEditTool creates ARMA biped and first-person gendered models", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginArmorCreationTests.cpp", "PluginEditTool fills missing ARMA biped and first-person models", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginArmorCreationTests.cpp", "PluginEditTool creates ARMO male and female model icon structures", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginArmorCreationTests.cpp", "PluginEditTool fills missing ARMO gendered fields after creation", "Patch.esp", "after", "    const auto key = created.at(\"formKey\").get<std::string>();"),
    ("PluginEditTool/native/tests/unit/RadiclePluginCreateTests.cpp", "Native CreatePlugin writes validates registers and immediately supports record creation", "FreshPatch.esp", "before", "    const auto after = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginCreateTests.cpp", "Native created-plugin rollback deletes only unchanged empty plugins", "NonEmptyRollback.esp", "before", "    const auto disk = fo4::read_plugin_file(nonEmptyPath);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginLvspTests.cpp", "Native SetField edits LVSP scalar settings transactionally", "Patch.esp", "before", "    const auto list = readList(patchPath);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginLvspTests.cpp", "Native SetField edits LVSP entry level count chance and spell", "Patch.esp", "before", "    const auto list = readList(patchPath);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginLvspGridTests.cpp", "LVSP spreadsheet column name round-trips through SetField", "Patch.esp", "before", "    const auto reloaded = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item appends a FormLink to an empty FLST", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item resolves an EditorID value", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item matches the field name case-insensitively", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool remove_list_item matches the field name case-insensitively", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item declares an external target's owner master", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(patchPath);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item appends a keyword into a packed KWDA array", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool add_list_item creates the KSIZ/KWDA block when a record has no keywords", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp", "PluginEditTool remove_list_item drops the last keyword and its KSIZ/KWDA block", "Patch.esp", "before", "    const auto disk = fo4::read_plugin_file(path);"),
    ("PluginEditTool/native/tests/unit/RadiclePluginEyesTests.cpp", "Native EYES SetField persists FULL ICON and DATA while preserving record flags", "EyesPatch.esp", "before", "    const auto reloaded = fo4::read_plugin_file(path);"),
]

multi_specs = [
    ("PluginEditTool/native/tests/unit/RadiclePluginMasterOverrideTests.cpp", "Master target copies top-level override and rebuilds ONAM", "Patch.esm", [
        ("before", "    auto disk = fo4::read_plugin_file(targetPath);"),
        ("before", "    disk = fo4::read_plugin_file(targetPath);")
    ])
]

save = lambda plugin: (
    '    REQUIRE(backend.invoke(\n'
    '        "backend.SavePlugin", nlohmann::json::array({"' + plugin + '"})).has_value());\n'
)

files = {}
for filename, name, plugin, mode, anchor in specs:
    files.setdefault(filename, Path(filename).read_text())
    text = files[filename]
    marker = f'TEST_CASE("{name}")'
    start = text.find(marker)
    if start == -1:
        raise SystemExit(f"missing test: {name}")
    next_test = text.find('TEST_CASE("', start + len(marker))
    end = len(text) if next_test == -1 else next_test
    block = text[start:end]
    count = block.count(anchor)
    if count != 1:
        raise SystemExit(f"anchor count {count} for {name}: {anchor}")
    insertion = save(plugin)
    if mode == "before":
        block = block.replace(anchor, insertion + anchor, 1)
    else:
        block = block.replace(anchor, anchor + "\n" + insertion.rstrip("\n"), 1)
    files[filename] = text[:start] + block + text[end:]

for filename, name, plugin, anchors in multi_specs:
    files.setdefault(filename, Path(filename).read_text())
    text = files[filename]
    marker = f'TEST_CASE("{name}")'
    start = text.find(marker)
    if start == -1:
        raise SystemExit(f"missing test: {name}")
    next_test = text.find('TEST_CASE("', start + len(marker))
    end = len(text) if next_test == -1 else next_test
    block = text[start:end]
    for mode, anchor in anchors:
        count = block.count(anchor)
        if count != 1:
            raise SystemExit(f"anchor count {count} for {name}: {anchor}")
        insertion = save(plugin)
        if mode == "before":
            block = block.replace(anchor, insertion + anchor, 1)
        else:
            block = block.replace(anchor, anchor + "\n" + insertion.rstrip("\n"), 1)
    files[filename] = text[:start] + block + text[end:]

for filename, text in files.items():
    Path(filename).write_text(text)
PY

git diff --check
expected_files="$(printf '%s\n' \
  PluginEditTool/native/tests/unit/RadiclePluginArmorCreationTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginComponentsTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginCreateTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginExteriorCellTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginEyesTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginListItemTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginLvspGridTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginLvspTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginMasterOverrideTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginNestedFormLinkListTests.cpp \
  PluginEditTool/native/tests/unit/RadiclePluginRaceCreationTests.cpp | sort)"
actual="$(git diff --name-only | sort)"
test "$actual" = "$expected_files"

cmake -S PluginEditTool/native -B build/fo4ide-174b-backend-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-backend-tests --target fo4ide_test_plugin_backend
binary=build/fo4ide-174b-backend-tests/bin/fo4ide_test_plugin_backend

filters=(
  "PluginEditTool edits indexed nested RACE subgraph FormLink"
  "PluginEditTool inserts into empty nested RACE subgraph list"
  "PluginEditTool removes nested RACE subgraph FormLink"
  "PluginEditTool creates a new exterior CELL in the WRLD hierarchy"
  "PluginEditTool set_components accepts a COBJ as a recipe component"
  "PluginEditTool set_components accepts broad item types on a COBJ"
  "PluginEditTool set_components skips a non-CMPO component on a MISC"
  "PluginEditTool set_components writes FVPA and no CDIX on a COBJ"
  "PluginEditTool creates current-format RACE with required base skeleton"
  "PluginEditTool creates ARMA biped and first-person gendered models"
  "PluginEditTool fills missing ARMA biped and first-person models"
  "PluginEditTool creates ARMO male and female model icon structures"
  "PluginEditTool fills missing ARMO gendered fields after creation"
  "Native CreatePlugin writes validates registers and immediately supports record creation"
  "Native created-plugin rollback deletes only unchanged empty plugins"
  "Native SetField edits LVSP scalar settings transactionally"
  "Native SetField edits LVSP entry level count chance and spell"
  "LVSP spreadsheet column name round-trips through SetField"
  "PluginEditTool add_list_item appends a FormLink to an empty FLST"
  "PluginEditTool add_list_item resolves an EditorID value"
  "PluginEditTool add_list_item matches the field name case-insensitively"
  "PluginEditTool remove_list_item matches the field name case-insensitively"
  "PluginEditTool add_list_item declares an external target's owner master"
  "PluginEditTool add_list_item appends a keyword into a packed KWDA array"
  "PluginEditTool add_list_item creates the KSIZ/KWDA block when a record has no keywords"
  "PluginEditTool remove_list_item drops the last keyword and its KSIZ/KWDA block"
  "Master target copies top-level override and rebuilds ONAM"
  "Native EYES SetField persists FULL ICON and DATA while preserving record flags"
)
for filter in "${filters[@]}"; do
  "$binary" "$filter"
done

git rm .github/workflows/174b-core-writers.yml .github/174b-quest-green.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit
git commit -m "test: align deferred backend persistence assertions"
git push origin HEAD:work/174b-deferred-mutations
