#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fo4/c_api_graph.h>
#include <fo4/graph/graph_tool_service.hpp>
#include <fo4/graph/node_palette.hpp>

#include "graph_builder.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

std::string uniqueRoot(std::string_view tag) {
    return (std::filesystem::temp_directory_path() / ("fo4re-tool-" + std::string(tag)) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

const std::string& imports() {
    static const std::string value = baseStubs().string();
    return value;
}

template <typename F>
auto withGraph(const GraphDocument& document, F&& work) -> decltype(work(std::string())) {
    const std::string directory = uniqueRoot("graph");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Fixture.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << documentJson::serialize(document);
    }
    const auto result = work(path.string());
    std::filesystem::remove_all(directory);
    return result;
}

GraphDocument goodGraph() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    return graph.document;
}

GraphDocument brokenGraph() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
    return graph.document;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string takeText(radicle_status status, uint8_t* bytes, size_t size) {
    EXPECT_EQ(status, RADICLE_STATUS_OK);
    std::string text = bytes ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string();
    radicle_free_bytes(bytes);
    return text;
}

}

TEST(GraphToolService, GraphToolServiceValidatingAGoodGraphReportsOkWithItsSize) {
    const auto text = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::validate(path, &imports());
    });

    EXPECT_TRUE(startsWith(text, "RESULT: OK"));
    EXPECT_TRUE(text.find("2 nodes") != std::string::npos);
    EXPECT_TRUE(text.find("1 wires") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceValidatingABrokenGraphNamesTheNodeAndThePin) {
    const auto text = withGraph(brokenGraph(), [](const std::string& path) {
        return GraphToolService::validate(path, &imports());
    });

    EXPECT_TRUE(startsWith(text, "RESULT: 1 error"));
    EXPECT_TRUE(text.find(std::string(codes::kMissingRequiredInput)) != std::string::npos);
    EXPECT_TRUE(text.find("node n") != std::string::npos);
    EXPECT_TRUE(text.find("pin arg:afSeconds") != std::string::npos);
    EXPECT_TRUE(text.find("global:Utility.Wait") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceAMissingGraphFileIsAMessageRatherThanAnException) {
    const auto text = GraphToolService::validate("/no/such/graph.fograph", &imports());

    EXPECT_TRUE(startsWith(text, "RESULT: FAILED"));
    EXPECT_TRUE(text.find("No graph document") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceAMalformedGraphFileIsAMessageRatherThanAnException) {
    const std::string directory = uniqueRoot("bad");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Bad.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "{ not json";
    }

    const auto text = GraphToolService::validate(path, &imports());
    EXPECT_TRUE(startsWith(text, "RESULT: FAILED"));

    std::filesystem::remove_all(directory);
}

TEST(GraphToolService, GraphToolServiceCompilingSourceOnlyReturnsTheGeneratedScript) {
    const auto text = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::compile(path, nullptr, &imports(), true);
    });

    EXPECT_TRUE(startsWith(text, "RESULT: OK"));
    EXPECT_TRUE(text.find("Scriptname Fixture extends ObjectReference") != std::string::npos);
    EXPECT_TRUE(text.find("Debug.Notification(\"opened\")") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceCompilingWithAnOutputFolderWritesBothFiles) {
    const std::string output = uniqueRoot("out");
    const auto outputPath = std::filesystem::path(output);
    const auto text = withGraph(goodGraph(), [&](const std::string& path) {
        return GraphToolService::compile(path, &outputPath, &imports(),
                                         false);
    });

    EXPECT_TRUE(startsWith(text, "RESULT: OK"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(output) / "Fixture.psc"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(output) / "Fixture.pex"));
    EXPECT_TRUE(text.find("Wrote ") != std::string::npos);

    std::filesystem::remove_all(output);
}

TEST(GraphToolService, GraphToolServiceAFailedCompileShowsTheGeneratedSourceItCouldNotUse) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto level = graph.node(env.palette, "call:Actor.GetLevel");
    graph.wire(entry, pinIds::kExec, level, pinIds::kExec);

    const auto text = withGraph(graph.document, [](const std::string& path) {
        return GraphToolService::compile(path, nullptr, &imports(), false);
    });

    EXPECT_TRUE(startsWith(text, "RESULT: FAILED"));
    EXPECT_TRUE(text.find(std::string(codes::kUnconnectedSelf)) != std::string::npos);
    EXPECT_TRUE(text.find("node n") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceSearchingThePaletteReturnsDefinitionIdsAndATrueTotal) {
    const auto text = GraphToolService::searchPalette("AddItem", &imports(), 5);

    EXPECT_TRUE(startsWith(text, "RESULT:"));
    EXPECT_TRUE(text.find("call:ObjectReference.AddItem") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceACappedSearchSaysHowMuchItIsHiding) {
    const auto text = GraphToolService::searchPalette("Get", &imports(), 2);

    EXPECT_TRUE(text.find("more not shown") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceDescribingANodeListsItsPinsWithTypesAndDefaults) {
    const auto text = GraphToolService::describeNode("call:ObjectReference.AddItem", &imports());

    EXPECT_TRUE(text.find("call:ObjectReference.AddItem") != std::string::npos);
    EXPECT_TRUE(text.find("arg:akItemToAdd") != std::string::npos);
    EXPECT_TRUE(text.find("Form") != std::string::npos);
    EXPECT_TRUE(text.find("arg:aiCount") != std::string::npos);
    EXPECT_TRUE(text.find("optional") != std::string::npos);
    EXPECT_TRUE(text.find("default 1") != std::string::npos);
    EXPECT_TRUE(text.find("Sequenced") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceDescribingAPureNodeSaysItEvaluatesInline) {
    const auto text = GraphToolService::describeNode("op.add", &imports());

    EXPECT_TRUE(text.find("Pure") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceAnUnknownNodeTypeIsAMessageRatherThanAnException) {
    const auto text = GraphToolService::describeNode("call:NoSuchScript.NoSuchThing", &imports());

    EXPECT_TRUE(startsWith(text, "RESULT: FAILED"));
    EXPECT_TRUE(text.find("No node type") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceCApiValidateReturnsTheServiceTextForAGoodGraph) {
    const auto text = withGraph(goodGraph(), [](const std::string& path) {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        const auto status =
            radicle_graph_validate(path.c_str(), imports().c_str(), nullptr, 0, &bytes, &size);
        return takeText(status, bytes, size);
    });
    const auto expected = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::validate(path, &imports());
    });

    EXPECT_EQ(text, expected);
    EXPECT_TRUE(startsWith(text, "RESULT: OK"));
}

TEST(GraphToolService, GraphToolServiceCApiValidateMatchesTheServiceForABrokenGraphWithBaseImports) {
    const char* const bases[] = {imports().c_str()};
    const std::vector<std::filesystem::path> roots{imports()};
    const auto text = withGraph(brokenGraph(), [&](const std::string& path) {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        const auto status = radicle_graph_validate(path.c_str(), nullptr, bases, 1, &bytes, &size);
        return takeText(status, bytes, size);
    });
    const auto expected = withGraph(brokenGraph(), [&](const std::string& path) {
        return GraphToolService::validate(path, nullptr, roots);
    });

    EXPECT_EQ(text, expected);
    EXPECT_TRUE(startsWith(text, "RESULT: 1 error"));
    EXPECT_TRUE(text.find(std::string(codes::kMissingRequiredInput)) != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceCApiAMissingGraphIsTextWithOkStatusNotAnErrorStatus) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_validate("/no/such/graph.fograph", imports().c_str(), nullptr,
                                               0, &bytes, &size);
    const auto text = takeText(status, bytes, size);

    EXPECT_EQ(text, GraphToolService::validate("/no/such/graph.fograph", &imports()));
    EXPECT_TRUE(startsWith(text, "RESULT: FAILED"));
}

TEST(GraphToolService, GraphToolServiceCApiCompileSourceOnlyMatchesTheServiceText) {
    const std::string output = uniqueRoot("cout");
    const auto outputPath = std::filesystem::path(output);
    const auto text = withGraph(goodGraph(), [&](const std::string& path) {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        const auto status = radicle_graph_compile(path.c_str(), output.c_str(), imports().c_str(),
                                                  1, nullptr, 0, &bytes, &size);
        return takeText(status, bytes, size);
    });
    const auto expected = withGraph(goodGraph(), [&](const std::string& path) {
        return GraphToolService::compile(path, &outputPath, &imports(), true);
    });

    EXPECT_EQ(text, expected);
    EXPECT_TRUE(startsWith(text, "RESULT: OK. Generated source for Fixture."));
    std::filesystem::remove_all(output);
}

TEST(GraphToolService, GraphToolServiceCApiCompileWithANullOrEmptyOutputPathWritesNothing) {
    const auto expected = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::compile(path, nullptr, &imports(), true);
    });
    for (const char* output : {static_cast<const char*>(nullptr), ""}) {
        const auto text = withGraph(goodGraph(), [&](const std::string& path) {
            uint8_t* bytes = nullptr;
            size_t size = 0;
            const auto status = radicle_graph_compile(path.c_str(), output, imports().c_str(), 1,
                                                      nullptr, 0, &bytes, &size);
            return takeText(status, bytes, size);
        });

        EXPECT_EQ(text, expected);
        EXPECT_TRUE(startsWith(text, "RESULT: OK. Generated source for Fixture."));
        EXPECT_TRUE(text.find("Wrote ") == std::string::npos);
    }
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::current_path() / "Fixture.psc"));
}

TEST(GraphToolService, GraphToolServiceCApiCompileRefusesAnOutputPathThatEscapesWithDotDot) {
    const std::string root = uniqueRoot("escape");
    const std::string output = (std::filesystem::path(root) / ".." / "escaped").string();
    const auto escaped = std::filesystem::path(root).parent_path() / "escaped";
    const auto status = withGraph(goodGraph(), [&](const std::string& path) {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        const auto result = radicle_graph_compile(path.c_str(), output.c_str(), imports().c_str(),
                                                  0, nullptr, 0, &bytes, &size);
        EXPECT_EQ(bytes, nullptr);
        EXPECT_EQ(size, 0u);
        return result;
    });

    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(std::filesystem::exists(escaped));
    std::filesystem::remove_all(escaped);
    std::filesystem::remove_all(root);
}

TEST(GraphToolService, GraphToolServiceCApiNullRequiredArgumentsAreRefusedWithClearedOutputs) {
    uint8_t sentinel = 0;
    uint8_t* bytes = &sentinel;
    size_t size = 99;
    auto status = radicle_graph_validate(nullptr, nullptr, nullptr, 0, &bytes, &size);
    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);

    bytes = &sentinel;
    size = 99;
    status = radicle_graph_compile(nullptr, "out", nullptr, 0, nullptr, 0, &bytes, &size);
    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);

    bytes = &sentinel;
    size = 99;
    status = radicle_graph_palette_search(nullptr, nullptr, 5, nullptr, 0, &bytes, &size);
    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);

    bytes = &sentinel;
    size = 99;
    status = radicle_graph_describe_node(nullptr, nullptr, nullptr, 0, &bytes, &size);
    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
    EXPECT_EQ(size, 0u);

    EXPECT_EQ(radicle_graph_validate("Fixture.fograph", nullptr, nullptr, 0, nullptr, nullptr),
              RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(GraphToolService, GraphToolServiceCApiRefusesANullBaseImportArrayWithANonZeroCount) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_graph_describe_node("op.add", nullptr, nullptr, 1, &bytes, &size);

    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
}

TEST(GraphToolService, GraphToolServiceCApiAZeroOrNegativeLimitGivesTheServiceDefaultOfThirty) {
    for (const int limit : {0, -1}) {
        uint8_t* bytes = nullptr;
        size_t size = 0;
        const auto status =
            radicle_graph_palette_search("Get", imports().c_str(), limit, nullptr, 0, &bytes, &size);

        EXPECT_EQ(takeText(status, bytes, size),
                  GraphToolService::searchPalette("Get", &imports(), 30));
    }
}

TEST(GraphToolService, GraphToolServiceCApiRefusesANullBaseImportEntry) {
    const char* const bases[] = {nullptr};
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status =
        radicle_graph_describe_node("op.add", nullptr, bases, 1, &bytes, &size);

    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(bytes, nullptr);
}

TEST(GraphToolService, GraphToolServiceCApiPaletteSearchMatchesTheServiceIncludingTheLimit) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status =
        radicle_graph_palette_search("Get", imports().c_str(), 2, nullptr, 0, &bytes, &size);
    const auto text = takeText(status, bytes, size);

    EXPECT_EQ(text, GraphToolService::searchPalette("Get", &imports(), 2));
    EXPECT_TRUE(text.find("more not shown") != std::string::npos);
}

TEST(GraphToolService, GraphToolServiceCApiDescribeNodeMatchesTheServiceForKnownAndUnknownTypes) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    auto status = radicle_graph_describe_node("call:ObjectReference.AddItem", imports().c_str(),
                                              nullptr, 0, &bytes, &size);
    const auto known = takeText(status, bytes, size);

    bytes = nullptr;
    size = 0;
    status = radicle_graph_describe_node("call:NoSuchScript.NoSuchThing", imports().c_str(),
                                         nullptr, 0, &bytes, &size);
    const auto unknown = takeText(status, bytes, size);

    EXPECT_EQ(known, GraphToolService::describeNode("call:ObjectReference.AddItem", &imports()));
    EXPECT_TRUE(known.find("arg:akItemToAdd") != std::string::npos);
    EXPECT_EQ(unknown, GraphToolService::describeNode("call:NoSuchScript.NoSuchThing", &imports()));
    EXPECT_TRUE(startsWith(unknown, "RESULT: FAILED"));
}
