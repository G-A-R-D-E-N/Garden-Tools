#include <chrono>
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

std::filesystem::path leveledTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_leveled_" + std::to_string(id));
}

struct LeveledCleanup {
    std::filesystem::path root;
    ~LeveledCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void leveledWriteBytes(const std::filesystem::path& path,
                       const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void writeLvlo(fo4::test::ByteWriter& out, std::uint32_t target) {
    fo4::test::ByteWriter lvlo;
    lvlo.u16(1);       // level
    lvlo.u16(0);       // unused
    lvlo.u32(target);  // reference FormID, schema-proven offset 4
    lvlo.u16(1);       // count
    lvlo.u8(100);      // chance none / entry chance byte
    lvlo.u8(0);        // padding
    fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
}

std::vector<std::byte> circularLeveledBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x802u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVLI", 0);
    const auto first = fo4::test::begin_record(out, "LVLI", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListA\0", 6});
    writeLvlo(out, 0x00000801u);
    fo4::test::end_record(out, first);

    const auto second = fo4::test::begin_record(out, "LVLI", 0, 0x00000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListB\0", 6});
    writeLvlo(out, 0x00000800u);
    fo4::test::end_record(out, second);
    fo4::test::end_group(out, group);
    return out.bytes;
}

}  // namespace

TEST_CASE("Native CheckCircularLeveledLists follows schema-proven LVLO references") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, circularLeveledBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                {
                    {"name", "Patch.esp"},
                    {"path", path.string()},
                    {"source", "LeveledTest"},
                    {"resolved", true}
                }
            })
        }});
    REQUIRE(loaded.has_value());

    const auto report = backend.checkCircularLeveledLists(
        nlohmann::json::array({"Patch.esp", 200}));
    REQUIRE(report.has_value());
    REQUIRE(report->is_string());
    CHECK(report->get<std::string>().find("1 circular leveled list(s)") != std::string::npos);
    CHECK(report->get<std::string>().find("ListA [000800:Patch.esp]") != std::string::npos);
    CHECK(report->get<std::string>().find("ListB [000801:Patch.esp]") != std::string::npos);
}
