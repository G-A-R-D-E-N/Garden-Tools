#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_editable_" + std::to_string(id));
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

std::vector<std::byte> makePlugin(std::uint32_t tes4Flags = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", tes4Flags, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"EditableProbe\0", 14});
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

TEST_CASE("Native editable plugin APIs agree on protected and writable plugins") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto fallout = root / "Fallout4.esm";
    const auto creationClub = root / "ccProbe.esl";
    const auto patch = root / "Patch.esp";

    writeBytes(fallout, makePlugin(fo4::RecordFlags::master));
    writeBytes(creationClub, makePlugin(fo4::RecordFlags::small_file));
    writeBytes(patch, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Fallout4.esm", fallout, "Data"),
            pluginEntry("ccProbe.esl", creationClub, "Creation Club"),
            pluginEntry("Patch.esp", patch, "Test Mod")
        })}
    };
    const auto loaded = backend.invoke("environment.LoadMo2", environment);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->at("loadedPlugins") == 3);

    const auto directEditable = parseStringResult(
        backend.invoke("backend.GetEditablePlugins", nlohmann::json::array()));
    const auto routedEditable = parseStringResult(
        backend.invokeBackend("GetEditablePlugins", nlohmann::json::array()));
    const auto mastersEditable = parseStringResult(
        backend.invoke("masters.GetPlugins", nlohmann::json::array()));

    REQUIRE(directEditable.size() == 1);
    CHECK(directEditable[0] == "Patch.esp");
    CHECK(routedEditable == directEditable);
    CHECK(mastersEditable == directEditable);

    const auto directActive = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    const auto routedActive = parseStringResult(
        backend.invokeBackend("GetActivePlugins", nlohmann::json::array()));
    CHECK(routedActive == directActive);
    REQUIRE(directActive.size() == 3);
    CHECK(directActive[0].at("Name") == "Fallout4.esm");
    CHECK(directActive[0].at("Editable") == false);
    CHECK(directActive[1].at("Name") == "ccProbe.esl");
    CHECK(directActive[1].at("Editable") == false);
    CHECK(directActive[2].at("Name") == "Patch.esp");
    CHECK(directActive[2].at("Editable") == true);
}

TEST_CASE("Native editable check protects plugins physically inside the Data directory") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto dataDir = root / "Fallout 4" / "Data";

    // A plugin whose MO2 source tag is missing or wrong must still be
    // protected when the file itself lives inside the registered Data dir.
    const auto vanilla = dataDir / "Vanilla.esp";
    writeBytes(vanilla, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(dataDir);

    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Vanilla.esp", vanilla, "")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    REQUIRE(active.size() == 1);
    CHECK(active[0].at("Name") == "Vanilla.esp");
    CHECK(active[0].at("Editable") == false);

    const auto save = backend.invokeBackend(
        "SavePlugin", nlohmann::json::array({"Vanilla.esp", ""}));
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().type == "PluginReadOnly");
}

TEST_CASE("Native editable check keeps plugins outside the Data directory writable") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto dataDir = root / "Fallout 4" / "Data";
    const auto modDir = root / "mods" / "MyMod";
    std::filesystem::create_directories(dataDir);

    // Same basename as a hypothetical Data plugin, but living in a mod dir.
    const auto modded = modDir / "Modded.esp";
    writeBytes(modded, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(dataDir);

    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Modded.esp", modded, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto editable = parseStringResult(
        backend.invoke("backend.GetEditablePlugins", nlohmann::json::array()));
    REQUIRE(editable.size() == 1);
    CHECK(editable[0] == "Modded.esp");
}
