#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_dialogue_" + std::to_string(id));
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

std::vector<std::byte> emptyPlugin(std::uint32_t tes4Flags = 0) {
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

std::string createdKey(const fo4ide::Result<nlohmann::json>& result) {
    return parseResult(result).at("formKey").get<std::string>();
}

std::string errorType(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(!result.has_value());
    return result.error().type;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", path.string()},
                                      {"source", "DialogueTests"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
}

nlohmann::json dialogueOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return parseResult(backend.invokeBackend("GetDialogue", nlohmann::json::array({"Patch.esp", quest})));
}

nlohmann::json stageAtLeast(int stage) {
    return nlohmann::json::array({{{"function", "GetStage"}, {"operator", ">="}, {"value", stage}, {"param1", 2}}});
}

void openPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root,
               std::uint32_t tes4Flags) {
    writeBytes(root / "Patch.esp", emptyPlugin(tes4Flags));
    loadPatch(backend, root / "Patch.esp");
}

std::string freshQuest(fo4ide::plugin::RadiclePluginBackend& backend) {
    return createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "DlgQuest"}}})));
}

std::string freshTopic(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    const auto branch = createdKey(backend.invokeBackend("CreateDialogueBranch", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "DlgBranch"}}})));
    return createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "DlgTopic"}, {"branch", branch}}})));
}

nlohmann::json firstInfoOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0);
}

fo4ide::Result<nlohmann::json> editInfo(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& info,
                                        const nlohmann::json& spec) {
    return backend.invokeBackend("SetInfo", nlohmann::json::array({"Patch.esp", info, spec}));
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

std::vector<int> savedStageBytes(const std::filesystem::path& path, const std::string& info) {
    const auto saved = fo4::read_plugin_file(path);
    const auto* tiqs = subrecordOf(saved, fo4::FormKey::parse(info), "TIQS");
    REQUIRE(tiqs != nullptr);
    std::vector<int> out;
    for (const auto byte : tiqs->data) out.push_back(std::to_integer<int>(byte));
    return out;
}

}

TEST_CASE("Dialogue authoring survives save and reload with the same read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "DlgQuest"}}})));
    const auto branch = createdKey(backend.invokeBackend("CreateDialogueBranch", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "DlgBranch"}, {"category", 1}, {"flags", 2}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "DlgTopic"}, {"name", "Hello"}, {"priority", 60},
            {"branch", branch}, {"subtype", 1}, {"subtypeName", "GREE"}}})));
    REQUIRE(backend.invokeBackend("SetDialogueBranchStart", nlohmann::json::array({"Patch.esp", branch, topic}))
                .has_value());
    const auto first = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{
            {"responses", nlohmann::json::array({{{"text", "Hi there"}, {"scriptNotes", "note"}}})},
            {"conditions", nlohmann::json::array({{{"function", "GetStage"}, {"operator", ">="},
                {"value", 5}, {"param1", 2}}})}}})));
    const auto second = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));

    const auto beforeSave = dialogueOf(backend, quest);
    REQUIRE(beforeSave.at("topics").size() == 1);
    const auto infos = beforeSave.at("topics").at(0).at("infos");
    REQUIRE(infos.size() == 2);
    CHECK(infos.at(0).at("responses").at(0).at("text") == "Hi there");
    CHECK(infos.at(0).at("responses").at(0).at("scriptNotes") == "note");
    CHECK(infos.at(0).at("responses").at(0).at("emotion").is_null());
    CHECK(infos.at(0).at("conditions").at(0).at("function") == "GetStage");
    CHECK(infos.at(0).at("previous").is_null());
    CHECK(infos.at(1).at("previous") == infos.at(0).at("formKey"));

    REQUIRE(backend.invokeBackend("SetInfo", nlohmann::json::array({"Patch.esp", first, nlohmann::json{
        {"responses", nlohmann::json::array({{{"text", "Changed"}}})}}})).has_value());
    REQUIRE(backend.invokeBackend("RemoveInfo", nlohmann::json::array({"Patch.esp", second})).has_value());
    const auto afterEdits = dialogueOf(backend, quest);
    const auto remaining = afterEdits.at("topics").at(0).at("infos");
    REQUIRE(remaining.size() == 1);
    CHECK(remaining.at(0).at("responses").at(0).at("text") == "Changed");
    CHECK(remaining.at(0).at("conditions").at(0).at("function") == "GetStage");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto* tifc = subrecordOf(saved, fo4::FormKey::parse(topic), "TIFC");
    REQUIRE(tifc != nullptr);
    REQUIRE(tifc->data.size() == 4);
    CHECK(std::to_integer<int>(tifc->data[0]) == 1);

    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, path);
    CHECK(dialogueOf(restarted, quest) == afterEdits);
}

TEST_CASE("Dialogue authoring rejects bad input with typed errors and leaves the topic list unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "DlgQuest"}}})));
    REQUIRE(backend.invokeBackend("CreateDialogueBranch", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "DlgBranch"}}})).has_value());

    CHECK(errorType(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "Bad"}, {"branch", "000FFF:Patch.esp"}}}))) == "BranchMissing");
    CHECK(errorType(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "Bad"}, {"subtypeName", "GRE"}}}))) ==
          "InvalidDialogueArgument");
    CHECK(errorType(backend.invokeBackend("CreateDialogueBranch", nlohmann::json::array({
        "Patch.esp", "000ABC:Fallout4.esm", nlohmann::json::object()}))) == "RecordNotLocal");
    CHECK(errorType(backend.invokeBackend("SetInfo", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json::object()}))) == "InfoMissing");
    CHECK(errorType(backend.invokeBackend("RemoveInfo", nlohmann::json::array({"Patch.esp", quest}))) ==
          "InfoMissing");

    const auto dialogue = dialogueOf(backend, quest);
    CHECK(dialogue.at("branches").size() == 1);
    CHECK(dialogue.at("topics").empty());
}

TEST_CASE("SetInfo keeps omitted runs, clears explicit empty runs and refuses bad flags without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic,
        nlohmann::json{{"flags", 1}, {"resetHours", 8}, {"responses", nlohmann::json::array({{{"text", "Hi"}}})},
                       {"conditions", stageAtLeast(5)}}})));
    const auto created = firstInfoOf(backend, quest);
    REQUIRE(created.at("flags") == 1);
    REQUIRE(created.at("resetTimeEncoded") == 21845);
    REQUIRE(created.at("responses").size() == 1);
    REQUIRE(created.at("conditions").size() == 1);

    REQUIRE(editInfo(backend, info, nlohmann::json{{"conditions", stageAtLeast(9)}}).has_value());
    const auto conditionsOnly = firstInfoOf(backend, quest);
    CHECK(conditionsOnly.at("responses") == created.at("responses"));
    CHECK(conditionsOnly.at("flags") == 1);
    CHECK(conditionsOnly.at("resetTimeEncoded") == 21845);
    CHECK(conditionsOnly.at("conditions").at(0).at("value") == 9);

    REQUIRE(editInfo(backend, info, nlohmann::json{{"flags", 2}}).has_value());
    const auto flagsOnly = firstInfoOf(backend, quest);
    CHECK(flagsOnly.at("flags") == 2);
    CHECK(flagsOnly.at("responses") == created.at("responses"));
    CHECK(flagsOnly.at("conditions") == conditionsOnly.at("conditions"));

    REQUIRE(editInfo(backend, info, nlohmann::json{{"flags", nullptr}, {"responses", nullptr}}).has_value());
    CHECK(firstInfoOf(backend, quest) == flagsOnly);

    REQUIRE(editInfo(backend, info, nlohmann::json{{"responses", nlohmann::json::array()}}).has_value());
    const auto noResponses = firstInfoOf(backend, quest);
    CHECK(noResponses.at("responses").empty());
    CHECK(noResponses.at("conditions") == conditionsOnly.at("conditions"));

    REQUIRE(editInfo(backend, info, nlohmann::json{{"conditions", nlohmann::json::array()}}).has_value());
    const auto cleared = firstInfoOf(backend, quest);
    CHECK(cleared.at("conditions").empty());
    CHECK(cleared.at("flags") == 2);

    const auto snapshot = dialogueOf(backend, quest);
    CHECK(errorType(editInfo(backend, info, nlohmann::json{{"flags", 70000}})) == "InvalidDialogueArgument");
    CHECK(dialogueOf(backend, quest) == snapshot);
}

TEST_CASE("INFO resetHours is taken in hours, stored rounded and refused outside the 16-bit range") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"resetHours", 8}}})));
    CHECK(firstInfoOf(backend, quest).at("resetTimeEncoded") == 21845);
    CHECK(firstInfoOf(backend, quest).at("resetHours") == 8.0);

    REQUIRE(editInfo(backend, info, nlohmann::json{{"resetHours", 1}}).has_value());
    CHECK(firstInfoOf(backend, quest).at("resetTimeEncoded") == 2731);

    REQUIRE(editInfo(backend, info, nlohmann::json{{"resetHours", 24}}).has_value());
    CHECK(firstInfoOf(backend, quest).at("resetTimeEncoded") == 65535);
    CHECK(firstInfoOf(backend, quest).at("resetHours") == 24.0);

    const auto snapshot = dialogueOf(backend, quest);
    for (const double hours : {-1.0, 24.5, 25.0}) {
        CHECK(errorType(editInfo(backend, info, nlohmann::json{{"resetHours", hours}})) == "InvalidDialogueArgument");
    }
    CHECK(dialogueOf(backend, quest) == snapshot);
}

TEST_CASE("Text that Windows-1252 cannot hold is refused before anything is written") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    CHECK(errorType(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "Bad"}, {"name", "\xe4\xb8\xad"}}}))) ==
          "InvalidDialogueArgument");
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "Cafe"}, {"name", "caf\xc3\xa9"}}})));
    const auto badText = nlohmann::json::array({{{"text", "\xe4\xb8\xad"}}});
    CHECK(errorType(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"responses", badText}}}))) == "InvalidDialogueArgument");
    REQUIRE(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "Sub"}, {"subtypeName", "caf\xc3\xa9"}}})).has_value());
    const auto dialogue = dialogueOf(backend, quest);
    REQUIRE(dialogue.at("topics").size() == 2);
    CHECK(dialogue.at("topics").at(0).at("name") == "caf\xc3\xa9");
    CHECK(dialogue.at("topics").at(0).at("infos").empty());
    CHECK(dialogue.at("topics").at(1).at("subtypeName") == "caf\xc3\xa9");
}

TEST_CASE("Response text is refused on a localized plugin and the INFO is left as created") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0x80u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));
    const auto snapshot = dialogueOf(backend, quest);
    const auto hiResponse = nlohmann::json::array({{{"text", "Hi"}}});
    CHECK(errorType(editInfo(backend, info, nlohmann::json{{"responses", hiResponse}})) ==
          "LocalizedDialogueUnsupported");
    CHECK(dialogueOf(backend, quest) == snapshot);
}

TEST_CASE("INFO conditions read back by get_dialogue write through SetInfo unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"conditions", stageAtLeast(5)}}})));
    const auto before = dialogueOf(backend, quest);
    const auto conditions = firstInfoOf(backend, quest).at("conditions");
    REQUIRE(conditions.size() == 1);
    REQUIRE(editInfo(backend, info, nlohmann::json{{"conditions", conditions}}).has_value());
    CHECK(dialogueOf(backend, quest) == before);
}

TEST_CASE("CreateInfo setStageOnEnd writes the parent quest stage subrecord before NAM0") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"setStageOnEnd", 30}}})));
    const auto created = firstInfoOf(backend, quest);
    CHECK(created.at("setStageOnEnd") == 30);
    CHECK(created.at("setStageOnBegin").is_null());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(savedStageBytes(root / "Patch.esp", info) == std::vector<int>{0xFF, 0xFF, 0x1E, 0x00});
    const auto saved = fo4::read_plugin_file(root / "Patch.esp");
    const auto* record = fo4::find_record(saved, fo4::FormKey::parse(info));
    REQUIRE(record != nullptr);
    std::vector<std::string> tags;
    for (const auto& sub : record->subrecords) tags.push_back(sub.signature.to_string());
    const auto nam0 = std::find(tags.begin(), tags.end(), "NAM0");
    REQUIRE(nam0 != tags.end());
    REQUIRE(nam0 != tags.begin());
    CHECK(*(nam0 - 1) == "TIQS");

    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, root / "Patch.esp");
    CHECK(dialogueOf(restarted, quest) == dialogueOf(backend, quest));
}

TEST_CASE("CreateInfo and SetInfo refuse parent stage 65535 and -1 and leave the plugin unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"flags", 1}}})));
    const auto snapshot = dialogueOf(backend, quest);
    for (const int stage : {65535, -1}) {
        CHECK(errorType(backend.invokeBackend("CreateInfo", nlohmann::json::array({
            "Patch.esp", topic, nlohmann::json{{"setStageOnBegin", stage}}}))) == "InvalidDialogueArgument");
        CHECK(errorType(backend.invokeBackend("CreateInfo", nlohmann::json::array({
            "Patch.esp", topic, nlohmann::json{{"setStageOnEnd", stage}}}))) == "InvalidDialogueArgument");
        CHECK(errorType(editInfo(backend, info, nlohmann::json{{"setStageOnBegin", stage}})) ==
              "InvalidDialogueArgument");
        CHECK(errorType(editInfo(backend, info, nlohmann::json{{"setStageOnEnd", stage}})) ==
              "InvalidDialogueArgument");
    }
    CHECK(dialogueOf(backend, quest) == snapshot);
}

TEST_CASE("SetInfo with only flags keeps an existing parent stage subrecord") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"setStageOnBegin", 12}, {"setStageOnEnd", 20}}})));
    REQUIRE(editInfo(backend, info, nlohmann::json{{"flags", 2}}).has_value());
    const auto edited = firstInfoOf(backend, quest);
    CHECK(edited.at("flags") == 2);
    CHECK(edited.at("setStageOnBegin") == 12);
    CHECK(edited.at("setStageOnEnd") == 20);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(savedStageBytes(root / "Patch.esp", info) == std::vector<int>{0x0C, 0x00, 0x14, 0x00});
}

TEST_CASE("SetInfo setStageOnEnd replaces only the end half of the parent stage subrecord") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"setStageOnBegin", 12}, {"setStageOnEnd", 20}}})));
    REQUIRE(editInfo(backend, info, nlohmann::json{{"setStageOnEnd", 30}}).has_value());
    const auto edited = firstInfoOf(backend, quest);
    CHECK(edited.at("setStageOnBegin") == 12);
    CHECK(edited.at("setStageOnEnd") == 30);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(savedStageBytes(root / "Patch.esp", info) == std::vector<int>{0x0C, 0x00, 0x1E, 0x00});
}

TEST_CASE("SetInfo setStageOnBegin null clears only the begin half and clearing both removes the subrecord") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"setStageOnBegin", 12}, {"setStageOnEnd", 20}}})));
    REQUIRE(editInfo(backend, info, nlohmann::json{{"setStageOnBegin", nullptr}}).has_value());
    const auto halfCleared = firstInfoOf(backend, quest);
    CHECK(halfCleared.at("setStageOnBegin").is_null());
    CHECK(halfCleared.at("setStageOnEnd") == 20);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(savedStageBytes(root / "Patch.esp", info) == std::vector<int>{0xFF, 0xFF, 0x14, 0x00});

    REQUIRE(editInfo(backend, info, nlohmann::json{{"setStageOnEnd", nullptr}}).has_value());
    const auto bothCleared = firstInfoOf(backend, quest);
    CHECK_FALSE(bothCleared.contains("setStageOnBegin"));
    CHECK_FALSE(bothCleared.contains("setStageOnEnd"));
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(root / "Patch.esp");
    CHECK(subrecordOf(saved, fo4::FormKey::parse(info), "TIQS") == nullptr);
}

TEST_CASE("INFO created without stage keys has no parent stage keys and no TIQS subrecord") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root, 0u);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto plain = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));
    const auto nullStages = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"setStageOnBegin", nullptr}, {"setStageOnEnd", nullptr}}})));
    const auto infos = dialogueOf(backend, quest).at("topics").at(0).at("infos");
    REQUIRE(infos.size() == 2);
    for (const auto& row : infos) {
        CHECK_FALSE(row.contains("setStageOnBegin"));
        CHECK_FALSE(row.contains("setStageOnEnd"));
    }

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(root / "Patch.esp");
    CHECK(subrecordOf(saved, fo4::FormKey::parse(plain), "TIQS") == nullptr);
    CHECK(subrecordOf(saved, fo4::FormKey::parse(nullStages), "TIQS") == nullptr);
}
