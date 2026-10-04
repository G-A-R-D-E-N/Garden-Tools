using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

/// <summary>
/// A read-only snapshot of one .seq container.
/// </summary>
/// <remarks>
/// A .seq is a bare array of little-endian uint32 runtime FormIDs: no header, no count and no
/// magic. Entries stay raw FormIDs and this model never resolves one to a FormKey, because that
/// needs a load order the container does not carry. The evidence and the rules that stay UNPROVEN
/// are in Radicle/docs/SEQ_FORMAT.md.
/// </remarks>
public sealed class SeqFile : IDisposable
{
    private IntPtr _handle;

    private SeqFile(IntPtr handle, IReadOnlyList<uint> entries)
    {
        _handle = handle;
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

    public unsafe byte[] ToBytes()
    {
        if (_handle == IntPtr.Zero) throw new ObjectDisposedException(nameof(SeqFile));
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SeqWriteBytes(_handle, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        RadicleNative.SeqDestroy(_handle);
        _handle = IntPtr.Zero;
    }

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
