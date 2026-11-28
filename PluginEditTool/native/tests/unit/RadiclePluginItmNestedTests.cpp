#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_itm_nested_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> cellPlugin(bool withMaster, std::string_view refEdid) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (withMaster) {
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
    fo4::test::end_record(out, cell);
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    out.patch_u32(children + 8, 0x800);
    const auto temporary = fo4::test::begin_group(out, "CELL", 9);
    out.patch_u32(temporary + 8, 0x800);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", refEdid);
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, temporary);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> udrCellPlugin(bool withMaster, bool deleted, bool stub) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (withMaster) {
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
    fo4::test::end_record(out, cell);
    const auto children = fo4::test::begin_group(out, "CELL", 6);
    out.patch_u32(children + 8, 0x800);
    const auto persistent = fo4::test::begin_group(out, "CELL", 8);
    out.patch_u32(persistent + 8, 0x800);
    const auto flags = deleted ? fo4::RecordFlags::deleted | fo4::RecordFlags::persistent
                               : fo4::RecordFlags::persistent;
    const auto refr = fo4::test::begin_record(out, "REFR", flags, 0x00000801);
    fo4::test::ByteWriter name;
    name.u32(0x00000800);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    if (!stub) {
        fo4::test::ByteWriter transform;
        for (int index = 0; index < 6; ++index) transform.f32(1.0f + static_cast<float>(index));
        fo4::test::write_subrecord(out, "DATA", transform.bytes);
    }
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, persistent);
    fo4::test::end_group(out, children);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::size_t subrecordSize(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == wanted) return subrecord.data.size();
    }
    return 0;
}

nlohmann::json entry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

std::string removeItm(fo4ide::plugin::RadiclePluginBackend& backend, bool apply) {
    const auto result = backend.invokeLegacyBackend(
        "RemoveIdenticalToMaster", nlohmann::json::array({"Patch.esp", apply, 2000}));
    REQUIRE(result.has_value());
    return result->get<std::string>();
}

}  // namespace

TEST_CASE("Native RemoveIdenticalToMaster removes a nested ITM reference and its emptied cell") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", cellPlugin(false, std::string_view{"Ref\0", 4}));
    writeBytes(root / "Patch.esp", cellPlugin(true, std::string_view{"Ref\0", 4}));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        entry("Base.esm", root / "Base.esm"), entry("Patch.esp", root / "Patch.esp")})}}).has_value());

    CHECK(removeItm(backend, false).starts_with("DRY RUN: 2 identical-to-master record(s) in Patch.esp of 2 checked."));
    CHECK(removeItm(backend, true) == "Removed 2 identical-to-master record(s) from Patch.esp.");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(root / "Patch.esp");
    CHECK(saved.groups.empty());
}

TEST_CASE("Native RemoveIdenticalToMaster keeps an ITM cell whose reference changed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", cellPlugin(false, std::string_view{"Ref\0", 4}));
    writeBytes(root / "Patch.esp", cellPlugin(true, std::string_view{"Moved\0", 6}));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        entry("Base.esm", root / "Base.esm"), entry("Patch.esp", root / "Patch.esp")})}}).has_value());

    CHECK(removeItm(backend, true).starts_with("No identical-to-master records in Patch.esp"));
}

TEST_CASE("Native CleanPlugin restores a deleted reference's content from its master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Base.esm", udrCellPlugin(false, false, false));
    writeBytes(root / "Patch.esp", udrCellPlugin(true, true, true));
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        entry("Base.esm", root / "Base.esm"), entry("Patch.esp", root / "Patch.esp")})}}).has_value());

    const auto cleaned = backend.invokeLegacyBackend(
        "CleanPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(cleaned.has_value());
    CHECK(cleaned->get<std::string>() == "Cleaned Patch.esp: undeleted + disabled 1 record(s).");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto saved = fo4::read_plugin_file(root / "Patch.esp");
    const auto* reference = fo4::find_record(saved, fo4::FormKey::parse("000801:Base.esm"));
    REQUIRE(reference != nullptr);
    CHECK_FALSE(reference->header.flags.is_deleted());
    CHECK(reference->header.flags.test(fo4::RecordFlags::initially_disabled));
    CHECK(subrecordSize(*reference, "DATA") == 24u);
    CHECK(subrecordSize(*reference, "XESP") == 8u);
}
