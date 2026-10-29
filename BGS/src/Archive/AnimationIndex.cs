using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace OpenCommonwealth.Services.Archive;

public enum AnimationKind
{
    Animation,
    Behaviour,
    Project,
    Character,
    Skeleton,
    Other,
}

public sealed record AnimationIndexEntry(
    string VirtualPath,
    string Source,
    AnimationKind Kind,
    string Project,
    string Race,
    string Character,
    string Folder,
    long Size,
    string Location);

public sealed record AnimationIndexProgress(int Done, int Total, string Phase);

public sealed record AnimationIndexSource(string Kind, string Path, string Source, string Stamp);

public sealed record AnimationIndexOptions(string? CachePath, bool ReuseCache)
{
    public static AnimationIndexOptions None { get; } = new(null, false);

    public static AnimationIndexOptions Cached(string? path) => new(path, true);

    public static AnimationIndexOptions Fresh(string? path) => new(path, false);
}

public sealed class AnimationIndexRequest
{
    public AnimationIndexRequest(string dataFolder, IReadOnlyList<string> modRootsHighToLow,
                                 IReadOnlyList<string> archivePaths, string? modlistPath, string? profile)
    {
        DataFolder = Full(dataFolder);
        ModRootsHighToLow = modRootsHighToLow;
        ArchivePaths = archivePaths;
        ModlistPath = modlistPath == null ? null : Full(modlistPath);
        Profile = profile;
    }

    public string DataFolder { get; }

    public IReadOnlyList<string> ModRootsHighToLow { get; }

    public IReadOnlyList<string> ArchivePaths { get; }

    public string? ModlistPath { get; }

    public string? Profile { get; }

    public static AnimationIndexRequest ForGameData(GameData data, string? modlistPath, string? profile)
    {
        var roots = new List<string>(data.ModRoots);
        roots.Reverse();
        return new AnimationIndexRequest(data.DataFolder, roots, new List<string>(data.ArchivePaths),
                                         modlistPath, profile);
    }

    public static AnimationIndexRequest ForDataFolder(string dataFolder)
    {
        using var data = GameData.Discover(dataFolder);
        return ForGameData(data, null, null);
    }

    public static AnimationIndexRequest ForModsFolder(string dataFolder, string modsFolder)
    {
        string? profile = GameData.ModlistProfile(modsFolder);
        string? modlist = GameData.ModlistPath(modsFolder, profile);
        if (modlist == null) return ForDataFolder(dataFolder);

        using var data = GameData.DiscoverModded(dataFolder, modsFolder, profile);
        return ForGameData(data, modlist, profile);
    }

    internal static AnimationIndexRequest FromLoadOrder(string? dataFolder, string? mo2Folder)
    {
        string? instance = ExistingFolder(mo2Folder) ?? ExistingFolder(dataFolder);
        if (instance != null && Mo2ScanData.TryOpen(instance, out Mo2ScanData? scan, out _))
        {
            using (scan)
                return ForGameData(scan!.Data, scan.Info.ModlistPath, scan.Info.Profile);
        }

        if (ExistingFolder(mo2Folder) is { } mods) return ForModsFolder(RequiredFolder(dataFolder), mods);

        GameData discovered = Mo2ScanData.OpenScanData(RequiredFolder(dataFolder));
        using (discovered)
            return ForGameData(discovered, null, null);
    }

    internal GameData OpenGameData()
    {
        var roots = new List<string>(ModRootsHighToLow);
        roots.Reverse();
        return GameData.FromParts(DataFolder, ArchivePaths, roots);
    }

    internal List<(string Kind, string Path, string Source)> LooseRoots()
    {
        var roots = new List<(string, string, string)>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string mod in ModRootsHighToLow)
        {
            string full = Full(mod);
            if (!seen.Add(full)) continue;
            roots.Add((AnimationIndex.ModKind, full, "loose:" + Leaf(full)));
        }

        if (seen.Add(DataFolder)) roots.Add((AnimationIndex.DataKind, DataFolder, "loose:Data"));
        return roots;
    }

    internal List<(string Kind, string Path, string Source)> ArchiveSources()
    {
        var list = new List<(string, string, string)>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string archive in ArchivePaths)
        {
            string full = Full(archive);
            if (seen.Add(full)) list.Add((AnimationIndex.ArchiveKind, full, Path.GetFileName(full)));
        }
        return list;
    }

    private static string? ExistingFolder(string? path) =>
        !string.IsNullOrWhiteSpace(path) && Directory.Exists(path) ? Path.GetFullPath(path) : null;

    private static string RequiredFolder(string? path) =>
        ExistingFolder(path) ?? throw new ArgumentException("no game Data folder was given", nameof(path));

    private static string Full(string path)
    {
        try
        {
            return Path.GetFullPath(path);
        }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return path;
        }
    }

    private static string Leaf(string path) =>
        Path.GetFileName(path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
}

public sealed class AnimationIndex
{
    internal const string ArchiveKind = "archive";
    internal const string DataKind = "loose-data";
    internal const string ModKind = "loose-mod";

    private static readonly string[] RoleFolders = { "Behaviors", "Animations", "Characters", "CharacterAssets" };

    private readonly List<AnimationIndexEntry> _entries;

    private AnimationIndex(AnimationIndexRequest request, List<AnimationIndexSource> sources,
                           List<AnimationIndexEntry> entries, int reused, int scanned, int rejected,
                           TimeSpan elapsed, string? note)
    {
        Request = request;
        Sources = sources;
        _entries = entries;
        ReusedSources = reused;
        ScannedSources = scanned;
        RejectedCacheEntries = rejected;
        Elapsed = elapsed;
        CacheNote = note;
    }

    public static string CacheFolder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "BehaviourGraphStudio", "cache");

    public static string DefaultCachePathFor(AnimationIndexRequest request) =>
        Path.Combine(CacheFolder, "animation-index-" + KeyHash(request) + ".json");

    internal static string KeyHash(AnimationIndexRequest request)
    {
        ulong hash = 14695981039346656037UL;
        hash = Mix(hash, KeyPart(request.DataFolder));
        hash = Mix(hash, "\u0000");
        hash = Mix(hash, KeyPart(request.Profile));
        hash = Mix(hash, "\u0000");
        hash = Mix(hash, KeyPart(request.ModlistPath));
        return hash.ToString("x16", CultureInfo.InvariantCulture);
    }

    private static string KeyPart(string? path) =>
        (path ?? "").TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToLowerInvariant();

    public AnimationIndexRequest Request { get; }

    public IReadOnlyList<AnimationIndexSource> Sources { get; }

    public IReadOnlyList<AnimationIndexEntry> Entries => _entries;

    public int ReusedSources { get; }

    public int ScannedSources { get; }

    public int RejectedCacheEntries { get; }

    public TimeSpan Elapsed { get; }

    public string? CacheNote { get; }

    public int Count(AnimationKind kind) => _entries.Count(entry => entry.Kind == kind);

    public static string Label(AnimationKind kind) => kind switch
    {
        AnimationKind.Animation => "animation",
        AnimationKind.Behaviour => "behaviour",
        AnimationKind.Project => "project",
        AnimationKind.Character => "character",
        AnimationKind.Skeleton => "skeleton",
        _ => "other",
    };

    public static string GroupLabel(AnimationKind kind) => kind switch
    {
        AnimationKind.Animation => "Animations",
        AnimationKind.Behaviour => "Behaviours",
        AnimationKind.Project => "Project files",
        AnimationKind.Character => "Characters",
        AnimationKind.Skeleton => "Skeletons",
        _ => "Other files",
    };

    public static bool IsIndexedName(string name) =>
        name.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) ||
        name.EndsWith(".hkt", StringComparison.OrdinalIgnoreCase);

    public static AnimationIndexEntry Classify(string virtualPath, string source, long size, string location)
    {
        string flat = (virtualPath ?? "").Replace('\\', '/').TrimStart('/');
        string folder = flat.Contains('/') ? flat[..flat.LastIndexOf('/')] : "";
        string[] parts = flat.Split('/', StringSplitOptions.RemoveEmptyEntries);

        int actors = Array.FindIndex(parts, part => part.Equals("Actors", StringComparison.OrdinalIgnoreCase));
        int role = Array.FindIndex(parts, part => RoleIndex(part) >= 0);

        string project = "";
        string race = "";
        if (actors >= 0)
        {
            int end = role > actors ? role : Math.Max(actors + 1, parts.Length - 1);
            if (end > actors + 1)
            {
                project = string.Join('/', parts[(actors + 1)..end]);
                race = parts[end - 1];
            }
        }

        AnimationKind kind = role < 0
            ? actors >= 0 ? AnimationKind.Project : AnimationKind.Other
            : KindOfRole(RoleIndex(parts[role]));

        string character = kind is AnimationKind.Character or AnimationKind.Skeleton
            ? Path.GetFileNameWithoutExtension(flat)
            : "";

        return new AnimationIndexEntry(flat, source, kind, project, race, character, folder, size, location);
    }

    public static AnimationIndex Build(AnimationIndexRequest request, AnimationIndexOptions options,
                                       Action<AnimationIndexProgress>? progress = null,
                                       CancellationToken token = default)
    {
        var watch = Stopwatch.StartNew();
        var planned = new List<(string Kind, string Path, string Source)>();
        planned.AddRange(request.LooseRoots());
        planned.AddRange(request.ArchiveSources());

        AnimationIndexCache? cache = options.CachePath == null ? null : new AnimationIndexCache(options.CachePath);
        AnimationIndexCacheData? stored = options.ReuseCache && cache != null ? cache.Load() : null;
        var notes = new List<string>();
        var warnings = new List<string>();
        var warned = new HashSet<string>(StringComparer.Ordinal);
        bool discarded = options.ReuseCache && cache != null && stored == null && File.Exists(cache.Path);
        if (discarded) notes.Add("the previous index cache was stale or unreadable and has been discarded");

        var reusable = new Dictionary<string, AnimationIndexCacheSource>(StringComparer.OrdinalIgnoreCase);
        if (stored != null && AnimationIndexCache.MatchesKey(stored, request))
            foreach (var source in stored.Sources)
                reusable[Identity(source.Kind, source.Path)] = source;

        var sources = new List<AnimationIndexSource>(planned.Count);
        var perSource = new Dictionary<string, List<AnimationIndexEntry>>(StringComparer.OrdinalIgnoreCase);
        var rowOf = new Dictionary<string, Dictionary<string, AnimationIndexEntry>>(StringComparer.OrdinalIgnoreCase);
        int done = 0, reused = 0, scanned = 0, rejected = 0;

        token.ThrowIfCancellationRequested();
        using GameData data = request.OpenGameData();

        var ordered = new List<(string Source, IEnumerable<(string VirtualPath, long Size)> Entries)>();

        progress?.Invoke(new AnimationIndexProgress(0, planned.Count, "reading sources"));
        foreach (var (kind, path, label) in planned)
        {
            token.ThrowIfCancellationRequested();
            done++;

            bool loose = kind != ArchiveKind;
            List<GameLooseFile> files = loose ? IndexedFiles(data.EnumerateLoose(path)) : new List<GameLooseFile>();
            string stamp = loose ? LooseStamp(files) : FileStamp(path);
            string identity = Identity(kind, path);

            List<AnimationIndexEntry> entries;
            if (reusable.TryGetValue(identity, out var known) && known.Stamp == stamp)
            {
                entries = FromCache(known, path, label, ref rejected);
                reused++;
                progress?.Invoke(new AnimationIndexProgress(done, planned.Count, "reused " + label));
            }
            else
            {
                entries = loose
                    ? files.Select(file => Classify(file.VirtualPath, label, file.Size, file.FullPath)).ToList()
                    : ArchiveEntries(data, path, label, token, message =>
                      {
                          if (warned.Add(message)) warnings.Add(message);
                      });
                scanned++;
                progress?.Invoke(new AnimationIndexProgress(done, planned.Count, "indexed " + label));
            }

            sources.Add(new AnimationIndexSource(kind, path, label, stamp));
            perSource[identity] = entries;
            rowOf[identity] = RowsOf(entries);
        }

        var rowsBySource = new Dictionary<string, IEnumerable<(string VirtualPath, long Size)>>(
            StringComparer.OrdinalIgnoreCase);
        foreach (AnimationIndexSource source in sources)
            rowsBySource[SourceKey(source.Kind, source.Path)] =
                perSource[Identity(source.Kind, source.Path)].Select(entry => (entry.VirtualPath, entry.Size));

        var emitted = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string source in data.WinnerSourceOrder())
            if (emitted.Add(source) && rowsBySource.TryGetValue(source, out var rows))
                ordered.Add((source, rows));

        token.ThrowIfCancellationRequested();
        progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "merging"));
        IReadOnlyList<GameAsset> winners = data.MergeWinners(ordered, token, offered =>
            progress?.Invoke(new AnimationIndexProgress(offered, planned.Count, "merging")));
        List<AnimationIndexEntry> merged = MergeWinners(SourceIdentities(sources), rowOf, winners);

        if (rejected > 0)
            notes.Add((rejected == 1 ? "1 cached entry" : rejected.ToString(CultureInfo.InvariantCulture) +
                                                                 " cached entries") +
                      " pointed outside its source and were dropped");

        notes.AddRange(warnings);

        if (cache != null)
        {
            progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "writing cache"));
            string? modlistStamp = FileStamp(request.ModlistPath);
            if (stored != null && (stored.ModlistStamp ?? "") != (modlistStamp ?? "") && !discarded)
                notes.Add("the modlist changed since the previous index build");

            var cacheData = new AnimationIndexCacheData(
                AnimationIndexCache.CurrentVersion, request.DataFolder, request.Profile, request.ModlistPath,
                modlistStamp, sources.Select(source => CacheOf(source, perSource)).ToList());
            if (!cache.Save(cacheData, out string failure)) notes.Add("the index cache could not be written: " + failure);
        }

        watch.Stop();
        progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "done"));
        return new AnimationIndex(request, sources, merged, reused, scanned, rejected, watch.Elapsed,
                                  notes.Count == 0 ? null : string.Join("; ", notes));
    }

    public static Task<AnimationIndex> BuildAsync(AnimationIndexRequest request, AnimationIndexOptions options,
                                                  Action<AnimationIndexProgress>? progress = null,
                                                  CancellationToken token = default) =>
        Task.Run(() => Build(request, options, progress, token), token);

    internal static string Identity(string kind, string path) => kind + "\u0000" + path;

    private static Dictionary<string, string> SourceIdentities(IReadOnlyList<AnimationIndexSource> sources)
    {
        var identities = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (AnimationIndexSource source in sources)
            identities[SourceKey(source.Kind, source.Path)] = Identity(source.Kind, source.Path);
        return identities;
    }

    private static string SourceKey(string kind, string path) =>
        (kind == ArchiveKind ? "archive:" : "loose:") + path;

    private static List<AnimationIndexEntry> MergeWinners(
        Dictionary<string, string> identityOfSource,
        Dictionary<string, Dictionary<string, AnimationIndexEntry>> rowOf, IReadOnlyList<GameAsset> winners)
    {
        var merged = new List<AnimationIndexEntry>(winners.Count);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (GameAsset winner in winners)
        {
            if (!identityOfSource.TryGetValue(winner.Source, out string? identity)) continue;
            if (!rowOf.TryGetValue(identity, out var rows)) continue;
            string flat = Flat(winner.VirtualPath);
            if (!rows.TryGetValue(flat, out AnimationIndexEntry? row) &&
                !rows.TryGetValue(GameData.Normalize(flat), out row)) continue;
            if (!seen.Add(identity + "\u0000" + Flat(row.VirtualPath))) continue;
            merged.Add(row);
        }

        return merged.OrderBy(entry => entry.VirtualPath, StringComparer.OrdinalIgnoreCase)
                     .ThenBy(entry => entry.Source, StringComparer.Ordinal)
                     .ToList();
    }

    private static Dictionary<string, AnimationIndexEntry> RowsOf(List<AnimationIndexEntry> entries)
    {
        var rows = new Dictionary<string, AnimationIndexEntry>(StringComparer.OrdinalIgnoreCase);
        foreach (AnimationIndexEntry entry in entries)
        {
            string flat = Flat(entry.VirtualPath);
            if (!rows.ContainsKey(flat)) rows[flat] = entry;
            string key = GameData.Normalize(flat);
            if (key.Length > 0 && !rows.ContainsKey(key)) rows[key] = entry;
        }
        return rows;
    }

    private static string Flat(string virtualPath) => virtualPath.Replace('\\', '/').TrimStart('/');

    private static List<GameLooseFile> IndexedFiles(IReadOnlyList<GameLooseFile> files)
    {
        var indexed = new List<GameLooseFile>(files.Count);
        foreach (GameLooseFile file in files)
            if (IsIndexedName(file.VirtualPath)) indexed.Add(file);
        return indexed;
    }

    private static AnimationIndexCacheSource CacheOf(AnimationIndexSource source,
                                                     Dictionary<string, List<AnimationIndexEntry>> perSource)
    {
        var entries = perSource[Identity(source.Kind, source.Path)];
        return new AnimationIndexCacheSource(source.Kind, source.Path, source.Source, source.Stamp,
            entries.Select(entry => entry.VirtualPath).ToList(),
            entries.Select(entry => entry.Size).ToList());
    }

    private static List<AnimationIndexEntry> FromCache(AnimationIndexCacheSource source, string root, string label,
                                                       ref int rejected)
    {
        var entries = new List<AnimationIndexEntry>(source.Paths.Count);
        bool loose = source.Kind != ArchiveKind;
        for (int i = 0; i < source.Paths.Count; i++)
        {
            string virtualPath = source.Paths[i];
            if (!IsSafeVirtualPath(virtualPath))
            {
                rejected++;
                continue;
            }

            string location;
            if (loose)
            {
                if (!TryLocation(source.Kind, root, virtualPath, out location) ||
                    !GameData.IsInsideRoot(root, location))
                {
                    rejected++;
                    continue;
                }
            }
            else
            {
                location = root;
            }

            long size = i < source.Sizes.Count ? source.Sizes[i] : 0;
            entries.Add(Classify(virtualPath, label, size, location));
        }
        return entries;
    }

    internal static bool IsSafeVirtualPath(string? virtualPath)
    {
        if (string.IsNullOrWhiteSpace(virtualPath)) return false;
        string flat = virtualPath.Replace('\\', '/');
        if (flat.Contains(':')) return false;
        if (flat.StartsWith('/')) return false;
        if (Path.IsPathRooted(flat)) return false;
        foreach (string part in flat.Split('/'))
            if (part == "..") return false;
        return IsIndexedName(flat);
    }

    private static bool TryLocation(string kind, string root, string virtualPath, out string location)
    {
        try
        {
            location = LocationOf(kind, root, virtualPath);
            return true;
        }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            location = "";
            return false;
        }
    }

    private static string LocationOf(string kind, string root, string virtualPath) =>
        kind == ArchiveKind ? root : Path.Combine(root, virtualPath.Replace('/', Path.DirectorySeparatorChar));

    private static List<AnimationIndexEntry> ArchiveEntries(GameData data, string path, string label,
                                                            CancellationToken token, Action<string> warn)
    {
        var entries = new List<AnimationIndexEntry>();
        if (!data.TryArchiveEntries(path, out IReadOnlyList<Ba2.Entry> found, out string? warning))
        {
            if (warning != null) warn(warning);
            return entries;
        }

        int seen = 0;
        foreach (Ba2.Entry entry in found)
        {
            if ((++seen & 0x3FF) == 0) token.ThrowIfCancellationRequested();
            if (!IsIndexedName(entry.Name)) continue;
            entries.Add(Classify(entry.Name, label, entry.Unpacked, path));
        }
        return entries;
    }

    private static string LooseStamp(List<GameLooseFile> files)
    {
        ulong hash = 14695981039346656037UL;
        long bytes = 0;
        long newest = 0;
        foreach (GameLooseFile file in files)
        {
            bytes += file.Size;
            if (file.ModifiedTicks > newest) newest = file.ModifiedTicks;
            hash = Mix(hash, file.VirtualPath);
            hash = Mix(hash, file.Size.ToString(CultureInfo.InvariantCulture));
            hash = Mix(hash, file.ModifiedTicks.ToString(CultureInfo.InvariantCulture));
        }

        return string.Join(':', files.Count.ToString(CultureInfo.InvariantCulture),
                           bytes.ToString(CultureInfo.InvariantCulture),
                           newest.ToString(CultureInfo.InvariantCulture),
                           hash.ToString("x16", CultureInfo.InvariantCulture));
    }

    private static ulong Mix(ulong hash, string text)
    {
        foreach (char value in text)
        {
            hash ^= value;
            hash *= 1099511628211UL;
        }
        return hash;
    }

    private static string FileStamp(string? path)
    {
        if (string.IsNullOrEmpty(path)) return "";
        try
        {
            var info = new FileInfo(path);
            if (!info.Exists) return "";
            return string.Join(':', info.Length.ToString(CultureInfo.InvariantCulture),
                               info.LastWriteTimeUtc.Ticks.ToString(CultureInfo.InvariantCulture));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException
                                       or NotSupportedException or PathTooLongException)
        {
            return "";
        }
    }

    private static int RoleIndex(string part)
    {
        for (int i = 0; i < RoleFolders.Length; i++)
            if (part.Equals(RoleFolders[i], StringComparison.OrdinalIgnoreCase)) return i;
        return -1;
    }

    private static AnimationKind KindOfRole(int role) => role switch
    {
        0 => AnimationKind.Behaviour,
        1 => AnimationKind.Animation,
        2 => AnimationKind.Character,
        3 => AnimationKind.Skeleton,
        _ => AnimationKind.Other,
    };
}
