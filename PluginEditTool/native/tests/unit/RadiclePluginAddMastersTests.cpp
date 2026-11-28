#include <chrono>
#include "../helpers/TempRoot.hpp"
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
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_addmasters_" + id);
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

std::vector<std::byte> makeDisabledMaster() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterWeapon\0", 13});
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, weaponGroup);

    const auto refrGroup = fo4::test::begin_group(out, "REFR", 0);
    const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterRef\0", 10});
    fo4::test::end_record(out, refr);
    fo4::test::end_group(out, refrGroup);
    return out.bytes;
}

std::vector<std::byte> makeTargetDeclaring(std::string_view master) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    const auto name = std::string(master) + '\0';
    fo4::test::write_subrecord(out, "MAST", std::string_view{name});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

std::vector<std::byte> makeLightTarget() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::small_file, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x01000001u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"LightLocal\0", 11});
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

int writeLockCount = 0;

void countWriteLock() {
    ++writeLockCount;
}

struct WriteLockHookGuard {
    explicit WriteLockHookGuard(fo4ide::plugin::WriteOpLockedHook hook) {
        fo4ide::plugin::setWriteOpLockedHook(hook);
    }
    ~WriteLockHookGuard() { fo4ide::plugin::setWriteOpLockedHook(nullptr); }
    WriteLockHookGuard(const WriteLockHookGuard&) = delete;
    WriteLockHookGuard& operator=(const WriteLockHookGuard&) = delete;
};

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

    const auto listedRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 1);
    CHECK(listed.at("masters")[0].at("name") == "Master.esm");

    const auto beforeSave = fo4::read_plugin_file(targetPath);
    CHECK(beforeSave.header.masters.empty());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
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

TEST_CASE("Native AddMasters treats an ESL-named master without the light flag as light") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Extension.esl";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster(false));
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Extension.esl", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Extension.esl"})).has_value());

    const auto listedRaw = backend.invoke("masters.List",
                                          nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 1);
    CHECK(listed.at("masters")[0].at("name") == "Extension.esl");
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
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(targetPath));

    const auto message = textResult(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm, Master.esm"})));
    CHECK(message.find("No new masters") == 0);
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(targetPath)) == before);
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
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(targetPath));

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
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(targetPath)) == before);
}

TEST_CASE("Native AddMasters refuses a master that loads before an already-declared master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto firstPath = root / "First.esm";
    const auto secondPath = root / "Second.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(firstPath, makeMaster());
    writeBytes(secondPath, makeMaster());
    writeBytes(targetPath, makeTargetDeclaring("Second.esm"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("First.esm", firstPath),
            pluginEntry("Second.esm", secondPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "First.esm"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MasterOrder");

    const auto listedRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 1);
    CHECK(listed.at("masters")[0].at("name") == "Second.esm");
}

TEST_CASE("Native AddMasters accepts a master that loads after an already-declared master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto firstPath = root / "First.esm";
    const auto secondPath = root / "Second.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(firstPath, makeMaster());
    writeBytes(secondPath, makeMaster());
    writeBytes(targetPath, makeTargetDeclaring("First.esm"));

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("First.esm", firstPath),
            pluginEntry("Second.esm", secondPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    REQUIRE(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Second.esm"})).has_value());
    const auto listedRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 2);
    CHECK(listed.at("masters")[0].at("name") == "First.esm");
    CHECK(listed.at("masters")[1].at("name") == "Second.esm");
}

TEST_CASE("Native AddMasters appends a comma list of masters in load order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto firstPath = root / "First.esm";
    const auto secondPath = root / "Second.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(firstPath, makeMaster());
    writeBytes(secondPath, makeMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("First.esm", firstPath),
            pluginEntry("Second.esm", secondPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    REQUIRE(backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "First.esm,Second.esm"})).has_value());
    const auto listedRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    REQUIRE(listed.at("masters").size() == 2);
    CHECK(listed.at("masters")[0].at("name") == "First.esm");
    CHECK(listed.at("masters")[1].at("name") == "Second.esm");
}

TEST_CASE("Native AddMasters refuses a master that loads after the target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Target.esp", targetPath),
            pluginEntry("Master.esm", masterPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.invokeBackend(
        "AddMasters", nlohmann::json::array({"Target.esp", "Master.esm"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MasterOrder");

    const auto listedRaw = backend.invoke(
        "masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listedRaw.has_value());
    const auto listed = nlohmann::json::parse(listedRaw->get<std::string>());
    CHECK(listed.at("masters").empty());
}

TEST_CASE("Native CleanUnusedMasters removes an unreferenced master and keeps local FormKeys") {
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
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
    REQUIRE(fo4::read_plugin_file(targetPath).header.masters.size() == 1);

    const auto preview = textResult(
        backend.cleanUnusedMasters(nlohmann::json::array({"Target.esp", false})));
    CHECK(preview == "DRY RUN: would remove unused master(s) from Target.esp: Master.esm.");
    CHECK(backend.invoke("masters.List", nlohmann::json::array({"Target.esp"}))->get<std::string>()
              .find("Master.esm") != std::string::npos);

    const auto message = textResult(
        backend.cleanUnusedMasters(nlohmann::json::array({"Target.esp", true})));
    CHECK(message.find("Removed unused master(s) from Target.esp: Master.esm") == 0);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(targetPath);
    CHECK(reloaded.header.masters.empty());
    const auto* local = fo4::find_record(
        reloaded, fo4::FormKey::parse("000810:Target.esp"));
    REQUIRE(local != nullptr);
    CHECK(local->header.form_id == fo4::RawFormId{0x00000810});

    const auto again = textResult(
        backend.cleanUnusedMasters(nlohmann::json::array({"Target.esp"})));
    CHECK(again == "No unused masters in Target.esp.");
}

TEST_CASE("Native CleanUnusedMasters refuses to drop the last master of a light plugin with low own IDs") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeMaster());
    writeBytes(targetPath, makeLightTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto result = backend.cleanUnusedMasters(nlohmann::json::array({"Target.esp", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "CleanMastersFailed");

    const auto listed = backend.invoke("masters.List", nlohmann::json::array({"Target.esp"}));
    REQUIRE(listed.has_value());
    CHECK(listed->get<std::string>().find("Master.esm") != std::string::npos);
}

TEST_CASE("Native ComparePluginTo diffs a loaded plugin against another file without loading it") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto targetPath = root / "Target.esp";
    const auto samePath = root / "backup" / "Same.esp";
    const auto otherPath = root / "backup" / "Other.esp";
    writeBytes(targetPath, makeTarget());
    writeBytes(samePath, makeTarget());
    writeBytes(otherPath, makeMaster());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Target.esp", targetPath)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto same = nlohmann::json::parse(textResult(backend.comparePluginTo(
        nlohmann::json::array({"Target.esp", samePath.string()}))));
    CHECK(same.empty());

    const auto diff = nlohmann::json::parse(textResult(backend.comparePluginTo(
        nlohmann::json::array({"Target.esp", otherPath.string()}))));
    REQUIRE(diff.size() == 3);
    CHECK(diff[0].at("Signature") == "TES4");
    CHECK(diff[0].at("Change") == "changed");
    CHECK(diff[0].at("HeaderChanged") == true);
    CHECK(diff[0].at("Subrecords") == nlohmann::json::array({"HEDR"}));
    CHECK(diff[1].at("FormKey") == "000810:Target.esp");
    CHECK(diff[1].at("Change") == "added");
    CHECK(diff[2].at("FormKey") == "000800:Target.esp");
    CHECK(diff[2].at("Change") == "removed");

    const auto rejected = backend.comparePluginTo(
        nlohmann::json::array({"Target.esp", (root / "notes.txt").string()}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "InvalidPluginExtension");
}

TEST_CASE("Native CopyAsDisabledOverride copies and sets Initially Disabled on the override only") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeDisabledMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto message = textResult(backend.copyAsDisabledOverride(nlohmann::json::array(
        {"Master.esm", "000801:Master.esm", "Target.esp", false})));
    CHECK(message.find("Marked it Initially Disabled.") != std::string::npos);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(targetPath);
    const auto* copy = fo4::find_record(reloaded, fo4::FormKey::parse("000801:Master.esm"));
    REQUIRE(copy != nullptr);
    CHECK((copy->header.flags.value() & 0x00000800u) != 0);

    const auto master = fo4::read_plugin_file(masterPath);
    const auto* original = fo4::find_record(master, fo4::FormKey::parse("000801:Master.esm"));
    REQUIRE(original != nullptr);
    CHECK((original->header.flags.value() & 0x00000800u) == 0);
}

TEST_CASE("Native CopyAsDisabledOverride takes one write lock and stages the flagged copy") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeDisabledMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    writeLockCount = 0;
    const WriteLockHookGuard guard(&countWriteLock);
    const auto message = textResult(backend.copyAsDisabledOverride(nlohmann::json::array(
        {"Master.esm", "000801:Master.esm", "Target.esp", false})));
    CHECK(writeLockCount == 1);
    CHECK(message.find("Marked it Initially Disabled.") != std::string::npos);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(targetPath);
    const auto* copy = fo4::find_record(reloaded, fo4::FormKey::parse("000801:Master.esm"));
    REQUIRE(copy != nullptr);
    CHECK((copy->header.flags.value() & 0x00000800u) != 0);
}

TEST_CASE("Native CopyAsDisabledOverride leaves an existing override untouched") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeDisabledMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto args = nlohmann::json::array({"Master.esm", "000801:Master.esm", "Target.esp", false});
    REQUIRE(backend.invoke("backend.CopyAsOverride", args).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto message = textResult(backend.copyAsDisabledOverride(args));
    CHECK(message.starts_with("EXISTS:"));
    const auto reloaded = fo4::read_plugin_file(targetPath);
    const auto* copy = fo4::find_record(reloaded, fo4::FormKey::parse("000801:Master.esm"));
    REQUIRE(copy != nullptr);
    CHECK((copy->header.flags.value() & 0x00000800u) == 0);
}

TEST_CASE("Native CopyAsDisabledOverride refuses a record type outside placed refs and NAVM") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeDisabledMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto args = nlohmann::json::array({"Master.esm", "000800:Master.esm", "Target.esp", false});
    const auto rejected = backend.copyAsDisabledOverride(args);
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "DisabledOverrideUnsupported");
    const auto copied = textResult(backend.invoke("backend.CopyAsOverride", args));
    CHECK(copied.starts_with("Copied "));
}

TEST_CASE("Native CopyAsDisabledOverride marks a placed reference Initially Disabled") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto targetPath = root / "Target.esp";
    writeBytes(masterPath, makeDisabledMaster());
    writeBytes(targetPath, makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Target.esp", targetPath)
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto message = textResult(backend.copyAsDisabledOverride(nlohmann::json::array(
        {"Master.esm", "000801:Master.esm", "Target.esp", false})));
    CHECK(message.find("Marked it Initially Disabled.") != std::string::npos);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(targetPath);
    const auto* copy = fo4::find_record(reloaded, fo4::FormKey::parse("000801:Master.esm"));
    REQUIRE(copy != nullptr);
    CHECK((copy->header.flags.value() & 0x00000800u) != 0);
}

TEST_CASE("Native CheckForErrors reports an unresolved link in the selected plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto targetPath = root / "Broken.esp";
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    fo4::test::ByteWriter ksiz;
    ksiz.u32(1);
    fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
    fo4::test::ByteWriter kwda;
    kwda.u32(0x00000999);
    fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    writeBytes(targetPath, out.bytes);

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({pluginEntry("Broken.esp", targetPath)})}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto rows = nlohmann::json::parse(textResult(
        backend.checkForErrors(nlohmann::json::array({"Broken.esp"}))));
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].at("Code") == "unresolved_link");
    CHECK(rows[0].at("Detail").is_string());
    CHECK(rows[0].at("FormKey") == "000810:Broken.esp");
    CHECK(rows[0].at("Target") == "000999:Broken.esp");

    const auto missing = backend.checkForErrors(nlohmann::json::array({"Nope.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginNotLoaded");
}

TEST_CASE("Native LootCleaningReport counts an identical-to-master override") {
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
    REQUIRE(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Master.esm", "000800:Master.esm", "Target.esp", false})).has_value());

    const auto pending = backend.lootCleaningReport(nlohmann::json::array({"Target.esp"}));
    REQUIRE_FALSE(pending.has_value());
    CHECK(pending.error().type == "PluginHasPendingChanges");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
    const auto yaml = textResult(backend.lootCleaningReport(nlohmann::json::array({"Target.esp"})));
    CHECK(yaml.find("- name: 'Target.esp'\n  dirty:\n    - crc: 0x") == 0);
    CHECK(yaml.find("      itm: 1\n") != std::string::npos);
    CHECK(yaml.find("      udr: 0\n") != std::string::npos);
}

TEST_CASE("Native UpdateOnam stages a missing ONAM for a master-flagged plugin") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esm";
    const auto writeDial = [](fo4::test::ByteWriter& out, std::uint32_t id) {
        const auto group = fo4::test::begin_group(out, "DIAL", 0);
        const auto record = fo4::test::begin_record(out, "DIAL", 0, id);
        fo4::test::write_subrecord(out, "EDID", std::string_view{"Topic\0", 6});
        fo4::test::end_record(out, record);
        fo4::test::end_group(out, group);
    };
    const auto writeCellArrow = [](fo4::test::ByteWriter& out) {
        const auto cells = fo4::test::begin_group(out, "CELL", 0);
        const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000801);
        fo4::test::end_record(out, cell);
        const auto children = fo4::test::begin_group(out, "CELL", 6);
        out.patch_u32(children + 8, 0x00000801);
        const auto temporary = fo4::test::begin_group(out, "CELL", 9);
        out.patch_u32(temporary + 8, 0x00000801);
        const auto arrow = fo4::test::begin_record(out, "PARW", 0, 0x00000802);
        fo4::test::end_record(out, arrow);
        fo4::test::end_group(out, temporary);
        fo4::test::end_group(out, children);
        fo4::test::end_group(out, cells);
    };
    const auto header = [](fo4::test::ByteWriter& out, bool withMaster) {
        const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
        fo4::test::ByteWriter hedr;
        hedr.f32(1.0f);
        hedr.u32(1);
        hedr.u32(0x801);
        fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
        if (withMaster) {
            fo4::test::ByteWriter data;
            data.u32(0);
            data.u32(0);
            fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
            fo4::test::write_subrecord(out, "DATA", data.bytes);
        }
        fo4::test::end_record(out, tes4);
    };
    fo4::test::ByteWriter master;
    header(master, false);
    writeDial(master, 0x00000800);
    writeCellArrow(master);
    writeBytes(masterPath, master.bytes);
    fo4::test::ByteWriter patch;
    header(patch, true);
    writeDial(patch, 0x00000800);
    writeCellArrow(patch);
    writeBytes(patchPath, patch.bytes);
    writeBytes(root / "Target.esp", makeTarget());

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", masterPath),
            pluginEntry("Patch.esm", patchPath),
            pluginEntry("Target.esp", root / "Target.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    const auto notMaster = textResult(backend.updateOnam(nlohmann::json::array({"Target.esp"})));
    CHECK(notMaster == "Target.esp is not master-flagged; the game ignores ONAM there.");

    const auto staged = textResult(backend.updateOnam(nlohmann::json::array({"Patch.esm"})));
    CHECK(staged == "Rebuilt ONAM in Patch.esm (2 override(s)). SavePlugin to persist.");
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esm"})).has_value());
    const auto saved = fo4::read_plugin_file(patchPath);
    REQUIRE(saved.header.overrides.size() == 2);
    CHECK(saved.header.overrides[0].value() == 0x00000800u);
    CHECK(saved.header.overrides[1].value() == 0x00000802u);

    const auto again = textResult(backend.updateOnam(nlohmann::json::array({"Patch.esm"})));
    CHECK(again == "ONAM in Patch.esm is already up to date.");
}
