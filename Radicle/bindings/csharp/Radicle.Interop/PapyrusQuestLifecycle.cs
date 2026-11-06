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

// Requires: the pex files outlive this object; the lifecycle borrows them and the gate delegate stays rooted here.
public sealed unsafe class PapyrusQuestLifecycle : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int GateCallback(IntPtr user, IntPtr key);

    private readonly IReadOnlyList<PexFile> _pex;
    private readonly GateCallback? _gate;
    private IntPtr _runtime;
    private IntPtr _lifecycle;

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
        _runtime = runtime;
        try
        {
            foreach (var file in _pex)
            {
                using var use = file.Use();
                Call.Check(RadicleNative.PapyrusRuntimeAddPex(_runtime, use.Pointer));
            }
            IntPtr lifecycle = IntPtr.Zero;
            Call.Check(RadicleNative.PapyrusLifecycleCreate(_runtime, &lifecycle));
            _lifecycle = lifecycle;
        }
        catch
        {
            Dispose();
            throw;
        }
    }

    public QuestStartupPlan Preflight(IReadOnlyList<QuestStartupInput> inputs)
    {
        using var pins = new NativePins();
        var native = inputs.Select(input => NativeStartup(input, pins)).ToArray();
        PapyrusQuestStartupPlan plan = default;
        fixed (PapyrusQuestStartupInput* items = native)
        {
            Call.Check(RadicleNative.PapyrusLifecyclePreflight(Lifecycle, items, (nuint)native.Length, &plan));
        }
        var result = new QuestStartupPlan(plan.Ready, plan.Blocked, plan.Skipped, Utf8.Read(plan.Report) ?? string.Empty, Utf8.Read(plan.Digest) ?? string.Empty);
        RadicleNative.PapyrusQuestStartupPlanDestroy(&plan);
        return result;
    }

    public QuestStartupResult Start(IReadOnlyList<QuestStartupInput> inputs)
    {
        using var pins = new NativePins();
        var native = inputs.Select(input => NativeStartup(input, pins)).ToArray();
        PapyrusQuestStartupResult outcome = default;
        fixed (PapyrusQuestStartupInput* items = native)
        {
            Call.Check(RadicleNative.PapyrusLifecycleStart(Lifecycle, items, (nuint)native.Length, &outcome));
        }
        var reasons = new string[checked((int)outcome.ReasonCount)];
        var raw = (IntPtr*)outcome.Reasons;
        for (var i = 0; i < reasons.Length; ++i) reasons[i] = Utf8.Read(raw[i]) ?? string.Empty;
        var result = new QuestStartupResult(outcome.Applied != 0, outcome.Started, outcome.AttachedScripts, reasons);
        RadicleNative.PapyrusQuestStartupResultDestroy(&outcome);
        return result;
    }

    public bool Stop(string quest)
    {
        int wasStarted = 0;
        Call.Check(RadicleNative.PapyrusLifecycleStop(Lifecycle, quest, &wasStarted));
        return wasStarted != 0;
    }

    public bool IsStarted(string quest) => RadicleNative.PapyrusLifecycleIsStarted(Lifecycle, quest) != 0;

    public IReadOnlyList<ulong> InstancesFor(string quest)
    {
        var count = (int)RadicleNative.PapyrusLifecycleInstanceCount(Lifecycle, quest);
        var result = new List<ulong>(count);
        for (var i = 0; i < count; ++i)
        {
            ulong instance = 0;
            if (RadicleNative.PapyrusLifecycleInstanceAt(Lifecycle, quest, (nuint)i, &instance) != 0) result.Add(instance);
        }
        return result;
    }

    public QuestLifecycleCounters Counters()
    {
        PapyrusLifecycleCounters counters = default;
        Call.Check(RadicleNative.PapyrusLifecycleGetCounters(Lifecycle, &counters));
        return new QuestLifecycleCounters(counters.Started, counters.Blocked, counters.Skipped, counters.AttachedScripts, counters.Approximations);
    }

    public AliasAttachReport AttachAliases(IReadOnlyList<AliasScriptInput> aliases)
    {
        using var pins = new NativePins();
        var native = aliases.Select(alias => NativeAlias(alias, pins)).ToArray();
        PapyrusAliasAttachReport report = default;
        fixed (PapyrusAliasScriptInput* items = native)
        {
            Call.Check(RadicleNative.PapyrusLifecycleAttachAliases(Lifecycle, items, (nuint)native.Length, &report));
        }
        var result = new AliasAttachReport(report.Attached, report.Skipped, report.Refused, report.Properties, report.Natives,
            report.Objectives, Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
        RadicleNative.PapyrusAliasAttachReportDestroy(&report);
        return result;
    }

    public ActorBindReport BindActors(IReadOnlyList<ActorAliasInput> aliases)
    {
        using var pins = new NativePins();
        var native = aliases.Select(alias => new PapyrusActorAliasInput
        {
            Alias = NativeAlias(alias.Alias, pins),
            Bound = alias.Bound ? 1 : 0,
            Target = pins.Strings.Add(alias.Target),
        }).ToArray();
        PapyrusActorBindReport report = default;
        fixed (PapyrusActorAliasInput* items = native)
        {
            Call.Check(RadicleNative.PapyrusLifecycleBindActors(Lifecycle, items, (nuint)native.Length, &report));
        }
        var result = new ActorBindReport(report.Bound, report.Unbound, report.Attached, report.Refused,
            Utf8.Read(report.Report) ?? string.Empty, Utf8.Read(report.Digest) ?? string.Empty);
        RadicleNative.PapyrusActorBindReportDestroy(&report);
        return result;
    }

    public bool ClearAlias(string quest, int aliasId)
    {
        int cleared = 0;
        Call.Check(RadicleNative.PapyrusLifecycleClearAlias(Lifecycle, quest, aliasId, &cleared));
        return cleared != 0;
    }

    public int ClearQuestAliases(string quest)
    {
        int cleared = 0;
        Call.Check(RadicleNative.PapyrusLifecycleClearQuestAliases(Lifecycle, quest, &cleared));
        return cleared;
    }

    public ulong? AliasInstance(string quest, int aliasId)
    {
        ulong instance = 0;
        return RadicleNative.PapyrusLifecycleAliasInstance(Lifecycle, quest, aliasId, &instance) != 0 ? instance : null;
    }

    public int ObjectiveOf(string quest, int aliasId, int index) =>
        RadicleNative.PapyrusLifecycleObjective(Lifecycle, quest, aliasId, index);

    public EventFanOutReport FanOut(EventOwner owner, string eventName, params object?[] arguments)
    {
        using var values = new NativeValues(arguments);
        PapyrusFanoutReport report = default;
        fixed (IntPtr* args = values.Handles)
        {
            Call.Check(RadicleNative.PapyrusRuntimeFanOutEvent(Runtime, owner.Kind, owner.BaseFormKey, owner.Discriminator,
                eventName, args, (nuint)values.Handles.Length, &report));
        }
        return TakeFanOut(&report);
    }

    public EventFanOutReport Broadcast(string eventName, params object?[] arguments)
    {
        using var values = new NativeValues(arguments);
        PapyrusFanoutReport report = default;
        fixed (IntPtr* args = values.Handles)
        {
            Call.Check(RadicleNative.PapyrusRuntimeBroadcastEvent(Runtime, eventName, args, (nuint)values.Handles.Length, &report));
        }
        return TakeFanOut(&report);
    }

    public void ParkOwner(EventOwner owner, bool parked) =>
        Call.Check(RadicleNative.PapyrusRuntimeParkOwner(Runtime, owner.Kind, owner.BaseFormKey, owner.Discriminator, parked ? 1 : 0));

    public PapyrusStepResult Step(int budget)
    {
        PapyrusStepResult result = default;
        Call.Check(RadicleNative.PapyrusRuntimeStep(Runtime, budget, &result));
        return result;
    }

    public void Dispose()
    {
        if (_lifecycle != IntPtr.Zero) RadicleNative.PapyrusLifecycleDestroy(_lifecycle);
        if (_runtime != IntPtr.Zero) RadicleNative.PapyrusRuntimeDestroy(_runtime);
        _lifecycle = IntPtr.Zero;
        _runtime = IntPtr.Zero;
    }

    private IntPtr Lifecycle => _lifecycle != IntPtr.Zero ? _lifecycle : throw new ObjectDisposedException(nameof(PapyrusQuestLifecycle));

    private IntPtr Runtime => _runtime != IntPtr.Zero ? _runtime : throw new ObjectDisposedException(nameof(PapyrusQuestLifecycle));

    private static EventFanOutReport TakeFanOut(PapyrusFanoutReport* report)
    {
        var outcomes = new List<EventFanOutOutcome>((int)report->Count);
        var raw = (PapyrusFanoutOutcome*)report->Outcomes;
        for (var i = 0; i < (int)report->Count; ++i)
        {
            outcomes.Add(new EventFanOutOutcome(Utf8.Read(raw[i].OwnerKey) ?? string.Empty, raw[i].Instance,
                Utf8.Read(raw[i].Script) ?? string.Empty, (EventFanOutResult)(int)raw[i].Result, Utf8.Read(raw[i].Reason) ?? string.Empty));
        }
        var result = new EventFanOutReport(Utf8.Read(report->Handler) ?? string.Empty, outcomes, report->Delivered, report->Refused,
            report->SkippedParked, report->NoHandler, report->UnknownOwner, Utf8.Read(report->Report) ?? string.Empty,
            Utf8.Read(report->Digest) ?? string.Empty);
        RadicleNative.PapyrusFanoutReportDestroy(report);
        return result;
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
