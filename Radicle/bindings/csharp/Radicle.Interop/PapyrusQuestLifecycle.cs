using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record QuestLifecycleProperty(
    string Script,
    string Name,
    string Type,
    bool HasAlias = false,
    string? Text = null,
    double? Real = null,
    long? Integer = null,
    bool? Boolean = null,
    string? Object = null);

public sealed record QuestStartupInput(
    string Quest,
    bool StartGameEnabled,
    IReadOnlyList<string> Scripts,
    int Fragments = 0,
    int Aliases = 0,
    IReadOnlyList<string>? Unsupported = null,
    IReadOnlyList<QuestLifecycleProperty>? Properties = null);

public sealed record QuestStartupPlan(int Ready, int Blocked, int Skipped, string Report, string Digest);

public sealed record QuestStartupResult(bool Applied, int Started, int AttachedScripts, IReadOnlyList<string> Reasons);

public sealed record AliasScriptInput(
    string Quest,
    int AliasId,
    string? Script,
    IReadOnlyList<QuestLifecycleProperty>? Properties = null,
    IReadOnlyList<int>? Objectives = null);

public sealed record AliasAttachReport(
    int Attached, int Skipped, int Refused, int Properties, int Natives, int Objectives, string Report, string Digest);

public sealed record ActorAliasInput(AliasScriptInput Alias, bool Bound, string? Target);

public sealed record ActorBindReport(int Bound, int Unbound, int Attached, int Refused, string Report, string Digest);

public sealed record QuestStageInput(int Index, bool StartupStage, int Fragments = 0);

public sealed record QuestStageSet(string Quest, IReadOnlyList<QuestStageInput> Stages);

public enum StartupStageState
{
    Identified = 0,
    Unidentified = 1,
    Ambiguous = 2,
    BlockedByFragments = 3,
}

public sealed record StartupStageOutcome(string Quest, StartupStageState State, int? Stage, string Reason);

public sealed record StartupStageReport(
    IReadOnlyList<StartupStageOutcome> Outcomes, int Identified, int Refused, int Unresolved, string Report, string Digest);

public sealed record QuestLifecycleCounters(int Started, int Blocked, int Skipped, int AttachedScripts, int Approximations);

public readonly record struct EventOwner(Native.PapyrusOwnerKind Kind, string BaseFormKey, string? Discriminator = null)
{
    public static EventOwner Form(string baseFormKey) => new(Native.PapyrusOwnerKind.Form, baseFormKey);

    public static EventOwner QuestAlias(string quest, int aliasId) =>
        new(Native.PapyrusOwnerKind.QuestAlias, quest, aliasId.ToString(System.Globalization.CultureInfo.InvariantCulture));
}

public enum EventFanOutResult
{
    Delivered = 0,
    Refused = 1,
    SkippedParked = 2,
    NoHandler = 3,
    UnknownOwner = 4,
}

public sealed record EventFanOutOutcome(string OwnerKey, ulong Instance, string Script, EventFanOutResult Result, string Reason);

public sealed record EventFanOutReport(
    string Handler,
    IReadOnlyList<EventFanOutOutcome> Outcomes,
    int Delivered,
    int Refused,
    int SkippedParked,
    int NoHandler,
    int UnknownOwner,
    string Report,
    string Digest);

// Requires: single-thread use; pex files outlive this object and a shared PapyrusRuntime outlives it (dispose the lifecycle first).
public sealed unsafe class PapyrusQuestLifecycle : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int GateCallback(IntPtr user, IntPtr key);

    private readonly IReadOnlyList<PexFile> _pex;
    private readonly GateCallback? _gate;
    private readonly PapyrusRuntime? _shared;
    private readonly List<HandleUse> _holds = new();
    private readonly PapyrusRuntimeHandle? _runtimeOwner;
    private readonly PapyrusLifecycleHandle? _lifecycleOwner;
    private PapyrusRuntimeHandle? _runtime;
    private PapyrusLifecycleHandle? _lifecycle;

    public PapyrusQuestLifecycle(PapyrusRuntime runtime)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        _pex = Array.Empty<PexFile>();
        _shared = runtime;
        IntPtr lifecycle = IntPtr.Zero;
        var owner = runtime.Handle;
        using (var runtimeUse = new HandleUse(owner, nameof(PapyrusRuntime)))
        {
            Call.Check(RadicleNative.PapyrusLifecycleCreate(runtimeUse.Pointer, &lifecycle));
            _lifecycle = new PapyrusLifecycleHandle(lifecycle, owner);
        }
        _runtimeOwner = owner;
        _lifecycleOwner = _lifecycle;
    }

    public PapyrusQuestLifecycle(IEnumerable<PexFile> pex, Func<string, bool>? gate = null)
    {
        _pex = pex.ToArray();
        PapyrusRuntimeCallbacks callbacks = default;
        if (gate is not null)
        {
            _gate = (_, key) => gate(Utf8.Read(key) ?? string.Empty) ? 1 : 0;
            callbacks.Gate = Marshal.GetFunctionPointerForDelegate(_gate);
        }
        IntPtr runtime = IntPtr.Zero;
        Call.Check(RadicleNative.PapyrusRuntimeCreate(&callbacks, &runtime));
        _runtime = new PapyrusRuntimeHandle(runtime);
        _runtimeOwner = _runtime;
        try
        {
            foreach (var file in _pex)
            {
                var hold = file.Use();
                try { Call.Check(RadicleNative.PapyrusRuntimeAddPex(runtime, hold.Pointer)); }
                catch
                {
                    hold.Dispose();
                    throw;
                }
                _holds.Add(hold);
            }
            IntPtr lifecycle = IntPtr.Zero;
            Call.Check(RadicleNative.PapyrusLifecycleCreate(runtime, &lifecycle));
            _lifecycle = new PapyrusLifecycleHandle(lifecycle, _runtime);
            _lifecycleOwner = _lifecycle;
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    public QuestStartupPlan Preflight(IReadOnlyList<QuestStartupInput> inputs)
    {
        using var handles = UseHandles();
        using var pins = new NativePins();
        var native = inputs.Select(input => NativeStartup(input, pins)).ToArray();
        PapyrusQuestStartupPlan plan = default;
        Status status;
        fixed (PapyrusQuestStartupInput* items = native)
        {
            status = RadicleNative.PapyrusLifecyclePreflight(handles.Lifecycle, items, (nuint)native.Length, &plan);
        }
        try
        {
            Check(status);
            return new QuestStartupPlan(plan.Ready, plan.Blocked, plan.Skipped, Utf8.Read(plan.Report) ?? string.Empty, Utf8.Read(plan.Digest) ?? string.Empty);
        }
        finally { RadicleNative.PapyrusQuestStartupPlanDestroy(&plan); }
    }

    public QuestStartupResult Start(IReadOnlyList<QuestStartupInput> inputs)
    {
        using var handles = UseHandles();
        using var pins = new NativePins();
        var native = inputs.Select(input => NativeStartup(input, pins)).ToArray();
        PapyrusQuestStartupResult outcome = default;
        Status status;
        fixed (PapyrusQuestStartupInput* items = native)
        {
            status = RadicleNative.PapyrusLifecycleStart(handles.Lifecycle, items, (nuint)native.Length, &outcome);
        }
        try
        {
            Check(status);
            var reasons = new string[checked((int)outcome.ReasonCount)];
            var raw = (IntPtr*)outcome.Reasons;
            for (var i = 0; i < reasons.Length; ++i) reasons[i] = Utf8.Read(raw[i]) ?? string.Empty;
            return new QuestStartupResult(outcome.Applied != 0, outcome.Started, outcome.AttachedScripts, reasons);
        }
        finally { RadicleNative.PapyrusQuestStartupResultDestroy(&outcome); }
    }

    public bool Stop(string quest)
    {
        using var handles = UseHandles();
        int wasStarted = 0;
        Check(RadicleNative.PapyrusLifecycleStop(handles.Lifecycle, quest, &wasStarted));
        return wasStarted != 0;
    }

    public bool IsStarted(string quest)
    {
        using var handles = UseHandles();
        return RadicleNative.PapyrusLifecycleIsStarted(handles.Lifecycle, quest) != 0;
    }

    public IReadOnlyList<ulong> InstancesFor(string quest)
    {
        using var handles = UseHandles();
        var count = (int)RadicleNative.PapyrusLifecycleInstanceCount(handles.Lifecycle, quest);
        var result = new List<ulong>(count);
        for (var i = 0; i < count; ++i)
        {
            ulong instance = 0;
            if (RadicleNative.PapyrusLifecycleInstanceAt(handles.Lifecycle, quest, (nuint)i, &instance) != 0) result.Add(instance);
        }
        return result;
    }

    public QuestLifecycleCounters Counters()
    {
        using var handles = UseHandles();
        PapyrusLifecycleCounters counters = default;
        Call.Check(RadicleNative.PapyrusLifecycleGetCounters(handles.Lifecycle, &counters));
        return new QuestLifecycleCounters(counters.Started, counters.Blocked, counters.Skipped, counters.AttachedScripts, counters.Approximations);
    }

    public AliasAttachReport AttachAliases(IReadOnlyList<AliasScriptInput> aliases)
    {
        using var handles = UseHandles();
        using var pins = new NativePins();
        var native = aliases.Select(alias => NativeAlias(alias, pins)).ToArray();
        PapyrusAliasAttachReport report = default;
        Status status;
        fixed (PapyrusAliasScriptInput* items = native)
        {
            status = RadicleNative.PapyrusLifecycleAttachAliases(handles.Lifecycle, items, (nuint)native.Length, &report);
        }
        try
        {
            Check(status);
            return new AliasAttachReport(report.Attached, report.Skipped, report.Refused, report.Properties, report.Natives,
                report.Objectives, Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
        }
        finally { RadicleNative.PapyrusAliasAttachReportDestroy(&report); }
    }

    public ActorBindReport BindActors(IReadOnlyList<ActorAliasInput> aliases)
    {
        using var handles = UseHandles();
        using var pins = new NativePins();
        var native = aliases.Select(alias => new PapyrusActorAliasInput
        {
            Alias = NativeAlias(alias.Alias, pins),
            Bound = alias.Bound ? 1 : 0,
            Target = pins.Strings.Add(alias.Target),
        }).ToArray();
        PapyrusActorBindReport report = default;
        Status status;
        fixed (PapyrusActorAliasInput* items = native)
        {
            status = RadicleNative.PapyrusLifecycleBindActors(handles.Lifecycle, items, (nuint)native.Length, &report);
        }
        try
        {
            Check(status);
            return new ActorBindReport(report.Bound, report.Unbound, report.Attached, report.Refused,
                Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
        }
        finally { RadicleNative.PapyrusActorBindReportDestroy(&report); }
    }

    public bool ClearAlias(string quest, int aliasId)
    {
        using var handles = UseHandles();
        int cleared = 0;
        Check(RadicleNative.PapyrusLifecycleClearAlias(handles.Lifecycle, quest, aliasId, &cleared));
        return cleared != 0;
    }

    public int ClearQuestAliases(string quest)
    {
        using var handles = UseHandles();
        int cleared = 0;
        Check(RadicleNative.PapyrusLifecycleClearQuestAliases(handles.Lifecycle, quest, &cleared));
        return cleared;
    }

    public ulong? AliasInstance(string quest, int aliasId)
    {
        using var handles = UseHandles();
        ulong instance = 0;
        return RadicleNative.PapyrusLifecycleAliasInstance(handles.Lifecycle, quest, aliasId, &instance) != 0 ? instance : null;
    }

    public int ObjectiveOf(string quest, int aliasId, int index)
    {
        using var handles = UseHandles();
        return RadicleNative.PapyrusLifecycleObjective(handles.Lifecycle, quest, aliasId, index);
    }

    public EventFanOutReport FanOut(EventOwner owner, string eventName, params object?[] arguments)
    {
        using var handles = UseHandles();
        using var values = new NativeValues(arguments);
        PapyrusFanoutReport report = default;
        Status status;
        fixed (IntPtr* args = values.Handles)
        {
            status = RadicleNative.PapyrusRuntimeFanOutEvent(handles.Runtime, owner.Kind, owner.BaseFormKey, owner.Discriminator,
                eventName, args, (nuint)values.Handles.Length, &report);
        }
        return TakeFanOut(status, &report);
    }

    public EventFanOutReport Broadcast(string eventName, params object?[] arguments)
    {
        using var handles = UseHandles();
        using var values = new NativeValues(arguments);
        PapyrusFanoutReport report = default;
        Status status;
        fixed (IntPtr* args = values.Handles)
        {
            status = RadicleNative.PapyrusRuntimeBroadcastEvent(handles.Runtime, eventName, args, (nuint)values.Handles.Length, &report);
        }
        return TakeFanOut(status, &report);
    }

    public void ParkOwner(EventOwner owner, bool parked)
    {
        using var handles = UseHandles();
        Check(RadicleNative.PapyrusRuntimeParkOwner(handles.Runtime, owner.Kind, owner.BaseFormKey, owner.Discriminator, parked ? 1 : 0));
    }

    public PapyrusStepResult Step(int budget)
    {
        using var handles = UseHandles();
        PapyrusStepResult result = default;
        Check(RadicleNative.PapyrusRuntimeStep(handles.Runtime, budget, &result));
        return result;
    }

    public void Dispose()
    {
        _lifecycle?.Dispose();
        if (_runtime is not null)
        {
            var runtime = _runtime;
            runtime.AfterRelease = ReleaseOwnedResources;
            runtime.Dispose();
        }
    }

    public static StartupStageReport IdentifyStartupStages(IReadOnlyList<QuestStageSet> quests)
    {
        ArgumentNullException.ThrowIfNull(quests);
        using var pins = new NativePins();
        var native = quests.Select(quest =>
        {
            var stages = quest.Stages.Select(stage => new PapyrusStageInput
            {
                Index = stage.Index,
                StartupStage = stage.StartupStage ? 1 : 0,
                Fragments = stage.Fragments,
            }).ToArray();
            return new PapyrusQuestStages { Quest = pins.Strings.Add(quest.Quest), Stages = pins.Pin(stages), StageCount = (nuint)stages.Length };
        }).ToArray();
        PapyrusStageReport report = default;
        fixed (PapyrusQuestStages* items = native)
        {
            Call.Check(RadicleNative.PapyrusIdentifyStartupStages(items, (nuint)native.Length, &report));
        }
        try
        {
            var raw = (PapyrusStageOutcome*)report.Outcomes;
            var outcomes = new List<StartupStageOutcome>((int)report.Count);
            for (var i = 0; i < (int)report.Count; ++i)
            {
                outcomes.Add(new StartupStageOutcome(Utf8.Read(raw[i].Quest) ?? string.Empty, (StartupStageState)raw[i].State,
                    raw[i].HasStage != 0 ? raw[i].Stage : null, Utf8.Read(raw[i].Reason) ?? string.Empty));
            }
            return new StartupStageReport(outcomes, report.Identified, report.Refused, report.Unresolved,
                Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
        }
        finally { RadicleNative.PapyrusStageReportDestroy(&report); }
    }

    private void Check(Status status)
    {
        if (_shared is null) Call.Check(status);
        else _shared.CheckAfterCallbacks(status);
    }

    private HandlePair UseHandles()
    {
        var lifecycle = _lifecycleOwner ?? throw new ObjectDisposedException(nameof(PapyrusQuestLifecycle));
        var runtime = _runtimeOwner ?? throw new ObjectDisposedException(nameof(PapyrusQuestLifecycle));
        return new HandlePair(this, lifecycle, runtime);
    }

    private void ReleaseOwnedResources()
    {
        foreach (var hold in _holds) hold.Dispose();
        _holds.Clear();
        GC.KeepAlive(_pex);
        GC.KeepAlive(_gate);
    }

    private readonly struct HandlePair : IDisposable
    {
        private readonly PapyrusQuestLifecycle _owner;
        private readonly HandleUse _lifecycleUse;
        private readonly HandleUse _runtimeUse;

        public HandlePair(PapyrusQuestLifecycle owner, PapyrusLifecycleHandle lifecycle, PapyrusRuntimeHandle runtime)
        {
            _owner = owner;
            _runtimeUse = new HandleUse(runtime, nameof(PapyrusRuntime));
            try { _lifecycleUse = new HandleUse(lifecycle, nameof(PapyrusQuestLifecycle)); }
            catch
            {
                _runtimeUse.Dispose();
                throw;
            }
            Lifecycle = _lifecycleUse.Pointer;
            Runtime = _runtimeUse.Pointer;
        }

        public IntPtr Lifecycle { get; }
        public IntPtr Runtime { get; }

        public void Dispose()
        {
            _lifecycleUse.Dispose();
            _runtimeUse.Dispose();
            GC.KeepAlive(_owner);
        }
    }

    private EventFanOutReport TakeFanOut(Status status, PapyrusFanoutReport* report)
    {
        try
        {
            Check(status);
            var outcomes = new List<EventFanOutOutcome>((int)report->Count);
            var raw = (PapyrusFanoutOutcome*)report->Outcomes;
            for (var i = 0; i < (int)report->Count; ++i)
            {
                outcomes.Add(new EventFanOutOutcome(Utf8.Read(raw[i].OwnerKey) ?? string.Empty, raw[i].Instance,
                    Utf8.Read(raw[i].Script) ?? string.Empty, (EventFanOutResult)(int)raw[i].Result, Utf8.Read(raw[i].Reason) ?? string.Empty));
            }
            return new EventFanOutReport(Utf8.Read(report->Handler) ?? string.Empty, outcomes, report->Delivered, report->Refused,
                report->SkippedParked, report->NoHandler, report->UnknownOwner, Utf8.Read(report->Report) ?? string.Empty,
                Utf8.Read(report->Digest) ?? string.Empty);
        }
        finally { RadicleNative.PapyrusFanoutReportDestroy(report); }
    }

    private static PapyrusQuestStartupInput NativeStartup(QuestStartupInput input, NativePins pins)
    {
        var properties = (input.Properties ?? Array.Empty<QuestLifecycleProperty>()).Select(p => NativeProperty(p, pins)).ToArray();
        var unsupported = input.Unsupported ?? Array.Empty<string>();
        return new PapyrusQuestStartupInput
        {
            Quest = pins.Strings.Add(input.Quest),
            StartGameEnabled = input.StartGameEnabled ? 1 : 0,
            Scripts = pins.Pin(pins.Strings.AddAll(input.Scripts)),
            ScriptCount = (nuint)input.Scripts.Count,
            Fragments = input.Fragments,
            Aliases = input.Aliases,
            Unsupported = pins.Pin(pins.Strings.AddAll(unsupported)),
            UnsupportedCount = (nuint)unsupported.Count,
            Properties = pins.Pin(properties),
            PropertyCount = (nuint)properties.Length,
        };
    }

    private static PapyrusAliasScriptInput NativeAlias(AliasScriptInput alias, NativePins pins)
    {
        var properties = (alias.Properties ?? Array.Empty<QuestLifecycleProperty>()).Select(p => NativeProperty(p, pins)).ToArray();
        var objectives = (alias.Objectives ?? Array.Empty<int>()).ToArray();
        return new PapyrusAliasScriptInput
        {
            Quest = pins.Strings.Add(alias.Quest),
            AliasId = alias.AliasId,
            Script = pins.Strings.Add(alias.Script),
            Properties = pins.Pin(properties),
            PropertyCount = (nuint)properties.Length,
            Objectives = pins.Pin(objectives),
            ObjectiveCount = (nuint)objectives.Length,
        };
    }

    private static PapyrusLifecycleProperty NativeProperty(QuestLifecycleProperty property, NativePins pins) => new()
    {
        Script = pins.Strings.Add(property.Script),
        Name = pins.Strings.Add(property.Name),
        Type = pins.Strings.Add(property.Type),
        HasAlias = property.HasAlias ? 1 : 0,
        Text = pins.Strings.Add(property.Text),
        HasReal = property.Real.HasValue ? 1 : 0,
        Real = property.Real ?? 0,
        HasInteger = property.Integer.HasValue ? 1 : 0,
        Integer = property.Integer ?? 0,
        HasBoolean = property.Boolean.HasValue ? 1 : 0,
        Boolean = property.Boolean == true ? 1 : 0,
        Object = pins.Strings.Add(property.Object),
    };

    private sealed class NativePins : IDisposable
    {
        private readonly List<GCHandle> _pinned = new();

        public NativeStrings Strings { get; } = new();

        public IntPtr Pin<T>(T[] items) where T : unmanaged
        {
            if (items.Length == 0) return IntPtr.Zero;
            var handle = GCHandle.Alloc(items, GCHandleType.Pinned);
            _pinned.Add(handle);
            return handle.AddrOfPinnedObject();
        }

        public void Dispose()
        {
            foreach (var handle in _pinned) handle.Free();
            _pinned.Clear();
            Strings.Dispose();
        }
    }

    private sealed class NativeValues : IDisposable
    {
        public NativeValues(object?[] arguments)
        {
            Handles = new IntPtr[arguments.Length];
            try
            {
                for (var i = 0; i < arguments.Length; ++i) Handles[i] = Make(arguments[i]);
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        public IntPtr[] Handles { get; }

        public void Dispose()
        {
            foreach (var handle in Handles)
            {
                if (handle != IntPtr.Zero) RadicleNative.PapyrusValueDestroy(handle);
            }
        }

        private static IntPtr Make(object? argument)
        {
            IntPtr value = IntPtr.Zero;
            switch (argument)
            {
                case null:
                    Call.Check(RadicleNative.PapyrusValueNone(&value));
                    break;
                case int number:
                    Call.Check(RadicleNative.PapyrusValueInt(number, &value));
                    break;
                case bool flag:
                    Call.Check(RadicleNative.PapyrusValueBool(flag ? 1 : 0, &value));
                    break;
                case string text:
                    Call.Check(RadicleNative.PapyrusValueString(text, (nuint)System.Text.Encoding.UTF8.GetByteCount(text), &value));
                    break;
                default:
                    throw new ArgumentException($"Unsupported event argument type {argument.GetType().Name}.");
            }
            return value;
        }
    }
}
