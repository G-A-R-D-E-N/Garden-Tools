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
}
