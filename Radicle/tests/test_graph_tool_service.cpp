#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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
