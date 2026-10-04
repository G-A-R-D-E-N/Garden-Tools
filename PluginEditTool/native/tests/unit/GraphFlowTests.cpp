#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/graph/GraphDefiniteAssignment.hpp>
#include <fo4ide/graph/GraphExecFlow.hpp>
#include <fo4ide/graph/GraphValidator.hpp>

#include "../helpers/GraphBuilder.hpp"

using namespace fo4ide::graph;
namespace papyrus = fo4ide::papyrus;
using test::Env;
using test::GraphBuilder;

namespace {

// Resolves the definitions the passes consume, exactly like the linearizer
// does (from the validation result).
std::map<std::string, const NodeDefinition*> definitionsOf(Env& env, GraphBuilder& builder) {
    GraphValidator validator(env.index, env.palette);
    const auto validation = validator.validate(builder.document);
    return validation.definitions;
}

// Finds the first diagnostic with `code`; null when absent.
const GraphDiagnostic* findByCode(const std::vector<GraphDiagnostic>& diagnostics,
                                  std::string_view code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code) return &diagnostic;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("A control flow loop back to a node that is not a loop is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto first = graph.node(env.palette, "global:Debug.Notification");
    const auto second = graph.node(env.palette, "global:Debug.Notification");

    graph.value(first, "arg:asNotificationText", "string", "\"a\"");
    graph.value(second, "arg:asNotificationText", "string", "\"b\"");
    graph.wire(entry, pinIds::kExec, first, pinIds::kExec);
    graph.wire(first, pinIds::kThen, second, pinIds::kExec);
    graph.wire(second, pinIds::kThen, first, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    const auto problems = flow.checkStructured(definitions);
    const auto* diagnostic = findByCode(problems, codes::kExecCycle);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->nodeId == first);
    REQUIRE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), second) !=
            diagnostic->relatedNodes.end());
    REQUIRE(diagnostic->message.find("goto") != std::string::npos);
}

TEST_CASE("A back edge into a while loop is structured, not a cycle") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    // The C# loop-condition shape: the entry feeds the loop directly and the
    // body closes back into it, so the back edge targets the loop header.
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");

    graph.wire(entry, pinIds::kExec, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kBody, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    REQUIRE(flow.backEdges().contains({enabled, loop}));
    REQUIRE(flow.checkStructured(definitions).empty());
}

TEST_CASE("Dominators: the entry dominates everything and the branch dominates the join") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto join = graph.node(env.palette, "global:Debug.Notification");

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, join, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, join, pinIds::kExec);
    graph.value(join, "arg:asNotificationText", "string", "\"j\"");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    for (const auto& node : {enabled, branch, count, join}) {
        REQUIRE(flow.dominates(entry, node));
        REQUIRE(flow.isReachable(node));
    }
    REQUIRE(flow.dominates(branch, join));
    REQUIRE_FALSE(flow.dominates(join, branch));
    REQUIRE_FALSE(flow.dominates(count, join));
    REQUIRE(flow.immediateDominator().at(join) == branch);
}

TEST_CASE("A node with no return path leaks out of the function") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"a\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    const auto leaks = flow.pathsLeavingWithoutReturn(definitions, entry);
    REQUIRE_FALSE(leaks.empty());
    REQUIRE(std::find(leaks.begin(), leaks.end(), notify) != leaks.end());
}

TEST_CASE("A return node satisfies every path") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    REQUIRE(flow.pathsLeavingWithoutReturn(definitions, entry).empty());
}

TEST_CASE("An exec node nothing reaches is flagged") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"a\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    // A wired pair that no flow reaches: touching is non-empty, so the
    // reachability check must flag the first one.
    const auto orphan = graph.node(env.palette, "global:Debug.Notification");
    const auto next = graph.node(env.palette, "global:Debug.Notification");
    graph.value(orphan, "arg:asNotificationText", "string", "\"o\"");
    graph.value(next, "arg:asNotificationText", "string", "\"n\"");
    graph.wire(orphan, pinIds::kThen, next, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    const auto problems =
        GraphExecFlow::checkReachability(graph.document, definitions, {&flow});
    const auto* diagnostic = findByCode(problems, codes::kUnreachableExec);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->severity == GraphSeverity::Warning);
    REQUIRE(diagnostic->nodeId == orphan);
}

TEST_CASE("A value produced on one arm and read after the merge is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, add, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    const auto* diagnostic = findByCode(problems, codes::kUseBeforeAssignment);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->nodeId == add);
    REQUIRE(diagnostic->pinId == "arg:aiCount");
    REQUIRE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST_CASE("An operator between the call and the use does not hide it") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);
    const auto sum = graph.node(env.palette, "op.add");
    const auto one = graph.node(env.palette, "literal.int");

    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, add, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, sum, pinIds::kLeft);
    graph.wire(one, pinIds::kValue, sum, pinIds::kRight);
    graph.wire(sum, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    const auto* diagnostic = findByCode(problems, codes::kUseBeforeAssignment);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->nodeId == add);
    REQUIRE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST_CASE("A value produced inside a loop and read after it is refused") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kCompleted, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    const auto* diagnostic = findByCode(problems, codes::kUseBeforeAssignment);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->nodeId == add);
    REQUIRE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST_CASE("A loop condition fed from the loop body is refused by name") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");

    graph.wire(entry, pinIds::kExec, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kBody, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    const auto* diagnostic = findByCode(problems, codes::kLoopConditionFromLoopBody);
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->nodeId == loop);
    REQUIRE(diagnostic->pinId == pinIds::kCondition);
    REQUIRE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), enabled) !=
            diagnostic->relatedNodes.end());
}

TEST_CASE("A value produced before the branch is readable after the merge") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, disable, pinIds::kExec);
    graph.wire(disable, pinIds::kThen, add, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    REQUIRE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    REQUIRE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}

TEST_CASE("A value produced and read on the same arm is accepted") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    REQUIRE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    REQUIRE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}

TEST_CASE("A value produced earlier in a loop body is readable later in it") {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto count = graph.node(env.palette, "call:ObjectReference.GetItemCount");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, count, pinIds::kExec);
    graph.wire(count, pinIds::kThen, add, pinIds::kExec);
    graph.wire(add, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.wire(count, pinIds::kReturn, add, "arg:aiCount");

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);
    const auto problems = definiteAssignment::check(graph.document, definitions, flow);

    REQUIRE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    REQUIRE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}
