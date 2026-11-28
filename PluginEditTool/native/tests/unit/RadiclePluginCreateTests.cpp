#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

namespace {

std::filesystem::path createTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_create_plugin_" + id);
}

struct CreateCleanup {
    std::filesystem::path root;
    ~CreateCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

bool hasEditorIdSubrecord(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == fo4::sig_edid) return true;
    }
    return false;
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path backupPathOf(const nlohmann::json& response) {
    return std::filesystem::path(
        nlohmann::json::parse(response.get<std::string>()).at("backup").get<std::string>());
}

void addEyes(fo4ide::plugin::RadiclePluginBackend& backend, const char* plugin, const char* editorId) {
    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            plugin,
            "EYES",
            nlohmann::json{
                {"editorId", editorId},
                {"fullText", editorId},
                {"icon", "Textures\\Interface\\Eyes.dds"},
                {"dataFlags", 0},
                {"recordFlags", 0}
            }
        }));
    REQUIRE(created.has_value());
}

}

TEST_CASE("Native CreatePlugin writes validates registers and immediately supports record creation") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto overwrite = root / "overwrite";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(overwrite).has_value());

    const auto created = backend.createPlugin(
        nlohmann::json::array({"FreshPatch"}));
    REQUIRE(created.has_value());
    REQUIRE(created->is_string());
    CHECK(created->get<std::string>().find("FreshPatch.esp") != std::string::npos);

    const auto path = overwrite / "FreshPatch.esp";
    REQUIRE(std::filesystem::exists(path));
    const auto disk = fo4::read_plugin_file(path);
    CHECK(disk.key.filename() == "FreshPatch.esp");
    CHECK(disk.header.version == 1.0f);
    CHECK(disk.header.next_form_id == 0x800u);
    CHECK_FALSE(disk.header.tes4_flags.is_master());
    CHECK_FALSE(disk.header.tes4_flags.is_small_file());
    CHECK(disk.header.masters.empty());
    CHECK(disk.groups.empty());

    const auto editable = backend.invokeBackend(
        "GetEditablePlugins", nlohmann::json::array());
    REQUIRE(editable.has_value());
    REQUIRE(editable->is_string());
    const auto editableJson = nlohmann::json::parse(editable->get<std::string>());
    CHECK(std::find(editableJson.begin(), editableJson.end(), "FreshPatch.esp") != editableJson.end());

    const auto record = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "FreshPatch.esp",
            "EYES",
            nlohmann::json{
                {"editorId", "FreshEyes"},
                {"fullText", "Fresh Eyes"},
                {"icon", "Textures\\Interface\\FreshEyes.dds"},
                {"dataFlags", 0},
                {"recordFlags", 0}
            }
        }));
    REQUIRE(record.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"FreshPatch.esp"})).has_value());
    const auto after = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(
        after, fo4::FormKey::parse("000800:FreshPatch.esp")) != nullptr);
    CHECK(after.header.next_form_id == 0x801u);

    const auto duplicate = backend.createPlugin(
        nlohmann::json::array({"FreshPatch.esp"}));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().type == "PluginAlreadyLoaded");
}

TEST_CASE("Native CreatePlugin sets ESL flags and rejects unsafe or protected names") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto overwrite = root / "overwrite";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(overwrite).has_value());

    const auto light = backend.createPlugin(
        nlohmann::json::array({"LightPatch.esl"}));
    REQUIRE(light.has_value());
    const auto lightDisk = fo4::read_plugin_file(overwrite / "LightPatch.esl");
    CHECK(lightDisk.header.tes4_flags.is_master());
    CHECK(lightDisk.header.tes4_flags.is_small_file());
    CHECK(lightDisk.header.next_form_id == 0x800u);

    const auto traversal = backend.createPlugin(
        nlohmann::json::array({"../escape.esp"}));
    REQUIRE_FALSE(traversal.has_value());
    CHECK(traversal.error().type == "InvalidPluginName");
    CHECK_FALSE(std::filesystem::exists(root / "escape.esp"));

    const auto device = backend.createPlugin(
        nlohmann::json::array({"NUL.esp"}));
    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().type == "InvalidPluginName");

    const auto protectedGame = backend.createPlugin(
        nlohmann::json::array({"Fallout4.esm"}));
    REQUIRE_FALSE(protectedGame.has_value());
    CHECK(protectedGame.error().type == "PluginProtected");
}

TEST_CASE("Native CreatePlugin requires an explicitly trusted output root") {
    fo4ide::plugin::RadiclePluginBackend backend;
    const auto created = backend.createPlugin(
        nlohmann::json::array({"NoRoot.esp"}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "NewPluginRootUnavailable");

    const auto relative = backend.setTrustedNewPluginRoot("relative-new-plugins");
    REQUIRE_FALSE(relative.has_value());
    CHECK(relative.error().type == "NewPluginRootInvalid");
}

TEST_CASE("Native created-plugin rollback deletes only unchanged empty plugins") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto overwrite = root / "overwrite";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(overwrite).has_value());

    REQUIRE(backend.createPlugin(nlohmann::json::array({"EmptyRollback.esp"})).has_value());
    const auto emptyPath = overwrite / "EmptyRollback.esp";
    REQUIRE(std::filesystem::exists(emptyPath));
    const auto discarded = backend.discardCreatedPlugin("EmptyRollback.esp");
    REQUIRE(discarded.has_value());
    CHECK_FALSE(std::filesystem::exists(emptyPath));

    REQUIRE(backend.createPlugin(nlohmann::json::array({"NonEmptyRollback.esp"})).has_value());
    const auto nonEmptyPath = overwrite / "NonEmptyRollback.esp";
    const auto record = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({
            "NonEmptyRollback.esp",
            "EYES",
            nlohmann::json{
                {"editorId", "KeepMe"},
                {"fullText", "Keep Me"},
                {"icon", "Textures\\Interface\\KeepMe.dds"},
                {"dataFlags", 0},
                {"recordFlags", 0}
            }
        }));
    REQUIRE(record.has_value());

    const auto refused = backend.discardCreatedPlugin("NonEmptyRollback.esp");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "CreatedPluginNotEmpty");
    CHECK(std::filesystem::exists(nonEmptyPath));
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"NonEmptyRollback.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(nonEmptyPath);
    CHECK(fo4::find_record(
        disk, fo4::FormKey::parse("000800:NonEmptyRollback.esp")) != nullptr);
}

TEST_CASE("Native CreateRecord with an empty editorId writes no EDID subrecord") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto overwrite = root / "overwrite";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(overwrite).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"EmptyEdid.esp"})).has_value());

    const auto formKeyOf = [&](const char* signature, const nlohmann::json& payload) {
        const auto created = backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"EmptyEdid.esp", signature, payload}));
        REQUIRE(created.has_value());
        REQUIRE(created->is_string());
        return nlohmann::json::parse(created->get<std::string>()).at("formKey").get<std::string>();
    };
    const auto emptyFaction = formKeyOf("FACT", nlohmann::json{{"editorId", ""}, {"flags", 0}});
    const auto emptyEyes = formKeyOf("EYES", nlohmann::json{
        {"editorId", ""},
        {"fullText", "Empty Eyes"},
        {"icon", "Textures\\Interface\\EmptyEyes.dds"},
        {"dataFlags", 0},
        {"recordFlags", 0}
    });
    const auto keptFaction = formKeyOf("FACT", nlohmann::json{{"editorId", "KeptFaction"}, {"flags", 0}});

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"EmptyEdid.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(overwrite / "EmptyEdid.esp");
    for (const auto& key : {emptyFaction, emptyEyes}) {
        const auto* record = fo4::find_record(disk, fo4::FormKey::parse(key));
        REQUIRE(record != nullptr);
        CHECK_FALSE(hasEditorIdSubrecord(*record));
    }
    const auto* kept = fo4::find_record(disk, fo4::FormKey::parse(keptFaction));
    REQUIRE(kept != nullptr);
    CHECK(hasEditorIdSubrecord(*kept));
}

TEST_CASE("Native CreatePlugin in a missing mods/FO4IDE New Plugins folder writes there and names the path and the MO2 F5 step (regression: plugin landed in overwrite and the reply gave no way to enable it in MO2)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto folder = root / "mods" / "FO4IDE New Plugins";
    REQUIRE_FALSE(std::filesystem::exists(folder));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(folder).has_value());

    const auto created = backend.createPlugin(nlohmann::json::array({"NewFolderPatch"}));
    REQUIRE(created.has_value());
    REQUIRE(created->is_string());
    const auto message = created->get<std::string>();
    const auto path = std::filesystem::weakly_canonical(folder / "NewFolderPatch.esp");
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(root / "overwrite" / "NewFolderPatch.esp"));
    CHECK(message.find(path.string()) != std::string::npos);
    CHECK(message.find("F5") != std::string::npos);
}

TEST_CASE("Native CreatePlugin never replaces an unloaded file already in the new-plugin folder (regression: create_plugin overwrote a loose file the user had placed there)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto folder = root / "mods" / "FO4IDE New Plugins";
    std::filesystem::create_directories(folder);
    const auto taken = folder / "Taken.esp";
    {
        std::ofstream out(taken, std::ios::binary);
        out << "not a plugin, keep me";
    }
    const auto before = readBytes(taken);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(folder).has_value());
    const auto created = backend.createPlugin(nlohmann::json::array({"Taken.esp"}));
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().type == "PluginAlreadyExists");
    CHECK(readBytes(taken) == before);
}

TEST_CASE("Native BackupPlugin copies the on-disk bytes to a timestamped .bak and never overwrites an earlier backup (regression: a second backup in the same second replaced the first, or the backup was not byte-identical to the saved plugin)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto folder = root / "mods" / "FO4IDE New Plugins";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(folder).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"Backed.esp"})).has_value());
    const auto path = std::filesystem::weakly_canonical(folder / "Backed.esp");
    addEyes(backend, "Backed.esp", "FirstEyes");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Backed.esp"})).has_value());
    const auto original = readBytes(path);

    const auto first = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Backed.esp"}));
    REQUIRE(first.has_value());
    const auto firstPath = backupPathOf(*first);
    CHECK(firstPath.string().starts_with(path.string() + "."));
    CHECK(firstPath.extension() == ".bak");
    CHECK(readBytes(firstPath) == original);
    CHECK(readBytes(path) == original);

    addEyes(backend, "Backed.esp", "SecondEyes");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Backed.esp"})).has_value());
    const auto saved = readBytes(path);
    REQUIRE(saved != original);

    const auto second = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Backed.esp"}));
    REQUIRE(second.has_value());
    const auto secondPath = backupPathOf(*second);
    CHECK(secondPath != firstPath);
    CHECK(readBytes(secondPath) == saved);
    CHECK(readBytes(firstPath) == original);
}

TEST_CASE("Native BackupPlugin copies the saved disk bytes while an unsaved edit is pending (regression: the backup captured the in-memory edit, so restoring it could not recover the last saved state)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto folder = root / "mods" / "FO4IDE New Plugins";

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.setTrustedNewPluginRoot(folder).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"Pending.esp"})).has_value());
    const auto path = std::filesystem::weakly_canonical(folder / "Pending.esp");
    addEyes(backend, "Pending.esp", "SavedEyes");
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Pending.esp"})).has_value());
    const auto original = readBytes(path);

    addEyes(backend, "Pending.esp", "UnsavedEyes");
    const auto backup = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Pending.esp"}));
    REQUIRE(backup.has_value());
    CHECK(readBytes(backupPathOf(*backup)) == original);
    CHECK(readBytes(path) == original);

    // A plugin whose file was deleted behind the editor has nothing on disk to copy.
    REQUIRE(std::filesystem::remove(path));
    const auto missing = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Pending.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginFileMissing");
}

TEST_CASE("Native BackupPlugin refuses a protected name and a game-Data plugin without writing a .bak (regression: a backup of a CC or Data plugin wrote a .bak into the game Data folder)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto folder = root / "mods" / "FO4IDE New Plugins";
    const auto data = root / "Game" / "Data";
    std::filesystem::create_directories(data);

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    REQUIRE(backend.setTrustedNewPluginRoot(folder).has_value());
    REQUIRE(backend.createPlugin(nlohmann::json::array({"Seed.esp"})).has_value());
    const auto seed = folder / "Seed.esp";

    const auto cc = root / "Loose" / "ccSeed.esp";
    std::filesystem::create_directories(cc.parent_path());
    std::filesystem::copy_file(seed, cc);
    REQUIRE(backend.invoke("backend.OpenPlugin", nlohmann::json::array({cc.string()})).has_value());
    const auto ccBackup = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"ccSeed.esp"}));
    REQUIRE_FALSE(ccBackup.has_value());
    CHECK(ccBackup.error().type == "PluginProtected");

    std::filesystem::copy_file(seed, data / "Loose.esp");
    REQUIRE(backend.invoke("backend.OpenPlugin", nlohmann::json::array({(data / "Loose.esp").string()})).has_value());
    const auto dataBackup = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Loose.esp"}));
    REQUIRE_FALSE(dataBackup.has_value());
    CHECK(dataBackup.error().type == "PluginReadOnly");
    CHECK(std::distance(std::filesystem::directory_iterator(data), std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("Native BackupPlugin refuses a plugin that is not loaded (regression: backing up an arbitrary name copied a file the editor does not own)") {
    fo4ide::plugin::RadiclePluginBackend backend;
    const auto backup = backend.invoke("backend.BackupPlugin", nlohmann::json::array({"Missing.esp"}));
    REQUIRE_FALSE(backup.has_value());
    CHECK(backup.error().type == "PluginNotLoaded");
}

TEST_CASE("Native setTrustedNewPluginRoot refuses a new-plugin folder inside the game Data directory (regression: created plugins could be written into the game Data folder)") {
    const auto root = createTempRoot();
    CreateCleanup cleanup{root};
    const auto data = root / "Game" / "Data";

    fo4ide::plugin::RadiclePluginBackend backend;
    backend.setDataDirectory(data);
    const auto refused = backend.setTrustedNewPluginRoot(data / "mods" / "FO4IDE New Plugins");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "NewPluginRootProtected");
}
