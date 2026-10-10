#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_editor_id_text_" + std::to_string(id));
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

std::vector<std::byte> makePlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Existing\0", 9});
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

nlohmann::json eyesPayload() {
    return {
        {"editorId", "CreatedEyes"},
        {"fullText", "Created Eyes"},
        {"icon", "Textures\\Eyes\\Created.dds"},
        {"dataFlags", 5},
        {"recordFlags", 0x00000404u}
    };
}

nlohmann::json parseStringResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return nlohmann::json::parse(result->get<std::string>());
}

}

TEST_CASE("Native EditorId reads decode Windows-1252 for listings and lookups") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto pluginPath = root / "mods" / "MyMod" / "Patch.esp";
    writeBytes(pluginPath, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", pluginPath, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    auto payload = eyesPayload();
    payload["editorId"] = "Caf\xC3\xA9";
    const auto created = parseStringResult(backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "EYES", payload})));
    REQUIRE(created.at("formKey") == "000900:Patch.esp");

    SECTION("listing returns the decoded EditorId") {
        const auto listed = parseStringResult(backend.invokeBackend(
            "SearchRecords", nlohmann::json::array({"", "EYES"})));
        REQUIRE(listed.is_array());
        REQUIRE(listed.size() == 1);
        CHECK(listed[0].at("EditorID") == "Caf\xC3\xA9");
    }

    SECTION("lookup by the decoded EditorId finds the record") {
        const auto conditions = parseStringResult(backend.invokeBackend(
            "GetConditions", nlohmann::json::array({"Patch.esp", "Caf\xC3\xA9"})));
        CHECK(conditions.is_array());
        CHECK(conditions.empty());
    }
}
