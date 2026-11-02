#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_author_" + std::to_string(id));
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

std::vector<std::byte> emptyPlugin(std::uint32_t tes4Flags) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
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
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "QuestAuthor"}, {"resolved", true}}})}})
                .has_value());
}

const fo4::Subrecord* subrecordOf(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view signature) {
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& sub : record->subrecords) {
        if (sub.signature == wanted) return &sub;
    }
    return nullptr;
}

std::string zstringOf(const fo4::Subrecord* sub) {
    REQUIRE(sub != nullptr);
    REQUIRE(!sub->data.empty());
    REQUIRE(sub->data.back() == std::byte{0});
    return std::string(reinterpret_cast<const char*>(sub->data.data()), sub->data.size() - 1);
}

}  // namespace

// Detects: CreateRecord QUST must write the FULL title and the DNAM flags, priority and type from its payload, so a created quest is complete after save.
TEST_CASE("Native CreateRecord QUST writes title, flags, priority and type from its payload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoTitle"}, {"name", "Welcome to Sanctuary"},
            {"flags", 0x11}, {"priority", 30}, {"type", 6}}})));
    const auto key = fo4::FormKey::parse(created.at("formKey").get<std::string>());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    CHECK(zstringOf(subrecordOf(disk, key, "FULL")) == "Welcome to Sanctuary");
    const auto* dnam = subrecordOf(disk, key, "DNAM");
    REQUIRE(dnam != nullptr);
    REQUIRE(dnam->data.size() == 12u);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[1]) == 0x00);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[2]) == 30);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[8]) == 6);
}

// Detects: set_field on QUST\Flags, QUST\Priority, QUST\Type and QUST\Name must update a created quest and keep the values after save and reload.
TEST_CASE("Native set_field edits quest flags, priority, type and title and keeps them after save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoEdit"}, {"name", "Old Title"}}})));
    const auto key = created.at("formKey").get<std::string>();

    const std::pair<std::string, std::string> edits[] = {
        {"QUST\\Flags", "17"}, {"QUST\\Priority", "30"}, {"QUST\\Type", "6"}, {"QUST\\Name", "New Title"}};
    for (const auto& [field, value] : edits) {
        INFO("set " + field);
        const auto result = backend.invokeBackend(
            "SetField", nlohmann::json::array({"Patch.esp", key, field, value}));
        REQUIRE(result.has_value());
    }
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto formKey = fo4::FormKey::parse(key);
    CHECK(zstringOf(subrecordOf(disk, formKey, "FULL")) == "New Title");
    const auto* dnam = subrecordOf(disk, formKey, "DNAM");
    REQUIRE(dnam != nullptr);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[2]) == 30);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[8]) == 6);
}

// Detects: CreateRecord QUST must refuse a title in a localized plugin rather than write inline text, and must leave the plugin file unchanged.
TEST_CASE("Native CreateRecord QUST refuses a title in a localized plugin without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(fo4::RecordFlags::localized));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto result = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoLocal"}, {"name", "Title"}}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidRecordCreationPayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == before);
}

// Detects: a quest title exceeding its row size must be rejected without creating a truncated record.
TEST_CASE("Native CreateRecord QUST rejects an oversized title") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto result = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "QUST",
        nlohmann::json{{"editorId", "Oversized"}, {"name", std::string(65536, 'A')}}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidRecordCreationPayload");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == emptyPlugin(0u));
}

// Detects: an end-to-end quest must survive save and reload with its reference alias, GetStage objective targets, stage flags, title and start-game flag, and its SEQ entry must name the quest.
TEST_CASE("Native quest build reads back after save: alias, GetStage objective, stages, start-game flag and SEQ") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "CoWelcome"}, {"name", "Welcome"}, {"flags", 0x11}}})));
    const auto questKey = created.at("formKey").get<std::string>();

    REQUIRE(backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({
            {{"index", 10}, {"flags", 1}, {"logEntry", "Caf\xC3\xA9"}},
            {{"index", 20}, {"logEntries", nlohmann::json::array({{{"note", "Caf\xC3\xA9"}, {"logText", "Done"}}})}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{
            {"index", 0}, {"flags", 0}, {"displayText", "Caf\xC3\xA9"},
            {"targets", nlohmann::json::array({{
                {"alias", "Player"}, {"flags", 0},
                {"conditions", nlohmann::json::array({{
                    {"function", "GetStage"}, {"operator", "=="}, {"param1", questKey}, {"value", 10}}})}}})}}})})).has_value());

    const auto seqResult = backend.createSeqFile(
        nlohmann::json::array({"Patch.esp", root.string()}));
    if (!seqResult.has_value()) INFO(seqResult.error().type + ": " + seqResult.error().message);
    REQUIRE(seqResult.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto seqBytes = readBytes(root / "Patch.seq");
    REQUIRE(seqBytes.size() == 4u);
    std::uint32_t entry = 0;
    std::memcpy(&entry, seqBytes.data(), sizeof(entry));
    CHECK(entry == 0x00000800u);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse(questKey);
    CHECK(zstringOf(subrecordOf(disk, key, "FULL")) == "Welcome");
    const auto stages = fo4::forms::decode_quest_stages(disk, key);
    REQUIRE(stages.stages.size() == 2u);
    CHECK(stages.stages[0].index == 10);
    CHECK(stages.stages[0].flags == 1);
    const auto objectives = fo4::forms::decode_quest_objectives(disk, key);
    REQUIRE(objectives.objectives.size() == 1u);
    REQUIRE(objectives.objectives[0].targets.size() == 1u);
    CHECK(objectives.objectives[0].targets[0].alias == 1);
    const auto aliases = fo4::forms::decode_quest_aliases(disk, key);
    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].name == std::optional<std::string>{"Player"});

    const auto readStages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readStages.at("stages").size() == 2u);
    CHECK(readStages.at("stages")[0].at("index") == 10);
    CHECK(readStages.at("stages")[0].at("flags") == 1);
    CHECK(readStages.at("stages")[0].at("logEntries")[0].at("logText") == "Caf\xC3\xA9");
    CHECK(readStages.at("stages")[1].at("logEntries")[0].at("note") == "Caf\xC3\xA9");

    const auto readAliases = parseResult(backend.invokeBackend(
        "GetQuestAliases", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readAliases.at("aliases").size() == 1u);
    CHECK(readAliases.at("aliases")[0].at("name") == "Player");
    CHECK(readAliases.at("aliases")[0].at("kind") == "reference");

    const auto readObjectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey})));
    CHECK(readObjectives.at("objectives")[0].at("displayText") == "Caf\xC3\xA9");
    CHECK(readObjectives.at("objectives")[0].at("targets")[0].at("aliasName") == "Player");
}
