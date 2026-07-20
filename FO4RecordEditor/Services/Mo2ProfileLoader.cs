using System.IO;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Plugins.Order;
using Noggog;

namespace FO4RecordEditor.Services;

/// <summary>
/// Loads a Mod Organizer 2 modlist by reading its profile + mod folders **directly from disk**,
/// without running inside MO2's virtual file system. The editor cannot run under usvfs (the .NET
/// runtime dies on injection), so instead of relying on the VFS we reconstruct the same load
/// order MO2 would present:
///   - profiles/&lt;profile&gt;/plugins.txt  -> the plugin load order (lines starting with '*')
///   - profiles/&lt;profile&gt;/modlist.txt  -> mod priority (top = highest)
///   - mods/&lt;mod&gt;/, overwrite/, and the game Data folder -> where each plugin file lives
/// The result is a real Mutagen load order + link cache, wrapped so the rest of the app consumes
/// it exactly like a GameEnvironmentState (LoadOrder / LinkCache / DataFolderPath).
/// </summary>
public static class Mo2ProfileLoader
{
    // At/below this size a plugin is full-read (unlocked, save_plugin can File.Replace in place);
    // larger ones are mmap overlays (record data stays off the managed heap). See docs Code Notes.
    private const long FullReadMaxBytes = 1 * 1024 * 1024;   // 1 MB

    /// <summary>Minimal env shape the rest of the app accesses via dynamic.</summary>
    public sealed class Mo2GameEnvironment
    {
        public required ILoadOrderGetter<IModListingGetter<IFallout4ModGetter>> LoadOrder { get; init; }
        public required ILinkCache LinkCache { get; init; }
        public required DirectoryPath DataFolderPath { get; init; }

        /// <summary>
        /// Plugin file name -> the REAL on-disk path each one was actually loaded from (an MO2 mod
        /// folder, the overwrite folder, or the game Data folder -- whichever won by priority). The
        /// standard Mutagen ModListing this env's LoadOrder entries wrap has no Path property, and
        /// DataFolderPath here is just the vanilla game Data folder, so without this map
        /// WriteService.FindPluginPath had no way to re-locate an MO2-resolved plugin for
        /// open_plugin/save_plugin/the Masters panel -- every one of them failed with "could not
        /// locate on disk" for every MO2-managed mod, which is effectively all of them. Read-only
        /// browsing (list_plugins/get_record/...) never hit this: it only needs the mod already
        /// loaded into memory, which the LoadOrder entries do carry correctly.
        /// </summary>
        public required IReadOnlyDictionary<string, string> PluginPaths { get; init; }
    }

    public sealed record Mo2Info(string InstancePath, string Profile, string GameDataFolder, string OverwriteFolder);

    /// <summary>Reads ModOrganizer.ini at the instance root to find the active profile + game Data folder.</summary>
    public static Mo2Info ReadInstanceInfo(string instancePath)
    {
        string profile = "Default";
        string gamePath = "";
        var ini = Path.Combine(instancePath, "ModOrganizer.ini");
        if (File.Exists(ini))
        {
            foreach (var raw in File.ReadAllLines(ini))
            {
                var line = raw.Trim();
                if (line.StartsWith("selected_profile=", StringComparison.OrdinalIgnoreCase))
                    profile = Unwrap(line);
                else if (line.StartsWith("gamePath=", StringComparison.OrdinalIgnoreCase))
                    gamePath = Unwrap(line);
            }
        }

        // gamePath points at the game install root; plugins live in its Data subfolder.
        var dataFolder = string.IsNullOrWhiteSpace(gamePath) ? "" : Path.Combine(gamePath, "Data");
        return new Mo2Info(instancePath, profile, dataFolder, ResolveOverwriteFolder(instancePath));
    }

    /// <summary>The instance's real overwrite folder, honoring ModOrganizer.ini's base_directory /
    /// overwrite_directory (with the %BASE_DIR% placeholder). Defaults to &lt;instance&gt;\overwrite --
    /// correct for portable instances and for global instances whose ini sets explicit paths.</summary>
    public static string ResolveOverwriteFolder(string instancePath)
    {
        string baseDir = "", overwriteDir = "";
        var ini = Path.Combine(instancePath, "ModOrganizer.ini");
        if (File.Exists(ini))
        {
            foreach (var raw in File.ReadAllLines(ini))
            {
                var line = raw.Trim();
                if (line.StartsWith("base_directory=", StringComparison.OrdinalIgnoreCase))
                    baseDir = Unwrap(line);
                else if (line.StartsWith("overwrite_directory=", StringComparison.OrdinalIgnoreCase))
                    overwriteDir = Unwrap(line);
            }
        }

        if (string.IsNullOrWhiteSpace(baseDir)) baseDir = instancePath;
        baseDir = baseDir.Replace("%BASE_DIR%", instancePath);

        var ow = string.IsNullOrWhiteSpace(overwriteDir)
            ? Path.Combine(baseDir, "overwrite")
            : overwriteDir.Replace("%BASE_DIR%", baseDir);

        ow = ow.Replace('/', Path.DirectorySeparatorChar);
        if (!Path.IsPathRooted(ow)) ow = Path.Combine(baseDir, ow);
        try { return Path.GetFullPath(ow); } catch { return ow; }
    }

    // ModOrganizer.ini values are stored as: key=@ByteArray(value)  with '\' escaped as '\\'.
    private static string Unwrap(string line)
    {
        var v = line[(line.IndexOf('=') + 1)..].Trim();
        const string prefix = "@ByteArray(";
        if (v.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && v.EndsWith(")"))
            v = v[prefix.Length..^1];
        return v.Replace("\\\\", "\\");
    }

    /// <summary>
    /// Build the environment for an MO2 instance + profile. Returns the env (store on
    /// ShellViewModel.GameEnvironment) and the ordered list of loaded plugin filenames.
    /// </summary>
    public static (object env, List<string> plugins) Load(
        string instancePath, string profile, string gameDataFolder,
        IProgress<(string message, double? percent)>? progress = null)
    {
        progress?.Report(("Reading MO2 profile...", 0));

        var profileDir = Path.Combine(instancePath, "profiles", profile);
        var pluginsTxt = Path.Combine(profileDir, "plugins.txt");
        var modlistTxt = Path.Combine(profileDir, "modlist.txt");
        if (!File.Exists(pluginsTxt))
            throw new FileNotFoundException($"plugins.txt not found for profile '{profile}'", pluginsTxt);

        // Ordered load order (top = loaded first). Lines starting with '*' are enabled plugins.
        var loadOrder = File.ReadAllLines(pluginsTxt)
            .Select(l => l.Trim())
            .Where(l => l.StartsWith('*'))
            .Select(l => l[1..].Trim())
            .Where(l => l.Length > 0)
            .ToList();

        // Fallout 4 force-loads the base masters (Fallout4.esm + the official DLCs) -- they are
        // NOT written to plugins.txt. Without them the link cache is missing the entire base game:
        // vanilla records don't appear and FormLinks into them never resolve to names. Prepend the
        // implicit listings (in canonical order) that aren't already present; missing DLCs are
        // simply skipped later when their file isn't found on disk.
        var present = new HashSet<string>(loadOrder, StringComparer.OrdinalIgnoreCase);
        var implicitMasters = Mutagen.Bethesda.Plugins.Implicits.Get(GameRelease.Fallout4).Listings
            .Select(m => m.FileName.String)
            .Where(m => !present.Contains(m))
            .ToList();
        loadOrder = implicitMasters.Concat(loadOrder).ToList();

        // Enabled mods in priority order (modlist.txt top = highest). '+' enabled, '-' disabled,
        // separators end with '_separator', '*' markers are unmanaged (game Data) -- skip those.
        var modsHighToLow = File.Exists(modlistTxt)
            ? File.ReadAllLines(modlistTxt)
                .Select(l => l.Trim())
                .Where(l => l.StartsWith('+'))
                .Select(l => l[1..].Trim())
                .Where(l => l.Length > 0 && !l.EndsWith("_separator", StringComparison.OrdinalIgnoreCase))
                .ToList()
            : new List<string>();

        progress?.Report(($"Resolving {loadOrder.Count} plugins across {modsHighToLow.Count} mods...", 5));

        // Map plugin filename -> real path, honoring MO2 priority. Build from lowest priority to
        // highest so higher-priority sources overwrite: game Data (lowest) -> mods bottom->top ->
        // overwrite (highest).
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
        for (int i = modsHighToLow.Count - 1; i >= 0; i--)   // bottom -> top so top wins
            Index(Path.Combine(modsDir, modsHighToLow[i]));
        Index(ResolveOverwriteFolder(instancePath));   // honors ModOrganizer.ini base/overwrite paths

        // Load each plugin (in load order). Patch-sized plugins are read FULLY into memory so they
        // are not memory-mapped -- the file stays unlocked, so the AI/editor can save edits straight
        // back over it. Big masters (Fallout4.esm, DLCs, large mods) stay as lazy binary overlays
        // (fast, low memory); you don't edit those in place anyway.
        var listings = new List<IModListingGetter<IFallout4ModGetter>>();
        var loaded = new List<string>();
        var missing = new List<string>();
        var pluginPaths = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (int i = 0; i < loadOrder.Count; i++)
        {
            var name = loadOrder[i];
            double pct = 5 + (double)(i + 1) / loadOrder.Count * 90;
            progress?.Report(($"Loading {name} ({i + 1}/{loadOrder.Count})...", pct));

            if (!resolved.TryGetValue(name, out var path)) { missing.Add(name); continue; }
            try
            {
                var modKey = ModKey.FromNameAndExtension(name);
                long size = 0;
                try { size = new FileInfo(path).Length; } catch { }

                IFallout4ModGetter mod = size > 0 && size <= FullReadMaxBytes
                    ? Fallout4Mod.CreateFromBinary(ModPath.FromPath(path), Fallout4Release.Fallout4)      // unlocked, editable
                    : Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(path), Fallout4Release.Fallout4); // mmap, fast

                listings.Add(new ModListing<IFallout4ModGetter>(modKey, mod, enabled: true, ghostSuffix: string.Empty));
                loaded.Add(name);
                pluginPaths[name] = path;
            }
            catch
            {
                missing.Add(name);
            }
        }

        var lo = new LoadOrder<IModListingGetter<IFallout4ModGetter>>(listings, disposeItems: true);
        var linkCache = lo.ToImmutableLinkCache<IFallout4Mod, IFallout4ModGetter>();
        MutagenLoader.LinkCache = linkCache;   // lets the field walker resolve FormLinks to names

        // Record each plugin's ESL ("small master") flag so the save path can master-map custom
        // ActorValue condition params correctly (0xFE light-master encoding for ESL masters).
        MutagenLoader.MasterIsEsl.Clear();
        foreach (var l in listings)
            if (l.Mod != null) MutagenLoader.MasterIsEsl[l.ModKey.FileName.String] = l.Mod.IsSmallMaster;

        var summary = $"Loaded {loaded.Count} plugins from MO2 profile '{profile}'."
            + (missing.Count > 0
                ? $" {missing.Count} could not be resolved (e.g. {string.Join(", ", missing.Take(5))})."
                : "");
        progress?.Report((summary, 100));

        // So texture/mesh lookups (TextureService.ResolveDds/ResolveNif) search this ACTUAL modlist's
        // mods\ and overwrite\ folders, not whatever (if anything) is saved in settings.json.
        TextureService.SetSessionRoots(new[] { gameDataFolder, modsDir, ResolveOverwriteFolder(instancePath) });

        var env = new Mo2GameEnvironment
        {
            LoadOrder = lo,
            LinkCache = linkCache,
            DataFolderPath = new DirectoryPath(gameDataFolder),
            PluginPaths = pluginPaths,
        };
        return (env, loaded);
    }
}
