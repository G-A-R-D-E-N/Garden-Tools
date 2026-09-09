#!/usr/bin/env bash
set -euo pipefail

base=4d8b8d10d089baaaf0a0fb09c9c6ceaaa27ff351

git cat-file -e "${base}^{commit}"
changed="$(git diff --name-only "${base}..HEAD" | sort)"
expected="$(printf '%s\n' '.github/174b-info-red.sh' '.github/workflows/174b-core-writers.yml' | sort)"
test "$changed" = "$expected"

rm -rf /tmp/ponytail-174b-info
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-info
test -f /tmp/ponytail-174b-info/AGENTS.md

python3 - <<'PY'
from pathlib import Path

path = Path("PluginEditTool/native/tests/unit/RadiclePluginMaintenanceTests.cpp")
text = path.read_text()
old = '''    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* duplicate = fo4::find_record(
        patch, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->header.signature == fo4::Signature::from_string("INFO"));
    CHECK(editorId(*duplicate) == "BackendInfoDUP");
}'''
new = '''    const auto pendingRaw = backend.invokeBackend(
        "GetRecordDetails", nlohmann::json::array({"000900:Patch.esp"}));
    REQUIRE(pendingRaw.has_value());
    REQUIRE(pendingRaw->is_string());
    const auto pending = nlohmann::json::parse(pendingRaw->get<std::string>());
    CHECK(pending.at("FormKey") == "000900:Patch.esp");
    CHECK(pending.at("EditorId") == "BackendInfoDUP");
    CHECK(pending.at("Signature") == "INFO");
    CHECK(pending.at("File") == "Patch.esp");

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000900:Patch.esp")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* duplicate = fo4::find_record(
        patch, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->header.signature == fo4::Signature::from_string("INFO"));
    CHECK(editorId(*duplicate) == "BackendInfoDUP");
}'''
if text.count(old) != 1:
    raise SystemExit(f"expected one legacy INFO disk assertion, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
PY

git diff --check
uncommitted="$(git diff --name-only | tr -d '\r')"
test "$uncommitted" = "PluginEditTool/native/tests/unit/RadiclePluginMaintenanceTests.cpp"

cmake -S PluginEditTool/native -B build/fo4ide-174b-info -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-info --target fo4ide_test_plugin_backend

set +e
build/fo4ide-174b-info/bin/fo4ide_test_plugin_backend "Native CopyAsNewRecord dispatches a contained INFO to the dialogue primitive" > /tmp/info-red.txt 2>&1
status=$?
set -e
cat /tmp/info-red.txt
test "$status" -ne 0
grep -F 'beforeSave' /tmp/info-red.txt
grep -F 'FAILED:' /tmp/info-red.txt

git rm .github/workflows/174b-core-writers.yml .github/174b-info-red.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginMaintenanceTests.cpp
git commit -m "test: prove deferred INFO copy contract"
git push origin HEAD:work/174b-deferred-mutations
