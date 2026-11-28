#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_assetrefs_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::string zstring(std::string_view text) {
    std::string out(text);
    out.push_back('\0');
    return out;
}

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", zstring("Base.esm"));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "ACTI", 0);
    const auto own = fo4::test::begin_record(out, "ACTI", 0, 0x01000800);
    fo4::test::write_subrecord(out, "MODL", zstring("Clutter\\Rock.nif"));
    fo4::test::write_subrecord(out, "DMDL", zstring("Clutter\\Broken.nif"));
    fo4::test::write_subrecord(out, "DMDL", zstring("Clutter\\notes.txt"));
    fo4::test::end_record(out, own);
    const auto second = fo4::test::begin_record(out, "ACTI", 0, 0x01000801);
    fo4::test::write_subrecord(out, "MODL", zstring("Clutter\\Rock.nif"));
    fo4::test::end_record(out, second);
    const auto inherited = fo4::test::begin_record(out, "ACTI", 0, 0x00000802);
    fo4::test::write_subrecord(out, "MODL", zstring("Clutter\\Inherited.nif"));
    fo4::test::end_record(out, inherited);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

}

TEST_CASE("CollectAssetReferences lists the plugin's own asset paths with the records that name them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{{"name", "Patch.esp"}, {"path", path.string()},
                                            {"source", "Test Mod"}, {"resolved", true}}})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invokeBackend("CollectAssetReferences", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->at("plugin") == "Patch.esp");
    CHECK(result->at("nativeRecords") == 2);
    std::error_code ec;
    CHECK(std::filesystem::equivalent(std::filesystem::path(result->at("pluginDirectory").get<std::string>()), root, ec));
    const auto& assets = result->at("assets");
    REQUIRE(assets.size() == 2);
    CHECK(assets[0].at("path") == "clutter/broken.nif");
    CHECK(assets[0].at("records").size() == 1);
    CHECK(assets[1].at("path") == "clutter/rock.nif");
    CHECK(assets[1].at("records").size() == 2);
}

TEST_CASE("CollectAssetReferences refuses a missing or unloaded plugin") {
    fo4ide::plugin::RadiclePluginBackend backend;
    CHECK(backend.invokeBackend("CollectAssetReferences", nlohmann::json::array({""})).error().type == "PluginMissing");
    CHECK(backend.invokeBackend("CollectAssetReferences", nlohmann::json::array({"Nope.esp"})).error().type ==
          "PluginNotLoaded");
}
