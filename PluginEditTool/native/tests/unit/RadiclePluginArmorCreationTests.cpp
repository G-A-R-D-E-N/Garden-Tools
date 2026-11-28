#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_armor_creation_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

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

std::vector<std::byte> legacyArmorWithBodyData() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "ARMO", 0);
    const auto record = fo4::test::begin_record(out, "ARMO", 0, 0x00000800);
    const std::array<std::byte, 8> body{};
    fo4::test::write_subrecord(out, "BOD2", body);
    fo4::test::ByteWriter data;
    data.u32(10);
    data.f32(2.0f);
    data.u32(100);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ArmorTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

std::optional<std::string> rowText(const fo4::Plugin& plugin, const std::string& key, std::string_view path) {
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto field = fo4::schema::scalar_field_descriptor(path);
    REQUIRE(field.has_value());
    return fo4::record_scalar_text(plugin, *record, *field, 0);
}

float rowFloat(const fo4::Plugin& plugin, const std::string& key, std::string_view path) {
    const auto text = rowText(plugin, key, path);
    REQUIRE(text.has_value());
    return std::stof(*text);
}

long rowInteger(const fo4::Plugin& plugin, const std::string& key, std::string_view path) {
    const auto text = rowText(plugin, key, path);
    REQUIRE(text.has_value());
    return std::stol(*text);
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("PluginEditTool creates ARMO male and female model icon structures") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"editorId", "GardenArmor"},
        {"maleWorldModel", "Meshes\\Armor\\Male.nif"},
        {"maleIcon", "Textures\\Interface\\Male.dds"},
        {"maleMessageIcon", "Textures\\Interface\\MaleMessage.dds"},
        {"femaleWorldModel", "Meshes\\Armor\\Female.nif"},
        {"femaleIcon", "Textures\\Interface\\Female.dds"},
        {"femaleMessageIcon", "Textures\\Interface\\FemaleMessage.dds"},
        {"value", 125},
        {"weight", 4.5},
        {"health", 900}
    };
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "ARMO", payload})));
    REQUIRE(created.at("formKey") == "000800:Patch.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const std::string key = "000800:Patch.esp";
    CHECK(rowText(disk, key, "ARMO\\MaleWorldModel") == "Meshes\\Armor\\Male.nif");
    CHECK(rowText(disk, key, "ARMO\\MaleIcon") == "Textures\\Interface\\Male.dds");
    CHECK(rowText(disk, key, "ARMO\\MaleMessageIcon") == "Textures\\Interface\\MaleMessage.dds");
    CHECK(rowText(disk, key, "ARMO\\FemaleWorldModel") == "Meshes\\Armor\\Female.nif");
    CHECK(rowText(disk, key, "ARMO\\FemaleIcon") == "Textures\\Interface\\Female.dds");
    CHECK(rowText(disk, key, "ARMO\\FemaleMessageIcon") == "Textures\\Interface\\FemaleMessage.dds");
    CHECK(rowInteger(disk, key, "ARMO\\Value") == 125);
    CHECK(rowFloat(disk, key, "ARMO\\Weight") == 4.5f);
    CHECK(rowInteger(disk, key, "ARMO\\Health") == 900);
}

TEST_CASE("PluginEditTool creates ARMA biped and first-person gendered models") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"editorId", "GardenArmorAddon"},
        {"maleBipedModel", "Meshes\\Armor\\MaleBiped.nif"},
        {"femaleBipedModel", "Meshes\\Armor\\FemaleBiped.nif"},
        {"maleFirstPersonModel", "Meshes\\Armor\\Male1st.nif"},
        {"femaleFirstPersonModel", "Meshes\\Armor\\Female1st.nif"},
        {"malePriority", 3},
        {"femalePriority", 4},
        {"maleWeightSlider", 2},
        {"femaleWeightSlider", 2},
        {"detectionSoundValue", 7},
        {"weaponAdjust", 1.25}
    };
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "ARMA", payload})));
    REQUIRE(created.at("formKey") == "000800:Patch.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const std::string key = "000800:Patch.esp";
    CHECK(rowText(disk, key, "ARMA\\BipedMaleModel") == "Meshes\\Armor\\MaleBiped.nif");
    CHECK(rowText(disk, key, "ARMA\\BipedFemaleModel") == "Meshes\\Armor\\FemaleBiped.nif");
    CHECK(rowText(disk, key, "ARMA\\FirstPersonMaleModel") == "Meshes\\Armor\\Male1st.nif");
    CHECK(rowText(disk, key, "ARMA\\FirstPersonFemaleModel") == "Meshes\\Armor\\Female1st.nif");
    CHECK(rowInteger(disk, key, "ARMA\\MalePriority") == 3);
    CHECK(rowInteger(disk, key, "ARMA\\FemalePriority") == 4);
    CHECK(rowInteger(disk, key, "ARMA\\WeightSliderMale") == 2);
    CHECK(rowInteger(disk, key, "ARMA\\WeightSliderFemale") == 2);
    CHECK(rowInteger(disk, key, "ARMA\\DetectionSoundValue") == 7);
    CHECK(rowFloat(disk, key, "ARMA\\WeaponAdjust") == 1.25f);
}

TEST_CASE("PluginEditTool fills missing ARMO gendered fields after creation") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "ARMO", nlohmann::json::object()})));
    const auto key = created.at("formKey").get<std::string>();
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMO\\MaleWorldModel", "Meshes\\Armor\\Male.nif"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMO\\MaleIcon", "Textures\\Male.dds"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMO\\FemaleWorldModel", "Meshes\\Armor\\Female.nif"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMO\\FemaleIcon", "Textures\\Female.dds"
    })).has_value());

    const auto beforeSave = fo4::read_plugin_file(path);
    CHECK_FALSE(rowText(beforeSave, key, "ARMO\\MaleWorldModel").has_value());
    CHECK_FALSE(rowText(beforeSave, key, "ARMO\\MaleIcon").has_value());
    CHECK_FALSE(rowText(beforeSave, key, "ARMO\\FemaleWorldModel").has_value());
    CHECK_FALSE(rowText(beforeSave, key, "ARMO\\FemaleIcon").has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(rowText(disk, key, "ARMO\\MaleWorldModel") == "Meshes\\Armor\\Male.nif");
    CHECK(rowText(disk, key, "ARMO\\MaleIcon") == "Textures\\Male.dds");
    CHECK(rowText(disk, key, "ARMO\\FemaleWorldModel") == "Meshes\\Armor\\Female.nif");
    CHECK(rowText(disk, key, "ARMO\\FemaleIcon") == "Textures\\Female.dds");
}

TEST_CASE("PluginEditTool fills missing ARMA biped and first-person models") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "ARMA", nlohmann::json::object()})));
    const auto key = created.at("formKey").get<std::string>();

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMA\\MaleBipedModel", "Meshes\\Armor\\MaleBiped.nif"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMA\\FemaleBipedModel", "Meshes\\Armor\\FemaleBiped.nif"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMA\\MaleFirstPersonModel", "Meshes\\Armor\\MaleFirst.nif"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "ARMA\\FemaleFirstPersonModel", "Meshes\\Armor\\FemaleFirst.nif"
    })).has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(rowText(disk, key, "ARMA\\BipedMaleModel") == "Meshes\\Armor\\MaleBiped.nif");
    CHECK(rowText(disk, key, "ARMA\\BipedFemaleModel") == "Meshes\\Armor\\FemaleBiped.nif");
    CHECK(rowText(disk, key, "ARMA\\FirstPersonMaleModel") == "Meshes\\Armor\\MaleFirst.nif");
    CHECK(rowText(disk, key, "ARMA\\FirstPersonFemaleModel") == "Meshes\\Armor\\FemaleFirst.nif");
}

TEST_CASE("PluginEditTool refuses missing armor insertion around unmodeled legacy data") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, legacyArmorWithBodyData());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp",
        "000800:Patch.esp",
        "ARMO\\FemaleWorldModel",
        "Meshes\\Armor\\Female.nif"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArmorWriteFailed");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Invalid ARMA creation is transactional through PluginEditTool") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"maleBipedModel", "Meshes\\Armor\\Male.nif"},
        {"maleWeightSlider", 128}
    };
    const auto created = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "ARMA", payload}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "ArmorCreateFailed");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool refuses an ARMO EditorID that Windows-1252 cannot store without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto before = readBytes(path);
    const auto refused = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "ARMO", nlohmann::json{{"editorId", "\xe6\x97\xa5"}}}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "ArmorCreateFailed");
    const auto pending = parseResult(backend.invoke("appInterop.GetPlugins", nlohmann::json::array()));
    CHECK(pending.at(0).at("Pending") == false);
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool stores an ARMO UTF-8 EditorID as Windows-1252") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto created = parseResult(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "ARMO", nlohmann::json{{"editorId", "Caf\xC3\xA9"}}})));
    REQUIRE(created.at("formKey") == "000800:Patch.esp");
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    const auto edid = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& sub) { return sub.signature == fo4::sig_edid; });
    REQUIRE(edid != record->subrecords.end());
    CHECK(edid->data == std::vector<std::byte>{
        std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}});
}

TEST_CASE("PluginEditTool stores an ARMO model path edit as Windows-1252 and refuses a mismatched record type") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto armo = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "ARMO", nlohmann::json::object()}))).at("formKey").get<std::string>();

    const auto mismatched = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", armo, "ARMA\\MaleBipedModel", "Meshes\\Armor\\Male.nif"}));
    REQUIRE_FALSE(mismatched.has_value());
    CHECK(mismatched.error().type == "ArmorWriteFailed");

    const auto unencodable = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", armo, "ARMO\\MaleWorldModel", "Meshes\\Armor\\\xE6\x97\xA5.nif"}));
    REQUIRE_FALSE(unencodable.has_value());
    CHECK(unencodable.error().type == "ArmorWriteFailed");

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", armo, "ARMO\\MaleWorldModel", "Meshes\\Armor\\Caf\xC3\xA9.nif"})).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse(armo));
    REQUIRE(record != nullptr);
    const auto mod2 = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& sub) { return sub.signature == fo4::Signature::from_string("MOD2"); });
    REQUIRE(mod2 != record->subrecords.end());
    const std::string stored = "Meshes\\Armor\\Caf\xE9.nif";
    std::vector<std::byte> expected;
    for (const char ch : stored) expected.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    expected.push_back(std::byte{0});
    CHECK(mod2->data == expected);
}

TEST_CASE("PluginEditTool refuses an ARMO model path with an embedded NUL byte") {
    // Invariant: an embedded NUL never reaches the string writer, where it would truncate the saved model path at the NUL.
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, emptyPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto armo = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "ARMO", nlohmann::json::object()})))
                          .at("formKey")
                          .get<std::string>();
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = readBytes(path);

    std::string value = "Meshes\\Armor\\Male";
    value.push_back('\0');
    value += ".nif";
    const auto refused = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", armo, "ARMO\\MaleWorldModel", value}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidFieldValue");
    CHECK(refused.error().message == "Armor model and icon fields cannot contain embedded NUL bytes");

    const auto pending = parseResult(backend.invoke("appInterop.GetPlugins", nlohmann::json::array()));
    CHECK(pending.at(0).at("Pending") == false);
    CHECK(readBytes(path) == before);
}
