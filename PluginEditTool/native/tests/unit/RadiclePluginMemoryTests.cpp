#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_memory_" + std::to_string(id));
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

std::vector<std::byte> makeBasePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Weapon\0", 7});
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, weaponGroup);
    return out.bytes;
}

std::vector<std::byte> makeOverridePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esp\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json pluginEntry(const std::string& name, const std::filesystem::path& path) {
    return {{"name", name}, {"path", path.string()}, {"source", name}, {"resolved", true}};
}

int worklistTotal(fo4ide::plugin::RadiclePluginBackend& backend, const std::string& name) {
    const auto result = backend.invoke("backend.GetPluginWorklist", nlohmann::json::array({name}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>()).at("Total").get<int>();
}

}

TEST_CASE("Native release_memory drop_conflict_graph keeps an edit visible to later graph queries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esp", makeBasePlugin());
    writeBytes(root / "Top.esp", makeOverridePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", root / "Base.esp"),
        pluginEntry("Top.esp", root / "Top.esp")
    })}}).has_value());
    const auto before = worklistTotal(backend, "Top.esp");

    REQUIRE(backend.invoke("backend.CopyAsOverride",
        nlohmann::json::array({"Base.esp", "000800:Base.esp", "Top.esp", false})).has_value());
    const auto edited = worklistTotal(backend, "Top.esp");
    CHECK(edited == before + 1);

    const auto released = backend.releaseMemory(nlohmann::json::array({true}));
    REQUIRE(released.has_value());
    CHECK(released->get<std::string>().find("Dropped the cached conflict graph") != std::string::npos);
    CHECK(worklistTotal(backend, "Top.esp") == edited);
}

#ifdef FO4IDE_TEST_HOOKS
// Invariant: a dropped graph must be destroyed, not pinned by a proxy snapshot.
TEST_CASE("Native release_memory drop_conflict_graph destroys the cached graph") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esp", makeBasePlugin());
    writeBytes(root / "Top.esp", makeOverridePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        pluginEntry("Base.esp", root / "Base.esp"),
        pluginEntry("Top.esp", root / "Top.esp")
    })}}).has_value());
    worklistTotal(backend, "Base.esp");
    const auto graph = backend.cachedGraphForTest();
    REQUIRE_FALSE(graph.expired());

    REQUIRE(backend.releaseMemory(nlohmann::json::array({true})).has_value());
    CHECK(graph.expired());
}
#endif

TEST_CASE("Native release_memory trims the working set and keeps the backend usable") {
    fo4ide::plugin::RadiclePluginBackend backend;
    const auto result = backend.releaseMemory(nlohmann::json::array());
    REQUIRE(result.has_value());
    const auto message = result->get<std::string>();
    CHECK(message.find("Trimmed the process working set") != std::string::npos);
    CHECK(message.find("stays loaded") != std::string::npos);
}
