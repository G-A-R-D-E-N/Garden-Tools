using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

using Numerics = System.Numerics;

public enum HkbVariableType
{
    Bool = 0,
    Int8 = 1,
    Int16 = 2,
    Int32 = 3,
    Real = 4,
    Pointer = 5,
    Vector3 = 6,
    Vector4 = 7,
    Quaternion = 8,
}

// Invariant: KindIndex indexes the table of StateMachine, Blender and Clip nodes and is 0 for every other kind, whose children are in HkbGraph.Edges.
public enum HkbNodeKind
{
    Other = 0,
    StateMachine = 1,
    Blender = 2,
    Clip = 3,
    Selector = 4,
    Layer = 5,
    PoseMatching = 6,
    ModifierGenerator = 7,
    StateTagging = 8,
    CyclicBlendTransition = 9,
    OffsetAnimation = 10,
    BehaviorGraphSwap = 11,
    BoneSwitch = 12,
    BehaviorReference = 13,
    GamebryoSequence = 14,
    DynamicAnimationTagging = 15,
    ReferencePose = 16,
}

public enum HkbEdgeRole
{
    Wrapped = 1,
    OffsetClip = 2,
    Selected = 3,
    Layer = 4,
    BoneSwitchChild = 5,
    BlendChild = 6,
}

public enum HkbConditionKind
{
    None = 0,
    Expression = 1,
    String = 2,
    Other = 3,
}

public enum HkbOwnerKind
{
    StateMachine = 1,
    State = 2,
    Blender = 3,
    BlendChild = 4,
    Clip = 5,
}

public sealed record HkbEvent(string Name, uint Flags);

// Invariant: Word is the file's 32-bit initial value; IntValue and RealValue are two views of it, and Vector is set only for the vector and quaternion types.
public sealed record HkbVariable(string Name, int Type, int Role, uint RoleFlags, uint Word, int IntValue,
                                 float RealValue, Numerics.Vector4? Vector)
{
    public HkbVariableType Kind => (HkbVariableType)Type;
}

public sealed record HkbNode(int Object, string ClassName, string Name, HkbNodeKind Kind, int KindIndex);

public sealed record HkbTimeInterval(int EnterEventId, int ExitEventId, float EnterTime, float ExitTime);

// Invariant: State is the owning state's index in HkbGraph.States, -1 for a state machine wildcard; EffectNode is an index in HkbGraph.Nodes or -1.
public sealed record HkbTransition(int StateMachine, int State, int EventId, int ToStateId, int FromNestedStateId,
                                   int ToNestedStateId, int Priority, uint Flags, HkbTimeInterval TriggerInterval,
                                   HkbTimeInterval InitiateInterval, int EffectNode, HkbConditionKind ConditionKind,
                                   string Condition, string ConditionClass);

public sealed record HkbState(int StateMachine, string Name, int StateId, float Probability, bool Enable,
                              int GeneratorNode, int FirstTransition, int TransitionCount);

public sealed record HkbStateMachine(int Node, int StartStateId, int ChangeEventId, int ReturnToPreviousEventId,
                                     int RandomTransitionEventId, int NextHigherEventId, int NextLowerEventId,
                                     int SyncVariableIndex, int StartStateMode, int SelfTransitionMode, int FirstState,
                                     int StateCount, int FirstWildcard, int WildcardCount);

// Invariant: Resolved means the file alone fixes every child's Contribution; otherwise Contribution is only the stored-weight share and a variable decides the real one.
public sealed record HkbBlender(int Node, float BlendParameter, float MinCyclicBlendParameter,
                                float MaxCyclicBlendParameter, int IndexOfSyncMasterChild, int Flags, bool Parametric,
                                bool SubtractLastChild, int BlendParameterVariable, bool Resolved, int FirstChild,
                                int ChildCount);

public sealed record HkbBlendChild(int Blender, int GeneratorNode, float Weight, float WorldFromModelWeight,
                                   bool HasBoneWeights, int WeightVariable, float Contribution);

public sealed record HkbClipTrigger(float LocalTime, int EventId, bool RelativeToEndOfClip, bool Acyclic,
                                    bool IsAnnotation);

public sealed record HkbClip(int Node, string Animation, string AnimationBundle, int Mode, uint Flags, float StartTime,
                             float PlaybackSpeed, float CropStart, float CropEnd, float EnforcedDuration,
                             HkbClipTrigger[] Triggers);

// Invariant: Node is an index in HkbGraph.Nodes and equals a transition's EffectNode; Flags, EndMode and BlendCurve are the file's raw hkbBlendingTransitionEffect values.
public sealed record HkbEffect(int Node, float Duration, float StartFraction, uint Flags, int EndMode, int BlendCurve);

// Invariant: Parent and Child are valid indexes in HkbGraph.Nodes; Slot is the entry's index in its parent's list (0 for a single pointer); Weight is the stored weight for Layer and BlendChild and 0 for the other roles.
public sealed record HkbEdge(int Parent, int Child, HkbEdgeRole Role, int Slot, float Weight, bool HasBoneWeights);

public sealed record HkbBinding(HkbOwnerKind OwnerKind, int OwnerIndex, string MemberPath, int VariableIndex,
                                int BitIndex, int BindingType);

// Invariant: the tables are flat and index-aligned with the file; an index in a row is valid in its table or -1, and an event or variable id is the file's own value, -1 for none.
public sealed class HkbGraph
{
    public required string Name { get; init; }

    public required int RootNode { get; init; }

    public required HkbEvent[] Events { get; init; }

    public required HkbVariable[] Variables { get; init; }

    public required HkbNode[] Nodes { get; init; }

    public required HkbStateMachine[] StateMachines { get; init; }

    public required HkbState[] States { get; init; }

    public required HkbTransition[] Transitions { get; init; }

    public required HkbBlender[] Blenders { get; init; }

    public required HkbBlendChild[] BlendChildren { get; init; }

    public required HkbClip[] Clips { get; init; }

    public required HkbBinding[] Bindings { get; init; }

    public required HkbEdge[] Edges { get; init; }

    public required HkbEffect[] Effects { get; init; }

    public required string[] Diagnostics { get; init; }

    public string? EventName(int id) => id >= 0 && id < Events.Length ? Events[id].Name : null;

    public string? VariableName(int index) => index >= 0 && index < Variables.Length ? Variables[index].Name : null;

    public HkbNode? NodeAt(int index) => index >= 0 && index < Nodes.Length ? Nodes[index] : null;

    public IEnumerable<HkbState> StatesOf(HkbStateMachine machine) =>
        States.Skip(machine.FirstState).Take(machine.StateCount);

    public IEnumerable<HkbTransition> TransitionsOf(HkbState state) =>
        Transitions.Skip(state.FirstTransition).Take(state.TransitionCount);

    public IEnumerable<HkbTransition> WildcardsOf(HkbStateMachine machine) =>
        Transitions.Skip(machine.FirstWildcard).Take(machine.WildcardCount);

    public IEnumerable<HkbBlendChild> ChildrenOf(HkbBlender blender) =>
        BlendChildren.Skip(blender.FirstChild).Take(blender.ChildCount);

    public IEnumerable<HkbEdge> EdgesOf(int node) => Edges.Where(edge => edge.Parent == node);

    public HkbEffect? EffectOf(int node) => Effects.FirstOrDefault(effect => effect.Node == node);
}

internal static unsafe class HkbGraphReader
{
    private delegate Status TextReader(IntPtr hkx, nuint index, byte** bytes, nuint* size);

    private static string Text(IntPtr handle, int index, TextReader read)
    {
        byte* bytes = null;
        nuint size = 0;
        Call.Check(read(handle, (nuint)index, &bytes, &size));
        return Call.TakeText(bytes, size);
    }

    private static T[] Table<T>(nuint count, Func<int, T> read)
    {
        var rows = new T[checked((int)count)];
        for (var index = 0; index < rows.Length; ++index) rows[index] = read(index);
        return rows;
    }

    public static HkbGraph? Read(IntPtr handle)
    {
        HkxBehaviorSummary summary;
        if (!Call.Found(RadicleNative.HkxBehaviorReadSummary(handle, &summary))) return null;
        byte* name = null;
        nuint nameSize = 0;
        Call.Check(RadicleNative.HkxBehaviorGraphName(handle, &name, &nameSize));
        var graphName = Call.TakeText(name, nameSize);

        return new HkbGraph {
            Name = graphName,
            RootNode = summary.RootNode,
            Events = Table(summary.EventCount, index => ReadEvent(handle, index)),
            Variables = Table(summary.VariableCount, index => ReadVariable(handle, index)),
            Nodes = Table(summary.NodeCount, index => ReadNode(handle, index)),
            StateMachines = Table(summary.StateMachineCount, index => ReadStateMachine(handle, index)),
            States = Table(summary.StateCount, index => ReadState(handle, index)),
            Transitions = Table(summary.TransitionCount, index => ReadTransition(handle, index)),
            Blenders = Table(summary.BlenderCount, index => ReadBlender(handle, index)),
            BlendChildren = Table(summary.BlendChildCount, index => ReadBlendChild(handle, index)),
            Clips = Table(summary.ClipCount, index => ReadClip(handle, index)),
            Bindings = Table(summary.BindingCount, index => ReadBinding(handle, index)),
            Edges = ReadEdges(handle),
            Effects = ReadEffects(handle),
            Diagnostics = Table(summary.DiagnosticCount,
                                index => Text(handle, index, RadicleNative.HkxBehaviorDiagnostic)),
        };
    }

    private static HkbEdge[] ReadEdges(IntPtr handle)
    {
        nuint count;
        Call.Check(RadicleNative.HkxBehaviorEdgeCount(handle, &count));
        return Table(count, index => ReadEdge(handle, index));
    }

    private static HkbEffect[] ReadEffects(IntPtr handle)
    {
        nuint count;
        Call.Check(RadicleNative.HkxBehaviorEffectCount(handle, &count));
        return Table(count, index => ReadEffect(handle, index));
    }

    private static HkbEffect ReadEffect(IntPtr handle, int index)
    {
        HkxBehaviorEffectInfo info;
        Call.Check(RadicleNative.HkxBehaviorEffectReadInfo(handle, (nuint)index, &info));
        return new HkbEffect(checked((int)info.Node), info.Duration, info.StartFraction, info.Flags, info.EndMode,
                             info.BlendCurve);
    }

    private static HkbEdge ReadEdge(IntPtr handle, int index)
    {
        HkxBehaviorEdgeInfo info;
        Call.Check(RadicleNative.HkxBehaviorEdgeReadInfo(handle, (nuint)index, &info));
        return new HkbEdge(checked((int)info.Parent), info.Child, (HkbEdgeRole)info.Role, info.Slot, info.Weight,
                           info.HasBoneWeights != 0);
    }

    private static HkbEvent ReadEvent(IntPtr handle, int index)
    {
        HkxBehaviorEventInfo info;
        Call.Check(RadicleNative.HkxBehaviorEventReadInfo(handle, (nuint)index, &info));
        return new HkbEvent(Text(handle, index, RadicleNative.HkxBehaviorEventName), info.Flags);
    }

    private static HkbVariable ReadVariable(IntPtr handle, int index)
    {
        HkxBehaviorVariableInfo info;
        Call.Check(RadicleNative.HkxBehaviorVariableReadInfo(handle, (nuint)index, &info));
        Numerics.Vector4? vector = info.HasVector != 0
            ? new Numerics.Vector4(info.Vector[0], info.Vector[1], info.Vector[2], info.Vector[3])
            : null;
        return new HkbVariable(Text(handle, index, RadicleNative.HkxBehaviorVariableName), info.Type, info.Role,
                               info.RoleFlags, info.Word, info.IntValue, info.RealValue, vector);
    }

    private static HkbNode ReadNode(IntPtr handle, int index)
    {
        HkxBehaviorNodeInfo info;
        Call.Check(RadicleNative.HkxBehaviorNodeReadInfo(handle, (nuint)index, &info));
        return new HkbNode(checked((int)info.Object), Text(handle, index, RadicleNative.HkxBehaviorNodeClass),
                           Text(handle, index, RadicleNative.HkxBehaviorNodeName), (HkbNodeKind)info.Kind,
                           checked((int)info.KindIndex));
    }

    private static HkbStateMachine ReadStateMachine(IntPtr handle, int index)
    {
        HkxBehaviorStateMachineInfo info;
        Call.Check(RadicleNative.HkxBehaviorStateMachineReadInfo(handle, (nuint)index, &info));
        return new HkbStateMachine(checked((int)info.Node), info.StartStateId, info.ChangeEventId,
                                   info.ReturnToPreviousEventId, info.RandomTransitionEventId, info.NextHigherEventId,
                                   info.NextLowerEventId, info.SyncVariableIndex, info.StartStateMode,
                                   info.SelfTransitionMode, checked((int)info.FirstState),
                                   checked((int)info.StateCount), checked((int)info.FirstWildcard),
                                   checked((int)info.WildcardCount));
    }

    private static HkbState ReadState(IntPtr handle, int index)
    {
        HkxBehaviorStateInfo info;
        Call.Check(RadicleNative.HkxBehaviorStateReadInfo(handle, (nuint)index, &info));
        return new HkbState(checked((int)info.StateMachine), Text(handle, index, RadicleNative.HkxBehaviorStateName),
                            info.StateId, info.Probability, info.Enable != 0, info.GeneratorNode,
                            checked((int)info.FirstTransition), checked((int)info.TransitionCount));
    }

    private static HkbTransition ReadTransition(IntPtr handle, int index)
    {
        HkxBehaviorTransitionInfo info;
        Call.Check(RadicleNative.HkxBehaviorTransitionReadInfo(handle, (nuint)index, &info));
        return new HkbTransition(
            checked((int)info.StateMachine), info.State, info.EventId, info.ToStateId, info.FromNestedStateId,
            info.ToNestedStateId, info.Priority, info.Flags,
            new HkbTimeInterval(info.TriggerEnterEventId, info.TriggerExitEventId, info.TriggerEnterTime,
                                info.TriggerExitTime),
            new HkbTimeInterval(info.InitiateEnterEventId, info.InitiateExitEventId, info.InitiateEnterTime,
                                info.InitiateExitTime),
            info.EffectNode, (HkbConditionKind)info.ConditionKind,
            Text(handle, index, RadicleNative.HkxBehaviorTransitionCondition),
            Text(handle, index, RadicleNative.HkxBehaviorTransitionConditionClass));
    }

    private static HkbBlender ReadBlender(IntPtr handle, int index)
    {
        HkxBehaviorBlenderInfo info;
        Call.Check(RadicleNative.HkxBehaviorBlenderReadInfo(handle, (nuint)index, &info));
        return new HkbBlender(checked((int)info.Node), info.BlendParameter, info.MinCyclicBlendParameter,
                              info.MaxCyclicBlendParameter, info.IndexOfSyncMasterChild, info.Flags,
                              info.Parametric != 0, info.SubtractLastChild != 0, info.BlendParameterVariable,
                              info.Resolved != 0, checked((int)info.FirstChild), checked((int)info.ChildCount));
    }

    private static HkbBlendChild ReadBlendChild(IntPtr handle, int index)
    {
        HkxBehaviorBlendChildInfo info;
        Call.Check(RadicleNative.HkxBehaviorBlendChildReadInfo(handle, (nuint)index, &info));
        return new HkbBlendChild(checked((int)info.Blender), info.GeneratorNode, info.Weight,
                                 info.WorldFromModelWeight, info.HasBoneWeights != 0, info.WeightVariable,
                                 info.Contribution);
    }

    private static HkbClip ReadClip(IntPtr handle, int index)
    {
        HkxBehaviorClipInfo info;
        Call.Check(RadicleNative.HkxBehaviorClipReadInfo(handle, (nuint)index, &info));
        var triggers = new HkbClipTrigger[checked((int)info.TriggerCount)];
        for (var trigger = 0; trigger < triggers.Length; ++trigger)
        {
            HkxBehaviorClipTriggerInfo row;
            Call.Check(RadicleNative.HkxBehaviorClipTriggerReadInfo(handle, (nuint)index, (nuint)trigger, &row));
            triggers[trigger] = new HkbClipTrigger(row.LocalTime, row.EventId, row.RelativeToEndOfClip != 0,
                                                   row.Acyclic != 0, row.IsAnnotation != 0);
        }
        return new HkbClip(checked((int)info.Node), Text(handle, index, RadicleNative.HkxBehaviorClipAnimation),
                           Text(handle, index, RadicleNative.HkxBehaviorClipBundle), info.Mode, info.Flags,
                           info.StartTime, info.PlaybackSpeed, info.CropStart, info.CropEnd, info.EnforcedDuration,
                           triggers);
    }

    private static HkbBinding ReadBinding(IntPtr handle, int index)
    {
        HkxBehaviorBindingInfo info;
        Call.Check(RadicleNative.HkxBehaviorBindingReadInfo(handle, (nuint)index, &info));
        return new HkbBinding((HkbOwnerKind)info.OwnerKind, checked((int)info.OwnerIndex),
                              Text(handle, index, RadicleNative.HkxBehaviorBindingMemberPath), info.VariableIndex,
                              info.BitIndex, info.BindingType);
    }
}
