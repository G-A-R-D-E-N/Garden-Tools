#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

namespace {

std::filesystem::path createTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_create_plugin_" + std::to_string(id));
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
