using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;

namespace FO4RecordEditor.Services;

























public static partial class WriteService
{
    public static string DisablePrevis(object? env, string cellId, string patchPlugin, bool apply)
    {
        return PluginGraphGate.Run(patchPlugin, () => DisablePrevisCore(env, cellId, patchPlugin, apply));
    }

    private static string DisablePrevisCore(object? env, string cellId, string patchPlugin, bool apply)
    {
        if (!ResolveFk(env, cellId, out var fk))
            return ToolError.Fail($"'{cellId}' is not a FormKey and no loaded record has that EditorID.");

        var versions = MutagenLoader.GetRecordContexts(env, fk);
        var winningPair = versions.LastOrDefault();
        if (winningPair.rec is not ICellGetter cell)
            return ToolError.Fail($"{fk} is not a CELL record.");

        if (cell.Flags.HasFlag(Cell.Flag.IsInteriorCell))
            return ToolError.Fail($"{fk} ('{cell.EditorID}') is an interior cell. xEdit's own Disable PreVis " +
                "script only targets exterior cells -- interiors don't use the same previs/precombine system.");

        if (!cell.PreCombinedFilesTimestamp.HasValue)
            return $"{fk} ('{cell.EditorID}') has no precombine data (no PCMB/PreCombinedFilesTimestamp); nothing to disable.";

        if (!apply)
            return $"DRY RUN: would copy {fk} ('{cell.EditorID}') from '{winningPair.plugin}' into '{patchPlugin}', " +
                   "set the NoPreVis flag, and clear its previs/precombine data (PreVisFilesTimestamp, " +
                   "InPreVisFileOf, PreCombinedFilesTimestamp, CombinedMeshes, CombinedMeshReferences -- the " +
                   "typed equivalents of xEdit's VISI/RVIS/PCMB/XCRI). Re-run with apply=true.";

        if (string.IsNullOrWhiteSpace(patchPlugin))
            return ToolError.Fail("Choose a patch plugin to write the override into.");
        if (MutagenLoader.LinkCache is not ILinkCache<IFallout4Mod, IFallout4ModGetter> linkCache)
            return ToolError.Fail("No environment loaded. Load Env or Open MO2 first.");

        var (patchName, patchPath) = NormalizePlugin(patchPlugin);
        if (GetMutable(patchName) == null)
        {
            bool existsOnDisk = (patchPath != null && System.IO.File.Exists(patchPath))
                || MutagenLoader.LooseModPaths.ContainsKey(patchName)
                || FindPluginPath(patchName, env) != null;
            if (existsOnDisk) OpenPlugin(patchPlugin, env);
            else CreatePlugin(patchPlugin);
        }
        var patch = GetMutable(patchName);
        if (patch == null) return ToolError.Fail($"Could not open or create patch plugin '{patchName}'.");

        if (!linkCache.TryResolveContext<ICell, ICellGetter>(fk, out var ctx))
            return ToolError.Fail($"Could not resolve {fk}'s parent chain (Cell/Worldspace block tree) via the link cache.");

        ICell ovr;
        try { ovr = ctx.GetOrAddAsOverride(patch); }
        catch (Exception ex) { return ToolError.Fail($"Could not create the override in '{patchName}': {ex.Message}"); }

        ovr.MajorFlags |= Cell.MajorFlag.NoPreVis;
        ovr.PreVisFilesTimestamp = null;
        ovr.InPreVisFileOf.SetToNull();
        ovr.PreCombinedFilesTimestamp = null;
        ovr.CombinedMeshes.Clear();
        ovr.CombinedMeshReferences.Clear();

        MutagenLoader.InvalidateModIndex(patchName);
        NotifyChanged(patchName);
        return $"Disabled PreVis on {fk} ('{cell.EditorID}') in '{patchName}': set NoPreVis, cleared " +
               "PreVisFilesTimestamp/InPreVisFileOf/PreCombinedFilesTimestamp/CombinedMeshes/CombinedMeshReferences. " +
               $"Ensure {patchName} loads after '{winningPair.plugin}', then save_plugin.";
    }

    public static string StampPrevis(object? env, string cellId, string patchPlugin, ushort pcmbStamp, ushort visiStamp,
        bool apply)
    {
        return PluginGraphGate.Run(patchPlugin, () => StampPrevisCore(env, cellId, patchPlugin, pcmbStamp, visiStamp, apply));
    }

    private static string StampPrevisCore(object? env, string cellId, string patchPlugin, ushort pcmbStamp,
        ushort visiStamp, bool apply)
    {
        if (!ResolveFk(env, cellId, out var fk))
            return ToolError.Fail($"'{cellId}' is not a FormKey and no loaded record has that EditorID.");

        var versions = MutagenLoader.GetRecordContexts(env, fk);
        var winningPair = versions.LastOrDefault();
        if (winningPair.rec is not ICellGetter cell)
            return ToolError.Fail($"{fk} is not a CELL record.");

        if (!apply)
            return $"DRY RUN: would stamp {fk} ('{cell.EditorID}') into '{patchPlugin}' with " +
                   $"PreCombinedFilesTimestamp=0x{pcmbStamp:X4} and PreVisFilesTimestamp=0x{visiStamp:X4}. Re-run with apply=true.";

        if (string.IsNullOrWhiteSpace(patchPlugin))
            return ToolError.Fail("Choose a patch plugin to write the override into.");
        if (MutagenLoader.LinkCache is not ILinkCache<IFallout4Mod, IFallout4ModGetter> linkCache)
            return ToolError.Fail("No environment loaded. Load Env or Open MO2 first.");

        var (patchName, patchPath) = NormalizePlugin(patchPlugin);
        if (GetMutable(patchName) == null)
        {
            bool existsOnDisk = (patchPath != null && System.IO.File.Exists(patchPath))
                || MutagenLoader.LooseModPaths.ContainsKey(patchName)
                || FindPluginPath(patchName, env) != null;
            if (existsOnDisk) OpenPlugin(patchPlugin, env);
            else CreatePlugin(patchPlugin);
        }
        var patch = GetMutable(patchName);
        if (patch == null) return ToolError.Fail($"Could not open or create patch plugin '{patchName}'.");

        if (!linkCache.TryResolveContext<ICell, ICellGetter>(fk, out var ctx))
            return ToolError.Fail($"Could not resolve {fk}'s parent chain (Cell/Worldspace block tree) via the link cache.");

        ICell ovr;
        try { ovr = ctx.GetOrAddAsOverride(patch); }
        catch (Exception ex) { return ToolError.Fail($"Could not create the override in '{patchName}': {ex.Message}"); }

        ovr.MajorFlags &= ~Cell.MajorFlag.NoPreVis;
        ovr.PreCombinedFilesTimestamp = pcmbStamp;
        ovr.PreVisFilesTimestamp = visiStamp;

        MutagenLoader.InvalidateModIndex(patchName);
        NotifyChanged(patchName);
        return $"Stamped PreVis on {fk} ('{cell.EditorID}') in '{patchName}': PCMB=0x{pcmbStamp:X4}, VISI=0x{visiStamp:X4}. " +
               $"Ensure {patchName} loads after '{winningPair.plugin}', then save_plugin. XCRI still requires native Radicle stamp when mesh pairs are ready.";
    }
}
