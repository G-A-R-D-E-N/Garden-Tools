using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed class RadicleException : Exception
{
    public RadicleException(Status status, string message) : base(message) => Status = status;

    public Status Status { get; }
}

public static class RadicleLibrary
{
    private static string? _path;
    private static int _registered;

    public static void SetPath(string path)
    {
        _path = path;
        Register();
    }

    internal static void Register()
    {
        if (System.Threading.Interlocked.Exchange(ref _registered, 1) != 0) return;
        NativeLibrary.SetDllImportResolver(typeof(RadicleNative).Assembly, Resolve);
    }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != RadicleNative.Library) return IntPtr.Zero;
        var path = _path ?? Environment.GetEnvironmentVariable("RADICLE_NATIVE_PATH");
        return string.IsNullOrEmpty(path) ? IntPtr.Zero : NativeLibrary.Load(path);
    }
}

public readonly record struct FormKey(string Plugin, uint LocalId)
{
    public static FormKey? FromNative(Native.FormKey key) =>
        key.Plugin == IntPtr.Zero ? null : new FormKey(Utf8.Read(key.Plugin)!, key.LocalId);

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

internal sealed class NativeStrings : IDisposable
{
    private readonly List<IntPtr> _owned = new();

    public IntPtr Add(string? text)
    {
        if (text is null) return IntPtr.Zero;
        var pointer = Marshal.StringToCoTaskMemUTF8(text);
        _owned.Add(pointer);
        return pointer;
    }

    public Native.FormKey Key(FormKey? key) =>
        key is { } value ? new Native.FormKey { Plugin = Add(value.Plugin), LocalId = value.LocalId } : default;

    public void Dispose()
    {
        foreach (var pointer in _owned) Marshal.FreeCoTaskMem(pointer);
        _owned.Clear();
    }
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

    public static T[] Copy<T>(IntPtr items, nuint count) where T : unmanaged
    {
        var result = new T[checked((int)count)];
        if (count != 0) new ReadOnlySpan<T>((void*)items, result.Length).CopyTo(result);
        return result;
    }
}
