#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"
#include "RadiclePluginCellFixture.hpp"

using namespace cell_fixture;

namespace {

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
    out.u16(static_cast<std::uint16_t>(text.size()));
    out.raw(text);
}

void objectElement(fo4::test::ByteWriter& out, std::uint32_t recordId) {
    out.u16(0);
    out.u16(0xFFFFu);
    out.u32(recordId);
}

std::vector<std::byte> baseVmad() {
    fo4::test::ByteWriter out;
    out.u16(6);
    out.u16(2);
    out.u16(1);
    wstring(out, "BaseScript");
    out.u8(0);
    out.u16(1);
    wstring(out, "Speed");
    out.u8(4);
    out.u8(1);
    out.f32(2.5f);
    return out.bytes;
}

std::vector<std::byte> referenceVmad() {
    fo4::test::ByteWriter out;
    out.u16(6);
    out.u16(2);
    out.u16(1);
    wstring(out, "Script");
    out.u8(1);
    out.u16(4);
    wstring(out, "Target");
    out.u8(1);
    out.u8(1);
    objectElement(out, 0x810u);
    wstring(out, "Label");
    out.u8(2);
    out.u8(1);
    wstring(out, "hello");
    wstring(out, "Count");
    out.u8(3);
    out.u8(1);
    out.i32(7);
    wstring(out, "Items");
    out.u8(11);
    out.u8(1);
    out.u32(2);
    objectElement(out, 0x812u);
    objectElement(out, 0x813u);
    out.u8(0xAA);
    out.u8(0xBB);
    out.u8(0xCC);
    return out.bytes;
}

void writeFloats(fo4::test::ByteWriter& out, std::initializer_list<float> values) {
    for (const float value : values) out.f32(value);
}

void writeReference(fo4::test::ByteWriter& out, std::uint32_t flags, std::uint32_t recordId,
                    std::uint32_t baseId) {
    const auto reference = fo4::test::begin_record(out, "REFR", flags, recordId);
    fo4::test::ByteWriter name;
    name.u32(baseId);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::ByteWriter transform;
    writeFloats(transform, {1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f});
    fo4::test::write_subrecord(out, "DATA", transform.bytes);
    fo4::test::end_record(out, reference);
}

std::vector<std::byte> referencePlugin(bool malformedReference) {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto statTop = fo4::test::begin_group(out, "STAT", 0);
    const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseStatic\0", 11});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\Base.nif\0", 16});
    fo4::test::write_subrecord(out, "VMAD", baseVmad());
    fo4::test::end_record(out, stat);
    fo4::test::end_group(out, statTop);

    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto subblock = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000800u);
    const std::vector<std::byte> cellData{std::byte{1}, std::byte{0}};
    fo4::test::write_subrecord(out, "DATA", cellData);
    fo4::test::end_record(out, cell);
    const auto children = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_children);
    const auto temporary = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);

    const auto parent = fo4::test::begin_record(out, "REFR", 0, 0x00000901u);
    fo4::test::ByteWriter parentName;
    parentName.u32(0x00000810u);
    fo4::test::write_subrecord(out, "NAME", parentName.bytes);
    fo4::test::end_record(out, parent);

    const auto reference = fo4::test::begin_record(out, "REFR", 0x00000C00u, 0x00000900u);
    fo4::test::ByteWriter name;
    name.u32(0x00000810u);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::ByteWriter transform;
    writeFloats(transform, {1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f});
    fo4::test::write_subrecord(out, "DATA", transform.bytes);
    fo4::test::ByteWriter scale;
    scale.f32(2.0f);
    fo4::test::write_subrecord(out, "XSCL", scale.bytes);
    fo4::test::ByteWriter enableParent;
    enableParent.u32(0x00000901u);
    enableParent.u8(3);
    enableParent.u8(0);
    enableParent.u8(0);
    enableParent.u8(0);
    fo4::test::write_subrecord(out, "XESP", enableParent.bytes);
    fo4::test::ByteWriter owner;
    owner.u32(0x00000811u);
    owner.u32(0);
    owner.u8(1);
    owner.u8(0);
    owner.u8(0);
    owner.u8(0);
    fo4::test::write_subrecord(out, "XOWN", owner.bytes);
    fo4::test::ByteWriter rank;
    rank.i32(2);
    fo4::test::write_subrecord(out, "XRNK", rank.bytes);
    fo4::test::ByteWriter linked;
    linked.u32(0x00000812u);
    linked.u32(0x00000813u);
    fo4::test::write_subrecord(out, "XLKR", linked.bytes);
    fo4::test::ByteWriter lock;
    lock.u8(75);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    lock.u32(0x00000814u);
    lock.u8(4);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    lock.u8(0);
    fo4::test::write_subrecord(out, "XLOC", lock.bytes);
    fo4::test::write_subrecord(out, "MNAM", std::string_view{"note\0", 5});
    fo4::test::write_subrecord(out, "VMAD", referenceVmad());
    fo4::test::end_record(out, reference);
    if (malformedReference) {
        const auto broken = fo4::test::begin_record(out, "REFR", 0, 0x00000902u);
        fo4::test::ByteWriter shortParent;
        shortParent.u8(1);
        shortParent.u8(2);
        shortParent.u8(3);
        fo4::test::write_subrecord(out, "XESP", shortParent.bytes);
        fo4::test::end_record(out, broken);
    }

    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

void loadPlugin(fo4ide::plugin::RadiclePluginBackend& backend,
                const std::filesystem::path& root,
                std::vector<std::byte> bytes) {
    const auto path = root / "Base.esm";
    writeBytes(path, bytes);
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Base.esm", path)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

}  // namespace

TEST_CASE("Native reference details decode a placed reference with its scripts and links") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, referencePlugin(false));

    const auto details = parseStringResult(
        backend.getReferenceDetails(nlohmann::json::array({"000900:Base.esm"})));
    CHECK_FALSE(details.contains("error"));
    CHECK(details.at("formKey") == "000900:Base.esm");
    CHECK(details.at("signature") == "REFR");
    CHECK(details.at("winningPlugin") == "Base.esm");
    CHECK(details.at("pluginsWithRecord").size() == 1);
    CHECK(details.at("cell").at("formKey") == "000800:Base.esm");

    CHECK(details.at("flags").at("persistent") == true);
    CHECK(details.at("flags").at("initiallyDisabled") == true);
    CHECK(details.at("flags").at("deleted") == false);
    CHECK(details.at("flags").at("respawns") == true);
    std::vector<std::string> flagNames;
    for (const auto& flag : details.at("flags").at("names")) {
        flagNames.push_back(flag.at("name").get<std::string>());
    }
    CHECK(flagNames == std::vector<std::string>{"Persistent", "Initially Disabled"});
    CHECK(details.at("flags").at("names")[0].at("bit") == 10);
    CHECK(details.at("flags").at("names")[1].at("bit") == 11);

    CHECK(details.at("base").at("formKey") == "000810:Base.esm");
    CHECK(details.at("base").at("signature") == "STAT");
    CHECK(details.at("base").at("editorId") == "BaseStatic");
    CHECK(details.at("base").at("model") == "Meshes\\Base.nif");
    CHECK(details.at("base").at("missing") == false);

    CHECK(details.at("transform").at("position").at("x") == 1.0f);
    CHECK(details.at("transform").at("scale") == 2.0f);

    CHECK(details.at("enableParent").at("reference").at("formKey") == "000901:Base.esm");
    CHECK(details.at("enableParent").at("flags") == 3);
    CHECK(details.at("enableParent").at("setOppositeToParent") == true);
    CHECK(details.at("enableParent").at("popIn") == true);

    CHECK(details.at("ownership").at("owner").at("formKey") == "000811:Base.esm");
    CHECK(details.at("ownership").at("noCrime") == true);
    CHECK(details.at("factionRank") == 2);

    REQUIRE(details.at("linkedReferences").size() == 1);
    CHECK(details.at("linkedReferences")[0].at("keyword").at("formKey") == "000812:Base.esm");
    CHECK(details.at("linkedReferences")[0].at("reference").at("formKey") == "000813:Base.esm");

    CHECK(details.at("lock").at("level") == 75);
    CHECK(details.at("lock").at("levelName") == "Expert");
    CHECK(details.at("lock").at("key").at("formKey") == "000814:Base.esm");
    CHECK(details.at("lock").at("flags") == 4);

    CHECK(details.at("comments") == "note");
    CHECK(details.at("count").is_null());

    const auto& scripts = details.at("scripts");
    CHECK(scripts.at("fragmentBytes") == 3);
    REQUIRE(scripts.at("scripts").size() == 1);
    const auto& properties = scripts.at("scripts")[0].at("properties");
    REQUIRE(properties.size() == 4);
    CHECK(properties[0].at("value").at("object").at("formKey") == "000810:Base.esm");
    CHECK(properties[1].at("value") == "hello");
    CHECK(properties[2].at("value") == 7);
    CHECK(properties[3].at("elements").size() == 2);

    const auto& baseScripts = details.at("baseScripts");
    CHECK(baseScripts.at("fragmentBytes") == 0);
    CHECK(baseScripts.at("scripts")[0].at("name") == "BaseScript");
    CHECK(baseScripts.at("scripts")[0].at("properties")[0].at("value") == 2.5f);
}

TEST_CASE("Native reference details return error JSON for unknown and malformed references") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, referencePlugin(true));

    const auto missing = parseStringResult(
        backend.getReferenceDetails(nlohmann::json::array({"0FFFFF:Base.esm"})));
    CHECK(missing.at("error").get<std::string>().find("not found") != std::string::npos);

    const auto notPlaced = parseStringResult(
        backend.getReferenceDetails(nlohmann::json::array({"000810:Base.esm"})));
    CHECK(notPlaced.at("error").get<std::string>().find("not a placed reference") != std::string::npos);

    const auto malformed = parseStringResult(
        backend.getReferenceDetails(nlohmann::json::array({"000902:Base.esm"})));
    CHECK(malformed.at("error").get<std::string>().find("malformed") != std::string::npos);
}
