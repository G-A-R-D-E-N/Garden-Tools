using System.Collections;
using FO4RecordEditor.Models;
using Mutagen.Bethesda.Plugins;

namespace FO4RecordEditor.Services;

/// <summary>
/// Scans the entire loaded load order for record conflicts (FormKeys touched by 2+ plugins).
/// Memory-efficient: tracks the plugin list + winning record info per FormKey, not the records.
/// </summary>
public static class ConflictScanner
{
    private static readonly HashSet<string> VanillaMasters = new(StringComparer.OrdinalIgnoreCase)
    {
        "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
        "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm",
        "DLCUltraHighResolution.esm",
    };

    public static Task<List<ConflictEntry>> ScanAsync(object env, Action<string>? progress = null) =>
        Task.Run(() => Scan(env, progress));

    public static List<ConflictEntry> Scan(object env, Action<string>? progress = null)
    {
        dynamic e = env;
        var order = new List<(string name, Mutagen.Bethesda.Fallout4.IFallout4ModGetter mod)>();
        foreach (var l in (IEnumerable)e.LoadOrder.ListedOrder)
        {
            dynamic ld = l;
            if (ld.Mod is Mutagen.Bethesda.Fallout4.IFallout4ModGetter m)
                order.Add(((string)ld.ModKey.FileName.String, m));
        }

        // Pass 1 (cheap): which FormKeys are touched by 2+ distinct plugins?
        var firstPlugin = new Dictionary<FormKey, string>();
        var multi = new HashSet<FormKey>();
        foreach (var (name, mod) in order)
        {
            progress?.Invoke($"Scanning {name}...");
            foreach (var rec in mod.EnumerateMajorRecords())
            {
                if (!firstPlugin.TryGetValue(rec.FormKey, out var fp)) firstPlugin[rec.FormKey] = name;
                else if (!string.Equals(fp, name, StringComparison.OrdinalIgnoreCase)) multi.Add(rec.FormKey);
            }
        }

        // Pass 2: collect only the override records for those FormKeys (bounded to candidates).
        var recs = new Dictionary<FormKey, List<(string plugin, Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter rec)>>();
        foreach (var (name, mod) in order)
        {
            progress?.Invoke($"Comparing overrides in {name}...");
            foreach (var rec in mod.EnumerateMajorRecords())
            {
                if (!multi.Contains(rec.FormKey)) continue;
                if (!recs.TryGetValue(rec.FormKey, out var lst)) { lst = new(); recs[rec.FormKey] = lst; }
                lst.Add((name, rec));
            }
        }

        progress?.Invoke("Determining real conflicts...");
        var result = new List<ConflictEntry>();
        foreach (var kv in recs)
        {
            var entries = kv.Value;
            // Only the versions that DIFFER in content are real conflicts; identical
            // overrides (the benign Cell/NavMesh noise) are dropped, exactly like xEdit.
            var first = entries[0].rec;
            bool differs = entries.Any(en => !RecordsEqual(en.rec, first));
            if (!differs) continue;

            var plugins = entries.Select(en => en.plugin).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            if (plugins.Count < 2) continue;

            var win = entries[^1];
            bool involvesMod = plugins.Any(p => !VanillaMasters.Contains(p));
            result.Add(new ConflictEntry(kv.Key.ToString(), win.rec.EditorID ?? "",
                win.rec.Registration.Name, plugins, win.plugin, involvesMod));
        }

        return result
            .OrderByDescending(c => c.InvolvesMod)
            .ThenBy(c => c.Type, StringComparer.Ordinal)
            .ThenBy(c => c.EditorID, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private static bool RecordsEqual(
        Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter a,
        Mutagen.Bethesda.Plugins.Records.IMajorRecordGetter b)
    {
        try { return a.Equals(b); } catch { return false; }
    }
}
