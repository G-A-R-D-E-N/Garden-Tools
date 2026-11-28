#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/signature.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../../src/plugin/RadiclePluginEffectRows.hpp"

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path formListTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_form_list_" + std::to_string(id));
}

struct FormListCleanup {
    std::filesystem::path root;
    ~FormListCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> emptyFormListPlugin() {
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

void writeFormListPlugin(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    const auto bytes = emptyFormListPlugin();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void loadFormListPlugin(fo4ide::plugin::RadiclePluginBackend& backend,
                        const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"},
                                                          {"path", path.string()},
                                                          {"source", "FormListTest"},
                                                          {"resolved", true}}})}});
    REQUIRE(result.has_value());
}

std::vector<std::byte> keywordMasterPlugin(std::string_view editorId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    const auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000800u);
    std::string edid(editorId);
    edid.push_back('\0');
    fo4::test::write_subrecord(out, "EDID", std::string_view{edid});
    fo4::test::end_record(out, keyword);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writePluginBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::string createdFormKey(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    REQUIRE(parsed.contains("formKey"));
    return parsed.at("formKey").get<std::string>();
}

nlohmann::json readEntries(fo4ide::plugin::RadiclePluginBackend& backend,
                           const std::string& plugin,
                           const std::string& formKey) {
    const auto result = backend.invokeBackend(
        "GetFormListEntries", nlohmann::json::array({plugin, formKey}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_object());
    return *result;
}

}

// Detects: form list edits must survive add, set, read, remove and save.
TEST_CASE("Form list entries round-trip through add set get and remove") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);

    const auto keyword = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD", {{"editorId", "DiseaseKeyword"}}})));
    const auto list = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST", {{"editorId", "DiseaseList"}}})));

    const auto empty = readEntries(backend, "Patch.esp", list);
    CHECK(empty.at("count") == 0);
    CHECK(empty.at("entries").empty());

    const auto appended = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, keyword}));
    REQUIRE(appended.has_value());
    CHECK(appended->get<std::string>().find("index 0") != std::string::npos);

    const auto second = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, "DiseaseKeyword"}));
    REQUIRE(second.has_value());

    const auto both = readEntries(backend, "Patch.esp", list);
    REQUIRE(both.at("count") == 2);
    REQUIRE(both.at("entries").size() == 2);
    CHECK(both.at("entries")[0] == keyword);
    CHECK(both.at("entries")[1] == keyword);

    const auto replaced = backend.invokeBackend(
        "SetFormListEntry", nlohmann::json::array({"Patch.esp", list, 1, keyword}));
    REQUIRE(replaced.has_value());
    const auto afterSet = readEntries(backend, "Patch.esp", list);
    REQUIRE(afterSet.at("count") == 2);

    const auto removed = backend.invokeBackend(
        "RemoveFormListEntry", nlohmann::json::array({"Patch.esp", list, 0}));
    REQUIRE(removed.has_value());
    const auto afterRemove = readEntries(backend, "Patch.esp", list);
    CHECK(afterRemove.at("count") == 1);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto values = fo4::schema::record_formlink_list_values(
        disk,
        fo4::FormKey::parse(list),
        *fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("FLST"), "FLST\\Items"));
    REQUIRE(values.has_value());
    CHECK(values->size() == 1);
}

// Detects: invalid form list entries must be refused without mutation.
TEST_CASE("Form list entry tools refuse by name and leave the plugin untouched") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);

    const auto keyword = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD", {{"editorId", "LonelyKeyword"}}})));
    const auto list = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST", {{"editorId", "EmptyList"}}})));
    const auto global = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "GLOB", {{"editorId", "NotAList"}, {"type", "short"}}})));

    const auto unresolved = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, "0008AB:Nope.esp"}));
    REQUIRE_FALSE(unresolved.has_value());
    CHECK(unresolved.error().type == "FormListEntryUnresolved");

    const auto missingEditorId = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, "NoSuchEditorId"}));
    REQUIRE_FALSE(missingEditorId.has_value());
    CHECK(missingEditorId.error().type == "FormListEntryUnresolved");

    const auto pastEnd = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, keyword, 4}));
    REQUIRE_FALSE(pastEnd.has_value());
    CHECK(pastEnd.error().type == "FormListEntryIndexOutOfRange");

    const auto notAList = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", global, keyword}));
    REQUIRE_FALSE(notAList.has_value());
    CHECK(notAList.error().type == "FormListRecordMismatch");

    const auto setPastEnd = backend.invokeBackend(
        "SetFormListEntry", nlohmann::json::array({"Patch.esp", list, 0, keyword}));
    REQUIRE_FALSE(setPastEnd.has_value());
    CHECK(setPastEnd.error().type == "FormListEntryIndexOutOfRange");

    const auto removePastEnd = backend.invokeBackend(
        "RemoveFormListEntry", nlohmann::json::array({"Patch.esp", list, 0}));
    REQUIRE_FALSE(removePastEnd.has_value());
    CHECK(removePastEnd.error().type == "FormListEntryIndexOutOfRange");

    const auto unknownPlugin = backend.invokeBackend(
        "GetFormListEntries", nlohmann::json::array({"Missing.esp", list}));
    REQUIRE_FALSE(unknownPlugin.has_value());
    CHECK(unknownPlugin.error().type == "PluginNotLoaded");

    const auto after = readEntries(backend, "Patch.esp", list);
    CHECK(after.at("count") == 0);
}

// Detects: form list creation must persist every requested entry.
TEST_CASE("A form list is created with its entries in one call") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);

    const auto keyword = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD", {{"editorId", "SeededKeyword"}}})));
    const auto list = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST",
                               {{"editorId", "SeededList"},
                                {"entries", nlohmann::json::array({keyword, "SeededKeyword"})}}})));

    const auto entries = readEntries(backend, "Patch.esp", list);
    REQUIRE(entries.at("count") == 2);
    CHECK(entries.at("entries")[0] == keyword);
    CHECK(entries.at("entries")[1] == keyword);

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    REQUIRE(fo4::find_record(disk, fo4::FormKey::parse(list)) != nullptr);

    const auto badEntry = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST",
                               {{"editorId", "RefusedList"},
                                {"entries", nlohmann::json::array({keyword, "MissingEditorId"})}}}));
    REQUIRE_FALSE(badEntry.has_value());
    CHECK(badEntry.error().type == "FormListEntryUnresolved");
    CHECK(badEntry.error().message.find("Entry #1") != std::string::npos);
}

// Detects: an FLST entry that lives in another loaded plugin must declare that plugin as a master and save a link that still resolves to it; an owner that loads after the edited plugin must be refused with MasterOrder by every FLST writer, leaving the list and the masters alone.
TEST_CASE("Form list writers declare an earlier owner as a master and refuse a later one") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto path = root / "Patch.esp";
    const auto latePath = root / "Late.esm";
    writePluginBytes(basePath, keywordMasterPlugin("BaseKeyword"));
    writeFormListPlugin(path);
    writePluginBytes(latePath, keywordMasterPlugin("LateKeyword"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto entry = [](const char* name, const std::filesystem::path& file) {
        return nlohmann::json{{"name", name}, {"path", file.string()}, {"source", "FormListTest"}, {"resolved", true}};
    };
    REQUIRE(backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({entry("Base.esm", basePath),
                                                          entry("Patch.esp", path),
                                                          entry("Late.esm", latePath)})}}).has_value());

    const auto list = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST",
                               {{"editorId", "CrossList"},
                                {"entries", nlohmann::json::array({"BaseKeyword"})}}})));
    REQUIRE(backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, "000800:Base.esm"})).has_value());

    const auto lateAdd = backend.invokeBackend(
        "AddFormListEntry", nlohmann::json::array({"Patch.esp", list, "000800:Late.esm"}));
    REQUIRE_FALSE(lateAdd.has_value());
    CHECK(lateAdd.error().type == "MasterOrder");
    const auto lateSet = backend.invokeBackend(
        "SetFormListEntry", nlohmann::json::array({"Patch.esp", list, 0, "LateKeyword"}));
    REQUIRE_FALSE(lateSet.has_value());
    CHECK(lateSet.error().type == "MasterOrder");
    const auto lateCreate = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "FLST",
                               {{"editorId", "LateList"},
                                {"entries", nlohmann::json::array({"000800:Late.esm"})}}}));
    REQUIRE_FALSE(lateCreate.has_value());
    CHECK(lateCreate.error().type == "MasterOrder");

    const auto entries = readEntries(backend, "Patch.esp", list);
    REQUIRE(entries.at("count") == 2);
    CHECK(entries.at("entries")[0] == "000800:Base.esm");
    CHECK(entries.at("entries")[1] == "000800:Base.esm");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    REQUIRE(disk.header.masters.size() == 1u);
    CHECK(disk.header.masters[0].key == fo4::PluginKey::from_filename("Base.esm"));
    const auto saved = fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::FormKey::parse(list).local()};
    const auto values = fo4::schema::record_formlink_list_values(
        disk, saved,
        *fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("FLST"), "FLST\\Items"));
    REQUIRE(values.has_value());
    REQUIRE(values->size() == 2u);
    CHECK((*values)[0] == fo4::FormKey::parse("000800:Base.esm"));
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000801:Patch.esp")) == nullptr);
}

// Detects: keyword creation must persist the requested color and type.
TEST_CASE("KYWD creation writes the requested color and type") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);

    const auto key = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD",
                               {{"editorId", "TintedKeyword"},
                                {"color", "3366CC"},
                                {"type", "Sound"}}})));
    const auto byNumber = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD",
                               {{"editorId", "NumberedKeyword"},
                                {"color", nlohmann::json::array({1, 2, 3, 4})},
                                {"type", 6}}})));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);

    const auto* tinted = fo4::find_record(disk, fo4::FormKey::parse(key));
    REQUIRE(tinted != nullptr);
    REQUIRE(tinted->header.signature == fo4::Signature::from_string("KYWD"));
    const fo4::Subrecord* color = nullptr;
    const fo4::Subrecord* type = nullptr;
    for (const auto& subrecord : tinted->subrecords) {
        if (subrecord.signature == fo4::Signature::from_string("CNAM")) color = &subrecord;
        if (subrecord.signature == fo4::Signature::from_string("TNAM")) type = &subrecord;
    }
    REQUIRE(color != nullptr);
    REQUIRE(color->data.size() == 4);
    CHECK(color->data[0] == std::byte{0x33});
    CHECK(color->data[1] == std::byte{0x66});
    CHECK(color->data[2] == std::byte{0xCC});
    CHECK(color->data[3] == std::byte{0xFF});
    REQUIRE(type != nullptr);
    REQUIRE(type->data.size() == 4);
    CHECK(type->data[0] == std::byte{6});

    const auto* numbered = fo4::find_record(disk, fo4::FormKey::parse(byNumber));
    REQUIRE(numbered != nullptr);
    for (const auto& subrecord : numbered->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("CNAM")) continue;
        REQUIRE(subrecord.data.size() == 4);
        CHECK(subrecord.data[0] == std::byte{1});
        CHECK(subrecord.data[3] == std::byte{4});
    }

    const auto badColor = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD",
                               {{"editorId", "BadColor"}, {"color", "12345"}}}));
    REQUIRE_FALSE(badColor.has_value());
    CHECK(badColor.error().type == "InvalidRecordCreationPayload");

    const auto badType = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD",
                               {{"editorId", "BadType"}, {"type", 99}}}));
    REQUIRE_FALSE(badType.has_value());
    CHECK(badType.error().type == "InvalidRecordCreationPayload");

    // Detects: a keyword has no flags subrecord, so asking for flags must be refused, not silently dropped.
    const auto flags = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "KYWD", {{"editorId", "FlaggedKeyword"}, {"flags", 1}}}));
    REQUIRE_FALSE(flags.has_value());
    CHECK(flags.error().type == "InvalidRecordCreationPayload");
}

// Detects: ALCH creation must persist its supplied magic effect.
TEST_CASE("ALCH creation writes the effect list from the payload") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);

    const auto magic = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "MGEF", {{"editorId", "CureMagic"}}})));
    const auto cure = createdFormKey(backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "ALCH",
                               {{"editorId", "RadCure"},
                                {"effects", nlohmann::json::array(
                                    {{{"effect", magic}, {"magnitude", -10.0},
                                      {"area", 0}, {"duration", 0}}})}}})));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* formula = fo4::find_record(disk, fo4::FormKey::parse(cure));
    REQUIRE(formula != nullptr);
    REQUIRE(formula->header.signature == fo4::Signature::from_string("ALCH"));

    const auto effects = fo4ide::plugin::effects::read(disk, *formula);
    REQUIRE(effects.size() == 1);
    CHECK(effects[0].effect.to_string() == magic);
    CHECK(effects[0].magnitude == -10.0f);

    const auto badEffect = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "ALCH",
                               {{"editorId", "BadCure"},
                                {"effects", nlohmann::json::array(
                                    {{{"effect", "000800:Nope.esp"}}})}}}));
    REQUIRE_FALSE(badEffect.has_value());
    CHECK(badEffect.error().type == "MagicEffectEntryRefused");
}

// Detects: effect writes must reject out-of-range magnitudes without mutating existing effects.
TEST_CASE("Magic effect writes refuse magnitudes outside float range") {
    const auto root = formListTempRoot();
    FormListCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeFormListPlugin(path);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadFormListPlugin(backend, path);
    const auto magic = createdFormKey(backend.invokeBackend("CreateRecord",
        nlohmann::json::array({"Patch.esp", "MGEF", {{"editorId", "Magic"}}})));
    const auto cure = createdFormKey(backend.invokeBackend("CreateRecord",
        nlohmann::json::array({"Patch.esp", "ALCH", {{"editorId", "Cure"}}})));
    REQUIRE(backend.addMagicEffect(nlohmann::json::array({"Patch.esp", cure, magic, 2.0, 0, 0})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    for (const auto magnitude : {1e100, -1e100}) {
        const auto create = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "ALCH",
            {{"editorId", "BadCure"}, {"effects", nlohmann::json::array({{{"effect", magic}, {"magnitude", magnitude}}})}}}));
        REQUIRE_FALSE(create.has_value());
        CHECK(create.error().type == "MagicEffectEntryRefused");
        CHECK_FALSE(backend.addMagicEffect(nlohmann::json::array({"Patch.esp", cure, magic, magnitude, 0, 0})).has_value());
        CHECK_FALSE(backend.setMagicEffect(nlohmann::json::array({"Patch.esp", cure, 0, magic, magnitude, 0, 0})).has_value());
    }
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(disk, fo4::FormKey::parse(cure));
    REQUIRE(record != nullptr);
    const auto effects = fo4ide::plugin::effects::read(disk, *record);
    REQUIRE(effects.size() == 1);
    CHECK(effects[0].magnitude == 2.0f);
    for (const auto* item : fo4::collect_records(disk)) CHECK(item->form_key != fo4::FormKey::parse("000802:Patch.esp"));
}
