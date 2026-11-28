#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4/graph/graph_document.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "../helpers/ServiceTargetFakes.hpp"

using namespace fo4ide::app::test;

TEST_CASE("GraphTarget serves the core palette with builtins") {
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared(withoutGame());
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
    fo4ide::settings::SharedSettings shared(withoutGame());
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
    fo4ide::settings::SharedSettings shared(withoutGame());
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
    fo4ide::settings::SharedSettings shared(withoutGame());
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
    using namespace fo4::graph;
    using namespace fo4::graph::test;

    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::settings::Settings settings;
    settings.fallout4Path = root / "Fallout 4";
    settings.papyrusBaseImports = baseStubs();
    fo4ide::settings::SharedSettings shared(withoutGame(settings));
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
    fo4ide::settings::SharedSettings shared(withoutGame());
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    auto result = target.invoke("Nonexistent", nlohmann::json::array());
    CHECK_FALSE(result.has_value());
}

TEST_CASE("GraphTarget SaveGraph reports malformed documents") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared(withoutGame());
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    const auto path = (root / "broken.fograph").string();
    auto result =
        target.invoke("SaveGraph", nlohmann::json::array({path, "not a document"}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().rfind("Error:", 0) == 0);
}

TEST_CASE("GraphTarget SaveGraph replaces an existing file whole and leaves no staging file") {
    using namespace fo4::graph;
    using namespace fo4::graph::test;

    const auto root = tempRoot();
    Cleanup cleanup{root};
    FakePlatform platform;
    fo4ide::settings::SharedSettings shared(withoutGame());
    fo4ide::tools::Locator locator(shared, "FO4IDE-native");
    fo4ide::app::GraphTarget target(platform, locator);

    const auto path = root / "nested" / "saved.fograph";
    const auto save = [&](const char* name) {
        GraphBuilder graph(name, "ObjectReference");
        const auto text = documentJson::serialize(graph.document);
        auto result = target.invoke("SaveGraph", nlohmann::json::array({path.string(), text}));
        REQUIRE(result.has_value());
        CHECK(result->get<std::string>().empty());
    };

    save("First");
    save("Second");

    std::ifstream stream(path, std::ios::binary);
    const std::string onDisk((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto loaded = documentJson::tryDeserialize(onDisk);
    REQUIRE(loaded.document.has_value());
    CHECK(loaded.document->header.scriptName == "Second");
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
        static_cast<void>(entry);
        ++entries;
    }
    CHECK(entries == 1);
}

TEST_CASE("GraphTarget CompileToPex errors instead of reporting a write that failed") {
    using namespace fo4::graph;
    using namespace fo4::graph::test;

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
    fo4ide::settings::SharedSettings shared(withoutGame());
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
    fo4ide::settings::SharedSettings shared(withoutGame());
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

TEST_CASE("GraphTarget CompileToPex writes nothing for a script name that is not a plain file name") {
    using namespace fo4::graph;
    using namespace fo4::graph::test;

    const auto root = tempRoot();
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);

    const auto stubs = baseStubs().string();
#ifdef _WIN32
    _putenv_s("PAPYRUS_BASE_IMPORTS", stubs.c_str());
#else
    setenv("PAPYRUS_BASE_IMPORTS", stubs.c_str(), 1);
#endif

    Env env;
    const std::pair<std::string, std::string> cases[] = {
        {"Ns:Fixture", "must use only ASCII letters, digits and underscore"},
        {"NUL", "is a reserved device name"},
    };
    std::vector<std::string> results;
    for (const auto& testCase : cases) {
        GraphBuilder graph(testCase.first, "ObjectReference");
        const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
        const auto notify = graph.node(env.palette, "global:Debug.Notification");
        graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
        graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
        const auto document = documentJson::serialize(graph.document);

        FakePlatform platform;
        fo4ide::settings::SharedSettings shared;
        fo4ide::tools::Locator locator(shared, "FO4IDE-native");
        fo4ide::app::GraphTarget target(platform, locator);
        const auto result = target.invoke("CompileToPex", nlohmann::json::array({document, root.string()}));
        results.push_back(result ? result->get<std::string>() : std::string("<no value>"));
    }

#ifdef _WIN32
    _putenv_s("PAPYRUS_BASE_IMPORTS", "");
#else
    unsetenv("PAPYRUS_BASE_IMPORTS");
#endif

    REQUIRE(results.size() == std::size(cases));
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i] == "Error: Script name '" + cases[i].first + "' cannot be written as a file: " + cases[i].second);
    }
    CHECK(std::filesystem::is_empty(root));
}
