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
        string? modlist = ModlistOf(modsFolder, profile);
        if (modlist == null) return ForDataFolder(dataFolder);

        using var data = GameData.DiscoverModded(dataFolder, modsFolder, profile);
        return ForGameData(data, modlist, profile);
    }

    public static string? ModlistOf(string modsFolder, string? profile)
    {
        if (profile != null)
        {
            string profiled = Path.Combine(modsFolder, "profiles", profile, "modlist.txt");
            return File.Exists(profiled) ? profiled : null;
        }

        string root = Path.Combine(modsFolder, "modlist.txt");
        return File.Exists(root) ? root : null;
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
                           List<AnimationIndexEntry> entries, int reused, int scanned,
                           TimeSpan elapsed, string? note)
    {
        Request = request;
        Sources = sources;
        _entries = entries;
        ReusedSources = reused;
        ScannedSources = scanned;
        Elapsed = elapsed;
        CacheNote = note;
    }

    public static string DefaultCachePath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "BehaviourGraphStudio", "cache", "animation-index.json");

    public AnimationIndexRequest Request { get; }

    public IReadOnlyList<AnimationIndexSource> Sources { get; }

    public IReadOnlyList<AnimationIndexEntry> Entries => _entries;

    public int ReusedSources { get; }

    public int ScannedSources { get; }

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
        string? note = null;
        if (options.ReuseCache && cache != null && stored == null && File.Exists(cache.Path))
            note = "the previous index cache was stale or unreadable and has been discarded";

        var reusable = new Dictionary<string, AnimationIndexCacheSource>(StringComparer.OrdinalIgnoreCase);
        if (stored != null && AnimationIndexCache.MatchesKey(stored, request))
            foreach (var source in stored.Sources)
                reusable[Identity(source.Kind, source.Path)] = source;

        var sources = new List<AnimationIndexSource>(planned.Count);
        var perSource = new Dictionary<string, List<AnimationIndexEntry>>(StringComparer.OrdinalIgnoreCase);
        int done = 0, reused = 0, scanned = 0;

        progress?.Invoke(new AnimationIndexProgress(0, planned.Count, "reading sources"));
        foreach (var (kind, path, label) in planned)
        {
            token.ThrowIfCancellationRequested();
            done++;

            bool loose = kind != ArchiveKind;
            var files = loose ? LooseFiles(path, token) : new List<(string Rel, string Full, long Size, long Ticks)>();
            string stamp = loose ? LooseStamp(files) : FileStamp(path);
            string identity = Identity(kind, path);

            List<AnimationIndexEntry> entries;
            if (reusable.TryGetValue(identity, out var known) && known.Stamp == stamp)
            {
                entries = FromCache(known, path, label);
                reused++;
                progress?.Invoke(new AnimationIndexProgress(done, planned.Count, "reused " + label));
            }
            else
            {
                entries = loose
                    ? files.Select(file => Classify(file.Rel, label, file.Size, file.Full)).ToList()
                    : ArchiveEntries(path, label, token);
                scanned++;
                progress?.Invoke(new AnimationIndexProgress(done, planned.Count, "indexed " + label));
            }

            sources.Add(new AnimationIndexSource(kind, path, label, stamp));
            perSource[identity] = entries;
        }

        progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "merging"));
        var winners = new Dictionary<string, AnimationIndexEntry>(StringComparer.Ordinal);
        foreach (var source in sources)
        {
            string identity = Identity(source.Kind, source.Path);
            foreach (var entry in perSource[identity]) winners.TryAdd(GameData.Normalize(entry.VirtualPath), entry);
        }

        var merged = winners.Values
            .OrderBy(entry => entry.VirtualPath, StringComparer.OrdinalIgnoreCase)
            .ThenBy(entry => entry.Source, StringComparer.Ordinal)
            .ToList();

        if (cache != null)
        {
            progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "writing cache"));
            string? modlistStamp = FileStamp(request.ModlistPath);
            if (stored != null && (stored.ModlistStamp ?? "") != (modlistStamp ?? "") && note == null)
                note = "the modlist changed since the previous index build";

            var data = new AnimationIndexCacheData(
                AnimationIndexCache.CurrentVersion, request.DataFolder, request.Profile, request.ModlistPath,
                modlistStamp, sources.Select(source => CacheOf(source, perSource)).ToList());
            if (!cache.Save(data, out string failure)) note = "the index cache could not be written: " + failure;
        }

        watch.Stop();
        progress?.Invoke(new AnimationIndexProgress(planned.Count, planned.Count, "done"));
        return new AnimationIndex(request, sources, merged, reused, scanned, watch.Elapsed, note);
    }

    public static Task<AnimationIndex> BuildAsync(AnimationIndexRequest request, AnimationIndexOptions options,
                                                  Action<AnimationIndexProgress>? progress = null,
                                                  CancellationToken token = default) =>
        Task.Run(() => Build(request, options, progress, token), token);

    internal static string Identity(string kind, string path) => kind + "\u0000" + path;

    private static AnimationIndexCacheSource CacheOf(AnimationIndexSource source,
                                                     Dictionary<string, List<AnimationIndexEntry>> perSource)
    {
        var entries = perSource[Identity(source.Kind, source.Path)];
        return new AnimationIndexCacheSource(source.Kind, source.Path, source.Source, source.Stamp,
            entries.Select(entry => entry.VirtualPath).ToList(),
            entries.Select(entry => entry.Size).ToList());
    }

    private static List<AnimationIndexEntry> FromCache(AnimationIndexCacheSource source, string root, string label)
    {
        var entries = new List<AnimationIndexEntry>(source.Paths.Count);
        for (int i = 0; i < source.Paths.Count; i++)
        {
            long size = i < source.Sizes.Count ? source.Sizes[i] : 0;
            entries.Add(Classify(source.Paths[i], label, size, LocationOf(source.Kind, root, source.Paths[i])));
        }
        return entries;
    }

    private static string LocationOf(string kind, string root, string virtualPath) =>
        kind == ArchiveKind ? root : Path.Combine(root, virtualPath.Replace('/', Path.DirectorySeparatorChar));

    private static List<AnimationIndexEntry> ArchiveEntries(string path, string label, CancellationToken token)
    {
        var entries = new List<AnimationIndexEntry>();
        Ba2 archive;
        try
        {
            archive = Ba2.Open(path);
        }
        catch (Exception e) when (GameData.IsSkippableArchiveFailure(e))
        {
            return entries;
        }

        using (archive)
        {
            int seen = 0;
            foreach (var entry in archive.Entries)
            {
                if ((++seen & 0x3FF) == 0) token.ThrowIfCancellationRequested();
                if (!IsIndexedName(entry.Name)) continue;
                entries.Add(Classify(entry.Name, label, entry.Unpacked, path));
            }
        }
        return entries;
    }

    private static List<(string Rel, string Full, long Size, long Ticks)> LooseFiles(string root,
                                                                                    CancellationToken token)
    {
        var found = new List<(string, string, long, long)>();
        if (!Directory.Exists(root)) return found;

        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.Count > 0)
        {
            token.ThrowIfCancellationRequested();
            string dir = pending.Pop();

            string[] files;
            try
            {
                files = Directory.GetFiles(dir);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                continue;
            }

            foreach (string file in files)
            {
                if (!IsIndexedName(file)) continue;
                try
                {
                    var info = new FileInfo(file);
                    found.Add((Path.GetRelativePath(root, file).Replace('\\', '/'), file, info.Length,
                               info.LastWriteTimeUtc.Ticks));
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                {
                }
            }

            string[] children;
            try
            {
                children = Directory.GetDirectories(dir);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                continue;
            }

            for (int i = children.Length - 1; i >= 0; i--) pending.Push(children[i]);
        }

        found.Sort((left, right) => string.Compare(left.Item1, right.Item1, StringComparison.OrdinalIgnoreCase));
        return found;
    }

    private static string LooseStamp(List<(string Rel, string Full, long Size, long Ticks)> files)
    {
        ulong hash = 14695981039346656037UL;
        long bytes = 0;
        long newest = 0;
        foreach (var file in files)
        {
            bytes += file.Size;
            if (file.Ticks > newest) newest = file.Ticks;
            hash = Mix(hash, file.Rel);
            hash = Mix(hash, file.Size.ToString(CultureInfo.InvariantCulture));
            hash = Mix(hash, file.Ticks.ToString(CultureInfo.InvariantCulture));
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
