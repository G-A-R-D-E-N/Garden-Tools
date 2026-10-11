using System;
using System.Collections.Generic;
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

    public FormKey? ResolveFormId(Record record, uint rawFormId)
    {
        using var use = Use(record);
        Native.FormKey key;
        Call.Check(RadicleNative.RecordResolveFormId(use.Pointer, record.Handle, rawFormId, &key));
        return FormKey.FromNative(key);
    }
}
