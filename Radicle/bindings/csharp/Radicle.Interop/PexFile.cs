using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct PexHeader(
    byte MajorVersion,
    byte MinorVersion,
    ushort GameId,
    long CompilationTime,
    int StringCount,
    int ObjectCount);

public readonly record struct PexObjectCounts(
    int StructCount,
    int VariableCount,
    int PropertyCount,
    int StateCount,
    int FunctionCount);

public sealed record PexObjectInfo(
    byte[] Name,
    byte[] ParentClassName,
    PexObjectCounts Counts);

public sealed partial class PexFile : IDisposable
{
    private readonly PexHandle _handle;

    private PexFile(IntPtr handle, PexHeader header, IReadOnlyList<PexObjectInfo> objects, IReadOnlyList<byte[]> stringTable)
    {
        _handle = new PexHandle(handle);
        Header = header;
        Objects = objects;
        StringTable = stringTable;
    }

    public PexHeader Header { get; }

    public IReadOnlyList<PexObjectInfo> Objects { get; }

    public IReadOnlyList<byte[]> StringTable { get; }

    public static unsafe PexFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.PexParse(bytes.IsEmpty ? null : pointer, (nuint)bytes.Length, &handle));
        }
        return Snapshot(handle);
    }

    public static unsafe PexFile FromFile(string path)
    {
        IntPtr handle = IntPtr.Zero;
        Call.Check(RadicleNative.PexReadFile(path, &handle));
        return Snapshot(handle);
    }

    internal HandleUse Use() => new(_handle, nameof(PexFile));

    public unsafe byte[] ToBytes()
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.PexWriteBytes(use.Pointer, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public void Dispose() => _handle.Dispose();

    internal static unsafe PexFile Snapshot(IntPtr handle)
    {
        try
        {
            Native.PexHeader rawHeader = default;
            Call.Check(RadicleNative.PexGetHeader(handle, &rawHeader));
            var header = new PexHeader(rawHeader.MajorVersion, rawHeader.MinorVersion, rawHeader.GameId,
                rawHeader.CompilationTime, checked((int)rawHeader.StringCount), checked((int)rawHeader.ObjectCount));

            var objects = new List<PexObjectInfo>(header.ObjectCount);
            for (var index = 0; index < header.ObjectCount; ++index)
            {
                Native.PexObjectCounts rawCounts = default;
                Call.Check(RadicleNative.PexObjectGetCounts(handle, (nuint)index, &rawCounts));
                objects.Add(new PexObjectInfo(
                    ReadBytes(handle, index, NameKind.Object),
                    ReadBytes(handle, index, NameKind.ParentClass),
                    new PexObjectCounts(checked((int)rawCounts.StructCount), checked((int)rawCounts.VariableCount),
                        checked((int)rawCounts.PropertyCount), checked((int)rawCounts.StateCount),
                        checked((int)rawCounts.FunctionCount))));
            }

            var stringTable = new List<byte[]>(header.StringCount);
            for (var index = 0; index < header.StringCount; ++index)
            {
                stringTable.Add(ReadBytes(handle, index, NameKind.StringTableEntry));
            }

            return new PexFile(handle, header, objects, stringTable);
        }
        catch
        {
            RadicleNative.PexDestroy(handle);
            throw;
        }
    }

    private enum NameKind
    {
        Object,
        ParentClass,
        StringTableEntry,
    }

    private static unsafe byte[] ReadBytes(IntPtr handle, int index, NameKind kind)
    {
        byte* bytes = null;
        nuint size = 0;
        var status = kind switch
        {
            NameKind.Object => RadicleNative.PexObjectName(handle, (nuint)index, &bytes, &size),
            NameKind.ParentClass => RadicleNative.PexObjectParentName(handle, (nuint)index, &bytes, &size),
            _ => RadicleNative.PexStringTableEntry(handle, (nuint)index, &bytes, &size),
        };
        Call.Check(status);
        return Call.TakeBytes(bytes, size);
    }
}
