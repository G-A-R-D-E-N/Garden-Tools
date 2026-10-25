using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools.Export;

public enum PlanNodeKind
{
    Clip,
    Blend,
    Select,
    Machine,
    ReferencePose,
    ExternalBehaviour,
    Sequence,
    Shared,
    Unmapped,
}

public sealed class PlanNode
{
    public PlanNodeKind Kind { get; set; }
    public string Class { get; set; } = "";
    public string Name { get; set; } = "";
    public string AnimationName { get; set; } = "";
    public string Detail { get; set; } = "";
    public float Speed { get; set; } = 1f;
    public float Weight { get; set; } = 1f;
    public int Machine { get; set; } = -1;
    public List<PlanNode> Children { get; } = new();
}

public sealed class PlanState
{
    public int StateId { get; set; }
    public string Name { get; set; } = "";
    public PlanNode? Generator { get; set; }
}

public sealed class PlanTransition
{
    public int From { get; set; } = -1;
    public int To { get; set; } = -1;
    public bool Wildcard { get; set; }
    public int EventId { get; set; } = -1;
    public string EventName { get; set; } = "";
    public string ConditionClass { get; set; } = "";
    public string ConditionText { get; set; } = "";
    public float Duration { get; set; }
}

public sealed class PlanMachine
{
    public string Name { get; set; } = "";
    public int StartStateId { get; set; }
    public List<PlanState> States { get; } = new();
    public List<PlanTransition> Transitions { get; } = new();
}

public sealed class PlanGap
{
    public int Count { get; set; }
    public string Explanation { get; init; } = "";
}

// Constraint: the plan is read out of the native XML and object graph, so no Java tool, converted file or external process is consulted.
public sealed class GraphPlan
{
    public const int MaximumWarnings = 40;

    private const int MaximumResolvedNodes = 200_000;

    private readonly Dictionary<string, int> _machinesById = new(StringComparer.Ordinal);
    private int _resolvedNodes;
    private bool _budgetNoted;

    private static readonly Dictionary<string, string> PassThrough = new(StringComparer.Ordinal)
    {
        ["hkbModifierGenerator"] = "generator",
        ["BSiStateTaggingGenerator"] = "pDefaultGenerator",
        ["BSCyclicBlendTransitionGenerator"] = "pBlenderGenerator",
        ["DynamicAnimationTaggingGenerator"] = "pDefaultGenerator",
        ["BSOffsetAnimationGenerator"] = "pDefaultGenerator",
        ["BSBehaviorGraphSwapGenerator"] = "pDefaultGenerator",
        ["BSBoneSwitchGenerator"] = "pDefaultGenerator",
    };

    public string GraphName { get; private set; } = "";
    public int RootMachine { get; private set; } = -1;
    public List<PlanMachine> Machines { get; } = new();
    public List<string> EventNames { get; } = new();
    public List<string> VariableNames { get; } = new();
    public List<string> Warnings { get; } = new();
    public SortedDictionary<string, PlanGap> Gaps { get; } = new(StringComparer.Ordinal);
    public SortedDictionary<string, int> UnmappedGenerators { get; } = new(StringComparer.Ordinal);
    public int MissingGenerators { get; private set; }
    public int RevisitedGenerators { get; private set; }
    public int BlendNodes { get; private set; }

    public int StateCount => Machines.Sum(machine => machine.States.Count);
    public int TransitionCount => Machines.Sum(machine => machine.Transitions.Count);
    public int StatesWithGenerator => Machines.Sum(
        machine => machine.States.Count(state => state.Generator is { Kind: not PlanNodeKind.Unmapped }));
    public int TransitionsWithEvent => Machines.Sum(
        machine => machine.Transitions.Count(transition => transition.EventId >= 0));
    public int TransitionsWithCondition => Machines.Sum(
        machine => machine.Transitions.Count(transition => transition.ConditionText.Length > 0));

    public int GapCount(string key) => Gaps.TryGetValue(key, out var gap) ? gap.Count : 0;

    public void NoteGap(string key, string explanation, int count = 1)
    {
        if (!Gaps.TryGetValue(key, out var gap))
        {
            gap = new PlanGap { Explanation = explanation };
            Gaps[key] = gap;
        }

        gap.Count += count;
    }

    public static GraphPlan Build(BehaviourGraphModel model)
    {
        ArgumentNullException.ThrowIfNull(model);

        var plan = new GraphPlan();

        if (model.Objects.FirstOrDefault(o => o.Class == "hkbBehaviorGraphStringData") is { } strings)
        {
            plan.EventNames.AddRange(strings.Strings("eventNames"));
            plan.VariableNames.AddRange(strings.Strings("variableNames"));
        }

        var graph = model.Objects.FirstOrDefault(o => o.Class == "hkbBehaviorGraph");
        plan.GraphName = graph?.Str("name") ?? "";

        var root = graph != null
            ? model.Follow(graph, "rootGenerator")
            : model.Objects.FirstOrDefault(o => o.Class == "hkbStateMachine");

        var node = plan.Resolve(model, root, new HashSet<string>(StringComparer.Ordinal));
        if (node is { Kind: PlanNodeKind.Machine }) plan.RootMachine = node.Machine;
        else if (plan.Machines.Count > 0) plan.RootMachine = 0;

        return plan;
    }

    // Constraint: only an ancestor cycle stubs a generator, so a shared subtree keeps its payload at every occurrence.
    private PlanNode? Resolve(BehaviourGraphModel model, HkObject? generator, HashSet<string> path)
    {
        if (generator == null) return null;

        if (!path.Add(generator.Id))
        {
            // Invariant: a machine already written keeps its index even when it closes a cycle, so recursion stays followable.
            if (generator.Class == "hkbStateMachine" && _machinesById.TryGetValue(generator.Id, out int written))
                return MachineReference(generator, written, "this reference closes a cycle");

            RevisitedGenerators++;
            return new PlanNode
            {
                Kind = PlanNodeKind.Shared,
                Class = generator.Class,
                Name = generator.Str("name"),
                Detail = "cycle: already being resolved above this point in the tree",
            };
        }

        try
        {
            return ResolveOnce(model, generator, path);
        }
        finally
        {
            path.Remove(generator.Id);
        }
    }

    // Constraint: the node budget bounds repeated expansion, so a pathological graph cannot exhaust memory.
    private PlanNode? ResolveOnce(BehaviourGraphModel model, HkObject generator, HashSet<string> path)
    {
        // Invariant: a machine already in machines[] keeps its index past the budget, so truncation cannot orphan it.
        if (generator.Class == "hkbStateMachine" && _machinesById.TryGetValue(generator.Id, out int written))
            return MachineReference(generator, written);

        if (++_resolvedNodes > MaximumResolvedNodes)
        {
            NoteGap("plan-node-budget",
                    "generator nodes the plan left unexpanded at its node budget, counted per node");

            if (!_budgetNoted)
            {
                _budgetNoted = true;
                if (Warnings.Count < MaximumWarnings)
                    Warnings.Add("plan node budget reached; deeper generator branches are shared stubs");
            }

            return new PlanNode
            {
                Kind = PlanNodeKind.Shared,
                Class = generator.Class,
                Name = generator.Str("name"),
                Detail = "plan node budget reached",
            };
        }

        switch (generator.Class)
        {
            case "hkbClipGenerator":
                return new PlanNode
                {
                    Kind = PlanNodeKind.Clip,
                    Class = generator.Class,
                    Name = generator.Str("name"),
                    AnimationName = generator.Str("animationName"),
                    Speed = Float(generator.Str("playbackSpeed"), 1f),
                };

            case "hkbReferencePoseGenerator":
                return new PlanNode
                {
                    Kind = PlanNodeKind.ReferencePose,
                    Class = generator.Class,
                    Name = generator.Str("name"),
                };

            case "hkbBehaviorReferenceGenerator":
                return new PlanNode
                {
                    Kind = PlanNodeKind.ExternalBehaviour,
                    Class = generator.Class,
                    Name = generator.Str("name"),
                    Detail = generator.Str("behaviorName"),
                };

            case "BGSGamebryoSequenceGenerator":
                return new PlanNode
                {
                    Kind = PlanNodeKind.Sequence,
                    Class = generator.Class,
                    Name = generator.Str("name"),
                    Detail = generator.Str("pSequence"),
                };

            case "hkbStateMachine":
                return MachineNode(model, generator, path);

            case "hkbBlenderGenerator":
            case "hkbPoseMatchingGenerator":
                return WrappedNode(model, generator, path, "children");

            case "hkbManualSelectorGenerator":
                return SelectorNode(model, generator, path);

            case "hkbLayerGenerator":
                return WrappedNode(model, generator, path, "layers");
        }

        if (PassThrough.TryGetValue(generator.Class, out string? field))
        {
            var inner = Resolve(model, model.Follow(generator, field), path);
            if (inner != null) return inner;

            return Unmapped(generator, $"{generator.Class}.{field} was null");
        }

        return Unmapped(generator, "no mapping for this generator class");
    }

    private static PlanNode MachineReference(HkObject machineObject, int written, string note = "") =>
        new PlanNode
        {
            Kind = PlanNodeKind.Machine,
            Class = machineObject.Class,
            Name = machineObject.Str("name"),
            Machine = written,
            Detail = note.Length == 0
                ? $"already written as machine {written}"
                : $"already written as machine {written}; {note}",
        };

    private PlanNode MachineNode(BehaviourGraphModel model, HkObject machineObject, HashSet<string> path)
    {
        var machine = new PlanMachine
        {
            Name = machineObject.Str("name"),
            StartStateId = machineObject.Int("startStateId", 0),
        };

        int index = Machines.Count;
        Machines.Add(machine);
        _machinesById[machineObject.Id] = index;

        foreach (string stateId in machineObject.Refs("states"))
        {
            var info = model.Get(stateId);
            if (info == null) continue;

            var generator = model.Follow(info, "generator");
            if (generator == null)
            {
                MissingGenerators++;
                NoteGap("generator-reference-unresolved", "a state's generator reference did not resolve to an object");
                if (MissingGenerators <= 5 && Warnings.Count < MaximumWarnings)
                    Warnings.Add($"state '{info.Str("name")}' in '{machine.Name}' has no resolvable generator");
            }

            var state = new PlanState
            {
                StateId = info.Int("stateId"),
                Name = info.Str("name"),
                Generator = Resolve(model, generator, path),
            };
            machine.States.Add(state);

            AddTransitions(model, machine, model.Follow(info, "transitions"), state.StateId, wildcard: false);
        }

        AddTransitions(model, machine, model.Follow(machineObject, "wildcardTransitions"), -1, wildcard: true);

        return new PlanNode
        {
            Kind = PlanNodeKind.Machine,
            Class = machineObject.Class,
            Name = machineObject.Str("name"),
            Machine = index,
        };
    }

    private PlanNode WrappedNode(BehaviourGraphModel model, HkObject generator, HashSet<string> path,
                                 string field)
    {
        var node = new PlanNode
        {
            Kind = PlanNodeKind.Blend,
            Class = generator.Class,
            Name = generator.Str("name"),
        };

        foreach (string wrapperId in generator.Refs(field))
        {
            var wrapper = model.Get(wrapperId);
            var inner = Resolve(model, model.Follow(wrapper, "generator"), path);
            if (inner == null) continue;

            if (wrapper != null && TryFloat(wrapper.Str("weight"), out float weight))
            {
                inner.Weight = weight;
                NoteGap("blend-weight-ignored", "blend weight read from the graph but not applied by any consumer here");
            }

            node.Children.Add(inner);
        }

        NoteBlend(node);
        return node;
    }

    private PlanNode SelectorNode(BehaviourGraphModel model, HkObject generator, HashSet<string> path)
    {
        var node = new PlanNode
        {
            Kind = PlanNodeKind.Select,
            Class = generator.Class,
            Name = generator.Str("name"),
        };

        foreach (string childId in generator.Refs("generators"))
        {
            var inner = Resolve(model, model.Get(childId), path);
            if (inner != null) node.Children.Add(inner);
        }

        NoteBlend(node);
        return node;
    }

    private void AddTransitions(BehaviourGraphModel model, PlanMachine machine, HkObject? holder,
                                int from, bool wildcard)
    {
        if (holder == null || !holder.StructLists.TryGetValue("transitions", out var rows)) return;

        foreach (var row in rows)
        {
            int eventId = Int(row, "eventId", -1);
            string eventName = eventId >= 0 && eventId < EventNames.Count ? EventNames[eventId] : "";

            string conditionClass = "";
            string conditionText = "";
            if (row.TryGetValue("condition", out string condition) && condition.StartsWith('#'))
            {
                var held = model.Get(condition[1..]);
                if (held != null)
                {
                    conditionClass = held.Class;
                    conditionText = held.Str("expression");
                    if (conditionText.Length == 0) conditionText = held.Str("conditionString");
                    if (conditionText.Length == 0) conditionText = held.Class;
                }
            }

            float duration = 0f;
            if (row.TryGetValue("transition", out string effect) && effect.StartsWith('#'))
            {
                var held = model.Get(effect[1..]);
                if (held != null) duration = Float(held.Str("duration"), 0f);
            }

            machine.Transitions.Add(new PlanTransition
            {
                From = from,
                To = Int(row, "toStateId", -1),
                Wildcard = wildcard,
                EventId = eventId,
                EventName = eventName,
                ConditionClass = conditionClass,
                ConditionText = conditionText,
                Duration = duration,
            });
        }
    }

    private void NoteBlend(PlanNode node)
    {
        BlendNodes++;

        if (node.Children.Count <= 1) return;

        NoteGap("blend-branch-not-wired",
                "only the first branch reaches a consumer's output, so the rest never play",
                node.Children.Count - 1);

        if (Warnings.Count < MaximumWarnings)
            Warnings.Add($"blend '{node.Name}' ({node.Class}) has {node.Children.Count} branches; " +
                         $"{node.Children.Count - 1} are not connected");
    }

    private PlanNode Unmapped(HkObject generator, string detail)
    {
        UnmappedGenerators.TryGetValue(generator.Class, out int seen);
        UnmappedGenerators[generator.Class] = seen + 1;
        NoteGap("generator-unmapped", "generator class with no mapping, carried as a placeholder node");

        if (seen == 0 && Warnings.Count < MaximumWarnings)
            Warnings.Add($"unmapped generator class: {generator.Class}");

        return new PlanNode
        {
            Kind = PlanNodeKind.Unmapped,
            Class = generator.Class,
            Name = generator.Str("name"),
            Detail = detail,
        };
    }

    private static int Int(Dictionary<string, string> row, string field, int fallback) =>
        row.TryGetValue(field, out string? text) &&
        int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)
            ? value
            : fallback;

    private static float Float(string text, float fallback) =>
        TryFloat(text, out float value) ? value : fallback;

    private static bool TryFloat(string text, out float value) =>
        float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
}
