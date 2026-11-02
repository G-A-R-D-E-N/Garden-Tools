#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "hedr_count.hpp"
#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_hedr_count_" + std::to_string(id));
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
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<std::byte> emptyPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({{
        {"name", "Patch.esp"}, {"path", path.string()}, {"source", "HedrCount"}, {"resolved", true}}})}})
                .has_value());
}

void createMessages(fo4ide::plugin::RadiclePluginBackend& backend, int from, int to) {
    for (int index = from; index < to; ++index) {
        const nlohmann::json payload{{"editorId", "Msg" + std::to_string(index)}};
        REQUIRE(backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"Patch.esp", "MESG", payload})).has_value());
    }
}

void save(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

}  // namespace

// Detects: content-only edits must preserve a noncanonical HEDR count while structural edits must recalculate it.
TEST_CASE("Native save preserves a noncanonical count until structure changes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadPatch(backend, path);
    createMessages(backend, 0, 11);
    save(backend);

    auto bytes = readBytes(path);
    const std::uint32_t wrong = 26;
    std::memcpy(bytes.data() + fo4::test::k_hedr_count_offset, &wrong, sizeof(wrong));
    writeBytes(path, bytes);

    fo4ide::plugin::RadiclePluginBackend reloaded;
    loadPatch(reloaded, path);
    REQUIRE(reloaded.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "EDID", "Renamed"})).has_value());
    save(reloaded);
    {
        CHECK(fo4::test::stored_hedr_count(readBytes(path)) == wrong);
    }

    createMessages(reloaded, 11, 14);
    save(reloaded);
    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::test::stored_hedr_count(readBytes(path)) == fo4::test::rebuilt_hedr_count(disk));
}
