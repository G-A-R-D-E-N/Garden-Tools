#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_rich_creation_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

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

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"},
                                                           {"path", path.string()},
                                                           {"source", "RichCreationTest"},
                                                           {"resolved", true}}})}});
    REQUIRE(result.has_value());
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

const fo4::Subrecord* findSubrecord(const fo4::Plugin& plugin,
                                    const fo4::FormKey& key,
                                    std::string_view signature) {
    const auto* record = fo4::find_record(plugin, key);
    if (record == nullptr) return nullptr;
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& sub : record->subrecords) {
        if (sub.signature == wanted) return &sub;
    }
    return nullptr;
}

std::string editorIdOf(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    const auto* sub = findSubrecord(plugin, key, "EDID");
    if (sub == nullptr || sub->data.empty() || sub->data.back() != std::byte{0}) return {};
    return std::string(reinterpret_cast<const char*>(sub->data.data()), sub->data.size() - 1);
}

std::string formKeyText(std::uint32_t local) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%06X:Patch.esp", local);
    return buffer;
}

}  // namespace

TEST_CASE("PluginEditTool creates the schema-defaulted rich records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    struct Case {
        const char* signature;
        const char* editorId;
        const char* type;
    };
    const Case cases[] = {
        {"QUST", "GardenQuest", nullptr},
        {"GLOB", "GardenGlobal", "short"},
        {"SPEL", "GardenSpell", nullptr},
        {"MGEF", "GardenMagicEffect", nullptr},
        {"FLST", "GardenFormList", nullptr},
        {"MESG", "GardenMessage", nullptr},
    };

    std::vector<fo4::FormKey> keys;
    for (std::size_t index = 0; index < std::size(cases); ++index) {
        nlohmann::json payload{{"editorId", cases[index].editorId}};
        if (cases[index].type != nullptr) payload["type"] = cases[index].type;
        const auto created = parseResult(backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"Patch.esp", cases[index].signature, payload})));
        CHECK(created.at("formKey") == formKeyText(0x800 + static_cast<std::uint32_t>(index)));
        keys.push_back(fo4::FormKey::parse(created.at("formKey").get<std::string>()));
    }

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    for (std::size_t index = 0; index < std::size(cases); ++index) {
        const auto* record = fo4::find_record(disk, keys[index]);
        REQUIRE(record != nullptr);
        CHECK(record->header.signature == fo4::Signature::from_string(cases[index].signature));
        CHECK(editorIdOf(disk, keys[index]) == cases[index].editorId);
    }

    const auto* global = fo4::find_record(disk, keys[1]);
    REQUIRE(global != nullptr);
    const auto* type = findSubrecord(disk, keys[1], "FNAM");
    REQUIRE(type != nullptr);
    REQUIRE(type->data.size() == 1u);
    CHECK(type->data[0] == std::byte{115});  // GLOB Type: Short
    const auto* value = findSubrecord(disk, keys[1], "FLTV");
    REQUIRE(value != nullptr);
    CHECK(value->data.size() == 4u);
}

TEST_CASE("PluginEditTool refuses rich record payloads it cannot prove") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto missingEditor = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "QUST", nlohmann::json::object()}));
    REQUIRE_FALSE(missingEditor.has_value());
    CHECK(missingEditor.error().type == "InvalidRecordCreationPayload");

    const auto globNoType = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "GLOB", {{"editorId", "NoType"}}}));
    REQUIRE_FALSE(globNoType.has_value());
    CHECK(globNoType.error().type == "InvalidRecordCreationPayload");

    const auto globBadType = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "GLOB", {{"editorId", "BadType"}, {"type", "quad"}}}));
    REQUIRE_FALSE(globBadType.has_value());
    CHECK(globBadType.error().type == "InvalidRecordCreationPayload");

    const auto unsupported = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "MISC", {{"editorId", "StillRefused"}}}));
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().type == "RecordCreationRequiresSchema");

    CHECK(readBytes(path) == before);
}
