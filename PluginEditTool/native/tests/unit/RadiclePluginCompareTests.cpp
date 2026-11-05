#include <chrono>
#include "../helpers/TempRoot.hpp"
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
    return std::filesystem::temp_directory_path() / ("fo4ide_compare_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> miscPlugin(std::string_view editorId, std::vector<std::byte> data = {}) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    fo4::test::write_subrecord(out, "EDID", editorId);
    if (data.empty()) {
        fo4::test::ByteWriter value;
        value.u32(0x0000002A);
        data = value.bytes;
    }
    fo4::test::write_subrecord(out, "DATA", data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

nlohmann::json detail(fo4ide::plugin::RadiclePluginBackend& backend,
                      const std::filesystem::path& other, std::string_view formKey) {
    const auto result = backend.comparePluginTo(
        nlohmann::json::array({"Target.esp", other.string(), std::string(formKey)}));
    REQUIRE(result.has_value());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native ComparePluginTo returns both versions of one record side by side") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Target.esp", miscPlugin(std::string_view{"Current\0", 8}));
    writeBytes(root / "backup" / "Old.esp", miscPlugin(std::string_view{"Older\0", 6}));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Target.esp"}, {"path", (root / "Target.esp").string()}, {"source", "test"},
         {"resolved", true}}})}}).has_value());

    const auto sides = detail(backend, root / "backup" / "Old.esp", "000810:Target.esp");
    REQUIRE(sides.at("Before").size() == 2);
    REQUIRE(sides.at("After").size() == 2);
    CHECK(sides["Before"][0].at("Signature") == "EDID");
    CHECK(sides["Before"][0].at("Text") == "Older");
    CHECK(sides["After"][0].at("Text") == "Current");
    CHECK(sides["Before"][1].at("Hex") == "2A 00 00 00");
    CHECK(sides["After"][1].at("Hex") == "2A 00 00 00");
    CHECK(sides["After"][1].at("Size") == 4);
    CHECK(sides.at("Differs") == nlohmann::json::array({true, false}));

    const auto missing = detail(backend, root / "backup" / "Old.esp", "000999:Target.esp");
    CHECK(missing.at("Before").is_null());
    CHECK(missing.at("After").is_null());
}

TEST_CASE("Native ComparePluginTo marks a subrecord that differs only past the hex preview") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::vector<std::byte> current(80, std::byte{0});
    auto older = current;
    older[70] = std::byte{1};
    writeBytes(root / "Target.esp", miscPlugin(std::string_view{"Same\0", 5}, current));
    writeBytes(root / "Old.esp", miscPlugin(std::string_view{"Same\0", 5}, older));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Target.esp"}, {"path", (root / "Target.esp").string()}, {"source", "test"},
         {"resolved", true}}})}}).has_value());

    const auto sides = detail(backend, root / "Old.esp", "000810:Target.esp");
    CHECK(sides["Before"][1].at("Hex") == sides["After"][1].at("Hex"));
    CHECK(sides.at("Differs") == nlohmann::json::array({false, true}));
}
