#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/flags.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

bool symlinkNotPermitted(const std::error_code& ec) {
    return ec.category() == std::system_category() && ec.value() == 1314;  // ERROR_PRIVILEGE_NOT_HELD
}

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_editable_" + id);
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

TEST_CASE("OpenPlugin never grants Save-As authority over its directory") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "trusted-mod");
    std::filesystem::create_directories(root / "untrusted-dir");

    // Trusted: arrives through the LoadMo2 environment snapshot.
    const auto trustedPath = root / "trusted-mod" / "Patch.esp";
    writeBytes(trustedPath, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", trustedPath, "Trusted Mod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    // Untrusted: opened ad hoc from an arbitrary directory.
    const auto openedPath = root / "untrusted-dir" / "Opened.esp";
    writeBytes(openedPath, makePlugin());
    REQUIRE(backend.invoke(
        "backend.OpenPlugin", nlohmann::json::array({openedPath.string()})).has_value());

    // Save-As into the trusted plugin's directory works.
    const auto allowed = backend.invokeBackend(
        "SavePlugin",
        nlohmann::json::array({"Patch.esp", (root / "trusted-mod" / "Copy.esp").string()}));
    REQUIRE(allowed.has_value());

    // Save-As into the OpenPlugin'd file's directory is refused: opening a
    // random file must not bootstrap an arbitrary write root.
    const auto denied = backend.invokeBackend(
        "SavePlugin",
        nlohmann::json::array({"Opened.esp", (root / "untrusted-dir" / "Out.esp").string()}));
    REQUIRE_FALSE(denied.has_value());
    CHECK(denied.error().type == "SavePathNotAllowed");

    // In-place saves of the opened plugin itself still work (same path).
    const auto inPlace = backend.invokeBackend(
        "SavePlugin", nlohmann::json::array({"Opened.esp", ""}));
    CHECK(inPlace.has_value());
}

TEST_CASE("OpenPlugin cannot retarget a trusted plugin via a same-name collision") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "trusted-mod");
    std::filesystem::create_directories(root / "arbitrary");

    const auto trustedPath = root / "trusted-mod" / "Patch.esp";
    writeBytes(trustedPath, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(root / "Fallout 4" / "Data");

    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", trustedPath, "Trusted Mod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    // A different file with the same plugin name, at an arbitrary location.
    // findPlugin() matches by PluginKey as well as name, so this collides.
    const auto impostorPath = root / "arbitrary" / "Patch.esp";
    writeBytes(impostorPath, makePlugin());
    const auto opened = backend.invoke(
        "backend.OpenPlugin", nlohmann::json::array({impostorPath.string()}));
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().type == "PluginAlreadyLoaded");

    // The trusted entry must still point at the original file.
    const auto active = parseStringResult(
        backend.invoke("backend.GetActivePlugins", nlohmann::json::array()));
    REQUIRE(active.size() == 1);
    CHECK(active[0].at("Name") == "Patch.esp");

    // And the arbitrary directory must not have become a Save-As root.
    const auto denied = backend.invokeBackend(
        "SavePlugin",
        nlohmann::json::array({"Patch.esp", (root / "arbitrary" / "Out.esp").string()}));
    REQUIRE_FALSE(denied.has_value());
    CHECK(denied.error().type == "SavePathNotAllowed");
}

TEST_CASE("Save As refuses a Data-directory destination even with a writable source") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto dataDir = root / "Fallout 4" / "Data";
    const auto modDir = root / "mods" / "MyMod";
    const auto dataPlugin = dataDir / "DataOnly.esp";
    const auto patch = modDir / "Patch.esp";
    writeBytes(dataPlugin, makePlugin());
    writeBytes(patch, makePlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(dataDir);
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("DataOnly.esp", dataPlugin, "Data"),
            pluginEntry("Patch.esp", patch, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto saved = backend.invokeBackend(
        "SavePlugin",
        nlohmann::json::array({"Patch.esp", (dataDir / "Injected.esp").string()}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "SavePathNotAllowed");
}

TEST_CASE("Save As does not follow a trusted root after it becomes a symlink") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto sourceDir = root / "mods" / "Source";
    const auto trustedRoot = root / "mods" / "Trusted";
    const auto relocatedRoot = root / "relocated";
    const auto originalRoot = root / "original";
    const auto source = sourceDir / "Patch.esp";
    const auto anchor = trustedRoot / "Anchor.esp";
    writeBytes(source, makePlugin());
    writeBytes(anchor, makePlugin());
    std::filesystem::create_directories(relocatedRoot);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", source, "Source"),
            pluginEntry("Anchor.esp", anchor, "Trusted")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    // Replacing the original trusted directory with a symlink must not grant
    // Save-As authority over its new target.
    std::filesystem::rename(trustedRoot, originalRoot);
    std::error_code symlinkEc;
    std::filesystem::create_directory_symlink(relocatedRoot, trustedRoot, symlinkEc);
    if (symlinkNotPermitted(symlinkEc)) SKIP("creating symlinks is not permitted here");
    REQUIRE_FALSE(symlinkEc);

    const auto saved = backend.invokeBackend(
        "SavePlugin",
        nlohmann::json::array({"Patch.esp", (trustedRoot / "Injected.esp").string()}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "SavePathNotAllowed");
    CHECK_FALSE(std::filesystem::exists(relocatedRoot / "Injected.esp"));
}

TEST_CASE("Save As writes the checked canonical destination") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto modDir = root / "mods" / "MyMod";
    const auto actualOutputDir = modDir / "actual-output";
    const auto outputAlias = modDir / "output-alias";
    const auto patch = modDir / "Patch.esp";
    writeBytes(patch, makePlugin());
    std::filesystem::create_directories(actualOutputDir);
    std::error_code symlinkEc;
    std::filesystem::create_directory_symlink(actualOutputDir, outputAlias, symlinkEc);
    if (symlinkNotPermitted(symlinkEc)) SKIP("creating symlinks is not permitted here");
    REQUIRE_FALSE(symlinkEc);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", patch, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto requested = outputAlias / "Saved.esp";
    REQUIRE(backend.invokeBackend(
        "SavePlugin", nlohmann::json::array({"Patch.esp", requested.string()})).has_value());

    const auto plugins = parseStringResult(
        backend.invoke("appInterop.GetPlugins", nlohmann::json::array()));
    REQUIRE(plugins.size() == 1);
    std::error_code canonicalEc;
    const auto canonical = std::filesystem::weakly_canonical(requested, canonicalEc);
    REQUIRE_FALSE(canonicalEc);
    CHECK(plugins[0].at("FilePath") == canonical.string());
}

#if !defined(_WIN32)
TEST_CASE("Save As rejects a plugin-named symlink to a non-plugin target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto modDir = root / "mods" / "MyMod";
    const auto patch = modDir / "Patch.esp";
    const auto victim = modDir / "notes.txt";
    const auto alias = modDir / "LooksSafe.esp";
    writeBytes(patch, makePlugin());
    std::filesystem::create_directories(modDir);
    {
        std::ofstream out(victim, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out << "sentinel";
    }
    std::error_code symlinkEc;
    std::filesystem::create_symlink(victim, alias, symlinkEc);
    REQUIRE_FALSE(symlinkEc);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Patch.esp", patch, "MyMod")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto saved = backend.invokeBackend(
        "SavePlugin", nlohmann::json::array({"Patch.esp", alias.string()}));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().type == "SavePathNotAllowed");

    std::ifstream in(victim, std::ios::binary);
    std::string contents{std::istreambuf_iterator<char>(in), {}};
    CHECK(contents == "sentinel");
    CHECK(std::filesystem::is_symlink(alias));
}
#endif
