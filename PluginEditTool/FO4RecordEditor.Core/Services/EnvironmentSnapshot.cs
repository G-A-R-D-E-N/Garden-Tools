using System;
using System.Collections.Generic;
using System.Linq;
using Mutagen.Bethesda.Plugins.Cache;

namespace FO4RecordEditor.Services;

public sealed class EnvironmentSnapshot
{
    public ILinkCache? LinkCache { get; }
    public IReadOnlyDictionary<string, bool> MasterIsEsl { get; }
    public IReadOnlyDictionary<string, string> PluginSourcePaths { get; }
    public IReadOnlyList<string> AssetRoots { get; }

    internal EnvironmentSnapshot(
        ILinkCache? linkCache,
        IReadOnlyDictionary<string, bool> masterIsEsl,
        IReadOnlyDictionary<string, string> pluginSourcePaths,
        IReadOnlyList<string> assetRoots)
    {
        LinkCache = linkCache;
        MasterIsEsl = new Dictionary<string, bool>(masterIsEsl, StringComparer.OrdinalIgnoreCase);
        PluginSourcePaths = new Dictionary<string, string>(pluginSourcePaths, StringComparer.OrdinalIgnoreCase);
        AssetRoots = assetRoots.ToArray();
    }

    internal EnvironmentSnapshot WithLinkCache(ILinkCache? linkCache)
        => new(linkCache, MasterIsEsl, PluginSourcePaths, AssetRoots);
}
