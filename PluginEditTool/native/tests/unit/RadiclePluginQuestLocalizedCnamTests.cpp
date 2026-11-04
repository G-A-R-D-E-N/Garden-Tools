#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_quest_localized_cnam_" + std::to_string(id));
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

void writeU32(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

void writeLocalizationSet(const std::filesystem::path& root,
                           std::vector<fo4::localization::LocalizedStringEntry> strings,
                           std::vector<fo4::localization::LocalizedStringEntry> dlstrings,
                           std::vector<fo4::localization::LocalizedStringEntry> ilstrings,
                           std::string_view language = "en") {
    const auto directory = root / "Strings";
    std::filesystem::create_directories(directory);
    const auto stem = "Patch_" + std::string(language);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, std::move(strings)},
        directory / (stem + ".STRINGS"));
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, std::move(dlstrings)},
        directory / (stem + ".DLSTRINGS"));
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, std::move(ilstrings)},
        directory / (stem + ".ILSTRINGS"));
}

nlohmann::json localizedQuestContext(const std::filesystem::path& root,
                                    std::string_view language = "en",
                                    const std::filesystem::path& directory = {}) {
    return nlohmann::json{
        {"language", language},
        {"stringsDirectory", (directory.empty() ? root / "Strings" : directory).string()}
    };
}

std::vector<std::byte> questPlugin(std::uint32_t tes4Flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    writeU32(out, "ANAM", 2);
    writeU32(out, "ALST", 1);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Existing\0", 9});
    writeU32(out, "FNAM", 0x10);
    fo4::test::write_subrecord(out, "ALED", std::vector<std::byte>{});
    fo4::test::write_subrecord(out, "NNAM", std::string_view{"Tail\0", 5});
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeIndx(fo4::test::ByteWriter& out,
               std::uint16_t index,
               std::uint8_t flags,
               std::uint8_t unknown) {
    fo4::test::ByteWriter data;
    data.u16(index);
    data.u8(flags);
    data.u8(unknown);
    fo4::test::write_subrecord(out, "INDX", data.bytes);
}

template <typename Body>
std::vector<std::byte> questPluginWith(std::uint32_t tes4Flags, Body&& body) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AliasQuest\0", 11});
    body(out);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "QuestTest"},
            {"resolved", true}
        }})}}).has_value());
}

}

TEST_CASE("Native SetQuestStages allocates changed localized CNAM from the full ID set") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPluginWith(fo4::RecordFlags::localized, [](fo4::test::ByteWriter& out) {
        writeIndx(out, 10, 0, 0);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        writeU32(out, "CNAM", 42);
    }));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {{42, rawBytes("Existing")}},
        {{200, rawBytes("Other")}});
    const auto stringsBefore = readBytes(root / "Strings" / "Patch_en.STRINGS");
    const auto ilstringsBefore = readBytes(root / "Strings" / "Patch_en.ILSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "Changed"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_quest_stages(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(decoded.stages.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries.size() == 1u);
    REQUIRE(decoded.stages[0].log_entries[0].log_text.has_value());
    CHECK(decoded.stages[0].log_entries[0].log_text->id == 201u);

    const auto dlstrings = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    REQUIRE(dlstrings.entries.size() == 2u);
    CHECK(fo4::localization::find_string(dlstrings, 42u) != nullptr);
    CHECK(fo4::localization::find_string(dlstrings, 201u) != nullptr);
    CHECK(readBytes(root / "Strings" / "Patch_en.STRINGS") == stringsBefore);
    CHECK(readBytes(root / "Strings" / "Patch_en.ILSTRINGS") == ilstringsBefore);
}

TEST_CASE("Native SetQuestStages writes English CNAM text as CP1252") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}});
    const auto cafe = std::string{"Caf"} + "\xc3\xa9";

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", cafe}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    const auto* entry = fo4::localization::find_string(table, 201u);
    REQUIRE(entry != nullptr);
    CHECK(entry->text == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xe9}});
}

TEST_CASE("Native SetQuestStages writes supported non-English CNAM text as UTF-8") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}},
        "de");
    const auto text = std::string{"Gr"} + "\xc3\xbc" + "\xc3\x9f" + "e";

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", text}}}),
            localizedQuestContext(root, "de")
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_de.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    const auto* entry = fo4::localization::find_string(table, 201u);
    REQUIRE(entry != nullptr);
    CHECK(entry->text == rawBytes(text));
}

TEST_CASE("Native SetQuestStages allocates a brand-new localized CNAM") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, questPlugin(fo4::RecordFlags::localized));
    writeLocalizationSet(
        root,
        {{100, rawBytes("Other")}},
        {},
        {{200, rawBytes("Other")}});

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    backend.testSetJournalRoot(root / "journal");
    const auto result = backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}, {"logEntry", "New"}}}),
            localizedQuestContext(root)
        }));
    REQUIRE(result.has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS",
        fo4::localization::StringTableKind::dlstrings);
    const auto* entry = fo4::localization::find_string(table, 201u);
    REQUIRE(entry != nullptr);
    CHECK(entry->text == rawBytes("New"));
}
