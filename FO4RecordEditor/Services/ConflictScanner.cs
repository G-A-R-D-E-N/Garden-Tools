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

        var plugins = new Dictionary<FormKey, List<string>>();
        var winner = new Dictionary<FormKey, (string eid, string type)>();

        foreach (var l in (IEnumerable)e.LoadOrder.ListedOrder)
        {
            dynamic ld = l;
            if (ld.Mod == null) continue;
            string pname = ld.ModKey.FileName.String;
            if (ld.Mod is not Mutagen.Bethesda.Fallout4.IFallout4ModGetter m) continue;

            progress?.Invoke($"Scanning {pname}...");
            int c = 0;
            foreach (var rec in m.EnumerateMajorRecords())
            {
                var fk = rec.FormKey;
                if (!plugins.TryGetValue(fk, out var lst)) { lst = new(); plugins[fk] = lst; }
                if (lst.Count == 0 || !string.Equals(lst[^1], pname, StringComparison.OrdinalIgnoreCase))
                    lst.Add(pname);
                winner[fk] = (rec.EditorID ?? "", rec.Registration.Name);   // last in load order wins
                if (++c % 25000 == 0) progress?.Invoke($"{pname}: {c} records...");
            }
        }

        progress?.Invoke("Building conflict list...");
        var result = new List<ConflictEntry>();
        foreach (var kv in plugins)
        {
            if (kv.Value.Count < 2) continue;
            var (eid, type) = winner[kv.Key];
            bool involvesMod = kv.Value.Any(p => !VanillaMasters.Contains(p));
            result.Add(new ConflictEntry(kv.Key.ToString(), eid, type, kv.Value, kv.Value[^1], involvesMod));
        }

        // Mod-involving conflicts first (the ones the user actually cares about), then by type.
        return result
            .OrderByDescending(c => c.InvolvesMod)
            .ThenBy(c => c.Type, StringComparer.Ordinal)
            .ThenBy(c => c.EditorID, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }
}
