#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/armor.hpp>
#include <fo4/plugin/reader.hpp>
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
    const auto model = fo4::forms::decode_armor(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(model.male_world_model.has_value());
    CHECK(*model.male_world_model == "Meshes\\Armor\\Male.nif");
    REQUIRE(model.male_icon.has_value());
    CHECK(*model.male_icon == "Textures\\Interface\\Male.dds");
    REQUIRE(model.male_message_icon.has_value());
    CHECK(*model.male_message_icon == "Textures\\Interface\\MaleMessage.dds");
    REQUIRE(model.female_world_model.has_value());
    CHECK(*model.female_world_model == "Meshes\\Armor\\Female.nif");
    REQUIRE(model.female_icon.has_value());
    CHECK(*model.female_icon == "Textures\\Interface\\Female.dds");
    REQUIRE(model.female_message_icon.has_value());
    CHECK(*model.female_message_icon == "Textures\\Interface\\FemaleMessage.dds");
    CHECK(model.value == 125);
    CHECK(model.weight == 4.5f);
    CHECK(model.health == 900u);
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
    const auto model = fo4::forms::decode_armor_addon(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(model.male_biped_model.has_value());
    CHECK(*model.male_biped_model == "Meshes\\Armor\\MaleBiped.nif");
    REQUIRE(model.female_biped_model.has_value());
    CHECK(*model.female_biped_model == "Meshes\\Armor\\FemaleBiped.nif");
    REQUIRE(model.male_first_person_model.has_value());
    CHECK(*model.male_first_person_model == "Meshes\\Armor\\Male1st.nif");
    REQUIRE(model.female_first_person_model.has_value());
    CHECK(*model.female_first_person_model == "Meshes\\Armor\\Female1st.nif");
    CHECK(model.male_priority == 3u);
    CHECK(model.female_priority == 4u);
    CHECK(model.weapon_adjust == 1.25f);
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
    const auto beforeModel = fo4::forms::decode_armor(
        beforeSave, fo4::FormKey::parse(key));
    CHECK_FALSE(beforeModel.male_world_model.has_value());
    CHECK_FALSE(beforeModel.male_icon.has_value());
    CHECK_FALSE(beforeModel.female_world_model.has_value());
    CHECK_FALSE(beforeModel.female_icon.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto model = fo4::forms::decode_armor(
        disk, fo4::FormKey::parse(key));
    REQUIRE(model.male_world_model.has_value());
    CHECK(*model.male_world_model == "Meshes\\Armor\\Male.nif");
    REQUIRE(model.male_icon.has_value());
    CHECK(*model.male_icon == "Textures\\Male.dds");
    REQUIRE(model.female_world_model.has_value());
    CHECK(*model.female_world_model == "Meshes\\Armor\\Female.nif");
    REQUIRE(model.female_icon.has_value());
    CHECK(*model.female_icon == "Textures\\Female.dds");
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
    const auto model = fo4::forms::decode_armor_addon(
        disk, fo4::FormKey::parse(key));
    REQUIRE(model.male_biped_model.has_value());
    CHECK(*model.male_biped_model == "Meshes\\Armor\\MaleBiped.nif");
    REQUIRE(model.female_biped_model.has_value());
    CHECK(*model.female_biped_model == "Meshes\\Armor\\FemaleBiped.nif");
    REQUIRE(model.male_first_person_model.has_value());
    CHECK(*model.male_first_person_model == "Meshes\\Armor\\MaleFirst.nif");
    REQUIRE(model.female_first_person_model.has_value());
    CHECK(*model.female_first_person_model == "Meshes\\Armor\\FemaleFirst.nif");
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
