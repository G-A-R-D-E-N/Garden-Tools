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
    uint DeclaredElementCount);

public sealed record VmadScriptInfo(string Name, byte Flags, IReadOnlyList<VmadPropertyInfo> Properties);

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

internal static unsafe class VmadScriptReader
{
    internal static VmadScriptInfo[] Read(IntPtr handle)
    {
        var count = RadicleNative.VmadScriptCount(handle);
        var result = new VmadScriptInfo[checked((int)count)];
        for (var index = 0; index < result.Length; ++index)
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
                    new VmadPropertyValue(
                        property.ValueKind,
                        property.ObjectFormId,
                        property.Alias,
                        Utf8.Read(property.Text),
                        property.Integer,
                        property.Real,
                        property.Boolean != 0),
                    property.DeclaredElementCount);
            }
            result[index] = new VmadScriptInfo(Utf8.Read(script.Name) ?? string.Empty, script.Flags, properties);
        }
        return result;
    }
}

// Constraint: a record without a VMAD subrecord must report false instead of throwing, matching PluginFile.TryScripts.
public sealed unsafe partial class RadicleEnvironment
{
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
