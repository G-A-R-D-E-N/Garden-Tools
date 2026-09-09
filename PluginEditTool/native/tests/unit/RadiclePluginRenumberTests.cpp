#include <algorithm>
#include <chrono>
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

std::filesystem::path renumberTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_renumber_" + std::to_string(id));
}

struct RenumberCleanup {
    std::filesystem::path root;
    ~RenumberCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void renumberWriteBytes(const std::filesystem::path& path,
                        const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> renumberSourceBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x1001u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00001000u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MoveFirst\0", 10});
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MoveSecond\0", 11});
    fo4::test::ByteWriter link;
    link.u32(0x00001000u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::uint32_t renumberRawSubrecord(const fo4::Record& record,
                                   std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != wanted) continue;
        REQUIRE(subrecord.data.size() == 4);
        std::uint32_t value = 0;
        std::memcpy(&value, subrecord.data.data(), sizeof(value));
        return value;
    }
    FAIL("missing requested FormLink subrecord");
    return 0;
}

}  // namespace

TEST_CASE("Native RenumberPluginFormIds vacates overlapping targets before rekeying dependents") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    renumberWriteBytes(path, renumberSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                {
                    {"name", "Patch.esp"},
                    {"path", path.string()},
                    {"source", "RenumberTest"},
                    {"resolved", true}
                }
            })
        }});
    REQUIRE(loaded.has_value());

    const auto preview = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", false}));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN:"));
    CHECK(preview->get<std::string>().find("001000-001001") != std::string::npos);

    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(path));

    const auto applied = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", true}));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Renumbered 2 record(s)") != std::string::npos);

    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == beforeSave);
    CHECK(applied->get<std::string>().find("SavePlugin") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp")) == nullptr);

    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("001000:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(race->header.signature == fo4::Signature::from_string("RACE"));
    CHECK(renumberRawSubrecord(*race, "AHCM") == 0x00001001u);

    const auto* color = fo4::find_record(
        disk, fo4::FormKey::parse("001001:Patch.esp"));
    REQUIRE(color != nullptr);
    CHECK(color->header.signature == fo4::Signature::from_string("CLFM"));
}
