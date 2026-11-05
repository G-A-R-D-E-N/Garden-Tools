#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_quest_e2e_" + id);
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
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "QuestEndToEnd"}, {"resolved", true}}})}})
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

}

TEST_CASE("Quest end-to-end build survives plain atomic save and reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{
            {"editorId", "E2EWelcome"}, {"name", "Welcome"}, {"flags", 0x11}, {"priority", 30}}})));
    const auto questKey = created.at("formKey").get<std::string>();

    REQUIRE(backend.invokeBackend("SetQuestAliases", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"kind", "reference"}, {"id", 1}, {"name", "Player"}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({
            {{"index", 10}, {"startUp", true}, {"logEntry", "Begin"}},
            {{"index", 20}, {"flags", 1}, {"logEntry", "Done"}}})})).has_value());
    REQUIRE(backend.invokeBackend("SetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{
            {"index", 0}, {"flags", 0}, {"displayText", "Objective"},
            {"targets", nlohmann::json::array({{
                {"alias", "Player"}, {"flags", 0},
                {"conditions", nlohmann::json::array({{
                    {"function", "GetStage"}, {"operator", "=="}, {"param1", questKey}, {"value", 10}}})}}})}}})})).has_value());

    const auto readStages = parseResult(backend.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readStages.at("stages").size() == 2u);
    CHECK(readStages.at("stages")[0].at("index") == 10);
    CHECK(readStages.at("stages")[0].at("startUp") == true);
    CHECK(readStages.at("stages")[0].at("flags") == 2);
    CHECK(readStages.at("stages")[0].at("logEntries")[0].at("logText") == "Begin");
    CHECK(readStages.at("stages")[1].at("startUp") == false);
    CHECK(readStages.at("stages")[1].at("flags") == 1);

    const auto readAliases = parseResult(backend.invokeBackend(
        "GetQuestAliases", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readAliases.at("aliases").size() == 1u);
    CHECK(readAliases.at("aliases")[0].at("name") == "Player");
    CHECK(readAliases.at("aliases")[0].at("kind") == "reference");

    const auto readObjectives = parseResult(backend.invokeBackend(
        "GetQuestObjectives", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readObjectives.at("objectives").size() == 1u);
    CHECK(readObjectives.at("objectives")[0].at("displayText") == "Objective");
    CHECK(readObjectives.at("objectives")[0].at("targets")[0].at("aliasName") == "Player");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) != emptyPlugin(0u));

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse(questKey);
    CHECK(zstringOf(subrecordOf(disk, key, "FULL")) == "Welcome");
    const auto* dnam = subrecordOf(disk, key, "DNAM");
    REQUIRE(dnam != nullptr);
    REQUIRE(dnam->data.size() == 12u);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[0]) == 0x11);
    CHECK(std::to_integer<std::uint8_t>(dnam->data[2]) == 30);

    const auto stages = fo4::forms::decode_quest_stages(disk, key);
    REQUIRE(stages.stages.size() == 2u);
    CHECK(stages.stages[0].index == 10);
    CHECK(stages.stages[0].flags == 2);
    CHECK(stages.stages[1].index == 20);
    CHECK(stages.stages[1].flags == 1);

    const auto aliases = fo4::forms::decode_quest_aliases(disk, key);
    REQUIRE(aliases.aliases.size() == 1u);
    CHECK(aliases.aliases[0].name == std::optional<std::string>{"Player"});

    const auto objectives = fo4::forms::decode_quest_objectives(disk, key);
    REQUIRE(objectives.objectives.size() == 1u);
    REQUIRE(objectives.objectives[0].targets.size() == 1u);
    const auto& target = objectives.objectives[0].targets[0];
    CHECK(target.alias == 1);
    CHECK(std::any_of(target.subrecords.begin(), target.subrecords.end(), [](const fo4::Subrecord& sub) {
        return sub.signature == fo4::Signature::from_string("CTDA");
    }));
}

TEST_CASE("Quest end-to-end fragment publishes through the journal and reloads its VMAD binding") {
#ifdef __linux__
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto imports = root / "imports";
    writeBytes(path, emptyPlugin(0u));
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json{{"editorId", "E2EFragment"}}})));
    const auto questKey = created.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", questKey,
        nlohmann::json::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    const auto actions = nlohmann::json::array({{{"kind", "stage"}, {"value", 10}}});
    REQUIRE(backend.invokeBackend("SetQuestFragment", nlohmann::json::array({"Patch.esp", questKey, 10, 0, "QF_E2EFragment",
        root.string(), imports.string(), actions})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    CHECK(std::filesystem::exists(root / "QF_E2EFragment.psc"));
    CHECK(std::filesystem::exists(root / "QF_E2EFragment.pex"));
    const auto source = readBytes(root / "QF_E2EFragment.psc");
    const std::string sourceText(reinterpret_cast<const char*>(source.data()), source.size());
    CHECK(sourceText.find("Function Fragment_Stage_0010_Item_00()") != std::string::npos);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse(questKey);
    const auto* vmad = subrecordOf(disk, key, "VMAD");
    REQUIRE(vmad != nullptr);
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    REQUIRE(scripts.trailing_bytes > 0u);
    const auto extras = fo4::read_quest_vmad_extras(
        std::span<const std::byte>(vmad->data).last(scripts.trailing_bytes), scripts.object_format);
    REQUIRE(extras.fragments.size() == 1u);
    CHECK(extras.script_name == "QF_E2EFragment");
    CHECK(extras.fragments[0].script == "QF_E2EFragment");
    CHECK(extras.fragments[0].function == "Fragment_Stage_0010_Item_00");
    CHECK(extras.fragments[0].stage == 10);
    CHECK(extras.fragments[0].stage_index == 0);

    fo4ide::plugin::RadiclePluginBackend restarted;
    restarted.testSetJournalRoot(root / "journal");
    loadPatch(restarted, path);
    const auto readStages = parseResult(restarted.invokeBackend(
        "GetQuestStages", nlohmann::json::array({"Patch.esp", questKey})));
    REQUIRE(readStages.at("stages").size() == 1u);
    CHECK(readStages.at("stages")[0].at("index") == 10);
#else
    SUCCEED("quest fragment journal publish is verified on Linux only");
#endif
}
