using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Order;

namespace CombatConflictScanner;

/// Builds a Mutagen load order + link cache from an MO2 instance by reading the profile and
/// mod folders directly off disk (no usvfs). Distilled from FO4RecordEditor's Mo2ProfileLoader:
///   profiles/<profile>/plugins.txt -> enabled plugins, in order (lines starting with '*')
///   profiles/<profile>/modlist.txt -> mod priority (top = highest, '+' = enabled)
///   game Data + mods/<mod>/ + overwrite/ -> where each plugin file actually lives
public static class Mo2LoadOrder
{
    public sealed record Result(
        LoadOrder<IModListingGetter<IFallout4ModGetter>> LoadOrder,
        ILinkCache LinkCache,
        List<string> LoadedPlugins,
        List<string> Missing,
        string Profile);

    public static Result Build(string instancePath, string? profileOverride = null)
    {
        var (profile, gameDataFolder) = ReadInstanceInfo(instancePath, profileOverride);

        var profileDir = Path.Combine(instancePath, "profiles", profile);
        var pluginsTxt = Path.Combine(profileDir, "plugins.txt");
        var modlistTxt = Path.Combine(profileDir, "modlist.txt");
        if (!File.Exists(pluginsTxt))
            throw new FileNotFoundException($"plugins.txt not found for profile '{profile}'", pluginsTxt);

        var loadOrder = File.ReadAllLines(pluginsTxt)
            .Select(l => l.Trim())
            .Where(l => l.StartsWith('*'))
            .Select(l => l[1..].Trim())
            .Where(l => l.Length > 0)
            .ToList();

        // FO4 force-loads the base masters; they aren't in plugins.txt. Prepend the implicit
        // listings (canonical order) that aren't already present.
        var present = new HashSet<string>(loadOrder, StringComparer.OrdinalIgnoreCase);
        var implicitMasters = Implicits.Get(GameRelease.Fallout4).Listings
            .Select(m => m.FileName.String)
            .Where(m => !present.Contains(m))
            .ToList();
        loadOrder = implicitMasters.Concat(loadOrder).ToList();

        var modsHighToLow = File.Exists(modlistTxt)
            ? File.ReadAllLines(modlistTxt)
                .Select(l => l.Trim())
                .Where(l => l.StartsWith('+'))
                .Select(l => l[1..].Trim())
                .Where(l => l.Length > 0 && !l.EndsWith("_separator", StringComparison.OrdinalIgnoreCase))
                .ToList()
            : new List<string>();

        // Resolve each plugin filename -> real path. Build lowest->highest priority so higher wins:
        // game Data (lowest) -> mods bottom->top -> overwrite (highest).
        var resolved = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        void Index(string folder)
        {
            if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder)) return;
            foreach (var f in Directory.EnumerateFiles(folder))
            {
                var ext = Path.GetExtension(f);
                if (ext.Equals(".esp", StringComparison.OrdinalIgnoreCase) ||
                    ext.Equals(".esm", StringComparison.OrdinalIgnoreCase) ||
                    ext.Equals(".esl", StringComparison.OrdinalIgnoreCase))
                    resolved[Path.GetFileName(f)] = f;
            }
        }

        Index(gameDataFolder);
        var modsDir = Path.Combine(instancePath, "mods");
        for (int i = modsHighToLow.Count - 1; i >= 0; i--)
            Index(Path.Combine(modsDir, modsHighToLow[i]));
        Index(Path.Combine(instancePath, "overwrite"));

        var listings = new List<IModListingGetter<IFallout4ModGetter>>();
        var loaded = new List<string>();
        var missing = new List<string>();
        foreach (var name in loadOrder)
        {
            if (!resolved.TryGetValue(name, out var path)) { missing.Add(name); continue; }
            try
            {
                var modKey = ModKey.FromNameAndExtension(name);
                // Overlay everything (read-only scan, fast + low memory).
                var mod = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(path), Fallout4Release.Fallout4);
                listings.Add(new ModListing<IFallout4ModGetter>(modKey, mod, enabled: true, ghostSuffix: string.Empty));
                loaded.Add(name);
            }
            catch { missing.Add(name); }
        }

        var lo = new LoadOrder<IModListingGetter<IFallout4ModGetter>>(listings, disposeItems: true);
        var linkCache = lo.ToImmutableLinkCache<IFallout4Mod, IFallout4ModGetter>();
        return new Result(lo, linkCache, loaded, missing, profile);
    }

    private static (string profile, string gameDataFolder) ReadInstanceInfo(string instancePath, string? profileOverride)
    {
        string profile = profileOverride ?? "Default";
        string gamePath = "";
        var ini = Path.Combine(instancePath, "ModOrganizer.ini");
        if (File.Exists(ini))
        {
            foreach (var raw in File.ReadAllLines(ini))
            {
                var line = raw.Trim();
                if (profileOverride is null && line.StartsWith("selected_profile=", StringComparison.OrdinalIgnoreCase))
                    profile = Unwrap(line);
                else if (line.StartsWith("gamePath=", StringComparison.OrdinalIgnoreCase))
                    gamePath = Unwrap(line);
            }
        }
        var dataFolder = string.IsNullOrWhiteSpace(gamePath) ? "" : Path.Combine(gamePath, "Data");
        return (profile, dataFolder);
    }

    // ModOrganizer.ini values: key=@ByteArray(value) with '\' escaped as '\\'.
    private static string Unwrap(string line)
    {
        var v = line[(line.IndexOf('=') + 1)..].Trim();
        const string prefix = "@ByteArray(";
        if (v.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && v.EndsWith(")"))
            v = v[prefix.Length..^1];
        return v.Replace("\\\\", "\\");
    }
}
