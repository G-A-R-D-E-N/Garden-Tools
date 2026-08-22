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
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_addmasters_" + std::to_string(id));
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

std::vector<std::byte> makeMaster(bool light = false) {
    fo4::test::ByteWriter out;
    const auto flags = fo4::RecordFlags::master |
        (light ? fo4::RecordFlags::small_file : 0u);
    const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "KYWD", 0);
    const auto record = fo4::test::begin_record(out, "KYWD", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterKeyword\0", 14});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makeTarget() {
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

std::string textResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

}

TEST_CASE("Native AddMasters appends loaded master and preserves target FormKey") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto message = textResult(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm"})));
    CHECK(message.find("Added 1 master") == 0);

    const auto reloaded = fo4::read_plugin_file(targetPath);
    REQUIRE(reloaded.header.masters.size() == 1);
    CHECK(reloaded.header.masters[0].key.filename() == "Master.esm");
    CHECK(reloaded.header.masters[0].kind == fo4::PluginKind::full);

    const auto* local = fo4::find_record(
        reloaded, fo4::FormKey::parse("000810:Target.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x01000810});
}

TEST_CASE("Native AddMasters preserves actual light kind for flagged ESP master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "FlaggedLight.esp";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster(true));
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("FlaggedLight.esp", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "FlaggedLight.esp"})).has_value());

    // The written plugin cannot carry the master's kind (readers derive it
    // from the filename), so assert on the backend's resolved state, which is
    // what a later remap will use. An ESL-flagged .esp must be light.
    const auto listedRaw = backend.invoke("masters.List",
                                          nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 1);
    CHECK(listed.at("masters")[0].at("name") == "FlaggedLight.esp");
    CHECK(listed.at("masters")[0].at("kind") == "light");
}

TEST_CASE("Native AddMasters duplicate is a no-op") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm"})).has_value());
    const auto before = fo4::read_plugin_file(targetPath).original_bytes;

    const auto message = textResult(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm, Master.esm"})));
    CHECK(message.find("No new masters") == 0);
    CHECK(fo4::read_plugin_file(targetPath).original_bytes == before);
}

TEST_CASE("Native AddMasters rejects self master and unknown ESP kind") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto targetPath = root / "Target.esp";
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto self = backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Target.esp"}));
    REQUIRE_FALSE(self.has_value());
    CHECK(self.error().type == "SelfMaster");

    const auto unknown = backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Unknown.esp"}));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "MasterNotLoaded");
    CHECK(fo4::read_plugin_file(targetPath).header.masters.empty());
}

TEST_CASE("Native AddMasters refuses protected target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster());
    writeBytes(targetPath, makeTarget());
    const auto before = fo4::read_plugin_file(targetPath).original_bytes;

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath, "Data")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginReadOnly");
    CHECK(fo4::read_plugin_file(targetPath).original_bytes == before);
}
