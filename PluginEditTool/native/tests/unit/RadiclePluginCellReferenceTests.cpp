#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/pex/pex.hpp>
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
    fo4::test::write_subrecord(out, "MNAM", std::string_view{"caf\xE9\0", 5});
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

void scriptHeader(fo4::test::ByteWriter& out, std::int16_t objectFormat, std::string_view script,
                  std::uint16_t propertyCount) {
    out.u16(6);
    out.u16(static_cast<std::uint16_t>(objectFormat));
    out.u16(1);
    wstring(out, script);
    out.u8(0);
    out.u16(propertyCount);
}

void propertyHeader(fo4::test::ByteWriter& out, std::string_view name, std::uint8_t type, std::uint8_t flags) {
    wstring(out, name);
    out.u8(type);
    out.u8(flags);
}

void objectElementWithAlias(fo4::test::ByteWriter& out, std::int16_t alias, std::uint32_t recordId) {
    out.u16(0);
    out.u16(static_cast<std::uint16_t>(alias));
    out.u32(recordId);
}

std::vector<std::byte> scriptPlugin(const std::vector<std::byte>& referenceVmad) {
    fo4::test::ByteWriter out;
    writeHeader(out, {});

    const auto statTop = fo4::test::begin_group(out, "STAT", 0);
    const auto stat = fo4::test::begin_record(out, "STAT", 0, 0x00000810u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ScriptStatic\0", 13});
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

    const auto reference = fo4::test::begin_record(out, "REFR", 0, 0x00000900u);
    fo4::test::ByteWriter name;
    name.u32(0x00000810u);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::ByteWriter transform;
    writeFloats(transform, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    fo4::test::write_subrecord(out, "DATA", transform.bytes);
    fo4::test::write_subrecord(out, "VMAD", referenceVmad);
    fo4::test::end_record(out, reference);

    endGroupRaw(out, temporary);
    endGroupRaw(out, children);
    endGroupRaw(out, subblock);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

nlohmann::json referenceDetails(fo4ide::plugin::RadiclePluginBackend& backend) {
    return parseStringResult(backend.getReferenceDetails(nlohmann::json::array({"000900:Base.esm"})));
}

fo4::pex::PexValue pexInt(std::int32_t value) {
    fo4::pex::PexValue out;
    out.type = fo4::pex::PexValueType::Integer;
    out.int_ = value;
    return out;
}

fo4::pex::PexValue pexFloat(float value) {
    fo4::pex::PexValue out;
    out.type = fo4::pex::PexValueType::Float;
    out.float_ = value;
    return out;
}

fo4::pex::PexValue pexBool(bool value) {
    fo4::pex::PexValue out;
    out.type = fo4::pex::PexValueType::Bool;
    out.bool_ = value;
    return out;
}

fo4::pex::PexValue pexString(std::string value) {
    fo4::pex::PexValue out;
    out.type = fo4::pex::PexValueType::String;
    out.str = std::move(value);
    return out;
}

fo4::pex::PexVariable pexVariable(std::string name, std::string type, fo4::pex::PexValue value) {
    fo4::pex::PexVariable out;
    out.name = std::move(name);
    out.type = std::move(type);
    out.defaultValue = std::move(value);
    return out;
}

fo4::pex::PexProperty autoProperty(std::string name, std::string type, std::string variable) {
    fo4::pex::PexProperty out;
    out.name = std::move(name);
    out.type = std::move(type);
    out.flags = 0x07;
    out.autoVarName = std::move(variable);
    return out;
}

fo4::pex::PexProperty literalProperty(std::string name, std::string type, fo4::pex::PexValue value,
                                      std::uint32_t userFlags = 0) {
    fo4::pex::PexProperty out;
    out.name = std::move(name);
    out.type = type;
    out.flags = 0x01;
    out.userFlags = userFlags;
    fo4::pex::PexFunction read;
    read.returnType = std::move(type);
    fo4::pex::PexInstruction ret;
    ret.opCode = 26;
    ret.mnemonic = "return";
    ret.fixedArgCount = 1;
    ret.args.push_back(std::move(value));
    read.instructions.push_back(std::move(ret));
    out.readHandler = std::move(read);
    return out;
}

fo4::pex::PexObject pexObject(std::string name, std::string parent, std::vector<fo4::pex::PexVariable> variables,
                              std::vector<fo4::pex::PexProperty> properties) {
    fo4::pex::PexObject out;
    out.name = std::move(name);
    out.parentClassName = std::move(parent);
    out.variables = std::move(variables);
    out.properties = std::move(properties);
    return out;
}

void writeScriptPex(const std::filesystem::path& scripts, fo4::pex::PexObject object) {
    fo4::pex::PexFile pex;
    pex.majorVersion = 3;
    pex.minorVersion = 2;
    pex.gameId = 2;
    pex.sourceFileName = object.name + ".psc";
    pex.userName = "tester";
    pex.computerName = "fixture";
    pex.userFlags.push_back({"Conditional", 1});
    const auto name = object.name;
    pex.objects.push_back(std::move(object));
    pex.rebuildStringTable();
    std::vector<std::byte> bytes;
    for (const auto byte : pex.toBytes()) bytes.push_back(std::byte{byte});
    writeBytes(scripts / (name + ".pex"), bytes);
}

const nlohmann::json* findDeclared(const nlohmann::json& script, std::string_view name) {
    for (const auto& declared : script.at("declared")) {
        if (declared.at("name").get<std::string>() == name) return &declared;
    }
    return nullptr;
}

nlohmann::json propertyValue(const nlohmann::json& details, std::string_view name) {
    if (!details.contains("scripts") || !details.at("scripts").is_object()) return nullptr;
    for (const auto& script : details.at("scripts").at("scripts")) {
        for (const auto& property : script.at("properties")) {
            if (property.at("name").get<std::string>() == name) return property.at("value");
        }
    }
    return nullptr;
}

void printProbe(const std::string& label, const nlohmann::json& details) {
    std::cout << "[probe] " << label << '\n';
    if (details.contains("error")) {
        std::cout << "  error: " << details.at("error").get<std::string>() << '\n';
        return;
    }
    std::cout << "  base: " << details.at("base").dump() << '\n';
    if (!details.at("scripts").is_object()) {
        std::cout << "  scripts: none\n";
        return;
    }
    for (const auto& script : details.at("scripts").at("scripts")) {
        std::cout << "  script " << script.at("name").get<std::string>() << " parent=" << script.at("parent").dump()
                  << " declaredError=" << script.at("declaredError").dump() << '\n';
        for (const auto& declared : script.at("declared")) {
            std::cout << "    declared " << declared.at("name").get<std::string>()
                      << " type=" << declared.at("type").dump() << " auto=" << declared.at("auto").dump()
                      << " conditional=" << declared.at("conditional").dump()
                      << " readOnly=" << declared.at("readOnly").dump()
                      << " default=" << declared.at("default").dump() << " status=" << declared.at("status").dump()
                      << " declaredBy=" << declared.at("declaredBy").dump() << '\n';
        }
        for (const auto& property : script.at("properties")) {
            std::cout << "    stored " << property.at("name").get<std::string>()
                      << " type=" << property.at("type").dump() << " flags=" << property.at("flags").dump()
                      << " value=" << property.at("value").dump() << " elements=" << property.at("elements").dump()
                      << " undecoded=" << property.at("undecoded").dump() << '\n';
        }
    }
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

    // Detects: a Windows-1252 byte in record text (here the comment) making the whole details call fail with ReferenceReadFailed.
    CHECK(details.at("comments") == "caf\xC3\xA9");
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

TEST_CASE("Native reference scripts read a script with no stored properties") {
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "MQ101GearDoorOpenStateScript", 0);
    REQUIRE(vmad.bytes.size() == 39);

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto details = referenceDetails(backend);
    CHECK_FALSE(details.contains("error"));
    const auto& scripts = details.at("scripts");
    CHECK(scripts.at("version") == 6);
    CHECK(scripts.at("objectFormat") == 2);
    CHECK(scripts.at("unsupported") == false);
    CHECK(scripts.at("fragmentBytes") == 0);
    REQUIRE(scripts.at("scripts").size() == 1);
    const auto& script = scripts.at("scripts")[0];
    CHECK(script.at("name") == "MQ101GearDoorOpenStateScript");
    CHECK(script.at("properties").empty());
    CHECK(script.at("declared").empty());
    CHECK(script.at("parent").is_null());
    CHECK(script.at("declaredError") == "game Data directory is not configured");
    CHECK(details.at("baseScripts").is_null());
}

TEST_CASE("Native reference scripts decode bool and object properties with their status flags") {
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "AttractionObjectScript", 3);
    propertyHeader(vmad, "EnableMeOnLoad", 5, 3);
    vmad.u8(0);
    propertyHeader(vmad, "ShowWhenDone", 5, 1);
    vmad.u8(1);
    propertyHeader(vmad, "Target", 1, 1);
    objectElementWithAlias(vmad, -1, 0x00000810u);

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto details = referenceDetails(backend);
    REQUIRE(details.at("scripts").at("scripts").size() == 1);
    const auto& properties = details.at("scripts").at("scripts")[0].at("properties");
    REQUIRE(properties.size() == 3);

    CHECK(properties[0].at("name") == "EnableMeOnLoad");
    CHECK(properties[0].at("typeName") == "bool");
    CHECK(properties[0].at("flags") == 3);
    CHECK(properties[0].at("value") == false);
    CHECK(properties[0].at("undecoded") == false);

    CHECK(properties[1].at("flags") == 1);
    CHECK(properties[1].at("value") == true);

    CHECK(properties[2].at("typeName") == "object");
    CHECK(properties[2].at("value").at("object").at("formKey") == "000810:Base.esm");
    CHECK(properties[2].at("value").at("alias") == -1);
}

TEST_CASE("Native reference scripts decode object, array, and struct property layouts") {
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "LayoutScript", 6);

    propertyHeader(vmad, "TriggeredByAliases", 11, 1);
    vmad.u32(1);
    objectElementWithAlias(vmad, 2, 0x0001ED86u);

    propertyHeader(vmad, "EmptyObjects", 11, 0);
    vmad.u32(0);

    propertyHeader(vmad, "Counts", 13, 1);
    vmad.u32(4);
    vmad.i32(10);
    vmad.i32(4);
    vmad.i32(5);
    vmad.i32(1);

    propertyHeader(vmad, "Ratios", 14, 1);
    vmad.u32(2);
    vmad.f32(1.5f);
    vmad.f32(-2.25f);

    propertyHeader(vmad, "Pairs", 17, 1);
    vmad.u32(1);
    vmad.u32(2);
    propertyHeader(vmad, "Count", 3, 1);
    vmad.i32(5);
    propertyHeader(vmad, "Label", 2, 0);
    wstring(vmad, "hi");

    propertyHeader(vmad, "Holder", 7, 0);
    vmad.u32(1);
    propertyHeader(vmad, "Items", 11, 0);
    vmad.u32(0);

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto details = referenceDetails(backend);
    REQUIRE(details.at("scripts").at("scripts").size() == 1);
    const auto& properties = details.at("scripts").at("scripts")[0].at("properties");
    REQUIRE(properties.size() == 6);

    REQUIRE(properties[0].at("elements").size() == 1);
    CHECK(properties[0].at("elements")[0].at("alias") == 2);
    CHECK(properties[0].at("elements")[0].at("object").at("formKey") == "01ED86:Base.esm");
    CHECK(properties[0].at("value").is_null());
    CHECK(properties[0].at("undecoded") == false);

    CHECK(properties[1].at("elementCount") == 0);
    CHECK(properties[1].at("elements") == nlohmann::json::array());
    CHECK(properties[1].at("undecoded") == false);

    CHECK(properties[2].at("elements") == nlohmann::json::array({10, 4, 5, 1}));

    REQUIRE(properties[3].at("elements").size() == 2);
    CHECK(properties[3].at("elements")[0] == 1.5);
    CHECK(properties[3].at("elements")[1] == -2.25);

    REQUIRE(properties[4].at("elements").size() == 1);
    const auto& members = properties[4].at("elements")[0].at("members");
    REQUIRE(members.size() == 2);
    CHECK(members[0].at("name") == "Count");
    CHECK(members[0].at("typeName") == "int");
    CHECK(members[0].at("value") == 5);
    CHECK(members[1].at("value") == "hi");

    CHECK(properties[5].at("elementCount") == 1);
    CHECK(properties[5].at("elements").is_null());
    CHECK(properties[5].at("undecoded") == true);
}

TEST_CASE("Native reference declared properties come from the compiled script with status and parent") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto scripts = root / "Data" / "Scripts";
    writeScriptPex(scripts, pexObject("GrandScript", "", {pexVariable("DepthVar", "Int", pexInt(4))},
                                      {autoProperty("Depth", "Int", "DepthVar")}));
    writeScriptPex(scripts, pexObject("ParentScript", "GrandScript",
                                      {pexVariable("ParentSpeed", "Int", pexInt(9)),
                                       pexVariable("FlagVar", "Bool", pexBool(true))},
                                      {autoProperty("Speed", "Int", "ParentSpeed"),
                                       literalProperty("Label", "String", pexString("hello")),
                                       autoProperty("Flag", "Bool", "FlagVar")}));
    writeScriptPex(scripts, pexObject("ChildScript", "ParentScript",
                                      {pexVariable("SpeedVar", "Float", pexFloat(1.5f))},
                                      {autoProperty("Speed", "Float", "SpeedVar"),
                                       literalProperty("Mode", "Int", pexInt(3), 2)}));

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root / "Data");
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "ChildScript", 1);
    propertyHeader(vmad, "Speed", 4, 1);
    vmad.f32(2.0f);
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto details = referenceDetails(backend);
    REQUIRE(details.at("scripts").at("scripts").size() == 1);
    const auto& script = details.at("scripts").at("scripts")[0];
    CHECK(script.at("declaredError").is_null());
    CHECK(script.at("parent") == "ParentScript");

    std::vector<std::string> names;
    for (const auto& declared : script.at("declared")) names.push_back(declared.at("name").get<std::string>());
    CHECK(names == std::vector<std::string>{"Speed", "Mode", "Label", "Flag", "Depth"});

    const auto* speed = findDeclared(script, "Speed");
    REQUIRE(speed != nullptr);
    CHECK(speed->at("type") == "Float");
    CHECK(speed->at("auto") == true);
    CHECK(speed->at("readOnly") == false);
    CHECK(speed->at("default") == 1.5);
    CHECK(speed->at("status") == "stored");
    CHECK(speed->at("declaredBy") == "ChildScript");

    const auto* mode = findDeclared(script, "Mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->at("type") == "Int");
    CHECK(mode->at("auto") == false);
    CHECK(mode->at("conditional") == true);
    CHECK(mode->at("readOnly") == true);
    CHECK(mode->at("default") == 3);
    CHECK(mode->at("status") == "default");
    CHECK(mode->at("declaredBy") == "ChildScript");

    const auto* label = findDeclared(script, "Label");
    REQUIRE(label != nullptr);
    CHECK(label->at("conditional") == false);
    CHECK(label->at("default") == "hello");
    CHECK(label->at("status") == "default");
    CHECK(label->at("declaredBy") == "ParentScript");

    const auto* flag = findDeclared(script, "Flag");
    REQUIRE(flag != nullptr);
    CHECK(flag->at("default") == true);
    CHECK(flag->at("declaredBy") == "ParentScript");

    const auto* depth = findDeclared(script, "Depth");
    REQUIRE(depth != nullptr);
    CHECK(depth->at("default") == 4);
    CHECK(depth->at("declaredBy") == "GrandScript");
}

TEST_CASE("Native reference declared chains stop at a cycle and at the depth cap") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto scripts = root / "Data" / "Scripts";
    writeScriptPex(scripts, pexObject("CycleA", "CycleB", {pexVariable("AlphaVar", "Int", pexInt(1))},
                                      {autoProperty("Alpha", "Int", "AlphaVar")}));
    writeScriptPex(scripts, pexObject("CycleB", "CycleA", {pexVariable("BetaVar", "Int", pexInt(2))},
                                      {autoProperty("Beta", "Int", "BetaVar")}));

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root / "Data");
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "CycleA", 0);
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto details = referenceDetails(backend);
    const auto& cycle = details.at("scripts").at("scripts")[0];
    CHECK(cycle.at("declaredError").is_null());
    CHECK(cycle.at("parent") == "CycleB");
    CHECK(cycle.at("declared").size() == 2);
    CHECK(findDeclared(cycle, "Alpha") != nullptr);
    CHECK(findDeclared(cycle, "Beta") != nullptr);
}

namespace {

void writeChain(const std::filesystem::path& scripts, const std::string& prefix, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = prefix + std::to_string(i);
        const auto parent = i + 1 < count ? prefix + std::to_string(i + 1) : std::string{};
        const auto variable = "Var" + std::to_string(i);
        const auto value = pexInt(static_cast<std::int32_t>(i));
        writeScriptPex(scripts, pexObject(name, parent, {pexVariable(variable, "Int", value)},
                                          {autoProperty("Prop" + std::to_string(i), "Int", variable)}));
    }
}

}  // namespace

TEST_CASE("Native reference declared chain of eight scripts resolves in full") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeChain(root / "Data" / "Scripts", "Chain", 8);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root / "Data");
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "Chain0", 0);
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto script = referenceDetails(backend).at("scripts").at("scripts")[0];
    CHECK(script.at("declaredError").is_null());
    CHECK(script.at("declared").size() == 8);
    CHECK(findDeclared(script, "Prop7") != nullptr);
}

TEST_CASE("Native reference declared chain of nine scripts stops at the depth cap") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeChain(root / "Data" / "Scripts", "Deep", 9);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root / "Data");
    fo4::test::ByteWriter vmad;
    scriptHeader(vmad, 2, "Deep0", 0);
    loadPlugin(backend, root, scriptPlugin(vmad.bytes));

    const auto script = referenceDetails(backend).at("scripts").at("scripts")[0];
    CHECK(script.at("declaredError") == "parent chain is deeper than 8");
    CHECK(script.at("declared").size() == 8);
}

TEST_CASE("RadicleReferenceReal.DeclaredProbe") {
    const char* env = std::getenv("FO4_DATA_DIR");
    if (env == nullptr || *env == '\0') SKIP("FO4_DATA_DIR is not set");
    const std::filesystem::path data = env;
    const auto master = data / "Fallout4.esm";
    if (!std::filesystem::exists(master)) SKIP("Fallout4.esm is not under FO4_DATA_DIR");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Fallout4.esm", master)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto probe = [&](const std::string& id) {
        const auto details = parseStringResult(
            backend.getReferenceDetails(nlohmann::json::array({id + ":Fallout4.esm"})));
        printProbe(id, details);
        return details;
    };

    const auto mq101 = probe("0CA2AC");
    if (mq101.contains("scripts") && mq101.at("scripts").is_object() && !mq101.at("scripts").at("scripts").empty()) {
        CHECK(mq101.at("scripts").at("scripts")[0].at("declaredError").is_null());
    }

    const auto trigger = probe("19D323");
    const auto myQuest = propertyValue(trigger, "myQuest");
    CHECK(myQuest.is_object());
    if (myQuest.is_object()) {
        CHECK(myQuest.at("alias") == -1);
        CHECK(myQuest.at("object").at("formKey") == "01ED86:Fallout4.esm");
    }
    CHECK(propertyValue(trigger, "preReqStage") == 500);
    CHECK(propertyValue(trigger, "StageToSet") == 632);

    probe("02B910");
    probe("197B99");
}
