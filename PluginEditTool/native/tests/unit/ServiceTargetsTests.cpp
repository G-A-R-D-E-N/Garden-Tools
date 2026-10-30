#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

#include "../helpers/GraphBuilder.hpp"
#include "../helpers/NifFixture.hpp"
#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

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
        {"DataFolder", (root / "Data").string()}
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
    fo4ide::app::NifTarget target(service, textures, platform);

    auto result = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());
    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    REQUIRE(parsed.contains("error"));
    CHECK_FALSE(parsed.at("error").get<std::string>().empty());
    CHECK_FALSE(parsed.contains("blocks"));
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
    fo4ide::app::NifTarget target(service, textures, platform);

    auto saved = target.invoke("ApplyEdits", nlohmann::json::array({nif.string(), edits.dump(), ""}));
    REQUIRE(saved.has_value());
    CHECK(saved->get<std::string>().find("set OK") != std::string::npos);

    auto tree = target.invoke("Tree", nlohmann::json::array({nif.string()}));
    REQUIRE(tree.has_value());
    CHECK(tree->get<std::string>().find("\"Renamed\"") != std::string::npos);
}

TEST_CASE("NifTarget stages browser dropped bytes without the CSharp host") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::app::NifTarget target(service, textures, platform);

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

namespace {

// Minimal backend that records the operation it was asked to run, so the
// CellTarget record-delegation path can be observed without a Radicle backend.
class CapturingCellBackend final : public fo4ide::plugin::IPluginBackend {
public:
    std::string lastOperation;

    fo4ide::plugin::Capabilities capabilities() const noexcept override {
        return {.available = true};
    }

    fo4ide::Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json&) override {
        lastOperation = operation;
        return nlohmann::json("delegated");
    }
};

}  // namespace

TEST_CASE("CellTarget geometry batch reports not-found meshes and dedupes case-insensitively") {
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;  // no roots -> nothing resolves
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["a/b.nif","A/B.NIF","c/d.nif"," c/d.nif"])"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 2);  // "A/B.NIF" deduped against "a/b.nif"
    const auto& geometry = parsed.at("geometry");
    REQUIRE(geometry.size() == 4);
    CHECK(geometry.at("a/b.nif").at("error") == "Not found (loose or in any indexed archive).");
    CHECK(geometry.at("A/B.NIF") == geometry.at("a/b.nif"));
    CHECK(geometry.at("c/d.nif").contains("error"));
    CHECK(geometry.at(" c/d.nif") == geometry.at("c/d.nif"));

    const auto progress = nlohmann::json::parse(
        target.invoke("GetGeometryBatchProgress", nlohmann::json::array())->get<std::string>());
    CHECK(progress.at("done") == 2);
    CHECK(progress.at("total") == 2);
}


TEST_CASE("CellTarget geometry batch embeds in-process geometry for a resolved mesh") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto mesh = root / "meshes" / "clutter" / "thing.nif";
    fo4ide::test::writeBytes(mesh, fo4ide::test::triangleNif());

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["meshes/clutter/thing.nif"])"}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 1);
    const auto& shapes = parsed.at("geometry").at("meshes/clutter/thing.nif").at("shapes");
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0].at("verts").size() == 9);
}

TEST_CASE("CellTarget geometry batch isolates a truncated mesh from its neighbours") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto good = root / "meshes" / "a.nif";
    const auto broken = root / "meshes" / "b.nif";
    fo4ide::test::writeBytes(good, fo4ide::test::triangleNif());
    auto truncated = fo4ide::test::triangleNif();
    truncated.resize(truncated.size() - 8);
    fo4ide::test::writeBytes(broken, truncated);

    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetGeometryBatch",
        nlohmann::json::array({R"(["meshes/a.nif","meshes/b.nif"])"}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("count") == 2);
    CHECK(parsed.at("geometry").at("meshes/a.nif").contains("shapes"));
    REQUIRE(parsed.at("geometry").at("meshes/b.nif").contains("error"));
}


TEST_CASE("NifTarget Geo reports a parse failure as JSON the panel can show") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto nif = root / "sample.nif";
    touch(nif);

    FakePlatform platform;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::app::NifTarget target(service, textures, platform);

    auto result = target.invoke("Geo", nlohmann::json::array({nif.string()}));
    REQUIRE(result.has_value());

    const auto parsed = nlohmann::json::parse(result->get<std::string>());
    CHECK(parsed.at("error") == "NIF header line is missing or longer than 1024 bytes");
}

TEST_CASE("CellTarget GetTexture returns empty when the model cannot be resolved") {
    FakeRunner runner;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetTexture", nlohmann::json::array({"meshes/x.nif", "textures/x_d.dds"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("CellTarget GetTexture stays empty when a resolved model has no usable texture") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto mesh = root / "meshes" / "thing.nif";
    touch(mesh);

    FakeRunner runner;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    textures.setSessionRoots({root});
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    auto result = target.invoke(
        "GetTexture", nlohmann::json::array({"meshes/thing.nif", "textures/missing.dds"}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().empty());
}

TEST_CASE("CellTarget GetGeometryBatch rejects a valid non-array payload") {
    FakeRunner runner;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    fo4ide::plugin::NullPluginBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    for (const auto* payload : {"{}", "5", "\"meshes/a.nif\""}) {
        auto result = target.invoke("GetGeometryBatch", nlohmann::json::array({payload}));
        REQUIRE(result.has_value());
        const auto parsed = nlohmann::json::parse(result->get<std::string>());
        REQUIRE(parsed.contains("error"));
        CHECK(parsed.find("count") == parsed.end());
    }
}

TEST_CASE("CellTarget delegates record queries to the plugin backend facet") {
    FakeRunner runner;
    fo4ide::assets::NifService service;
    fo4ide::assets::TextureResolver textures;
    CapturingCellBackend backend;
    fo4ide::app::CellTarget target(backend, service, textures);

    // GetPlugins on the "cell" facet forwards to appInterop.GetPlugins.
    auto result = target.invoke("GetPlugins", nlohmann::json::array());
    REQUIRE(result.has_value());
    CHECK(backend.lastOperation == "appInterop.GetPlugins");
}

TEST_CASE("GraphTarget serves the core palette with builtins") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto result = target.invoke("GetCorePalette", nlohmann::json::array());
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    const auto json = nlohmann::json::parse(result->get<std::string>());

    CHECK(json.at("version").get<int>() == 1);
    REQUIRE(json.at("builtins").is_array());
    REQUIRE_FALSE(json.at("builtins").empty());
    CHECK(json.at("wiki").contains("available"));
    CHECK(json.at("scripts").is_array());

    const auto& first = json.at("builtins").at(0);
    CHECK(first.contains("type"));
    CHECK(first.contains("kind"));
    CHECK(first.at("pins").is_array());
}

TEST_CASE("GraphTarget describes a known builtin and rejects an unknown one") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto palette = target.invoke("GetCorePalette", nlohmann::json::array());
    REQUIRE(palette.has_value());
    const auto paletteJson = nlohmann::json::parse(palette->get<std::string>());
    const auto nodeType = paletteJson.at("builtins").at(0).at("type").get<std::string>();

    auto known = target.invoke("GetNodeSignature", nlohmann::json::array({nodeType}));
    REQUIRE(known.has_value());
    const auto knownText = known->get<std::string>();
    CHECK(knownText.rfind("Error:", 0) != 0);
    CHECK(nlohmann::json::parse(knownText).at("type").get<std::string>() == nodeType);

    auto unknown =
        target.invoke("GetNodeSignature", nlohmann::json::array({"no.such.node.type"}));
    REQUIRE(unknown.has_value());
    CHECK(unknown->get<std::string>() ==
          "Error: no node type 'no.such.node.type' on this palette");
}

TEST_CASE("GraphTarget searches the palette and reports a total") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto result =
        target.invoke("SearchPalette", nlohmann::json::array({"any", "", "", 0}));
    REQUIRE(result.has_value());
    const auto json = nlohmann::json::parse(result->get<std::string>());
    REQUIRE(json.at("entries").is_array());
    CHECK(json.at("total").get<int>() >= static_cast<int>(json.at("entries").size()));
}

TEST_CASE("GraphTarget validates documents and reports malformed input") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto malformed =
        target.invoke("ValidateGraph", nlohmann::json::array({"this is not a document"}));
    REQUIRE(malformed.has_value());
    const auto malformedJson = nlohmann::json::parse(malformed->get<std::string>());
    CHECK(malformedJson.at("ok").get<bool>() == false);
    CHECK_FALSE(malformedJson.at("diagnostics").empty());

    auto valid =
        target.invoke("ValidateGraph", nlohmann::json::array({R"({ "schema": 1 })"}));
    REQUIRE(valid.has_value());
    const auto validJson = nlohmann::json::parse(valid->get<std::string>());
    CHECK(validJson.contains("ok"));
    CHECK(validJson.at("diagnostics").is_array());
}

TEST_CASE("GraphTarget resolves Extends and palette scripts from Settings base imports without the env var") {
    using namespace fo4ide::graph;
    using namespace fo4ide::graph::test;

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root / "Fallout 4";
    settings.papyrusBaseImports = baseStubs();
    fo4ide::settings::SharedSettings shared(settings);
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    FakePlatform platform;
    fo4ide::app::GraphTarget target(platform, locator);

    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto document = documentJson::serialize(graph.document);

    auto validation = target.invoke("ValidateGraph", nlohmann::json::array({document}));
    REQUIRE(validation.has_value());
    const auto diagnostics =
        nlohmann::json::parse(validation->get<std::string>()).at("diagnostics");
    for (const auto& diagnostic : diagnostics) {
        CHECK(diagnostic.at("code").get<std::string>() != "GRA0031");
    }

    auto palette = target.invoke("GetCorePalette", nlohmann::json::array());
    REQUIRE(palette.has_value());
    const auto scripts = nlohmann::json::parse(palette->get<std::string>())
                             .at("scripts")
                             .get<std::vector<std::string>>();
    CHECK(std::find(scripts.begin(), scripts.end(), "ObjectReference") != scripts.end());
}

TEST_CASE("GraphTarget rejects an unknown method") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto result = target.invoke("Nonexistent", nlohmann::json::array());
    CHECK_FALSE(result.has_value());
}

TEST_CASE("GraphTarget SaveGraph reports malformed documents") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    const auto path = (root / "broken.fograph").string();
    auto result =
        target.invoke("SaveGraph", nlohmann::json::array({path, "not a document"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().rfind("Error:", 0) == 0);
}

TEST_CASE("GraphTarget CompileToPex errors instead of reporting a write that failed") {
    using namespace fo4ide::graph;
    using namespace fo4ide::graph::test;

    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root / "Fixture.pex");

    const auto stubs = baseStubs().string();
#ifdef _WIN32
    _putenv_s("PAPYRUS_BASE_IMPORTS", stubs.c_str());
#else
    setenv("PAPYRUS_BASE_IMPORTS", stubs.c_str(), 1);
#endif

    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    const auto document = documentJson::serialize(graph.document);

    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);
    const auto result =
        target.invoke("CompileToPex", nlohmann::json::array({document, root.string()}));

#ifdef _WIN32
    _putenv_s("PAPYRUS_BASE_IMPORTS", "");
#else
    unsetenv("PAPYRUS_BASE_IMPORTS");
#endif

    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.rfind("Error:", 0) == 0);
    CHECK(text.find("\"written\"") == std::string::npos);
}

TEST_CASE("GraphTarget serializes concurrent RPC access to its compiler cache") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared;
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    std::atomic<bool> threw{false};
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&, t] {
                for (int i = 0; i < 40; ++i) {
                    try {
                        switch ((t + i) % 4) {
                            case 0:
                                static_cast<void>(target.invoke("GetCorePalette", nlohmann::json::array()));
                                break;
                            case 1:
                                static_cast<void>(target.invoke("SearchPalette",
                                              nlohmann::json::array({"any", "debug", "", 10})));
                                break;
                            case 2:
                                static_cast<void>(target.invoke("ValidateGraph", nlohmann::json::array({"{}"})));
                                break;
                            default:
                                static_cast<void>(target.invoke("Refresh", nlohmann::json::array()));
                                break;
                        }
                    } catch (...) {
                        threw = true;
                    }
                }
            });
        }
    }
    CHECK_FALSE(threw.load());
}
