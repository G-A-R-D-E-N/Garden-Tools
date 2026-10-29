#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4/forms/proj_expl_hazd.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_proj_expl_hazd_" + std::to_string(id));
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
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> pluginWithRecord(std::string_view signature,
                                        std::uint32_t formId = 0x00000800,
                                        std::uint32_t nextFormId = 0x801,
                                        std::uint32_t tes4Flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, formId);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

const fo4::Subrecord* findSub(const fo4::Record& record, std::string_view signature) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == signature) return &sub;
    }
    return nullptr;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ProjExplHazd"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

nlohmann::json parseResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
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

void savePlugin(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
}

}

TEST_CASE("Native CreateRecord writes a measured PROJ DNAM and remaps a typed EXPL slot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("EXPL"));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"editorId", "GardenProjectile"},
        {"speed", 12000.0},
        {"range", 4096.0},
        {"explosionType", "000800:Patch.esp"},
        {"tracerFrequency", 4},
        {"muzzleFlashModel", "Effects\\MuzPistolSmall01.nif"},
        {"soundLevel", 2}
    };
    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PROJ", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto* record = fo4::find_record(disk, key);
    REQUIRE(record != nullptr);
    const auto* dnam = findSub(*record, "DNAM");
    REQUIRE(dnam != nullptr);
    CHECK(dnam->data.size() == fo4::forms::k_proj_dnam_size);
    const auto* data = findSub(*record, "DATA");
    REQUIRE(data != nullptr);
    CHECK(data->data.empty());

    const auto decoded = fo4::forms::decode_projectile(disk, key);
    CHECK(decoded.editor_id == "GardenProjectile");
    CHECK(decoded.speed == 12000.0f);
    CHECK(decoded.range == 4096.0f);
    CHECK(decoded.explosion_type == fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(decoded.tracer_frequency == 4u);
    CHECK(decoded.muzzle_flash_model == "Effects\\MuzPistolSmall01.nif");
    CHECK(decoded.sound_level == 2u);
    CHECK(disk.header.next_form_id == 0x802u);

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000801:Patch.esp", "PROJ\\Speed", "9000"
    })).has_value());
    const auto beforeSave = fo4::forms::decode_projectile(
        fo4::read_plugin_file(path), key);
    CHECK(beforeSave.speed == 12000.0f);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto after = fo4::forms::decode_projectile(
        fo4::read_plugin_file(path), key);
    CHECK(after.speed == 9000.0f);
}

TEST_CASE("Native CreateRecord writes a measured EXPL DATA payload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("PROJ"));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const nlohmann::json payload{
        {"editorId", "GardenExplosion"},
        {"spawnProjectile", "000800:Patch.esp"},
        {"force", 1024.0},
        {"damage", 25.0},
        {"innerRadius", 300.0},
        {"outerRadius", 1024.0},
        {"soundLevel", 2},
        {"staggerMagnitude", 3},
        {"spawnProjectileVector", nlohmann::json::array({0.0, 1.0, -0.25})},
        {"projectileSpread", 180.0},
        {"projectileCount", 15}
    };
    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EXPL", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto* record = fo4::find_record(disk, key);
    REQUIRE(record != nullptr);
    const auto* data = findSub(*record, "DATA");
    REQUIRE(data != nullptr);
    CHECK(data->data.size() == fo4::forms::k_expl_data_size);

    const auto decoded = fo4::forms::decode_explosion(disk, key);
    CHECK(decoded.spawn_projectile == fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(decoded.force == 1024.0f);
    CHECK(decoded.inner_radius == 300.0f);
    CHECK(decoded.sound_level == 2u);
    CHECK(decoded.stagger_magnitude == 3u);
    CHECK(decoded.spawn_projectile_vector[1] == 1.0f);
    CHECK(decoded.spawn_projectile_vector[2] == -0.25f);
    CHECK(decoded.projectile_count == 15u);
}

TEST_CASE("Native CreateRecord requires a HAZD model and writes the attested MODT default") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("SPEL"));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto missingModel = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "HAZD",
            nlohmann::json{
                {"editorId", "NoModel"},
                {"spell", "000800:Patch.esp"}
            }
        }));
    REQUIRE_FALSE(missingModel.has_value());
    CHECK(missingModel.error().type == "InvalidRecordCreationPayload");
    CHECK(fo4::read_plugin_file(path).header.next_form_id == 0x801u);

    const nlohmann::json payload{
        {"editorId", "GardenHazard"},
        {"model", "Effects\\FXFireMedSmokey01.nif"},
        {"limit", 20},
        {"radius", 351.0},
        {"lifetime", 30.0},
        {"targetInterval", 0.1},
        {"flags", 36},
        {"spell", "000800:Patch.esp"},
        {"taperWeight", 1.0}
    };
    const auto beforeCreate = readBytes(path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "HAZD", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    CHECK(readBytes(path) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto* record = fo4::find_record(disk, key);
    REQUIRE(record != nullptr);
    const auto* dnam = findSub(*record, "DNAM");
    REQUIRE(dnam != nullptr);
    CHECK(dnam->data.size() == fo4::forms::k_hazd_dnam_size);
    const auto* modt = findSub(*record, "MODT");
    REQUIRE(modt != nullptr);
    CHECK(modt->data == fo4::forms::empty_model_texture_data());

    const auto decoded = fo4::forms::decode_hazard(disk, key);
    CHECK(decoded.model == "Effects\\FXFireMedSmokey01.nif");
    CHECK(decoded.limit == 20u);
    CHECK(decoded.radius == 351.0f);
    CHECK(decoded.flags == 36u);
    CHECK(decoded.spell == fo4::FormKey::parse("000800:Patch.esp"));
}

TEST_CASE("Native PROJ creation refuses a loaded FormKey of the wrong type without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("SPEL"));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "BadLight"},
                {"light", "000800:Patch.esp"}
            }
        }));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "InvalidTypedFormKey");
    CHECK(readBytes(path) == before);
    CHECK(fo4::find_record(
        fo4::read_plugin_file(path),
        fo4::FormKey::parse("000801:Patch.esp")) == nullptr);
}

TEST_CASE("Native PROJ empty editorId is refused as ProjExplHazdCreateFailed without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("EXPL"));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", ""},
                {"explosionType", "000800:Patch.esp"}
            }
        }));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "ProjExplHazdCreateFailed");
    CHECK(created.error().message.find("editor ID is empty") != std::string::npos);
    CHECK(readBytes(path) == before);
    CHECK(fo4::find_record(
        fo4::read_plugin_file(path),
        fo4::FormKey::parse("000801:Patch.esp")) == nullptr);
}

TEST_CASE("Native CreateRecord adds a loaded master exactly once and remaps the slot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(
        masterPath,
        pluginWithRecord("EXPL", 0x00000800, 0x801, fo4::RecordFlags::master));
    writeBytes(patchPath, pluginWithRecord("MISC", 0x00000810, 0x811));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {
                {"name", "Master.esm"},
                {"path", masterPath.string()},
                {"source", "MasterMod"},
                {"resolved", true}
            },
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "PatchMod"},
                {"resolved", true}
            }
        })}}).has_value());

    const auto beforeCreate = readBytes(patchPath);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMaster"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    REQUIRE(created.at("formKey") == "000811:Patch.esp");
    CHECK(readBytes(patchPath) == beforeCreate);
    savePlugin(backend);

    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto* local = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000810});
    const auto decoded = fo4::forms::decode_projectile(
        disk, fo4::FormKey::parse("000811:Patch.esp"));
    CHECK(decoded.explosion_type == fo4::FormKey::parse("000800:Master.esm"));

    const auto beforeSecondCreate = readBytes(patchPath);
    const auto second = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "FromMasterAgain"},
                {"explosionType", "000800:Master.esm"}
            }
        })));
    CHECK(second.at("formKey") == "000812:Patch.esp");
    CHECK(readBytes(patchPath) == beforeSecondCreate);
    savePlugin(backend);
    const auto again = fo4::read_plugin_file(patchPath);
    REQUIRE(again.header.masters.size() == 1u);
    CHECK(again.header.masters[0].key.filename() == "Master.esm");
}

TEST_CASE("Native CreateRecord refuses an unloaded FormKey owner without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("MISC"));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{
                {"editorId", "MissingOwner"},
                {"explosionType", "000800:Missing.esm"}
            }
        }));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "InvalidTypedFormKey");
    CHECK(readBytes(path) == before);
    CHECK(fo4::find_record(
        fo4::read_plugin_file(path),
        fo4::FormKey::parse("000801:Patch.esp")) == nullptr);
}

TEST_CASE("Native SetField edits an embedded PROJ FormLink") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("LIGH"));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "FormLinkTarget"}, {"light", "000800:Patch.esp"}}
        })));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000801:Patch.esp", "PROJ\\Light", "000800:Patch.esp"
    }));
    REQUIRE(result.has_value());
}

TEST_CASE("Native FormLink SetField rejects missing and non-string values without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("LIGH"));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "FormLinkBoundary"}, {"light", "000800:Patch.esp"}}
        })));
    REQUIRE(created.at("formKey") == key.to_string());
    savePlugin(backend);

    const auto beforeBytes = readBytes(path);
    const auto invalidValues = std::array{
        nlohmann::json::array({"Patch.esp", key.to_string(), "PROJ\\Light", 123}),
        nlohmann::json::array({"Patch.esp", key.to_string(), "PROJ\\Light"})
    };
    for (const auto& arguments : invalidValues) {
        const auto result = backend.setFieldValue(arguments);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "InvalidFormLinkValue");
        CHECK(readBytes(path) == beforeBytes);
        CHECK(fo4::forms::decode_projectile(
                  fo4::read_plugin_file(path), key)
                  .light == fo4::FormKey::parse("000800:Patch.esp"));
    }
}
