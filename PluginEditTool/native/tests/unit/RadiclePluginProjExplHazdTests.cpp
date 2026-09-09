#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
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
#include <fo4/schema/formlink_lists.hpp>
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

std::vector<std::byte> pluginWithPayloadRecord(std::string_view signature,
                                               std::string_view subrecord,
                                               std::size_t payloadSize,
                                               std::uint32_t formId = 0x00000800,
                                               std::uint32_t nextFormId = 0x801) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, formId);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
    std::vector<std::byte> payload(payloadSize, std::byte{0xA5});
    fo4::test::write_subrecord(out, subrecord, payload);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> pluginWithAllAuditedFormLinkRecords() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x903);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto addSimple = [&](std::string_view signature, std::uint32_t formId) {
        const auto group = fo4::test::begin_group(out, signature, 0);
        const auto record = fo4::test::begin_record(out, signature, 0, formId);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Target\0", 7});
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    const auto addPayload = [&](std::string_view signature, std::string_view subrecord,
                                std::size_t payloadSize, std::uint32_t formId) {
        const auto group = fo4::test::begin_group(out, signature, 0);
        const auto record = fo4::test::begin_record(out, signature, 0, formId);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Subject\0", 8});
        std::vector<std::byte> payload(payloadSize, std::byte{0});
        fo4::test::write_subrecord(out, subrecord, payload);
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };

    addSimple("LIGH", 0x800);
    addSimple("EXPL", 0x801);
    addSimple("SNDR", 0x802);
    addSimple("WEAP", 0x803);
    addSimple("TXST", 0x804);
    addSimple("COLL", 0x805);
    addSimple("PROJ", 0x806);
    addSimple("IPDS", 0x807);
    addSimple("ACTI", 0x808);
    addSimple("SPEL", 0x809);
    addSimple("ENCH", 0x80A);
    addPayload("PROJ", "DNAM", 93, 0x900);
    addPayload("EXPL", "DATA", 84, 0x901);
    addPayload("HAZD", "DNAM", 52, 0x902);
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
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PROJ", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

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
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EXPL", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

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
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "HAZD", payload})));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");

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

TEST_CASE("Native CreateRecord rejects payload scalars outside the generated schema") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("EXPL"));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto soundLevel = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "TooLoud"}, {"soundLevel", 5}}
        }));
    REQUIRE_FALSE(soundLevel.has_value());
    CHECK(soundLevel.error().type == "InvalidRecordCreationPayload");
    CHECK(soundLevel.error().message.find("PROJ\\SoundLevel") != std::string::npos);

    const auto stagger = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EXPL",
            nlohmann::json{{"editorId", "TooStaggered"}, {"staggerMagnitude", 5}}
        }));
    REQUIRE_FALSE(stagger.has_value());
    CHECK(stagger.error().type == "InvalidRecordCreationPayload");
    CHECK(stagger.error().message.find("EXPL\\StaggerMagnitude") != std::string::npos);

    const auto count = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "EXPL",
            nlohmann::json{{"editorId", "TooMany"}, {"projectileCount", 65536}}
        }));
    REQUIRE_FALSE(count.has_value());
    CHECK(count.error().type == "InvalidRecordCreationPayload");
    CHECK(count.error().message.find("EXPL\\ProjectileCount") != std::string::npos);

    const auto speed = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "PROJ",
            nlohmann::json{{"editorId", "TooFast"}, {"speed", 2000000000.0}}
        }));
    REQUIRE_FALSE(speed.has_value());
    CHECK(speed.error().type == "InvalidRecordCreationPayload");
    CHECK(speed.error().message.find("PROJ\\Speed") != std::string::npos);
    CHECK(readBytes(path) == before);

    writeBytes(path, pluginWithRecord("SPEL"));
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "ProjExplHazd"},
            {"resolved", true}
        }})}}).has_value());
    const auto limit = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp",
            "HAZD",
            nlohmann::json{
                {"editorId", "TooManyHazards"},
                {"model", "Effects\\FXFireMedSmokey01.nif"},
                {"limit", 65536}
            }
        }));
    REQUIRE_FALSE(limit.has_value());
    CHECK(limit.error().type == "InvalidRecordCreationPayload");
    CHECK(limit.error().message.find("HAZD\\Limit") != std::string::npos);
    CHECK(readBytes(path) == pluginWithRecord("SPEL"));
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

    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto* local = fo4::find_record(disk, fo4::FormKey::parse("000810:Patch.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000810});
    const auto decoded = fo4::forms::decode_projectile(
        disk, fo4::FormKey::parse("000811:Patch.esp"));
    CHECK(decoded.explosion_type == fo4::FormKey::parse("000800:Master.esm"));

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

TEST_CASE("Generated PROJ EXPL HAZD FormLink descriptors match the audited slots") {
    struct Expected {
        std::string_view record;
        std::string_view path;
        std::string_view subrecord;
        std::size_t offset;
        std::array<std::string_view, 2> targets;
        std::size_t targetCount;
    };
    const std::array expected{
        Expected{"PROJ", "PROJ\\Light", "DNAM", 16, {"LIGH", {}}, 1},
        Expected{"PROJ", "PROJ\\MuzzleFlashLight", "DNAM", 20, {"LIGH", {}}, 1},
        Expected{"PROJ", "PROJ\\ExplosionType", "DNAM", 32, {"EXPL", {}}, 1},
        Expected{"PROJ", "PROJ\\ActiveSoundLoop", "DNAM", 36, {"SNDR", {}}, 1},
        Expected{"PROJ", "PROJ\\CountdownSound", "DNAM", 52, {"SNDR", {}}, 1},
        Expected{"PROJ", "PROJ\\DeactivateSound", "DNAM", 56, {"SNDR", {}}, 1},
        Expected{"PROJ", "PROJ\\DefaultWeaponSource", "DNAM", 60, {"WEAP", {}}, 1},
        Expected{"PROJ", "PROJ\\DecalData", "DNAM", 80, {"TXST", {}}, 1},
        Expected{"PROJ", "PROJ\\CollisionLayer", "DNAM", 84, {"COLL", {}}, 1},
        Expected{"EXPL", "EXPL\\Light", "DATA", 0, {"LIGH", {}}, 1},
        Expected{"EXPL", "EXPL\\Sound1", "DATA", 4, {"SNDR", {}}, 1},
        Expected{"EXPL", "EXPL\\Sound2", "DATA", 8, {"SNDR", {}}, 1},
        Expected{"EXPL", "EXPL\\ImpactDataSet", "DATA", 12, {"IPDS", {}}, 1},
        Expected{"EXPL", "EXPL\\ImpactPlacedObject", "DATA", 16, {"ACTI", "WEAP"}, 43},
        Expected{"EXPL", "EXPL\\SpawnProjectile", "DATA", 20, {"PROJ", {}}, 1},
        Expected{"HAZD", "HAZD\\Spell", "DNAM", 24, {"SPEL", "ENCH"}, 2},
        Expected{"HAZD", "HAZD\\Light", "DNAM", 28, {"LIGH", {}}, 1},
        Expected{"HAZD", "HAZD\\ImpactDataSet", "DNAM", 32, {"IPDS", {}}, 1},
        Expected{"HAZD", "HAZD\\Sound", "DNAM", 36, {"SNDR", {}}, 1},
    };

    REQUIRE(fo4::schema::formlink_fields(fo4::Signature::from_string("PROJ")).size() == 9);
    REQUIRE(fo4::schema::formlink_fields(fo4::Signature::from_string("EXPL")).size() == 6);
    REQUIRE(fo4::schema::formlink_fields(fo4::Signature::from_string("HAZD")).size() == 4);
    for (const auto& item : expected) {
        const auto descriptor = fo4::schema::formlink_field_descriptor(item.path);
        REQUIRE(descriptor.has_value());
        CHECK(descriptor->record == fo4::Signature::from_string(item.record));
        CHECK(descriptor->subrecord == fo4::Signature::from_string(item.subrecord));
        CHECK(descriptor->offset == item.offset);
        CHECK(descriptor->nullable);
        CHECK(descriptor->target_count == item.targetCount);
        CHECK(fo4::schema::formlink_field_target_allowed(
            *descriptor, fo4::Signature::from_string(item.targets[0])));
        if (item.targetCount == 2) {
            CHECK(fo4::schema::formlink_field_target_allowed(
                *descriptor, fo4::Signature::from_string(item.targets[1])));
        }
    }
}

TEST_CASE("Native SetField writes every audited PROJ EXPL HAZD FormLink slot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithAllAuditedFormLinkRecords());
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    struct Write {
        std::string_view record;
        std::string_view key;
        std::string_view path;
        std::string_view target;
        std::uint32_t raw;
    };
    const std::array writes{
        Write{"PROJ", "000900:Patch.esp", "PROJ\\Light", "000800:Patch.esp", 0x800},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\MuzzleFlashLight", "000800:Patch.esp", 0x800},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\ExplosionType", "000801:Patch.esp", 0x801},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\ActiveSoundLoop", "000802:Patch.esp", 0x802},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\CountdownSound", "000802:Patch.esp", 0x802},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\DeactivateSound", "000802:Patch.esp", 0x802},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\DefaultWeaponSource", "000803:Patch.esp", 0x803},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\DecalData", "000804:Patch.esp", 0x804},
        Write{"PROJ", "000900:Patch.esp", "PROJ\\CollisionLayer", "000805:Patch.esp", 0x805},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\Light", "000800:Patch.esp", 0x800},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\Sound1", "000802:Patch.esp", 0x802},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\Sound2", "000802:Patch.esp", 0x802},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\ImpactDataSet", "000807:Patch.esp", 0x807},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\ImpactPlacedObject", "000808:Patch.esp", 0x808},
        Write{"EXPL", "000901:Patch.esp", "EXPL\\SpawnProjectile", "000806:Patch.esp", 0x806},
        Write{"HAZD", "000902:Patch.esp", "HAZD\\Spell", "000809:Patch.esp", 0x809},
        Write{"HAZD", "000902:Patch.esp", "HAZD\\Light", "000800:Patch.esp", 0x800},
        Write{"HAZD", "000902:Patch.esp", "HAZD\\ImpactDataSet", "000807:Patch.esp", 0x807},
        Write{"HAZD", "000902:Patch.esp", "HAZD\\Sound", "000802:Patch.esp", 0x802},
    };

    for (const auto& write : writes) {
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", write.key, write.path, write.target
        })).has_value());
        const auto disk = fo4::read_plugin_file(path);
        const auto* record = fo4::find_record(disk, fo4::FormKey::parse(write.key));
        REQUIRE(record != nullptr);
        const auto* subrecord = findSub(*record, write.record == "EXPL" ? "DATA" : "DNAM");
        REQUIRE(subrecord != nullptr);
        const auto descriptor = fo4::schema::formlink_field_descriptor(write.path);
        REQUIRE(descriptor.has_value());
        REQUIRE(descriptor->offset + 4 <= subrecord->data.size());
        std::uint32_t raw = 0;
        std::memcpy(&raw, subrecord->data.data() + descriptor->offset, sizeof(raw));
        CHECK(raw == write.raw);
    }
}

TEST_CASE("Native PROJ FormLink SetField preserves neighbors, NULL, grid, and reload") {
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
            nlohmann::json{{"editorId", "FormLinkRoundTrip"}, {"light", "000800:Patch.esp"}}
        })));
    REQUIRE(created.at("formKey") == "000801:Patch.esp");
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto before = fo4::read_plugin_file(path);
    const auto* beforeRecord = fo4::find_record(before, key);
    REQUIRE(beforeRecord != nullptr);
    const auto* beforeDnam = findSub(*beforeRecord, "DNAM");
    REQUIRE(beforeDnam != nullptr);

    const auto gridResult = backend.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Patch.esp", "PROJ", 20, 0}));
    const auto gridError = gridResult.has_value()
                               ? std::string{}
                               : gridResult.error().type + ": " + gridResult.error().message;
    const auto grid = parseResult(gridResult);
    CHECK(std::find(grid.at("columns").begin(), grid.at("columns").end(), "PROJ\\Light") !=
          grid.at("columns").end());

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key.to_string(), "PROJ\\Light", "NULL"
    })).has_value());
    const auto cleared = fo4::read_plugin_file(path);
    const auto* clearedRecord = fo4::find_record(cleared, key);
    REQUIRE(clearedRecord != nullptr);
    const auto* clearedDnam = findSub(*clearedRecord, "DNAM");
    REQUIRE(clearedDnam != nullptr);
    CHECK(std::all_of(clearedDnam->data.begin() + 16, clearedDnam->data.begin() + 20,
                      [](std::byte value) { return value == std::byte{0}; }));
    for (std::size_t index = 0; index < beforeDnam->data.size(); ++index) {
        if (index < 16 || index >= 20) CHECK(clearedDnam->data[index] == beforeDnam->data[index]);
    }

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key.to_string(), "PROJ\\Light", "000800:Patch.esp"
    })).has_value());
    const auto after = fo4::read_plugin_file(path);
    CHECK(fo4::forms::decode_projectile(after, key).light ==
          fo4::FormKey::parse("000800:Patch.esp"));

    fo4ide::plugin::RadiclePluginBackend reloaded;
    load(reloaded, path);
    const auto reloadedGridResult = reloaded.invokeBackend(
        "GetRecordsGrid", nlohmann::json::array({"Patch.esp", "PROJ", 20, 0}));
    const auto reloadedGridError = reloadedGridResult.has_value()
                                       ? std::string{}
                                       : reloadedGridResult.error().type + ": " +
                                             reloadedGridResult.error().message;
    const auto reloadedGrid = parseResult(reloadedGridResult);
    REQUIRE(reloadedGrid.at("rows").size() == 1);
    CHECK(std::find(reloadedGrid.at("rows")[0].at("cells").begin(),
                    reloadedGrid.at("rows")[0].at("cells").end(),
                    "000800:Patch.esp") != reloadedGrid.at("rows")[0].at("cells").end());
}

TEST_CASE("Native EXPL and HAZD FormLink SetField replace same-plugin targets") {
    {
        const auto root = tempRoot();
        Cleanup cleanup{root};
        const auto path = root / "Patch.esp";
        writeBytes(path, pluginWithRecord("PROJ"));
        fo4ide::plugin::RadiclePluginBackend backend;
        load(backend, path);
        const auto explosion = parseResult(backend.invokeBackend(
            "CreateRecord",
            nlohmann::json::array({
                "Patch.esp", "EXPL",
                nlohmann::json{{"editorId", "Explosion"}, {"spawnProjectile", "000800:Patch.esp"}}
            })));
        const auto second = parseResult(backend.invokeBackend(
            "CreateRecord",
            nlohmann::json::array({
                "Patch.esp", "PROJ", nlohmann::json{{"editorId", "SecondProjectile"}}
            })));
        const auto explosionKey = explosion.at("formKey").get<std::string>();
        REQUIRE(second.at("formKey") == "000802:Patch.esp");
        REQUIRE(backend.setFieldValue(nlohmann::json::array({
            "Patch.esp", explosionKey, "EXPL\\SpawnProjectile", "000802:Patch.esp"
        })).has_value());
        CHECK(fo4::forms::decode_explosion(
                  fo4::read_plugin_file(path), fo4::FormKey::parse(explosionKey)).spawn_projectile ==
              fo4::FormKey::parse("000802:Patch.esp"));
    }

    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("SPEL"));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto hazard = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "Patch.esp", "HAZD",
            nlohmann::json{{"editorId", "Hazard"}, {"model", "Effects\\Hazard.nif"},
                           {"spell", "000800:Patch.esp"}}
        })));
    const auto hazardKey = hazard.at("formKey").get<std::string>();
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\Spell", "NULL"
    })).has_value());
    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", hazardKey, "HAZD\\Spell", "000800:Patch.esp"
    })).has_value());
    CHECK(fo4::forms::decode_hazard(
              fo4::read_plugin_file(path), fo4::FormKey::parse(hazardKey)).spell ==
          fo4::FormKey::parse("000800:Patch.esp"));
}

TEST_CASE("Native FormLink SetField rejects wrong and unresolved targets without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithRecord("MISC"));
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "PROJ", nlohmann::json{{"editorId", "Reject"}}})));
    const auto key = created.at("formKey").get<std::string>();
    const auto before = readBytes(path);

    const auto wrong = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "PROJ\\Light", "000800:Patch.esp"
    }));
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().type == "FormLinkTargetTypeMismatch");
    CHECK(readBytes(path) == before);

    const auto unresolved = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", key, "PROJ\\Light", "000999:Patch.esp"
    }));
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(unresolved.error().type == "FormLinkTargetUnresolved");
    CHECK(readBytes(path) == before);
}

TEST_CASE("Native FormLink SetField adds an external master and remaps the owner") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, pluginWithRecord("LIGH", 0x800, 0x801, fo4::RecordFlags::master));
    writeBytes(patchPath, pluginWithRecord("PROJ", 0x810, 0x811));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({
            {{"name", "Master.esm"}, {"path", masterPath.string()}, {"source", "Master"}, {"resolved", true}},
            {{"name", "Patch.esp"}, {"path", patchPath.string()}, {"source", "Patch"}, {"resolved", true}}
        })}}).has_value());
    const auto created = parseResult(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "PROJ", nlohmann::json{{"editorId", "External"}}})));
    REQUIRE(created.at("formKey") == "000811:Patch.esp");

    REQUIRE(backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000811:Patch.esp", "PROJ\\Light", "000800:Master.esm"
    })).has_value());
    const auto disk = fo4::read_plugin_file(patchPath);
    REQUIRE(disk.header.masters.size() == 1);
    CHECK(disk.header.masters[0].key.filename() == "Master.esm");
    const auto* local = fo4::find_record(disk, fo4::FormKey::parse("000811:Patch.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000811});
    CHECK(fo4::forms::decode_projectile(disk, fo4::FormKey::parse("000811:Patch.esp")).light ==
          fo4::FormKey::parse("000800:Master.esm"));
}

TEST_CASE("Native FormLink SetField rejects a malformed embedded payload without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWithPayloadRecord("PROJ", "DNAM", 12));
    const auto before = readBytes(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);

    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "PROJ\\Light", "NULL"
    }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkFieldWriteFailed");
    CHECK(readBytes(path) == before);
}
