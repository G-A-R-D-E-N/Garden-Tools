#!/usr/bin/env bash
set -euo pipefail

base=55b10ce32470b69b3f07e803a2af41aff639c34c
git cat-file -e "${base}^{commit}"
changed="$(git diff --name-only "${base}..HEAD" | sort)"
expected="$(printf '%s\n' '.github/174b-quest-red.sh' '.github/workflows/174b-core-writers.yml' | sort)"
test "$changed" = "$expected"

rm -rf /tmp/ponytail-174b-quest-red
git clone --depth=1 https://github.com/DietrichGebert/ponytail.git /tmp/ponytail-174b-quest-red
test -f /tmp/ponytail-174b-quest-red/AGENTS.md

python3 - <<'PY'
from pathlib import Path

path = Path("PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp")
text = path.read_text()


def replace_in_case(name, old, new):
    global text
    marker = f'TEST_CASE("{name}")'
    start = text.index(marker)
    end = text.find('TEST_CASE("', start + len(marker))
    if end == -1:
        end = len(text)
    segment = text[start:end]
    if segment.count(old) != 1:
        raise SystemExit(f"{name}: expected one anchor, found {segment.count(old)}")
    segment = segment.replace(old, new, 1)
    text = text[:start] + segment + text[end:]


alias_name = "Native SetQuestAliases replaces reference location and collection aliases"
replace_in_case(
    alias_name,
    '''    writeBytes(path, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;''',
    '''    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;''')
replace_in_case(
    alias_name,
    '''    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Set 3 quest alias(es)") == 0);

    const auto disk = fo4::read_plugin_file(path);''',
    '''    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Set 3 quest alias(es)") == 0);
    CHECK(readBytes(path) == before);
    CHECK(result->get<std::string>().find("Written to disk") == std::string::npos);
    CHECK(result->get<std::string>().find("SavePlugin") != std::string::npos);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);''')

stage_name = "Native SetQuestStages writes multiple log entries with note and log text"
replace_in_case(
    stage_name,
    '''    writeBytes(path, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;''',
    '''    writeBytes(path, questPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;''')
replace_in_case(
    stage_name,
    '''    REQUIRE(result.has_value());

    const auto disk = fo4::read_plugin_file(path);''',
    '''    REQUIRE(result.has_value());
    CHECK(readBytes(path) == before);

    const auto aliasResult = backend.invokeBackend(
        "SetQuestAliases",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{
                {"kind", "reference"},
                {"id", 2},
                {"name", "AfterStage"},
                {"flags", 0x20}
            }})
        }));
    REQUIRE(aliasResult.has_value());
    CHECK(readBytes(path) == before);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);''')
replace_in_case(
    stage_name,
    '''    REQUIRE_FALSE(aliases.aliases.empty());''',
    '''    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].name == std::optional<std::string>{"AfterStage"});''')

master_name = "Native SetQuestAliases adds a loaded uniqueActor master and remaps the fill"
replace_in_case(
    master_name,
    '''    writeBytes(masterPath, npcMasterPlugin());
    writeBytes(patchPath, questPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;''',
    '''    writeBytes(masterPath, npcMasterPlugin());
    writeBytes(patchPath, questPlugin());
    const auto before = readBytes(patchPath);

    fo4ide::plugin::RadiclePluginBackend backend;''')
replace_in_case(
    master_name,
    '''    REQUIRE(result.has_value());

    const auto disk = fo4::read_plugin_file(patchPath);''',
    '''    REQUIRE(result.has_value());

    const auto pendingMastersRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(pendingMastersRaw.has_value());
    REQUIRE(pendingMastersRaw->is_string());
    const auto pendingMasters = nlohmann::json::parse(pendingMastersRaw->get<std::string>());
    REQUIRE(pendingMasters.at("masters").size() == 1u);
    CHECK(pendingMasters.at("masters")[0].at("name") == "Master.esm");
    CHECK(readBytes(patchPath) == before);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);''')

path.write_text(text)
PY

git diff --check
test "$(git diff --name-only | tr -d '\r')" = "PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp"

cmake -S PluginEditTool/native -B build/fo4ide-174b-quest-red -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DFO4IDE_BUILD_HTTP=OFF
cmake --build build/fo4ide-174b-quest-red --target fo4ide_test_quest_authoring
binary=build/fo4ide-174b-quest-red/bin/fo4ide_test_quest_authoring

prove_red() {
    local filter="$1"
    local log="$2"
    set +e
    "$binary" "$filter" > "$log" 2>&1
    local status=$?
    set -e
    cat "$log"
    test "$status" -ne 0
    grep -F 'readBytes' "$log"
    grep -F 'FAILED:' "$log"
}

prove_red "Native SetQuestAliases replaces reference location and collection aliases" /tmp/alias-red.txt
prove_red "Native SetQuestStages writes multiple log entries with note and log text" /tmp/stage-red.txt
prove_red "Native SetQuestAliases adds a loaded uniqueActor master and remaps the fill" /tmp/alias-master-red.txt

git rm .github/workflows/174b-core-writers.yml .github/174b-quest-red.sh
git config user.name "NomadsReach"
git config user.email "144523850+NomadsReach@users.noreply.github.com"
git add PluginEditTool/native/tests/unit/RadiclePluginQuestAuthoringTests.cpp
git commit -m "test: prove deferred quest persistence contract"
git push origin HEAD:work/174b-deferred-mutations
