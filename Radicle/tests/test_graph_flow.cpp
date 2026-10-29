#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fo4/graph/graph_definite_assignment.hpp>
#include <fo4/graph/graph_exec_flow.hpp>
#include <fo4/graph/graph_validator.hpp>

#include "graph_builder.hpp"

using namespace fo4::graph;
namespace papyrus = fo4::papyrus;
using test::Env;
using test::GraphBuilder;

namespace {

std::map<std::string, const NodeDefinition*> definitionsOf(Env& env, GraphBuilder& builder) {
    GraphValidator validator(env.index, env.palette);
    const auto validation = validator.validate(builder.document);
    return validation.definitions;
}

const GraphDiagnostic* findByCode(const std::vector<GraphDiagnostic>& diagnostics,
                                  std::string_view code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code) return &diagnostic;
    }
    return nullptr;
}

}

TEST(GraphFlow, AControlFlowLoopBackToANodeThatIsNotALoopIsRefused) {
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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->nodeId == first);
    ASSERT_TRUE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), second) !=
            diagnostic->relatedNodes.end());
    ASSERT_TRUE(diagnostic->message.find("goto") != std::string::npos);
}

TEST(GraphFlow, ABackEdgeIntoAWhileLoopIsStructuredNotACycle) {
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

    ASSERT_TRUE(flow.backEdges().contains({enabled, loop}));
    ASSERT_TRUE(flow.checkStructured(definitions).empty());
}

TEST(GraphFlow, DominatorsTheEntryDominatesEverythingAndTheBranchDominatesTheJoin) {
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
        ASSERT_TRUE(flow.dominates(entry, node));
        ASSERT_TRUE(flow.isReachable(node));
    }
    ASSERT_TRUE(flow.dominates(branch, join));
    ASSERT_FALSE(flow.dominates(join, branch));
    ASSERT_FALSE(flow.dominates(count, join));
    ASSERT_TRUE(flow.immediateDominator().at(join) == branch);
}

TEST(GraphFlow, ANodeWithNoReturnPathLeaksOutOfTheFunction) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"a\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    const auto leaks = flow.pathsLeavingWithoutReturn(definitions, entry);
    ASSERT_FALSE(leaks.empty());
    ASSERT_TRUE(std::find(leaks.begin(), leaks.end(), notify) != leaks.end());
}

TEST(GraphFlow, AReturnNodeSatisfiesEveryPath) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);

    const auto definitions = definitionsOf(env, graph);
    GraphExecFlow flow(graph.document, definitions, entry);

    ASSERT_TRUE(flow.pathsLeavingWithoutReturn(definitions, entry).empty());
}

TEST(GraphFlow, AnExecNodeNothingReachesIsFlagged) {
    Env env;
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"a\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->severity == GraphSeverity::Warning);
    ASSERT_TRUE(diagnostic->nodeId == orphan);
}

TEST(GraphFlow, AValueProducedOnOneArmAndReadAfterTheMergeIsRefused) {
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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->nodeId == add);
    ASSERT_TRUE(diagnostic->pinId == "arg:aiCount");
    ASSERT_TRUE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST(GraphFlow, AnOperatorBetweenTheCallAndTheUseDoesNotHideIt) {
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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->nodeId == add);
    ASSERT_TRUE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST(GraphFlow, AValueProducedInsideALoopAndReadAfterItIsRefused) {
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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->nodeId == add);
    ASSERT_TRUE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), count) !=
            diagnostic->relatedNodes.end());
}

TEST(GraphFlow, ALoopConditionFedFromTheLoopBodyIsRefusedByName) {
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
    ASSERT_TRUE(diagnostic != nullptr);
    ASSERT_TRUE(diagnostic->nodeId == loop);
    ASSERT_TRUE(diagnostic->pinId == pinIds::kCondition);
    ASSERT_TRUE(std::find(diagnostic->relatedNodes.begin(), diagnostic->relatedNodes.end(), enabled) !=
            diagnostic->relatedNodes.end());
}

TEST(GraphFlow, AValueProducedBeforeTheBranchIsReadableAfterTheMerge) {
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

    ASSERT_TRUE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    ASSERT_TRUE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}

TEST(GraphFlow, AValueProducedAndReadOnTheSameArmIsAccepted) {
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

    ASSERT_TRUE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    ASSERT_TRUE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}

TEST(GraphFlow, AValueProducedEarlierInALoopBodyIsReadableLaterInIt) {
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

    ASSERT_TRUE(findByCode(problems, codes::kUseBeforeAssignment) == nullptr);
    ASSERT_TRUE(findByCode(problems, codes::kLoopConditionFromLoopBody) == nullptr);
}
