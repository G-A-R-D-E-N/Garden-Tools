#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_fragments_" + std::to_string(id));
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

std::string createdKey(const fo4ide::Result<nlohmann::json>& result) {
    return parseResult(result).at("formKey").get<std::string>();
}

std::string errorType(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(!result.has_value());
    return result.error().type;
}

void openPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root) {
    writeBytes(root / "Patch.esp", emptyPlugin());
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()},
                                      {"source", "FragmentTests"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
}

nlohmann::json dialogueOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return parseResult(backend.invokeBackend("GetDialogue", nlohmann::json::array({"Patch.esp", quest})));
}

nlohmann::json sceneOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& scene) {
    return parseResult(backend.invokeBackend("GetScene", nlohmann::json::array({"Patch.esp", scene})));
}

std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(data, data + text.size());
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root) {
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()},
                                      {"source", "FragmentTests"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
}

}

TEST_CASE("INFO fragment bindings survive save and reload and clear their flag bit") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragTopic"}}})));
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"responses", nlohmann::json::array({{{"text", "Hi"}}})}}})));
    REQUIRE(dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments").empty());

    REQUIRE(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Begin"})).has_value());
    REQUIRE(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 1, "TIF_FragQuest", "Fragment_End"})).has_value());
    auto readback = dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(readback.size() == 2);
    CHECK(readback.at(0).at("index") == 0);
    CHECK(readback.at(0).at("script") == "TIF_FragQuest");
    CHECK(readback.at(0).at("function") == "Fragment_Begin");
    CHECK(readback.at(1).at("index") == 1);
    CHECK(readback.at(1).at("function") == "Fragment_End");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted;
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()},
                                      {"source", "FragmentTests"}, {"resolved", true}};
    REQUIRE(restarted.invoke("environment.LoadMo2",
                             nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
    CHECK(dialogueOf(restarted, quest).at("topics").at(0).at("infos").at(0).at("fragments") == readback);

    REQUIRE(backend.invokeBackend("RemoveInfoFragment", nlohmann::json::array({"Patch.esp", info, 1}))
                .has_value());
    readback = dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(readback.size() == 1);
    CHECK(readback.at(0).at("function") == "Fragment_Begin");

    REQUIRE(backend.invokeBackend("RemoveInfoFragment", nlohmann::json::array({"Patch.esp", info, 0}))
                .has_value());
    CHECK(dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments").empty());
}

TEST_CASE("INFO fragment edits refuse bad indexes, flags and names without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragTopic"}}})));
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));
    const auto snapshot = dialogueOf(backend, quest);

    CHECK(errorType(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 2, "S", "F"}))) == "FragmentIndexOutOfRange");
    CHECK(errorType(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "", "F"}))) == "InvalidFragmentArgument");
    CHECK(errorType(backend.invokeBackend("RemoveInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0}))) == "FragmentMissing");
    CHECK(dialogueOf(backend, quest) == snapshot);
}

TEST_CASE("Scene phase fragments bind start and completion and read back per phase") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto scene = createdKey(backend.invokeBackend("CreateScene", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragScene"}}})));
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 1, nlohmann::json{{"name", "P2"}}})).has_value());

    REQUIRE(backend.invokeBackend("SetScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 1, 1, "QF_FragScene", "Fragment_Start"})).has_value());
    REQUIRE(backend.invokeBackend("SetScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 2, "QF_FragScene", "Fragment_Done"})).has_value());
    auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 2);
    CHECK(readback.at("phases").at(0).at("completionFragment").at("function") == "Fragment_Done");
    CHECK_FALSE(readback.at("phases").at(0).contains("startFragment"));
    CHECK(readback.at("phases").at(1).at("startFragment").at("script") == "QF_FragScene");
    CHECK(readback.at("phases").at(1).at("startFragment").at("function") == "Fragment_Start");

    REQUIRE(backend.invokeBackend("RemoveScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 1, 1})).has_value());
    readback = sceneOf(backend, scene);
    CHECK_FALSE(readback.at("phases").at(1).contains("startFragment"));
    CHECK(readback.at("phases").at(0).at("completionFragment").at("function") == "Fragment_Done");

    CHECK(errorType(backend.invokeBackend("SetScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 3, "S", "F"}))) == "InvalidFragmentArgument");
    CHECK(errorType(backend.invokeBackend("RemoveScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1}))) == "FragmentMissing");
    CHECK(errorType(backend.invokeBackend("RemoveScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 0}))) == "InvalidFragmentArgument");
}

TEST_CASE("INFO add refuses an absent section and a bound slot, then fills the free slot and round-trips") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragTopic"}}})));
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));

    const auto fresh = dialogueOf(backend, quest);
    CHECK(errorType(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Begin"}))) == "FragmentSectionMissing");
    CHECK(dialogueOf(backend, quest) == fresh);

    REQUIRE(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Begin"})).has_value());
    const auto bound = dialogueOf(backend, quest);
    CHECK(errorType(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Other"}))) == "FragmentAlreadyBound");
    CHECK(dialogueOf(backend, quest) == bound);

    REQUIRE(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 1, "TIF_FragQuest", "Fragment_End"})).has_value());
    const auto readback = dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(readback.size() == 2);
    CHECK(readback.at(0).at("index") == 0);
    CHECK(readback.at(0).at("function") == "Fragment_Begin");
    CHECK(readback.at(1).at("index") == 1);
    CHECK(readback.at(1).at("function") == "Fragment_End");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted;
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()},
                                      {"source", "FragmentTests"}, {"resolved", true}};
    REQUIRE(restarted.invoke("environment.LoadMo2",
                             nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
    CHECK(dialogueOf(restarted, quest).at("topics").at(0).at("infos").at(0).at("fragments") == readback);
}

TEST_CASE("Scene phase add refuses an absent section and a bound pair, then binds the other flag") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto scene = createdKey(backend.invokeBackend("CreateScene", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragScene"}}})));
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());

    const auto fresh = sceneOf(backend, scene);
    CHECK(errorType(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1, "QF_FragScene", "Fragment_Start"}))) == "FragmentSectionMissing");
    CHECK(sceneOf(backend, scene) == fresh);
    CHECK(errorType(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 3, "QF_FragScene", "Fragment_Start"}))) == "InvalidFragmentArgument");

    REQUIRE(backend.invokeBackend("SetScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1, "QF_FragScene", "Fragment_Start"})).has_value());
    const auto bound = sceneOf(backend, scene);
    CHECK(errorType(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1, "QF_FragScene", "Fragment_Other"}))) == "FragmentAlreadyBound");
    CHECK(sceneOf(backend, scene) == bound);

    REQUIRE(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 2, "QF_FragScene", "Fragment_Done"})).has_value());
    const auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 1);
    CHECK(readback.at("phases").at(0).at("startFragment").at("function") == "Fragment_Start");
    CHECK(readback.at("phases").at(0).at("completionFragment").at("function") == "Fragment_Done");
}

TEST_CASE("INFO add at index 0 inserts before a bound index 1 and reads back in order after reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragTopic"}}})));
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"responses", nlohmann::json::array({{{"text", "Hi"}}})}}})));

    REQUIRE(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 1, "TIF_FragQuest", "Fragment_End"})).has_value());
    auto before = dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(before.size() == 1);
    REQUIRE(before.at(0).at("index") == 1);

    REQUIRE(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Begin"})).has_value());
    auto readback = dialogueOf(backend, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(readback.size() == 2);
    CHECK(readback.at(0).at("index") == 0);
    CHECK(readback.at(0).at("script") == "TIF_FragQuest");
    CHECK(readback.at(0).at("function") == "Fragment_Begin");
    CHECK(readback.at(1).at("index") == 1);
    CHECK(readback.at(1).at("function") == "Fragment_End");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, root);
    CHECK(dialogueOf(restarted, quest).at("topics").at(0).at("infos").at(0).at("fragments") == readback);

    REQUIRE(restarted.invokeBackend("RemoveInfoFragment", nlohmann::json::array({"Patch.esp", info, 0}))
                .has_value());
    readback = dialogueOf(restarted, quest).at("topics").at(0).at("infos").at(0).at("fragments");
    REQUIRE(readback.size() == 1);
    CHECK(readback.at(0).at("index") == 1);
    CHECK(readback.at(0).at("function") == "Fragment_End");
}

TEST_CASE("INFO add refuses index 2 and non-fragment records and leaves the saved plugin unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto armor = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "ARMO", nlohmann::json{{"editorId", "FragArmor"}}})));
    const auto topic = createdKey(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragTopic"}}})));
    const auto info = createdKey(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));
    REQUIRE(backend.invokeBackend("SetInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 0, "TIF_FragQuest", "Fragment_Begin"})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fileBytes(root / "Patch.esp");
    const auto snapshot = dialogueOf(backend, quest);

    CHECK(errorType(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", info, 2, "TIF_FragQuest", "Fragment_Overflow"}))) == "FragmentIndexOutOfRange");
    CHECK(errorType(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", quest, 0, "TIF_FragQuest", "Fragment_Begin"}))) == "FragmentNotAuthorable");
    CHECK(errorType(backend.invokeBackend("AddInfoFragment", nlohmann::json::array({
        "Patch.esp", armor, 0, "TIF_FragQuest", "Fragment_Begin"}))) == "FragmentNotAuthorable");
    CHECK(errorType(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", info, 0, 1, "QF_FragScene", "Fragment_Start"}))) == "FragmentNotAuthorable");
    CHECK(dialogueOf(backend, quest) == snapshot);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fileBytes(root / "Patch.esp") == saved);
}

TEST_CASE("Scene phase add inserts before an existing later phase and reads back after reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = createdKey(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "FragQuest"}}})));
    const auto scene = createdKey(backend.invokeBackend("CreateScene", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "FragScene"}}})));
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 0, nlohmann::json{{"name", "P1"}}})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhase", nlohmann::json::array({
        "Patch.esp", scene, 1, nlohmann::json{{"name", "P2"}}})).has_value());

    REQUIRE(backend.invokeBackend("SetScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 1, 1, "QF_FragScene", "Fragment_Start1"})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1, "QF_FragScene", "Fragment_Start0"})).has_value());
    REQUIRE(backend.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 2, "QF_FragScene", "Fragment_Done0"})).has_value());

    auto readback = sceneOf(backend, scene);
    REQUIRE(readback.at("phases").size() == 2);
    CHECK(readback.at("phases").at(0).at("startFragment").at("function") == "Fragment_Start0");
    CHECK(readback.at("phases").at(0).at("completionFragment").at("function") == "Fragment_Done0");
    CHECK(readback.at("phases").at(1).at("startFragment").at("function") == "Fragment_Start1");
    CHECK_FALSE(readback.at("phases").at(1).contains("completionFragment"));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    fo4ide::plugin::RadiclePluginBackend restarted;
    loadPatch(restarted, root);
    CHECK(sceneOf(restarted, scene).at("phases") == readback.at("phases"));

    CHECK(errorType(restarted.invokeBackend("AddScenePhaseFragment", nlohmann::json::array({
        "Patch.esp", scene, 0, 1, "QF_FragScene", "Fragment_Other"}))) == "FragmentAlreadyBound");
    CHECK(sceneOf(restarted, scene).at("phases") == readback.at("phases"));
}
