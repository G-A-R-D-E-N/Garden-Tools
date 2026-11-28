#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/NifService.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/ServiceTargetFakes.hpp"
#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_backend_" + id);
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

std::vector<std::byte> makePlugin(std::string_view master,
                                  std::string_view signature,
                                  std::uint32_t formId,
                                  std::string_view editorId,
                                  std::optional<std::uint32_t> link = std::nullopt) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

    if (!master.empty()) {
        std::vector<std::byte> masterName;
        masterName.reserve(master.size() + 1);
        for (const char ch : master) {
            masterName.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        masterName.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", masterName);

        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, formId);

    std::vector<std::byte> editorBytes;
    editorBytes.reserve(editorId.size() + 1);
    for (const char ch : editorId) {
        editorBytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    editorBytes.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", editorBytes);

    if (link.has_value()) {
        fo4::test::ByteWriter linked;
        linked.u32(*link);
        fo4::test::write_subrecord(out, "NAME", linked.bytes);
    }

    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::vector<std::byte> collisionLayerPlugin(std::uint32_t layer, std::string_view name) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "COLL", 0);
    const auto record = fo4::test::begin_record(out, "COLL", 0, 0x800);
    fo4::test::write_subrecord(out, "EDID", std::string(name) + '\0');
    fo4::test::ByteWriter index;
    index.u32(layer);
    fo4::test::write_subrecord(out, "BNAM", index.bytes);
    fo4::test::write_subrecord(out, "MNAM", std::string(name) + '\0');
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
    out.u16(static_cast<std::uint16_t>(text.size()));
    out.raw(text);
}

std::vector<std::byte> makeTrailingVmadPlugin(
                                              std::initializer_list<std::string_view> masters,
                                              std::string_view signature,
                                              std::uint32_t formId,
                                              std::uint32_t linkFormId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

    for (const auto master : masters) {
        std::vector<std::byte> masterName;
        masterName.reserve(master.size() + 1);
        for (const char ch : master) {
            masterName.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        masterName.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", masterName);

        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, formId);

    fo4::test::ByteWriter vmad;
    vmad.u16(5);
    vmad.u16(2);
    vmad.u16(1);
    wstring(vmad, "DemoScript");
    vmad.u8(0);
    vmad.u16(1);
    wstring(vmad, "LinkedRef");
    vmad.u8(1);
    vmad.u8(1);
    vmad.u16(0);
    vmad.u16(0xFFFF);
    vmad.u32(linkFormId);
    vmad.u32(0x0100009Bu);
    vmad.u32(0x0100009Cu);
    fo4::test::write_subrecord(out, "VMAD", vmad.bytes);

    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> twoKeywordPlugin(std::uint32_t formId) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    for (const auto editorId : {"DupFirst", "DupLast"}) {
        const auto record = fo4::test::begin_record(out, "KYWD", 0, formId);
        fo4::test::write_subrecord(out, "EDID", std::string(editorId) + '\0');
        fo4::test::end_record(out, record);
    }
    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name,
                           const std::filesystem::path& path,
                           std::string source) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", std::move(source)},
        {"resolved", true}
    };
}

void openNestedGroups(fo4::test::ByteWriter& out, int depth) {
    if (depth == 0) return;
    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    openNestedGroups(out, depth - 1);
    fo4::test::end_group(out, group);
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Radicle plugin backend exposes native conflicts references and search") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto basePath = root / "Base.esp";
    const auto overridePath = root / "Override.esp";
    const auto referencePath = root / "Reference.esp";

    writeBytes(basePath, makePlugin({}, "WEAP", 0x00000800, "BaseWeapon"));
    writeBytes(overridePath,
               makePlugin("Base.esp", "WEAP", 0x00000800, "OverrideWeapon"));
    writeBytes(referencePath,
               makePlugin("Base.esp", "MISC", 0x01000801, "ReferenceRecord", 0x00000800));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base"),
            pluginEntry("Override.esp", overridePath, "Override"),
            pluginEntry("Reference.esp", referencePath, "Reference")
        })}
    };

    auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at("loadedPlugins") == 3);
    CHECK(loaded->at("failedPlugins") == 0);

    const auto editable = parseStringResult(
        backend.invoke("backend.GetEditablePlugins", nlohmann::json::array()));
    REQUIRE(editable.size() == 3);
    CHECK(editable[0] == "Base.esp");
    CHECK(editable[1] == "Override.esp");

    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    REQUIRE(active.size() == 3);
    CHECK(active[0].at("Name") == "Base.esp");
    CHECK(active[0].at("LoadOrder") == 0);
    CHECK(active[1].at("LoadOrder") == 1);

    const auto conflicts = parseStringResult(
        backend.invoke("backend.GetConflicts", nlohmann::json::array()));
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].at("FormKey") == "000800:Base.esp");
    CHECK(conflicts[0].at("Winner") == "Override.esp");
    CHECK(conflicts[0].at("Plugins").size() == 2);
    CHECK(conflicts[0].at("Category") == "override");
    CHECK(parseStringResult(backend.invoke("backend.GetConflicts",
        nlohmann::json::array({"no_conflict,only_one"}))).empty());
    CHECK(parseStringResult(backend.invoke("backend.GetConflicts",
        nlohmann::json::array({"override,conflict"}))).size() == 1);
    CHECK(!backend.invoke("backend.GetConflicts", nlohmann::json::array({"bogus"})).has_value());

    const auto matrix = parseStringResult(
        backend.invoke("backend.GetConflictMatrix",
                       nlohmann::json::array({"000800:Base.esp"})));
    CHECK(matrix.at("Winner") == "Override.esp");
    CHECK(matrix.at("EditorID") == "OverrideWeapon");
    CHECK(matrix.at("Type") == "WEAP");
    CHECK(matrix.at("Level") == "conflict");
    CHECK(matrix.at("Category") == "override");
    REQUIRE(matrix.at("Rows").size() >= 2);

    const auto& matrixRows = matrix.at("Rows");
    const auto edid = std::find_if(matrixRows.begin(), matrixRows.end(), [](const auto& row) {
        return row.value("Field", std::string{}) == "Subrecords\\EDID[0]";
    });
    REQUIRE(edid != matrixRows.end());
    REQUIRE(edid->at("Values").size() == 2);
    CHECK(edid->at("Values")[0] == "BaseWeapon");
    CHECK(edid->at("Values")[1] == "OverrideWeapon");
    CHECK(edid->at("Differs") == true);
    CHECK(edid->at("Statuses")[0] == "master");
    CHECK(edid->at("Statuses")[1] == "win");

    const auto referencedBy = parseStringResult(
        backend.invoke("backend.GetReferencedBy",
                       nlohmann::json::array({"000800:Base.esp"})));
    REQUIRE(referencedBy.size() == 1);
    CHECK(referencedBy[0].at("Plugin") == "Reference.esp");
    CHECK(referencedBy[0].at("FormKey") == "000801:Reference.esp");
    CHECK(referencedBy[0].at("EditorID") == "ReferenceRecord");

    const auto dependencies = parseStringResult(
        backend.invoke("backend.GetDependencies",
                       nlohmann::json::array({"000801:Reference.esp"})));
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies[0].at("Kind") == "link");
    CHECK(dependencies[0].at("FormKey") == "000800:Base.esp");
    CHECK(dependencies[0].at("EditorId") == "OverrideWeapon");
    CHECK(dependencies[0].at("Type") == "WEAP");
    CHECK(dependencies[0].at("Plugin") == "Override.esp");

    const auto history = parseStringResult(
        backend.invoke("backend.GetHistory",
                       nlohmann::json::array({"000800:Base.esp"})));
    REQUIRE(history.size() == 2);
    CHECK(history[0].at("Plugin") == "Base.esp");
    CHECK(history[0].at("Action") == "Origin");
    CHECK(history[1].at("Plugin") == "Override.esp");
    CHECK(history[1].at("Action") == "Winning override");
    CHECK(history[1].at("ChangedFields").get<std::size_t>() >= 1);

    const auto search = parseStringResult(
        backend.invoke("backend.SearchRecords",
                       nlohmann::json::array({"ReferenceRecord", ""})));
    REQUIRE(search.size() == 1);
    CHECK(search[0].at("FormKey") == "000801:Reference.esp");
    const auto badType = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", "REFER"})));
    CHECK(badType.empty());

    const auto everything = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", ""})));
    const auto byFormId = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", "", "000801"})));
    REQUIRE_FALSE(byFormId.empty());
    for (const auto& row : byFormId) CHECK(row.at("FormKey").get<std::string>().starts_with("000801:"));

    const auto byEditor = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", "", "", "ReferenceRecord"})));
    REQUIRE(byEditor.size() == 1);
    CHECK(byEditor[0].at("FormKey") == "000801:Reference.esp");

    const auto capped = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", "", "", "", "", "", "", "", 1})));
    REQUIRE(capped.size() == 1);

    const auto allCategories = parseStringResult(backend.invoke("backend.SearchRecords", nlohmann::json::array({"", "", "", "", "", "", "", "unknown,only_one,no_conflict,benign,override,conflict,critical"})));
    CHECK(allCategories.size() == everything.size());
}

// Detects: regex search must match as a case-insensitive pattern, keep the signature filter, and refuse a bad or over-long pattern with InvalidRegex instead of an empty result or an exception.
TEST_CASE("Radicle plugin backend searches records with an optional regex and rejects an invalid pattern") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto basePath = root / "Base.esp";
    const auto referencePath = root / "Reference.esp";
    writeBytes(basePath, makePlugin({}, "WEAP", 0x00000800, "BaseWeapon"));
    writeBytes(referencePath, makePlugin({}, "MISC", 0x00000801, "ReferenceRecord"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base"),
            pluginEntry("Reference.esp", referencePath, "Reference")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto literal = parseStringResult(
        backend.invoke("backend.SearchRecords", nlohmann::json::array({"^base.*weapon$", "", "", "", "", "", "", "", "", "", false})));
    CHECK(literal.empty());

    const auto byEditorId = parseStringResult(
        backend.invoke("backend.SearchRecords", nlohmann::json::array({"^base.*weapon$", "", "", "", "", "", "", "", "", "", true})));
    REQUIRE(byEditorId.size() == 1);
    CHECK(byEditorId[0].at("FormKey") == "000800:Base.esp");

    // Detects: the WebView sidebar passes the regex flag third, which must still switch the search to regex.
    const auto webForm = parseStringResult(
        backend.invoke("backend.SearchRecords", nlohmann::json::array({"^base.*weapon$", "", true})));
    REQUIRE(webForm.size() == 1);
    CHECK(webForm[0].at("FormKey") == "000800:Base.esp");

    const auto byFormKey = parseStringResult(
        backend.invoke("backend.SearchRecords",
                       nlohmann::json::array({"^000801:Reference\\.esp$", "", "", "", "", "", "", "", "", "", true})));
    REQUIRE(byFormKey.size() == 1);
    CHECK(byFormKey[0].at("Type") == "MISC");

    const auto filteredOut = parseStringResult(
        backend.invoke("backend.SearchRecords", nlohmann::json::array({"^MISC$", "WEAP", "", "", "", "", "", "", "", "", true})));
    CHECK(filteredOut.empty());

    const auto invalid = backend.invoke("backend.SearchRecords", nlohmann::json::array({"([", "", "", "", "", "", "", "", "", "", true}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidRegex");

    const auto literalInvalid = parseStringResult(
        backend.invoke("backend.SearchRecords", nlohmann::json::array({"([", "", "", "", "", "", "", "", "", "", false})));
    CHECK(literalInvalid.empty());

    const auto tooLong = backend.invoke(
        "backend.SearchRecords", nlohmann::json::array({std::string(257, 'a'), "", "", "", "", "", "", "", "", "", true}));
    REQUIRE_FALSE(tooLong.has_value());
    CHECK(tooLong.error().type == "InvalidRegex");
}

TEST_CASE("Radicle plugin backend reports dangling FormLinks") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto basePath = root / "Base.esp";
    const auto brokenPath = root / "Broken.esp";
    writeBytes(basePath, makePlugin({}, "WEAP", 0x00000800, "BaseWeapon"));
    writeBytes(brokenPath,
               makePlugin("Base.esp", "MISC", 0x01000801, "BrokenReference", 0x00000999));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base"),
            pluginEntry("Broken.esp", brokenPath, "Broken")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto report = backend.invoke("appInterop.ScanBrokenRefs", nlohmann::json::array());
    REQUIRE(report.has_value());
    REQUIRE(report->is_string());
    CHECK(report->get<std::string>().find("Dangling FormLinks: 1") != std::string::npos);
    CHECK(report->get<std::string>().find("000999:Base.esp") != std::string::npos);

    const auto dependencies = parseStringResult(
        backend.invoke("backend.GetDependencies",
                       nlohmann::json::array({"000801:Broken.esp"})));
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies[0].at("Kind") == "missing");
    CHECK(dependencies[0].at("FormKey") == "000999:Base.esp");

    const auto problems = parseStringResult(
        backend.invoke("backend.GetProblems",
                       nlohmann::json::array({"000801:Broken.esp"})));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].at("Code") == "unresolved_link");
    CHECK(problems[0].at("Severity") == "critical");
    CHECK(problems[0].at("Description").get<std::string>().find("000999:Base.esp") != std::string::npos);
}

TEST_CASE("Radicle GetDuplicateFormIds reports a FormID shared by two records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Dup.esp";
    writeBytes(path, twoKeywordPlugin(0x00000800));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Dup.esp", path, "Dup")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Dup.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_array());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0].at("FormId") == "00000800");
    CHECK((*result)[0].at("Count") == 2);
    CHECK((*result)[0].at("RecordTypes") == nlohmann::json::array({"KYWD", "KYWD"}));
}

TEST_CASE("Radicle GetDuplicateFormIds is empty for a plugin with unique FormIDs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Clean.esp";
    writeBytes(path, makePlugin({}, "KYWD", 0x00000800, "Clean"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Clean.esp", path, "Clean")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Clean.esp"}));
    REQUIRE(result.has_value());
    CHECK(result->is_array());
    CHECK(result->empty());
}

TEST_CASE("Radicle GetDuplicateFormIds reports two zero form IDs as one duplicate row") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Zero.esp";
    writeBytes(path, twoKeywordPlugin(0x00000000));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Zero.esp", path, "Zero")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Zero.esp"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_array());
    REQUIRE(result->size() == 1);
    CHECK((*result)[0].at("FormId") == "00000000");
    CHECK((*result)[0].at("Count") == 2);
    CHECK((*result)[0].at("RecordTypes") == nlohmann::json::array({"KYWD", "KYWD"}));
}

TEST_CASE("Radicle GetDuplicateFormIds reports a removed plugin file as an I/O error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Gone.esp";
    writeBytes(path, twoKeywordPlugin(0x00000800));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Gone.esp", path, "Gone")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(std::filesystem::remove(path));

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Gone.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "DuplicateFormIdIo");
    CHECK(result.error().message.starts_with("Failed to stat plugin file:"));
}

TEST_CASE("Radicle GetDuplicateFormIds reports a truncated plugin file as a scan error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Truncated.esp";
    writeBytes(path, twoKeywordPlugin(0x00000800));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Truncated.esp", path, "Truncated")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    writeBytes(path, std::vector<std::byte>(10, std::byte{0}));

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Truncated.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "DuplicateFormIdScan");
    CHECK(result.error().message == "Truncated plugin record header.");
}

TEST_CASE("Radicle GetDuplicateFormIds reports over-nested groups as a scan error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto path = root / "Nested.esp";
    writeBytes(path, twoKeywordPlugin(0x00000800));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Nested.esp", path, "Nested")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    fo4::test::ByteWriter nested;
    openNestedGroups(nested, 130);
    writeBytes(path, nested.bytes);

    const auto result = backend.invoke("backend.GetDuplicateFormIds",
                                       nlohmann::json::array({"Nested.esp"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "DuplicateFormIdScan");
    CHECK(result.error().message == "Plugin GRUP nesting exceeds maximum depth of 128.");
}

TEST_CASE("Radicle plugin backend caveats partial VMAD coverage as non-exhaustive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto basePath = root / "Base.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(basePath, makePlugin({}, "WEAP", 0x00000800, "BaseWeapon"));
    writeBytes(patchPath,
               makeTrailingVmadPlugin({"Base.esp"}, "ACTI", 0x01000801, 0x0000009A));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base"),
            pluginEntry("Patch.esp", patchPath, "Patch")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto report = backend.invoke("appInterop.ScanBrokenRefs", nlohmann::json::array());
    REQUIRE(report.has_value());
    REQUIRE(report->is_string());
    const auto text = report->get<std::string>();

    CHECK(text.find("1 record(s) contain VMAD script data") != std::string::npos);
    CHECK(text.find("the results below are not exhaustive") != std::string::npos);
    CHECK(text.find("NOT included") == std::string::npos);

    CHECK(text.find("Dangling FormLinks: 1") != std::string::npos);
    CHECK(text.find("00009A:Base.esp") != std::string::npos);
}

TEST_CASE("Radicle masters list keeps every master used when VMAD is incomplete") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto patchPath = root / "Patch.esp";
    writeBytes(patchPath,
               makeTrailingVmadPlugin({"Base.esp", "Other.esm"}, "ACTI",
                                      0x01000801, 0x0000009A));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", patchPath, "Patch")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto masters = parseStringResult(
        backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"})));
    REQUIRE(masters.at("masters").size() == 2);
    CHECK(masters.at("masters")[0].at("name") == "Base.esp");
    CHECK(masters.at("masters")[0].at("used") == true);
    CHECK(masters.at("masters")[1].at("name") == "Other.esm");
    CHECK(masters.at("masters")[1].at("used") == true);
}

TEST_CASE("Radicle plugin backend refreshes the cached graph when the environment changes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto basePath = root / "Base.esp";
    const auto overridePath = root / "Override.esp";
    writeBytes(basePath, makePlugin({}, "WEAP", 0x00000800, "BaseWeapon"));
    writeBytes(overridePath,
               makePlugin("Base.esp", "WEAP", 0x00000800, "OverrideWeapon"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json withOverride{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base"),
            pluginEntry("Override.esp", overridePath, "Override")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", withOverride).has_value());
    CHECK(parseStringResult(
              backend.invoke("backend.GetConflicts", nlohmann::json::array())).size() == 1);

    const nlohmann::json baseOnly{
        {"plugins", nlohmann::json::array({
            pluginEntry("Base.esp", basePath, "Base")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", baseOnly).has_value());
    CHECK(parseStringResult(
              backend.invoke("backend.GetConflicts", nlohmann::json::array())).empty());
}

// Detects: a CollisionInfo reply that keeps the layer number although a loaded COLL record names that layer, so the panel never shows the name.
TEST_CASE("NifTarget CollisionInfo names a body's layer from the loaded COLL records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    writeText(obj, "v 0 0 0\nv 70 0 0\nv 0 70 0\nf 1 2 3\n");
    const auto nif = root / "box.nif";
    fo4ide::assets::NifService service;
    fo4ide::assets::NifImportOptions options;
    options.collision = fo4ide::assets::NifCollisionShape::box;
    REQUIRE(service.importObj(obj, nif, options).has_value());

    const auto plugin = root / "Collision.esm";
    writeBytes(plugin, collisionLayerPlugin(1, "L_STATIC"));
    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Collision.esm", plugin, "test")})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    fo4ide::app::test::FakePlatform platform;
    fo4ide::assets::TextureResolver textures;
    fo4ide::app::NifTarget target(service, textures, platform, backend);
    auto info = target.invoke("CollisionInfo", nlohmann::json::array({nif.string()}));
    REQUIRE(info.has_value());
    const auto body = nlohmann::json::parse(info->get<std::string>()).at("systems")[0].at("bodies")[0];
    CHECK(body.at("layer") == 1);
    CHECK(body.at("layerName") == "L_STATIC");
}

// Detects: a session with no plugin loaded reports no layer names, because the backend never reads the base game's COLL records.
TEST_CASE("Radicle backend reads COLL layer names from the base game when no plugin is loaded") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Fallout4.esm", collisionLayerPlugin(3, "L_TEST"));
    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root);

    auto rows = backend.invokeBackend("GetCollisionLayers", nlohmann::json::array());
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    CHECK((*rows)[0].at("index") == 3);
    CHECK((*rows)[0].at("name") == "L_TEST");
}
