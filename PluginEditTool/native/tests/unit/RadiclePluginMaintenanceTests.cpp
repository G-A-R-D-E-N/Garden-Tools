#include <algorithm>
#include "../helpers/TempRoot.hpp"
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

#include <fo4/flags.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_maintenance_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void writeHeader(fo4::test::ByteWriter& out,
                 std::uint32_t nextFormId,
                 std::initializer_list<std::string_view> masters = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::size_t beginGroupRaw(fo4::test::ByteWriter& out, std::uint32_t label,
                          fo4::GroupType type) {
    const auto offset = out.size();
    out.sig("GRUP");
    out.u32(0);
    out.u32(label);
    out.i32(static_cast<std::int32_t>(type));
    out.u16(0);
    out.u16(0);
    out.u16(0);
    out.u16(0);
    return offset;
}

void endGroupRaw(fo4::test::ByteWriter& out, std::size_t offset) {
    out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

void writeCellRecord(fo4::test::ByteWriter& out, std::uint32_t raw) {
    const auto cell = fo4::test::begin_record(out, "CELL", 0, raw, 131);
    const std::vector<std::byte> data{std::byte{1}, std::byte{0}};
    fo4::test::write_subrecord(out, "DATA", data);
    fo4::test::end_record(out, cell);
}

std::vector<std::byte> cellRefrSourceBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x802);
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto sub = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);
    writeCellRecord(out, 0x00000800u);
    const auto children = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_children);
    const auto temp = beginGroupRaw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000801u, 131);
    fo4::test::ByteWriter name;
    name.u32(0x00000800u);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SourceRef\0", 10});
    fo4::test::end_record(out, refr);
    endGroupRaw(out, temp);
    endGroupRaw(out, children);
    endGroupRaw(out, sub);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> cellTargetBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x900, {"Base.esm"});
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    const auto block = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_block);
    const auto sub = beginGroupRaw(out, 0, fo4::GroupType::interior_cell_subblock);
    writeCellRecord(out, 0x00000800u);
    endGroupRaw(out, sub);
    endGroupRaw(out, block);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> noCellTargetBytes() {
  fo4::test::ByteWriter out;
  writeHeader(out, 0x900, {"Base.esm"});
  return out.bytes;
}

std::vector<std::byte> infoCopySourceBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x803);
    const auto questTop = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u, 131);
    fo4::test::end_record(out, quest);
    const auto questChildren = beginGroupRaw(
        out, 0x00000800u, fo4::GroupType::cell_visible_distant_children);
    const auto dialTop = fo4::test::begin_group(out, "DIAL", 0);
    const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000801u, 131);
    fo4::test::end_record(out, dial);
    const auto topic = beginGroupRaw(out, 0x00000801u, fo4::GroupType::topic_children);
    const auto info = fo4::test::begin_record(out, "INFO", 0, 0x00000802u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BackendInfo\0", 12});
    fo4::test::end_record(out, info);
    endGroupRaw(out, topic);
    fo4::test::end_group(out, dialTop);
    endGroupRaw(out, questChildren);
    fo4::test::end_group(out, questTop);
    return out.bytes;
}

std::vector<std::byte> infoCopyTargetBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x900, {"Base.esm"});
    const auto questTop = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u, 131);
    fo4::test::end_record(out, quest);
    const auto questChildren = beginGroupRaw(
        out, 0x00000800u, fo4::GroupType::cell_visible_distant_children);
    const auto dialTop = fo4::test::begin_group(out, "DIAL", 0);
    const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x00000801u, 131);
    fo4::test::end_record(out, dial);
    const auto topic = beginGroupRaw(out, 0x00000801u, fo4::GroupType::topic_children);
    endGroupRaw(out, topic);
    fo4::test::end_group(out, dialTop);
    endGroupRaw(out, questChildren);
    fo4::test::end_group(out, questTop);
    return out.bytes;
}

std::vector<std::byte> deletedPluginBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(
        out, "STAT", fo4::RecordFlags::deleted, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"DeletedStatic\0", 14});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> basePluginBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(out, "STAT", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SharedStatic\0", 13});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\Shared.nif\0", 18});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> identicalOverrideBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x800, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "STAT", 0);
    const auto record = fo4::test::begin_record(out, "STAT", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SharedStatic\0", 13});
    fo4::test::write_subrecord(out, "MODL", std::string_view{"Meshes\\Shared.nif\0", 18});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> copySourceBytes(std::string_view raceEditorId = "SourceRace") {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x802);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000800u, 131);
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string{raceEditorId} + '\0');
    fo4::test::ByteWriter link;
    link.u32(0x00000800u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::vector<std::byte> compactSourceBytes(bool hasMaster) {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x1001, hasMaster ? std::initializer_list<std::string_view>{"Base.esm"}
                                     : std::initializer_list<std::string_view>{});
    const auto ownerIndex = hasMaster ? 0x01000000u : 0u;

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, ownerIndex | 0x1000u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"HighColor\0", 10});
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, ownerIndex | 0x800u, 131);
    fo4::test::ByteWriter link;
    link.u32(ownerIndex | 0x1000u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::vector<std::byte> fullCapacityBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x1001, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    for (std::uint32_t local = 1; local < 0xFFFu; ++local) {
        const auto record = fo4::test::begin_record(out, "MISC", 0, 0x01000000u | local, 131);
        fo4::test::end_record(out, record);
    }
    const auto high = fo4::test::begin_record(out, "MISC", 0, 0x01001000u, 131);
    fo4::test::end_record(out, high);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> emptyPatchBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x800);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "MaintenanceTest"},
        {"resolved", true}
    };
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const nlohmann::json& plugins) {
    const auto result = backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", plugins}});
    REQUIRE(result.has_value());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    return bytes;
}

nlohmann::json eyesPayload() {
    return {
        {"editorId", "CompactEyes"},
        {"fullText", "Compact Eyes"},
        {"icon", "Textures\\Actors\\Character\\Eyes\\compact.dds"}
    };
}

std::uint32_t rawSubrecord(const fo4::Record& record, std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing requested FormLink subrecord");
    REQUIRE(found->data.size() == 4);
    std::uint32_t value = 0;
    std::memcpy(&value, found->data.data(), sizeof(value));
    return value;
}

std::string editorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
}

}  // namespace

TEST_CASE("Native CleanPlugin undeletes and initially disables records on disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, deletedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({pluginEntry("Patch.esp", path)}));

    const auto result = backend.invokeLegacyBackend(
        "CleanPlugin", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("undeleted + disabled 1 record") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeRecord = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(beforeRecord != nullptr);
    CHECK(beforeRecord->header.flags.is_deleted());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    CHECK_FALSE(record->header.flags.is_deleted());
    CHECK((record->header.flags.value() & 0x00000800u) != 0u);
}

TEST_CASE("Native RemoveIdenticalToMaster previews and removes top-level ITM overrides") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, basePluginBytes());
    writeBytes(patchPath, identicalOverrideBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", basePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto preview = backend.invokeLegacyBackend(
        "RemoveIdenticalToMaster",
        nlohmann::json::array({"Patch.esp", false, 2000}));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN: 1 identical-to-master record"));
    CHECK(preview->get<std::string>().find("SharedStatic") != std::string::npos);

    const auto applied = backend.invokeLegacyBackend(
        "RemoveIdenticalToMaster",
        nlohmann::json::array({"Patch.esp", true, 2000}));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Removed 1 identical-to-master record") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Base.esm")) != nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        patch, fo4::FormKey::parse("000800:Base.esm")) == nullptr);

    const auto base = fo4::read_plugin_file(basePath);
    CHECK(fo4::find_record(
        base, fo4::FormKey::parse("000800:Base.esm")) != nullptr);
}

TEST_CASE("Native CopyAsNewRecord allocates a local record and remaps source FormLinks") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, copySourceBytes());
    writeBytes(patchPath, emptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto result = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({
            "Base.esm", "000801:Base.esm", "Patch.esp", ""
        }));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("as NEW record 000800:Patch.esp") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    CHECK(beforeSave.header.masters.empty());
    CHECK(beforeSave.header.next_form_id == 0x800u);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Patch.esp")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    REQUIRE(patch.header.masters.size() == 1);
    CHECK(patch.header.masters[0].key.filename() == "Base.esm");
    CHECK(patch.header.next_form_id == 0x801u);

    const auto* duplicate = fo4::find_record(
        patch, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->header.signature == fo4::Signature::from_string("RACE"));
    CHECK(editorId(*duplicate) == "SourceRaceDUP");
    CHECK(rawSubrecord(*duplicate, "AHCM") == 0x00000800u);
}

TEST_CASE("Native CopyAsNewRecord duplicates a Windows-1252 EditorID as UTF-8 text") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, copySourceBytes(std::string_view{"Caf\xE9", 4}));
    writeBytes(patchPath, emptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto result = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000801:Base.esm", "Patch.esp", ""}));
    REQUIRE(result.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* duplicate = fo4::find_record(
        patch, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(editorId(*duplicate) == std::string{"Caf\xE9" "DUP"});
}

TEST_CASE("Native CopyAsNewRecord refuses an EditorID that Windows-1252 cannot store without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, copySourceBytes(std::string_view{"Caf\xE9", 4}));
    writeBytes(patchPath, emptyPatchBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));
    const auto before = readBytes(patchPath);

    const auto refused = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000801:Base.esm", "Patch.esp", "\xe6\x97\xa5"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "CopyAsNewFailed");
    const auto pending = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(pending.has_value());
    for (const auto& plugin : nlohmann::json::parse(pending->get<std::string>())) {
        CHECK(plugin.at("Pending") == false);
    }
    CHECK(readBytes(patchPath) == before);
}

TEST_CASE("Native CompactToEsl remaps top-level records and their in-plugin references") {
    bool hasMaster = false;
    SECTION("Masterless plugins keep the full plugin floor") {}
    SECTION("Plugins with HEDR 1.0 and a master allocate below the full plugin floor") {
        hasMaster = true;
    }
    const auto target = fo4::FormKey::parse(hasMaster ? "000001:Patch.esp" : "000801:Patch.esp");
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, compactSourceBytes(hasMaster));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({pluginEntry("Patch.esp", path)}));

    const auto result = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("remapped 1 record") != std::string::npos);
    CHECK(result->get<std::string>().find(
        "Run masters.SetLight to flag the plugin light; Radicle does not flag it from a .esl file name.") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("001000:Patch.esp")) != nullptr);
    CHECK(fo4::find_record(beforeSave, target) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(
        disk, fo4::FormKey::parse("001000:Patch.esp")) == nullptr);
    const auto* color = fo4::find_record(disk, target);
    REQUIRE(color != nullptr);
    CHECK(editorId(*color) == "HighColor");

    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(rawSubrecord(*race, "AHCM") == (hasMaster ? 0x01000001u : 0x00000801u));
    CHECK_FALSE(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.next_form_id == (hasMaster ? 0x002u : 0x802u));
}

TEST_CASE("Native CompactToEsl leaves a light plugin that can still create records") {
    bool hasMaster = false;
    SECTION("Masterless plugins") {}
    SECTION("Plugins with HEDR 1.0 and a master") {
        hasMaster = true;
    }
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, compactSourceBytes(hasMaster));

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({pluginEntry("Patch.esp", path)}));
    REQUIRE(backend.compactToEsl(nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(backend.invoke(
        "masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());

    const auto created = backend.invokeLegacyBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "EYES", eyesPayload()}));
    REQUIRE(created.has_value());
    const auto expected = hasMaster ? "000002:Patch.esp" : "000802:Patch.esp";
    CHECK(nlohmann::json::parse(created->get<std::string>()).at("formKey") == expected);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.next_form_id == (hasMaster ? 0x003u : 0x803u));
    CHECK(fo4::find_record(disk, fo4::FormKey::parse(expected)) != nullptr);
}

TEST_CASE("Native CompactToEsl at full capacity leaves no ID and creation fails cleanly") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, fullCapacityBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({pluginEntry("Patch.esp", path)}));
    const auto compacted = backend.compactToEsl(nlohmann::json::array({"Patch.esp"}));
    REQUIRE(compacted.has_value());
    CHECK(compacted->get<std::string>().find("remapped 1 record") != std::string::npos);
    REQUIRE(backend.invoke(
        "masters.SetLight", nlohmann::json::array({"Patch.esp", true})).has_value());

    const auto created = backend.invokeLegacyBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "EYES", eyesPayload()}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "EyesCreateFailed");
    CHECK(created.error().message.find("no local ") != std::string::npos);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.header.next_form_id == 0x1000u);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000FFF:Patch.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("001000:Patch.esp")) == nullptr);
}

TEST_CASE("Native CopyAsNewRecord duplicates a contained REFR into an existing target CELL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, cellRefrSourceBytes());
    writeBytes(patchPath, cellTargetBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto result = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000801:Base.esm", "Patch.esp", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("as NEW record 000900:Patch.esp") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000900:Patch.esp")) == nullptr);
    REQUIRE(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Base.esm")) != nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* duplicate = fo4::find_record(patch, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->header.signature == fo4::Signature::from_string("REFR"));
    CHECK(editorId(*duplicate) == "SourceRefDUP");
    const auto* sourceCell = fo4::find_record(patch, fo4::FormKey::parse("000800:Base.esm"));
    REQUIRE(sourceCell != nullptr);
    CHECK(sourceCell->header.signature == fo4::Signature::from_string("CELL"));
}

TEST_CASE("Native CopyAsNewRecord fails closed when the target lacks the parent CELL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, cellRefrSourceBytes());
    writeBytes(patchPath, noCellTargetBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto result = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000801:Base.esm", "Patch.esp", ""}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "TargetParentCellMissing");
}

TEST_CASE("Native CopyAsNewRecord dispatches a contained INFO to the dialogue primitive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(sourcePath, infoCopySourceBytes());
    writeBytes(patchPath, infoCopyTargetBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, nlohmann::json::array({
        pluginEntry("Base.esm", sourcePath),
        pluginEntry("Patch.esp", patchPath)
    }));

    const auto result = backend.invokeLegacyBackend(
        "CopyAsNewRecord",
        nlohmann::json::array({"Base.esm", "000802:Base.esm", "Patch.esp", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("as NEW record 000900:Patch.esp") != std::string::npos);

    const auto pendingRaw = backend.invokeBackend(
        "GetRecordDetails", nlohmann::json::array({"000900:Patch.esp"}));
    REQUIRE(pendingRaw.has_value());
    REQUIRE(pendingRaw->is_string());
    const auto pending = nlohmann::json::parse(pendingRaw->get<std::string>());
    CHECK(pending.at("FormKey") == "000900:Patch.esp");
    CHECK(pending.at("EditorId") == "BackendInfoDUP");
    CHECK(pending.at("Signature") == "INFO");
    CHECK(pending.at("File") == "Patch.esp");

    const auto beforeSave = fo4::read_plugin_file(patchPath);
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000900:Patch.esp")) == nullptr);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto patch = fo4::read_plugin_file(patchPath);
    const auto* duplicate = fo4::find_record(
        patch, fo4::FormKey::parse("000900:Patch.esp"));
    REQUIRE(duplicate != nullptr);
    CHECK(duplicate->header.signature == fo4::Signature::from_string("INFO"));
    CHECK(editorId(*duplicate) == "BackendInfoDUP");
}
