#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

bool related(const GraphDiagnostic& diagnostic, const std::string& node) {
    return std::find(diagnostic.relatedNodes.begin(), diagnostic.relatedNodes.end(), node) !=
           diagnostic.relatedNodes.end();
}

GraphDiagnostic refusedForFallingOff(Env& env, GraphBuilder& graph) {
    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success()) << "control can leave this function without a Return";
    const auto* match = firstError(result.diagnostics, codes::kNotAllPathsReturn);
    EXPECT_TRUE(match != nullptr) << describe(result.diagnostics);
    return match != nullptr ? *match : GraphDiagnostic{};
}

void accepted(Env& env, GraphBuilder& graph) {
    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kNotAllPathsReturn)) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
}

}

TEST(GraphReturnFlow, AReturningFunctionWithAnEmptyBodyIsRefusedNamingTheEntry) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});

    const auto diagnostic = refusedForFallingOff(env, graph);

    EXPECT_EQ(diagnostic.nodeId.value_or(""), entry);
    EXPECT_TRUE(related(diagnostic, entry));
}

TEST(GraphReturnFlow, AReturningFunctionThatRunsOffTheEndOfAStraightLineIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");

    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto diagnostic = refusedForFallingOff(env, graph);

    EXPECT_EQ(diagnostic.nodeId.value_or(""), entry);
    EXPECT_TRUE(related(diagnostic, disable));
}

TEST(GraphReturnFlow, ABranchThatReturnsOnOnlyOneArmIsRefusedNamingTheBranch) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto yes = graph.node(env.palette, builtinDefs::kReturn);
    const auto one = graph.node(env.palette, "literal.int");

    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);
    graph.wire(one, pinIds::kValue, yes, pinIds::kValue);

    const auto diagnostic = refusedForFallingOff(env, graph);

    EXPECT_EQ(diagnostic.nodeId.value_or(""), entry);
    EXPECT_TRUE(related(diagnostic, branch));
    EXPECT_FALSE(related(diagnostic, yes));
}

TEST(GraphReturnFlow, ALoopWhoseBodyAlwaysReturnsStillHasToReturnAfterTheLoop) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto one = graph.node(env.palette, "literal.int");

    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, ret, pinIds::kExec);
    graph.wire(one, pinIds::kValue, ret, pinIds::kValue);

    const auto diagnostic = refusedForFallingOff(env, graph);

    EXPECT_TRUE(related(diagnostic, loop));
}

TEST(GraphReturnFlow, ALoopFollowedByAReturnIsAccepted) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto one = graph.node(env.palette, "literal.int");

    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, disable, pinIds::kExec);
    graph.wire(disable, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kCompleted, ret, pinIds::kExec);
    graph.wire(one, pinIds::kValue, ret, pinIds::kValue);

    accepted(env, graph);
}

TEST(GraphReturnFlow, NestedBranchesThatAllReachAReturnAreAccepted) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Score"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto outer = graph.node(env.palette, builtinDefs::kBranch);
    const auto inner = graph.node(env.palette, builtinDefs::kBranch);
    const auto first = graph.node(env.palette, builtinDefs::kReturn);
    const auto second = graph.node(env.palette, builtinDefs::kReturn);
    const auto third = graph.node(env.palette, builtinDefs::kReturn);
    const auto one = graph.node(env.palette, "literal.int");
    const auto two = graph.node(env.palette, "literal.int");
    const auto three = graph.node(env.palette, "literal.int");

    graph.value(one, pinIds::kValue, "int", "1");
    graph.value(two, pinIds::kValue, "int", "2");
    graph.value(three, pinIds::kValue, "int", "3");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, outer, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, outer, pinIds::kCondition);
    graph.wire(outer, pinIds::kThen, inner, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, inner, pinIds::kCondition);
    graph.wire(inner, pinIds::kThen, first, pinIds::kExec);
    graph.wire(inner, pinIds::kElse, second, pinIds::kExec);
    graph.wire(outer, pinIds::kElse, third, pinIds::kExec);
    graph.wire(one, pinIds::kValue, first, pinIds::kValue);
    graph.wire(two, pinIds::kValue, second, pinIds::kValue);
    graph.wire(three, pinIds::kValue, third, pinIds::kValue);

    accepted(env, graph);
}

TEST(GraphReturnFlow, AVoidFunctionThatFallsOffTheEndIsFine) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    accepted(env, graph);
}

TEST(GraphReturnFlow, AnEventIsNeverAskedToReturnAValue) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    accepted(env, graph);
}
