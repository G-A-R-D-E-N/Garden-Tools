using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using OpenCommonwealth.Services;

namespace OpenCommonwealth.Services.Archive;

public enum GameAssetOrigin
{
    Loose,
    Archive,
}

public sealed record GameAsset(
    string VirtualPath,
    GameAssetOrigin Origin,
    string? SourceRoot,
    string? ArchivePath,
    string? EntryName,
    long Size,
    string? FullPath)
{
    public IReadOnlyList<GameAsset> Shadowed { get; init; } = Array.Empty<GameAsset>();

    public string Source { get; init; } = "";
}

public readonly record struct GameLooseFile(string VirtualPath, string FullPath, long Size, long ModifiedTicks);

public sealed partial class GameData
{
    public byte[]? ReadEntry(string archivePath, string entryName) =>
        TryReadEntry(archivePath, entryName, out byte[] bytes, out _) ? bytes : null;

    public bool TryReadEntry(string archivePath, string entryName, out byte[] bytes, out string? warning)
    {
        bytes = Array.Empty<byte>();
        warning = null;

        string key = Normalize(entryName);
        if (key.Length == 0) return false;
        if (!TryIndex(archivePath, out ArchiveIndex index, out warning)) return false;

        int at;
        if (!index.Exact.TryGetValue(ExactName(entryName), out at) &&
            !index.Normalized.TryGetValue(key, out at))
            return false;
        return TryReadAt(archivePath, at, out bytes, out warning);
    }

    internal long ReadLimitBytes { get; set; } = InputFilePolicy.MaximumHkxBytes;

    public byte[]? Read(GameAsset asset) =>
        TryRead(asset, out byte[] bytes, out _) ? bytes : null;

    public bool TryRead(GameAsset asset, out byte[] bytes, out string? warning)
    {
        ArgumentNullException.ThrowIfNull(asset);
        bytes = Array.Empty<byte>();
        warning = null;

        if (asset.Size >= 0)
        {
            warning = InputFilePolicy.RefusalIfTooLarge(asset.Size, ReadLimitBytes, asset.VirtualPath);
            if (warning != null) return false;
        }

        if (asset.Origin == GameAssetOrigin.Archive)
        {
            if (asset.ArchivePath == null || asset.EntryName == null)
            {
                warning = $"{asset.VirtualPath}: the entry names no archive";
                return false;
            }

            if (!TryReadEntry(asset.ArchivePath, asset.EntryName, out bytes, out warning)) return false;
            warning = InputFilePolicy.RefusalIfTooLarge(bytes.Length, ReadLimitBytes, asset.VirtualPath);
            if (warning == null) return true;

            bytes = Array.Empty<byte>();
            return false;
        }

        string? full = asset.FullPath;
        if (full == null)
        {
            warning = $"{asset.VirtualPath}: the entry has no loose file behind it";
            return false;
        }

        try
        {
            long length = new FileInfo(full).Length;
            warning = InputFilePolicy.RefusalIfTooLarge(length, ReadLimitBytes, asset.VirtualPath);
            if (warning != null) return false;

            bytes = File.ReadAllBytes(full);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            warning = $"{asset.VirtualPath}: {e.Message.Split('\n')[0].Trim()}";
            return false;
        }
    }

    internal static GameData FromParts(string dataFolder, IReadOnlyList<string> archivePaths,
                                       IReadOnlyList<string> modRootsLowToHigh)
    {
        var roots = new List<string>(modRootsLowToHigh.Count);
        var placed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string root in modRootsLowToHigh)
        {
            string full = Path.GetFullPath(root);
            if (placed.Add(full)) roots.Add(full);
        }

        var archives = new List<string>(archivePaths.Count);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string archive in archivePaths)
        {
            string full = Path.GetFullPath(archive);
            if (seen.Add(full)) archives.Add(full);
        }

        return new GameData(Path.GetFullPath(dataFolder), archives, null, roots);
    }

    public IReadOnlyList<GameLooseFile> EnumerateLoose(string root)
    {
        EnsureAlive();
        var files = new List<GameLooseFile>();
        if (!Directory.Exists(root)) return files;

        string fullRoot = Path.GetFullPath(root);
        foreach (string file in WalkLoose(root))
        {
            string relative = Path.GetRelativePath(fullRoot, file).Replace('\\', '/');
            FileInfo info;
            try { info = new FileInfo(file); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { continue; }
            long size = info.Exists ? info.Length : -1;
            long ticks = info.Exists ? info.LastWriteTimeUtc.Ticks : 0;
            files.Add(new GameLooseFile(relative, file, size, ticks));
        }

        files.Sort((left, right) =>
            string.Compare(left.VirtualPath, right.VirtualPath, StringComparison.OrdinalIgnoreCase));
        return files;
    }

    public IReadOnlyList<GameAsset> EnumerateWinners(string virtualPrefix, Func<string, bool>? nameFilter = null,
                                                     Action<string>? warn = null) =>
        EnumerateWinners(virtualPrefix, nameFilter,
                         warn == null ? null : (string archivePath, string message) => warn(ArchiveWarning(archivePath, message)));

    private const string LoosePrefix = "loose:";
    private const string ArchivePrefix = "archive:";

    public IReadOnlyList<GameAsset> EnumerateWinners(string virtualPrefix, Func<string, bool>? nameFilter,
                                                     Action<string, string>? warnPath)
    {
        EnsureAlive();
        string prefix = FlattenPrefix(virtualPrefix);
        var sources = new List<(string Source, IEnumerable<(string VirtualPath, long Size)> Entries)>(
            ModRoots.Count + 1 + ArchivePaths.Count);
        var warned = new HashSet<string>(StringComparer.Ordinal);

        for (int i = ModRoots.Count - 1; i >= 0; i--)
        {
            string root = ModRoots[i];
            sources.Add((LooseSource(root), LooseEntries(root, prefix, nameFilter)));
        }

        sources.Add((LooseSource(DataFolder), LooseEntries(DataFolder, prefix, nameFilter)));

        foreach (string archive in ArchivePaths)
        {
            if (!TryArchiveEntries(archive, out IReadOnlyList<Ba2.Entry> entries, out string? warning))
            {
                if (warning != null && warned.Add(warning))
                    warnPath?.Invoke(archive, ArchiveReason(ReasonAfter(archive, warning)));
                sources.Add((ArchiveSource(archive), new List<(string VirtualPath, long Size)>()));
                continue;
            }

            var found = new List<(string VirtualPath, long Size)>(entries.Count);
            foreach (Ba2.Entry entry in entries)
            {
                if (!UnderPrefix(entry.Name, prefix)) continue;
                if (nameFilter != null && !nameFilter(entry.Name)) continue;
                found.Add((entry.Name, entry.Unpacked));
            }
            sources.Add((ArchiveSource(archive), found));
        }

        return MergeWinners(sources);
    }

    public static string LooseSource(string root) => LoosePrefix + root;

    public static string ArchiveSource(string archive) => ArchivePrefix + archive;

    private static List<(string VirtualPath, long Size)> LooseEntries(string root, string prefix,
                                                                      Func<string, bool>? nameFilter)
    {
        var entries = new List<(string VirtualPath, long Size)>();
        foreach (string file in WalkLoose(root))
        {
            string relative = Path.GetRelativePath(root, file).Replace('\\', '/');
            if (!UnderPrefix(relative, prefix)) continue;
            if (nameFilter != null && !nameFilter(relative)) continue;
            entries.Add((relative, SizeOf(file)));
        }
        return entries;
    }

    public IReadOnlyList<string> WinnerSourceOrder()
    {
        var sources = new List<string>(ModRoots.Count + 1 + ArchivePaths.Count);
        for (int i = ModRoots.Count - 1; i >= 0; i--) sources.Add(LooseSource(ModRoots[i]));
        sources.Add(LooseSource(DataFolder));
        foreach (string archive in ArchivePaths) sources.Add(ArchiveSource(archive));
        return sources;
    }

    public IReadOnlyList<GameAsset> MergeWinners(
        IEnumerable<(string Source, IEnumerable<(string VirtualPath, long Size)> Entries)> orderedSources,
        CancellationToken token = default,
        Action<int>? progress = null)
    {
        EnsureAlive();
        var order = new List<Winner>();
        var byKey = new Dictionary<string, List<Winner>>(StringComparer.Ordinal);
        int offered = 0;

        foreach (var (source, entries) in orderedSources)
        {
            foreach (var (virtualPath, size) in entries)
            {
                if ((++offered & 0x3FF) == 0)
                {
                    token.ThrowIfCancellationRequested();
                    progress?.Invoke(offered);
                }
                Offer(AssetOf(source, virtualPath, size), source);
            }
        }

        progress?.Invoke(offered);
        var winners = new List<GameAsset>(order.Count);
        foreach (Winner winner in order)
            winners.Add(winner.Asset with { Shadowed = winner.Shadows.ToArray(), Source = winner.Source });
        return winners;

        void Offer(GameAsset candidate, string source)
        {
            string virtualPath = candidate.VirtualPath;
            string key = Normalize(virtualPath);
            if (key.Length == 0) return;

            if (!byKey.TryGetValue(key, out List<Winner>? group))
            {
                var winner = new Winner(candidate, source);
                byKey[key] = new List<Winner> { winner };
                order.Add(winner);
                return;
            }

            if (string.Equals(group[0].Source, source, StringComparison.OrdinalIgnoreCase))
            {
                var winner = new Winner(candidate, source);
                group.Add(winner);
                order.Add(winner);
                return;
            }

            foreach (Winner shadowed in group) shadowed.Shadows.Add(candidate);
        }
    }

    private static GameAsset AssetOf(string source, string virtualPath, long size)
    {
        if (source.StartsWith(ArchivePrefix, StringComparison.Ordinal))
            return new GameAsset(virtualPath, GameAssetOrigin.Archive, null,
                                 source[ArchivePrefix.Length..], virtualPath, size, null);

        string root = source.StartsWith(LoosePrefix, StringComparison.Ordinal)
            ? source[LoosePrefix.Length..]
            : "";
        string full = root.Length == 0
            ? virtualPath
            : Path.Combine(root, virtualPath.Replace('/', Path.DirectorySeparatorChar));
        return new GameAsset(virtualPath, GameAssetOrigin.Loose, root, null, null, size, full);
    }

    public static string? ModlistPath(string modsFolder, string? profile)
    {
        string mods = Path.GetFullPath(modsFolder);
        string candidate = profile != null
            ? Path.Combine(mods, "profiles", profile, "modlist.txt")
            : Path.Combine(mods, "modlist.txt");
        return File.Exists(candidate) ? candidate : null;
    }

    public static bool IsSkeletonName(string fileName)
    {
        string flat = fileName.Replace('\\', '/').Trim();
        if (!flat.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase)) return false;

        int slash = flat.LastIndexOf('/');
        string name = slash >= 0 ? flat[(slash + 1)..] : flat;
        return name.StartsWith("skeleton", StringComparison.OrdinalIgnoreCase);
    }

    public static bool IsCharacterAssetPath(string virtualPath)
    {
        string flat = virtualPath.Replace('\\', '/').Trim();
        string[] parts = flat.Split('/', StringSplitOptions.RemoveEmptyEntries);
        for (int i = 0; i < parts.Length - 1; i++)
            if (string.Equals(parts[i], "CharacterAssets", StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    public static bool IsSkeletonPath(string virtualPath)
    {
        string flat = virtualPath.Replace('\\', '/').Trim();
        if (!flat.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase)) return false;
        return IsSkeletonName(flat) || IsCharacterAssetPath(flat);
    }

    internal static bool IsInsideRoot(string root, string path) =>
        InsideRootFull(Path.GetFullPath(root), Path.GetFullPath(path));

    private static bool InsideRootFull(string fullRoot, string full)
    {
        if (fullRoot.Length > 0 && fullRoot[^1] == Path.DirectorySeparatorChar)
            fullRoot = fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

        if (string.Equals(full, fullRoot, StringComparison.OrdinalIgnoreCase)) return true;
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) return false;
        if (fullRoot.Length >= full.Length) return false;
        return full[fullRoot.Length] == Path.DirectorySeparatorChar ||
               full[fullRoot.Length] == Path.AltDirectorySeparatorChar;
    }

    internal static string ArchiveWarning(string archivePath, Exception error) =>
        ArchiveWarning(archivePath, error.Message);

    internal static string ArchiveWarning(string archivePath, string message) =>
        $"{archivePath}: {ArchiveReason(message)}";

    internal static string ArchiveReason(string message) =>
        message.Split('\n', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()?.Trim() ?? message.Trim();

    private static string ReasonAfter(string archivePath, string warning)
    {
        string prefix = archivePath + ": ";
        return warning.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? warning[prefix.Length..] : warning;
    }

    public string? DataRelativePath(string fullPath)
    {
        if (string.IsNullOrWhiteSpace(fullPath)) return null;
        try
        {
            if (!IsInsideRoot(DataFolder, fullPath)) return null;
            string relative = Path.GetRelativePath(DataFolder, Path.GetFullPath(fullPath)).Replace('\\', '/');
            return relative == "." ? null : relative;
        }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }
    }

    public byte[]? SkeletonBytes(string animationEntryName) =>
        SkeletonEntryName(animationEntryName) is { } entryName && TryReadSkeletonEntry(entryName, out byte[] bytes)
            ? bytes
            : null;

    public string? SkeletonEntryName(string animationEntryName)
    {
        string assets = SkeletonFolder(animationEntryName);
        if (assets.Length == 0) return null;

        foreach (string archive in ArchivePaths)
        {
            foreach (Ba2.Entry entry in Entries(archive))
            {
                if (!entry.Name.StartsWith(assets, StringComparison.OrdinalIgnoreCase)) continue;
                if (!IsSkeletonPath(entry.Name)) continue;
                return entry.Name;
            }
        }
        return null;
    }

    public bool TrySkeletonEntry(string animationEntryName, out string? entryName, out byte[] bytes)
    {
        entryName = SkeletonEntryName(animationEntryName);
        bytes = Array.Empty<byte>();
        return entryName != null && TryReadSkeletonEntry(entryName, out bytes);
    }

    public bool TryReadSkeletonEntry(string entryName, out byte[] bytes)
    {
        bytes = Array.Empty<byte>();
        if (string.IsNullOrWhiteSpace(entryName)) return false;

        foreach (string archive in ArchivePaths)
        {
            if (!TryReadEntry(archive, entryName, out byte[] read, out _)) continue;
            if (read.LongLength > ReadLimitBytes) return false;
            bytes = read;
            return true;
        }
        return false;
    }

    private static string SkeletonFolder(string animationEntryName)
    {
        if (string.IsNullOrWhiteSpace(animationEntryName)) return "";
        string flat = animationEntryName.Replace('\\', '/');
        int anims = flat.LastIndexOf("/Animations/", StringComparison.Ordinal);
        return anims < 0 ? "" : flat[..anims] + "/CharacterAssets/";
    }

    private static string FlattenPrefix(string virtualPrefix) =>
        (virtualPrefix ?? "").Replace('\\', '/').Trim().Trim('/').ToLowerInvariant();

    private static bool UnderPrefix(string virtualPath, string prefix)
    {
        if (prefix.Length == 0) return true;
        string flat = virtualPath.Replace('\\', '/').TrimStart('/').ToLowerInvariant();
        if (string.Equals(flat, prefix, StringComparison.Ordinal)) return true;
        return flat.StartsWith(prefix + "/", StringComparison.Ordinal);
    }

    private static long SizeOf(string path)
    {
        try { return new FileInfo(path).Length; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return -1; }
    }

    private static IEnumerable<string> WalkLoose(string root)
    {
        if (!Directory.Exists(root)) yield break;

        string fullRoot = Path.GetFullPath(root);
        var pending = new Stack<string>();
        pending.Push(fullRoot);
        while (pending.Count > 0)
        {
            string folder = pending.Pop();

            string[] files;
            try { files = Directory.GetFiles(folder); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { continue; }
            foreach (string file in files)
            {
                string full = Path.GetFullPath(file);
                if (!InsideRootFull(fullRoot, full)) continue;
                if (IsReparsePoint(full)) continue;
                yield return full;
            }

            string[] children;
            try { children = Directory.GetDirectories(folder); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { continue; }
            foreach (string child in children)
            {
                string full = Path.GetFullPath(child);
                if (!InsideRootFull(fullRoot, full)) continue;
                if (IsReparsePoint(full)) continue;
                pending.Push(full);
            }
        }
    }

    private static bool IsReparsePoint(string path)
    {
        try { return (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0; }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return true; }
    }

    private sealed class Winner
    {
        internal Winner(GameAsset asset, string source)
        {
            Asset = asset;
            Source = source;
        }

        internal GameAsset Asset { get; }
        internal string Source { get; }
        internal List<GameAsset> Shadows { get; } = new();
    }
}
