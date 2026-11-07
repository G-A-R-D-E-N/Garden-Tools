#include <cstring>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

constexpr std::string_view kNamedTypes[] = {
    "WEAP", "ARMO", "MISC", "ALCH", "BOOK", "KEYM", "AMMO", "NOTE", "CMPO", "INGR",
    "NPC_", "QUST", "CONT", "ACTI", "FURN", "DOOR", "LIGH", "SPEL", "PERK", "MGEF",
};

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_name_field_" + id);
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

std::vector<std::byte> headerPlugin(std::uint32_t tes4Flags) {
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

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    return bytes;
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    if (!result.has_value()) INFO(result.error().type + ": " + result.error().message);
    REQUIRE(result.has_value());
    if (result->is_string()) return nlohmann::json::parse(result->get<std::string>());
    return *result;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "NameField"}, {"resolved", true}}})}})
                .has_value());
}

void writeStringTables(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{100, rawBytes("Other")}}},
        directory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {{500, rawBytes("Desc")}}},
        directory / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, {{900, rawBytes("Dialog")}}},
        directory / "Patch_en.ILSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{3000, rawBytes("Andere")}}},
        directory / "Patch_de.STRINGS");
}

std::map<std::string, fo4::FormKey> createNamedRecords(fo4ide::plugin::RadiclePluginBackend& backend) {
    std::map<std::string, fo4::FormKey> keys;
    for (const auto type : kNamedTypes) {
        INFO("create " + std::string(type));
        const nlohmann::json payload{{"editorId", "Named" + std::string(type)}, {"schema", true}};
        const auto created = parseResult(backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"Patch.esp", std::string(type), payload})));
        keys.emplace(std::string(type), fo4::FormKey::parse(created.at("formKey").get<std::string>()));
    }
    return keys;
}

}  // namespace

// Detects: SetField on <TYPE>\Full in a plain plugin must write the inline name for every named record type, including records created by CreateRecord.
TEST_CASE("Native SetField Full names every named record type in a plain plugin", "[!shouldfail]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, headerPlugin(0u));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto keys = createNamedRecords(backend);

    for (const auto type : kNamedTypes) {
        INFO("set " + std::string(type) + "\\Full");
        const auto result = backend.invokeBackend(
            "SetField", nlohmann::json::array({"Patch.esp", keys.at(std::string(type)).to_string(),
                                               std::string(type) + "\\Full", "Name " + std::string(type)}));
        if (!result.has_value()) INFO(result.error().type + ": " + result.error().message);
        CHECK(result.has_value());
    }

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    for (const auto type : kNamedTypes) {
        INFO("read " + std::string(type));
        const auto* record = fo4::find_record(disk, keys.at(std::string(type)));
        REQUIRE(record != nullptr);
        bool found = false;
        for (const auto& sub : record->subrecords) {
            if (sub.signature != fo4::Signature::from_string("FULL")) continue;
            found = true;
            const std::string text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size() - 1);
            CHECK(text == "Name " + std::string(type));
        }
        CHECK(found);
    }
}

// Detects: SetField on <TYPE>\Full in a localized plugin must store the name as a new string in the plugin's STRINGS table, above every existing ID in every language, and persist it with save_plugin.
TEST_CASE("Native SetField Full names every named record type in a localized plugin", "[!shouldfail]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, headerPlugin(fo4::RecordFlags::localized));
    writeStringTables(root / "Strings");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    const auto keys = createNamedRecords(backend);

    for (const auto type : kNamedTypes) {
        INFO("set " + std::string(type) + "\\Full");
        const auto result = backend.invokeBackend(
            "SetField", nlohmann::json::array({"Patch.esp", keys.at(std::string(type)).to_string(),
                                               std::string(type) + "\\Full", "Name " + std::string(type)}));
        if (!result.has_value()) INFO(result.error().type + ": " + result.error().message);
        CHECK(result.has_value());
    }

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    for (const auto type : kNamedTypes) {
        INFO("read " + std::string(type));
        const auto* record = fo4::find_record(disk, keys.at(std::string(type)));
        REQUIRE(record != nullptr);
        for (const auto& sub : record->subrecords) {
            if (sub.signature != fo4::Signature::from_string("FULL")) continue;
            REQUIRE(sub.data.size() == 4u);
            std::uint32_t id = 0;
            std::memcpy(&id, sub.data.data(), sizeof(id));
            CHECK(id > 3000u);
            const auto* entry = fo4::localization::find_string(table, id);
            REQUIRE(entry != nullptr);
            CHECK(std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size()) ==
                  "Name " + std::string(type));
        }
    }
}
