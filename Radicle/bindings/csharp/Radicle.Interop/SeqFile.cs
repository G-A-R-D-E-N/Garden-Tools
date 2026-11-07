using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed class SeqFile : IDisposable
{
    private readonly SeqHandle _handle;

    private SeqFile(IntPtr handle, IReadOnlyList<uint> entries)
    {
        _handle = new SeqHandle(handle);
        Entries = entries;
    }

    public IReadOnlyList<uint> Entries { get; }

    public static unsafe SeqFile FromBytes(ReadOnlySpan<byte> bytes)
    {
        IntPtr handle = IntPtr.Zero;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.SeqParse(bytes.IsEmpty ? null : pointer, (nuint)bytes.Length, &handle));
        }
        return Snapshot(handle);
    }

    public static unsafe SeqFile FromFile(string path)
    {
        IntPtr handle = IntPtr.Zero;
        Call.Check(RadicleNative.SeqReadFile(path, &handle));
        return Snapshot(handle);
    }

    private HandleUse Use() => new(_handle, nameof(SeqFile));

    public unsafe byte[] ToBytes()
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SeqWriteBytes(use.Pointer, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public void Dispose() => _handle.Dispose();

    private static unsafe SeqFile Snapshot(IntPtr handle)
    {
        try
        {
            nuint count = 0;
            Call.Check(RadicleNative.SeqEntryCount(handle, &count));
            var entries = new List<uint>(checked((int)count));
            for (var index = 0; index < checked((int)count); ++index)
            {
                uint formId = 0;
                Call.Check(RadicleNative.SeqEntryFormId(handle, (nuint)index, &formId));
                entries.Add(formId);
            }
            return new SeqFile(handle, entries);
        }
        catch
        {
            RadicleNative.SeqDestroy(handle);
            throw;
        }
    }
}
