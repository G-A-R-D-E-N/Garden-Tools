#include <cstring>
#include "../helpers/TempRoot.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

using Bytes = std::vector<std::byte>;
using Pieces = std::vector<std::pair<std::string, Bytes>>;

constexpr std::string_view k_mesgKey = "000800:Patch.esp";
constexpr std::string_view k_miscKey = "000801:Patch.esp";

std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("fo4ide_message_buttons_" + fo4ide_test::uniqueTempName());
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

Bytes rawBytes(std::string_view text) {
    Bytes bytes;
    for (const auto ch : text) bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    return bytes;
}

Bytes zbytes(std::string_view text) {
    auto bytes = rawBytes(text);
    bytes.push_back(std::byte{0});
    return bytes;
}

Bytes u32Bytes(std::uint32_t value) {
    fo4::test::ByteWriter out;
    out.u32(value);
    return out.bytes;
}

Bytes pluginBytes(std::uint32_t tes4Flags, const Pieces& mesg) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto mesgGroup = fo4::test::begin_group(out, "MESG", 0);
    const auto mesgRecord = fo4::test::begin_record(out, "MESG", 0, 0x00000800, 131);
    for (const auto& [name, data] : mesg) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, mesgRecord);
    fo4::test::end_group(out, mesgGroup);

    const auto miscGroup = fo4::test::begin_group(out, "MISC", 0);
    const auto miscRecord = fo4::test::begin_record(out, "MISC", 0, 0x00000801, 131);
    fo4::test::end_record(out, miscRecord);
    fo4::test::end_group(out, miscGroup);
    return out.bytes;
}

Pieces oldMessage(std::uint32_t flags, bool localized = false) {
    return {{"EDID", zbytes("TestMessage")},
            {"DESC", localized ? u32Bytes(50) : zbytes("Body")},
            {"INAM", Bytes(4)},
            {"DNAM", u32Bytes(flags)},
            {"ITXT", localized ? u32Bytes(60) : zbytes("Old")},
            {"CTDA", Bytes(32)},
            {"ITXT", localized ? u32Bytes(61) : zbytes("Older")}};
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"},
                                                            {"path", path.string()},
                                                            {"source", "MessageButtonsTest"},
                                                            {"resolved", true}}})}});
    REQUIRE(result.has_value());
}

void save(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

std::vector<std::string> signaturesOf(const std::filesystem::path& path, std::string_view key) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    std::vector<std::string> out;
    for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
    return out;
}

std::vector<Bytes> payloadsOf(const std::filesystem::path& path, std::string_view signature) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(k_mesgKey));
    REQUIRE(record != nullptr);
    std::vector<Bytes> out;
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature.to_string() == signature) out.push_back(subrecord.data);
    }
    return out;
}

std::uint32_t wordOf(const Bytes& data) {
    REQUIRE(data.size() >= 4);
    std::uint32_t value = 0;
    std::memcpy(&value, data.data(), sizeof(value));
    return value;
}

fo4ide::Result<nlohmann::json> setButtons(fo4ide::plugin::RadiclePluginBackend& backend, std::string_view record,
                                          const nlohmann::json& buttons) {
    fo4ide::plugin::IPluginBackend& typed = backend;
    return typed.setMessageButtons(nlohmann::json::array({"Patch.esp", std::string(record), buttons}));
}

std::string errorType(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().type;
}

}  // namespace

// Detects: set_message_buttons must replace the ITXT rows and their conditions, OR the MessageBox bit into DNAM, and reach disk only through save.
TEST_CASE("Native set_message_buttons writes ITXT rows and the MessageBox bit with a saved round trip") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes(0u, oldMessage(0x2u)));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto written = setButtons(backend, k_mesgKey, nlohmann::json::array({"Use", {{"text", "Cancel"}}}).dump());
    REQUIRE(written.has_value());
    CHECK(signaturesOf(path, k_mesgKey) == std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM", "ITXT", "CTDA", "ITXT"});

    save(backend);
    CHECK(signaturesOf(path, k_mesgKey) == std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM", "ITXT", "ITXT"});
    CHECK(payloadsOf(path, "ITXT") == std::vector<Bytes>{zbytes("Use"), zbytes("Cancel")});
    CHECK(wordOf(payloadsOf(path, "DNAM").at(0)) == 0x3u);

    const auto parsed = setButtons(backend, k_mesgKey, nlohmann::json::array({"Only"}));
    REQUIRE(parsed.has_value());
    save(backend);
    CHECK(payloadsOf(path, "ITXT") == std::vector<Bytes>{zbytes("Only")});

    const auto cleared = setButtons(backend, k_mesgKey, nlohmann::json(nullptr));
    REQUIRE(cleared.has_value());
    save(backend);
    CHECK(signaturesOf(path, k_mesgKey) == std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM"});
    CHECK(wordOf(payloadsOf(path, "DNAM").at(0)) == 0x3u);
}

// Detects: in a localized plugin every button text must become a new string in the staged STRINGS table, with the existing entries kept.
TEST_CASE("Native set_message_buttons stores button text as new strings in a localized plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes(fo4::RecordFlags::localized, oldMessage(0, true)));
    std::filesystem::create_directories(root / "Strings");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{60, rawBytes("Old")}, {61, rawBytes("Older")}, {100, rawBytes("Other")}}},
        root / "Strings" / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {{50, rawBytes("Desc")}}}, root / "Strings" / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, {{900, rawBytes("Dialog")}}}, root / "Strings" / "Patch_en.ILSTRINGS");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    load(backend, path);
    const auto written = setButtons(backend, k_mesgKey, nlohmann::json::array({"Use", "Cancel"}));
    INFO((written.has_value() ? std::string{} : written.error().type + ": " + written.error().message));
    REQUIRE(written.has_value());
    save(backend);

    const auto itxt = payloadsOf(path, "ITXT");
    REQUIRE(itxt.size() == 2u);
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const std::vector<std::string> expected{"Use", "Cancel"};
    for (std::size_t index = 0; index < itxt.size(); ++index) {
        REQUIRE(itxt[index].size() == 4u);
        const auto id = wordOf(itxt[index]);
        CHECK(id > 100u);
        const auto* entry = fo4::localization::find_string(table, id);
        REQUIRE(entry != nullptr);
        CHECK(std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size()) == expected[index]);
    }
    CHECK(wordOf(itxt[0]) != wordOf(itxt[1]));
    CHECK(fo4::localization::find_string(table, 100) != nullptr);
    CHECK(wordOf(payloadsOf(path, "DNAM").at(0)) == 0x1u);
}

// Detects: invalid input and a non-MESG record must be refused with the plugin left as it was.
TEST_CASE("Native set_message_buttons refuses invalid input and other record types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes(0u, oldMessage(0x2u)));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    CHECK(errorType(setButtons(backend, k_mesgKey, "not json")) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::object())) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({1}))) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({""}))) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({nlohmann::json::object()}))) ==
          "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({{{"text", 5}}}))) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({std::string("a\0b", 3)}))) ==
          "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array({"\xE4\xB8\xAD"}))) == "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_mesgKey, nlohmann::json::array_t(65536, nlohmann::json("x")))) ==
          "InvalidMessageButtons");
    CHECK(errorType(setButtons(backend, k_miscKey, nlohmann::json::array({"Use"}))) == "WrongRecordType");
    CHECK(errorType(setButtons(backend, "000999:Patch.esp", nlohmann::json::array({"Use"}))) == "RecordNotFound");

    save(backend);
    CHECK(signaturesOf(path, k_mesgKey) == std::vector<std::string>{"EDID", "DESC", "INAM", "DNAM", "ITXT", "CTDA", "ITXT"});
    CHECK(wordOf(payloadsOf(path, "DNAM").at(0)) == 0x2u);
}
