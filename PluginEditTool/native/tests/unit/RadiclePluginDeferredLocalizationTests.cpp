#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_deferred_localization_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

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

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

void writeU32(fo4::test::ByteWriter& out,
              std::string_view signature,
              std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(
        out, "TES4", fo4::RecordFlags::localized, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto refrGroup = fo4::test::begin_group(out, "REFR", 0);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
    writeU32(out, "XHLT", 50);
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, refrGroup);

    const auto questGroup = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeferredQuest\0", 14});
    fo4::test::ByteWriter indx;
    indx.u16(10);
    indx.u8(0);
    indx.u8(0);
    fo4::test::write_subrecord(out, "INDX", indx.bytes);
    fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
    writeU32(out, "CNAM", 42);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, questGroup);
    return out.bytes;
}

void writeLocalizationSet(const std::filesystem::path& root) {
    const auto directory = root / "Strings";
    std::filesystem::create_directories(directory);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings,
         {{100, rawBytes("Other")}}},
        directory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings,
         {{42, rawBytes("Original")}}},
        directory / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings,
         {{200, rawBytes("Other")}}},
        directory / "Patch_en.ILSTRINGS");
}

std::uint32_t healthValue(const std::filesystem::path& path) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        plugin, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() != "XHLT") continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("XHLT missing");
    REQUIRE(found->data.size() == 4u);
    std::uint32_t value = 0;
    std::memcpy(&value, found->data.data(), sizeof(value));
    return value;
}

}

TEST_CASE("Localized quest persistence stages alongside unrelated pending edits") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    writeLocalizationSet(root);
    const auto pluginBefore = readBytes(path);
    const auto dlstringsPath = root / "Strings" / "Patch_en.DLSTRINGS";
    const auto dlstringsBefore = readBytes(dlstringsPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "DeferredLocalization"},
            {"resolved", true}
        }})}}).has_value());

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "REFR\\HealthPercent", "75"
    })).has_value());
    CHECK(readBytes(path) == pluginBefore);

    const auto localized = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            nlohmann::json{
                {"language", "en"},
                {"stringsDirectory", (root / "Strings").string()}
            }
        }));
    REQUIRE(localized.has_value());
    CHECK(localized->get<std::string>().find("SavePlugin") != std::string::npos);
    CHECK(readBytes(path) == pluginBefore);
    CHECK(readBytes(dlstringsPath) == dlstringsBefore);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(healthValue(path) == 75u);
    const auto table = fo4::localization::read_string_table_file(
        dlstringsPath, fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, 42u) != nullptr);
    REQUIRE(fo4::localization::find_string(table, 201u) != nullptr);
}

TEST_CASE("A string edit is kept alongside a pending localized quest edit (regression: a SaveStringTables after SavePlugin overwrote the quest log IDs with the older DLSTRINGS table)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    writeLocalizationSet(root);
    const auto dlstringsPath = root / "Strings" / "Patch_en.DLSTRINGS";
    const auto dlstringsBefore = readBytes(dlstringsPath);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "DeferredLocalization"},
            {"resolved", true}
        }})}}).has_value());
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            nlohmann::json{
                {"language", "en"},
                {"stringsDirectory", (root / "Strings").string()}
            }
        })).has_value());

    REQUIRE(backend.invoke(
        "strings.SetString", nlohmann::json::array({"Patch.esp", "", "DLSTRINGS", 42, "Edited"})).has_value());
    CHECK(readBytes(dlstringsPath) == dlstringsBefore);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        dlstringsPath, fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, 201u) != nullptr);
    REQUIRE(fo4::localization::find_string(table, 42u) != nullptr);
    CHECK(fo4::localization::find_string(table, 42u)->text == rawBytes("Edited"));
}

TEST_CASE("A localized quest edit is kept alongside a pending string edit (regression: a quest table built from disk dropped the pending string edit when SaveStringTables wrote DLSTRINGS)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    writeLocalizationSet(root);
    const auto dlstringsPath = root / "Strings" / "Patch_en.DLSTRINGS";

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "DeferredLocalization"},
            {"resolved", true}
        }})}}).has_value());
    REQUIRE(backend.invoke(
        "strings.SetString", nlohmann::json::array({"Patch.esp", "", "DLSTRINGS", 42, "Edited"})).has_value());
    const auto dlstringsBefore = readBytes(dlstringsPath);

    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000801:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            nlohmann::json{
                {"language", "en"},
                {"stringsDirectory", (root / "Strings").string()}
            }
        })).has_value());
    CHECK(readBytes(dlstringsPath) == dlstringsBefore);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(backend.invoke("strings.SaveStringTables", nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        dlstringsPath, fo4::localization::StringTableKind::dlstrings);
    REQUIRE(fo4::localization::find_string(table, 42u) != nullptr);
    CHECK(fo4::localization::find_string(table, 42u)->text == rawBytes("Edited"));
    REQUIRE(fo4::localization::find_string(table, 201u) != nullptr);
}
