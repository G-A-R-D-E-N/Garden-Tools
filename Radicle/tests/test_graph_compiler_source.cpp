#include <gtest/gtest.h>

#include <string>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

GraphCompileResult compileOk(Env& env, GraphBuilder& graph) {
    auto result = compileDoc(env, graph);
    EXPECT_TRUE(result.errors().empty()) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
    return result;
}

}

TEST(GraphCompilerSource, AnEventCallingAGlobalCompilesToPex) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "Scriptname Fixture extends ObjectReference"));
    EXPECT_TRUE(contains(source, "Event OnActivate(ObjectReference akActionRef)"));
    EXPECT_TRUE(contains(source, "Debug.Notification(\"opened\")"));
    EXPECT_TRUE(contains(source, "EndEvent"));
    ASSERT_TRUE(result.pex != nullptr);
    ASSERT_FALSE(result.pex->objects.empty());
    EXPECT_EQ(result.pex->objects[0].parentClassName, "ObjectReference");
}

TEST(GraphCompilerSource, AnEventParameterFlowsIntoACall) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto distance = graph.node(env.palette, "call:ObjectReference.GetDistance");
    graph.wire(entry, pinIds::kExec, distance, pinIds::kExec);
    graph.wire(entry, "param:akActionRef", distance, "arg:akOther");

    const auto result = compileOk(env, graph);

    EXPECT_TRUE(contains(result.source.value_or(""), "GetDistance(akActionRef)"));
}

TEST(GraphCompilerSource, ABranchEmitsAnIfAndRejoinsOnceAfterTheMerge) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto isEnabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto yes = graph.node(env.palette, "global:Debug.Notification");
    const auto no = graph.node(env.palette, "global:Debug.Notification");
    const auto after = graph.node(env.palette, "global:Debug.Notification");

    graph.value(yes, "arg:asNotificationText", "string", "\"on\"");
    graph.value(no, "arg:asNotificationText", "string", "\"off\"");
    graph.value(after, "arg:asNotificationText", "string", "\"done\"");

    graph.wire(entry, pinIds::kExec, isEnabled, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);
    graph.wire(branch, pinIds::kElse, no, pinIds::kExec);
    graph.wire(yes, pinIds::kThen, after, pinIds::kExec);
    graph.wire(no, pinIds::kThen, after, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "If ("));
    EXPECT_TRUE(contains(source, "Else"));
    EXPECT_TRUE(contains(source, "EndIf"));
    EXPECT_EQ(occurrences(source, "\"done\""), 1);
}

TEST(GraphCompilerSource, ABranchWithNoElseEmitsNoElseClause) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto isEnabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto yes = graph.node(env.palette, "global:Debug.Notification");
    graph.value(yes, "arg:asNotificationText", "string", "\"on\"");

    graph.wire(entry, pinIds::kExec, isEnabled, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "If ("));
    EXPECT_FALSE(contains(source, "Else"));
}

TEST(GraphCompilerSource, AWhileLoopEmitsAWhileAndContinuesAfterIt) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto loop = graph.node(env.palette, builtinDefs::kWhile);
    const auto isEnabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto body = graph.node(env.palette, "global:Debug.Notification");
    const auto after = graph.node(env.palette, "global:Debug.Notification");

    graph.value(body, "arg:asNotificationText", "string", "\"tick\"");
    graph.value(after, "arg:asNotificationText", "string", "\"done\"");

    graph.wire(entry, pinIds::kExec, isEnabled, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(isEnabled, pinIds::kReturn, loop, pinIds::kCondition);
    graph.wire(loop, pinIds::kBody, body, pinIds::kExec);
    graph.wire(body, pinIds::kThen, loop, pinIds::kExec);
    graph.wire(loop, pinIds::kCompleted, after, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "While ("));
    EXPECT_TRUE(contains(source, "EndWhile"));
    EXPECT_TRUE(contains(source, "\"tick\""));
    EXPECT_TRUE(contains(source, "\"done\""));
    EXPECT_GT(source.find("\"done\""), source.find("EndWhile"));
}

TEST(GraphCompilerSource, ATrailingOptionalThatIsNotSuppliedDisappears) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");

    const auto result = compileOk(env, graph);

    EXPECT_TRUE(contains(result.source.value_or(""), "AddItem(None)"));
}

TEST(GraphCompilerSource, ASkippedMiddleOptionalForcesNamedArguments) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    const auto none = graph.node(env.palette, builtinDefs::kNoneValue);

    graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
    graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    graph.value(add, "arg:abSilent", "bool", "true");

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "abSilent = true"));
    EXPECT_FALSE(contains(source, "AddItem(None, true)"));
}

TEST(GraphCompilerSource, AnImpureCallUsedTwiceBindsOneLocalRatherThanCallingTwice) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto player = graph.node(env.palette, "global:Game.GetPlayer");
    const auto move = graph.node(env.palette, "call:ObjectReference.MoveTo");
    const auto distance = graph.node(env.palette, "call:ObjectReference.GetDistance");

    graph.wire(entry, pinIds::kExec, player, pinIds::kExec);
    graph.wire(player, pinIds::kThen, move, pinIds::kExec);
    graph.wire(move, pinIds::kThen, distance, pinIds::kExec);
    graph.wire(player, pinIds::kReturn, move, "arg:akTarget");
    graph.wire(player, pinIds::kReturn, distance, "arg:akOther");

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_EQ(occurrences(source, "Game.GetPlayer()"), 1);
    EXPECT_TRUE(contains(source, "Actor player"));
}

TEST(GraphCompilerSource, ACallWhoseResultNothingUsesEmitsNoLocal) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "IsEnabled()"));
    EXPECT_FALSE(contains(source, "bool enabled"));
}

TEST(GraphCompilerSource, AnOperatorTreeInlinesAtItsUseSite) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    const auto add = graph.node(env.palette, "op.add");
    const auto left = graph.node(env.palette, "literal.float");
    const auto right = graph.node(env.palette, "literal.float");

    graph.value(left, pinIds::kValue, "float", "1.5");
    graph.value(right, pinIds::kValue, "float", "2.5");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
    graph.wire(left, pinIds::kValue, add, pinIds::kLeft);
    graph.wire(right, pinIds::kValue, add, pinIds::kRight);
    graph.wire(add, pinIds::kReturn, wait, "arg:afSeconds");

    const auto result = compileOk(env, graph);

    EXPECT_TRUE(contains(result.source.value_or(""), "Utility.Wait(1.5 + 2.5)"));
}

TEST(GraphCompilerSource, AVariableSetAndGetRoundTripsThroughADeclaration) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Counter", "int");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto set = graph.node(env.palette, builtinDefs::kVariableSet, {{"name", "Counter"}});
    const auto literal = graph.node(env.palette, "literal.int");
    const auto trace = graph.node(env.palette, "global:Debug.Trace");
    const auto get = graph.node(env.palette, builtinDefs::kVariableGet, {{"name", "Counter"}});

    graph.value(literal, pinIds::kValue, "int", "7");
    graph.wire(entry, pinIds::kExec, set, pinIds::kExec);
    graph.wire(literal, pinIds::kValue, set, pinIds::kValue);
    graph.wire(set, pinIds::kThen, trace, pinIds::kExec);
    graph.wire(get, pinIds::kValue, trace, "arg:asTextToPrint");

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "int Counter"));
    EXPECT_TRUE(contains(source, "Counter = 7"));
    EXPECT_TRUE(contains(source, "Debug.Trace(Counter)"));
}

TEST(GraphCompilerSource, APropertyIsDeclaredAutoByDefault) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Target", "ObjectReference", true);
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto result = compileOk(env, graph);

    EXPECT_TRUE(contains(result.source.value_or(""), "ObjectReference Property Target Auto"));
}

TEST(GraphCompilerSource, ACallOnThisScriptsOwnChainNeedsNoReceiver) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto result = compileOk(env, graph);
    const auto source = result.source.value_or("");

    EXPECT_TRUE(contains(source, "Disable()"));
    EXPECT_FALSE(contains(source, ".Disable("));
}

TEST(GraphCompilerSource, ACallOnAnotherTypeTakesItsReceiverFromTheTargetPin) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto player = graph.node(env.palette, "global:Game.GetPlayer");
    const auto level = graph.node(env.palette, "call:Actor.GetLevel");
    const auto trace = graph.node(env.palette, "global:Debug.Trace");

    graph.wire(entry, pinIds::kExec, player, pinIds::kExec);
    graph.wire(player, pinIds::kThen, level, pinIds::kExec);
    graph.wire(level, pinIds::kThen, trace, pinIds::kExec);
    graph.wire(player, pinIds::kReturn, level, pinIds::kSelf);
    graph.wire(level, pinIds::kReturn, trace, "arg:asTextToPrint");

    const auto result = compileOk(env, graph);

    EXPECT_TRUE(contains(result.source.value_or(""), ".GetLevel()"));
}

TEST(GraphCompilerSource, StoppingAfterSourceYieldsSourceAndNoCompiledObject) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.node(env.palette, "event:ObjectReference.OnLoad");

    const auto result = compileDoc(env, graph, true);

    ASSERT_TRUE(result.source.has_value());
    EXPECT_FALSE(result.source->empty());
    EXPECT_TRUE(result.sourceMap.has_value());
    EXPECT_TRUE(result.pex == nullptr);
}

TEST(GraphCompilerSource, TheSourceMapAttributesEveryStatementToANode) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"hello\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = compileOk(env, graph);
    ASSERT_TRUE(result.source.has_value());
    ASSERT_TRUE(result.sourceMap.has_value());
    const auto offset = static_cast<int>(result.source->find("Debug.Notification"));
    ASSERT_GE(offset, 0);

    const auto mapped = result.sourceMap->find(offset);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->nodeId, notify);

    const auto function = result.sourceMap->functionAt(offset);
    ASSERT_TRUE(function.has_value());
    EXPECT_EQ(function->nodeId, entry);
}

TEST(GraphCompilerSource, ValidationAndCompilationNeverDisagree) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto add = graph.node(env.palette, "call:ObjectReference.AddItem");
    graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
    graph.document.invalidate();

    GraphCompiler compiler(env.index, &env.palette);
    const auto validation = compiler.validate(graph.document);
    const auto compiled = compiler.compile(graph.document);

    EXPECT_FALSE(validation.ok());
    for (const auto* error : validation.errors()) {
        EXPECT_TRUE(hasCode(compiled.diagnostics, error->code)) << error->toString();
    }
}
