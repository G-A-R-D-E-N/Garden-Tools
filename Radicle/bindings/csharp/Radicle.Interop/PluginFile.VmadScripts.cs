using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

// Constraint: a property value is reported by kind, so a caller must switch on Kind before reading Text, Integer, Real, Boolean or ObjectFormId.
public readonly record struct VmadPropertyValue(
    VmadValueKind Kind,
    uint ObjectFormId,
    short Alias,
    string? Text,
    int Integer,
    float Real,
    bool Boolean);

public sealed record VmadPropertyInfo(
    string Name,
    byte Type,
    byte Flags,
    VmadPropertyValue Value,
    uint DeclaredElementCount)
{
    public VmadValueKind ElementKind { get; init; }

    public IReadOnlyList<VmadElementInfo> Elements { get; init; } = Array.Empty<VmadElementInfo>();

    public int ElementTotal => Elements.Count;
}

// Constraint: for a struct (type 7) or struct array (type 17) property Kind is the member's own kind and a struct with no members has no row, so a consumer groups rows by StructIndex and compares the group count with DeclaredElementCount.
public readonly record struct VmadElementInfo(
    VmadValueKind Kind,
    string? MemberName,
    int StructIndex,
    int IndexInProperty,
    byte Type,
    byte Flags,
    VmadPropertyValue Value)
{
    public IReadOnlyList<VmadElementInfo>? Children { get; init; }
}

// Constraint: a read cut short at a property of unknown size names that script and property and the script count the file declares; the scripts before ScriptIndex are complete and the scripts after it are absent.
public sealed record VmadReadStop(int ScriptIndex, string Property, byte PropertyType, int DeclaredScripts);

// Constraint: Unsupported describes the whole read, so every script of one result reports the same value; it does not say which script or property is incomplete; when an undecodable property ended the read, the scripts after it are absent.
public sealed record VmadScriptInfo(string Name, byte Flags, IReadOnlyList<VmadPropertyInfo> Properties)
{
    public bool Unsupported { get; init; }

    public long TrailingBytes { get; init; }

    public VmadReadStop? Stop { get; init; }
}

// Constraint: these reads stay on the plugin handle being edited, so a script sees its own unsaved VMAD edits without a load order; a record or VMAD that is absent reports false instead of throwing.
public sealed unsafe partial class PluginFile
{
    public IReadOnlyList<VmadScriptInfo> Scripts(FormKey record) =>
        TryScripts(record, out var scripts)
            ? scripts
            : throw new KeyNotFoundException($"Record or VMAD not found: {record}");

    public bool TryScripts(FormKey record, out IReadOnlyList<VmadScriptInfo> scripts)
    {
        scripts = Array.Empty<VmadScriptInfo>();
        using var use = Use();
        using var strings = new NativeStrings();
        IntPtr handle;
        var status = RadicleNative.PluginVmadScripts(use.Pointer, strings.Key(record), &handle);
        if (status == Status.NotFound) return false;
        Call.Check(status);
        try
        {
            scripts = VmadScriptReader.Read(handle);
            return true;
        }
        finally
        {
            RadicleNative.VmadScriptsDestroy(handle);
        }
    }
}

// Constraint: these reads decode the record's own VMAD script list; a record with no VMAD reports false, and an undecodable record or VMAD throws RadicleException, which a caller must treat as an incomplete read.
public sealed unsafe partial class RadicleEnvironment
{
    public IReadOnlyList<VmadScriptInfo> Scripts(Record record) =>
        TryScripts(record, out var scripts)
            ? scripts
            : throw new KeyNotFoundException("The record has no VMAD.");

    public bool TryScripts(Record record, out IReadOnlyList<VmadScriptInfo> scripts)
    {
        scripts = Array.Empty<VmadScriptInfo>();
        using var use = Use(record);
        IntPtr handle;
        var status = RadicleNative.RecordVmadScripts(use.Pointer, record.Handle, &handle);
        if (status == Status.NotFound) return false;
        Call.Check(status);
        try
        {
            scripts = VmadScriptReader.Read(handle);
            return true;
        }
        finally
        {
            RadicleNative.VmadScriptsDestroy(handle);
        }
    }
}

internal static unsafe class VmadScriptReader
{
    // Requires: handle is a live radicle_vmad_scripts; the caller destroys it after this returns.
    public static VmadScriptInfo[] Read(IntPtr handle)
    {
        var result = new VmadScriptInfo[checked((int)RadicleNative.VmadScriptCount(handle))];
        for (var index = 0; index < result.Length; ++index) result[index] = ReadScript(handle, index);
        return result;
    }

    // Requires: handle is a live radicle_vmad_scripts holding a script at index; the caller destroys it after this returns.
    public static VmadScriptInfo ReadScript(IntPtr handle, int index)
    {
        VmadScript script;
        Call.Check(RadicleNative.VmadScriptsScript(handle, (nuint)index, &script));
        var properties = new VmadPropertyInfo[checked((int)script.PropertyCount)];
        for (var propertyIndex = 0; propertyIndex < properties.Length; ++propertyIndex)
        {
            VmadProperty property;
            Call.Check(RadicleNative.VmadScriptsProperty(handle, (nuint)index, (nuint)propertyIndex, &property));
            properties[propertyIndex] = new VmadPropertyInfo(
                Utf8.Read(property.Name) ?? string.Empty,
                property.Type,
                property.Flags,
                Value(property.ValueKind, property.ObjectFormId, property.Alias, property.Text, property.Integer, property.Real, property.Boolean),
                property.DeclaredElementCount)
            {
                ElementKind = property.ElementKind,
                Elements = Elements(handle, index, propertyIndex, property.ElementTotal),
            };
        }
        return new VmadScriptInfo(Utf8.Read(script.Name) ?? string.Empty, script.Flags, properties)
        {
            Unsupported = script.Unsupported != 0,
            TrailingBytes = checked((long)script.TrailingBytes),
            Stop = ReadStop(handle),
        };
    }

    private static VmadReadStop? ReadStop(IntPtr handle)
    {
        nuint scriptIndex;
        IntPtr name;
        byte type;
        nuint declared;
        var status = RadicleNative.VmadScriptsStop(handle, &scriptIndex, &name, &type, &declared);
        if (status == Status.NotFound) return null;
        Call.Check(status);
        return new VmadReadStop(checked((int)scriptIndex), Utf8.Read(name) ?? string.Empty, type, checked((int)declared));
    }

    private static bool IsContainer(byte type) => type == 7 || (type >= 11 && type <= 15) || type == 17;

    private static VmadElementInfo[] Children(IntPtr handle, nuint node)
    {
        var rows = new VmadElementInfo[checked((int)RadicleNative.VmadNodeChildCount(handle, node))];
        for (var index = 0; index < rows.Length; ++index)
        {
            VmadElement element;
            Call.Check(RadicleNative.VmadNodeChild(handle, node, (nuint)index, &element));
            rows[index] = Row(element);
            if (!IsContainer(element.Type)) continue;
            nuint child;
            Call.Check(RadicleNative.VmadNodeChildNode(handle, node, (nuint)index, &child));
            rows[index] = rows[index] with { Children = Children(handle, child) };
        }
        return rows;
    }

    private static VmadElementInfo Row(VmadElement element) => new(
        element.Kind,
        Utf8.Read(element.MemberName),
        checked((int)element.StructIndex),
        checked((int)element.IndexInProperty),
        element.Type,
        element.Flags,
        Value(element.Kind, element.ObjectFormId, element.Alias, element.Text, element.Integer, element.Real, element.Boolean));

    private static VmadElementInfo[] Elements(IntPtr handle, int script, int property, nuint total)
    {
        if (total == 0) return [];
        var rows = new VmadElementInfo[checked((int)total)];
        for (var index = 0; index < rows.Length; ++index)
        {
            VmadElement element;
            Call.Check(RadicleNative.VmadScriptsElement(handle, (nuint)script, (nuint)property, (nuint)index, &element));
            rows[index] = Row(element);
            if (!IsContainer(element.Type)) continue;
            nuint node;
            Call.Check(RadicleNative.VmadScriptsElementNode(handle, (nuint)script, (nuint)property, (nuint)index, &node));
            rows[index] = rows[index] with { Children = Children(handle, node) };
        }
        return rows;
    }

    private static VmadPropertyValue Value(VmadValueKind kind, uint objectFormId, short alias, IntPtr text, int integer, float real, int boolean) =>
        new(kind, objectFormId, alias, Utf8.Read(text), integer, real, boolean != 0);
}
