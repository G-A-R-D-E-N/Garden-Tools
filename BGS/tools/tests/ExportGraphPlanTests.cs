using System.Linq;
using System.Text;
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

    // Invariant: a non-machine generator that is its own ancestor becomes one stub instead of recursing forever.
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
                        <hkparam name="generator">#94</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGenerator" name="#94" signature="0x0">
                        <hkparam name="name">LoopBlend</hkparam>
                        <hkparam name="children" numelements="1">#95</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGeneratorChild" name="#95" signature="0x0">
                        <hkparam name="generator">#94</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        Assert.Single(plan.Machines);
        var blend = Assert.Single(plan.Machines[0].States).Generator!;
        Assert.Equal(PlanNodeKind.Blend, blend.Kind);
        Assert.Equal(PlanNodeKind.Shared, Assert.Single(blend.Children).Kind);
        Assert.Equal(1, plan.RevisitedGenerators);

        var document = JsonNode.Parse(GraphExport.Json(plan, "cycle.hkx"))!.AsObject();
        Assert.Equal("shared",
                     document["machines"]![0]!["states"]![0]!["generator"]!["children"]![0]!["kind"]!
                         .GetValue<string>());
    }

    // Invariant: a machine that closes a cycle keeps the index of the copy already written, so recursion stays followable.
    [Fact]
    public void MachineCycleReferenceCarriesItsWrittenIndex()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">RecursiveGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Reenters</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#92</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        Assert.Single(plan.Machines);
        var reentry = Assert.Single(plan.Machines[0].States).Generator!;
        Assert.Equal(PlanNodeKind.Machine, reentry.Kind);
        Assert.Equal(0, reentry.Machine);
        Assert.Empty(reentry.Children);
        Assert.Equal(0, plan.RevisitedGenerators);

        var document = JsonNode.Parse(GraphExport.Json(plan, "recursive.hkx"))!.AsObject();
        var written = document["machines"]![0]!["states"]![0]!["generator"]!.AsObject();
        Assert.Equal("machine", written["kind"]!.GetValue<string>());
        Assert.Equal(0, written["machine"]!.GetValue<int>());
        Assert.Contains("closes a cycle", written["detail"]!.GetValue<string>());
    }

    // Invariant: a truncated plan still points at machines it already wrote, and its gap counts every stub.
    [Fact]
    public void BudgetedPlanCountsItsStubsAndKeepsMachineReferences()
    {
        const int Levels = 17;

        var xml = new StringBuilder();
        xml.Append("<hkpackfile classversion=\"11\" contentsversion=\"hk_2014.1.0-r1\"><hksection name=\"__data__\">");
        xml.Append("<hkobject class=\"hkbBehaviorGraph\" name=\"#91\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">BudgetGraph</hkparam>")
           .Append("<hkparam name=\"rootGenerator\">#92</hkparam></hkobject>");
        xml.Append("<hkobject class=\"hkbStateMachine\" name=\"#92\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">Root</hkparam>")
           .Append("<hkparam name=\"states\" numelements=\"3\">#93 #94 #95</hkparam></hkobject>");

        foreach (var (state, name, generator) in new[] { (93, "Before", 96), (94, "Chain", 100), (95, "After", 96) })
            xml.Append($"<hkobject class=\"hkbStateMachineStateInfo\" name=\"#{state}\" signature=\"0x0\">")
               .Append($"<hkparam name=\"name\">{name}</hkparam>")
               .Append($"<hkparam name=\"stateId\">{state - 93}</hkparam>")
               .Append($"<hkparam name=\"generator\">#{generator}</hkparam></hkobject>");

        xml.Append("<hkobject class=\"hkbStateMachine\" name=\"#96\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">Written</hkparam>")
           .Append("<hkparam name=\"states\" numelements=\"1\">#97</hkparam></hkobject>");
        xml.Append("<hkobject class=\"hkbStateMachineStateInfo\" name=\"#97\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">Only</hkparam><hkparam name=\"stateId\">0</hkparam>")
           .Append("<hkparam name=\"generator\">#98</hkparam></hkobject>");
        xml.Append("<hkobject class=\"hkbClipGenerator\" name=\"#98\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">WrittenClip</hkparam>")
           .Append("<hkparam name=\"animationName\">Written.hkx</hkparam></hkobject>");

        for (int level = 0; level < Levels; level++)
        {
            int blend = 100 + level;
            int first = 120 + level * 2;
            int next = level + 1 < Levels ? blend + 1 : 300;

            xml.Append($"<hkobject class=\"hkbBlenderGenerator\" name=\"#{blend}\" signature=\"0x0\">")
               .Append($"<hkparam name=\"name\">Blend{level}</hkparam>")
               .Append($"<hkparam name=\"children\" numelements=\"2\">#{first} #{first + 1}</hkparam>")
               .Append("</hkobject>");

            for (int child = 0; child < 2; child++)
                xml.Append($"<hkobject class=\"hkbBlenderGeneratorChild\" name=\"#{first + child}\" signature=\"0x0\">")
                   .Append("<hkparam name=\"weight\">0.5</hkparam>")
                   .Append($"<hkparam name=\"generator\">#{next}</hkparam>")
                   .Append("</hkobject>");
        }

        xml.Append("<hkobject class=\"hkbClipGenerator\" name=\"#300\" signature=\"0x0\">")
           .Append("<hkparam name=\"name\">Leaf</hkparam>")
           .Append("<hkparam name=\"animationName\">Leaf.hkx</hkparam></hkobject>");
        xml.Append("</hksection></hkpackfile>");

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml.ToString()));

        Assert.Equal(2, plan.Machines.Count);
        Assert.Equal(3, plan.Machines[0].States.Count);

        var after = plan.Machines[0].States[2].Generator!;
        Assert.Equal(PlanNodeKind.Machine, after.Kind);
        Assert.Equal(1, after.Machine);

        int stubs = plan.GapCount("plan-node-budget");
        Assert.True(stubs > 1, $"plan-node-budget counted {stubs} stub(s); it must count every stub");
    }

    // Invariant: a truncated plan names the truncation and its size in notes, so a short plan is measurable.
    [Fact]
    public void PlanNotesNameTheTruncationCount()
    {
        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(Xml));
        plan.NoteGap("plan-node-budget", "generator nodes left unexpanded", 12);

        var document = JsonNode.Parse(GraphExport.Json(plan, "notes.hkx"))!.AsObject();
        string notes = document["notes"]!.ToJsonString();

        Assert.Contains("12 generator node(s) were left unexpanded", notes);
    }

    // Invariant: a state whose generator is a shared stub has no usable generator, so the stats must not count it.
    [Fact]
    public void AStateWhoseGeneratorIsASharedStubIsNotCounted()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">SharedStubGraph</hkparam>
                        <hkparam name="rootGenerator">#96</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGenerator" name="#96" signature="0x0">
                        <hkparam name="name">Outer</hkparam>
                        <hkparam name="children" numelements="1">#97</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGeneratorChild" name="#97" signature="0x0">
                        <hkparam name="generator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Loops</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#96</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        var state = Assert.Single(Assert.Single(plan.Machines).States);
        Assert.Equal(PlanNodeKind.Shared, state.Generator!.Kind);
        Assert.Equal(1, plan.RevisitedGenerators);
        Assert.Equal(0, plan.StatesWithGenerator);

        var document = JsonNode.Parse(GraphExport.Json(plan, "shared-stub.hkx"))!.AsObject();
        Assert.Equal(0, document["stats"]!["statesWithGenerator"]!.GetValue<int>());
        Assert.Equal(1, document["stats"]!["revisitedGenerators"]!.GetValue<int>());
    }

    // Invariant: a blend child whose generator reference does not resolve must become a gap row, not vanish.
    [Fact]
    public void ADanglingBlendChildBecomesAGapRow()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">DanglingBlendGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Dangling</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#96</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGenerator" name="#96" signature="0x0">
                        <hkparam name="name">Blend</hkparam>
                        <hkparam name="children" numelements="1">#97</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGeneratorChild" name="#97" signature="0x0">
                        <hkparam name="generator">#999</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        var blend = Assert.Single(Assert.Single(plan.Machines).States).Generator!;
        Assert.Equal(PlanNodeKind.Blend, blend.Kind);
        Assert.Empty(blend.Children);
        Assert.Equal(1, plan.GapCount("generator-reference-unresolved"));
        Assert.Contains(plan.Warnings, warning => warning.Contains("'999'", StringComparison.Ordinal));

        var document = JsonNode.Parse(GraphExport.Json(plan, "dangling-blend.hkx"))!.AsObject();
        var gaps = document["gaps"]!.AsArray()
            .Select(gap => gap!.AsObject())
            .ToDictionary(gap => gap["key"]!.GetValue<string>(), gap => gap["count"]!.GetValue<int>());
        Assert.Equal(1, gaps["generator-reference-unresolved"]);
    }

    // Invariant: a selector child whose generator reference does not resolve must become a gap row too.
    [Fact]
    public void ADanglingSelectorChildBecomesAGapRow()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">DanglingSelectorGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Select</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#96</hkparam>
                    </hkobject>
                    <hkobject class="hkbManualSelectorGenerator" name="#96" signature="0x0">
                        <hkparam name="name">Select</hkparam>
                        <hkparam name="generators" numelements="2">#97 #999</hkparam>
                    </hkobject>
                    <hkobject class="hkbClipGenerator" name="#97" signature="0x0">
                        <hkparam name="name">OnlyClip</hkparam>
                        <hkparam name="animationName">Only.hkx</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        var selector = Assert.Single(Assert.Single(plan.Machines).States).Generator!;
        Assert.Equal(PlanNodeKind.Select, selector.Kind);
        Assert.Equal("Only.hkx", Assert.Single(selector.Children).AnimationName);
        Assert.Equal(1, plan.GapCount("generator-reference-unresolved"));
        Assert.Contains(plan.Warnings, warning => warning.Contains("'999'", StringComparison.Ordinal));
    }

    // Invariant: a blend child whose wrapper object is absent still reports the wrapper it could not follow.
    [Fact]
    public void AMissingBlendChildWrapperBecomesAGapRow()
    {
        const string xml = """
            <hkpackfile classversion="11" contentsversion="hk_2014.1.0-r1">
                <hksection name="__data__">
                    <hkobject class="hkbBehaviorGraph" name="#91" signature="0x0">
                        <hkparam name="name">MissingWrapperGraph</hkparam>
                        <hkparam name="rootGenerator">#92</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachine" name="#92" signature="0x0">
                        <hkparam name="name">Root</hkparam>
                        <hkparam name="states" numelements="1">#93</hkparam>
                    </hkobject>
                    <hkobject class="hkbStateMachineStateInfo" name="#93" signature="0x0">
                        <hkparam name="name">Missing</hkparam>
                        <hkparam name="stateId">0</hkparam>
                        <hkparam name="generator">#96</hkparam>
                    </hkobject>
                    <hkobject class="hkbBlenderGenerator" name="#96" signature="0x0">
                        <hkparam name="name">Blend</hkparam>
                        <hkparam name="children" numelements="1">#998</hkparam>
                    </hkobject>
                </hksection>
            </hkpackfile>
            """;

        var plan = GraphPlan.Build(BehaviourGraphModel.Parse(xml));

        var blend = Assert.Single(Assert.Single(plan.Machines).States).Generator!;
        Assert.Equal(PlanNodeKind.Blend, blend.Kind);
        Assert.Empty(blend.Children);
        Assert.Equal(1, plan.GapCount("generator-reference-unresolved"));
        Assert.Contains(plan.Warnings, warning => warning.Contains("'998'", StringComparison.Ordinal));
    }
}
