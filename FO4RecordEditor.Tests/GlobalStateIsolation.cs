using FO4RecordEditor.Services;

namespace FO4RecordEditor.Tests;

/// <summary>
/// Snapshots MutagenLoader's process-global registries + link cache and restores them on dispose,
/// so a test that loads an environment (Mo2ProfileLoader.Load / MutagenLoader.BuildEnvironment)
/// cannot leave ITS environment in the process globals for the next test class. Load() mutates the
/// globals by contract -- it is the app's "current environment" holder -- but every class that ran
/// after the loader was reading whatever the LAST loader class left behind: the exact class of
/// order-dependent static-state leak that made CellCompactionTests spin at 182% CPU in one suite
/// ordering and finish in 0.06s in another. Assembly-level test parallelization is already disabled
/// (AssemblyConfig.cs), so per-test save/restore makes the suite deterministic.
/// </summary>
/// <remarks>
/// Deliberately not captured:
/// - TextureService/AssetResolver session roots: setters only, no getters; only asset-resolving
///   tests read them and those set their own roots.
/// - Mo2ProfileLoader.FailedToLoad: written and read only by Mo2StartupLoadTests (and it is the
///   fixture under test there, not ambient state).
/// - RecordCountCache: keyed by plugin path, so synthetic temp paths can never collide with or
///   answer for a real plugin's entry.
/// - The per-mod index cache: a pure cache; dropping entries on restore is always safe (they
///   rebuild from the mod instance on demand), so restore = clear, not an exact snapshot.
/// </remarks>
public sealed class GlobalStateIsolation : IDisposable
{
    private readonly Mutagen.Bethesda.Plugins.Cache.ILinkCache? _linkCache;
    private readonly KeyValuePair<string, object>[] _looseMods;
    private readonly KeyValuePair<string, string>[] _looseModPaths;
    private readonly KeyValuePair<string, string>[] _pluginSourcePaths;
    private readonly KeyValuePair<string, object>[] _editableMods;
    private readonly KeyValuePair<string, bool>[] _masterIsEsl;
    private readonly int _maxCachedModIndexes;
    private readonly long _maxCachedIndexRecords;

    public GlobalStateIsolation()
    {
        _linkCache = MutagenLoader.LinkCache;
        _looseMods = MutagenLoader.LooseMods.ToArray();
        _looseModPaths = MutagenLoader.LooseModPaths.ToArray();
        _pluginSourcePaths = MutagenLoader.PluginSourcePaths.ToArray();
        _editableMods = MutagenLoader.EditableMods.ToArray();
        _masterIsEsl = MutagenLoader.MasterIsEsl.ToArray();
        _maxCachedModIndexes = MutagenLoader.MaxCachedModIndexes;
        _maxCachedIndexRecords = MutagenLoader.MaxCachedIndexRecords;
    }

    public void Dispose()
    {
        MutagenLoader.LinkCache = _linkCache;
        Restore(MutagenLoader.LooseMods, _looseMods);
        Restore(MutagenLoader.LooseModPaths, _looseModPaths);
        Restore(MutagenLoader.PluginSourcePaths, _pluginSourcePaths);
        Restore(MutagenLoader.EditableMods, _editableMods);
        Restore(MutagenLoader.MasterIsEsl, _masterIsEsl);
        MutagenLoader.MaxCachedModIndexes = _maxCachedModIndexes;
        MutagenLoader.MaxCachedIndexRecords = _maxCachedIndexRecords;
        MutagenLoader.ClearModIndexCacheForTest();
        ConflictScanner.InvalidateCache();
    }

    // Works for both ConcurrentDictionary and plain Dictionary through IDictionary.
    private static void Restore<TKey, TValue>(
        IDictionary<TKey, TValue> dict,
        KeyValuePair<TKey, TValue>[] snapshot)
        where TKey : notnull
    {
        dict.Clear();
        foreach (var kv in snapshot) dict[kv.Key] = kv.Value;
    }
}
