using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;

namespace FO4RecordEditor.Services;

public static partial class WriteService
{
    public static string CompactToEsl(string plugin, object? env)
    {
        return PluginGraphGate.Run(plugin, () => CompactToEslCore(plugin, env));
    }

    private static string CompactToEslCore(string plugin, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var (first, last, capacity, range) = EslSlots(mod);

        var native = mod.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == mod.ModKey).ToList();
        var inRange = new HashSet<uint>(native.Select(r => r.FormKey.ID).Where(id => id >= first && id <= last));

        var remap = new Dictionary<FormKey, FormKey>();
        uint next = first;
        foreach (var r in native.Where(r => r.FormKey.ID < first || r.FormKey.ID > last).OrderBy(r => r.FormKey.ID))
        {
            while (inRange.Contains(next)) next++;
            if (next > last)
                return $"Cannot compact '{plugin}': it has more than {capacity} records, which won't fit the ESL range.";
            remap[r.FormKey] = new FormKey(mod.ModKey, next);
            inRange.Add(next);
            next++;
        }

        if (remap.Count == 0)
            return $"'{plugin}' is already ESL-compatible (all native form IDs within {range}).";

        if (!TryRekeyRecords(mod, remap, "compact_to_esl", plugin,
                $"Compact in xEdit instead, or keep cell-placed records within {range} when authoring them.",
                out var rekeyError))
            return rekeyError;

        mod.ModHeader.Stats.NextFormID = next;
        MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
        return $"Compacted {plugin}: remapped {remap.Count} record(s) into the ESL range ({range}) and fixed " +
               $"all references. Now save_plugin to a path ending in .esl (and the plugin can be a light master). " +
               $"This is in memory until you save.";
    }

    public static string CheckEslEligibility(string plugin, object? env)
    {
        return PluginGraphGate.Run(plugin, () => CheckEslEligibilityCore(plugin, env));
    }

    private static string CheckEslEligibilityCore(string plugin, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var (first, last, capacity, range) = EslSlots(mod);

        var native = mod.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == mod.ModKey).ToList();
        var outOfRange = native.Where(r => r.FormKey.ID < first || r.FormKey.ID > last).ToList();

        if (outOfRange.Count == 0)
            return $"'{plugin}' is already ESL-compatible: all {native.Count} native record(s) sit within {range}. " +
                   "compact_to_esl would be a no-op; save_plugin to a path ending in .esl directly.";

        if (native.Count > capacity)
            return $"'{plugin}' CANNOT fit the ESL range: {native.Count} native record(s) exceed the " +
                   $"{capacity}-slot capacity ({range}). compact_to_esl would refuse this plugin.";



        var topLevelGroups = mod.GetType().GetProperties()
            .Where(p => typeof(Mutagen.Bethesda.Plugins.Records.IGroup).IsAssignableFrom(p.PropertyType))
            .Select(p => p.GetValue(mod) as Mutagen.Bethesda.Plugins.Records.IGroup)
            .Where(g => g != null).Select(g => g!).ToList();
        var reachable = new HashSet<FormKey>(topLevelGroups
            .SelectMany(g => ((System.Collections.IEnumerable)g)
                .Cast<Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter>())
            .Select(r => r.FormKey));
        var unreachable = outOfRange.Where(r => !reachable.Contains(r.FormKey)).ToList();
        if (unreachable.Count > 0)
        {
            var byType = string.Join(", ", unreachable.GroupBy(r => r.GetType().Name).Select(g => $"{g.Key} x{g.Count()}"));
            return $"'{plugin}' would BLOCK on compact_to_esl: {unreachable.Count} out-of-range record(s) live in " +
                   $"nested groups this tool cannot re-key [{byType}] (cells / placed objects / worldspace sub-cells). " +
                   $"{outOfRange.Count - unreachable.Count} other out-of-range record(s) are otherwise fine. " +
                   $"Keep cell-placed records within {range} when authoring them, or compact in xEdit instead.";
        }

        return $"'{plugin}' is ESL-ELIGIBLE: {outOfRange.Count} of {native.Count} native record(s) sit outside " +
               $"{range} and would be remapped by compact_to_esl (dry, no failure modes detected). " +
               "Run compact_to_esl to actually perform the remap.";
    }

    private static (uint First, uint Last, int Capacity, string Range) EslSlots(IFallout4Mod mod)
    {
        var legacyFloor = mod.ModHeader.Stats.Version >= 1.0f && mod.MasterReferences.Count > 0 && HasForeignReference(mod);
        var first = legacyFloor ? 0x001u : 0x800u;
        const uint last = 0xFFFu;
        return (first, last, (int)(last - first + 1), $"0x{first:X3}-0x{last:X3}");
    }

    private static bool HasForeignReference(IFallout4Mod mod)
    {
        return mod.EnumerateMajorRecords().Any(r =>
            IsForeign(mod, r.FormKey) || r.EnumerateFormLinks().Any(link => IsForeign(mod, link.FormKey)));
    }

    private static bool IsForeign(IFallout4Mod mod, FormKey key) => !key.IsNull && key.ModKey != mod.ModKey;
}
