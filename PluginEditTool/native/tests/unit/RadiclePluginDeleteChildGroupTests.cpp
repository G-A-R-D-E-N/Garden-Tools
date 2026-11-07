#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/flags.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_delete_children_" + id);
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

void writeRecord(fo4::test::ByteWriter& out,
                 std::string_view signature,
                 std::uint32_t recordId,
                 std::string_view editorId) {
    const auto record = fo4::test::begin_record(out, signature, 0, recordId);
    std::string edid(editorId);
    edid.push_back('\0');
    fo4::test::write_subrecord(out, "EDID", std::string_view{edid});
    fo4::test::end_record(out, record);
}

std::vector<std::byte> makeTopicPlugin(bool referenced) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x805);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto dials = fo4::test::begin_group(out, "DIAL", 0);
    writeRecord(out, "DIAL", 0x800, "TopicA");
    writeRecord(out, "DIAL", 0x801, "TopicB");
    const auto first = fo4::test::begin_group(out, "DIAL", 7);
    out.patch_u32(first + 8, 0x801);
    writeRecord(out, "INFO", 0x802, "InfoB");
    fo4::test::end_group(out, first);
    writeRecord(out, "DIAL", 0x803, "TopicC");
    const auto second = fo4::test::begin_group(out, "DIAL", 7);
    out.patch_u32(second + 8, 0x803);
    const auto info = fo4::test::begin_record(out, "INFO", 0, 0x804);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"InfoC\0", 6});
    if (referenced) {
        fo4::test::ByteWriter pnam;
        pnam.u32(0x802);
        fo4::test::write_subrecord(out, "PNAM", pnam.bytes);
    }
    fo4::test::end_record(out, info);
    fo4::test::end_group(out, second);
    fo4::test::end_group(out, dials);
    return out.bytes;
}

std::vector<std::byte> makeLinkingPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Topic.esp\0", 10});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto dials = fo4::test::begin_group(out, "DIAL", 0);
    writeRecord(out, "DIAL", 0x01000800, "LinkTopic");
    const auto children = fo4::test::begin_group(out, "DIAL", 7);
    out.patch_u32(children + 8, 0x01000800);
    const auto info = fo4::test::begin_record(out, "INFO", 0, 0x01000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"LinkInfo\0", 9});
    fo4::test::ByteWriter pnam;
    pnam.u32(0x00000802);
    fo4::test::write_subrecord(out, "PNAM", pnam.bytes);
    fo4::test::end_record(out, info);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, dials);
    return out.bytes;
}

std::vector<std::byte> makeOverridePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Topic.esp\0", 10});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto dials = fo4::test::begin_group(out, "DIAL", 0);
    const auto children = fo4::test::begin_group(out, "DIAL", 7);
    out.patch_u32(children + 8, 0x801);
    writeRecord(out, "INFO", 0x00000802, "InfoBOverride");
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, dials);
    return out.bytes;
}

std::vector<std::byte> makeMasterCellPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x901);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto cells = fo4::test::begin_group(out, "CELL", 0);
    writeRecord(out, "CELL", 0x800, "BaseCell");
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    out.patch_u32(children + 8, 0x800);
    writeRecord(out, "REFR", 0x900, "BaseRef");
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, cells);
    return out.bytes;
}

std::vector<std::byte> makeMasterOverrideCellPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto cells = fo4::test::begin_group(out, "CELL", 0);
    writeRecord(out, "CELL", 0x01000800, "PatchCell");
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    out.patch_u32(children + 8, 0x01000800);
    writeRecord(out, "REFR", 0x00000900, "OverrideRef");
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, cells);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name,
                           const std::filesystem::path& path,
                           std::string source) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", std::move(source)},
        {"resolved", true}
    };
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native delete removes a DIAL topic children group on save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "TopicMod" / "Patch.esp";
    writeBytes(pluginPath, makeTopicPlugin(false));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, "TopicMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto deleted = parseStringResult(backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Patch.esp", "000801:Patch.esp"})));
    CHECK(deleted.at("deleted") == "000801:Patch.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(pluginPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000801:Patch.esp")) == nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000802:Patch.esp")) == nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000804:Patch.esp")) != nullptr);
    REQUIRE(disk.groups.size() == 1u);
    REQUIRE(disk.groups[0].children.size() == 3u);
    CHECK(std::holds_alternative<fo4::Record>(disk.groups[0].children[1]));
    CHECK(fo4::collect_groups(disk).size() == 2u);
}

TEST_CASE("Native delete refuses a DIAL whose topic child is referenced in plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "TopicMod" / "Patch.esp";
    writeBytes(pluginPath, makeTopicPlugin(true));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, "TopicMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto refused = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Patch.esp", "000801:Patch.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RecordReferenced");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(pluginPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000801:Patch.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000802:Patch.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000804:Patch.esp")) != nullptr);
}

TEST_CASE("Native delete refuses a DIAL whose topic child is linked from another loaded plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto topicPath = root / "mods" / "TopicMod" / "Topic.esp";
    const auto linkPath = root / "mods" / "LinkMod" / "Link.esp";
    writeBytes(topicPath, makeTopicPlugin(false));
    writeBytes(linkPath, makeLinkingPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Topic.esp", topicPath, "TopicMod"),
            pluginEntry("Link.esp", linkPath, "LinkMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto refused = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Topic.esp", "000801:Topic.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RecordReferenced");
    CHECK(refused.error().message ==
          "Cannot delete 000801:Topic.esp: 000801:Link.esp in Link.esp refers to 000802:Topic.esp inside it");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Topic.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(topicPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000801:Topic.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000802:Topic.esp")) != nullptr);
}

TEST_CASE("Native delete refuses a DIAL whose topic child is overridden in another loaded plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto topicPath = root / "mods" / "TopicMod" / "Topic.esp";
    const auto overridePath = root / "mods" / "OverrideMod" / "Override.esp";
    writeBytes(topicPath, makeTopicPlugin(false));
    writeBytes(overridePath, makeOverridePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Topic.esp", topicPath, "TopicMod"),
            pluginEntry("Override.esp", overridePath, "OverrideMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto refused = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Topic.esp", "000801:Topic.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "RecordHasExternalVersions");
    CHECK(refused.error().message ==
          "Cannot delete 000801:Topic.esp: Override.esp overrides 000802:Topic.esp inside it");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Topic.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(topicPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000801:Topic.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000802:Topic.esp")) != nullptr);
}

TEST_CASE("Native delete removes a cell whose child group overrides a master record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Data" / "Fallout4.esm";
    const auto patchPath = root / "mods" / "CellMod" / "Patch.esp";
    writeBytes(masterPath, makeMasterCellPlugin());
    writeBytes(patchPath, makeMasterOverrideCellPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Fallout4.esm", masterPath, "Data"),
            pluginEntry("Patch.esp", patchPath, "CellMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto deleted = parseStringResult(backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    CHECK(deleted.at("deleted") == "000800:Patch.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp")) == nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000900:Fallout4.esm")) == nullptr);
    const auto resolved = parseStringResult(backend.invokeBackend(
        "GetRecordDetails", nlohmann::json::array({"000900:Fallout4.esm"})));
    CHECK(resolved.at("FormKey") == "000900:Fallout4.esm");
    CHECK(resolved.at("File") == "Fallout4.esm");
}
