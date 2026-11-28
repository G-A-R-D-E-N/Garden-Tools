using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record FieldSchemaEntry(string Path, FieldKind Kind, bool CanInsert, bool Repeated);

public static unsafe class RecordFieldSchema
{
    static RecordFieldSchema() => RadicleLibrary.Register();

    public static IReadOnlyList<FieldSchemaEntry> For(string signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        FieldSchemaRow* rows;
        nuint count;
        Call.Check(RadicleNative.RecordFieldSchema(signature, &rows, &count));
        try
        {
            var entries = new FieldSchemaEntry[checked((int)count)];
            for (var i = 0; i < entries.Length; ++i)
            {
                entries[i] = new FieldSchemaEntry(Utf8.Read(rows[i].Path) ?? string.Empty, rows[i].Kind, rows[i].CanInsert != 0, rows[i].Repeated != 0);
            }
            return entries;
        }
        finally
        {
            RadicleNative.FieldSchemaFree(rows);
        }
    }
}

public sealed unsafe partial class RadicleEnvironment
{
    public string? GetField(Record record, string path, int index = 0)
    {
        ArgumentNullException.ThrowIfNull(path);
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        using var use = Use(record);
        IntPtr text;
        if (!Call.Found(RadicleNative.RecordGetField(use.Pointer, record.Handle, path, (nuint)index, &text))) return null;
        try
        {
            return Utf8.Read(text);
        }
        finally
        {
            RadicleNative.RecordScalarFree(text);
        }
    }

    // Constraint: record must be an EXPL; null means a null link or no DATA subrecord, and a non-EXPL record throws like GetField.
    public FormKey? ExplosionPlacedObject(Record record)
    {
        var text = GetField(record, "EXPL\\ImpactPlacedObject");
        if (string.IsNullOrEmpty(text)) return null;
        var colon = text.IndexOf(':');
        return new FormKey(text[(colon + 1)..], System.Convert.ToUInt32(text[..colon], 16));
    }

    // Constraint: path must name one schema record-flag row such as LIGH\RandomAnimStart; an unknown path or a row of another record type throws instead of reading as a clear flag.
    public bool GetRecordFlag(Record record, string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        using var use = Use(record);
        int value;
        Call.Check(RadicleNative.RecordGetFlag(use.Pointer, record.Handle, path, &value));
        return value != 0;
    }

    public bool TryListSize(Record record, string path, out int size)
    {
        ArgumentNullException.ThrowIfNull(path);
        size = 0;
        using var use = Use(record);
        nuint native;
        if (!Call.Found(RadicleNative.RecordListSize(use.Pointer, record.Handle, path, &native)))
            return false;
        size = checked((int)native);
        return true;
    }
}
