#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    return std::filesystem::temp_directory_path() / ("fo4ide_bulk_" + std::to_string(id));
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

std::string errorType(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(!result.has_value());
    return result.error().type;
}

void openPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& root,
               std::uint32_t tes4Flags = 0) {
    writeBytes(root / "Patch.esp", emptyPlugin(tes4Flags));
    const auto entry = nlohmann::json{{"name", "Patch.esp"}, {"path", (root / "Patch.esp").string()},
                                      {"source", "BulkTests"}, {"resolved", true}};
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({entry})}})
                .has_value());
}

std::string freshQuest(fo4ide::plugin::RadiclePluginBackend& backend) {
    return parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "QUST", nlohmann::json{{"editorId", "BulkQuest"}}}))).at("formKey").get<std::string>();
}

nlohmann::json dialogueOf(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& quest) {
    return parseResult(backend.invokeBackend("GetDialogue", nlohmann::json::array({"Patch.esp", quest})));
}

}

TEST_CASE("Bulk topic creation splits long lines at sentence boundaries into numbered responses") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto created = parseResult(backend.invokeBackend("CreateDialogueTopicBulk", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{
            {"editorId", "BulkTopic"}, {"lines", nlohmann::json::array({
                "Short line.",
                "This is a longer line that we will split at the sentence end. Another sentence here."})},
            {"maxChars", 40}}})));
    REQUIRE(created.at("infos").size() == 2);
    const auto& first = created.at("infos").at(0).at("responses");
    REQUIRE(first.size() == 1);
    CHECK(first.at(0) == "Short line.");
    const auto& second = created.at("infos").at(1).at("responses");
    REQUIRE(second.size() == 3);
    CHECK(second.at(0) == "This is a longer line that we will");
    CHECK(second.at(1) == "split at the sentence end.");
    CHECK(second.at(2) == "Another sentence here.");

    const auto dialogue = dialogueOf(backend, quest);
    REQUIRE(dialogue.at("topics").size() == 1);
    CHECK(dialogue.at("topics").at(0).at("formKey") == created.at("formKey"));
    const auto infos = dialogue.at("topics").at(0).at("infos");
    REQUIRE(infos.size() == 2);
    REQUIRE(infos.at(1).at("responses").size() == 3);
    CHECK(infos.at(1).at("responses").at(0).at("text") == "This is a longer line that we will");
    CHECK(infos.at(1).at("responses").at(0).at("number") == 1);
    CHECK(infos.at(1).at("responses").at(2).at("number") == 3);
}

TEST_CASE("Bulk topic creation refuses bad input and localized plugins without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    CHECK(errorType(backend.invokeBackend("CreateDialogueTopicBulk", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"lines", nlohmann::json::array()}}}))) ==
          "InvalidDialogueArgument");
    CHECK(errorType(backend.invokeBackend("CreateDialogueTopicBulk", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"lines", nlohmann::json::array({"Hi"})},
                                           {"maxChars", 0}}}))) == "InvalidDialogueArgument");
    CHECK(errorType(backend.invokeBackend("CreateDialogueTopicBulk", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"lines", nlohmann::json::array({"  "})}}}))) ==
          "InvalidDialogueArgument");
    CHECK(dialogueOf(backend, quest).at("topics").empty());

    const auto localizedRoot = root / "localized";
    fo4ide::plugin::RadiclePluginBackend localized;
    openPatch(localized, localizedRoot, 0x80u);
    const auto localizedQuest = freshQuest(localized);
    CHECK(errorType(localized.invokeBackend("CreateDialogueTopicBulk", nlohmann::json::array({
        "Patch.esp", localizedQuest, nlohmann::json{{"lines", nlohmann::json::array({"Hi"})}}}))) ==
          "LocalizedDialogueUnsupported");
}

TEST_CASE("Voice path helper names every stored response of an INFO") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    openPatch(backend, root);
    const auto quest = freshQuest(backend);
    const auto topic = parseResult(backend.invokeBackend("CreateDialogueTopic", nlohmann::json::array({
        "Patch.esp", quest, nlohmann::json{{"editorId", "VoiceTopic"}}}))).at("formKey").get<std::string>();
    const auto info = parseResult(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json{{"responses", nlohmann::json::array({
            nlohmann::json{{"text", "First"}, {"number", 1}}, nlohmann::json{{"text", "Second"}, {"number", 2}}})}}})))
            .at("formKey").get<std::string>();

    const auto paths = parseResult(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", info, "MaleEvenToned"})));
    CHECK(paths.at("plugin") == "Patch.esp");
    CHECK(paths.at("voicetype") == "MaleEvenToned");
    REQUIRE(paths.at("paths").size() == 2);
    const auto local = std::string(info.begin(), info.begin() + info.find(':'));
    CHECK(paths.at("paths").at(0).get<std::string>().starts_with("Sound\\Voice\\Patch.esp\\MaleEvenToned\\"));
    CHECK(paths.at("paths").at(0).get<std::string>().ends_with("_1.fuz"));
    CHECK(paths.at("paths").at(1).get<std::string>().ends_with("_2.fuz"));
    CHECK(paths.at("paths").at(0).get<std::string>().find(local) != std::string::npos);

    const auto withLanguage = parseResult(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", info, "MaleEvenToned", "English"})));
    CHECK(withLanguage.at("paths").at(0).get<std::string>().find("\\MaleEvenToned\\English\\") !=
          std::string::npos);

    CHECK(errorType(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", topic, "MaleEvenToned"}))) == "RecordTypeMismatch");
    CHECK(errorType(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", info, ""}))) == "InvalidVoicePathArgument");
    CHECK(errorType(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", info, "MaleEvenToned", "", "wav"}))) == "InvalidVoicePathArgument");
    const auto noResponses = parseResult(backend.invokeBackend("CreateInfo", nlohmann::json::array({
        "Patch.esp", topic, nlohmann::json::object()})));
    const auto emptyPaths = parseResult(backend.invokeBackend("GetVoicePath", nlohmann::json::array({
        "Patch.esp", noResponses.at("formKey").get<std::string>(), "MaleEvenToned"})));
    CHECK(emptyPaths.at("paths").empty());
}
