#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/proj_expl_hazd.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_proj_flags_" + id);
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

std::vector<std::byte> pluginWithLight() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LIGH", 0);
    const auto record = fo4::test::begin_record(out, "LIGH", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

const fo4::Subrecord* findSub(const fo4::Record& record, std::string_view signature) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == signature) return &sub;
    }
    return nullptr;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ProjFlags"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

void savePlugin(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

}

TEST_CASE("Native PROJ create stores a UTF-8 EditorID as Windows-1252") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithLight());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    REQUIRE(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "Caf\xC3\xA9"}}
        })).has_value());
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000801:Patch.esp"));
    REQUIRE(record != nullptr);
    const auto* edid = findSub(*record, "EDID");
    REQUIRE(edid != nullptr);
    CHECK(edid->data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}});
}

TEST_CASE("Native PROJ Flags SetField writes its u16 and leaves the embedded Type bytes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithLight());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "FlagsNeighbour"}}
        }));
    REQUIRE(created.has_value());
    savePlugin(backend);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto before = fo4::read_plugin_file(path);
    const auto* beforeRecord = fo4::find_record(before, key);
    REQUIRE(beforeRecord != nullptr);
    const auto* beforeDnam = findSub(*beforeRecord, "DNAM");
    REQUIRE(beforeDnam != nullptr);
    REQUIRE(beforeDnam->data.size() == 93);
    REQUIRE(beforeDnam->data[2] == std::byte{0x01});

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key.to_string(), "PROJ\\Flags", "5"
    })).has_value());
    savePlugin(backend);

    const auto after = fo4::read_plugin_file(path);
    const auto* afterRecord = fo4::find_record(after, key);
    REQUIRE(afterRecord != nullptr);
    const auto* afterDnam = findSub(*afterRecord, "DNAM");
    REQUIRE(afterDnam != nullptr);
    CHECK(afterDnam->data[0] == std::byte{0x05});
    CHECK(afterDnam->data[1] == std::byte{0x00});
    for (std::size_t index = 2; index < beforeDnam->data.size(); ++index) {
        CHECK(afterDnam->data[index] == beforeDnam->data[index]);
    }
}

TEST_CASE("Native PROJ creation writes Type as its own u16 and reads it back") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithLight());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "TypeLobber"}, {"type", 2}}
        }));
    REQUIRE(created.has_value());
    savePlugin(backend);

    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    const auto* dnam = findSub(*record, "DNAM");
    REQUIRE(dnam != nullptr);
    CHECK(dnam->data[0] == std::byte{0x00});
    CHECK(dnam->data[1] == std::byte{0x00});
    CHECK(dnam->data[2] == std::byte{0x02});
    CHECK(dnam->data[3] == std::byte{0x00});
    CHECK(fo4::forms::decode_projectile(plugin, key).type == 2);
}
