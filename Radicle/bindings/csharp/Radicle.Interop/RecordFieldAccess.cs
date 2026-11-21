using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record WinnerFieldValue(FormKey Key, string? Text);

public sealed record WinnerFieldsRowValue(FormKey Key, IReadOnlyList<string?> Texts);

public sealed record FieldSchemaExEntry(
    string Path, FieldKind Kind, bool CanInsert, bool Repeated, string? Subrecord, string? CountSubrecord,
    int Storage, int Presentation, int Embedded, bool Localized, ulong Offset, ulong PayloadSize, ulong Stride,
    double Minimum, double Maximum, ushort MinFormVersion, ushort MaxFormVersion, IReadOnlyList<string> Names);

public static unsafe class RecordFieldWinners
{
    static RecordFieldWinners() => RadicleLibrary.Register();

    public static IReadOnlyList<WinnerFieldValue> Read(RadicleEnvironment env, string path, int index = 0,
        StringTables? strings = null)
    {
        ArgumentNullException.ThrowIfNull(env);
        ArgumentNullException.ThrowIfNull(path);
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        if (strings is not null && strings.Owner != env)
            throw new ArgumentException("The strings do not belong to this environment.", nameof(strings));
        using var envUse = env.Use();
        using var stringsUse = strings?.Borrow() ?? default;
        WinnerField* rows;
        nuint count;
        Call.Check(RadicleNative.EnvReadWinners(envUse.Pointer,
            strings is null ? IntPtr.Zero : stringsUse.Pointer, path, (nuint)index, &rows, &count));
        try
        {
            var values = new WinnerFieldValue[checked((int)count)];
            for (var i = 0; i < values.Length; ++i)
                values[i] = new WinnerFieldValue(FormKey.Required(rows[i].Key, "winner field"), Utf8.Read(rows[i].Text));
            return values;
        }
        finally
        {
            RadicleNative.EnvReadWinnersFree(rows, count);
        }
    }

    public static IReadOnlyList<WinnerFieldsRowValue> ReadFields(RadicleEnvironment env, string signature,
        IReadOnlyList<string> paths, int index = 0, StringTables? strings = null)
    {
        ArgumentNullException.ThrowIfNull(env);
        ArgumentNullException.ThrowIfNull(signature);
        ArgumentNullException.ThrowIfNull(paths);
        if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
        if (paths.Count == 0) throw new ArgumentException("at least one path is required", nameof(paths));
        if (strings is not null && strings.Owner != env)
            throw new ArgumentException("The strings do not belong to this environment.", nameof(strings));
        var pathCount = (nuint)paths.Count;
        using var native = new NativeStrings();
        var utf8 = native.AddAll(paths);
        fixed (IntPtr* pathPtr = utf8)
        {
            using var envUse = env.Use();
            using var stringsUse = strings?.Borrow() ?? default;
            WinnerFieldsRow* rows;
            nuint count;
            Call.Check(RadicleNative.EnvReadFields(envUse.Pointer,
                strings is null ? IntPtr.Zero : stringsUse.Pointer, signature, pathPtr, pathCount, (nuint)index,
                &rows, &count));
            try
            {
                var values = new WinnerFieldsRowValue[checked((int)count)];
                for (var row = 0; row < values.Length; ++row)
                {
                    var texts = new string?[paths.Count];
                    for (var col = 0; col < texts.Length; ++col)
                    {
                        var pointer = Marshal.ReadIntPtr(rows[row].Texts, col * IntPtr.Size);
                        texts[col] = Utf8.Read(pointer);
                    }

                    values[row] = new WinnerFieldsRowValue(FormKey.Required(rows[row].Key, "winner field row"), texts);
                }

                return values;
            }
            finally
            {
                RadicleNative.EnvReadFieldsFree(rows, count, pathCount);
            }
        }
    }
}

public static unsafe class RecordFieldSchemaEx
{
    static RecordFieldSchemaEx() => RadicleLibrary.Register();

    public static IReadOnlyList<FieldSchemaExEntry> For(string signature)
    {
        ArgumentNullException.ThrowIfNull(signature);
        FieldSchemaExRow* rows;
        nuint count;
        Call.Check(RadicleNative.RecordFieldSchemaEx(signature, &rows, &count));
        try
        {
            var entries = new FieldSchemaExEntry[checked((int)count)];
            for (var i = 0; i < entries.Length; ++i)
            {
                var row = rows[i];
                var names = new List<string>(checked((int)row.NameCount));
                if (row.Names != IntPtr.Zero && row.NameCount > 0)
                {
                    var nameRows = (FieldSchemaName*)row.Names;
                    for (var name = 0u; name < row.NameCount; ++name)
                        names.Add(Utf8.Read(nameRows[name].Name) ?? string.Empty);
                }

                entries[i] = new FieldSchemaExEntry(Utf8.Read(row.Path) ?? string.Empty, row.Kind, row.CanInsert != 0,
                    row.Repeated != 0, Utf8.Read(row.Subrecord), Utf8.Read(row.CountSubrecord), row.Storage,
                    row.Presentation, row.Embedded, row.Localized != 0, row.Offset, row.PayloadSize, row.Stride,
                    row.Minimum, row.Maximum, row.MinFormVersion, row.MaxFormVersion, names);
            }

            return entries;
        }
        finally
        {
            RadicleNative.FieldSchemaExFree(rows);
        }
    }

    public static IReadOnlyList<string> Signatures()
    {
        IntPtr* signatures;
        nuint count;
        Call.Check(RadicleNative.SchemaRecordSignatures(&signatures, &count));
        try
        {
            var values = new string[checked((int)count)];
            for (var i = 0; i < values.Length; ++i) values[i] = Utf8.Read(signatures[i]) ?? string.Empty;
            return values;
        }
        finally
        {
            RadicleNative.SchemaRecordSignaturesFree(signatures);
        }
    }
}