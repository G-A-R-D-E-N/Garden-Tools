#include <cstring>
#include "../helpers/TempRoot.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/record_constraints.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

// Constraint: every record type whose authoring rows list a FULL, read from the schema so a type that gains a name is covered without editing this file. EYES keeps its own EYES\\Full route, and CELL and REFR cannot sit in a top-level group.
const std::vector<std::string>& namedTypes() {
    static const std::vector<std::string> types = [] {
        std::vector<std::string> found;
        const auto full = fo4::Signature::from_string("FULL");
        for (const auto& constraint : fo4::schema::k_generated_record_authoring_constraints) {
            if (constraint.placement != fo4::schema::RecordPlacement::top_level) continue;
            if (constraint.record == fo4::Signature::from_string("EYES")) continue;
            const auto rules = fo4::schema::authoring_subrecords(constraint);
            if (std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == full; })) {
                found.push_back(constraint.record.to_string());
            }
        }
        return found;
    }();
    return types;
}

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_name_field_" + id);
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
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
    return bytes;
}

std::vector<std::byte> headerPlugin(std::uint32_t tes4Flags) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

std::vector<std::byte> rawBytes(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const auto ch : text) bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    return bytes;
}

std::vector<std::byte> zbytes(std::string_view text) {
    auto bytes = rawBytes(text);
    bytes.push_back(std::byte{0});
    return bytes;
}

using Subrecords = std::vector<std::pair<std::string, std::vector<std::byte>>>;

struct RecordSpec {
    std::string type;
    std::uint32_t formId;
    Subrecords subrecords;
};

std::vector<std::byte> pluginWith(std::uint32_t tes4Flags, const std::vector<RecordSpec>& records,
                                  std::string_view master = {}) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(static_cast<std::uint32_t>(records.size()));
    hedr.u32(0x1000);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (!master.empty()) {
        fo4::test::write_subrecord(out, "MAST", zbytes(master));
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    std::vector<std::string> done;
    for (const auto& top : records) {
        if (std::find(done.begin(), done.end(), top.type) != done.end()) continue;
        done.push_back(top.type);
        const auto group = fo4::test::begin_group(out, top.type, 0);
        for (const auto& spec : records) {
            if (spec.type != top.type) continue;
            const auto record = fo4::test::begin_record(out, spec.type, 0, spec.formId);
            for (const auto& [name, data] : spec.subrecords) fo4::test::write_subrecord(out, name, data);
            fo4::test::end_record(out, record);
        }
        fo4::test::end_group(out, group);
    }
    return out.bytes;
}

std::string localKey(std::uint32_t local, std::string_view owner = "Patch.esp") {
    char text[16];
    std::snprintf(text, sizeof(text), "%06X:", static_cast<unsigned>(local));
    return std::string(text) + std::string(owner);
}

std::vector<std::byte> namedTypesPlugin(std::uint32_t tes4Flags) {
    std::vector<RecordSpec> records;
    std::uint32_t formId = 0x800;
    for (const auto& type : namedTypes()) {
        // Constraint: OBND goes only on the types that list it, since an unlisted row makes an LCTN name uninsertable.
        Subrecords subrecords{{"EDID", zbytes("Named" + type)}};
        const auto rules = fo4::schema::authoring_subrecords(
            *fo4::schema::record_authoring_constraint(fo4::Signature::from_string(type)));
        if (std::any_of(rules.begin(), rules.end(), [](const auto& rule) {
                return rule.signature == fo4::Signature::from_string("OBND");
            })) {
            subrecords.emplace_back("OBND", std::vector<std::byte>(12));
        }
        records.push_back({type, formId++, std::move(subrecords)});
    }
    return pluginWith(tes4Flags, records);
}

std::map<std::string, fo4::FormKey> namedKeys() {
    std::map<std::string, fo4::FormKey> keys;
    std::uint32_t formId = 0x800;
    for (const auto& type : namedTypes()) keys.emplace(type, fo4::FormKey::parse(localKey(formId++)));
    return keys;
}

void loadPlugins(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::vector<std::pair<std::string, std::filesystem::path>>& plugins) {
    auto entries = nlohmann::json::array();
    for (const auto& [name, path] : plugins) {
        entries.push_back({{"name", name}, {"path", path.string()}, {"source", "NameField"}, {"resolved", true}});
    }
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", entries}}).has_value());
}

void loadPatch(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& path) {
    loadPlugins(backend, {{"Patch.esp", path}});
}

void writeStringTables(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{100, rawBytes("Other")}}},
        directory / "Patch_en.STRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::dlstrings, {{500, rawBytes("Desc")}}},
        directory / "Patch_en.DLSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::ilstrings, {{900, rawBytes("Dialog")}}},
        directory / "Patch_en.ILSTRINGS");
    fo4::localization::write_string_table_file(
        {fo4::localization::StringTableKind::strings, {{3000, rawBytes("Andere")}}},
        directory / "Patch_de.STRINGS");
}

fo4ide::Result<nlohmann::json> setName(fo4ide::plugin::RadiclePluginBackend& backend, std::string_view plugin,
                                       const std::string& key, const std::string& path, const std::string& value) {
    return backend.invokeBackend("SetField", nlohmann::json::array({std::string(plugin), key, path, value}));
}

std::vector<std::vector<std::byte>> fullSubrecords(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    const auto* record = fo4::find_record(plugin, key);
    REQUIRE(record != nullptr);
    std::vector<std::vector<std::byte>> found;
    for (const auto& sub : record->subrecords) {
        if (sub.signature == fo4::Signature::from_string("FULL")) found.push_back(sub.data);
    }
    return found;
}

std::string inlineText(const std::vector<std::byte>& data) {
    REQUIRE(!data.empty());
    return std::string(reinterpret_cast<const char*>(data.data()), data.size() - 1);
}

std::string errorType(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error().type;
}

}  // namespace

// Detects: SetField on <TYPE>\Full in a plain plugin must write the inline name for every named record type, and a fresh backend must reload and rename it.
TEST_CASE("Native SetField Full names every named record type in a plain plugin and survives reload") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, namedTypesPlugin(0u));
    const auto keys = namedKeys();
    CHECK(namedTypes().size() >= 46u);
    for (const std::string_view type : {"WEAP", "ARMO", "MISC", "ALCH", "BOOK", "KEYM", "AMMO", "NOTE", "CMPO", "INGR",
                                        "NPC_", "QUST", "CONT", "ACTI", "FURN", "DOOR", "LIGH", "SPEL", "PERK", "MGEF",
                                        "STAT", "TERM", "FACT", "RACE", "MESG", "ENCH", "OMOD", "LCTN", "WRLD", "KYWD"}) {
        INFO(std::string(type));
        CHECK(std::find(namedTypes().begin(), namedTypes().end(), type) != namedTypes().end());
    }

    {
        fo4ide::plugin::RadiclePluginBackend backend;
        backend.testSetJournalRoot(root / "journal");
        loadPatch(backend, path);
        for (const auto& type : namedTypes()) {
            INFO("set " + type + "\\Full");
            const auto result = setName(backend, "Patch.esp", keys.at(type).to_string(),
                                        type + "\\Full", "Name " + type);
            INFO((result.has_value() ? std::string{} : result.error().type + ": " + result.error().message));
            CHECK(result.has_value());
        }
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }

    const auto disk = fo4::read_plugin_file(path);
    for (const auto& type : namedTypes()) {
        INFO("read " + type);
        const auto found = fullSubrecords(disk, keys.at(type));
        REQUIRE(found.size() == 1u);
        CHECK(inlineText(found.front()) == "Name " + type);
    }

    fo4ide::plugin::RadiclePluginBackend reloaded;
    reloaded.testSetJournalRoot(root / "journal");
    loadPatch(reloaded, path);
    const auto weap = keys.at("WEAP").to_string();
    CHECK(setName(reloaded, "Patch.esp", weap, "WEAP\\Full", "Name WEAP").has_value());
    REQUIRE(setName(reloaded, "Patch.esp", weap, "WEAP\\Full", "Renamed WEAP").has_value());
    REQUIRE(reloaded.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto renamed = fo4::read_plugin_file(path);
    const auto found = fullSubrecords(renamed, keys.at("WEAP"));
    REQUIRE(found.size() == 1u);
    CHECK(inlineText(found.front()) == "Renamed WEAP");
    CHECK(inlineText(fullSubrecords(renamed, keys.at("ARMO")).front()) == "Name ARMO");
}

// Detects: SetField on <TYPE>\Full in a localized plugin must store the name as a new string in the plugin's STRINGS table above every existing ID in every language, keep the existing STRINGS and DLSTRINGS entries, and persist with save_plugin.
TEST_CASE("Native SetField Full names every named record type in a localized plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, namedTypesPlugin(fo4::RecordFlags::localized));
    writeStringTables(root / "Strings");
    const auto keys = namedKeys();

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);

    for (const auto& type : namedTypes()) {
        INFO("set " + type + "\\Full");
        const auto result = setName(backend, "Patch.esp", keys.at(type).to_string(),
                                    type + "\\Full", "Name " + type);
        INFO((result.has_value() ? std::string{} : result.error().type + ": " + result.error().message));
        CHECK(result.has_value());
    }

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto table = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    std::vector<std::uint32_t> ids;
    for (const auto& type : namedTypes()) {
        INFO("read " + type);
        const auto found = fullSubrecords(disk, keys.at(type));
        REQUIRE(found.size() == 1u);
        REQUIRE(found.front().size() == 4u);
        std::uint32_t id = 0;
        std::memcpy(&id, found.front().data(), sizeof(id));
        CHECK(id > 3000u);
        ids.push_back(id);
        const auto* entry = fo4::localization::find_string(table, id);
        REQUIRE(entry != nullptr);
        CHECK(std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size()) ==
              "Name " + type);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    const auto* kept = fo4::localization::find_string(table, 100);
    REQUIRE(kept != nullptr);
    CHECK(std::string(reinterpret_cast<const char*>(kept->text.data()), kept->text.size()) == "Other");
    const auto descriptions = fo4::localization::read_string_table_file(
        root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings);
    CHECK(fo4::localization::find_string(descriptions, 500) != nullptr);
}

// Detects: SetField on <TYPE>\Full on a master-owned record copied into a plugin as an override must replace the copied name in the override only and leave the master file byte-identical.
TEST_CASE("Native SetField Full renames an override and leaves the master untouched") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, pluginWith(fo4::RecordFlags::master,
                                    {{"WEAP", 0x800, {{"EDID", zbytes("BaseGun")}, {"OBND", std::vector<std::byte>(12)},
                                                      {"FULL", zbytes("Old Name")}}}}));
    writeBytes(patchPath, headerPlugin(0u));
    const auto baseBefore = readBytes(basePath);
    const auto key = localKey(0x800, "Base.esm");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPlugins(backend, {{"Base.esm", basePath}, {"Patch.esp", patchPath}});
    REQUIRE(backend.invokeBackend("CopyAsOverride", nlohmann::json::array({"Base.esm", key, "Patch.esp", false})).has_value());
    REQUIRE(setName(backend, "Patch.esp", key, "WEAP\\Full", "New Name").has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(patchPath);
    const auto found = fullSubrecords(disk, fo4::FormKey::parse(key));
    REQUIRE(found.size() == 1u);
    CHECK(inlineText(found.front()) == "New Name");
    CHECK(readBytes(basePath) == baseBefore);
}

// Detects: renaming a vanilla item through an override. The master is localized and the patch is not, so the copied FULL is the master's string ID (here one with a zero low byte, which is not readable text); SetField must replace it with the inline name rather than refuse it as opaque.
TEST_CASE("Native SetField Full renames an override of a localized master in a plain plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    std::vector<std::byte> stringId(4);
    const std::uint32_t id = 0x0000AB00u;
    std::memcpy(stringId.data(), &id, sizeof(id));
    writeBytes(basePath, pluginWith(fo4::RecordFlags::master | fo4::RecordFlags::localized,
                                    {{"WEAP", 0x800, {{"EDID", zbytes("BaseGun")}, {"OBND", std::vector<std::byte>(12)},
                                                      {"FULL", stringId}}}}));
    writeBytes(patchPath, headerPlugin(0u));
    const auto baseBefore = readBytes(basePath);
    const auto key = localKey(0x800, "Base.esm");

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPlugins(backend, {{"Base.esm", basePath}, {"Patch.esp", patchPath}});
    REQUIRE(backend.invokeBackend("CopyAsOverride", nlohmann::json::array({"Base.esm", key, "Patch.esp", false})).has_value());
    const auto renamed = setName(backend, "Patch.esp", key, "WEAP\\Full", "New Name");
    INFO((renamed.has_value() ? std::string{} : renamed.error().type + ": " + renamed.error().message));
    REQUIRE(renamed.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto found = fullSubrecords(fo4::read_plugin_file(patchPath), fo4::FormKey::parse(key));
    REQUIRE(found.size() == 1u);
    CHECK(inlineText(found.front()) == "New Name");
    CHECK(readBytes(basePath) == baseBefore);
}

// Detects: SetField on <TYPE>\Full must refuse a missing record, a wrong signature, a record type without FULL, a LCTN holding unlisted rows, an unreadable existing FULL and empty, NUL or unrepresentable text with a specific error, and leave no pending edit (a second LoadMo2 without discardPending only succeeds when nothing is dirty).
TEST_CASE("Native SetField Full maps every refusal to its own error and writes nothing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginWith(0u, {
        {"WEAP", 0x800, {{"EDID", zbytes("Gun")}, {"OBND", std::vector<std::byte>(12)}}},
        {"GLOB", 0x801, {{"EDID", zbytes("Number")}}},
        {"LCTN", 0x802, {{"EDID", zbytes("Loc")}, {"LCSR", std::vector<std::byte>(16)}}},
        {"MISC", 0x803, {{"EDID", zbytes("Junk")}, {"FULL", std::vector<std::byte>(4, std::byte{0x78})}}},
    }));
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.testSetJournalRoot(root / "journal");
    loadPatch(backend, path);

    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x8FF), "WEAP\\Full", "x")) == "RecordNotFound");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x800), "ARMO\\Full", "x")) == "ScalarFieldRecordMismatch");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x801), "GLOB\\Full", "x")) == "NameFieldUnsupported");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x802), "LCTN\\Full", "x")) == "NameFieldNotInsertable");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x803), "MISC\\Full", "x")) == "NameFieldOpaque");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x800), "WEAP\\Full", "")) == "InvalidFieldValue");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x800), "WEAP\\Full", std::string("a\0b", 3))) == "InvalidFieldValue");
    CHECK(errorType(setName(backend, "Patch.esp", localKey(0x800), "WEAP\\Full", "caf\xE2\x82\xAC\xF0\x9F\x98\x80")) == "InvalidFieldValue");

    loadPatch(backend, path);
    CHECK(readBytes(path) == before);
}

// Detects: the set_name MCP method must name any record from just plugin, form key and text, in plain and localized plugins, and must refuse a missing record and empty text.
TEST_CASE("Native SetRecordName names a record without a field path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto keys = namedKeys();
    const auto setRecordName = [](fo4ide::plugin::RadiclePluginBackend& backend, const std::string& key,
                                  const std::string& text) {
        return backend.invokeBackend("SetRecordName", nlohmann::json::array({"Patch.esp", key, text}));
    };

    const auto plainPath = root / "plain" / "Patch.esp";
    writeBytes(plainPath, namedTypesPlugin(0u));
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        backend.testSetJournalRoot(root / "journal");
        loadPatch(backend, plainPath);
        REQUIRE(setRecordName(backend, keys.at("NPC_").to_string(), "Preston").has_value());
        CHECK(errorType(setRecordName(backend, localKey(0x8FF), "x")) == "RecordNotFound");
        CHECK(errorType(setRecordName(backend, keys.at("NPC_").to_string(), "")) == "InvalidFieldValue");
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    const auto plain = fo4::read_plugin_file(plainPath);
    CHECK(inlineText(fullSubrecords(plain, keys.at("NPC_")).front()) == "Preston");

    const auto localizedPath = root / "localized" / "Patch.esp";
    writeBytes(localizedPath, namedTypesPlugin(fo4::RecordFlags::localized));
    writeStringTables(root / "localized" / "Strings");
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        backend.testSetJournalRoot(root / "journal");
        loadPatch(backend, localizedPath);
        REQUIRE(setRecordName(backend, keys.at("SPEL").to_string(), "Fireball").has_value());
        REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    }
    const auto localized = fo4::read_plugin_file(localizedPath);
    const auto found = fullSubrecords(localized, keys.at("SPEL"));
    REQUIRE(found.size() == 1u);
    REQUIRE(found.front().size() == 4u);
    std::uint32_t id = 0;
    std::memcpy(&id, found.front().data(), sizeof(id));
    const auto table = fo4::localization::read_string_table_file(
        root / "localized" / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings);
    const auto* entry = fo4::localization::find_string(table, id);
    REQUIRE(entry != nullptr);
    CHECK(std::string(reinterpret_cast<const char*>(entry->text.data()), entry->text.size()) == "Fireball");
}

// Detects: the record grid and set_field must agree on the name path. The grid must list <TYPE>\\Full as an editable column holding the inline name (empty when the record has none), and show a string ID in a localized plugin as a read-only placeholder that cannot be copied onto another record as text.
TEST_CASE("Records grid lists the name column that SetField Full writes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto gridOf = [](fo4ide::plugin::RadiclePluginBackend& backend, const std::string& type) {
        const auto result = backend.invokeBackend("GetRecordsGrid", nlohmann::json::array({"Patch.esp", type, 200, 0}));
        REQUIRE(result.has_value());
        return nlohmann::json::parse(result->get<std::string>());
    };
    const auto nameCell = [](const nlohmann::json& grid, const std::string& type) {
        const auto& columns = grid.at("columns");
        const auto column = std::find(columns.begin(), columns.end(), type + "\\Full");
        REQUIRE(column != columns.end());
        const auto index = static_cast<std::size_t>(column - columns.begin());
        CHECK(index == 1u);
        REQUIRE(grid.at("rows").size() == 1u);
        return std::pair<std::string, bool>{grid.at("rows")[0].at("cells")[index].get<std::string>(),
                                            grid.at("rows")[0].at("editable")[index].get<bool>()};
    };
    const auto keys = namedKeys();

    const auto plainPath = root / "plain" / "Patch.esp";
    writeBytes(plainPath, namedTypesPlugin(0u));
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        backend.testSetJournalRoot(root / "journal");
        loadPatch(backend, plainPath);
        for (const auto& type : namedTypes()) {
            INFO(type);
            CHECK(nameCell(gridOf(backend, type), type) == std::pair<std::string, bool>{"", true});
            REQUIRE(setName(backend, "Patch.esp", keys.at(type).to_string(), type + "\\Full", "Caf\xC3\xA9 " + type).has_value());
            CHECK(nameCell(gridOf(backend, type), type) == std::pair<std::string, bool>{"Caf\xC3\xA9 " + type, true});
        }
    }

    const auto localizedPath = root / "localized" / "Patch.esp";
    writeBytes(localizedPath, namedTypesPlugin(fo4::RecordFlags::localized));
    writeStringTables(root / "localized" / "Strings");
    {
        fo4ide::plugin::RadiclePluginBackend backend;
        backend.testSetJournalRoot(root / "journal");
        loadPatch(backend, localizedPath);
        CHECK(nameCell(gridOf(backend, "MISC"), "MISC") == std::pair<std::string, bool>{"", true});
        REQUIRE(setName(backend, "Patch.esp", keys.at("MISC").to_string(), "MISC\\Full", "Junk").has_value());
        CHECK(nameCell(gridOf(backend, "MISC"), "MISC") == std::pair<std::string, bool>{"<opaque/localized>", false});
    }
}
