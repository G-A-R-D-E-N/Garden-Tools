#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_mutation_" + std::to_string(id));
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

void loadBackend(fo4ide::plugin::RadiclePluginBackend& backend,
                 const std::filesystem::path& pluginPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", pluginPath.string()},
            {"source", "test"},
            {"resolved", true}
        }})}
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

    const auto reloaded = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(
        reloaded, fo4::FormKey::parse("000800:Patch.esp"));
    REQUIRE(record != nullptr);
    CHECK(editorId(*record) == "NewEditorId");
    REQUIRE(record->subrecords.size() == 2);
    CHECK(record->subrecords[1].signature.to_string() == "FULL");

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
    CHECK(result.error().code == "FieldWriteUnsupported");

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
    CHECK(result.error().code == "RecordNotFound");
}
