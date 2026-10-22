#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <fo4ide/graph/GraphToolService.hpp>
#include <fo4ide/graph/NodePalette.hpp>

#include "../helpers/GraphBuilder.hpp"
#include "../helpers/TempRoot.hpp"

using namespace fo4ide::graph;
using namespace fo4ide::graph::test;
using fo4ide_test::TempRoot;

namespace {

// Static so `&imports()` is a stable pointer to pass to the service.
const std::string& imports() {
    static const std::string value = baseStubs().string();
    return value;
}

// Port of WithGraph: serializes the document to a temp .fograph and runs work.
template <typename F>
auto withGraph(const GraphDocument& document, F&& work) -> decltype(work(std::string())) {
    const TempRoot guard("fo4ide-tool-graph");
    const auto path = guard.path() / "Fixture.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << documentJson::serialize(document);
    }
    const auto result = work(path.string());
    return result;
}

// Port of GoodGraph: OnActivate -> Debug.Notification("opened").
GraphDocument goodGraph() {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    return graph.document;
}

// Port of BrokenGraph: OnLoad -> Utility.Wait with no arguments.
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

}  // namespace

TEST_CASE("GraphToolService validating a good graph reports ok with its size") {
    const auto text = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::validate(path, &imports());
    });

    CHECK(startsWith(text, "RESULT: OK"));
    CHECK(text.find("2 nodes") != std::string::npos);
    CHECK(text.find("1 wires") != std::string::npos);
}

TEST_CASE("GraphToolService validating a broken graph names the node and the pin") {
    const auto text = withGraph(brokenGraph(), [](const std::string& path) {
        return GraphToolService::validate(path, &imports());
    });

    CHECK(startsWith(text, "RESULT: 1 error"));
    CHECK(text.find(std::string(codes::kMissingRequiredInput)) != std::string::npos);
    CHECK(text.find("node n") != std::string::npos);
    CHECK(text.find("pin arg:afSeconds") != std::string::npos);
    CHECK(text.find("global:Utility.Wait") != std::string::npos);
}

TEST_CASE("GraphToolService a missing graph file is a message rather than an exception") {
    const auto text = GraphToolService::validate("/no/such/graph.fograph", &imports());

    CHECK(startsWith(text, "RESULT: FAILED"));
    CHECK(text.find("No graph document") != std::string::npos);
}

TEST_CASE("GraphToolService a malformed graph file is a message rather than an exception") {
    const TempRoot guard("fo4ide-tool-bad");
    const auto path = guard.path() / "Bad.fograph";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "{ not json";
    }

    const auto text = GraphToolService::validate(path, &imports());
    CHECK(startsWith(text, "RESULT: FAILED"));
}

TEST_CASE("GraphToolService compiling source only returns the generated script") {
    const auto text = withGraph(goodGraph(), [](const std::string& path) {
        return GraphToolService::compile(path, nullptr, &imports(), /*sourceOnly=*/true);
    });

    CHECK(startsWith(text, "RESULT: OK"));
    CHECK(text.find("Scriptname Fixture extends ObjectReference") != std::string::npos);
    CHECK(text.find("Debug.Notification(\"opened\")") != std::string::npos);
}

TEST_CASE("GraphToolService compiling with an output folder writes both files") {
    const TempRoot output("fo4ide-tool-out");
    const auto outputPath = output.path() / "out";
    const auto text = withGraph(goodGraph(), [&](const std::string& path) {
        return GraphToolService::compile(path, &outputPath, &imports(),
                                         /*sourceOnly=*/false);
    });

    CHECK(startsWith(text, "RESULT: OK"));
    CHECK(std::filesystem::exists(outputPath / "Fixture.psc"));
    CHECK(std::filesystem::exists(outputPath / "Fixture.pex"));
    CHECK(text.find("Wrote ") != std::string::npos);
}

TEST_CASE("GraphToolService a failed compile shows the generated source it could not use") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto level = graph.node(env.palette, "call:Actor.GetLevel");
    graph.wire(entry, pinIds::kExec, level, pinIds::kExec);

    const auto text = withGraph(graph.document, [](const std::string& path) {
        return GraphToolService::compile(path, nullptr, &imports(), /*sourceOnly=*/false);
    });

    CHECK(startsWith(text, "RESULT: FAILED"));
    CHECK(text.find(std::string(codes::kUnconnectedSelf)) != std::string::npos);
    CHECK(text.find("node n") != std::string::npos);
}

TEST_CASE("GraphToolService searching the palette returns definition ids and a true total") {
    const auto text = GraphToolService::searchPalette("AddItem", &imports(), /*limit=*/5);

    CHECK(startsWith(text, "RESULT:"));
    CHECK(text.find("call:ObjectReference.AddItem") != std::string::npos);
}

TEST_CASE("GraphToolService a capped search says how much it is hiding") {
    const auto text = GraphToolService::searchPalette("Get", &imports(), /*limit=*/2);

    CHECK(text.find("more not shown") != std::string::npos);
}

TEST_CASE("GraphToolService describing a node lists its pins with types and defaults") {
    const auto text = GraphToolService::describeNode("call:ObjectReference.AddItem", &imports());

    CHECK(text.find("call:ObjectReference.AddItem") != std::string::npos);
    CHECK(text.find("arg:akItemToAdd") != std::string::npos);
    CHECK(text.find("Form") != std::string::npos);
    CHECK(text.find("arg:aiCount") != std::string::npos);
    CHECK(text.find("optional") != std::string::npos);
    CHECK(text.find("default 1") != std::string::npos);
    CHECK(text.find("Sequenced") != std::string::npos);
}

TEST_CASE("GraphToolService describing a pure node says it evaluates inline") {
    const auto text = GraphToolService::describeNode("op.add", &imports());

    CHECK(text.find("Pure") != std::string::npos);
}

TEST_CASE("GraphToolService an unknown node type is a message rather than an exception") {
    const auto text = GraphToolService::describeNode("call:NoSuchScript.NoSuchThing", &imports());

    CHECK(startsWith(text, "RESULT: FAILED"));
    CHECK(text.find("No node type") != std::string::npos);
}
