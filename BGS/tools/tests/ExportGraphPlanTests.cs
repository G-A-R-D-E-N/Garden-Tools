using System.Linq;
using System.Text.Json.Nodes;
using BehaviourStudio.Tools.Export;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

// Invariant: the plan is built from the native XML alone, so a graph with no vanilla file still pins its shape.
public sealed class ExportGraphPlanTests
{
    private const string Xml = """
        <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
            <hksection name="__data__">
                <hkobject class="hkbBehaviorGraphStringData" name="#90" signature="0x0">
                    <hkparam name="eventNames" numelements="1">Attack</hkparam>
                    <hkparam name="variableNames" numelements="1">Speed</hkparam>
                </hkobject>
                <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                    <hkparam name="name">TestGraph</hkparam>
                    <hkparam name="rootGenerator">#92</hkparam>
                </hkobject>
                <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                    <hkparam name="name">Root</hkparam>
                    <hkparam name="startStateId">0</hkparam>
                    <hkparam name="states" numelements="3">#93 #96 #100</hkparam>
                    <hkparam name="wildcardTransitions">#104</hkparam>
                </hkobject>
                <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                    <hkparam name="name">Idle</hkparam>
                    <hkparam name="stateId">0</hkparam>
                    <hkparam name="generator">#94</hkparam>
                    <hkparam name="transitions">#103</hkparam>
                </hkobject>
                <hkobject class="hkbClipGenerator" name="#94" signature="0x0">
                    <hkparam name="name">IdleClip</hkparam>
                    <hkparam name="animationName">Idle.hkx</hkparam>
                    <hkparam name="playbackSpeed">1.5</hkparam>
                </hkobject>
                <hkobject class="hkbStateMachineStateInfo" name="#96" signature="0x0">
                    <hkparam name="name">BlendState</hkparam>
                    <hkparam name="stateId">1</hkparam>
                    <hkparam name="generator">#97</hkparam>
                </hkobject>
                <hkobject class="hkbBlenderGenerator" name="#97" signature="0x0">
                    <hkparam name="name">Blend</hkparam>
                    <hkparam name="children" numelements="2">#98 #99</hkparam>
                </hkobject>
                <hkobject class="hkbBlenderGeneratorChild" name="#98" signature="0x0">
                    <hkparam name="weight">0.75</hkparam>
                    <hkparam name="generator">#105</hkparam>
                </hkobject>
                <hkobject class="hkbBlenderGeneratorChild" name="#99" signature="0x0">
                    <hkparam name="weight">0.25</hkparam>
                    <hkparam name="generator">#106</hkparam>
                </hkobject>
                <hkobject class="hkbStateMachineStateInfo" name="#100" signature="0x0">
                    <hkparam name="name">OddState</hkparam>
                    <hkparam name="stateId">2</hkparam>
                    <hkparam name="generator">#101</hkparam>
                </hkobject>
                <hkobject class="SomethingUnknownGenerator" name="#101" signature="0x0">
                    <hkparam name="name">Odd</hkparam>
                </hkobject>
                <hkobject class="hkbStringCondition" name="#102" signature="0x0">
                    <hkparam name="conditionString">Speed &gt; 1</hkparam>
                </hkobject>
                <hkobject class="hkbStateMachineTransitionInfoArray" name="#103" signature="0x0">
                    <hkparam name="transitions" numelements="1">
                        <hkobject>
                            <hkparam name="toStateId">1</hkparam>
                            <hkparam name="eventId">0</hkparam>
                            <hkparam name="condition">#102</hkparam>
                            <hkparam name="transition">#107</hkparam>
                        </hkobject>
                    </hkparam>
                </hkobject>
                <hkobject class="hkbStateMachineTransitionInfoArray" name="#104" signature="0x0">
                    <hkparam name="transitions" numelements="1">
                        <hkobject>
                            <hkparam name="toStateId">0</hkparam>
                        </hkobject>
                    </hkparam>
                </hkobject>
                <hkobject class="hkbClipGenerator" name="#105" signature="0x0">
                    <hkparam name="name">BlendA</hkparam>
                    <hkparam name="animationName">A.hkx</hkparam>
                </hkobject>
                <hkobject class="hkbClipGenerator" name="#106" signature="0x0">
                    <hkparam name="name">BlendB</hkparam>
                    <hkparam name="animationName">B.hkx</hkparam>
                </hkobject>
                <hkobject class="hkbBlendingTransitionEffect" name="#107" signature="0x0">
                    <hkparam name="duration">0.25</hkparam>
                </hkobject>
            </hksection>
        </hkpackfile>
        """;

    // Invariant: the plan must keep every state, transition, generator and gap, so a consumer cannot lose graph data.
    [Fact]
    public void PlanCarriesStatesTransitionsGeneratorsAndGaps()
    {
        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(Xml));

        Assert.Equal("TestGraph", plan.GraphName);
        Assert.Equal(0, plan.RootMachine);
        var machine = Assert.Single(plan.Machines);
        Assert.Equal("Root", machine.Name);
        Assert.Equal(3, machine.States.Count);
        Assert.Equal(new[] { "Attack" }, plan.EventNames);
        Assert.Equal(new[] { "Speed" }, plan.VariableNames);

        Assert.Equal(PlanNodeKind.Clip, machine.States[0].Generator!.Kind);
        Assert.Equal("IdleClip", machine.States[0].Generator!.Name);
        Assert.Equal("Idle.hkx", machine.States[0].Generator!.AnimationName);
        Assert.Equal(1.5f, machine.States[0].Generator!.Speed, 6);

        var blend = machine.States[1].Generator!;
        Assert.Equal(PlanNodeKind.Blend, blend.Kind);
        Assert.Equal(2, blend.Children.Count);
        Assert.Equal(0.75f, blend.Children[0].Weight, 6);
        Assert.Equal(0.25f, blend.Children[1].Weight, 6);
        Assert.Equal("A.hkx", blend.Children[0].AnimationName);
        Assert.Equal("B.hkx", blend.Children[1].AnimationName);

        Assert.Equal(PlanNodeKind.Unmapped, machine.States[2].Generator!.Kind);
        Assert.Equal(1, plan.UnmappedGenerators["SomethingUnknownGenerator"]);

        Assert.Equal(2, machine.Transitions.Count);
        var attack = Assert.Single(machine.Transitions, row => !row.Wildcard);
        Assert.Equal(0, attack.From);
        Assert.Equal(1, attack.To);
        Assert.Equal(0, attack.EventId);
        Assert.Equal("Attack", attack.EventName);
        Assert.Equal("hkbStringCondition", attack.ConditionClass);
        Assert.Equal("Speed > 1", attack.ConditionText);
        Assert.Equal(0.25f, attack.Duration, 6);

        var wildcard = Assert.Single(machine.Transitions, row => row.Wildcard);
        Assert.Equal(-1, wildcard.From);
        Assert.Equal(0, wildcard.To);
        Assert.Equal(-1, wildcard.EventId);

        Assert.Equal(1, plan.GapCount("blend-branch-not-wired"));
        Assert.Equal(2, plan.GapCount("blend-weight-ignored"));
        Assert.Equal(1, plan.GapCount("generator-unmapped"));
        Assert.Equal(0, plan.GapCount("generator-reference-unresolved"));
        Assert.Equal(1, plan.BlendNodes);
        Assert.Equal(2, plan.StatesWithGenerator);
        Assert.Equal(1, plan.TransitionsWithEvent);
        Assert.Equal(1, plan.TransitionsWithCondition);
        Assert.Equal(0, plan.MissingGenerators);
        Assert.Equal(0, plan.RevisitedGenerators);
    }

    // Invariant: the JSON must carry the plan's fields unchanged, so a serializer slip cannot drop them.
    [Fact]
    public void PlanJsonCarriesTheSameFieldsAsThePlan()
    {
        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(Xml));

        var document = JsonNode.Parse(GraphExport.Json(plan, "synthetic.hkx"))!.AsObject();
        Assert.Equal(ExportFormats.BehaviourGraph, document["format"]!.GetValue<string>());
        Assert.Equal("TestGraph", document["graphName"]!.GetValue<string>());
        Assert.Equal(0, document["rootMachine"]!.GetValue<int>());
        Assert.Equal("Attack", document["events"]![0]!.GetValue<string>());

        var machine = document["machines"]![0]!.AsObject();
        Assert.Equal(3, machine["states"]!.AsArray().Count);
        Assert.Equal("clip", machine["states"]![0]!["generator"]!["kind"]!.GetValue<string>());
        Assert.Equal("blend", machine["states"]![1]!["generator"]!["kind"]!.GetValue<string>());
        Assert.Equal("unmapped", machine["states"]![2]!["generator"]!["kind"]!.GetValue<string>());

        var gaps = document["gaps"]!.AsArray()
            .Select(gap => gap!.AsObject())
            .ToDictionary(gap => gap["key"]!.GetValue<string>(), gap => gap["count"]!.GetValue<int>());
        Assert.Equal(1, gaps["blend-branch-not-wired"]);
        Assert.Equal(2, gaps["blend-weight-ignored"]);
        Assert.Equal(1, gaps["generator-unmapped"]);

        Assert.Equal(3, document["stats"]!["states"]!.GetValue<int>());
    }

    // Invariant: a shared clip keeps its payload in every occurrence, so a consumer can build every branch.
    [Fact]
    public void SharedGeneratorKeepsItsPayloadInEveryOccurrence()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">SharedGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">BlendState</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#97</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGenerator" name="#97" signature="0x0">
                        <hkparam name="name">Blend</hkparam>
                        <hkparam name="children" numelements="2">#98 #99</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGeneratorChild" name="#98" signature="0x0">
                        <hkparam name="weight">0.5</hkparam>
                        <hkparam name="generator">#105</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGeneratorChild" name="#99" signature="0x0">
                        <hkparam name="weight">0.5</hkparam>
                        <hkparam name="generator">#105</hkparam>
                    </hkobject>
                    <hkobject class="hkbClipGenerator" name="#105" signature="0x0">
                        <hkparam name="name">SharedClip</hkparam>
                        <hkparam name="animationName">Shared.hkx</hkparam>
                        <hkparam name="playbackSpeed">1.25</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));
        var machine = Assert.Single(plan.Machines);
        var blend = machine.States[0].Generator!;

        Assert.Equal(PlanNodeKind.Blend, blend.Kind);
        Assert.Equal(2, blend.Children.Count);
        Assert.All(blend.Children, child => Assert.Equal(PlanNodeKind.Clip, child.Kind));
        Assert.All(blend.Children, child => Assert.Equal("Shared.hkx", child.AnimationName));
        Assert.All(blend.Children, child => Assert.Equal(1.25f, child.Speed, 6));
        Assert.Equal(0, plan.RevisitedGenerators);

        var document = JsonNode.Parse(GraphExport.Json(plan, "shared.hkx"))!.AsObject();
        var children = document["machines"]![0]!["states"]![0]!["generator"]!["children"]!.AsArray();
        Assert.Equal(2, children.Count);
        Assert.All(children, child => Assert.Equal("clip", child!["kind"]!.GetValue<string>()));
    }

    // Invariant: a shared state machine is written once and referenced by index, so its states are not duplicated.
    [Fact]
    public void SharedStateMachineIsWrittenOnceAndReferencedByIndex()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">SharedMachineGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="2">#93 #94</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">First</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#95</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#94" signature="0x0">
                        <hkparam name="name">Second</hkparam>
                        <hkparam name="stateId">1</hkparam>
                        <hkparam name="generator">#95</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#95" signature="0x0">
                        <hkparam name="name">Shared</hkparam>
                        <hkparam name="states" numelements="1">#96</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#96" signature="0x0">
                        <hkparam name="name">ClipState</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#106</hkparam>
                    </hkobject>
                    <hkobject class="hkbClipGenerator" name="#106" signature="0x0">
                        <hkparam name="name">SharedMachineClip</hkparam>
                        <hkparam name="animationName">Machine.hkx</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        Assert.Equal(2, plan.Machines.Count);
        Assert.Equal(0, plan.RootMachine);
        Assert.Equal(2, plan.Machines[0].States.Count);
        Assert.Equal("Shared", plan.Machines[1].Name);
        Assert.Equal("Machine.hkx", Assert.Single(plan.Machines[1].States).Generator!.AnimationName);

        Assert.All(plan.Machines[0].States,
                   state => Assert.Equal(1, state.Generator!.Machine));
        Assert.All(plan.Machines[0].States,
                   state => Assert.Empty(state.Generator!.Children));
        Assert.Equal(0, plan.RevisitedGenerators);

        var document = JsonNode.Parse(GraphExport.Json(plan, "shared-machine.hkx"))!.AsObject();
        var second = document["machines"]![0]!["states"]![1]!["generator"]!.AsObject();
        Assert.Equal("machine", second["kind"]!.GetValue<string>());
        Assert.Equal(1, second["machine"]!.GetValue<int>());
        Assert.Empty(second["children"]!.AsArray());
    }

    // Invariant: a generator that is its own ancestor becomes one stub instead of recursing forever.
    [Fact]
    public void GeneratorCycleBecomesASharedStub()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">CycleGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Loops</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#92</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        Assert.Single(plan.Machines);
        Assert.Equal(PlanNodeKind.Shared, Assert.Single(plan.Machines[0].States).Generator!.Kind);
        Assert.Equal(1, plan.RevisitedGenerators);

        var document = JsonNode.Parse(GraphExport.Json(plan, "cycle.hkx"))!.AsObject();
        Assert.Equal("shared", document["machines"]![0]!["states"]![0]!["generator"]!["kind"]!.GetValue<string>());
    }
}
