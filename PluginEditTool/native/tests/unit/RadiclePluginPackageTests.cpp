#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/environment/package_procedure.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path packTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_pack_" + std::to_string(id));
}

struct PackCleanup {
    std::filesystem::path root;
    ~PackCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void packWrite(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::byte> packRead(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

void z(fo4::test::ByteWriter& out, std::string_view text) {
    std::vector<std::byte> bytes;
    for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    bytes.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "ANAM", bytes);
}

std::vector<std::byte> packPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "PACK", 0);
    const auto record = fo4::test::begin_record(out, "PACK", 0, 0x800);
    fo4::test::ByteWriter pku;
    pku.u32(0);
    pku.u32(0);
    pku.u32(1);
    fo4::test::write_subrecord(out, "PKCU", pku.bytes);
    z(out, "Sequence");
    fo4::test::ByteWriter citc;
    citc.u32(0);
    fo4::test::write_subrecord(out, "CITC", citc.bytes);
    fo4::test::ByteWriter prcb;
    prcb.u32(1);
    prcb.u32(0);
    fo4::test::write_subrecord(out, "PRCB", prcb.bytes);
    fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{std::byte{'P'}, std::byte{'r'}, std::byte{'o'},
                                                                   std::byte{'c'}, std::byte{'e'}, std::byte{'d'},
                                                                   std::byte{'u'}, std::byte{'r'}, std::byte{'e'},
                                                                   std::byte{0}});
    fo4::test::write_subrecord(out, "CITC", citc.bytes);
    fo4::test::write_subrecord(out, "PNAM", std::vector<std::byte>{std::byte{'A'}, std::byte{'c'}, std::byte{'t'},
                                                                   std::byte{'i'}, std::byte{'v'}, std::byte{'a'},
                                                                   std::byte{'t'}, std::byte{'e'}, std::byte{0}});
    fo4::test::write_subrecord(out, "POBA", std::string_view{});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json packEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

}  // namespace

TEST_CASE("Native package procedure edits add and remove a leaf through save and re-read") {
    const auto root = packTempRoot();
    PackCleanup cleanup{root};
    const auto path = root / "Pack.esp";
    packWrite(path, packPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", nlohmann::json::array({packEntry("Pack.esp", path)})}})
                .has_value());
    const std::string key = "000800:Pack.esp";
    const auto added = backend.addPackageProcedure(nlohmann::json::array({"Pack.esp", key, 1, "Activate"}));
    REQUIRE(added.has_value());
    REQUIRE(backend.removePackageProcedure(nlohmann::json::array({"Pack.esp", key, 1})).has_value());
    const auto refused = backend.addPackageProcedure(nlohmann::json::array({"Pack.esp", key, 1, "Fly"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PackageWriteFailed");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Pack.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(saved, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    CHECK(fo4::env::decode_package_procedure(*record).decoded);
    CHECK(packRead(path).size() > 0);
}
