#!/usr/bin/env bash
set -euo pipefail

base=b3ea7a45a99370ee487b752330a025004b69cf93
git cat-file -e "${base}^{commit}"
rm -rf /tmp/ponytail-174b-proj-align
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-proj-align
test -f /tmp/ponytail-174b-proj-align/AGENTS.md

python3 - <<'PY'
from pathlib import Path

path = Path("PluginEditTool/native/tests/unit/RadiclePluginProjExplHazdTests.cpp")
text = path.read_text()

def replace_once(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label} count {count}")
    text = text.replace(old, new, 1)

replace_once(
'''    return bytes;
}

}

TEST_CASE("Native CreateRecord writes a measured PROJ DNAM and remaps a typed EXPL slot") {
''',
'''    return bytes;
}

void savePlugin(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

}

TEST_CASE("Native CreateRecord writes a measured PROJ DNAM and remaps a typed EXPL slot") {
''',
"save helper")

replace_once(
'''    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PROJ", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

    const auto disk = fo4::read_plugin_file(path);
''',
'''    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PROJ", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
''',
"PROJ creation persistence")

replace_once(
'''    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EXPL", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

    const auto disk = fo4::read_plugin_file(path);
''',
'''    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EXPL", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
''',
"EXPL creation persistence")

replace_once(
'''    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "HAZD", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

    const auto disk = fo4::read_plugin_file(path);
''',
'''    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "HAZD", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
''',
"HAZD creation persistence")

replace_once(
'''    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMaster"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    REQUIRE(created.at("formKey") == "000811:Patch.esp");

    const auto disk = fo4::read_plugin_file(patchPath);
''',
'''    const auto beforeCreate = readBytes(patchPath);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMaster"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    REQUIRE(created.at("formKey") == "000811:Patch.esp");
    CHECK(readBytes(patchPath) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(patchPath);
''',
"master create first persistence")

replace_once(
'''    const auto second = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMasterAgain"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    CHECK(second.at("formKey") == "000812:Patch.esp");
    const auto again = fo4::read_plugin_file(patchPath);
''',
'''    const auto beforeSecondCreate = readBytes(patchPath);
    const auto second = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMasterAgain"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    CHECK(second.at("formKey") == "000812:Patch.esp");
    CHECK(readBytes(patchPath) == beforeSecondCreate);
    savePlugin(backend);
    const auto again = fo4::read_plugin_file(patchPath);
''',
"master create second persistence")

replace_once(
'''    REQUIRE(created.at("formKey") == key.to_string());

    const auto beforeBytes = readBytes(path);
    const auto invalidValues = std::array{
''',
'''    REQUIRE(created.at("formKey") == key.to_string());
    savePlugin(backend);

    const auto beforeBytes = readBytes(path);
    const auto invalidValues = std::array{
''',
"invalid FormLink setup persistence")

replace_once(
'''    for (const auto& write : writes) {
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", write.key, write.path, write.target
        })).has_value());
        const auto disk = fo4::read_plugin_file(path);
''',
'''    for (const auto& write : writes) {
        const auto beforeSave = readBytes(path);
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", write.key, write.path, write.target
        })).has_value());
        CHECK(readBytes(path) == beforeSave);
        savePlugin(backend);
        const auto disk = fo4::read_plugin_file(path);
''',
"audited FormLink persistence")

replace_once(
'''    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto before = fo4::read_plugin_file(path);
''',
'''    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    savePlugin(backend);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto before = fo4::read_plugin_file(path);
''',
"PROJ roundtrip setup persistence")

replace_once(
'''        const auto explosionKey = explosion.at("formKey").get<std::string>();
        REQUIRE(second.at("formKey") == "000802:Patch.esp");
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", explosionKey, "EXPL\\\\SpawnProjectile", "000802:Patch.esp"
        })).has_value());
        CHECK(fo4::forms::decode_explosion(
''',
'''        const auto explosionKey = explosion.at("formKey").get<std::string>();
        REQUIRE(second.at("formKey") == "000802:Patch.esp");
        savePlugin(backend);
        const auto beforeSet = readBytes(path);
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", explosionKey, "EXPL\\\\SpawnProjectile", "000802:Patch.esp"
        })).has_value());
        CHECK(readBytes(path) == beforeSet);
        savePlugin(backend);
        CHECK(fo4::forms::decode_explosion(
''',
"EXPL FormLink persistence")

replace_once(
'''    const auto hazardKey = hazard.at("formKey").get<std::string>();
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\\\Spell", "NULL"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\\\Spell", "000800:Patch.esp"
    })).has_value());
    CHECK(fo4::forms::decode_hazard(
''',
'''    const auto hazardKey = hazard.at("formKey").get<std::string>();
    savePlugin(backend);
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\\\Spell", "NULL"
    })).has_value());
    savePlugin(backend);
    const auto beforeRestore = readBytes(path);
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\\\Spell", "000800:Patch.esp"
    })).has_value());
    CHECK(readBytes(path) == beforeRestore);
    savePlugin(backend);
    CHECK(fo4::forms::decode_hazard(
''',
"HAZD FormLink persistence")

replace_once(
'''    REQUIRE(created.at("formKey") == "000811:Patch.esp");

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000811:Patch.esp", "PROJ\\\\Light", "000800:Master.esm"
    })).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
''',
'''    REQUIRE(created.at("formKey") == "000811:Patch.esp");
    savePlugin(backend);

    const auto beforeSet = readBytes(patchPath);
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000811:Patch.esp", "PROJ\\\\Light", "000800:Master.esm"
    })).has_value());
    CHECK(readBytes(patchPath) == beforeSet);
    savePlugin(backend);
    const auto disk = fo4::read_plugin_file(patchPath);
''',
"external master FormLink persistence")

path.write_text(text)
PY

git diff --check
actual="$(git diff --name-only | sort)"
expected="PluginEditTool/native/tests/unit/RadiclePluginProjExplHazdTests.cpp"
test "$actual" = "$expected"

cmake -S PluginEditTool/native -B build/fo4ide-174b-proj-align -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-proj-align --target fo4ide_test_proj_expl_hazd
build/fo4ide-174b-proj-align/bin/fo4ide_test_proj_expl_hazd

git rm .github/workflows/174b-proj-test-align.yml .github/174b-proj-test-align.sh
final="$(git diff --name-only | sort)"
test "$final" = "$expected"
git diff --check

git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginProjExplHazdTests.cpp
git commit -m "test: align deferred proj expl hazd persistence"
git push origin HEAD:work/174b-deferred-mutations
