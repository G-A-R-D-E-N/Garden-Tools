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

std::filesystem::path deepTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_deep_copy_" + std::to_string(id));
}

struct DeepCleanup {
    std::filesystem::path root;
    ~DeepCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void deepWriteBytes(const std::filesystem::path& path,
                    const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void deepWriteHeader(fo4::test::ByteWriter& out,
                     std::uint32_t nextFormId) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> deepSourceBytes() {
    fo4::test::ByteWriter out;
    deepWriteHeader(out, 0x802);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeepColor\0", 10});
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeepRace\0", 9});
    fo4::test::ByteWriter link;
    link.u32(0x00000800u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::vector<std::byte> deepEmptyPatchBytes() {
    fo4::test::ByteWriter out;
    deepWriteHeader(out, 0x800);
    return out.bytes;
}

nlohmann::json deepPluginEntry(std::string name,
                               const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "DeepCopyTest"},
        {"resolved", true}
    };
}

std::uint32_t deepRawSubrecord(const fo4::Record& record,
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

TEST_CASE("Native DeepCopyAsOverride previews copies dependencies and prompts on collisions") {
    const auto root = deepTempRoot();
    DeepCleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    deepWriteBytes(sourcePath, deepSourceBytes());
    deepWriteBytes(patchPath, deepEmptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                deepPluginEntry("Base.esm", sourcePath),
                deepPluginEntry("Patch.esp", patchPath)
            })
        }});
    REQUIRE(loaded.has_value());

    const auto args = nlohmann::json::array({
        "Base.esm", "000801:Base.esm", "Patch.esp", false, false, 200
    });
    const auto preview = backend.deepCopyAsOverride(args);
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN:"));
    CHECK(preview->get<std::string>().find("2 record(s)") != std::string::npos);
    CHECK(preview->get<std::string>().find("CLFM x1") != std::string::npos);
    CHECK(preview->get<std::string>().find("RACE x1") != std::string::npos);

    const auto applied = backend.deepCopyAsOverride(nlohmann::json::array({
        "Base.esm", "000801:Base.esm", "Patch.esp", true, false, 200
    }));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Deep-copied 2 of 2 record(s)") != std::string::npos);

    const auto patch = fo4::read_plugin_file(patchPath);
    REQUIRE(patch.header.masters.size() == 1);
    CHECK(patch.header.masters[0].key.filename() == "Base.esm");

    const auto* color = fo4::find_record(
        patch, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(color != nullptr);
    CHECK(color->header.signature == fo4::Signature::from_string("CLFM"));

    const auto* race = fo4::find_record(
        patch, fo4::FormKey::parse("000801:Base.esm"));
    REQUIRE(race != nullptr);
    CHECK(race->header.signature == fo4::Signature::from_string("RACE"));
    CHECK(deepRawSubrecord(*race, "AHCM") == 0x00000800u);

    const auto collision = backend.deepCopyAsOverride(nlohmann::json::array({
        "Base.esm", "000801:Base.esm", "Patch.esp", true, false, 200
    }));
    REQUIRE(collision.has_value());
    REQUIRE(collision->is_string());
    CHECK(collision->get<std::string>().starts_with("EXISTS:"));
    CHECK(collision->get<std::string>().find("2 of the 2 record(s)") != std::string::npos);
}

TEST_CASE("Native DeepCopyAsOverride previews a brand-new target without creating it") {
    const auto root = deepTempRoot();
    DeepCleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    deepWriteBytes(sourcePath, deepSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({deepPluginEntry("Base.esm", sourcePath)})
        }});
    REQUIRE(loaded.has_value());

    const auto missingPath = root / "NewPatch.esp";
    REQUIRE_FALSE(std::filesystem::exists(missingPath));
    const auto preview = backend.deepCopyAsOverride(nlohmann::json::array({
        "Base.esm", "000801:Base.esm", "NewPatch.esp", false, false, 200
    }));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN:"));
    CHECK(preview->get<std::string>().find("2 record(s)") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(missingPath));
}
