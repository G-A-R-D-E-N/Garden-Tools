#include <gtest/gtest.h>

#include <string>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

TEST(GraphLocal, ADeclaredLocalIsEmittedAndAssignable) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "counter"}, {"type", "int"}});
    const auto zero = graph.node(env.palette, "literal.int");

    graph.value(zero, pinIds::kValue, "int", "0");
    graph.wire(entry, pinIds::kExec, local, pinIds::kExec);
    graph.wire(zero, pinIds::kValue, local, pinIds::kValue);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "int counter"));
    EXPECT_TRUE(contains(source, "counter = 0"));
}

TEST(GraphLocal, ADeclaredLocalNeedsNoInitialValue) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "counter"}, {"type", "int"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");

    graph.wire(entry, pinIds::kExec, local, pinIds::kExec);
    graph.wire(local, pinIds::kThen, disable, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "int counter"));
    EXPECT_FALSE(contains(source, "counter ="));
}

TEST(GraphLocal, ALocalCanBeReadAndWrittenBack) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "counter"}, {"type", "int"}});
    const auto zero = graph.node(env.palette, "literal.int");
    const auto set = graph.node(env.palette, builtinDefs::kVariableSet,
                                {{"name", "counter"}, {"type", "int"}});
    const auto get = graph.node(env.palette, builtinDefs::kVariableGet,
                                {{"name", "counter"}, {"type", "int"}});
    const auto one = graph.node(env.palette, "literal.int");
    const auto sum = graph.node(env.palette, "op.add");

    graph.value(zero, pinIds::kValue, "int", "0");
    graph.value(one, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, local, pinIds::kExec);
    graph.wire(zero, pinIds::kValue, local, pinIds::kValue);
    graph.wire(local, pinIds::kThen, set, pinIds::kExec);
    graph.wire(get, pinIds::kValue, sum, pinIds::kLeft);
    graph.wire(one, pinIds::kValue, sum, pinIds::kRight);
    graph.wire(sum, pinIds::kReturn, set, pinIds::kValue);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "int counter"));
    EXPECT_TRUE(contains(source, "counter = counter + 1"));
}

TEST(GraphLocal, ALocalSurvivesBeingDeclaredInsideALoopBody) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "each"}, {"type", "int"}});

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, local, pinIds::kExec);
    graph.wire(local, pinIds::kThen, loop, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_EQ(occurrences(source, "int each"), 1);
}

TEST(GraphLocal, AnArrayLocalKeepsItsArrayType) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "things"}, {"type", "Form[]"}});
    graph.wire(entry, pinIds::kExec, local, pinIds::kExec);

    EXPECT_TRUE(contains(sourceOf(env, graph), "Form[] things"));
}

TEST(GraphLocal, TheSameLocalDeclaredTwiceIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto first = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "counter"}, {"type", "int"}});
    const auto second = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                   {{"name", "counter"}, {"type", "int"}});

    graph.wire(entry, pinIds::kExec, first, pinIds::kExec);
    graph.wire(first, pinIds::kThen, second, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    const auto* diagnostic = firstError(result.diagnostics, codes::kDuplicateDeclaration);
    ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), second);
}

TEST(GraphLocal, TheSameNameInTwoFunctionsIsFine) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto first = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "A"}});
    const auto localA = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                   {{"name", "counter"}, {"type", "int"}});
    graph.wire(first, pinIds::kExec, localA, pinIds::kExec);

    const auto second = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "B"}});
    const auto localB = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                   {{"name", "counter"}, {"type", "int"}});
    graph.wire(second, pinIds::kExec, localB, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kDuplicateDeclaration)) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
}

TEST(GraphLoopExit, AnEarlyReturnFromInsideALoopCompiles) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto first = graph.node(env.palette, "global:Debug.Notification");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto rest = graph.node(env.palette, "global:Debug.Notification");
    const auto after = graph.node(env.palette, "global:Debug.Notification");

    graph.value(first, "arg:asNotificationText", "string", "\"A\"");
    graph.value(rest, "arg:asNotificationText", "string", "\"B\"");
    graph.value(after, "arg:asNotificationText", "string", "\"after\"");

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, first, pinIds::kExec);
    graph.wire(first, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, ret, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, rest, pinIds::kExec);
    graph.wire(rest, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kCompleted, after, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "Return"));
    EXPECT_TRUE(contains(source, "Debug.Notification(\"B\")"));
    EXPECT_EQ(occurrences(source, "While ("), 1);

    const auto endWhile = source.find("EndWhile");
    ASSERT_NE(endWhile, std::string::npos);
    EXPECT_TRUE(contains(source.substr(endWhile), "\"after\""));
    EXPECT_TRUE(contains(source.substr(0, endWhile), "\"B\""));
}

TEST(GraphLoopExit, ABranchArmWiredStraightBackToTheLoopHeaderCompiles) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"work\"");

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, loop, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_EQ(occurrences(source, "While ("), 1);
    EXPECT_TRUE(contains(source, "Debug.Notification(\"work\")"));
}

TEST(GraphLoopExit, AReturnFromANestedLoopCompiles) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto outer = graph.node(env.palette, builtinDefs::kWhile);
    const auto inner = graph.node(env.palette, builtinDefs::kWhile);
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, "arg:asNotificationText", "string", "\"inner\"");

    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, outer, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, outer, pinIds::kCondition);
    graph.wire(outer, pinIds::kBody, inner, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, inner, pinIds::kCondition);
    graph.wire(inner, pinIds::kBody, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, ret, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    graph.wire(work, pinIds::kThen, inner, pinIds::kExec);
    graph.wire(inner, pinIds::kCompleted, outer, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_EQ(occurrences(source, "While ("), 2);
    EXPECT_EQ(occurrences(source, "EndWhile"), 2);
}
