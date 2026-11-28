#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/papyrus/decompiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

struct TwoStates {
    GraphBuilder graph{"Fixture", "ObjectReference"};
    std::string idle;
    std::string busy;

    explicit TwoStates(Env& env) {
        idle = graph.node(env.palette, "event:ObjectReference.OnLoad");
        const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
        graph.wire(idle, pinIds::kExec, disable, pinIds::kExec);

        busy = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "Busy"}});
        const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
        graph.wire(busy, pinIds::kExec, enable, pinIds::kExec);
    }
};

const GraphDiagnostic* refused(const GraphCompileResult& result, std::string_view code) {
    EXPECT_FALSE(result.success()) << "this graph should not compile";
    const auto* match = firstError(result.diagnostics, code);
    EXPECT_TRUE(match != nullptr) << "expected " << code << "; got " << describe(result.diagnostics);
    return match;
}

}

TEST(GraphState, AHandlerCarryingAStateIsEmittedInsideThatState) {
    Env env(true);
    TwoStates states(env);

    const auto source = sourceOf(env, states.graph);

    EXPECT_TRUE(contains(source, "State Busy"));
    EXPECT_TRUE(contains(source, "EndState"));

    const auto stateStart = source.find("State Busy");
    const auto stateEnd = source.find("EndState");
    ASSERT_NE(stateStart, std::string::npos);
    ASSERT_NE(stateEnd, std::string::npos);
    ASSERT_LT(stateStart, stateEnd);
    const auto inside = source.substr(stateStart, stateEnd - stateStart);
    EXPECT_TRUE(contains(inside, "Enable()"));
    EXPECT_FALSE(contains(inside, "Disable()"));
}

TEST(GraphState, TheStartingStateIsWrittenAsAnAutoState) {
    Env env(true);
    TwoStates states(env);
    states.graph.autoState("Busy");

    EXPECT_TRUE(contains(sourceOf(env, states.graph), "Auto State Busy"));
}

TEST(GraphState, OnlyTheStartingStateIsWrittenAsAuto) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.autoState("Waiting");

    const auto waiting = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "Waiting"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(waiting, pinIds::kExec, disable, pinIds::kExec);

    const auto busy = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "Busy"}});
    const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
    graph.wire(busy, pinIds::kExec, enable, pinIds::kExec);

    const auto source = sourceOf(env, graph);

    EXPECT_TRUE(contains(source, "Auto State Waiting"));
    EXPECT_TRUE(contains(source, "State Busy"));
    EXPECT_FALSE(contains(source, "Auto State Busy"));
}

TEST(GraphState, AStartingStateNothingDeclaresIsRefusedNamingIt) {
    Env env(true);
    TwoStates states(env);
    states.graph.autoState("Missing");

    const auto result = compileDoc(env, states.graph);
    const auto* diagnostic = refused(result, codes::kInvalidScriptHeader);

    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_TRUE(contains(diagnostic->message, "Missing"));
}

TEST(GraphState, AStateNameThatIsNotAnIdentifierIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "not a name"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto result = compileDoc(env, graph);
    const auto* diagnostic = refused(result, codes::kInvalidScriptHeader);

    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), entry);
}

TEST(GraphState, AGlobalFunctionCannotBePlacedInAStateAndTheEntryIsNamed) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Helper"}, {"global", "true"}, {"state", "Busy"}});
    const auto notify = graph.node(env.palette, "global:Debug.Notification");

    graph.value(notify, "arg:asNotificationText", "string", "\"hi\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = compileDoc(env, graph);
    const auto* diagnostic = refused(result, codes::kInvalidScriptHeader);

    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), entry);
    EXPECT_TRUE(contains(diagnostic->message, "global"));
}

TEST(GraphState, TheSameEventInTwoDifferentStatesIsNotADuplicate) {
    Env env(true);
    TwoStates states(env);

    const auto result = compileDoc(env, states.graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kDuplicateDeclaration)) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
}

TEST(GraphState, TheSameEventTwiceInOneStateIsStillADuplicate) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto first = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "Busy"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(first, pinIds::kExec, disable, pinIds::kExec);

    const auto second = graph.node(env.palette, "event:ObjectReference.OnLoad", {{"state", "Busy"}});
    const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
    graph.wire(second, pinIds::kExec, enable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_TRUE(refused(result, codes::kDuplicateDeclaration) != nullptr);
}

TEST(GraphState, SiblingEntriesDoNotMakeEachOtherLookUnreachable) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto load = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(load, pinIds::kExec, disable, pinIds::kExec);

    const auto activate = graph.node(env.palette, "event:ObjectReference.OnActivate");
    const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
    graph.wire(activate, pinIds::kExec, enable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kUnreachableExec)) << describe(result.diagnostics);
}

TEST(GraphState, ANodeNoEntryReachesIsStillReported) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto load = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(load, pinIds::kExec, disable, pinIds::kExec);

    const auto stranded = graph.node(env.palette, "call:ObjectReference.Enable");
    const auto alsoStranded = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(stranded, pinIds::kThen, alsoStranded, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    const bool reported = std::any_of(
        result.diagnostics.begin(), result.diagnostics.end(), [&](const GraphDiagnostic& d) {
            return d.code == codes::kUnreachableExec && d.nodeId.value_or("") == alsoStranded;
        });
    EXPECT_TRUE(reported) << describe(result.diagnostics);
}

TEST(GraphRemoteEvent, ARemoteHandlerIsWrittenWithTheRaisingTypeAndASenderParameter) {
    Env env(true);
    GraphBuilder graph("Fixture", "Quest");

    const auto entry = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"loaded\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    EXPECT_TRUE(contains(sourceOf(env, graph), "Event ObjectReference.OnLoad(ObjectReference akSender)"));
}

TEST(GraphRemoteEvent, ARemoteHandlerKeepsTheSourceEventsOwnParametersAfterTheSender) {
    Env env(true);
    GraphBuilder graph("Fixture", "Quest");

    const auto entry = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnActivate"));
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"used\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    EXPECT_TRUE(contains(
        sourceOf(env, graph),
        "Event ObjectReference.OnActivate(ObjectReference akSender, ObjectReference akActionRef)"));
}

TEST(GraphRemoteEvent, ACustomEventHandlerTakesTheSenderAndAVarArray) {
    Env env(true);
    GraphBuilder graph("Fixture", "Quest");

    const auto entry =
        graph.node(env.palette, NodePalette::remoteEventId("FixtureEventSource", "AffinityChanged"));
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"affinity\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    EXPECT_TRUE(contains(
        sourceOf(env, graph),
        "Event FixtureEventSource.AffinityChanged(FixtureEventSource akSender, Var[] akArgs)"));
}

TEST(GraphRemoteEvent, TheSenderAndThePayloadAreReadableAsParameters) {
    Env env(true);
    GraphBuilder graph("Fixture", "Quest");

    const auto entry = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    const auto distance = graph.node(env.palette, "call:ObjectReference.GetDistance");
    const auto sender = pinIds::parameter(std::string(NodePalette::kRemoteSenderName));

    graph.wire(entry, pinIds::kExec, distance, pinIds::kExec);
    graph.wire(entry, sender, distance, pinIds::kSelf);
    graph.wire(entry, sender, distance, "arg:akOther");

    EXPECT_TRUE(contains(sourceOf(env, graph), "akSender.GetDistance(akSender)"));
}

TEST(GraphRemoteEvent, ADeclaredCustomEventIsEmitted) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.customEvent("Ready");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    EXPECT_TRUE(contains(sourceOf(env, graph), "CustomEvent Ready"));
}

TEST(GraphRemoteEvent, ACustomEventNameThatIsNotAnIdentifierIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.customEvent("not a name");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    EXPECT_TRUE(firstError(result.diagnostics, codes::kInvalidScriptHeader) != nullptr)
        << describe(result.diagnostics);
}

TEST(GraphRemoteEvent, TheSameCustomEventDeclaredTwiceIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.customEvent("Ready");
    graph.customEvent("Ready");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    EXPECT_TRUE(firstError(result.diagnostics, codes::kDuplicateDeclaration) != nullptr)
        << describe(result.diagnostics);
}

TEST(GraphRemoteEvent, ALocalOverrideAndARemoteHandlerOfTheSameEventCoexist) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto local = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(local, pinIds::kExec, disable, pinIds::kExec);

    const auto remote = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
    graph.wire(remote, pinIds::kExec, enable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(hasCode(result.diagnostics, codes::kDuplicateDeclaration)) << describe(result.diagnostics);
    EXPECT_TRUE(result.success()) << describe(result.diagnostics);
}

TEST(GraphRemoteEvent, TheSameRemoteHandlerDeclaredTwiceIsStillRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto first = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(first, pinIds::kExec, disable, pinIds::kExec);

    const auto second = graph.node(env.palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    const auto enable = graph.node(env.palette, "call:ObjectReference.Enable");
    graph.wire(second, pinIds::kExec, enable, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_TRUE(firstError(result.diagnostics, codes::kDuplicateDeclaration) != nullptr)
        << describe(result.diagnostics);
}

TEST(GraphRemoteEvent, ThePaletteShowsTheSignatureTheNodeActuallyEmits) {
    Env env(true);

    const auto signatureOf = [&](std::string_view query, const std::string& id) {
        const auto found = env.palette.search(query, 20);
        for (const auto& entry : found.entries) {
            if (entry.id == id) return std::optional<std::string>(entry.signature);
        }
        return std::optional<std::string>();
    };

    const auto withParameters = signatureOf(
        "ObjectReference.OnActivate", NodePalette::remoteEventId("ObjectReference", "OnActivate"));
    ASSERT_TRUE(withParameters.has_value());
    EXPECT_EQ(*withParameters,
              "Event ObjectReference.OnActivate(ObjectReference akSender, ObjectReference akActionRef)");

    const auto withoutParameters =
        signatureOf("ObjectReference.OnLoad", NodePalette::remoteEventId("ObjectReference", "OnLoad"));
    ASSERT_TRUE(withoutParameters.has_value());
    EXPECT_EQ(*withoutParameters, "Event ObjectReference.OnLoad(ObjectReference akSender)");

    const auto custom = signatureOf("FixtureEventSource.AffinityChanged",
                                    NodePalette::remoteEventId("FixtureEventSource", "AffinityChanged"));
    ASSERT_TRUE(custom.has_value());
    EXPECT_EQ(*custom,
              "Event FixtureEventSource.AffinityChanged(FixtureEventSource akSender, Var[] akArgs)");
}

TEST(GraphRemoteEvent, ACustomEventHandlerDecompilesBackToItsDottedName) {
    Env env(true);
    GraphBuilder graph("Fixture", "Quest");

    const auto entry =
        graph.node(env.palette, NodePalette::remoteEventId("FixtureEventSource", "AffinityChanged"));
    const auto notify = graph.node(env.palette, "global:Debug.Notification");
    graph.value(notify, "arg:asNotificationText", "string", "\"affinity\"");
    graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);

    const auto result = compileDoc(env, graph);
    ASSERT_TRUE(result.success()) << describe(result.diagnostics);

    const auto text = fo4::papyrus::Decompiler::toSource(*result.pex);

    EXPECT_TRUE(contains(text, "FixtureEventSource.AffinityChanged")) << text;
    EXPECT_FALSE(contains(text, "FixtureEventSource_AffinityChanged")) << text;
}
