#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/OracleFixtures.hpp"

namespace {

using namespace oracle_fixture;
using fo4ide::plugin::RadiclePluginBackend;

std::vector<std::byte> armorPlugin(std::uint16_t formVersion, std::size_t entryStride) {
    fo4::test::ByteWriter out;
    writeHeader(out, 1.0f, 0, 0xF00);
    const auto damage = fo4::test::begin_group(out, "DMGT", 0);
    writeNamedRecord(out, "DMGT", 0x810, "DamageA");
    writeNamedRecord(out, "DMGT", 0x811, "DamageB");
    fo4::test::end_group(out, damage);
    const auto armor = fo4::test::begin_group(out, "ARMO", 0);
    const auto record = fo4::test::begin_record(out, "ARMO", 0, 0xA00, formVersion);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Suit\0", 5});
    fo4::test::ByteWriter dama;
    for (std::uint32_t entry = 0; entry < 2; ++entry) {
        dama.u32(0x810 + entry);
        dama.u32(10 + entry);
        if (entryStride == 12) dama.u32(0x900 + entry);
    }
    fo4::test::write_subrecord(out, "DAMA", dama.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, armor);
    return out.bytes;
}

std::vector<std::vector<std::uint32_t>> savedDama(const std::filesystem::path& path) {
    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000A00:Patch.esp"));
    REQUIRE(record != nullptr);
    std::vector<std::vector<std::uint32_t>> payloads;
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("DAMA")) continue;
        REQUIRE(subrecord.data.size() % 4 == 0);
        std::vector<std::uint32_t> words(subrecord.data.size() / 4);
        std::memcpy(words.data(), subrecord.data.data(), subrecord.data.size());
        payloads.push_back(std::move(words));
    }
    return payloads;
}

fo4ide::Result<nlohmann::json> setArmorRow(RadiclePluginBackend& backend, const std::string& field,
                                           const std::string& value) {
    return backend.invokeBackend("SetField", nlohmann::json::array({"Patch.esp", "000A00:Patch.esp", field, value}));
}

}  // namespace

TEST_CASE("Oracle armor resistance rows write damage type and amount into the form version 151 DAMA stride") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, armorPlugin(151, 8));

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    REQUIRE(setArmorRow(backend, "ARMO\\ResistanceType[0]", "000811:Patch.esp").has_value());
    REQUIRE(setArmorRow(backend, "ARMO\\ResistanceAmount[0]", "37").has_value());
    REQUIRE(setArmorRow(backend, "ARMO\\ResistanceType[1]", "000810:Patch.esp").has_value());
    REQUIRE(setArmorRow(backend, "ARMO\\ResistanceAmount[1]", "91").has_value());
    savePatch(backend);

    const auto saved = savedDama(path);
    REQUIRE(saved.size() == 1);
    REQUIRE(saved[0].size() == 4);
    CHECK(saved[0][0] == 0x811u);
    CHECK(saved[0][1] == 37u);
    CHECK(saved[0][2] == 0x810u);
    CHECK(saved[0][3] == 91u);
}

TEST_CASE("Oracle armor resistance rows refuse a form version 152 record and leave its DAMA stride 12 bytes alone") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, armorPlugin(152, 12));

    RadiclePluginBackend backend;
    loadPatch(backend, path);
    CHECK_FALSE(setArmorRow(backend, "ARMO\\ResistanceAmount[0]", "37").has_value());
    CHECK_FALSE(setArmorRow(backend, "ARMO\\ResistanceType[1]", "000811:Patch.esp").has_value());
    savePatch(backend);

    const auto saved = savedDama(path);
    REQUIRE(saved.size() == 1);
    CHECK(saved[0] == std::vector<std::uint32_t>{0x810u, 10u, 0x900u, 0x811u, 11u, 0x901u});
}
