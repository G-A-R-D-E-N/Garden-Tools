using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed class RadicleException : Exception
{
    public RadicleException(Status status, string message) : base(message) => Status = status;

    public Status Status { get; }
}

public sealed class RadicleAbiMismatchException : InvalidOperationException
{
    public RadicleAbiMismatchException(string message) : base(message) { }
}

public static class RadicleLibrary
{
    private static readonly object Gate = new();
    private static string? _path;
    private static IntPtr _handle;

    static RadicleLibrary() => NativeLibrary.SetDllImportResolver(typeof(RadicleNative).Assembly, Resolve);

    public static void SetPath(string path)
    {
        lock (Gate)
        {
            if (_handle != IntPtr.Zero && path != _path)
                throw new InvalidOperationException("The native radicle library is already loaded; set its path before the first call.");
            _path = path;
        }
    }

    internal static void Register() { }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != RadicleNative.Library) return IntPtr.Zero;
        lock (Gate)
        {
            if (_handle != IntPtr.Zero) return _handle;
            var path = _path ?? Environment.GetEnvironmentVariable("RADICLE_NATIVE_PATH");
            IntPtr handle;
            if (string.IsNullOrEmpty(path))
            {
                if (!NativeLibrary.TryLoad(name, assembly, searchPath, out handle)) return IntPtr.Zero;
            }
            else handle = NativeLibrary.Load(path);
            try { VerifyAbi(handle, RadicleNative.CompiledAbiVersion, string.IsNullOrEmpty(path) ? name : path); }
            catch
            {
                NativeLibrary.Free(handle);
                throw;
            }
            _path ??= path;
            _handle = handle;
            return _handle;
        }
    }

    private static unsafe void VerifyAbi(IntPtr library, uint expected, string description)
    {
        if (!NativeLibrary.TryGetExport(library, "radicle_abi_version", out var export))
            throw new RadicleAbiMismatchException($"{description} does not export radicle_abi_version, so it is not a Radicle library.");
        var loaded = ((delegate* unmanaged[Cdecl]<uint>)export)();
        if (loaded != expected)
            throw new RadicleAbiMismatchException(
                $"{description} reports Radicle ABI {loaded}, but Radicle.Interop was generated for ABI {expected}. " +
                "Use a Radicle build from the same commit as these bindings.");
    }
}

public readonly record struct FormKey(string Plugin, uint LocalId)
{
    public static FormKey? FromNative(Native.FormKey key) =>
        key.Plugin == IntPtr.Zero ? null : new FormKey(Utf8.Read(key.Plugin)!, key.LocalId);

    public bool Equals(FormKey other) =>
        LocalId == other.LocalId && string.Equals(Plugin, other.Plugin, StringComparison.OrdinalIgnoreCase);

    public override int GetHashCode() => HashCode.Combine(LocalId, Plugin is null ? 0 : StringComparer.OrdinalIgnoreCase.GetHashCode(Plugin));

    public override string ToString() => $"{LocalId:X6}:{Plugin}";
}

internal static class Utf8
{
    public static string? Read(IntPtr text) => text == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(text);

    public static unsafe byte[] Raw(IntPtr text, nuint size)
    {
        var bytes = new byte[checked((int)size)];
        if (size != 0) new ReadOnlySpan<byte>((void*)text, bytes.Length).CopyTo(bytes);
        return bytes;
    }
}

internal sealed class Utf8NoNulMarshaler : ICustomMarshaler
{
    public static ICustomMarshaler GetInstance(string cookie) => new Utf8NoNulMarshaler();

    public static IntPtr Allocate(string text)
    {
        if (text.Contains('\0')) throw new ArgumentException("A string argument cannot hold an embedded NUL.");
        return Marshal.StringToCoTaskMemUTF8(text);
    }

    public IntPtr MarshalManagedToNative(object? managedObj) => managedObj is null ? IntPtr.Zero : Allocate((string)managedObj);

    public object MarshalNativeToManaged(IntPtr pNativeData) => throw new NotSupportedException();

    public void CleanUpNativeData(IntPtr pNativeData) => Marshal.FreeCoTaskMem(pNativeData);

    public void CleanUpManagedData(object? managedObj) { }

    public int GetNativeDataSize() => -1;
}

internal sealed class NativeStrings : IDisposable
{
    private readonly List<IntPtr> _owned = new();

    public IntPtr Add(string? text)
    {
        if (text is null) return IntPtr.Zero;
        var pointer = Utf8NoNulMarshaler.Allocate(text);
        _owned.Add(pointer);
        return pointer;
    }

    public IntPtr[] AddAll(IEnumerable<string>? texts) => (texts ?? Array.Empty<string>()).Select(Add).ToArray();

    public Native.FormKey Key(FormKey? key) =>
        key is { } value ? new Native.FormKey { Plugin = Add(value.Plugin), LocalId = value.LocalId } : default;

    public void Dispose()
    {
        foreach (var pointer in _owned) Marshal.FreeCoTaskMem(pointer);
        _owned.Clear();
    }
}

internal sealed class EnvironmentHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public EnvironmentHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.EnvDestroy(handle);
        return true;
    }
}

internal sealed class PluginHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public PluginHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.PluginDestroy(handle);
        return true;
    }
}

internal sealed class PexHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public PexHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.PexDestroy(handle);
        return true;
    }
}

internal sealed class SeqHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public SeqHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.SeqDestroy(handle);
        return true;
    }
}

internal sealed class StringsHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public StringsHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.StringsDestroy(handle);
        return true;
    }
}

internal sealed class AssetsHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public AssetsHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.AssetsDestroy(handle);
        return true;
    }
}

internal sealed class Ba2Handle : SafeHandleZeroOrMinusOneIsInvalid
{
    private readonly bool _owned;

    public Ba2Handle(IntPtr handle, bool owned) : base(ownsHandle: true)
    {
        _owned = owned;
        SetHandle(handle);
    }

    protected override bool ReleaseHandle()
    {
        if (_owned) RadicleNative.Ba2Close(handle);
        return true;
    }
}

internal readonly struct HandleUse : IDisposable
{
    private readonly SafeHandle _handle;

    public HandleUse(SafeHandle handle, string name)
    {
        var added = false;
        handle.DangerousAddRef(ref added);
        if (!added) throw new ObjectDisposedException(name);
        _handle = handle;
    }

    public IntPtr Pointer => _handle.DangerousGetHandle();

    public void Dispose() => _handle?.DangerousRelease();
}

internal static unsafe class Call
{
    public static void Check(Status status)
    {
        if (status == Status.Ok) return;
        throw new RadicleException(status, Utf8.Read(RadicleNative.LastError()) ?? status.ToString());
    }

    public static bool Found(Status status)
    {
        if (status == Status.NotFound) return false;
        Check(status);
        return true;
    }

    public static IntPtr[] TakeRecords(IntPtr* items, nuint count)
    {
        var result = new IntPtr[checked((int)count)];
        for (var i = 0; i < result.Length; ++i) result[i] = items[i];
        if (items != null) RadicleNative.FreeRecordArray(items);
        return result;
    }

    public static string[] TakeStrings(IntPtr* items, nuint count)
    {
        var result = new string[checked((int)count)];
        for (var i = 0; i < result.Length; ++i) result[i] = Utf8.Read(items[i]) ?? string.Empty;
        if (items != null) RadicleNative.FreeStringArray(items, count);
        return result;
    }

    public static byte[] TakeBytes(byte* bytes, nuint size)
    {
        var result = Utf8.Raw((IntPtr)bytes, size);
        if (bytes != null) RadicleNative.FreeBytes(bytes);
        return result;
    }

    public static string TakeText(byte* text, nuint size) => Encoding.UTF8.GetString(TakeBytes(text, size));

    public static float[] TakeFloats(float* values, nuint count)
    {
        var result = new float[checked((int)count)];
        if (count != 0) new ReadOnlySpan<float>(values, result.Length).CopyTo(result);
        if (values != null) RadicleNative.FreeFloats(values);
        return result;
    }

    public static ushort[] TakeTriangles(ushort* values, nuint count)
    {
        var result = new ushort[checked((int)count)];
        if (count != 0) new ReadOnlySpan<ushort>(values, result.Length).CopyTo(result);
        if (values != null) RadicleNative.FreeTriangles(values);
        return result;
    }

    public static uint[] TakeNodeRefs(uint* values, nuint count)
    {
        var result = new uint[checked((int)count)];
        if (count != 0) new ReadOnlySpan<uint>(values, result.Length).CopyTo(result);
        if (values != null) RadicleNative.FreeNodeRefs(values);
        return result;
    }
    public static T[] Copy<T>(IntPtr items, nuint count) where T : unmanaged
    {
        var result = new T[checked((int)count)];
        if (count != 0) new ReadOnlySpan<T>((void*)items, result.Length).CopyTo(result);
        return result;
    }
}
