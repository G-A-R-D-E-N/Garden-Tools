// CombatConflictScanner — parses the full MO2 load order and emits a JSON report (plus ready-to-use
// RobCo Patcher INI lines) of everything that conflicts with the FallenWorldCombat system:
//
//   1. Coverage gaps    — armor with no DT_Phys_* keyword / ammo with no AmmoPierce_* keyword.
//                         These silently fall through the keyword-driven damage pipeline.
//   2. Competing overrides — armor/ammo/NPC records edited by multiple plugins (other mods fighting
//                         over the same combat-relevant data).
//   3. Race scaling gaps — races used by NPCs but absent from RaceScaling.json, so they spawn at
//                         vanilla bulk; plus the NPCs that drive each unscaled race.
//
// Output: out/combat_conflicts.json, out/robco_armor_DT.ini, out/robco_ammo_AmmoPierce.ini
//
// Usage: CombatConflictScanner [--instance <MO2 path>] [--profile <name>] [--racejson <path>] [--out <dir>]

using System.Text.Json;
using CombatConflictScanner;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;

// ── Args ──────────────────────────────────────────────────────────────────────
string instance = @"D:\Games\ModlistDownloads";
string? profile  = null;
string raceJson  = @"D:\Games\ModlistDownloads\mods\FallenWorldCombat\F4SE\Plugins\FallenWorldCombat\RaceScaling.json";
string outDir    = Path.Combine(AppContext.BaseDirectory, "out");

for (int i = 0; i < args.Length - 1; i++)
{
    switch (args[i].ToLowerInvariant())
    {
        case "--instance": instance = args[++i]; break;
        case "--profile":  profile  = args[++i]; break;
        case "--racejson": raceJson = args[++i]; break;
        case "--out":      outDir   = args[++i]; break;
    }
}

Console.WriteLine($"Instance : {instance}");
Console.WriteLine($"RaceJson : {raceJson}");
Console.WriteLine("Building load order from MO2 profile...");

var lo = Mo2LoadOrder.Build(instance, profile);
Console.WriteLine($"Profile '{lo.Profile}': {lo.LoadedPlugins.Count} plugins loaded, {lo.Missing.Count} unresolved.");

var cache = lo.LinkCache;

// Resolve a keyword link to its EditorID (cached).
var kwEidCache = new Dictionary<FormKey, string?>();
string? KeywordEid(IFormLinkGetter<IKeywordGetter> link)
{
    if (link.IsNull) return null;
    if (kwEidCache.TryGetValue(link.FormKey, out var cached)) return cached;
    string? eid = cache.TryResolve<IKeywordGetter>(link.FormKey, out var kw) ? kw.EditorID : null;
    kwEidCache[link.FormKey] = eid;
    return eid;
}

bool HasKeywordPrefix(IReadOnlyList<IFormLinkGetter<IKeywordGetter>>? kws, string prefix)
{
    if (kws == null) return false;
    foreach (var k in kws)
    {
        var eid = KeywordEid(k);
        if (eid != null && eid.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return true;
    }
    return false;
}

// Heuristic tier suggestions (tunable). Map armor rating / ammo damage to a DT/Pierce tier.
static string? SuggestDtPhys(ushort r) =>
    r >= 121 ? "DT_Phys_75" : r >= 81 ? "DT_Phys_50" : r >= 51 ? "DT_Phys_30" :
    r >= 31  ? "DT_Phys_20" : r >= 16 ? "DT_Phys_10" : r >= 1  ? "DT_Phys_5"  : null;

static string? SuggestPierce(float d) =>
    d >= 100 ? "AmmoPierce_75" : d >= 75 ? "AmmoPierce_50" : d >= 50 ? "AmmoPierce_30" :
    d >= 30  ? "AmmoPierce_20" : d >= 15 ? "AmmoPierce_10" : d > 0  ? "AmmoPierce_5"  : null;

static string RobCoRef(FormKey fk) => $"{fk.ModKey.FileName}|{fk.ID:X}";

// ── 1. Coverage gaps ───────────────────────────────────────────────────────────
Console.WriteLine("Scanning armor coverage...");
var armorGaps = new List<GapEntry>();
foreach (var armo in lo.LoadOrder.PriorityOrder.WinningOverrides<IArmorGetter>())
{
    // Skip templates / non-wearable shells with no body slots and zero rating.
    if (HasKeywordPrefix(armo.Keywords, "DT_Phys_")) continue;
    if (armo.ArmorRating == 0 && (armo.Keywords == null || armo.Keywords.Count == 0)) continue;
    armorGaps.Add(new GapEntry(
        armo.FormKey.ModKey.FileName, RobCoRef(armo.FormKey), armo.EditorID ?? "",
        armo.Name?.String ?? "", armo.ArmorRating, SuggestDtPhys(armo.ArmorRating)));
}

Console.WriteLine("Scanning ammo coverage...");
var ammoGaps = new List<GapEntry>();
foreach (var ammo in lo.LoadOrder.PriorityOrder.WinningOverrides<IAmmunitionGetter>())
{
    if (HasKeywordPrefix(ammo.Keywords, "AmmoPierce_")) continue;
    if (ammo.Damage <= 0f) continue;   // grenades / non-damage casings
    ammoGaps.Add(new GapEntry(
        ammo.FormKey.ModKey.FileName, RobCoRef(ammo.FormKey), ammo.EditorID ?? "",
        ammo.Name?.String ?? "", (int)ammo.Damage, SuggestPierce(ammo.Damage)));
}

// ── 2. Competing overrides ──────────────────────────────────────────────────────
// One pass over the load order: FormKey -> ordered plugin list, per combat-relevant type.
Console.WriteLine("Mapping override chains...");
var armorChain = new Dictionary<FormKey, List<string>>();
var ammoChain  = new Dictionary<FormKey, List<string>>();
var npcChain   = new Dictionary<FormKey, List<string>>();
var npcInfo    = new Dictionary<FormKey, (string eid, int health, string race)>();
var racesUsedByNpcs = new Dictionary<FormKey, string>();   // race FormKey -> race editorID

void Track(Dictionary<FormKey, List<string>> map, FormKey fk, string plugin)
{
    if (!map.TryGetValue(fk, out var list)) map[fk] = list = new();
    list.Add(plugin);
}

foreach (var listing in lo.LoadOrder.ListedOrder)
{
    var mod = listing.Mod;
    if (mod == null) continue;
    var plugin = listing.ModKey.FileName;

    foreach (var a in mod.Armors)       Track(armorChain, a.FormKey, plugin);
    foreach (var a in mod.Ammunitions)  Track(ammoChain,  a.FormKey, plugin);
    foreach (var n in mod.Npcs)
    {
        Track(npcChain, n.FormKey, plugin);
        // Capture winning (latest) health + race for the report.
        var raceFk = n.Race.FormKey;
        var raceEid = raceFk.IsNull ? "" : (cache.TryResolve<IRaceGetter>(raceFk, out var rc) ? rc.EditorID ?? "" : "");
        npcInfo[n.FormKey] = (n.EditorID ?? "", n.CalculatedHealth, raceEid);
        if (!raceFk.IsNull) racesUsedByNpcs[raceFk] = raceEid;
    }
}

List<OverrideEntry> Competing(Dictionary<FormKey, List<string>> map, bool npc = false)
{
    var list = new List<OverrideEntry>();
    foreach (var (fk, plugins) in map)
    {
        if (plugins.Count < 2) continue;   // single owner — nothing competing
        var eid = npc && npcInfo.TryGetValue(fk, out var ni) ? ni.eid : "";
        int? hp = npc && npcInfo.TryGetValue(fk, out var ni2) ? ni2.health : null;
        var race = npc && npcInfo.TryGetValue(fk, out var ni3) ? ni3.race : null;
        list.Add(new OverrideEntry(RobCoRef(fk), eid, plugins[^1], plugins, hp, race));
    }
    // Most-contested first.
    return list.OrderByDescending(e => e.Plugins.Count).ToList();
}

var armorOverrides = Competing(armorChain);
var ammoOverrides  = Competing(ammoChain);
var npcOverrides   = Competing(npcChain, npc: true);

// ── 3. Race scaling gaps ────────────────────────────────────────────────────────
Console.WriteLine("Checking race scaling coverage...");
var scaledRaces = LoadScaledRaceKeys(raceJson, lo);   // set of "plugin|ID" normalized
var unscaledRaces = new List<UnscaledRace>();
foreach (var (raceFk, raceEid) in racesUsedByNpcs)
{
    var key = $"{raceFk.ModKey.FileName}|{raceFk.ID:X}".ToUpperInvariant();
    if (scaledRaces.Contains(key)) continue;
    // Count NPCs of this race for priority.
    int npcCount = npcInfo.Count(kv => kv.Value.race == raceEid && raceEid.Length > 0);
    unscaledRaces.Add(new UnscaledRace(raceFk.ModKey.FileName, RobCoRef(raceFk), raceEid, npcCount));
}
unscaledRaces = unscaledRaces.OrderByDescending(r => r.NpcCount).ToList();

// ── Output ───────────────────────────────────────────────────────────────────
Directory.CreateDirectory(outDir);

var report = new
{
    generatedUtc = DateTime.UtcNow.ToString("o"),
    profile = lo.Profile,
    loadOrderCount = lo.LoadedPlugins.Count,
    unresolvedPlugins = lo.Missing,
    coverageGaps = new
    {
        armorMissingDT = armorGaps.OrderByDescending(g => g.Value).ToList(),
        ammoMissingPierce = ammoGaps.OrderByDescending(g => g.Value).ToList(),
    },
    competingOverrides = new
    {
        armor = armorOverrides,
        ammo = ammoOverrides,
        npc = npcOverrides,
    },
    raceScaling = new
    {
        unscaledRacesUsedByNpcs = unscaledRaces,
    },
};

var jsonOpts = new JsonSerializerOptions { WriteIndented = true };
var jsonPath = Path.Combine(outDir, "combat_conflicts.json");
File.WriteAllText(jsonPath, JsonSerializer.Serialize(report, jsonOpts));

// RobCo Patcher INI files — group gap records by suggested keyword, chunk filter lists.
WriteRobCoIni(Path.Combine(outDir, "robco_armor_DT.ini"), "filterByArmors", armorGaps);
WriteRobCoIni(Path.Combine(outDir, "robco_ammo_AmmoPierce.ini"), "filterByAmmo", ammoGaps);

// ── Summary ────────────────────────────────────────────────────────────────────
Console.WriteLine();
Console.WriteLine("=== SUMMARY ===");
Console.WriteLine($"Armor missing DT_Phys keyword   : {armorGaps.Count}");
Console.WriteLine($"Ammo missing AmmoPierce keyword : {ammoGaps.Count}");
Console.WriteLine($"Armor edited by 2+ plugins      : {armorOverrides.Count}");
Console.WriteLine($"Ammo edited by 2+ plugins        : {ammoOverrides.Count}");
Console.WriteLine($"NPCs edited by 2+ plugins         : {npcOverrides.Count}");
Console.WriteLine($"Races used by NPCs but unscaled : {unscaledRaces.Count}");
Console.WriteLine();
Console.WriteLine($"JSON   -> {jsonPath}");
Console.WriteLine($"RobCo  -> {Path.Combine(outDir, "robco_armor_DT.ini")}");
Console.WriteLine($"RobCo  -> {Path.Combine(outDir, "robco_ammo_AmmoPierce.ini")}");

// ── Helpers ────────────────────────────────────────────────────────────────────
static HashSet<string> LoadScaledRaceKeys(string raceJson, Mo2LoadOrder.Result lo)
{
    var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    if (!File.Exists(raceJson)) { Console.WriteLine($"[warn] RaceScaling.json not found: {raceJson}"); return set; }
    try
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(raceJson));
        if (doc.RootElement.TryGetProperty("races", out var races))
            foreach (var r in races.EnumerateArray())
            {
                var plugin = r.TryGetProperty("plugin", out var p) ? p.GetString() ?? "" : "";
                var formID = r.TryGetProperty("formID", out var f) ? f.GetString() ?? "" : "";
                if (plugin.Length == 0 || formID.Length == 0) continue;
                // Normalize: strip leading zeros, uppercase, match "plugin|ID".
                var id = uint.Parse(formID, System.Globalization.NumberStyles.HexNumber);
                set.Add($"{plugin}|{id:X}".ToUpperInvariant());
            }
    }
    catch (Exception ex) { Console.WriteLine($"[warn] RaceScaling.json parse failed: {ex.Message}"); }
    return set;
}

static void WriteRobCoIni(string path, string filterKey, List<GapEntry> gaps)
{
    // Group by suggested keyword; each line lists up to N forms for that keyword.
    const int chunk = 40;
    using var w = new StreamWriter(path);
    w.WriteLine("; Auto-generated by CombatConflictScanner — assign FallenWorldCombat keywords to");
    w.WriteLine("; records that lacked them. Review tiers before use (suggestions are heuristic).");
    w.WriteLine("; Drop into a mod's F4SE\\Plugins\\RobCo_Patcher\\" + (filterKey.Contains("Armor") ? "armor" : "ammo") + "\\ folder.");
    w.WriteLine();
    foreach (var grp in gaps.Where(g => g.SuggestedKeyword != null)
                            .GroupBy(g => g.SuggestedKeyword!)
                            .OrderBy(g => g.Key))
    {
        w.WriteLine($"// {grp.Key} ({grp.Count()} records)");
        var refs = grp.Select(g => g.RobCoRef).ToList();
        for (int i = 0; i < refs.Count; i += chunk)
        {
            var slice = refs.Skip(i).Take(chunk);
            w.WriteLine($"{filterKey}={string.Join(",", slice)}:keywordsToAdd={grp.Key}");
        }
        w.WriteLine();
    }
}

// ── DTOs ───────────────────────────────────────────────────────────────────────
record GapEntry(string Plugin, string RobCoRef, string EditorID, string Name, int Value, string? SuggestedKeyword);
record OverrideEntry(string RobCoRef, string EditorID, string WinningPlugin, List<string> Plugins, int? Health, string? Race);
record UnscaledRace(string Plugin, string RobCoRef, string EditorID, int NpcCount);
