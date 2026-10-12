using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;

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
        var duplicate = SharedFormKey(native);
        if (duplicate != null)
            return $"Cannot compact '{plugin}': form ID {duplicate.Key} is shared by {duplicate.Count()} records. Fix the duplicate in xEdit first.";
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
        {
            var lowered = LowerHedrNext(mod, native.Select(r => r.FormKey.ID), first, last);
            if (lowered is null)
                return $"'{plugin}' is already ESL-compatible (all native form IDs within {range}).";
            MutagenLoader.InvalidateModIndex(plugin); NotifyChanged(plugin);
            return $"'{plugin}' already has all native form IDs within {range}; lowered HEDR next object ID to 0x{lowered:X3}. In memory until save_plugin.";
        }

        if (!TryRekeyRecords(mod, remap, "compact_to_esl", plugin,
                $"Compact in xEdit instead, or keep cell-placed records within {range} when authoring them.",
                out var rekeyError))
            return rekeyError;

        while (next <= last && inRange.Contains(next)) next++;
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
        if (SharedFormKey(native) is { } duplicate)
            return $"'{plugin}' would BLOCK on compact_to_esl: form ID {duplicate.Key} is shared by {duplicate.Count()} records. " +
                   "Fix the duplicate in xEdit first.";
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

    public static string SetLightFlag(string plugin, bool light, object? env)
    {
        return PluginGraphGate.Run(plugin, () => SetLightFlagCore(plugin, light, env));
    }

    private static string SetLightFlagCore(string plugin, bool light, object? env)
    {
        var mod = EnsureOpen(plugin, env, out var msg); if (mod == null) return msg;
        var (name, _) = NormalizePlugin(plugin);

        bool wasLight = mod.ModHeader.Flags.HasFlag(Fallout4ModHeader.HeaderFlag.Small);
        if (light == wasLight)
            return $"'{name}' already {(light ? "has" : "does not have")} the ESL (Small) flag set.";

        if (light)
        {
            var (first, last, _, range) = EslSlots(mod);
            var native = mod.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == mod.ModKey).ToList();
            var outOfRange = native.Count(r => r.FormKey.ID < first || r.FormKey.ID > last);
            if (outOfRange > 0)
                return $"Cannot flag '{name}' light: {outOfRange} native record(s) sit outside {range}. Run compact_to_esl first.";

            var lowerNote = "";
            if (LowerHedrNext(mod, native.Select(r => r.FormKey.ID), first, last) is uint lowered)
                lowerNote = $" Lowered HEDR next object ID to 0x{lowered:X3}.";

            mod.ModHeader.Flags |= Fallout4ModHeader.HeaderFlag.Small;
            MutagenLoader.InvalidateModIndex(name); NotifyChanged(name);
            return $"Set the ESL (Small) flag on '{name}'.{lowerNote} In memory until save_plugin.";
        }

        mod.ModHeader.Flags &= ~Fallout4ModHeader.HeaderFlag.Small;
        MutagenLoader.InvalidateModIndex(name); NotifyChanged(name);
        return $"Cleared the ESL (Small) flag on '{name}'. In memory until save_plugin.";
    }

    private static (uint First, uint Last, int Capacity, string Range) EslSlots(IFallout4Mod mod)
    {
        var legacyFloor = mod.ModHeader.Stats.Version >= 1.0f && mod.MasterReferences.Count > 0 && HasForeignReference(mod);
        var first = legacyFloor ? 0x001u : 0x800u;
        const uint last = 0xFFFu;
        return (first, last, (int)(last - first + 1), $"0x{first:X3}-0x{last:X3}");
    }

    private static uint? LowerHedrNext(IFallout4Mod mod, IEnumerable<uint> nativeIds, uint first, uint last)
    {
        var lowered = nativeIds.Any() ? nativeIds.Max() + 1 : first;
        if (mod.ModHeader.Stats.NextFormID <= last || mod.ModHeader.Stats.NextFormID <= lowered) return null;
        mod.ModHeader.Stats.NextFormID = lowered;
        return lowered;
    }

    private static bool HasForeignReference(IFallout4Mod mod)
    {
        return mod.EnumerateMajorRecords().Any(r =>
            IsForeign(mod, r.FormKey) || r.EnumerateFormLinks().Any(link => IsForeign(mod, link.FormKey)));
    }

    private static bool IsForeign(IFallout4Mod mod, FormKey key) => !key.IsNull && key.ModKey != mod.ModKey;

    private static IGrouping<FormKey, IMajorRecordGetter>? SharedFormKey(IEnumerable<IMajorRecordGetter> native) =>
        native.GroupBy(r => r.FormKey).FirstOrDefault(g => g.Count() > 1);
}
