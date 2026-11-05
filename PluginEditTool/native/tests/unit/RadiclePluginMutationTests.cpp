#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_mutation_" + id);
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

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"OldEditorId\0", 12});
    fo4::test::write_subrecord(out, "FULL", std::string_view{"KeepMe\0", 7});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeMultipleConditionListsPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", 0, 0x00000800);
    fo4::test::ByteWriter number;
    number.u32(1);
    fo4::test::write_subrecord(out, "ANAM", number.bytes);
    number.bytes.clear();
    number.u32(0);
    fo4::test::write_subrecord(out, "ALST", number.bytes);
    fo4::test::write_subrecord(out, "ALID", std::string_view{"Alias\0", 6});
    fo4::test::ByteWriter condition;
    condition.u32(0);
    condition.f32(1.0f);
    condition.u16(24);
    condition.u16(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0xFFFFFFFFu);
    fo4::test::write_subrecord(out, "CTDA", condition.bytes);
    fo4::test::write_subrecord(out, "ALFA", number.bytes);
    fo4::test::write_subrecord(out, "CTDA", condition.bytes);
    fo4::test::write_subrecord(out, "ALED", std::span<const std::byte>{});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeSourceOverride() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "WEAP", 0);
    const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"SourceWeapon\0", 13});
    fo4::test::ByteWriter name;
    name.u32(0x00000801);
    fo4::test::write_subrecord(out, "NAME", name.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeTargetPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetLocal\0", 12});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeRootAndBranchPackPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "PACK", 0);
    const auto record = fo4::test::begin_record(out, "PACK", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"RootPack\0", 9});
    fo4::test::ByteWriter condition;
    condition.u32(0);
    condition.f32(1.0f);
    condition.u16(8);
    condition.u16(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0);
    condition.u32(0xFFFFFFFFu);
    fo4::test::write_subrecord(out, "CTDA", condition.bytes);
    fo4::test::write_subrecord(out, "ANAM", std::string_view{"Sandbox\0", 8});
    fo4::test::ByteWriter count;
    count.u32(1);
    fo4::test::write_subrecord(out, "CITC", count.bytes);
    fo4::test::write_subrecord(out, "CTDA", condition.bytes);
    fo4::test::write_subrecord(out, "PRCB", std::span<const std::byte>{});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
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

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

nlohmann::json pluginEntry(std::string name,
                           const std::filesystem::path& path,
                           std::string source = "test") {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", std::move(source)},
        {"resolved", true}
    };
}

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& pluginPath,
                 std::string source = "test") {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, std::move(source))
        })}
    };
    auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 1);
}

}

TEST_CASE("Native SetField persists EDID and refreshes the record matrix") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            "Subrecords\\EDID[0]",
            "NewEditorId"
        }));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("Updated EDID") != std::string::npos);

    const auto beforeSave = fo4::read_plugin_file(path);
    const auto* beforeSaveRecord = fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(beforeSaveRecord != nullptr);
    CHECK(editorId(*beforeSaveRecord) == "OldEditorId");

    const auto matrix = parseStringResult(
        backend.invoke("backend.GetConflictMatrix",
                       nlohmann::json::array({"000800:Patch.esp"})));
    CHECK(matrix.at("EditorID") == "NewEditorId");
    const auto& rows = matrix.at("Rows");
    const auto edid = std::find_if(rows.begin(), rows.end(), [](const auto& row) {
        return row.value("Field", std::string{}) == "Subrecords\\EDID[0]";
    });
    REQUIRE(edid != rows.end());
    CHECK(edid->at("Values")[0] == "NewEditorId");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        reloaded, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    CHECK(editorId(*record) == "NewEditorId");
    REQUIRE(record->subrecords.size() == 2);
    CHECK(record->subrecords[1].signature.to_string() == "FULL");
}

TEST_CASE("Native SetConditions refuses a record whose lists are all nested without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeMultipleConditionListsPlugin();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const nlohmann::json rows = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
    });
    const auto result = backend.invokeBackend(
        "SetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", rows.dump()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NoConditionList");
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK_FALSE(active.at(0).at("Pending").get<bool>());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native SetConditions refuses a record with no condition list without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makePlugin();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const nlohmann::json rows = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
    });
    const auto result = backend.invokeBackend(
        "SetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", rows.dump()}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "NoConditionList");
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK_FALSE(active.at(0).at("Pending").get<bool>());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native SetConditions accepts an empty list on a record with no condition list without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makePlugin();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "[]"}));
    REQUIRE(result.has_value());
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK_FALSE(active.at(0).at("Pending").get<bool>());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native bare condition reads skip nested lists and scoped reads report multiple lists without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeMultipleConditionListsPlugin();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto whole = backend.invokeBackend(
        "GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"}));
    REQUIRE(whole.has_value());
    CHECK(whole->get<std::string>() == "[]");
    const auto scoped = backend.invokeBackend(
        "GetConditionsAtPath", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Aliases[0].Conditions"}));
    CHECK_FALSE(scoped.has_value());
    if (!scoped.has_value()) {
        CHECK(scoped.error().type == "ConditionScopeMultipleLists");
    }
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK_FALSE(active.at(0).at("Pending").get<bool>());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native SetConditionsAtPath reports multiple lists without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    const auto before = makeMultipleConditionListsPlugin();
    writeBytes(path, before);
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetConditionsAtPath",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Aliases[0].Conditions", "[]"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ConditionScopeMultipleLists");
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    CHECK_FALSE(active.at(0).at("Pending").get<bool>());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native bare Conditions edits the PACK root list and leaves the branch identical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makeRootAndBranchPackPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto branchArgs = nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "Branches[0].Conditions"});
    const auto branchBefore = backend.invokeBackend("GetConditionsAtPath", branchArgs);
    REQUIRE(branchBefore.has_value());
    const auto rootBefore = parseStringResult(backend.invokeBackend(
        "GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(rootBefore.size() == 1);
    CHECK(rootBefore[0].at("function") == "GetAngle");

    const nlohmann::json rows = nlohmann::json::array({
        {
            {"function", "GetStage"},
            {"operator", ">="},
            {"value", 5},
            {"param1", 2},
            {"runOn", "Subject"},
        },
    });
    const auto message = backend.invokeBackend(
        "SetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp", rows.dump()}));
    REQUIRE(message.has_value());
    CHECK(message->get<std::string>().find("Set 1 condition(s)") == 0);

    const auto rootAfter = parseStringResult(backend.invokeBackend(
        "GetConditions", nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    REQUIRE(rootAfter.size() == 1);
    CHECK(rootAfter[0].at("function") == "GetStage");
    const auto branchAfter = backend.invokeBackend("GetConditionsAtPath", branchArgs);
    REQUIRE(branchAfter.has_value());
    CHECK(branchAfter->get<std::string>() == branchBefore->get<std::string>());
}

TEST_CASE("Native SetField rejects opaque raw fields without touching disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            "Subrecords\\FULL[0]",
            "unsafe"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FieldWriteUnsupported");

    const auto reloaded = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        reloaded, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    CHECK(editorId(*record) == "OldEditorId");
}

TEST_CASE("Native SetField reports missing records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path);

    const auto result = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "Patch.esp",
            "000999:Patch.esp",
            "Subrecords\\EDID[0]",
            "Missing"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "RecordNotFound");
}

TEST_CASE("Native writes refuse plugins resolved directly from game Data") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, makePlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(path));

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, path, "Data");

    const auto edit = backend.invokeBackend(
        "SetField",
        nlohmann::json::array({
            "Patch.esp",
            "000800:Patch.esp",
            "Subrecords\\EDID[0]",
            "Blocked"
        }));
    REQUIRE_FALSE(edit.has_value());
    CHECK(edit.error().type == "PluginReadOnly");

    const auto save = backend.invokeBackend(
        "SavePlugin", nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().type == "PluginReadOnly");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == before);
}

TEST_CASE("Native CopyAsOverride persists a destination-safe override") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Source.esp";
    const auto targetPath = root / "Target.esp";
    writeBytes(sourcePath, makeSourceOverride());
    writeBytes(targetPath, makeTargetPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto copied = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp",
            "000800:Master.esm",
            "Target.esp",
            false
        }));
    REQUIRE(copied.has_value());
    CHECK(copied->get<std::string>().find("Copied") == 0);

    const auto beforeSave = fo4::read_plugin_file(targetPath);
    CHECK(beforeSave.header.masters.empty());
    CHECK(fo4::find_record(
        beforeSave, fo4::FormKey::parse("000800:Master.esm")) == nullptr);

    const auto pendingMatrix = parseStringResult(
        backend.invoke("backend.GetConflictMatrix",
                       nlohmann::json::array({"000800:Master.esm"})));
    CHECK(pendingMatrix.at("Winner") == "Target.esp");

    const auto exists = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp",
            "000800:Master.esm",
            "Target.esp",
            false
        }));
    REQUIRE(exists.has_value());
    CHECK(exists->get<std::string>().find("EXISTS:") == 0);

    const auto replaced = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp",
            "000800:Master.esm",
            "Target.esp",
            true
        }));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().find("Replaced") == 0);

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
    const auto reloaded = fo4::read_plugin_file(targetPath);
    REQUIRE(reloaded.header.masters.size() == 1);
    CHECK(reloaded.header.masters[0].key.filename() == "Master.esm");

    const auto* local = fo4::find_record(
        reloaded, fo4::FormKey::parse("000810:Target.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000810});

    const auto* overrideRecord = fo4::find_record(
        reloaded, fo4::FormKey::parse("000800:Master.esm"));
    REQUIRE(overrideRecord != nullptr);
    CHECK(editorId(*overrideRecord) == "SourceWeapon");

    const auto links = fo4::collect_form_links(reloaded);
    const auto link = std::find_if(links.begin(), links.end(), [](const auto& hit) {
        return hit.record == fo4::FormKey::parse("000800:Master.esm") &&
               hit.subrecord.to_string() == "NAME";
    });
    REQUIRE(link != links.end());
    CHECK(link->key == fo4::FormKey::parse("000801:Master.esm"));
}

TEST_CASE("Native CopyAsOverride asks before writing an override that would lose") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Source.esp";
    const auto targetPath = root / "Target.esp";
    writeBytes(sourcePath, makeSourceOverride());
    writeBytes(targetPath, makeTargetPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Target.esp", targetPath),
            pluginEntry("Source.esp", sourcePath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto winner = [&] {
        return parseStringResult(backend.invoke(
            "backend.GetConflictMatrix",
            nlohmann::json::array({"000800:Master.esm"}))).at("Plugins").size();
    };
    REQUIRE(winner() == 1);

    const auto asked = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({"Source.esp", "000800:Master.esm", "Target.esp", false}));
    REQUIRE(asked.has_value());
    const auto message = asked->get<std::string>();
    CHECK(message.find("LOSES:") == 0);
    CHECK(message.find("Source.esp") != std::string::npos);
    CHECK(winner() == 1);

    const auto invalid = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({"Source.esp", "000800:Master.esm", "Target.esp", false, 1}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidAllowLosingFlag");

    const auto missing = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({"Source.esp", "000999:Master.esm", "Target.esp", false}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "RecordNotFound");

    const auto copied = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({"Source.esp", "000800:Master.esm", "Target.esp", false, true}));
    REQUIRE(copied.has_value());
    CHECK(copied->get<std::string>().find("Copied") == 0);
    const auto matrix = parseStringResult(backend.invoke(
        "backend.GetConflictMatrix", nlohmann::json::array({"000800:Master.esm"})));
    CHECK(matrix.at("Plugins").size() == 2);
    CHECK(matrix.at("Winner") == "Source.esp");
}

TEST_CASE("Native CopyAsOverride refuses protected destination plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourcePath = root / "Source.esp";
    const auto targetPath = root / "Target.esp";
    writeBytes(sourcePath, makeSourceOverride());
    writeBytes(targetPath, makeTargetPlugin());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(targetPath));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Source.esp", sourcePath),
            pluginEntry("Target.esp", targetPath, "Data")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invokeBackend(
        "CopyAsOverride",
        nlohmann::json::array({
            "Source.esp",
            "000800:Master.esm",
            "Target.esp",
            false
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginReadOnly");
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(targetPath)) == before);
}
