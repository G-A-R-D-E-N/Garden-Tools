using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record Subrecord(string Signature, byte[] Bytes, bool ExtendedSize);
public sealed record RecordPayload(ushort FormVersion, IReadOnlyList<Subrecord> Subrecords);

public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<Record> TopicChildren(Record record)
    {
        using var use = Use(record);
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.RecordTopicChildren(use.Pointer, record.Handle, &items, &count));
        return Records(items, count);
    }

    public RecordPayload ReadPayload(Record record)
    {
        using var use = Use(record);
        ushort version;
        nuint count;
        Call.Check(RadicleNative.RecordPayloadInfo(use.Pointer, record.Handle, &version, &count));
        var subs = new Subrecord[checked((int)count)];
        var signature = stackalloc byte[4];
        for (var i = 0; i < subs.Length; ++i)
        {
            byte* bytes;
            nuint size;
            int extended;
            Call.Check(RadicleNative.RecordCopySubrecord(use.Pointer, record.Handle, (nuint)i, signature, &bytes, &size, &extended));
            try
            {
                subs[i] = new Subrecord(Encoding.Latin1.GetString(signature, 4), Utf8.Raw((IntPtr)bytes, size), extended != 0);
            }
            finally
            {
                if (bytes != null) RadicleNative.FreeBytes(bytes);
            }
        }
        return new RecordPayload(version, subs);
    }

    // Returns null when the path or the occurrence is absent; the text is the form PluginFile.SetScalar accepts for a numeric or bytes field, and PluginFile.SetString accepts for a string field.
    public string? GetScalar(Record record, string path, int index = 0)
    {
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        using var use = Use(record);
        IntPtr text;
        if (!Call.Found(RadicleNative.RecordGetScalar(use.Pointer, record.Handle, path, (nuint)index, &text))) return null;
        try
        {
            return Utf8.Read(text);
        }
        finally
        {
            RadicleNative.RecordScalarFree(text);
        }
    }

    // Constraint: top-level KWDA keys in stored order with null entries dropped; a QUST throws InvalidArgument.
    public IReadOnlyList<FormKey> RecordKeywords(Record record)
    {
        using var use = Use(record);
        Native.FormKey* keys;
        nuint count;
        Call.Check(RadicleNative.RecordKeywords(use.Pointer, record.Handle, &keys, &count));
        try { return Call.Copy<Native.FormKey>((IntPtr)keys, count).Select(key => FormKey.Required(key, "record keyword")).ToArray(); }
        finally { RadicleNative.FreeFormKeys(keys); }
    }

    public FormKey? ResolveFormId(Record record, uint rawFormId)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.RecordResolveFormId(use.Pointer, record.Handle, rawFormId, &key));
        return FormKey.FromNative(key);
    }
}
