using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record PapyrusOwner(PapyrusOwnerKind Kind, string BaseFormKey, string Discriminator = "");

public sealed record PapyrusObjectRef(ulong Handle, string Type, string State);

// Constraint: struct values are write-only here; member names are ASCII-case-insensitive.
public sealed record PapyrusStructValue(string TypeName, IReadOnlyDictionary<string, object?> Members);

public readonly record struct PapyrusRemoteTarget(long Handle, ulong Instance);

public delegate bool PapyrusNativeHandler(string script, string function, IReadOnlyList<object?> arguments, out object? result);

public delegate bool PapyrusOverrideHandler(ulong instance, string function, IReadOnlyList<object?> arguments,
    out PapyrusOverrideOutcome outcome, out object? result);

public delegate long PapyrusLatentHandler(string script, string function, IReadOnlyList<object?> arguments);

public sealed record PapyrusHostCallbacks(
    Func<string, bool>? Gate = null,
    PapyrusNativeHandler? CallNative = null,
    PapyrusOverrideHandler? Override = null,
    PapyrusLatentHandler? BeginLatent = null);

public sealed record PapyrusEngineHandlers(
    Func<PapyrusNativeCall, bool> CallNative,
    Func<PapyrusObjectRef, string, (bool Handled, object? Value)>? GetProperty = null,
    Func<PapyrusObjectRef, string, object?, bool>? SetProperty = null);

public sealed record PapyrusCallResult(bool Found, PapyrusCallState State, PapyrusStatus Status, ulong Instructions, object? Value, string? Detail = null);

public static class PapyrusStatusExtensions
{
    public static string Name(this PapyrusStatus status) => Utf8.Read(RadicleNative.PapyrusStatusName(status)) ?? "Unknown";
}

// Invariant: valid only inside the engine native callback that received it; later use throws.
public sealed unsafe class PapyrusNativeCall
{
    private readonly IntPtr _call;
    private IReadOnlyList<object?>? _args;
    private bool _open = true;

    internal PapyrusNativeCall(IntPtr call)
    {
        _call = call;
        Script = Utf8.Read(RadicleNative.PapyrusNativeCallScript(call)) ?? string.Empty;
        Function = Utf8.Read(RadicleNative.PapyrusNativeCallFunction(call)) ?? string.Empty;
        var self = RadicleNative.PapyrusNativeCallSelf(call);
        SelfRef = self == IntPtr.Zero ? null : PapyrusValues.ReadObject(self);
    }

    public string Script { get; }

    public string Function { get; }

    public PapyrusObjectRef? SelfRef { get; }

    public IReadOnlyList<object?> Args
    {
        get
        {
            Require();
            return _args ??= ReadArgs();
        }
    }

    public bool SetResult(object? value)
    {
        Require();
        var made = PapyrusValues.Make(value);
        try { return RadicleNative.PapyrusNativeCallSetResult(_call, made) == Status.Ok; }
        finally { RadicleNative.PapyrusValueDestroy(made); }
    }

    public bool TryBeginWait(out ulong token)
    {
        Require();
        ulong raw = 0;
        var status = RadicleNative.PapyrusNativeCallBeginWait(_call, &raw);
        token = raw;
        return status == Status.Ok;
    }

    public void Fail(PapyrusStatus status)
    {
        Require();
        Call.Check(RadicleNative.PapyrusNativeCallFail(_call, status));
    }

    internal void Close() => _open = false;

    private void Require()
    {
        if (!_open) throw new InvalidOperationException("PapyrusNativeCall is only valid inside its native callback.");
    }

    private object?[] ReadArgs()
    {
        var count = checked((int)RadicleNative.PapyrusNativeCallArgCount(_call));
        var result = new object?[count];
        for (var i = 0; i < count; ++i) result[i] = PapyrusValues.Read(RadicleNative.PapyrusNativeCallArg(_call, (nuint)i));
        return result;
    }
}

// Constraint: struct values can be sent but cannot be read through PapyrusRuntime.
internal static unsafe class PapyrusValues
{
    public static IntPtr Make(object? value)
    {
        IntPtr made = IntPtr.Zero;
        switch (value)
        {
            case null:
                Call.Check(RadicleNative.PapyrusValueNone(&made));
                break;
            case int number:
                Call.Check(RadicleNative.PapyrusValueInt(number, &made));
                break;
            case bool flag:
                Call.Check(RadicleNative.PapyrusValueBool(flag ? 1 : 0, &made));
                break;
            case float real:
                Call.Check(RadicleNative.PapyrusValueFloat(real, &made));
                break;
            case double real:
                Call.Check(RadicleNative.PapyrusValueFloat((float)real, &made));
                break;
            case string text:
                Call.Check(RadicleNative.PapyrusValueString(text, (nuint)Encoding.UTF8.GetByteCount(text), &made));
                break;
            case PapyrusObjectRef reference:
                Call.Check(RadicleNative.PapyrusValueObject(reference.Handle, reference.Type, reference.State, &made));
                break;
            case IReadOnlyList<object?> items:
                made = MakeArray(items);
                break;
            case PapyrusStructValue structure:
                made = MakeStruct(structure);
                break;
            default:
                throw new ArgumentException($"Unsupported Papyrus value type {value.GetType().Name}.", nameof(value));
        }
        return made;
    }

    public static object? Read(IntPtr value)
    {
        switch (RadicleNative.PapyrusValueKindOf(value))
        {
            case PapyrusValueKind.None:
                return null;
            case PapyrusValueKind.String:
                return Utf8.Read(RadicleNative.PapyrusValueText(value)) ?? string.Empty;
            case PapyrusValueKind.Int:
                return RadicleNative.PapyrusValueIntOf(value);
            case PapyrusValueKind.Float:
                return RadicleNative.PapyrusValueFloatOf(value);
            case PapyrusValueKind.Bool:
                return RadicleNative.PapyrusValueBoolOf(value) != 0;
            case PapyrusValueKind.Object:
                return new PapyrusObjectRef(
                    RadicleNative.PapyrusValueObjectHandle(value),
                    Utf8.Read(RadicleNative.PapyrusValueObjectType(value)) ?? string.Empty,
                    Utf8.Read(RadicleNative.PapyrusValueObjectState(value)) ?? string.Empty);
            case PapyrusValueKind.Array:
                return ReadArray(value);
            default:
                throw new NotSupportedException("Papyrus struct values are not readable through PapyrusRuntime.");
        }
    }

    public static PapyrusObjectRef ReadObject(IntPtr value) => new(
        RadicleNative.PapyrusObjectHandle(value),
        Utf8.Read(RadicleNative.PapyrusObjectType(value)) ?? string.Empty,
        Utf8.Read(RadicleNative.PapyrusObjectState(value)) ?? string.Empty);

    public static object?[] ReadAll(IntPtr values, nuint count)
    {
        var result = new object?[checked((int)count)];
        var raw = (IntPtr*)values;
        for (var i = 0; i < result.Length; ++i) result[i] = Read(raw[i]);
        return result;
    }

    private static IntPtr MakeArray(IReadOnlyList<object?> items)
    {
        IntPtr array = IntPtr.Zero;
        Call.Check(RadicleNative.PapyrusValueArray((nuint)items.Count, &array));
        try
        {
            for (var i = 0; i < items.Count; ++i)
            {
                var element = Make(items[i]);
                try { Call.Check(RadicleNative.PapyrusValueArraySet(array, (nuint)i, element)); }
                finally { RadicleNative.PapyrusValueDestroy(element); }
            }
        }
        catch
        {
            RadicleNative.PapyrusValueDestroy(array);
            throw;
        }
        return array;
    }

    private static IntPtr MakeStruct(PapyrusStructValue structure)
    {
        IntPtr made = IntPtr.Zero;
        Call.Check(RadicleNative.PapyrusValueStruct(structure.TypeName, &made));
        try
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var (name, member) in structure.Members)
            {
                var folded = string.Concat(name.Select(c => c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c));
                if (!names.Add(folded)) throw new ArgumentException($"Duplicate struct member name: {name}.", nameof(structure));
                var element = Make(member);
                try { Call.Check(RadicleNative.PapyrusValueStructSet(made, name, element)); }
                finally { RadicleNative.PapyrusValueDestroy(element); }
            }
        }
        catch
        {
            RadicleNative.PapyrusValueDestroy(made);
            throw;
        }
        return made;
    }

    private static object?[] ReadArray(IntPtr array)
    {
        var result = new object?[checked((int)RadicleNative.PapyrusValueArrayCount(array))];
        for (var i = 0; i < result.Length; ++i)
        {
            IntPtr element = IntPtr.Zero;
            Call.Check(RadicleNative.PapyrusValueArrayGet(array, (nuint)i, &element));
            try { result[i] = Read(element); }
            finally { RadicleNative.PapyrusValueDestroy(element); }
        }
        return result;
    }
}

// Invariant: owns the handles it made and destroys them on Dispose; the runtime copies what it keeps, so they may be freed right after the call.
internal sealed class PapyrusValueSet : IDisposable
{
    public PapyrusValueSet(IReadOnlyList<object?> values)
    {
        Handles = new IntPtr[values.Count];
        try
        {
            for (var i = 0; i < Handles.Length; ++i) Handles[i] = PapyrusValues.Make(values[i]);
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
        for (var i = 0; i < Handles.Length; ++i)
        {
            if (Handles[i] == IntPtr.Zero) continue;
            RadicleNative.PapyrusValueDestroy(Handles[i]);
            Handles[i] = IntPtr.Zero;
        }
    }
}

// Requires: every call and the callbacks it runs happen on one thread; a callback must not re-enter the runtime, since a nested call would consume the outer callback fault.
// Invariant: Dispose destroys the runtime before the pex files it borrows, and callback delegates stay rooted in fields for the runtime's whole life.
public sealed unsafe partial class PapyrusRuntime : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int GateCallback(IntPtr user, IntPtr key);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int NativeCallback(IntPtr user, IntPtr script, IntPtr function, IntPtr args, nuint count, IntPtr outValue);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int OverrideCallback(IntPtr user, ulong instance, IntPtr function, IntPtr args, nuint count, IntPtr outOutcome, IntPtr outValue);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate long LatentCallback(IntPtr user, IntPtr script, IntPtr function, IntPtr args, nuint count);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int EngineCallNativeCallback(IntPtr user, IntPtr call);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int EngineGetPropertyCallback(IntPtr user, IntPtr obj, IntPtr name, IntPtr outValue);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int EngineSetPropertyCallback(IntPtr user, IntPtr obj, IntPtr name, IntPtr value);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int EngineCastHandleCallback(IntPtr user, IntPtr obj, IntPtr targetType, IntPtr outInstance);

    public const ulong EngineHandleBit = 0x8000000000000000UL;

    private readonly List<PexFile> _pex = new();
    private readonly List<PexFile> _owned = new();
    private readonly List<HandleUse> _holds = new();
    private readonly GateCallback? _gate;
    private readonly NativeCallback? _native;
    private readonly OverrideCallback? _override;
    private readonly LatentCallback? _latent;
    private EngineCallNativeCallback? _engineCallNative;
    private EngineGetPropertyCallback? _engineGetProperty;
    private EngineSetPropertyCallback? _engineSetProperty;
    private EngineCastHandleCallback? _engineCastHandle;
    private IntPtr _engineBlock;
    private IntPtr _runtime;
    private int _callbacks;
    private Exception? _fault;

    static PapyrusRuntime() => RadicleLibrary.Register();

    public PapyrusRuntime(PapyrusHostCallbacks? host = null)
    {
        PapyrusRuntimeCallbacks callbacks = default;
        if (host?.Gate is { } gate)
        {
            _gate = (_, key) => Guard(() => gate(Text(key)) ? 1 : 0, 0);
            callbacks.Gate = Marshal.GetFunctionPointerForDelegate(_gate);
        }
        if (host?.CallNative is { } native)
        {
            _native = (_, script, function, args, count, outValue) => Guard(() =>
            {
                if (!native(Text(script), Text(function), PapyrusValues.ReadAll(args, count), out var result)) return 0;
                *(IntPtr*)outValue = PapyrusValues.Make(result);
                return 1;
            }, 0);
            callbacks.CallNative = Marshal.GetFunctionPointerForDelegate(_native);
        }
        if (host?.Override is { } handler)
        {
            _override = (_, instance, function, args, count, outOutcome, outValue) =>
            {
                *(int*)outOutcome = (int)PapyrusOverrideOutcome.Refused;
                *(IntPtr*)outValue = IntPtr.Zero;
                return Guard(() =>
                {
                    if (!handler(instance, Text(function), PapyrusValues.ReadAll(args, count), out var outcome, out var result))
                    {
                        *(int*)outOutcome = (int)PapyrusOverrideOutcome.NotRegistered;
                        return 0;
                    }
                    var made = outcome == PapyrusOverrideOutcome.Handled ? PapyrusValues.Make(result) : IntPtr.Zero;
                    *(IntPtr*)outValue = made;
                    *(int*)outOutcome = (int)outcome;
                    return 1;
                }, 1);
            };
            callbacks.OverrideFunction = Marshal.GetFunctionPointerForDelegate(_override);
        }
        if (host?.BeginLatent is { } latent)
        {
            _latent = (_, script, function, args, count) => Guard(
                () => latent(Text(script), Text(function), PapyrusValues.ReadAll(args, count)), 0L);
            callbacks.BeginLatent = Marshal.GetFunctionPointerForDelegate(_latent);
        }
        IntPtr runtime = IntPtr.Zero;
        Call.Check(RadicleNative.PapyrusRuntimeCreate(&callbacks, &runtime));
        _runtime = runtime;
    }

    public int PexCount => _pex.Count;

    // Invariant: the runtime owns and later disposes the pex it parses here.
    public void AddPex(ReadOnlySpan<byte> pexBytes)
    {
        _ = Runtime;
        var pex = PexFile.FromBytes(pexBytes);
        try { Add(pex); }
        catch
        {
            pex.Dispose();
            throw;
        }
        _owned.Add(pex);
    }

    // Invariant: the runtime holds a reference on pex until Dispose, so the caller may dispose its PexFile first.
    public void AddPex(PexFile pex)
    {
        ArgumentNullException.ThrowIfNull(pex);
        Add(pex);
    }

    public ulong Attach(string script, PapyrusOwner owner)
    {
        ulong instance = 0;
        Call.Check(RadicleNative.PapyrusRuntimeAttach(Runtime, script, owner.Kind, owner.BaseFormKey, owner.Discriminator, &instance));
        return instance;
    }

    public ulong AttachWithProperties(string script, PapyrusOwner owner, IReadOnlyDictionary<string, object?> properties)
    {
        var names = properties.Keys.ToArray();
        var runtime = Runtime;
        using var strings = new NativeStrings();
        var rawNames = names.Select(strings.Add).ToArray();
        using var values = new PapyrusValueSet(names.Select(name => properties[name]).ToArray());
        ulong instance = 0;
        fixed (IntPtr* n = rawNames)
        fixed (IntPtr* v = values.Handles)
            Call.Check(RadicleNative.PapyrusRuntimeAttachWithProperties(runtime, script, owner.Kind, owner.BaseFormKey,
                owner.Discriminator, n, v, (nuint)names.Length, &instance));
        return instance;
    }

    public bool Detach(ulong instance) => RadicleNative.PapyrusRuntimeDetach(Runtime, instance) != 0;

    public PapyrusLifecycle Lifecycle(ulong instance) => RadicleNative.PapyrusRuntimeLifecycle(Runtime, instance);

    public string? Failure(ulong instance)
    {
        var text = Utf8.Read(RadicleNative.PapyrusRuntimeFailure(Runtime, instance));
        return string.IsNullOrEmpty(text) ? null : text;
    }

    public string? InstanceState(ulong instance) => Utf8.Read(RadicleNative.PapyrusRuntimeInstanceState(Runtime, instance));

    public PapyrusStatus Initialize(ulong instance)
    {
        PapyrusStatus status = PapyrusStatus.InternalError;
        CheckAfterCallbacks(RadicleNative.PapyrusRuntimeInitialize(Runtime, instance, &status));
        return status;
    }

    public PapyrusStatus SendEvent(ulong instance, string function, params object?[] arguments)
    {
        using var values = new PapyrusValueSet(arguments ?? Array.Empty<object?>());
        PapyrusStatus status = PapyrusStatus.InternalError;
        fixed (IntPtr* args = values.Handles)
            CheckAfterCallbacks(RadicleNative.PapyrusRuntimeSendEvent(Runtime, instance, function, args, (nuint)values.Handles.Length, &status));
        return status;
    }

    public (ulong Ticket, PapyrusStatus Status) CallFunction(ulong instance, string function, params object?[] arguments)
    {
        using var values = new PapyrusValueSet(arguments ?? Array.Empty<object?>());
        ulong ticket = 0;
        PapyrusStatus status = PapyrusStatus.InternalError;
        fixed (IntPtr* args = values.Handles)
            CheckAfterCallbacks(RadicleNative.PapyrusRuntimeCallFunction(Runtime, instance, function, args, (nuint)values.Handles.Length, &ticket, &status));
        return (ticket, status);
    }

    public PapyrusCallResult CallResult(ulong ticket, bool consume)
    {
        var runtime = Runtime;
        PapyrusCallInfo info = default;
        info.StructSize = (nuint)sizeof(PapyrusCallInfo);
        IntPtr value = IntPtr.Zero;
        var status = RadicleNative.PapyrusRuntimeCallResult(runtime, ticket, consume ? 1 : 0, &info, &value);
        if (status == Status.NotFound) return new PapyrusCallResult(false, default, default, 0, null);
        Call.Check(status);
        try
        {
            var read = value != IntPtr.Zero ? PapyrusValues.Read(value) : null;
            var detail = info.State == PapyrusCallState.Failed && info.Status == PapyrusStatus.FunctionNotFound ? Utf8.Read(RadicleNative.LastError()) : null;
            return new PapyrusCallResult(true, info.State, info.Status, info.Instructions, read, string.IsNullOrEmpty(detail) ? null : detail);
        }
        finally
        {
            if (value != IntPtr.Zero) RadicleNative.PapyrusValueDestroy(value);
        }
    }

    public PapyrusStepResult Step(int budget)
    {
        PapyrusStepResult result = default;
        CheckAfterCallbacks(RadicleNative.PapyrusRuntimeStep(Runtime, budget, &result));
        return result;
    }

    public PapyrusStepResult? RunCall(ulong ticket, int budget)
    {
        PapyrusStepResult result = default;
        var status = RadicleNative.PapyrusRuntimeRunCall(Runtime, ticket, budget, &result);
        if (status == Status.NotFound) return null;
        CheckAfterCallbacks(status);
        return result;
    }

    public bool CompleteLatent(ulong token, object? value)
    {
        var made = PapyrusValues.Make(value);
        try
        {
            int accepted = 0;
            CheckAfterCallbacks(RadicleNative.PapyrusRuntimeCompleteLatent(Runtime, token, made, &accepted));
            return accepted != 0;
        }
        finally { RadicleNative.PapyrusValueDestroy(made); }
    }

    public void RegisterNative(string script, string function, PapyrusNativeKind kind = PapyrusNativeKind.Static,
        string requiredScript = "", bool latent = false) =>
        Call.Check(RadicleNative.PapyrusRuntimeRegisterNative(Runtime, script, function, kind, requiredScript, latent ? 1 : 0));

    public void RegisterNativeEx(string script, string function, PapyrusNativeKind kind, string requiredScript, uint flags) =>
        Call.Check(RadicleNative.PapyrusRuntimeRegisterNativeEx(Runtime, script, function, kind, requiredScript, flags));

    public void RegisterOverride(string script, string function, int parameterCount = -1) =>
        Call.Check(RadicleNative.PapyrusRuntimeRegisterOverride(Runtime, script, function, parameterCount));

    public bool TryGetVariable(ulong instance, string name, out object? value)
    {
        IntPtr read = IntPtr.Zero;
        return Take(RadicleNative.PapyrusRuntimeGetVariable(Runtime, instance, name, &read), read, out value);
    }

    public bool TryGetProperty(ulong instance, string name, out object? value)
    {
        IntPtr read = IntPtr.Zero;
        return Take(RadicleNative.PapyrusRuntimeGetProperty(Runtime, instance, name, &read), read, out value);
    }

    public void SetVariable(ulong instance, string name, object? value)
    {
        var made = PapyrusValues.Make(value);
        try { Call.Check(RadicleNative.PapyrusRuntimeSetVariable(Runtime, instance, name, made)); }
        finally { RadicleNative.PapyrusValueDestroy(made); }
    }

    public long RegisterCustomEvent(ulong instance, string eventName)
    {
        long handle = 0;
        Call.Check(RadicleNative.PapyrusRuntimeRegisterCustomEvent(Runtime, instance, eventName, &handle));
        return handle;
    }

    public long RegisterTimer(ulong instance, double intervalSeconds, int repeats, PapyrusClock clock)
    {
        long handle = 0;
        Call.Check(RadicleNative.PapyrusRuntimeRegisterTimer(Runtime, instance, intervalSeconds, repeats, clock, &handle));
        return handle;
    }

    public long RegisterRemoteEvent(ulong instance, PapyrusOwner source, string eventName)
    {
        long handle = 0;
        Call.Check(RadicleNative.PapyrusRuntimeRegisterRemoteEvent(Runtime, instance, source.Kind, source.BaseFormKey,
            source.Discriminator, eventName, &handle));
        return handle;
    }

    public IReadOnlyList<PapyrusRemoteTarget> MatchRemoteEvent(PapyrusOwner source, string eventName)
    {
        var runtime = Runtime;
        while (true)
        {
            nuint count = 0;
            Call.Check(RadicleNative.PapyrusRuntimeMatchRemoteEvent(runtime, source.Kind, source.BaseFormKey, source.Discriminator,
                eventName, null, null, 0, &count));
            if (count == 0) return Array.Empty<PapyrusRemoteTarget>();
            var handles = new long[checked((int)count)];
            var instances = new ulong[handles.Length];
            nuint total;
            fixed (long* h = handles)
            fixed (ulong* i = instances)
                Call.Check(RadicleNative.PapyrusRuntimeMatchRemoteEvent(runtime, source.Kind, source.BaseFormKey, source.Discriminator,
                    eventName, h, i, count, &total));
            if (total > count) continue;
            return Enumerable.Range(0, (int)total).Select(index => new PapyrusRemoteTarget(handles[index], instances[index])).ToArray();
        }
    }

    public bool Unregister(long handle)
    {
        int removed = 0;
        Call.Check(RadicleNative.PapyrusRuntimeUnregister(Runtime, handle, &removed));
        return removed != 0;
    }

    public int AdvanceClock(string owner, double deltaSeconds, PapyrusClock clock)
    {
        int fired = 0;
        CheckAfterCallbacks(RadicleNative.PapyrusRuntimeAdvanceClock(Runtime, owner, deltaSeconds, clock, &fired));
        return fired;
    }

    public void SetEngineCallbacks(PapyrusEngineHandlers? handlers)
    {
        var runtime = Runtime;
        if (handlers is null)
        {
            Call.Check(RadicleNative.PapyrusRuntimeSetEngineCallbacks(runtime, null));
            _engineCallNative = null;
            _engineGetProperty = null;
            _engineSetProperty = null;
            return;
        }
        bool fresh = _engineBlock == IntPtr.Zero;
        if (fresh) _engineBlock = Marshal.AllocHGlobal(sizeof(PapyrusEngineCallbacks));
        var previous = fresh ? default : *(PapyrusEngineCallbacks*)_engineBlock;
        PapyrusEngineCallbacks engine = default;
        engine.StructSize = (nuint)sizeof(PapyrusEngineCallbacks);
        EngineCallNativeCallback callNative = (_, call) => Guard(() => EngineCallNative(call, handlers.CallNative), 0);
        engine.CallNativeEx = Marshal.GetFunctionPointerForDelegate(callNative);
        EngineGetPropertyCallback? getProperty = null;
        if (handlers.GetProperty is { } getPropertyHandler)
        {
            getProperty = (_, obj, name, outValue) => EngineGetProperty(obj, name, outValue, getPropertyHandler);
            engine.GetProperty = Marshal.GetFunctionPointerForDelegate(getProperty);
        }
        EngineSetPropertyCallback? setProperty = null;
        if (handlers.SetProperty is { } setPropertyHandler)
        {
            setProperty = (_, obj, name, value) => EngineSetProperty(obj, name, value, setPropertyHandler);
            engine.SetProperty = Marshal.GetFunctionPointerForDelegate(setProperty);
        }
        *(PapyrusEngineCallbacks*)_engineBlock = engine;
        try { Call.Check(RadicleNative.PapyrusRuntimeSetEngineCallbacks(runtime, (PapyrusEngineCallbacks*)_engineBlock)); }
        catch
        {
            *(PapyrusEngineCallbacks*)_engineBlock = previous;
            throw;
        }
        _engineCallNative = callNative;
        _engineGetProperty = getProperty;
        _engineSetProperty = setProperty;
    }

    // Invariant: the handler names the attached script instance an engine-bit handle stands for, or null to decline; Radicle checks the instance.
    public void SetCastHandler(Func<PapyrusObjectRef, string, ulong?>? handler)
    {
        var runtime = Runtime;
        if (handler is null)
        {
            Call.Check(RadicleNative.PapyrusRuntimeSetCastHandler(runtime, IntPtr.Zero, IntPtr.Zero));
            _engineCastHandle = null;
            return;
        }
        EngineCastHandleCallback callback = (_, obj, targetType, outInstance) => EngineCastHandle(obj, targetType, outInstance, handler);
        Call.Check(RadicleNative.PapyrusRuntimeSetCastHandler(runtime, IntPtr.Zero, Marshal.GetFunctionPointerForDelegate(callback)));
        _engineCastHandle = callback;
    }

    public bool HasScript(string name) => RadicleNative.PapyrusRuntimeHasScript(Runtime, name) != 0;

    public bool InstanceDeclares(ulong instance, string function) => RadicleNative.PapyrusRuntimeInstanceDeclares(Runtime, instance, function) != 0;

    public string? InstanceScript(ulong instance) => Utf8.Read(RadicleNative.PapyrusRuntimeInstanceScript(Runtime, instance));

    public IReadOnlyList<ulong> ListInstances()
    {
        var runtime = Runtime;
        while (true)
        {
            nuint count = 0;
            Call.Check(RadicleNative.PapyrusRuntimeListInstances(runtime, null, 0, &count));
            if (count == 0) return Array.Empty<ulong>();
            var ids = new ulong[checked((int)count)];
            nuint total;
            fixed (ulong* p = ids) Call.Check(RadicleNative.PapyrusRuntimeListInstances(runtime, p, count, &total));
            if (total > count) continue;
            return total == count ? ids : ids.AsSpan(0, (int)total).ToArray();
        }
    }

    public PapyrusRuntimeStats GetStats()
    {
        PapyrusRuntimeStats stats = default;
        stats.StructSize = (nuint)sizeof(PapyrusRuntimeStats);
        Call.Check(RadicleNative.PapyrusRuntimeGetStats(Runtime, &stats));
        return stats;
    }

    public void SetOninitPolicy(bool queue) =>
        Call.Check(RadicleNative.PapyrusRuntimeSetOninitPolicy(Runtime, queue ? PapyrusOninitPolicy.Queue : PapyrusOninitPolicy.Refuse));

    public void SetMaxParks(int parks) => Call.Check(RadicleNative.PapyrusRuntimeSetMaxParks(Runtime, parks));

    public void Dispose()
    {
        if (_callbacks > 0) throw new InvalidOperationException("PapyrusRuntime cannot be disposed from one of its callbacks.");
        if (_runtime != IntPtr.Zero) RadicleNative.PapyrusRuntimeDestroy(_runtime);
        _runtime = IntPtr.Zero;
        if (_engineBlock != IntPtr.Zero)
        {
            Marshal.FreeHGlobal(_engineBlock);
            _engineBlock = IntPtr.Zero;
        }
        foreach (var hold in _holds) hold.Dispose();
        _holds.Clear();
        foreach (var pex in _owned) pex.Dispose();
        _owned.Clear();
        _pex.Clear();
        GC.KeepAlive(_gate);
        GC.KeepAlive(_native);
        GC.KeepAlive(_override);
        GC.KeepAlive(_latent);
        GC.KeepAlive(_engineCallNative);
        GC.KeepAlive(_engineGetProperty);
        GC.KeepAlive(_engineSetProperty);
        GC.KeepAlive(_engineCastHandle);
    }

    private void Add(PexFile pex)
    {
        var runtime = Runtime;
        var hold = pex.Use();
        try { Call.Check(RadicleNative.PapyrusRuntimeAddPex(runtime, hold.Pointer)); }
        catch
        {
            hold.Dispose();
            throw;
        }
        _holds.Add(hold);
        _pex.Add(pex);
    }

    private static int EngineCallNative(IntPtr call, Func<PapyrusNativeCall, bool> callNative)
    {
        var view = new PapyrusNativeCall(call);
        try { return callNative(view) ? 1 : 0; }
        finally { view.Close(); }
    }

    private int EngineGetProperty(IntPtr obj, IntPtr name, IntPtr outValue, Func<PapyrusObjectRef, string, (bool Handled, object? Value)> getProperty)
    {
        *(IntPtr*)outValue = IntPtr.Zero;
        return Guard(() =>
        {
            var (handled, value) = getProperty(PapyrusValues.ReadObject(obj), Text(name));
            if (!handled) return 0;
            *(IntPtr*)outValue = PapyrusValues.Make(value);
            return 1;
        }, 0);
    }

    private int EngineSetProperty(IntPtr obj, IntPtr name, IntPtr value, Func<PapyrusObjectRef, string, object?, bool> setProperty) =>
        Guard(() => setProperty(PapyrusValues.ReadObject(obj), Text(name), PapyrusValues.Read(value)) ? 1 : 0, 0);

    private int EngineCastHandle(IntPtr obj, IntPtr targetType, IntPtr outInstance, Func<PapyrusObjectRef, string, ulong?> castHandle) =>
        Guard(() =>
        {
            if (castHandle(PapyrusValues.ReadObject(obj), Text(targetType)) is not { } instance) return 0;
            *(ulong*)outInstance = instance;
            return 1;
        }, 0);

    private IntPtr Runtime => _runtime != IntPtr.Zero ? _runtime : throw new ObjectDisposedException(nameof(PapyrusRuntime));

    internal IntPtr NativeHandle => Runtime;

    private static string Text(IntPtr pointer) => Utf8.Read(pointer) ?? string.Empty;

    private static bool Take(Status status, IntPtr read, out object? value)
    {
        value = null;
        if (status == Status.NotFound) return false;
        Call.Check(status);
        try { value = PapyrusValues.Read(read); }
        finally { RadicleNative.PapyrusValueDestroy(read); }
        return true;
    }

    internal void CheckAfterCallbacks(Status status)
    {
        if (_fault is { } fault)
        {
            _fault = null;
            ExceptionDispatchInfo.Throw(fault);
        }
        Call.Check(status);
    }

    private T Guard<T>(Func<T> body, T failed)
    {
        _callbacks += 1;
        try { return body(); }
        catch (Exception fault)
        {
            _fault ??= fault;
            return failed;
        }
        finally { _callbacks -= 1; }
    }
}
