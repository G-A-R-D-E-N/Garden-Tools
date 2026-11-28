#include <gtest/gtest.h>

#include <string>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

GraphBuilder loopWithExit(Env& env, std::string_view exitKind) {
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto exitNode = graph.node(env.palette, exitKind);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, exitNode, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, loop, pinIds::kExec);
    return graph;
}

}

TEST(GraphBreakContinue, BreakMakesTheLoopConditionFalse) {
    Env env(true);
    auto graph = loopWithExit(env, builtinDefs::kBreak);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "broke = false"));
    EXPECT_TRUE(contains(source, "While (enabled && !broke)"));
    EXPECT_TRUE(contains(source, "broke = true"));
}

TEST(GraphBreakContinue, ContinueEmitsNothingOfItsOwn) {
    Env env(true);
    auto graph = loopWithExit(env, builtinDefs::kContinue);

    const auto source = sourceOf(env, graph);

    EXPECT_FALSE(contains(source, "broke"));
    EXPECT_TRUE(contains(source, "While (enabled)"));
    EXPECT_TRUE(contains(source, "Debug.Notification(\"work\")"));
}

TEST(GraphBreakContinue, ALoopNobodyBreaksOutOfGetsNoSentinel) {
    Env env(true);
    auto graph = loopWithExit(env, builtinDefs::kContinue);

    EXPECT_FALSE(contains(sourceOf(env, graph), "broke"));
}

TEST(GraphBreakContinue, TwoBreaksInOneLoopShareOneSentinel) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto outer = graph.node(env.palette, builtinDefs::kBranch);
    const auto firstBreak = graph.node(env.palette, builtinDefs::kBreak);
    const auto inner = graph.node(env.palette, builtinDefs::kBranch);
    const auto secondBreak = graph.node(env.palette, builtinDefs::kBreak);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, outer, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, outer, pinIds::kCondition);
    graph.wire(outer, pinIds::kThen, firstBreak, pinIds::kExec);
    graph.wire(outer, pinIds::kElse, inner, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, inner, pinIds::kCondition);
    graph.wire(inner, pinIds::kThen, secondBreak, pinIds::kExec);
    graph.wire(inner, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, loop, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_EQ(occurrences(source, "bool broke"), 1);
    EXPECT_EQ(occurrences(source, "broke = true"), 2);
}

TEST(GraphBreakContinue, ABreakInTheInnerLoopLeavesOnlyTheInnerLoop) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto outer = graph.node(env.palette, builtinDefs::kWhile);
    const auto inner = graph.node(env.palette, builtinDefs::kWhile);
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto leave = graph.node(env.palette, builtinDefs::kBreak);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, outer, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, outer, pinIds::kCondition);
    graph.wire(outer, pinIds::kBody, inner, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, inner, pinIds::kCondition);
    graph.wire(inner, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, leave, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, inner, pinIds::kExec);
    graph.wire(inner, pinIds::kCompleted, outer, pinIds::kExec);

    const auto source = sourceOf(env, graph);
    const auto lines = trimmedLines(source);
    const auto outerLine = indexOfLine(lines, "While (enabled)");
    const auto innerLine = indexOfLine(lines, "While (enabled && !broke)");
    const auto resetLine = indexOfLine(lines, "broke = false");

    EXPECT_GT(outerLine, -1);
    EXPECT_GT(innerLine, outerLine);
    EXPECT_GE(resetLine, outerLine + 1);
    EXPECT_LE(resetLine, innerLine - 1);
}

TEST(GraphBreakContinue, ABreakInAForEachGuardsTheBoundsCheck) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Items", "Form[]", true);

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kForEach);
    const auto items = graph.node(env.palette, builtinDefs::kVariableGet, {{"name", "Items"}});
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto leave = graph.node(env.palette, builtinDefs::kBreak);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(items, pinIds::kValue, loop, pinIds::kArray);
    graph.wire(loop, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, leave, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "While (index < items.Length && !broke)"));
    EXPECT_TRUE(contains(source, "index = index + 1"));
}

TEST(GraphBreakContinue, ABreakDoesNotCountAsLeavingTheFunction) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Count"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto leave = graph.node(env.palette, builtinDefs::kBreak);
    const auto work = graph.node(env.palette, "global:Debug.Notification");
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto zero = graph.node(env.palette, "literal.int");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");
    graph.value(zero, pinIds::kValue, "int", "0");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, leave, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kCompleted, ret, pinIds::kExec);
    graph.wire(zero, pinIds::kValue, ret, pinIds::kValue);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kNotAllPathsReturn)) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
}

TEST(GraphBreakContinue, ALoopWhoseOnlyExitIsABreakStillHasToReturn) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Count"}, {"returns", "int"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto leave = graph.node(env.palette, builtinDefs::kBreak);

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, leave, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    EXPECT_TRUE(firstError(result.diagnostics, codes::kNotAllPathsReturn) != nullptr)
        << describe(result.diagnostics);
}

TEST(GraphBreakContinue, ALoopExitWithNoLoopIsRefusedNamingTheNode) {
    for (const auto kind : {builtinDefs::kBreak, builtinDefs::kContinue}) {
        SCOPED_TRACE(std::string(kind));
        Env env(true);
        GraphBuilder graph("Fixture", "ObjectReference");

        const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
        const auto exitNode = graph.node(env.palette, kind);
        graph.wire(entry, pinIds::kExec, exitNode, pinIds::kExec);

        const auto result = compileDoc(env, graph);

        EXPECT_FALSE(result.success());
        const auto* diagnostic = firstError(result.diagnostics, codes::kLoopExitOutsideLoop);
        ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
        EXPECT_EQ(diagnostic->nodeId.value_or(""), exitNode);
    }
}

TEST(GraphBreakContinue, AnEmptyThenArmIsInvertedRatherThanLeftEmpty) {
    Env env(true);
    auto graph = loopWithExit(env, builtinDefs::kContinue);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "If (!enabled)"));
    EXPECT_FALSE(contains(source, "Else"));
}
