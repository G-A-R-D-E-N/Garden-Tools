#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_formlink_list_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> pluginBytes(bool anchor = false) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(4);
    hedr.u32(0x804);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800, 131);
    fo4::test::end_record(out, color);
    color = fo4::test::begin_record(out, "CLFM", 0, 0x00000801, 131);
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto npcs = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000802, 131);
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, npcs);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);
    fo4::test::ByteWriter link;
    link.u32(0x00000800);
    if (anchor) {
        fo4::test::ByteWriter preset;
        preset.u32(0x00000802);
        fo4::test::write_subrecord(out, "RPRM", preset.bytes);
    }
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
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
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "FormLinkListTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

std::uint32_t nthRaw(const fo4::Record& record,
                     std::string_view signature,
                     std::size_t wanted) {
    const auto sig = fo4::Signature::from_string(signature);
    std::size_t ordinal = 0;
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != sig) continue;
        if (ordinal++ != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing requested subrecord ordinal");
    REQUIRE(found->data.size() == 4u);
    std::uint32_t raw = 0;
    std::memcpy(&raw, found->data.data(), sizeof(raw));
    return raw;
}

std::size_t countSubrecords(const fo4::Record& record,
                            std::string_view signature) {
    const auto sig = fo4::Signature::from_string(signature);
    std::size_t count = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == sig) ++count;
    }
    return count;
}

}

TEST_CASE("PluginEditTool edits generated indexed RACE FormLink list field") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp",
        "000803:Patch.esp",
        "RACE\\MaleHairColors[1]",
        "000801:Patch.esp"
    }));
    REQUIRE(result.has_value());

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveRace = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(beforeSaveRace != nullptr);
    CHECK(nthRaw(*beforeSaveRace, "AHCM", 0) == 0x00000800u);
    CHECK(nthRaw(*beforeSaveRace, "AHCM", 1) == 0x00000800u);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(nthRaw(*race, "AHCM", 0) == 0x00000800u);
    CHECK(nthRaw(*race, "AHCM", 1) == 0x00000801u);
}

TEST_CASE("PluginEditTool inserts generated FormLink list element between neighbors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "InsertFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\MaleHairColors",
            1,
            "000801:Patch.esp"
        }));
    REQUIRE(result.has_value());

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveRace = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(beforeSaveRace != nullptr);
    REQUIRE(countSubrecords(*beforeSaveRace, "AHCM") == 2u);
    CHECK(nthRaw(*beforeSaveRace, "AHCM", 0) == 0x00000800u);
    CHECK(nthRaw(*beforeSaveRace, "AHCM", 1) == 0x00000800u);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    REQUIRE(countSubrecords(*race, "AHCM") == 3u);
    CHECK(nthRaw(*race, "AHCM", 0) == 0x00000800u);
    CHECK(nthRaw(*race, "AHCM", 1) == 0x00000801u);
    CHECK(nthRaw(*race, "AHCM", 2) == 0x00000800u);
}

TEST_CASE("PluginEditTool removes generated FormLink list element") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\MaleHairColors",
            0
        }));
    REQUIRE(result.has_value());

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveRace = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(beforeSaveRace != nullptr);
    REQUIRE(countSubrecords(*beforeSaveRace, "AHCM") == 2u);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    REQUIRE(countSubrecords(*race, "AHCM") == 1u);
    CHECK(nthRaw(*race, "AHCM", 0) == 0x00000800u);
}

TEST_CASE("PluginEditTool rejects wrong FormLink target type without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp",
        "000803:Patch.esp",
        "RACE\\MaleHairColors[0]",
        "000802:Patch.esp"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkTargetTypeMismatch");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool SetField does not silently turn replacement into insertion") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp",
        "000803:Patch.esp",
        "RACE\\MaleHairColors[2]",
        "000801:Patch.esp"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkListIndexOutOfRange");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool refuses empty-list insertion without a schema anchor") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    REQUIRE(backend.invokeBackend(
        "RemoveFormLinkListElement",
        nlohmann::json::array({"Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors", 1})).has_value());
    REQUIRE(backend.invokeBackend(
        "RemoveFormLinkListElement",
        nlohmann::json::array({"Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors", 0})).has_value());
    const auto before = readBytes(path);

    const auto result = backend.invokeBackend(
        "InsertFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\MaleHairColors",
            0,
            "000801:Patch.esp"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkListInsertionBoundaryUnavailable");
    CHECK(readBytes(path) == before);
}

TEST_CASE("W0 grid projects generated list count and indexed values", "[w0]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend("GetRecordsGrid",
        nlohmann::json::array({"Patch.esp", "RACE", 200, 0}));
    REQUIRE(result.has_value());
    const auto grid = nlohmann::json::parse(result->get<std::string>());
    bool count = false;
    bool element = false;
    for (std::size_t index = 0; index < grid.at("columns").size(); ++index) {
        if (grid.at("columns")[index] == "RACE\\MaleHairColors\\Count") {
            count = true;
            CHECK(grid.at("rows")[0].at("cells")[index] == "2");
            CHECK(grid.at("rows")[0].at("editable")[index] == false);
        }
        if (grid.at("columns")[index] == "RACE\\MaleHairColors[1]") {
            element = true;
            CHECK(grid.at("rows")[0].at("cells")[index] == "000800:Patch.esp");
            CHECK(grid.at("rows")[0].at("editable")[index] == true);
        }
    }
    CHECK(count);
    CHECK(element);
}

TEST_CASE("W0 generated list count rows expose list actions", "[w0]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
    const auto result = elements.describeElement(nlohmann::json::array({
        "Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors\\Count"}));
    REQUIRE(result.has_value());
    const auto actions = nlohmann::json::parse(result->get<std::string>());
    REQUIRE(actions.contains("count"));
    CHECK(actions.at("count") == 2);
    CHECK(actions.at("canAdd") == true);
    CHECK(actions.at("canClear") == true);
    CHECK(actions.at("canRemove") == false);
}

TEST_CASE("W0 empty-list capability uses the native insertion anchor", "[w0]") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes(true));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    auto& elements = static_cast<fo4ide::plugin::IPluginBackend&>(backend);
    REQUIRE(elements.clearElement(nlohmann::json::array({
        "Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors"})).has_value());
    const auto result = elements.describeElement(nlohmann::json::array({
        "Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors"}));
    REQUIRE(result.has_value());
    const auto actions = nlohmann::json::parse(result->get<std::string>());
    CHECK(actions.at("count") == 0);
    CHECK(actions.at("canAdd") == true);
    REQUIRE(elements.addElement(nlohmann::json::array({
        "Patch.esp", "000803:Patch.esp", "RACE\\MaleHairColors\\Count", "FormLink<CLFM>"})).has_value());
    REQUIRE(backend.invokeBackend("SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(record != nullptr);
    REQUIRE(record->subrecords.size() == 2);
    CHECK(record->subrecords[0].signature == fo4::Signature::from_string("RPRM"));
    CHECK(record->subrecords[1].signature == fo4::Signature::from_string("AHCM"));
    CHECK(record->subrecords[1].data == std::vector<std::byte>(4, std::byte{0}));
}
