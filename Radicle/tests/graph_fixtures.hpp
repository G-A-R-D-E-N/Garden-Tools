#pragma once

#include <functional>
#include <string>
#include <vector>

#include "graph_builder.hpp"

namespace fo4::graph::test {

struct GraphFixture {
    std::string name;
    std::function<GraphDocument()> build;
    std::string reference;
};

inline std::vector<GraphFixture> allFixtures(NodePalette palette) {
    std::vector<GraphFixture> fixtures;

    auto add = [&](std::string name, std::function<void(GraphBuilder&)> build,
                   std::string reference) {
        fixtures.push_back(GraphFixture{
            std::move(name),
            [build, palette]() {
                GraphBuilder builder("Fixture", "ObjectReference");
                build(builder);
                return builder.document;
            },
            std::move(reference)});
    };

    add("01_OnActivateNotify", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnActivate");
        const auto notify = graph.node(palette, "global:Debug.Notification");
        graph.value(notify, "arg:asNotificationText", "string", "\"opened\"");
        graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnActivate(ObjectReference akActionRef)
    Debug.Notification("opened")
EndEvent
)");

    add("02_EventParameterFlows", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnActivate");
        const auto distance = graph.node(palette, "call:ObjectReference.GetDistance");
        graph.wire(entry, pinIds::kExec, distance, pinIds::kExec);
        graph.wire(entry, "param:akActionRef", distance, "arg:akOther");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnActivate(ObjectReference akActionRef)
    GetDistance(akActionRef)
EndEvent
)");

    add("03_BranchRejoins", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto yes = graph.node(palette, "global:Debug.Notification");
        const auto no = graph.node(palette, "global:Debug.Notification");
        const auto after = graph.node(palette, "global:Debug.Notification");

        graph.value(yes, "arg:asNotificationText", "string", "\"on\"");
        graph.value(no, "arg:asNotificationText", "string", "\"off\"");
        graph.value(after, "arg:asNotificationText", "string", "\"done\"");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);
        graph.wire(branch, pinIds::kElse, no, pinIds::kExec);
        graph.wire(yes, pinIds::kThen, after, pinIds::kExec);
        graph.wire(no, pinIds::kThen, after, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    enabled = IsEnabled()
    If (enabled)
        Debug.Notification("on")
    Else
        Debug.Notification("off")
    EndIf
    Debug.Notification("done")
EndEvent
)");

    add("04_BranchNoElse", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto yes = graph.node(palette, "call:ObjectReference.Disable");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    enabled = IsEnabled()
    If (enabled)
        Disable()
    EndIf
EndEvent
)");

    add("05_WhileLoop", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto loop = graph.node(palette, builtinDefs::kWhile);
        const auto body = graph.node(palette, "global:Utility.Wait");
        const auto after = graph.node(palette, "global:Debug.Notification");

        graph.value(body, "arg:afSeconds", "float", "1.0");
        graph.value(after, "arg:asNotificationText", "string", "\"done\"");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
        graph.wire(loop, pinIds::kBody, body, pinIds::kExec);
        graph.wire(body, pinIds::kThen, loop, pinIds::kExec);
        graph.wire(loop, pinIds::kCompleted, after, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    enabled = IsEnabled()
    While (enabled)
        Utility.Wait(1.0)
    EndWhile
    Debug.Notification("done")
EndEvent
)");

    add("06_TrailingOptionalOmitted", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto add = graph.node(palette, "call:ObjectReference.AddItem");
        const auto none = graph.node(palette, builtinDefs::kNoneValue);
        graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
        graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    AddItem(None)
EndEvent
)");

    add("07_NamedArgumentAfterSkip", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto add = graph.node(palette, "call:ObjectReference.AddItem");
        const auto none = graph.node(palette, builtinDefs::kNoneValue);
        graph.wire(entry, pinIds::kExec, add, pinIds::kExec);
        graph.wire(none, pinIds::kValue, add, "arg:akItemToAdd");
        graph.value(add, "arg:abSilent", "bool", "true");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    AddItem(None, abSilent = true)
EndEvent
)");

    add("08_SharedCallBindsOneLocal", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto player = graph.node(palette, "global:Game.GetPlayer");
        const auto move = graph.node(palette, "call:ObjectReference.MoveTo");
        const auto distance = graph.node(palette, "call:ObjectReference.GetDistance");

        graph.wire(entry, pinIds::kExec, player, pinIds::kExec);
        graph.wire(player, pinIds::kThen, move, pinIds::kExec);
        graph.wire(move, pinIds::kThen, distance, pinIds::kExec);
        graph.wire(player, pinIds::kReturn, move, "arg:akTarget");
        graph.wire(player, pinIds::kReturn, distance, "arg:akOther");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Actor player
    player = Game.GetPlayer()
    MoveTo(player)
    GetDistance(player)
EndEvent
)");

    add("09_ArithmeticInlines", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto wait = graph.node(palette, "global:Utility.Wait");
        const auto add = graph.node(palette, "op.add");
        const auto left = graph.node(palette, "literal.float");
        const auto right = graph.node(palette, "literal.float");

        graph.value(left, pinIds::kValue, "float", "1.5");
        graph.value(right, pinIds::kValue, "float", "2.5");
        graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
        graph.wire(left, pinIds::kValue, add, pinIds::kLeft);
        graph.wire(right, pinIds::kValue, add, pinIds::kRight);
        graph.wire(add, pinIds::kReturn, wait, "arg:afSeconds");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Utility.Wait(1.5 + 2.5)
EndEvent
)");

    add("10_ComparisonBranch", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto count = graph.node(palette, "call:ObjectReference.GetItemCount");
        const auto compare = graph.node(palette, "op.gt");
        const auto zero = graph.node(palette, "literal.int");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto notify = graph.node(palette, "global:Debug.Notification");

        graph.value(zero, pinIds::kValue, "int", "0");
        graph.value(notify, "arg:asNotificationText", "string", "\"has items\"");
        graph.wire(entry, pinIds::kExec, count, pinIds::kExec);
        graph.wire(count, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(count, pinIds::kReturn, compare, pinIds::kLeft);
        graph.wire(zero, pinIds::kValue, compare, pinIds::kRight);
        graph.wire(compare, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, notify, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    int itemCount
    itemCount = GetItemCount(None)
    If (itemCount > 0)
        Debug.Notification("has items")
    EndIf
EndEvent
)");

    add("11_VariableRoundTrip", [palette](GraphBuilder& graph) mutable {
        graph.variable("Counter", "int");
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto set = graph.node(palette, builtinDefs::kVariableSet, {{"name", "Counter"}});
        const auto literal = graph.node(palette, "literal.int");
        const auto trace = graph.node(palette, "global:Debug.Trace");
        const auto get = graph.node(palette, builtinDefs::kVariableGet, {{"name", "Counter"}});

        graph.value(literal, pinIds::kValue, "int", "7");
        graph.wire(entry, pinIds::kExec, set, pinIds::kExec);
        graph.wire(literal, pinIds::kValue, set, pinIds::kValue);
        graph.wire(set, pinIds::kThen, trace, pinIds::kExec);
        graph.wire(get, pinIds::kValue, trace, "arg:asTextToPrint");
    }, R"(Scriptname Fixture extends ObjectReference

int Counter

Event OnLoad()
    Counter = 7
    Debug.Trace(Counter)
EndEvent
)");

    add("12_AutoProperty", [palette](GraphBuilder& graph) mutable {
        graph.variable("Target", "ObjectReference", true);
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto notify = graph.node(palette, "global:Debug.Notification");
        graph.value(notify, "arg:asNotificationText", "string", "\"ready\"");
        graph.wire(entry, pinIds::kExec, notify, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

ObjectReference Property Target Auto

Event OnLoad()
    Debug.Notification("ready")
EndEvent
)");

    add("13_ReceiverFromPin", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto player = graph.node(palette, "global:Game.GetPlayer");
        const auto level = graph.node(palette, "call:Actor.GetLevel");
        const auto trace = graph.node(palette, "global:Debug.Trace");

        graph.wire(entry, pinIds::kExec, player, pinIds::kExec);
        graph.wire(player, pinIds::kThen, level, pinIds::kExec);
        graph.wire(level, pinIds::kThen, trace, pinIds::kExec);
        graph.wire(player, pinIds::kReturn, level, pinIds::kSelf);
        graph.wire(level, pinIds::kReturn, trace, "arg:asTextToPrint");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Actor player
    int level
    player = Game.GetPlayer()
    level = player.GetLevel()
    Debug.Trace(level)
EndEvent
)");

    add("14_InheritedCallNoReceiver", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto disable = graph.node(palette, "call:ObjectReference.Disable");
        graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Disable()
EndEvent
)");

    add("15_ExplicitCast", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnActivate");
        const auto cast = graph.node(palette, builtinDefs::kCast, {{"type", "Actor"}});
        const auto level = graph.node(palette, "call:Actor.GetLevel");
        const auto trace = graph.node(palette, "global:Debug.Trace");

        graph.wire(entry, pinIds::kExec, level, pinIds::kExec);
        graph.wire(entry, "param:akActionRef", cast, pinIds::kValue);
        graph.wire(cast, pinIds::kReturn, level, pinIds::kSelf);
        graph.wire(level, pinIds::kThen, trace, pinIds::kExec);
        graph.wire(level, pinIds::kReturn, trace, "arg:asTextToPrint");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnActivate(ObjectReference akActionRef)
    int level
    level = (akActionRef as Actor).GetLevel()
    Debug.Trace(level)
EndEvent
)");

    add("16_NotOperator", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto notOp = graph.node(palette, "op.not");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto enable = graph.node(palette, "call:ObjectReference.Enable");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, notOp, pinIds::kValue);
        graph.wire(notOp, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, enable, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    enabled = IsEnabled()
    If (!enabled)
        Enable()
    EndIf
EndEvent
)");

    add("17_ShortCircuit", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto count = graph.node(palette, "call:ObjectReference.GetItemCount");
        const auto compare = graph.node(palette, "op.gt");
        const auto zero = graph.node(palette, "literal.int");
        const auto andOp = graph.node(palette, "op.and");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto disable = graph.node(palette, "call:ObjectReference.Disable");

        graph.value(zero, pinIds::kValue, "int", "0");
        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, count, pinIds::kExec);
        graph.wire(count, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(count, pinIds::kReturn, compare, pinIds::kLeft);
        graph.wire(zero, pinIds::kValue, compare, pinIds::kRight);
        graph.wire(enabled, pinIds::kReturn, andOp, pinIds::kLeft);
        graph.wire(compare, pinIds::kReturn, andOp, pinIds::kRight);
        graph.wire(andOp, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, disable, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    int itemCount
    enabled = IsEnabled()
    itemCount = GetItemCount(None)
    If (enabled && itemCount > 0)
        Disable()
    EndIf
EndEvent
)");

    add("18_FunctionReturnsOnBothArms", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, builtinDefs::kFunctionEntry,
                                     {{"name", "Score"}, {"returns", "int"}});
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto yes = graph.node(palette, builtinDefs::kReturn);
        const auto no = graph.node(palette, builtinDefs::kReturn);
        const auto one = graph.node(palette, "literal.int");
        const auto zero = graph.node(palette, "literal.int");

        graph.value(one, pinIds::kValue, "int", "1");
        graph.value(zero, pinIds::kValue, "int", "0");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, yes, pinIds::kExec);
        graph.wire(branch, pinIds::kElse, no, pinIds::kExec);
        graph.wire(one, pinIds::kValue, yes, pinIds::kValue);
        graph.wire(zero, pinIds::kValue, no, pinIds::kValue);
    }, R"(Scriptname Fixture extends ObjectReference

int Function Score()
    bool enabled
    enabled = IsEnabled()
    If (enabled)
        Return 1
    Else
        Return 0
    EndIf
EndFunction
)");

    add("19_VoidFunction", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, builtinDefs::kFunctionEntry, {{"name", "Reset"}});
        const auto disable = graph.node(palette, "call:ObjectReference.Disable");
        const auto enable = graph.node(palette, "call:ObjectReference.Enable");
        graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);
        graph.wire(disable, pinIds::kThen, enable, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Function Reset()
    Disable()
    Enable()
EndFunction
)");

    add("20_TwoEntries", [palette](GraphBuilder& graph) mutable {
        const auto load = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto unload = graph.node(palette, "event:ObjectReference.OnUnload");
        const auto first = graph.node(palette, "global:Debug.Notification");
        const auto second = graph.node(palette, "global:Debug.Notification");

        graph.value(first, "arg:asNotificationText", "string", "\"in\"");
        graph.value(second, "arg:asNotificationText", "string", "\"out\"");
        graph.wire(load, pinIds::kExec, first, pinIds::kExec);
        graph.wire(unload, pinIds::kExec, second, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Debug.Notification("in")
EndEvent

Event OnUnload()
    Debug.Notification("out")
EndEvent
)");

    add("21_SelfAsArgument", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto self = graph.node(palette, builtinDefs::kSelf);
        const auto activate = graph.node(palette, "call:ObjectReference.Activate");
        graph.wire(entry, pinIds::kExec, activate, pinIds::kExec);
        graph.wire(self, pinIds::kValue, activate, "arg:akActivator");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Activate(Self)
EndEvent
)");

    add("22_GlobalUtilityScript", [palette](GraphBuilder& graph) mutable {
        graph = GraphBuilder("Fixture");
        const auto entry = graph.node(palette, builtinDefs::kFunctionEntry,
                                      {{"name", "Twice"}, {"returns", "int"}, {"global", "true"}});
        const auto ret = graph.node(palette, builtinDefs::kReturn);
        const auto literal = graph.node(palette, "literal.int");

        graph.value(literal, pinIds::kValue, "int", "84");
        graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);
        graph.wire(literal, pinIds::kValue, ret, pinIds::kValue);
    }, R"(Scriptname Fixture

int Function Twice() global
    Return 84
EndFunction
)");

    add("23_NestedBranch", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto outer = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto inner = graph.node(palette, "call:ObjectReference.GetItemCount");
        const auto innerCompare = graph.node(palette, "op.gt");
        const auto zero = graph.node(palette, "literal.int");
        const auto innerBranch = graph.node(palette, builtinDefs::kBranch);
        const auto deep = graph.node(palette, "call:ObjectReference.Disable");

        graph.value(zero, pinIds::kValue, "int", "0");
        graph.wire(entry, pinIds::kExec, outer, pinIds::kExec);
        graph.wire(outer, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(outer, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, inner, pinIds::kExec);
        graph.wire(inner, pinIds::kThen, innerBranch, pinIds::kExec);
        graph.wire(inner, pinIds::kReturn, innerCompare, pinIds::kLeft);
        graph.wire(zero, pinIds::kValue, innerCompare, pinIds::kRight);
        graph.wire(innerCompare, pinIds::kReturn, innerBranch, pinIds::kCondition);
        graph.wire(innerBranch, pinIds::kThen, deep, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    int itemCount
    enabled = IsEnabled()
    If (enabled)
        itemCount = GetItemCount(None)
        If (itemCount > 0)
            Disable()
        EndIf
    EndIf
EndEvent
)");

    add("24_HexFormId", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto getForm = graph.node(palette, "global:Game.GetFormFromFile");
        const auto add = graph.node(palette, "call:ObjectReference.AddItem");

        graph.value(getForm, "arg:aiFormID", "int", "0x0000000F");
        graph.value(getForm, "arg:asFilename", "string", "\"Fallout4.esm\"");
        graph.wire(entry, pinIds::kExec, getForm, pinIds::kExec);
        graph.wire(getForm, pinIds::kThen, add, pinIds::kExec);
        graph.wire(getForm, pinIds::kReturn, add, "arg:akItemToAdd");
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Form formFromFile
    formFromFile = Game.GetFormFromFile(0x0000000F, "Fallout4.esm")
    AddItem(formFromFile)
EndEvent
)");

    add("25_AutoStateMachine", [palette](GraphBuilder& graph) mutable {
        graph.autoState("Waiting");

        const auto waiting = graph.node(palette, "event:ObjectReference.OnActivate",
                                        {{"state", "Waiting"}});
        const auto goTo = graph.node(palette, "call:ScriptObject.GotoState");
        const auto starting = graph.node(palette, "global:Debug.Notification");

        graph.value(goTo, "arg:asNewState", "string", "\"Busy\"");
        graph.value(starting, "arg:asNotificationText", "string", "\"starting\"");
        graph.wire(waiting, pinIds::kExec, goTo, pinIds::kExec);
        graph.wire(goTo, pinIds::kThen, starting, pinIds::kExec);

        const auto busy = graph.node(palette, "event:ObjectReference.OnActivate",
                                     {{"state", "Busy"}});
        const auto already = graph.node(palette, "global:Debug.Notification");

        graph.value(already, "arg:asNotificationText", "string", "\"busy\"");
        graph.wire(busy, pinIds::kExec, already, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Auto State Waiting
    Event OnActivate(ObjectReference akActionRef)
        GotoState("Busy")
        Debug.Notification("starting")
    EndEvent
EndState

State Busy
    Event OnActivate(ObjectReference akActionRef)
        Debug.Notification("busy")
    EndEvent
EndState
)");

    add("26_RemoteAndCustomEvents", [palette](GraphBuilder& graph) mutable {
        graph.customEvent("Ready");

        const auto remote = graph.node(palette, NodePalette::remoteEventId("ObjectReference", "OnLoad"));
        const auto loaded = graph.node(palette, "global:Debug.Notification");
        graph.value(loaded, "arg:asNotificationText", "string", "\"loaded\"");
        graph.wire(remote, pinIds::kExec, loaded, pinIds::kExec);

        const auto custom = graph.node(palette,
                                       NodePalette::remoteEventId("FixtureEventSource", "AffinityChanged"));
        const auto affinity = graph.node(palette, "global:Debug.Notification");
        graph.value(affinity, "arg:asNotificationText", "string", "\"affinity\"");
        graph.wire(custom, pinIds::kExec, affinity, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

CustomEvent Ready

Event ObjectReference.OnLoad(ObjectReference akSender)
    Debug.Notification("loaded")
EndEvent

Event FixtureEventSource.AffinityChanged(FixtureEventSource akSender, Var[] akArgs)
    Debug.Notification("affinity")
EndEvent
)");

    add("27_BreakLeavesTheLoop", [palette](GraphBuilder& graph) mutable {
        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto loop = graph.node(palette, builtinDefs::kWhile);
        const auto first = graph.node(palette, "global:Debug.Notification");
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto leave = graph.node(palette, builtinDefs::kBreak);
        const auto rest = graph.node(palette, "global:Debug.Notification");
        const auto after = graph.node(palette, "global:Debug.Notification");

        graph.value(first, "arg:asNotificationText", "string", "\"A\"");
        graph.value(rest, "arg:asNotificationText", "string", "\"B\"");
        graph.value(after, "arg:asNotificationText", "string", "\"after\"");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, loop, pinIds::kCondition);
        graph.wire(loop, pinIds::kBody, first, pinIds::kExec);
        graph.wire(first, pinIds::kThen, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, leave, pinIds::kExec);
        graph.wire(branch, pinIds::kElse, rest, pinIds::kExec);
        graph.wire(rest, pinIds::kThen, loop, pinIds::kExec);
        graph.wire(loop, pinIds::kCompleted, after, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    bool enabled
    bool broke
    enabled = IsEnabled()
    broke = false
    While (enabled && !broke)
        Debug.Notification("A")
        If (enabled)
            broke = true
        Else
            Debug.Notification("B")
        EndIf
    EndWhile
    Debug.Notification("after")
EndEvent
)");

    add("28_ContinueSkipsThePass", [palette](GraphBuilder& graph) mutable {
        graph.variable("Items", "Form[]", true);

        const auto entry = graph.node(palette, "event:ObjectReference.OnLoad");
        const auto enabled = graph.node(palette, "call:ObjectReference.IsEnabled");
        const auto loop = graph.node(palette, builtinDefs::kForEach);
        const auto items = graph.node(palette, builtinDefs::kVariableGet, {{"name", "Items"}});
        const auto branch = graph.node(palette, builtinDefs::kBranch);
        const auto skip = graph.node(palette, builtinDefs::kContinue);
        const auto work = graph.node(palette, "global:Debug.Notification");

        graph.value(work, "arg:asNotificationText", "string", "\"work\"");

        graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
        graph.wire(enabled, pinIds::kThen, loop, pinIds::kExec);
        graph.wire(items, pinIds::kValue, loop, pinIds::kArray);
        graph.wire(loop, pinIds::kBody, branch, pinIds::kExec);
        graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
        graph.wire(branch, pinIds::kThen, skip, pinIds::kExec);
        graph.wire(branch, pinIds::kElse, work, pinIds::kExec);
    }, R"(Scriptname Fixture extends ObjectReference

Form[] Property Items Auto

Event OnLoad()
    bool enabled
    Form[] items
    int index
    Form item
    enabled = IsEnabled()
    items = Items
    index = 0
    While (index < items.Length)
        item = items[index]
        If (!enabled)
            Debug.Notification("work")
        EndIf
        index = index + 1
    EndWhile
EndEvent
)");

    return fixtures;
}

}
