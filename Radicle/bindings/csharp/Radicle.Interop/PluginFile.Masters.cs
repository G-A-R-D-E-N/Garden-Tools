using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record PluginMaster(string Filename, PluginKind Kind = PluginKind.Full);

public sealed unsafe partial class PluginFile
{
    public void SetAuthor(string author)
    {
        ArgumentNullException.ThrowIfNull(author);
        using var use = Use();
        Call.Check(RadicleNative.PluginSetAuthor(use.Pointer, author));
    }

    public void SetDescription(string description)
    {
        ArgumentNullException.ThrowIfNull(description);
        using var use = Use();
        Call.Check(RadicleNative.PluginSetDescription(use.Pointer, description));
    }

    public void SetMasterFlag(bool master)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginSetMasterFlag(use.Pointer, master ? 1 : 0));
    }

    public void SetLocalizedFlag(bool localized)
    {
        using var use = Use();
        Call.Check(RadicleNative.PluginSetLocalizedFlag(use.Pointer, localized ? 1 : 0));
    }

    public void RemapMasters(IReadOnlyList<PluginMaster> masters)
    {
        ArgumentNullException.ThrowIfNull(masters);
        using var use = Use();
        using var strings = new NativeStrings();
        var native = new MasterDescriptor[masters.Count];
        for (var i = 0; i < native.Length; ++i)
            native[i] = new MasterDescriptor { Filename = strings.Add(masters[i].Filename), Kind = masters[i].Kind };
        fixed (MasterDescriptor* p = native)
            Call.Check(RadicleNative.PluginRemapMasters(use.Pointer, p, (nuint)native.Length));
    }

    public void StampMasterKinds(IReadOnlyList<string> masterPaths)
    {
        ArgumentNullException.ThrowIfNull(masterPaths);
        using var use = Use();
        using var strings = new NativeStrings();
        var native = strings.AddAll(masterPaths);
        fixed (IntPtr* p = native)
            Call.Check(RadicleNative.PluginStampMasterKinds(use.Pointer, p, (nuint)native.Length));
    }

    public IReadOnlyList<string> CollectFormKeys()
    {
        using var use = Use();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.PluginCollectFormKeys(use.Pointer, &items, &count));
        return Call.TakeStrings(items, count);
    }

    public IReadOnlyList<string> CollectFormLinks()
    {
        using var use = Use();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.PluginCollectFormLinks(use.Pointer, &items, &count));
        return Call.TakeStrings(items, count);
    }
}
