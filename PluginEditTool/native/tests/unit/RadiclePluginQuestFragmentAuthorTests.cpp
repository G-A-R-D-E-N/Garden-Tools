#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/pex/pex.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/papyrus/QuestFragmentSource.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/GraphTestSupport.hpp"
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

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

}  // namespace

namespace {

std::string readText(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

fo4::pex::PexFile readPex(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    std::vector<std::uint8_t> raw;
    raw.reserve(bytes.size());
    for (const auto byte : bytes) raw.push_back(std::to_integer<std::uint8_t>(byte));
    return fo4::pex::PexFile::fromBytes(raw);
}

void copyImports(const std::filesystem::path& imports) {
    std::filesystem::copy(fo4::graph::test::baseStubs(), imports, std::filesystem::copy_options::recursive);
}

std::string createStagedQuest(fo4ide::plugin::RadiclePluginBackend& backend, const std::vector<int>& stageIndexes) {
    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "CoWelcome"}}})));
    const auto key = created.at("formKey").get<std::string>();
    auto stages = nlohmann::json::array();
    for (const int index : stageIndexes) {
        stages.push_back({{"index", index}, {"logEntry", "Log " + std::to_string(index)}});
    }
    REQUIRE(backend.invokeBackend("SetQuestStages", nlohmann::json::array({"Patch.esp", key, stages})).has_value());
    return key;
}

nlohmann::json fragmentArgs(const std::string& key, int stage, const std::filesystem::path& root,
                            const std::filesystem::path& imports, const nlohmann::json& actions) {
    return nlohmann::json::array({"Patch.esp", key, stage, 0, "QF_CoWelcome", root.string(), imports.string(), actions});
}

nlohmann::json removeArgs(const std::string& key, int stage, const std::filesystem::path& root,
                          const std::filesystem::path& imports) {
    return nlohmann::json::array({"Patch.esp", key, stage, 0, "QF_CoWelcome", root.string(), imports.string()});
}

nlohmann::json listedFragments(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& key) {
    return parseResult(backend.invokeBackend("GetQuestFragments", nlohmann::json::array({"Patch.esp", key})));
}

}  // namespace

TEST_CASE("Native quest fragment bindings list and remove one binding without a journal") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto key = createStagedQuest(backend, {10, 20});
    CHECK(listedFragments(backend, key).empty());
    const auto display = nlohmann::json::array({{{"kind", "display"}, {"value", 0}, {"visible", false}, {"force", true}}});
    const auto complete = nlohmann::json::array({{{"kind", "complete"}, {"value", 1}, {"completed", false}}});
    REQUIRE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, display)).has_value());
    REQUIRE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, 20, root, imports, complete)).has_value());

    auto listed = listedFragments(backend, key);
    REQUIRE(listed.is_array());
    REQUIRE(listed.size() == 2u);
    CHECK(listed[0].at("stage").get<int>() == 10);
    CHECK(listed[0].at("logEntryIndex").get<int>() == 0);
    CHECK(listed[0].at("script").get<std::string>() == "QF_CoWelcome");
    CHECK(listed[0].at("function").get<std::string>() == "Fragment_Stage_0010_Item_00");
    CHECK(listed[0].at("fragmentScript").get<std::string>() == "QF_CoWelcome");
    CHECK(listed[1].at("stage").get<int>() == 20);
    CHECK(listed[1].at("function").get<std::string>() == "Fragment_Stage_0020_Item_00");

    REQUIRE(backend.invokeBackend("RemoveQuestFragment", removeArgs(key, 10, root, imports)).has_value());
    listed = listedFragments(backend, key);
    REQUIRE(listed.size() == 1u);
    CHECK(listed[0].at("stage").get<int>() == 20);
    const auto missing = backend.invokeBackend("RemoveQuestFragment", removeArgs(key, 10, root, imports));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("not found") != std::string::npos);
    CHECK(listedFragments(backend, key).size() == 1u);
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    REQUIRE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes(path) == emptyPlugin(0u));
}

TEST_CASE("Native SetQuestFragment refuses malformed objective flags and stages nothing") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto key = createStagedQuest(backend, {10});
    const std::vector<nlohmann::json> malformed{
        nlohmann::json::array({{{"kind", "display"}, {"value", 0}, {"visible", "yes"}}}),
        nlohmann::json::array({{{"kind", "complete"}, {"value", 0}, {"visible", true}}}),
        nlohmann::json::array({{{"kind", "display"}, {"value", 0}, {"completed", true}}}),
        nlohmann::json::array({{{"kind", "stage"}, {"value", 10}, {"force", false}}})};
    for (const auto& actions : malformed) {
        CHECK_FALSE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, actions)).has_value());
    }
    CHECK(listedFragments(backend, key).empty());
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    REQUIRE(backend.invoke("backend.DiscardPlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

TEST_CASE("Journal saves objective flags from JSON into the quest fragment script and PEX") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    fo4ide::plugin::RadiclePluginBackend backend; backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);
    const auto key = createStagedQuest(backend, {10});
    const auto actions = nlohmann::json::array({{{"kind", "display"}, {"value", 0}, {"visible", false}, {"force", true}},
                                                {{"kind", "complete"}, {"value", 1}, {"completed", false}},
                                                {{"kind", "stage"}, {"value", 10}}});
    REQUIRE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, actions)).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto source = readText(root / "QF_CoWelcome.psc");
    CHECK(source.find("SetObjectiveDisplayed(0, false, true)") != std::string::npos);
    CHECK(source.find("SetObjectiveCompleted(1, false)") != std::string::npos);
    CHECK(source.find("SetStage(10)") != std::string::npos);
    const auto pex = readPex(root / "QF_CoWelcome.pex");
    REQUIRE(pex.objects.size() == 1u);
    CHECK(pex.objects[0].name == "QF_CoWelcome");
}

TEST_CASE("Journal removes one quest fragment binding across save and reload") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    const auto display = nlohmann::json::array({{{"kind", "display"}, {"value", 0}}});
    const auto stage = nlohmann::json::array({{{"kind", "stage"}, {"value", 20}}});
    std::string key;
    {
        fo4ide::plugin::RadiclePluginBackend writer; writer.testSetJournalRoot(root / "journal");
        loadPatch(writer, path);
        key = createStagedQuest(writer, {10, 20});
        REQUIRE(writer.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, display)).has_value());
        REQUIRE(writer.invokeBackend("SetQuestFragment", fragmentArgs(key, 20, root, imports, stage)).has_value());
        REQUIRE(writer.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    {
        fo4ide::plugin::RadiclePluginBackend remover; remover.testSetJournalRoot(root / "journal");
        loadPatch(remover, path);
        REQUIRE(listedFragments(remover, key).size() == 2u);
        REQUIRE(remover.invokeBackend("RemoveQuestFragment", removeArgs(key, 10, root, imports)).has_value());
        REQUIRE(remover.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    {
        fo4ide::plugin::RadiclePluginBackend reader; reader.testSetJournalRoot(root / "journal");
        loadPatch(reader, path);
        const auto listed = listedFragments(reader, key);
        REQUIRE(listed.size() == 1u);
        CHECK(listed[0].at("function").get<std::string>() == "Fragment_Stage_0020_Item_00");
        const auto source = readText(root / "QF_CoWelcome.psc");
        CHECK(source.find("Fragment_Stage_0010_Item_00") == std::string::npos);
        CHECK(source.find("Fragment_Stage_0020_Item_00") != std::string::npos);
        const auto pex = readPex(root / "QF_CoWelcome.pex");
        REQUIRE(pex.objects.size() == 1u);
        CHECK(pex.objects[0].name == "QF_CoWelcome");
    }
}

TEST_CASE("Journal refuses to remove a binding backed by unmanaged script source") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    const auto display = nlohmann::json::array({{{"kind", "display"}, {"value", 0}}});
    std::string key;
    {
        fo4ide::plugin::RadiclePluginBackend writer; writer.testSetJournalRoot(root / "journal");
        loadPatch(writer, path);
        key = createStagedQuest(writer, {10});
        REQUIRE(writer.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, display)).has_value());
        REQUIRE(writer.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    writeBytes(root / "QF_CoWelcome.psc", rawBytes("Scriptname QF_CoWelcome Extends Quest Hidden Const\n\n"
        "Function Fragment_Stage_0010_Item_00()\n    SetObjectiveDisplayed(0)\nEndFunction\n"));
    const auto scriptBefore = readBytes(root / "QF_CoWelcome.psc");
    const auto pexBefore = readBytes(root / "QF_CoWelcome.pex");
    fo4ide::plugin::RadiclePluginBackend reader; reader.testSetJournalRoot(root / "journal");
    loadPatch(reader, path);
    const auto pluginBefore = readBytes(path);
    const auto removed = reader.invokeBackend("RemoveQuestFragment", removeArgs(key, 10, root, imports));
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().type == "UnmanagedQuestFragmentSource");
    CHECK(readBytes(root / "QF_CoWelcome.psc") == scriptBefore);
    CHECK(readBytes(root / "QF_CoWelcome.pex") == pexBefore);
    CHECK(readBytes(path) == pluginBefore);
    CHECK(listedFragments(reader, key).size() == 1u);
}

TEST_CASE("Quest fragment source edits carry objective flags and removals without a journal") {
    using namespace fo4ide::papyrus;
    const std::vector<QuestFragmentAction> stage10{
        {QuestFragmentActionKind::Display, 0, false, true, std::nullopt},
        {QuestFragmentActionKind::Complete, 1, std::nullopt, std::nullopt, false},
        {QuestFragmentActionKind::SetStage, 10, std::nullopt, std::nullopt, std::nullopt}};
    const std::vector<QuestFragmentAction> stage20{
        {QuestFragmentActionKind::Display, 0, true, std::nullopt, std::nullopt}};
    const auto first = updateQuestFragmentSource({}, "QF_CoWelcome", 10, 0, stage10);
    REQUIRE(first.has_value());
    CHECK(isManagedQuestFragmentSource(*first));
    CHECK(first->find("SetObjectiveDisplayed(0, false, true)") != std::string::npos);
    CHECK(first->find("SetObjectiveCompleted(1, false)") != std::string::npos);
    CHECK(first->find("SetStage(10)") != std::string::npos);
    CHECK(first->find("Fragment_Stage_0010_Item_00") != std::string::npos);
    const auto both = updateQuestFragmentSource(*first, "QF_CoWelcome", 20, 0, stage20);
    REQUIRE(both.has_value());
    const auto removed = removeQuestFragmentSource(*both, "QF_CoWelcome", 10, 0);
    REQUIRE(removed.has_value());
    CHECK(removed->find("Fragment_Stage_0010_Item_00") == std::string::npos);
    CHECK(removed->find("SetObjectiveCompleted(1, false)") == std::string::npos);
    CHECK(removed->find("Fragment_Stage_0020_Item_00") != std::string::npos);
    CHECK_FALSE(removeQuestFragmentSource(*removed, "QF_CoWelcome", 10, 0).has_value());
    const std::string unmanaged = "Scriptname QF_CoWelcome Extends Quest\n";
    CHECK_FALSE(isManagedQuestFragmentSource(unmanaged));
    CHECK_FALSE(removeQuestFragmentSource(unmanaged, "QF_CoWelcome", 10, 0).has_value());
}

TEST_CASE("Quest fragment objective flags stage in memory without a journal") {
    const auto root = tempRoot(); Cleanup cleanup{root}; const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin(0u));
    const auto imports = root / "imports"; copyImports(imports);
    fo4ide::plugin::RadiclePluginBackend backend; loadPatch(backend, path);
    const auto key = createStagedQuest(backend, {10});
    const auto actions = nlohmann::json::array({{{"kind", "display"}, {"value", 0}, {"visible", false}, {"force", true}},
                                                {{"kind", "complete"}, {"value", 1}, {"completed", false}},
                                                {{"kind", "stage"}, {"value", 10}}});
    REQUIRE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, 10, root, imports, actions)).has_value());
    const auto listed = listedFragments(backend, key);
    REQUIRE(listed.size() == 1u);
    CHECK(listed[0].at("function").get<std::string>() == "Fragment_Stage_0010_Item_00");
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.psc"));
    CHECK_FALSE(std::filesystem::exists(root / "QF_CoWelcome.pex"));
}
