using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public static unsafe class PluginFileCellEdits
{
    public static FormKey CreateInteriorCell(this PluginFile plugin, string editorId)
    {
        ArgumentNullException.ThrowIfNull(editorId);
        using var use = plugin.Use();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateInteriorCell(use.Pointer, editorId, &key));
        return FormKey.Required(key, "created interior cell");
    }

    // Constraint: flags bit 0 must stay clear and 0 selects the shipped exterior value 0x0002; the worldspace must already exist.
    public static FormKey CreateExteriorCell(this PluginFile plugin, FormKey worldspace, int x, int y,
                                             string? editorId = null, ushort flags = 0)
    {
        using var use = plugin.Use();
        using var strings = new NativeStrings();
        var creation = new Native.ExteriorCellCreation
        {
            Worldspace = strings.Key(worldspace), X = x, Y = y, EditorId = strings.Add(editorId), Flags = flags,
        };
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateExteriorCell(use.Pointer, &creation, &key));
        return FormKey.Required(key, "created exterior cell");
    }

    // Constraint: flags bit 0 must stay clear and is written verbatim, so 0 stays 0; the worldspace must exist and not have a persistent cell yet.
    public static FormKey CreateWorldspacePersistentCell(this PluginFile plugin, FormKey worldspace, ushort flags = 0)
    {
        using var use = plugin.Use();
        using var strings = new NativeStrings();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateWorldspacePersistentCell(use.Pointer, strings.Key(worldspace), flags, &key));
        return FormKey.Required(key, "created persistent cell");
    }

    // Constraint: signature is a placed reference type or LAND, localId 0 takes the next free id, and flags, position and subrecords stay with the caller.
    public static FormKey CreateRawCellChild(this PluginFile plugin, FormKey cell, string signature, bool persistent = false,
                                             uint localId = 0, ushort formVersion = 0)
    {
        ArgumentNullException.ThrowIfNull(signature);
        using var use = plugin.Use();
        using var strings = new NativeStrings();
        Native.FormKey key;
        Call.Check(RadicleNative.PluginCreateRawCellChild(use.Pointer, strings.Key(cell), signature, persistent ? 1 : 0, localId, formVersion, &key));
        return FormKey.Required(key, "created cell child");
    }
}
