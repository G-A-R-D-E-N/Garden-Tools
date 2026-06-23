using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;

// Find the real settlement/workshop LOCATION keyword so FallenWorldLoot's
// IsWorkshopLocation() matches the correct editorID instead of guessing.

var path = args.Length > 0 ? args[0]
    : @"D:\Games\ModlistDownloads\Stock Folder\Data\Fallout4.esm";

Console.WriteLine($"Loading {path}");
var mod = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(path), Fallout4Release.Fallout4);

// FormKey -> keyword EditorID, for resolving keyword links.
var kwName = new Dictionary<FormKey, string>();
foreach (var k in mod.Keywords) kwName[k.FormKey] = k.EditorID ?? "";

bool M(string? s, params string[] needles) =>
    s != null && needles.Any(n => s.Contains(n, StringComparison.OrdinalIgnoreCase));

Console.WriteLine("\n=== KYWD editorIDs matching workshop / settlement / loctype ===");
foreach (var k in mod.Keywords)
    if (M(k.EditorID, "workshop", "settlement", "loctype"))
        Console.WriteLine($"  {k.FormKey.ID:X6}  {k.EditorID}");

Console.WriteLine("\n=== Sample settlement LOCATIONS and the keywords they carry ===");
int shown = 0;
foreach (var loc in mod.Locations)
{
    var e = loc.EditorID ?? "";
    if (!M(e, "Sanctuary", "RedRocket", "Starlight", "Abernathy", "Workshop", "Settlement", "County Crossing", "Tenpines"))
        continue;
    var kws = loc.Keywords;
    if (kws == null || kws.Count == 0) continue;
    Console.WriteLine($"  LOC {loc.FormKey.ID:X6}  {e}");
    foreach (var link in kws)
        Console.WriteLine($"      - {(kwName.TryGetValue(link.FormKey, out var n) ? n : link.FormKey.ToString())}");
    if (++shown >= 8) break;
}

Console.WriteLine("\n=== Keyword frequency across ALL locations that have a 'workshop'-ish keyword ===");
var freq = new Dictionary<string, int>();
foreach (var loc in mod.Locations)
{
    if (loc.Keywords == null) continue;
    foreach (var link in loc.Keywords)
    {
        var n = kwName.TryGetValue(link.FormKey, out var nm) ? nm : "";
        if (M(n, "workshop", "settlement"))
            freq[n] = freq.GetValueOrDefault(n) + 1;
    }
}
foreach (var kv in freq.OrderByDescending(x => x.Value))
    Console.WriteLine($"  {kv.Value,5}  {kv.Key}");

Console.WriteLine("\nDone.");
