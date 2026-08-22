#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_lifecycle_" + std::to_string(id));
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

std::vector<std::byte> makePlugin(std::uint32_t tes4Flags = 0,
                                  std::uint32_t nextFormId = 0x900) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Existing\0", 9});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeReferencingPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x901);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esp\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x01000900);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
    fo4::test::ByteWriter name;
    name.u32(0x00000800);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
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

TEST_CASE("Native lifecycle creates renumbers and deletes a persisted local record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "MyMod" / "Patch.esp";
    writeBytes(pluginPath, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto created = parseStringResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EYES"})));
    REQUIRE(created.at("formKey") == "000900:Patch.esp");

    auto disk = fo4::read_plugin_file(pluginPath);
    REQUIRE(fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp")) != nullptr);
    CHECK(disk.header.next_form_id == 0x901u);

    const auto renumbered = parseStringResult(backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Patch.esp", "000900:Patch.esp", "000950:Patch.esp"})));
    CHECK(renumbered.at("changed") == true);

    disk = fo4::read_plugin_file(pluginPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000900:Patch.esp")) == nullptr);
    REQUIRE(fo4::find_record(disk, fo4::FormKey::parse("000950:Patch.esp")) != nullptr);
    CHECK(disk.header.next_form_id == 0x951u);

    const auto deleted = parseStringResult(backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Patch.esp", "000950:Patch.esp"})));
    CHECK(deleted.at("deleted") == "000950:Patch.esp");

    disk = fo4::read_plugin_file(pluginPath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000950:Patch.esp")) == nullptr);
    CHECK(disk.header.next_form_id == 0x951u);
}

TEST_CASE("Native lifecycle refuses protected Data plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto dataDir = root / "Fallout 4" / "Data";
    const auto pluginPath = dataDir / "Protected.esp";
    writeBytes(pluginPath, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(dataDir);
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Protected.esp", pluginPath, "WrongSource")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto created = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Protected.esp", "EYES"}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "PluginReadOnly");

    const auto deleted = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Protected.esp", "000800:Protected.esp"}));
    REQUIRE_FALSE(deleted.has_value());
    CHECK(deleted.error().type == "PluginReadOnly");
}

TEST_CASE("Native lifecycle refuses light create and renumber") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "LightMod" / "Light.esl";
    writeBytes(pluginPath, makePlugin(fo4::RecordFlags::small_file));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Light.esl", pluginPath, "LightMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto created = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Light.esl", "EYES"}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "LightPluginLifecycleUnsupported");

    const auto renumbered = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Light.esl", "000800:Light.esl", "000900:Light.esl"}));
    REQUIRE_FALSE(renumbered.has_value());
    CHECK(renumbered.error().type == "LightPluginLifecycleUnsupported");
}

TEST_CASE("Native lifecycle refuses delete and renumber across external references") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "mods" / "BaseMod" / "Base.esp";
    const auto refPath = root / "mods" / "RefMod" / "Ref.esp";
    writeBytes(basePath, makePlugin());
    writeBytes(refPath, makeReferencingPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "BaseMod"),
            pluginEntry("Ref.esp", refPath, "RefMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto deleted = backend.invokeBackend(
        "DeleteRecord", nlohmann::json::array({"Base.esp", "000800:Base.esp"}));
    REQUIRE_FALSE(deleted.has_value());
    CHECK(deleted.error().type == "RecordReferenced");

    const auto renumbered = backend.invokeBackend(
        "RenumberRecord",
        nlohmann::json::array({"Base.esp", "000800:Base.esp", "000950:Base.esp"}));
    REQUIRE_FALSE(renumbered.has_value());
    CHECK(renumbered.error().type == "RecordReferencedExternally");

    const auto disk = fo4::read_plugin_file(basePath);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Base.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000950:Base.esp")) == nullptr);
}
