using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed unsafe partial class PluginFile
{
    public (ushort? Index, string? Name) ReadSceneActionStartPhase(FormKey scene, int action, int index)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        byte hasIndex, hasName;
        byte* name;
        ushort phase;
        nuint size;
        Call.Check(RadicleNative.PluginReadSceneActionStartPhase(use.Pointer, strings.Key(scene), Index(action), Index(index),
            &hasIndex, &phase, &hasName, &name, &size));
        var text = Call.TakeText(name, size);
        return (hasIndex != 0 ? phase : null, hasName != 0 ? text : null);
    }

    public bool SetSceneActionStartPhaseIndex(FormKey scene, int action, int index, ushort? value)
    {
        var at = Index(action);
        var entry = Index(index);
        return Edit(scene, (plugin, strings, key) => RadicleNative.PluginSetSceneActionStartPhaseIndex(
            plugin, key, at, entry, value.HasValue ? (byte)1 : (byte)0, value.GetValueOrDefault()));
    }

    public bool SetSceneActionStartPhaseName(FormKey scene, int action, int index, string? value)
    {
        var at = Index(action);
        var entry = Index(index);
        return Edit(scene, (plugin, strings, key) => RadicleNative.PluginSetSceneActionStartPhaseName(plugin, key, at, entry, value));
    }

    public bool InsertSceneActionStartScene(FormKey scene, int action, int index, FormKey target)
    {
        var at = Index(action);
        var entry = Index(index);
        return Edit(scene, (plugin, strings, key) =>
        {
            var native = strings.Key(target);
            return RadicleNative.PluginInsertSceneActionStartScene(plugin, key, at, entry, &native);
        });
    }

    public bool SetSceneActionStartScene(FormKey scene, int action, int index, FormKey target)
    {
        var at = Index(action);
        var entry = Index(index);
        return Edit(scene, (plugin, strings, key) =>
        {
            var native = strings.Key(target);
            return RadicleNative.PluginSetSceneActionStartScene(plugin, key, at, entry, &native);
        });
    }

    public bool RemoveSceneActionStartScene(FormKey scene, int action, int index)
    {
        var at = Index(action);
        var entry = Index(index);
        return Edit(scene, (plugin, strings, key) => RadicleNative.PluginRemoveSceneActionStartScene(plugin, key, at, entry));
    }

    public IReadOnlyList<FormKey> ReadSceneActionPackages(FormKey scene, int action)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.ScenePackageList* list;
        Call.Check(RadicleNative.PluginReadSceneActionPackages(use.Pointer, strings.Key(scene), Index(action), &list));
        try
        {
            return Call.Copy<Native.FormKey>(list->Packages, list->Count)
                .Select(key => FormKey.FromNative(key) ?? throw new InvalidOperationException("Package link has no owner"))
                .ToArray();
        }
        finally { RadicleNative.ScenePackageListFree(list); }
    }

    public bool SetSceneActionPackages(FormKey scene, int action, IReadOnlyList<FormKey> packages)
    {
        ArgumentNullException.ThrowIfNull(packages);
        var at = Index(action);
        return Edit(scene, (plugin, strings, key) =>
        {
            var native = packages.Select(item => strings.Key(item)).ToArray();
            fixed (Native.FormKey* values = native)
                return RadicleNative.PluginSetSceneActionPackages(plugin, key, at, values, (nuint)native.Length);
        });
    }
}
