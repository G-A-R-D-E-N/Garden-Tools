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

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

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

    const auto matrix = parseStringResult(
        backend.invoke("backend.GetConflictMatrix",
                       nlohmann::json::array({"000800:Base.esp"})));
    CHECK(matrix.at("Winner") == "Override.esp");
    CHECK(matrix.at("EditorID") == "OverrideWeapon");
    CHECK(matrix.at("Type") == "WEAP");
    CHECK(matrix.at("Level") == "conflict");
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
