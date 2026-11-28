#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/NifGltfService.hpp>
#include <fo4/graph/graph_document.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4ide/logging/Logger.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/NifFixture.hpp"
#include "../helpers/ServiceTargetFakes.hpp"
#include "McpTools.hpp"

using namespace fo4ide::app::test;

namespace {

std::string fileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}

TEST_CASE("SettingsTarget returns the existing JSON string contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "test-model";
    settings.dataFolder = root / "Data";
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("GetSettings", nlohmann::json::array());

    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    CHECK(json.at("Model") == "test-model");
    CHECK(json.at("DataFolder") == (root / "Data").string());
}

TEST_CASE("SettingsTarget applies partial updates and persists them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "before";
    settings.archive2Path = root / "Archive2.exe";
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    const nlohmann::json payload{
        {"Model", "after"},
        {"DataFolder", (root / "Data").string()},
        {"NewPluginFolder", (root / "New Plugins").string()}
    };
    auto result = target.invoke("SaveSettings", nlohmann::json::array({payload.dump()}));

    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().model == "after");
    CHECK(shared.snapshot().dataFolder == root / "Data");
    CHECK(shared.snapshot().archive2Path == root / "Archive2.exe");

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->model == "after");
    CHECK(loaded->newPluginFolder == root / "New Plugins");
}

TEST_CASE("SettingsTarget refuses a relative NewPluginFolder and keeps the stored one (regression: a relative folder was saved and every later MO2 profile load failed with NewPluginRootInvalid)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.model = "before";
    settings.newPluginFolder = root / "Kept";
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    const nlohmann::json payload{{"Model", "after"}, {"NewPluginFolder", "relative/New Plugins"}};
    auto result = target.invoke("SaveSettings", nlohmann::json::array({payload.dump()}));

    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Invalid NewPluginFolder") != std::string::npos);
    CHECK(shared.snapshot().newPluginFolder == root / "Kept");
    CHECK(shared.snapshot().model == "before");
    CHECK_FALSE(std::filesystem::exists(root / "settings.json"));

    const nlohmann::json cleared{{"NewPluginFolder", "  "}};
    result = target.invoke("SaveSettings", nlohmann::json::array({cleared.dump()}));
    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().newPluginFolder.empty());
}

TEST_CASE("SettingsTarget saves PrevisOutputFolder and refuses a relative one") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    settings.previsOutputFolder = root / "Kept";
    FakeRunner runner;
    FakePlatform platform;

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    const nlohmann::json relative{{"PrevisOutputFolder", "relative/Previs"}};
    auto result = target.invoke("SaveSettings", nlohmann::json::array({relative.dump()}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find("Invalid PrevisOutputFolder") != std::string::npos);
    CHECK(shared.snapshot().previsOutputFolder == root / "Kept");

    const nlohmann::json full{{"PrevisOutputFolder", (root / "Previs Out").string()}};
    result = target.invoke("SaveSettings", nlohmann::json::array({full.dump()}));
    REQUIRE(result.has_value());
    CHECK(*result == "Settings saved.");
    CHECK(shared.snapshot().previsOutputFolder == root / "Previs Out");
    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->previsOutputFolder == root / "Previs Out");
}

TEST_CASE("SettingsTarget delegates folder selection to native platform services") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakeRunner runner;
    FakePlatform platform;
    platform.folderSelection = root / "MO2";

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("BrowseFolder", nlohmann::json::array({"Choose MO2", ""}));

    REQUIRE(result.has_value());
    CHECK(*result == platform.folderSelection.string());
}

TEST_CASE("SettingsTarget routes Claude version checks through ProcessRunner") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Store store(root / "settings.json");
    fo4ide::settings::Settings settings;
    FakeRunner runner;
    runner.output.stdOut = "claude-native 1.0\n";
    FakePlatform platform;

    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::app::SettingsTarget target(store, shared, platform, runner);
    auto result = target.invoke("TestClaude", nlohmann::json::array({"claude"}));

    REQUIRE(result.has_value());
    CHECK(*result == "Found: claude-native 1.0");
    CHECK(runner.last.timeout == std::chrono::seconds(30));
}


TEST_CASE("NifTarget Tree returns the error JSON for an unreadable NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "truncated.nif";
    auto bytes = fo4ide::test::triangleNif();
    bytes.resize(bytes.size() - 8);
    fo4ide::test::writeBytes(nif, bytes);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto result = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    REQUIRE(parsed.contains("error"));
    CHECK_FALSE(parsed.at("error").get<std::string>().empty());
    CHECK_FALSE(parsed.contains("blocks"));
}

// Detects: malformed selected-check arguments must not trigger whole-file repair.
TEST_CASE("NifTarget refuses malformed selected repair without writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "input.nif";
    const auto output = root / "output.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto result = target.invoke("Fix", nlohmann::json::array({nif.string(), output.string(), 42}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidArgument");
    CHECK(result.error().message == "Invalid NIF repair check");
    CHECK_FALSE(std::filesystem::exists(output));
}

// Detects: the bridge must forward a selected repair instead of running all fixes.
TEST_CASE("NifTarget forwards the selected repair") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "input.nif";
    const auto output = root / "output.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM ",
                                                       {" Textures\\Thing_d.dds "}));
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto result = target.invoke("Fix", nlohmann::json::array({nif.string(), output.string(), "no editor markers"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("RESULT: fix OK"));
    const auto inspected = service.inspect(output);
    REQUIRE(inspected.has_value());
    const auto report = nlohmann::json::parse(*inspected);
    CHECK(report.at("shapes").at(0).at("shader") == "Materials\\Clutter\\Thing.BGSM ");
    CHECK(report.at("shapes").at(0).at("textures").at(0).at("path") == " Textures\\Thing_d.dds ");
}

// Constraint: detects the Import RPC dropping the collision argument, which would save a NIF without the box the user asked for.
TEST_CASE("NifTarget Import passes the collision argument to the service") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto boxed = root / "boxed.nif";
    auto result = target.invoke("Import", nlohmann::json::array({obj.string(), boxed.string(), "", "", "", true, false}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("RESULT: import OK"));
    std::ifstream written(boxed, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("bhkPhysicsSystem") != std::string::npos);

    const auto plain = root / "plain.nif";
    auto plainResult = target.invoke("Import", nlohmann::json::array({obj.string(), plain.string(), "", "", "", false, false}));
    REQUIRE(plainResult.has_value());
    std::ifstream plainFile(plain, std::ios::binary);
    const std::string plainBytes((std::istreambuf_iterator<char>(plainFile)), std::istreambuf_iterator<char>());
    CHECK(plainBytes.find("bhkPhysicsSystem") == std::string::npos);
}

// Regression: the collision shape name and the mass were dropped by the Import RPC, so a hull came out as a static box.
TEST_CASE("NifTarget Import reads a collision shape name and a mass") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto hull = root / "hull.nif";
    auto result = target.invoke("Import", nlohmann::json::array(
        {obj.string(), hull.string(), "", "", "", "hull", false, 12.0}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("RESULT: import OK"));

    auto info = target.invoke("CollisionInfo", nlohmann::json::array({hull.string()}));
    REQUIRE(info.has_value());
    const auto body = nlohmann::json::parse(info->get<std::string>()).at("systems")[0].at("bodies")[0];
    CHECK(body.at("motion") == "dynamic");
    CHECK(body.at("shape").at("vertices") == 4);
}

// Detects: a mass_kg sent as the string "12" was read as 0, so the Import RPC wrote a static body with no error.
TEST_CASE("NifTarget Import refuses a mass_kg that is not a JSON number") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto hull = root / "hull.nif";
    auto refused = target.invoke("Import", nlohmann::json::array(
        {obj.string(), hull.string(), "", "", "", "hull", false, "12"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidMass");
    CHECK(refused.error().message == "mass_kg must be a number of kilograms");
    CHECK_FALSE(std::filesystem::exists(hull));

    auto omitted = target.invoke("Import", nlohmann::json::array(
        {obj.string(), hull.string(), "", "", "", "hull", false, nlohmann::json(nullptr)}));
    REQUIRE(omitted.has_value());
    CHECK(omitted->get<std::string>().starts_with("RESULT: import OK"));
}

// Detects: a mass_kg sent as the string "3" was read as 0 by CollisionSet, so the generated body was written static.
TEST_CASE("NifTarget CollisionSet refuses a mass_kg that is not a JSON number") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tetra.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto plain = root / "plain.nif";
    REQUIRE(target.invoke("Import", nlohmann::json::array(
        {obj.string(), plain.string(), "", "", "", false, false})).has_value());

    const auto out = root / "out.nif";
    auto result = target.invoke("CollisionSet", nlohmann::json::array(
        {plain.string(), "Scene Root", out.string(), "kind", "", "hull", "3"}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidMass");
    CHECK_FALSE(std::filesystem::exists(out));
}

// Detects: a donor_body sent as the string "1" or as -1 was ignored, so CollisionSet copied the default donor body instead of refusing.
TEST_CASE("NifTarget CollisionSet refuses a donor_body that is not a whole number") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto out = root / "out.nif";
    for (const auto& bad : {nlohmann::json("1"), nlohmann::json(-1)}) {
        auto result = target.invoke("CollisionSet", nlohmann::json::array(
            {(root / "plain.nif").string(), "Scene Root", out.string(), "donor_nif",
             (root / "donor.nif").string(), "", 0, bad}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "InvalidDonorBody");
    }
    CHECK_FALSE(std::filesystem::exists(out));
}

#ifndef _WIN32
// Constraint: POSIX only, because Windows refuses this byte in the path conversion before any reply is built.
TEST_CASE("NifTarget Tree replaces a stray UTF-8 byte in its error reply") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto missing = root / std::string("missing\xFF.nif");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto result = target.invoke("Tree", nlohmann::json::array({missing.string()}));
    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("error").get<std::string>().find("missing") != std::string::npos);
}
#endif

// Detects: the panel and MCP ImportGltf must not replace an existing NIF without overwrite, and the refusal must reach the panel as the EXISTS: reply.
TEST_CASE("NifTarget ImportGltf replies EXISTS for an existing output until overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto glb = root / "tri.glb";
    REQUIRE(fo4ide::assets::exportNifGltf(nif, glb).has_value());
    const auto out = root / "back.nif";
    touch(out, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("ImportGltf", nlohmann::json::array({glb.string(), out.string(), "", "", "", false, "", false}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: Refusing to replace an existing file"));
    CHECK(std::filesystem::file_size(out) == 3U);

    auto replaced = target.invoke("ImportGltf", nlohmann::json::array({glb.string(), out.string(), "", "", "", false, "", true}));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().starts_with("RESULT: import glTF OK"));
}

TEST_CASE("NifTarget Fix replies EXISTS for an existing output until overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "input.nif";
    const auto output = root / "output.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM ",
                                                       {" Textures\\Thing_d.dds "}));
    touch(output, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("Fix", nlohmann::json::array({nif.string(), output.string(), "no editor markers", false}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: Refusing to replace an existing file"));
    CHECK(std::filesystem::file_size(output) == 3U);

    auto replaced = target.invoke("Fix", nlohmann::json::array({nif.string(), output.string(), nlohmann::json(nullptr), true}));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().starts_with("RESULT: fix OK"));
}

TEST_CASE("NifTarget Import replies EXISTS for an existing output until overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "quad.obj";
    touch(obj, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const auto out = root / "quad.nif";
    touch(out, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("Import", nlohmann::json::array({obj.string(), out.string(), "", "", "", "none", false, 0.0, false}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: Refusing to replace an existing file"));
    CHECK(std::filesystem::file_size(out) == 3U);

    auto replaced = target.invoke("Import", nlohmann::json::array({obj.string(), out.string(), "", "", "", "none", false, 0.0, true}));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().starts_with("RESULT: import OK"));
}

// Detects: glTF import must not accept hull or mesh collision options that the native path cannot honour.
TEST_CASE("NifTarget ImportGltf refuses unsupported collision kinds") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto glb = root / "tri.glb";
    REQUIRE(fo4ide::assets::exportNifGltf(nif, glb).has_value());
    const auto out = root / "back.nif";

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("ImportGltf", nlohmann::json::array({glb.string(), out.string(), "", "", "", "hull", "", false}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidCollisionKind");
}

TEST_CASE("NifTarget BlockOp accepts a named argument object") {
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto result = target.invoke("BlockOp", nlohmann::json::array({nlohmann::json::object({
        {"path", ""}, {"op", "append_child"}, {"block", 3}})}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InvalidArgument");
    CHECK(result.error().message == "append_child needs child");
}

// Detects: the panel and MCP ExportGltf replacing a .glb without overwrite, or the refusal not reaching the panel as the EXISTS: reply it checks for.
TEST_CASE("NifTarget ExportGltf replies EXISTS for an existing output until overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto out = root / "tri.glb";
    touch(out, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto refused = target.invoke("ExportGltf", nlohmann::json::array({nif.string(), out.string(), false}));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: Refusing to replace existing files"));
    CHECK(std::filesystem::file_size(out) == 3U);

    auto replaced = target.invoke("ExportGltf", nlohmann::json::array({nif.string(), out.string(), true}));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().starts_with("RESULT: gltf export OK"));
}

TEST_CASE("NifTarget ApplyEdits takes the editor's JSON string and saves in-process") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tree.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Thing.BGSM",
                                                       {"Textures\\Thing_d.dds", "Textures\\Thing_n.dds"}));
    const auto edits = nlohmann::json::array({{{"id", 1}, {"cat", "shape"}, {"key", "name"}, {"value", "Renamed"}}});

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto saved = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), edits.dump(), ""}));
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("set OK") != std::string::npos);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    CHECK(tree->get<std::string>().find("\"Renamed\"") != std::string::npos);
}

// Detects: a valid signed block index must allow creating a texture set for an untextured shape.
TEST_CASE("NifTarget CreateTextureSet gives an untextured shape a texture set and refuses a node") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "untextured.nif";
    const std::vector<fo4ide::test::FixtureBlock> blocks{
        {"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U})},
        {"BSTriShape", fo4ide::test::triangleShapeBlock(2U, 0.25f)},
        {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 0xFFFFFFFFU)},
    };
    fo4ide::test::writeBytes(nif, fo4ide::test::nifFile(blocks, {"Tri", "Material", "Root"}));
    const auto paths = nlohmann::json::array({"Textures\\Thing_d.dds"});

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto created = target.invoke("CreateTextureSet", nlohmann::json::array({nif.string(), 1, paths, ""}));
    REQUIRE(created.has_value());
    CHECK(created->get<std::string>().find("texture set OK") != std::string::npos);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    CHECK(tree->get<std::string>().find("Thing_d.dds") != std::string::npos);

    auto refused = target.invoke("CreateTextureSet", nlohmann::json::array({nif.string(), 0, paths, ""}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "NifEditFailed");
    CHECK(refused.error().message.find("has no shader slot") != std::string::npos);
    CHECK(refused.error().message.find("whole block index") == std::string::npos);
}

// Detects: the RPC must reject invalid block indices and accept valid signed and unsigned indices.
TEST_CASE("NifTarget CreateTextureSet validates block indices") {
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);
    const auto paths = nlohmann::json::array({"Textures\\Thing_d.dds"});
    const auto invoke = [&](const nlohmann::json& block) {
        return target.invoke("CreateTextureSet", nlohmann::json::array({"", block, paths, ""}));
    };
    for (const auto& block : {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json(std::uint64_t{4294967296ULL})}) {
        const auto result = invoke(block);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "InvalidBlock");
        CHECK(result.error().message == "CreateTextureSet needs a whole block index");
    }
    for (const auto& block : {nlohmann::json(0), nlohmann::json(std::uint64_t{4294967295ULL})}) {
        const auto result = invoke(block);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("whole block index") == std::string::npos);
    }
}

TEST_CASE("NifTarget MaterialTextureEdits reports the same source digest as Tree") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "untextured.nif";
    const std::vector<fo4ide::test::FixtureBlock> blocks{
        {"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U})},
        {"BSTriShape", fo4ide::test::triangleShapeBlock(2U, 0.25f)},
        {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 0xFFFFFFFFU)},
    };
    fo4ide::test::writeBytes(nif, fo4ide::test::nifFile(blocks, {"Tri", "Material", "Root"}));

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    auto plan = target.invoke("MaterialTextureEdits", nlohmann::json::array({nif.string(), 1, ""}));
    REQUIRE(plan.has_value());
    CHECK(nlohmann::json::parse(plan->get<std::string>()).at("source") ==
          nlohmann::json::parse(tree->get<std::string>()).at("source"));
}

TEST_CASE("NifTarget CreateTextureSet takes the source digest as argument four and refuses a replaced NIF") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "untextured.nif";
    const std::vector<fo4ide::test::FixtureBlock> blocks{
        {"NiNode", fo4ide::test::nodeBlock(2U, 0.5f, {1U})},
        {"BSTriShape", fo4ide::test::triangleShapeBlock(2U, 0.25f)},
        {"BSLightingShaderProperty", fo4ide::test::shaderBlock(1U, 0xFFFFFFFFU)},
    };
    fo4ide::test::writeBytes(nif, fo4ide::test::nifFile(blocks, {"Tri", "Material", "Root"}));
    const auto paths = nlohmann::json::array({"Textures\\Thing_d.dds"});

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    const auto staleSource = nlohmann::json::parse(tree->get<std::string>()).at("source");

    auto matching = target.invoke("CreateTextureSet",
                                  nlohmann::json::array({nif.string(), 1, paths, (root / "textured.nif").string(), staleSource}));
    REQUIRE(matching.has_value());
    CHECK(matching->get<std::string>().find("texture set OK") != std::string::npos);

    fo4ide::test::writeBytes(nif, fo4ide::test::treeNif("Materials\\Clutter\\Other.BGSM", {"Textures\\Other_d.dds"}));
    auto replacedTree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(replacedTree.has_value());
    const auto replacementSource = nlohmann::json::parse(replacedTree->get<std::string>()).at("source");

    auto refused = target.invoke("CreateTextureSet", nlohmann::json::array({nif.string(), 1, paths, "", staleSource}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("changed on disk after it was loaded") != std::string::npos);

    auto afterTree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(afterTree.has_value());
    CHECK(nlohmann::json::parse(afterTree->get<std::string>()).at("source") == replacementSource);
}

TEST_CASE("NifTarget stages browser dropped bytes without the CSharp host") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto result = target.invoke("StageDroppedFile", nlohmann::json::array({"mesh.nif", "aGVsbG8="}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto path = std::filesystem::path(result->get<std::string>());
    REQUIRE(std::filesystem::is_regular_file(path));
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(contents == "hello");
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
}


TEST_CASE("NifTarget Geo reports a parse failure as JSON the panel can show") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    touch(nif);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto result = target.invoke("Geo", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("error") == "NIF header line is missing or longer than 1024 bytes");
}

// Detects: a block index above 32 bits narrowing to block 0 and reading that segment table; the request must be refused.
TEST_CASE("NifTarget Segments refuses a block index above UINT32_MAX instead of reading block 0") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "seg.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::subIndexNif());

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    auto result = target.invoke("Segments", nlohmann::json::array({nif.string(), std::uint64_t{4294967296ULL}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "BlockIndexMissing");
}

TEST_CASE("NifTarget CollisionSet and CollisionRemove replace an existing output only when overwrite is passed") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto obj = root / "tri.obj";
    touch(obj, "v 0 0 0\nv 70 0 0\nv 0 70 0\nf 1 2 3\n");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto plain = root / "plain.nif";
    REQUIRE(target.invoke("Import", nlohmann::json::array(
        {obj.string(), plain.string(), "", "", "", false, false})).has_value());

    const auto contents = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    };

    const auto boxed = root / "boxed.nif";
    const auto setArgs = [&](const std::filesystem::path& out, bool overwrite) {
        return nlohmann::json::array({plain.string(), "Scene Root", out.string(), "kind", "", "box", 0, nullptr, overwrite});
    };
    REQUIRE(target.invoke("CollisionSet", setArgs(boxed, false)).has_value());

    const auto set = root / "set.nif";
    touch(set, "old bytes");
    auto refused = target.invoke("CollisionSet", setArgs(set, false));
    REQUIRE(refused.has_value());
    CHECK(refused->get<std::string>().starts_with("EXISTS: "));
    CHECK(contents(set) == "old bytes");
    auto replaced = target.invoke("CollisionSet", setArgs(set, true));
    REQUIRE(replaced.has_value());
    CHECK(replaced->get<std::string>().starts_with("RESULT: collision set OK (in-process)"));
    CHECK(contents(set) != "old bytes");
    CHECK(contents(root / "set.nif.bak") == "old bytes");

    const auto removed = root / "removed.nif";
    touch(removed, "old bytes");
    auto removeRefused = target.invoke("CollisionRemove", nlohmann::json::array({boxed.string(), "Scene Root", removed.string(), false}));
    REQUIRE(removeRefused.has_value());
    CHECK(removeRefused->get<std::string>().starts_with("EXISTS: "));
    CHECK(contents(removed) == "old bytes");
    auto removeReplaced = target.invoke("CollisionRemove", nlohmann::json::array({boxed.string(), "Scene Root", removed.string(), true}));
    REQUIRE(removeReplaced.has_value());
    CHECK(removeReplaced->get<std::string>().starts_with("RESULT: collision removed"));
    CHECK(contents(removed) != "old bytes");
}

TEST_CASE("NifTarget UvTransform refuses a channel that is not 0 or 2 and leaves the NIF unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "uv.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::dualUvTriangleNif());
    const auto before = fileBytes(nif);
    const auto block = fo4::nif::read_shapes(fo4::nif::read_nif(fo4ide::test::dualUvTriangleNif())).at(0).block;

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    for (const std::int64_t channel : {std::int64_t{2147483648}, std::int64_t{1}}) {
        auto refused = target.invoke("UvTransform", nlohmann::json::array(
            {nif.string(), block, channel, 0.25, 0.0, 1.0, 1.0, 0.0, nif.string()}));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().type == "InvalidChannel");
        CHECK(fileBytes(nif) == before);
    }
}

TEST_CASE("NifTarget UvOp clamps selected UVs through Radicle and refuses bad arguments without calling it") {
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto call = [&target](const char* op, const char* body) {
        auto answered = target.invoke("UvOp", nlohmann::json::array({op, body}));
        REQUIRE(answered.has_value());
        return nlohmann::json::parse(answered->get<std::string>());
    };
    const auto clamped = call("clamp_tile", R"({"uvs":[-0.5,0.25,1.5,0.75],"selection":[0,1]})");
    CHECK(clamped.at("uvs") == nlohmann::json::array({0.0, 0.25, 1.0, 0.75}));
    CHECK(call("smart_project", R"({"uvs":[0,0,1,0,0,1],"positions":[0,0,0],"tris":[0,1,2],"selection":[0],"margin":0.01})").contains("error"));
    CHECK(call("clamp_tile", R"({"uvs":[0.5],"selection":[0]})").contains("error"));
    CHECK(call("clamp_tile", R"({"uvs":[0.5,0.5],"selection":[-1]})").contains("error"));
    CHECK(call("nope", R"({"uvs":[]})").contains("error"));
    CHECK(call("clamp_tile", "not json").contains("error"));
}

TEST_CASE("NifTarget UvOp answers islands, pack and split_plan from Radicle") {
    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::app::NifTarget target(service, textures, platform, plugins);

    const auto call = [&target](const char* op, const char* body) {
        auto answered = target.invoke("UvOp", nlohmann::json::array({op, body}));
        REQUIRE(answered.has_value());
        return nlohmann::json::parse(answered->get<std::string>());
    };
    CHECK(call("islands", R"({"vertex_count":6,"tris":[0,1,2,3,4,5]})").at("ids") == nlohmann::json::array({0, 0, 0, 1, 1, 1}));

    const auto packed = call("pack", R"({"uvs":[0,0,0.5,0,0.5,0.5,0,0.5,0.6,0.6,0.9,0.6,0.9,0.9,0.6,0.9],"tris":[0,1,2,0,2,3,4,5,6,4,6,7],"selection":[0,4],"margin":0.1})");
    CHECK(packed.at("fits").get<bool>());
    REQUIRE(packed.at("uvs").size() == 16);
    CHECK(packed.at("uvs")[0].get<double>() == Catch::Approx(0.1).margin(1e-5));
    CHECK(packed.at("uvs")[1].get<double>() == Catch::Approx(0.1).margin(1e-5));
    CHECK(packed.at("uvs")[2].get<double>() == Catch::Approx(0.5375).margin(1e-5));

    const auto plan = call("split_plan", R"({"vertex_count":9,"tris":[0,1,2,0,3,4,5,6,7,6,5,8],"seams":[5,6]})");
    CHECK(plan.at("sources") == nlohmann::json::array({5, 6}));
    CHECK(plan.at("tris") == nlohmann::json::array({0, 1, 2, 0, 3, 4, 5, 6, 7, 10, 9, 8}));
    const auto none = call("split_plan", R"({"vertex_count":5,"tris":[0,1,2,0,3,4],"seams":[]})");
    CHECK(none.at("sources").empty());
    CHECK(none.at("tris") == nlohmann::json::array({0, 1, 2, 0, 3, 4}));
}

TEST_CASE("MCP nif_uv_transform refuses an omitted out_nif and keeps the NIF, and writes an explicit out_nif") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "uv.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::dualUvTriangleNif());
    const auto before = fileBytes(nif);
    const auto block = fo4::nif::read_shapes(fo4::nif::read_nif(fo4ide::test::dualUvTriangleNif())).at(0).block;

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));

    const nlohmann::json edit{{"path", nif.string()}, {"block", block}, {"du", 0.25}};
    const auto omitted = registry.call("nif_uv_transform", edit);
    CHECK(omitted.isError);
    CHECK(omitted.text == "Missing argument: out_nif");
    CHECK(fileBytes(nif) == before);

    const auto out = root / "uv-out.nif";
    auto explicitOut = edit;
    explicitOut["out_nif"] = out.string();
    const auto written = registry.call("nif_uv_transform", explicitOut);
    CHECK_FALSE(written.isError);
    CHECK(written.text.starts_with("set OK"));
    CHECK(std::filesystem::exists(out));
    CHECK(fileBytes(nif) == before);
}

TEST_CASE("MCP registered NIF tools flag an EXISTS reply as an error and keep the existing output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "tri.nif";
    fo4ide::test::writeBytes(nif, fo4ide::test::triangleNif());
    const auto out = root / "tri.glb";
    touch(out, "old");

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend plugins;
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("nif", std::make_shared<fo4ide::app::NifTarget>(service, textures, platform, plugins)).has_value());
    const fo4ide::settings::SharedSettings settings{[&] {
        fo4ide::settings::Settings allowed;
        allowed.mo2InstancePath = root;
        return allowed;
    }()};
    fo4ide::mcp::Registry registry;
    REQUIRE(registerNativeMcpTools(registry, dispatcher, fo4ide::logging::get("mcp-service-targets"), settings,
                                   [] { return std::vector<std::filesystem::path>{}; }));

    const nlohmann::json exportArgs{{"path", nif.string()}, {"out", out.string()}, {"overwrite", false},
                                    {"materials", true}, {"textures", "png"}, {"tangents", true}, {"vertex_colours", true}};
    const auto refused = registry.call("nif_export_gltf", exportArgs);
    CHECK(refused.isError);
    CHECK(refused.text.starts_with("EXISTS: "));
    CHECK(fileBytes(out) == "old");
}
