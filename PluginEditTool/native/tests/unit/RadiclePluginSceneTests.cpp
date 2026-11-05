#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_scene_" + std::to_string(id));
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
                                      {"source", "SceneTests"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
}

void openPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root,
               std::uint32_t tes4Flags = 0) {
    writeBytes(root / "Patch.esp", emptyPlugin(tes4Flags));
    loadPatch(backend, root / "Patch.esp");
}

std::string freshQuest(fo4ide::plugin::RadiclePluginBackend& backend) {
    return createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "SceneQuest"}}})));
}

std::string freshScene(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return createdKey(backend.invokeBackend("CreateScene", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "TestScene"}, {"flags", 2}}})));
}

nlohmann::json sceneOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& scene) {
    return parseResult(backend.invokeBackend("GetScene", nlohmann::json::array({"Patch.esp", scene})));
}

std::string freshTopic(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "SceneTopic"}}})));
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

}

TEST_CASE("Scene authoring survives save and reload with the same read") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);
    REQUIRE(sceneOf(backend, scene).at("editorId") == "TestScene");
    REQUIRE(sceneOf(backend, scene).at("flags") == 2);

    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "Opening"}}})).has_value());
    nlohmann::json startConditions = nlohmann::json::array();
    startConditions.push_back(nlohmann::json{
        {"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}});
    REQUIRE(backend.invokeBackend("SetScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"editorWidth", 400}, {"flags", 1},
                                              {"questStageOnStart", 10},
                                              {"startConditions", startConditions}}})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneActor", nlohmann::json::array({"Patch.esp", scene, 0})).has_value());
    REQUIRE(backend.invokeBackend("SetSceneActor", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"alias", 3}, {"flags", 1}, {"behaviorFlags", 26}}})).has_value());

    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, 2})).has_value());
    REQUIRE(backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"startPhase", 0}, {"endPhase", 0}, {"timerMax", 2.5f},
                                              {"timerMin", 1.5f}}})).has_value());

    const auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 1);
    CHECK(readback.at("phases").at(0).at("name") == "Opening");
    CHECK(readback.at("phases").at(0).at("editorWidth") == 400);
    CHECK(readback.at("phases").at(0).at("flags") == 1);
    CHECK(readback.at("phases").at(0).at("questStageOnStart") == 10);
    CHECK(readback.at("phases").at(0).at("startConditions").at(0).at("function") == "GetStage");
    REQUIRE(readback.at("actors").size() == 1);
    CHECK(readback.at("actors").at(0).at("alias") == 3);
    CHECK(readback.at("actors").at(0).at("flags") == 1);
    CHECK(readback.at("actors").at(0).at("behaviorFlags") == 26);
    REQUIRE(readback.at("actions").size() == 1);
    CHECK(readback.at("actions").at(0).at("type") == 2);
    CHECK(readback.at("actions").at(0).at("typeName") == "timer");
    CHECK(readback.at("actions").at(0).at("startPhase") == 0);
    CHECK(readback.at("actions").at(0).at("payload").at("timerMax") == 2.5f);
    CHECK(readback.at("actions").at(0).at("payload").at("timerMin") == 1.5f);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto sceneKey = fo4::FormKey::parse(scene);
    CHECK(subrecordOf(saved, sceneKey, "HNAM") != nullptr);
    CHECK(subrecordOf(saved, sceneKey, "ALID") != nullptr);
    CHECK(subrecordOf(saved, sceneKey, "ANAM") != nullptr);
    CHECK(subrecordOf(saved, sceneKey, "PNAM") != nullptr);

    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, path);
    CHECK(sceneOf(restarted, scene) == readback);
}

TEST_CASE("Scene phases and actions move, remove and renumber as stored order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 1, nlohmann::json{{"name", "P2"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 0, 0})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 1, 2})).has_value());
    REQUIRE(backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"startPhase", 1}}})).has_value());

    REQUIRE(backend.invokeBackend("MoveScenePhase", nlohmann::json::array({"Patch.esp", scene, 0, 1})).has_value());
    REQUIRE(backend.invokeBackend("MoveSceneAction", nlohmann::json::array({"Patch.esp", scene, 1, 0})).has_value());
    auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 2);
    CHECK(readback.at("phases").at(0).at("name") == "P2");
    CHECK(readback.at("phases").at(1).at("name") == "P1");
    REQUIRE(readback.at("actions").size() == 2);
    CHECK(readback.at("actions").at(0).at("type") == 2);
    CHECK(readback.at("actions").at(0).at("actionIndex") == 0);
    CHECK(readback.at("actions").at(1).at("type") == 0);
    CHECK(readback.at("actions").at(1).at("actionIndex") == 1);

    REQUIRE(backend.invokeBackend("RemoveScenePhase", nlohmann::json::array({"Patch.esp", scene, 1})).has_value());
    REQUIRE(backend.invokeBackend("RemoveSceneAction", nlohmann::json::array({"Patch.esp", scene, 0})).has_value());
    readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 1);
    CHECK(readback.at("phases").at(0).at("name") == "P2");
    REQUIRE(readback.at("actions").size() == 1);
    CHECK(readback.at("actions").at(0).at("type") == 0);
    CHECK(readback.at("actions").at(0).at("actionIndex") == 0);
    CHECK(readback.at("lastActionIndex") == 0);

    REQUIRE(backend.invokeBackend("SetScene", nlohmann::json::array({
        "Patch.esp", scene, nlohmann::json{{"flags", 9}, {"index", 3}}})).has_value());
    readback = sceneOf(backend, scene);
    CHECK(readback.at("flags") == 9);
    CHECK(readback.at("sceneIndex") == 3);
}

TEST_CASE("Scene dialogue actions resolve their topic and INFO response texts") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto topic = freshTopic(backend, quest);
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"responses", nlohmann::json::array({{{"text", "Get out of here."}}})}}})));
    const auto scene = freshScene(backend, quest);
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 0, 0})).has_value());
    REQUIRE(backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"startPhase", 0}, {"topic", topic}}})).has_value());

    const auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("actions").size() == 1);
    const auto& action = readback.at("actions").at(0);
    CHECK(action.at("payload").at("topic") == topic);
    REQUIRE(action.at("topics").size() == 1);
    CHECK(action.at("topics").at(0).at("formKey") == topic);
    CHECK(action.at("topics").at(0).at("infos").at(0).at("formKey") == info);
    CHECK(action.at("topics").at(0).at("infos").at(0).at("responses").at(0).at("text") == "Get out of here.");
}

TEST_CASE("Scene edits reject out of range indexes and bad input without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);

    CHECK(errorType(backend.invokeBackend("CreateScene", nlohmann::json::array({
        "Patch.esp", "000ABC:Fallout4.esm", nlohmann::json::object()}))) == "RecordNotLocal");
    CHECK(errorType(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 5}))) == "SceneIndexOutOfRange");
    CHECK(errorType(backend.invokeBackend("RemoveSceneActor", nlohmann::json::array({
        "Patch.esp", scene, 0}))) == "SceneIndexOutOfRange");
    CHECK(errorType(backend.invokeBackend("AddSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, 9}))) == "InvalidSceneArgument");
    CHECK(errorType(backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"unknownMember", 1}}}))) == "InvalidSceneArgument");
    CHECK(!backend.invokeBackend("SetScene", nlohmann::json::array({
        "Patch.esp", scene, nlohmann::json{{"parentQuest", "000FFF:Patch.esp"}}}))
               .has_value());

    const auto snapshot = sceneOf(backend, scene);
    CHECK(snapshot.at("phases").empty());
    CHECK(snapshot.at("actors").empty());
    CHECK(snapshot.at("actions").empty());
}

TEST_CASE("Timer action fields need a phase and are inserted before the timer values") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 0, 2})).has_value());
    CHECK(!backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"startPhase", 0}}})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("SetSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"startPhase", 0}, {"timerMax", 3.0f}}})).has_value());
    const auto readback = sceneOf(backend, scene);
    CHECK(readback.at("actions").at(0).at("startPhase") == 0);
    CHECK(readback.at("actions").at(0).at("payload").at("timerMax") == 3.0f);
}

TEST_CASE("Inserting and moving scene actions keeps the stored action indexes ordered") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 0, 0})).has_value());
    REQUIRE(backend.invokeBackend("AddSceneAction", nlohmann::json::array({"Patch.esp", scene, 1, 2})).has_value());
    REQUIRE(backend.invokeBackend("MoveSceneAction", nlohmann::json::array({
        "Patch.esp", scene, 1, 0})).has_value());
    const auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("actions").size() == 2);
    CHECK(readback.at("actions").at(0).at("type") == 2);
    CHECK(readback.at("actions").at(0).at("actionIndex") == 0);
    CHECK(readback.at("actions").at(1).at("type") == 0);
    CHECK(readback.at("actions").at(1).at("actionIndex") == 1);
    CHECK(readback.at("lastActionIndex") == 1);
}

TEST_CASE("Scene phases carry start and completion conditions through the scoped condition writer") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto scene = freshScene(backend, quest);
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    nlohmann::json startConditions = nlohmann::json::array();
    startConditions.push_back(nlohmann::json{
        {"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}});
    nlohmann::json completionConditions = nlohmann::json::array();
    completionConditions.push_back(nlohmann::json{
        {"function", "GetStageDone"}, {"operator", "=="}, {"value", 1}, {"param1", 2}, {"param2", 10}});
    REQUIRE(backend.invokeBackend("SetScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0,
        nlohmann::json{{"startConditions", startConditions},
                       {"completionConditions", completionConditions}}})).has_value());
    const auto readback = sceneOf(backend, scene);
    CHECK(readback.at("phases").at(0).at("startConditions").at(0).at("function") == "GetStage");
    CHECK(readback.at("phases").at(0).at("completionConditions").at(0).at("function") == "GetStageDone");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, root / "Patch.esp");
    CHECK(sceneOf(restarted, scene) == readback);
}

// Detects: a phase whose condition run cannot be decoded must be flagged unreadable, not reported as an empty (unconditional) list.
TEST_CASE("Scene read flags phase conditions it cannot decode instead of reporting none") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto top = fo4::test::begin_group(out, "SCEN", 0);
    const auto scene = fo4::test::begin_record(out, "SCEN", 0, 0x800);
    const std::span<const std::byte> none;
    fo4::test::write_subrecord(out, "HNAM", none);
    fo4::test::write_subrecord(out, "CTDA", std::vector<std::byte>(20));
    fo4::test::write_subrecord(out, "NEXT", none);
    fo4::test::write_subrecord(out, "NEXT", none);
    fo4::test::write_subrecord(out, "HNAM", none);
    fo4::test::ByteWriter quest;
    quest.u32(0x00000801u);
    fo4::test::write_subrecord(out, "PNAM", quest.bytes);
    fo4::test::end_record(out, scene);
    fo4::test::end_group(out, top);
    writeBytes(root / "Patch.esp", out.bytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, root / "Patch.esp");
    const auto broken = sceneOf(backend, "000800:Patch.esp");
    REQUIRE(broken.at("phases").size() == 1);
    CHECK(broken.at("phases").at(0).at("startConditions").empty());
    CHECK(broken.at("phases").at(0).at("startConditionsUnreadable") == true);
    CHECK_FALSE(broken.at("phases").at(0).contains("completionConditionsUnreadable"));

    const auto quest2 = freshQuest(backend);
    const auto healthy = freshScene(backend, quest2);
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", healthy, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    const auto readback = sceneOf(backend, healthy);
    CHECK_FALSE(readback.at("phases").at(0).contains("startConditionsUnreadable"));
    CHECK_FALSE(readback.contains("conditionsUnreadable"));
}
