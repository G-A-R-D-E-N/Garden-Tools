#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_objectives_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<std::byte> emptyPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    if (result->is_string()) return nlohmann::json::parse(result->get<std::string>());
    return *result;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "QuestObjectives"}, {"resolved", true}}})}})
                .has_value());
}

std::string createQuestWithAlias(fo4ide::plugin::RadiclePluginBackend& backend) {
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    const auto key = created.at("formKey").get<std::string>();
    const nlohmann::json aliases = nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}}});
    const auto aliased = backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", key, aliases}));
    if (!aliased.has_value()) INFO(aliased.error().type + ": " + aliased.error().message);
    REQUIRE(aliased.has_value());
    return key;
}

void writeU32Subrecord(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    return bytes;
}

void writeTexts(const std::filesystem::path& root) {
    const auto strings = root / "Strings";
    std::filesystem::create_directories(strings);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{84, rawBytes("Present")}}}, strings / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {{42, rawBytes("Log")}}}, strings / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, {}}, strings / "Patch_en.ILSTRINGS");
}

nlohmann::json localizedQuestContext(const std::filesystem::path& root) {
    return nlohmann::json{{"language", "en"}, {"stringsDirectory", (root / "Strings").string()}};
}

std::vector<std::byte> localizedObjectiveQuest(std::uint32_t logTextId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::localized, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TextQuest\0", 10});
    fo4::test::ByteWriter stageIndex;
    stageIndex.u16(10);
    stageIndex.u8(0);
    stageIndex.u8(0);
    fo4::test::write_subrecord(out, "INDX", stageIndex.bytes);
    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
    writeU32Subrecord(out, "CNAM", logTextId);
    const auto addObjective = [&out](std::uint16_t objectiveIndex, std::uint32_t textId) {
        fo4::test::ByteWriter objective;
        objective.u16(objectiveIndex);
        fo4::test::write_subrecord(out, "QOBJ", objective.bytes);
        writeU32Subrecord(out, "FNAM", 0);
        writeU32Subrecord(out, "NNAM", textId);
    };
    addObjective(20, 0);
    addObjective(21, 999);
    addObjective(22, 84);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json objectivesWithGetStage(const std::string& questKey) {
    return nlohmann::json::array({{
        {"index", 10},
        {"flags", 0},
        {"displayText", "Find the courier"},
        {"targets", nlohmann::json::array({{
            {"alias", "Player"},
            {"flags", 2},
            {"conditions", nlohmann::json::array({{
                {"function", "GetStage"},
                {"operator", "=="},
                {"param1", questKey},
                {"value", 10}
            }})}
        }})}
    }});
}

}  // namespace

// Detects: SetQuestObjectives must store each target as an alias ID with its flags and a CTDA condition, and GetQuestObjectives must read the same alias name and flags back after save.
TEST_CASE("Native SetQuestObjectives stores alias targets with conditions and reads them back after save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto questKey = createQuestWithAlias(backend);

    REQUIRE(backend.invokeBackend(
        "SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey, objectivesWithGetStage(questKey)})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto read = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(read.at("objectives").size() == 1u);
    const auto& objective = read.at("objectives")[0];
    CHECK(objective.at("index") == 10);
    CHECK(objective.at("displayText") == "Find the courier");
    REQUIRE(objective.at("targets").size() == 1u);
    CHECK(objective.at("targets")[0].at("alias") == 1);
    CHECK(objective.at("targets")[0].at("aliasName") == "Player");
    CHECK(objective.at("targets")[0].at("flags") == 2);

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_objectives(disk, fo4::FormKey::parse(questKey));
    REQUIRE(decoded.objectives.size() == 1u);
    REQUIRE(decoded.objectives[0].targets.size() == 1u);
    const auto& target = decoded.objectives[0].targets[0];
    CHECK(target.alias == 1);
    CHECK(target.flags == 2u);
    bool sawCtda = false;
    for (const auto& member : target.subrecords) {
        if (member.signature.to_string() != "CTDA") continue;
        sawCtda = true;
        const auto condition = fo4::parse_ctda_condition(member.data);
        CHECK(condition.function == 58);
    }
    CHECK(sawCtda);
}

// Detects: SetQuestObjectives must refuse a target that names a form (a reference) rather than an alias, and must leave the plugin unchanged.
TEST_CASE("Native SetQuestObjectives refuses a reference target without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto questKey = createQuestWithAlias(backend);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = readBytes(path);

    const nlohmann::json objectives = nlohmann::json::array({{
        {"index", 0},
        {"flags", 0},
        {"displayText", "Go"},
        {"targets", nlohmann::json::array({{{"reference", "000800:Patch.esp"}}})}
    }});
    const auto result = backend.invokeBackend(
        "SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey, objectives}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestObjectivePayload");

    const auto byAlias = backend.invokeBackend(
        "SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey, nlohmann::json::array({{
            {"index", 0}, {"flags", 0}, {"displayText", "Go"},
            {"targets", nlohmann::json::array({{{"alias", 7}}})}}})}));
    REQUIRE_FALSE(byAlias.has_value());
    CHECK(byAlias.error().type == "InvalidQuestObjectivePayload");
    CHECK(readBytes(path) == before);
}

// Detects: reordering targets must preserve each alias's opaque members and omitted conditions across save and reload.
TEST_CASE("Native objective target reorder keeps metadata with its alias") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto key = createQuestWithAlias(backend);
    REQUIRE(backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}},
                               {{"kind", "reference"}, {"id", 2}, {"name", "Other"}}})})).has_value());
    auto payload = objectivesWithGetStage(key);
    auto& targets = payload[0]["targets"];
    auto second = targets[0];
    second["alias"] = "Other";
    second["conditions"][0]["value"] = 20;
    targets.push_back(second);
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    auto disk = fo4::read_plugin_file(path);
    auto objectives = fo4::forms::decode_quest_objectives(disk, fo4::FormKey::parse(key));
    objectives.objectives[0].targets[0].subrecords.push_back({fo4::Signature::from_string("ZZZZ"), {std::byte{1}}});
    objectives.objectives[0].targets[1].subrecords.push_back({fo4::Signature::from_string("ZZZZ"), {std::byte{2}}});
    fo4::forms::write_quest_objectives(disk, fo4::FormKey::parse(key), objectives);
    fo4::write_plugin_file(disk, path);
    fo4ide::plugin::RadiclePluginBackend reload;
    loadPatch(reload, path);
    targets = nlohmann::json::array({{{"alias", 2}}, {{"alias", 2}}});
    const auto ambiguous = reload.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload}));
    REQUIRE_FALSE(ambiguous.has_value());
    CHECK(ambiguous.error().type == "InvalidQuestObjectivePayload");
    targets = nlohmann::json::array({{{"alias", 2}}, {{"alias", 1}, {"conditions", nlohmann::json::array()}}});
    REQUIRE(reload.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload})).has_value());
    REQUIRE(reload.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto after = fo4::forms::decode_quest_objectives(saved, fo4::FormKey::parse(key));
    const auto& first = after.objectives[0].targets[0];
    const auto& last = after.objectives[0].targets[1];
    CHECK(first.alias == 2);
    CHECK(last.alias == 1);
    const auto opaque = [](const auto& target) {
        for (const auto& sub : target.subrecords) if (sub.signature.to_string() == "ZZZZ") return sub.data;
        return std::vector<std::byte>{};
    };
    CHECK(opaque(first) == std::vector<std::byte>{std::byte{2}});
    CHECK(opaque(last) == std::vector<std::byte>{std::byte{1}});
    CHECK(std::count_if(first.subrecords.begin(), first.subrecords.end(), [](const auto& sub) {
        return sub.signature.to_string() == "CTDA";
    }) == 1);
    for (const auto& sub : first.subrecords) {
        if (sub.signature.to_string() == "CTDA") CHECK(fo4::parse_ctda_condition(sub.data).comparison_float() == 20.0f);
    }
    CHECK(std::none_of(last.subrecords.begin(), last.subrecords.end(), [](const auto& sub) {
        return sub.signature.to_string() == "CTDA";
    }));
}

// Detects: adding a condition's master must remap preserved target conditions to the new local FormID index.
TEST_CASE("Native objective update rebases preserved conditions after adding a master") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto masterPath = root / "Master.esm";
    writeBytes(path, emptyPlugin());
    fo4::test::ByteWriter master;
    const auto tes4 = fo4::test::begin_record(master, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr; hedr.f32(1.0f); hedr.u32(2); hedr.u32(0x801);
    fo4::test::write_subrecord(master, "HEDR", hedr.bytes);
    fo4::test::end_record(master, tes4);
    const auto group = fo4::test::begin_group(master, "QUST", 0);
    const auto quest = fo4::test::begin_record(master, "QUST", 0, 0x800);
    fo4::test::write_subrecord(master, "EDID", std::string_view{"External\0", 9});
    fo4::test::end_record(master, quest); fo4::test::end_group(master, group);
    writeBytes(masterPath, master.bytes);
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", {{"plugins", nlohmann::json::array({
        {{"name", "Master.esm"}, {"path", masterPath.string()}, {"source", "test"}, {"resolved", true}},
        {{"name", "Patch.esp"}, {"path", path.string()}, {"source", "test"}, {"resolved", true}}})}}).has_value());
    const auto key = createQuestWithAlias(backend);
    REQUIRE(backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", key,
        nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}},
                               {{"kind", "reference"}, {"id", 2}, {"name", "Other"}}})})).has_value());
    auto payload = objectivesWithGetStage(key);
    payload[0]["targets"].push_back({{"alias", 2}});
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload})).has_value());
    auto replacement = payload;
    replacement[0]["targets"][0].erase("conditions");
    replacement[0]["targets"][1]["conditions"] = nlohmann::json::array({{
        {"function", "GetStage"}, {"operator", "=="}, {"param1", "000800:Master.esm"}, {"value", 5}}});
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, replacement})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_objectives(saved, fo4::FormKey::parse(key));
    REQUIRE(decoded.objectives.size() == 1u);
    REQUIRE(decoded.objectives[0].targets.size() == 2u);
    const auto& preserved = decoded.objectives[0].targets[0].subrecords;
    const auto ctda = std::find_if(preserved.begin(), preserved.end(), [](const auto& sub) {
        return sub.signature.to_string() == "CTDA";
    });
    REQUIRE(ctda != preserved.end());
    CHECK(static_cast<std::uint32_t>(fo4::parse_ctda_condition(ctda->data).param1) == 0x01000800u);
    const auto& added = decoded.objectives[0].targets[1].subrecords;
    const auto external = std::find_if(added.begin(), added.end(), [](const auto& sub) {
        return sub.signature.to_string() == "CTDA";
    });
    REQUIRE(external != added.end());
    CHECK(static_cast<std::uint32_t>(fo4::parse_ctda_condition(external->data).param1) == 0x00000800u);
}

// Detects: objective text containing a NUL must be rejected rather than silently truncated after saving.
TEST_CASE("Native objective text refuses embedded NUL") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto key = createQuestWithAlias(backend);
    auto payload = objectivesWithGetStage(key);
    payload[0]["displayText"] = std::string("Before\0After", 12);
    const auto result = backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestObjectivePayload");
}

// Detects: a bulk SetQuestObjectives with two objectives sharing one index must be refused without writing, so index-based edits stay unambiguous.
TEST_CASE("Native SetQuestObjectives refuses duplicate objective indexes without writing") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto key = createQuestWithAlias(backend);
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, objectivesWithGetStage(key)})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = readBytes(path);
    auto payload = objectivesWithGetStage(key);
    payload[0]["index"] = 3;
    payload.push_back(payload[0]);
    const auto result = backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", key, payload}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidQuestObjectivePayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native localized quest text ID zero reads as empty text") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, localizedObjectiveQuest(0));
    writeTexts(root);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto context = localizedQuestContext(root);
    const auto stages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    REQUIRE(stages.at("stages").size() == 1u);
    CHECK(stages["stages"][0]["logEntries"][0]["logText"] == "");
    CHECK(stages["stages"][0]["logEntries"][0]["logTextId"] == 0);
    CHECK_FALSE(stages["stages"][0]["logEntries"][0].contains("textMissing"));
    const auto objectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    REQUIRE(objectives.at("objectives").size() == 3u);
    CHECK(objectives["objectives"][0]["displayText"] == "");
    CHECK(objectives["objectives"][0]["displayTextId"] == 0);
    CHECK_FALSE(objectives["objectives"][0].contains("textMissing"));
    CHECK(objectives["objectives"][2]["displayText"] == "Present");
}

TEST_CASE("Native localized quest text ID missing from the table reads with textMissing") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, localizedObjectiveQuest(999));
    writeTexts(root);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto context = localizedQuestContext(root);
    const auto stages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    REQUIRE(stages.at("stages").size() == 1u);
    CHECK(stages["stages"][0]["logEntries"][0]["logText"] == "");
    CHECK(stages["stages"][0]["logEntries"][0]["logTextId"] == 999);
    CHECK(stages["stages"][0]["logEntries"][0]["textMissing"] == true);
    const auto objectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", context})));
    REQUIRE(objectives.at("objectives").size() == 3u);
    CHECK(objectives["objectives"][0]["displayText"] == "");
    CHECK_FALSE(objectives["objectives"][0].contains("textMissing"));
    CHECK(objectives["objectives"][1]["displayText"] == "");
    CHECK(objectives["objectives"][1]["displayTextId"] == 999);
    CHECK(objectives["objectives"][1]["textMissing"] == true);
    CHECK(objectives["objectives"][2]["displayText"] == "Present");
    CHECK_FALSE(objectives["objectives"][2].contains("textMissing"));
}

TEST_CASE("Native localized textMissing rows keep their text IDs on write back") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, localizedObjectiveQuest(999));
    writeTexts(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);
    const auto context = localizedQuestContext(root);
    const std::string key = "000800:Patch.esp";
    const auto stagesRead = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", key, context})));
    const auto objectivesRead = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", key, context})));
    REQUIRE(objectivesRead["objectives"][1]["textMissing"] == true);
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({
        "Patch.esp", key, objectivesRead["objectives"], context})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({
        "Patch.esp", key, stagesRead["stages"], context})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto objectives = fo4::forms::decode_quest_objectives(disk, fo4::FormKey::parse(key));
    REQUIRE(objectives.objectives.size() == 3u);
    REQUIRE(objectives.objectives[0].display_text.has_value());
    REQUIRE(objectives.objectives[1].display_text.has_value());
    REQUIRE(objectives.objectives[2].display_text.has_value());
    CHECK(objectives.objectives[0].display_text->id == 0u);
    CHECK(objectives.objectives[1].display_text->id == 999u);
    CHECK(objectives.objectives[2].display_text->id == 84u);
    const auto stages = fo4::forms::decode_quest_stages(disk, fo4::FormKey::parse(key));
    REQUIRE(stages.stages.size() == 1u);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
    CHECK(stages.stages[0].log_entries[0].log_text->id == 999u);
    const auto dlstrings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    CHECK(dlstrings.entries.size() == 1u);
    const auto strings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    CHECK(strings.entries.size() == 1u);
}

TEST_CASE("Native textMissing rows without an existing text ID are refused without writing") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, localizedObjectiveQuest(999));
    writeTexts(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);
    const auto context = localizedQuestContext(root);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = readBytes(path);
    const nlohmann::json objectives = nlohmann::json::array({{
        {"index", 99}, {"flags", 0}, {"displayText", ""}, {"textMissing", true},
        {"targets", nlohmann::json::array()}}});
    const auto objectiveResult = backend.invokeBackend(
        "SetQuestObjectives", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", objectives, context}));
    REQUIRE_FALSE(objectiveResult.has_value());
    CHECK(objectiveResult.error().type == "InvalidQuestObjectivePayload");
    const nlohmann::json stages = nlohmann::json::array({{
        {"index", 77}, {"flags", 0},
        {"logEntries", nlohmann::json::array({nlohmann::json{{"flags", 0}, {"logText", ""}, {"textMissing", true}}})}}});
    const auto stageResult = backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", stages, context}));
    REQUIRE_FALSE(stageResult.has_value());
    CHECK(stageResult.error().type == "InvalidQuestStagePayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native clearing localized quest text writes text ID zero without adding table rows") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, localizedObjectiveQuest(42));
    writeTexts(root);
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);
    const auto context = localizedQuestContext(root);
    const std::string key = "000800:Patch.esp";
    auto stagesRead = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", key, context})));
    auto objectivesRead = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", key, context})));
    REQUIRE(stagesRead["stages"][0]["logEntries"][0]["logText"] == "Log");
    REQUIRE(objectivesRead["objectives"][2]["displayText"] == "Present");
    stagesRead["stages"][0]["logEntries"][0]["logText"] = "";
    objectivesRead["objectives"][2]["displayText"] = "";
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({
        "Patch.esp", key, objectivesRead["objectives"], context})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({
        "Patch.esp", key, stagesRead["stages"], context})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto objectives = fo4::forms::decode_quest_objectives(disk, fo4::FormKey::parse(key));
    REQUIRE(objectives.objectives.size() == 3u);
    REQUIRE(objectives.objectives[1].display_text.has_value());
    REQUIRE(objectives.objectives[2].display_text.has_value());
    CHECK(objectives.objectives[1].display_text->id == 999u);
    CHECK(objectives.objectives[2].display_text->id == 0u);
    const auto stages = fo4::forms::decode_quest_stages(disk, fo4::FormKey::parse(key));
    REQUIRE(stages.stages.size() == 1u);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    REQUIRE(stages.stages[0].log_entries[0].log_text.has_value());
    CHECK(stages.stages[0].log_entries[0].log_text->id == 0u);
    const auto dlstrings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    CHECK(dlstrings.entries.size() == 1u);
    const auto strings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    CHECK(strings.entries.size() == 1u);
}
