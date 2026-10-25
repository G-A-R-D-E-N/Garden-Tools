using System.Collections.Generic;

namespace BehaviourStudio.Tools.Export;

public static class GraphExport
{
    public static string Json(GraphPlan plan, string source)
    {
        var notes = new List<string>();
        if (plan.RootMachine < 0)
            notes.Add("no root state machine was resolved, so the plan holds no entry point");

        var json = new ExportJson().Object()
            .Name("format").Text(ExportFormats.BehaviourGraph)
            .Name("version").Number(ExportFormats.Version)
            .Name("source").Text(source)
            .Name("graphName").Text(plan.GraphName)
            .Name("rootMachine");

        if (plan.RootMachine >= 0) json.Number(plan.RootMachine);
        else json.Null();

        json.Name("machines").Array();
        for (int m = 0; m < plan.Machines.Count; m++)
        {
            var machine = plan.Machines[m];

            json.Object()
                .Name("index").Number(m)
                .Name("name").Text(machine.Name)
                .Name("startStateId").Number(machine.StartStateId)
                .Name("states").Array();

            foreach (var state in machine.States)
            {
                json.Object()
                    .Name("stateId").Number(state.StateId)
                    .Name("name").Text(state.Name)
                    .Name("generator");

                if (state.Generator == null) json.Null();
                else Node(json, state.Generator);

                json.EndObject();
            }

            json.EndArray().Name("transitions").Array();
            foreach (var transition in machine.Transitions)
            {
                json.Object()
                    .Name("from").Number(transition.From)
                    .Name("to").Number(transition.To)
                    .Name("wildcard").Boolean(transition.Wildcard)
                    .Name("eventId").Number(transition.EventId)
                    .Name("eventName").Text(transition.EventName)
                    .Name("conditionClass").Text(transition.ConditionClass)
                    .Name("conditionText").Text(transition.ConditionText)
                    .Name("duration").Number(transition.Duration)
                    .EndObject();
            }

            json.EndArray().EndObject();
        }

        json.EndArray().Name("events").Array();
        foreach (string name in plan.EventNames) json.Text(name);

        json.EndArray().Name("variables").Array();
        foreach (string name in plan.VariableNames) json.Text(name);

        json.EndArray().Name("gaps").Array();
        foreach (var (key, gap) in plan.Gaps)
        {
            json.Object()
                .Name("key").Text(key)
                .Name("count").Number(gap.Count)
                .Name("explanation").Text(gap.Explanation)
                .EndObject();
        }

        json.EndArray().Name("unmappedGenerators").Array();
        foreach (var (className, count) in plan.UnmappedGenerators)
            json.Object().Name("class").Text(className).Name("count").Number(count).EndObject();

        json.EndArray().Name("warnings").Array();
        foreach (string warning in plan.Warnings) json.Text(warning);

        json.EndArray().Name("stats").Object()
            .Name("states").Number(plan.StateCount)
            .Name("transitions").Number(plan.TransitionCount)
            .Name("statesWithGenerator").Number(plan.StatesWithGenerator)
            .Name("transitionsWithEvent").Number(plan.TransitionsWithEvent)
            .Name("transitionsWithCondition").Number(plan.TransitionsWithCondition)
            .Name("blendNodes").Number(plan.BlendNodes)
            .Name("missingGenerators").Number(plan.MissingGenerators)
            .Name("revisitedGenerators").Number(plan.RevisitedGenerators)
            .EndObject()
            .Name("notes").Array();

        foreach (string note in notes) json.Text(note);
        json.EndArray();

        return json.EndObject().Finish();
    }

    private static void Node(ExportJson json, PlanNode node)
    {
        json.Object()
            .Name("kind").Text(Kind(node.Kind))
            .Name("class").Text(node.Class)
            .Name("name").Text(node.Name)
            .Name("animationName").Text(node.AnimationName)
            .Name("detail").Text(node.Detail)
            .Name("speed").Number(node.Speed)
            .Name("weight").Number(node.Weight)
            .Name("machine");

        if (node.Machine >= 0) json.Number(node.Machine);
        else json.Null();

        json.Name("children").Array();
        foreach (var child in node.Children) Node(json, child);
        json.EndArray().EndObject();
    }

    private static string Kind(PlanNodeKind kind) => kind switch
    {
        PlanNodeKind.Clip => "clip",
        PlanNodeKind.Blend => "blend",
        PlanNodeKind.Select => "select",
        PlanNodeKind.Machine => "machine",
        PlanNodeKind.ReferencePose => "referencePose",
        PlanNodeKind.ExternalBehaviour => "externalBehaviour",
        PlanNodeKind.Sequence => "sequence",
        PlanNodeKind.Shared => "shared",
        _ => "unmapped",
    };
}
