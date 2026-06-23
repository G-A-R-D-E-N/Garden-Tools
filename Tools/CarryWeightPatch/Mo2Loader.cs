using System.IO;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Order;
using Noggog;

// Self-contained port of FO4RecordEditor.Services.Mo2ProfileLoader — reconstructs the MO2
// load order from disk (plugins.txt + modlist.txt + mod folders) into a real Mutagen load
// order + link cache, so a console tool can scan/patch the whole modlist. The only edit vs
// the original is dropping the editor-only `MutagenLoader.LinkCache = ...` line.
public static class Mo2Loader
{
    private const long FullReadMaxBytes = 16 * 1024 * 1024;

    public sealed class Mo2GameEnvironment
    {
        public required ILoadOrderGetter<IModListingGetter<IFallout4ModGetter>> LoadOrder { get; init; }
        public required ILinkCache LinkCache { get; init; }
        public required DirectoryPath DataFolderPath { get; init; }
    }

    public sealed record Mo2Info(string InstancePath, string Profile, string GameDataFolder, string OverwriteFolder);

    public static Mo2Info ReadInstanceInfo(string instancePath)
    {
        string profile = "Default", gamePath = "";
        var ini = Path.Combine(instancePath, "ModOrganizer.ini");
        if (File.Exists(ini))
            foreach (var raw in File.ReadAllLines(ini))
            {
                var line = raw.Trim();
                if (line.StartsWith("selected_profile=", StringComparison.OrdinalIgnoreCase)) profile = Unwrap(line);
                else if (line.StartsWith("gamePath=", StringComparison.OrdinalIgnoreCase)) gamePath = Unwrap(line);
            }
        var dataFolder = string.IsNullOrWhiteSpace(gamePath) ? "" : Path.Combine(gamePath, "Data");
        return new Mo2Info(instancePath, profile, dataFolder, ResolveOverwriteFolder(instancePath));
    }

    public static string ResolveOverwriteFolder(string instancePath)
    {
        string baseDir = "", overwriteDir = "";
        var ini = Path.Combine(instancePath, "ModOrganizer.ini");
        if (File.Exists(ini))
            foreach (var raw in File.ReadAllLines(ini))
            {
                var line = raw.Trim();
                if (line.StartsWith("base_directory=", StringComparison.OrdinalIgnoreCase)) baseDir = Unwrap(line);
                else if (line.StartsWith("overwrite_directory=", StringComparison.OrdinalIgnoreCase)) overwriteDir = Unwrap(line);
            }
        if (string.IsNullOrWhiteSpace(baseDir)) baseDir = instancePath;
        baseDir = baseDir.Replace("%BASE_DIR%", instancePath);
        var ow = string.IsNullOrWhiteSpace(overwriteDir) ? Path.Combine(baseDir, "overwrite") : overwriteDir.Replace("%BASE_DIR%", baseDir);
        ow = ow.Replace('/', Path.DirectorySeparatorChar);
        if (!Path.IsPathRooted(ow)) ow = Path.Combine(baseDir, ow);
        try { return Path.GetFullPath(ow); } catch { return ow; }
    }

    private static string Unwrap(string line)
    {
        var v = line[(line.IndexOf('=') + 1)..].Trim();
        const string prefix = "@ByteArray(";
        if (v.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && v.EndsWith(")")) v = v[prefix.Length..^1];
        return v.Replace("\\\\", "\\");
    }

    public static (Mo2GameEnvironment env, List<string> plugins) Load(string instancePath, string profile, string gameDataFolder)
    {
        var profileDir = Path.Combine(instancePath, "profiles", profile);
        var pluginsTxt = Path.Combine(profileDir, "plugins.txt");
        var modlistTxt = Path.Combine(profileDir, "modlist.txt");
        if (!File.Exists(pluginsTxt)) throw new FileNotFoundException($"plugins.txt not found for profile '{profile}'", pluginsTxt);

        var loadOrder = File.ReadAllLines(pluginsTxt).Select(l => l.Trim())
            .Where(l => l.StartsWith('*')).Select(l => l[1..].Trim()).Where(l => l.Length > 0).ToList();

        var present = new HashSet<string>(loadOrder, StringComparer.OrdinalIgnoreCase);
        var implicitMasters = Mutagen.Bethesda.Plugins.Implicits.Get(GameRelease.Fallout4).Listings
            .Select(m => m.FileName.String).Where(m => !present.Contains(m)).ToList();
        loadOrder = implicitMasters.Concat(loadOrder).ToList();

        var modsHighToLow = File.Exists(modlistTxt)
            ? File.ReadAllLines(modlistTxt).Select(l => l.Trim()).Where(l => l.StartsWith('+'))
                .Select(l => l[1..].Trim()).Where(l => l.Length > 0 && !l.EndsWith("_separator", StringComparison.OrdinalIgnoreCase)).ToList()
            : new List<string>();

        var resolved = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        void Index(string folder)
        {
            if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder)) return;
            foreach (var f in Directory.EnumerateFiles(folder))
            {
                var ext = Path.GetExtension(f);
                if (ext.Equals(".esp", StringComparison.OrdinalIgnoreCase) || ext.Equals(".esm", StringComparison.OrdinalIgnoreCase) || ext.Equals(".esl", StringComparison.OrdinalIgnoreCase))
                    resolved[Path.GetFileName(f)] = f;
            }
        }
        Index(gameDataFolder);
        var modsDir = Path.Combine(instancePath, "mods");
        for (int i = modsHighToLow.Count - 1; i >= 0; i--) Index(Path.Combine(modsDir, modsHighToLow[i]));
        Index(ResolveOverwriteFolder(instancePath));

        var listings = new List<IModListingGetter<IFallout4ModGetter>>();
        var loaded = new List<string>();
        int missing = 0;
        foreach (var name in loadOrder)
        {
            if (!resolved.TryGetValue(name, out var path)) { missing++; continue; }
            try
            {
                var modKey = ModKey.FromNameAndExtension(name);
                long size = 0; try { size = new FileInfo(path).Length; } catch { }
                IFallout4ModGetter mod = size > 0 && size <= FullReadMaxBytes
                    ? Fallout4Mod.CreateFromBinary(ModPath.FromPath(path), Fallout4Release.Fallout4)
                    : Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(path), Fallout4Release.Fallout4);
                listings.Add(new ModListing<IFallout4ModGetter>(modKey, mod, enabled: true, ghostSuffix: string.Empty));
                loaded.Add(name);
            }
            catch { missing++; }
        }

        var lo = new LoadOrder<IModListingGetter<IFallout4ModGetter>>(listings, disposeItems: true);
        var linkCache = lo.ToImmutableLinkCache<IFallout4Mod, IFallout4ModGetter>();
        Console.WriteLine($"Loaded {loaded.Count} plugins ({missing} unresolved) from profile '{profile}'.");
        var env = new Mo2GameEnvironment { LoadOrder = lo, LinkCache = linkCache, DataFolderPath = new DirectoryPath(gameDataFolder) };
        return (env, loaded);
    }
}
