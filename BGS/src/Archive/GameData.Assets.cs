using System;
using System.Collections.Generic;
using System.IO;

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
}

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
        if (!TryArchiveEntries(archivePath, out IReadOnlyList<Ba2.Entry> entries, out warning)) return false;

        foreach (Ba2.Entry entry in entries)
        {
            if (!string.Equals(Normalize(entry.Name), key, StringComparison.Ordinal)) continue;
            try
            {
                bytes = Open(archivePath).Read(entry);
                return true;
            }
            catch (Exception e) when (IsSkippableArchiveFailure(e))
            {
                warning = ArchiveWarning(archivePath, e);
                return false;
            }
        }
        return false;
    }

    public byte[]? Read(GameAsset asset)
    {
        ArgumentNullException.ThrowIfNull(asset);

        if (asset.Origin == GameAssetOrigin.Archive)
            return asset.ArchivePath == null || asset.EntryName == null
                ? null
                : ReadEntry(asset.ArchivePath, asset.EntryName);

        string? full = asset.FullPath;
        if (full == null) return null;
        try { return File.ReadAllBytes(full); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
    }

    public IReadOnlyList<GameAsset> EnumerateWinners(string virtualPrefix, Func<string, bool>? nameFilter = null)
    {
        EnsureAlive();
        string prefix = FlattenPrefix(virtualPrefix);
        var order = new List<Winner>();
        var byKey = new Dictionary<string, Winner>(StringComparer.Ordinal);

        for (int i = ModRoots.Count - 1; i >= 0; i--)
        {
            string root = ModRoots[i];
            foreach (string file in WalkLoose(root)) OfferLoose(root, file);
        }

        foreach (string file in WalkLoose(DataFolder)) OfferLoose(DataFolder, file);

        foreach (string archive in ArchivePaths)
            foreach (Ba2.Entry entry in Entries(archive))
                Offer(new GameAsset(entry.Name, GameAssetOrigin.Archive, null, archive, entry.Name,
                                    entry.Unpacked, null),
                      "archive:" + archive);

        var winners = new List<GameAsset>(order.Count);
        foreach (Winner winner in order)
            winners.Add(winner.Asset with { Shadowed = winner.Shadows.ToArray() });
        return winners;

        void OfferLoose(string root, string file)
        {
            string full = Path.GetFullPath(file);
            if (!IsInsideRoot(root, full)) return;
            string relative = Path.GetRelativePath(root, full).Replace('\\', '/');
            if (relative.Length == 0) return;
            Offer(new GameAsset(relative, GameAssetOrigin.Loose, root, null, null, SizeOf(full), full),
                  "loose:" + root);
        }

        void Offer(GameAsset candidate, string source)
        {
            string virtualPath = candidate.VirtualPath;
            if (!UnderPrefix(virtualPath, prefix)) return;
            if (nameFilter != null && !nameFilter(virtualPath)) return;

            string key = Normalize(virtualPath);
            if (key.Length == 0) return;

            if (!byKey.TryGetValue(key, out Winner? winner))
            {
                winner = new Winner(candidate, source);
                byKey[key] = winner;
                order.Add(winner);
                return;
            }

            if (string.Equals(winner.Source, source, StringComparison.OrdinalIgnoreCase))
            {
                order.Add(new Winner(candidate, source));
                return;
            }

            winner.Shadows.Add(candidate);
        }
    }

    public static string? ModlistPath(string modsFolder, string? profile)
    {
        string mods = Path.GetFullPath(modsFolder);
        string candidate = profile != null
            ? Path.Combine(mods, "profiles", profile, "modlist.txt")
            : Path.Combine(mods, "modlist.txt");
        return File.Exists(candidate) ? candidate : null;
    }

    public static bool IsSkeletonPath(string virtualPath)
    {
        string flat = virtualPath.Replace('\\', '/').Trim();
        if (!flat.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase)) return false;

        int slash = flat.LastIndexOf('/');
        string name = slash >= 0 ? flat[(slash + 1)..] : flat;
        if (name.StartsWith("skeleton", StringComparison.OrdinalIgnoreCase)) return true;

        string[] parts = flat.Split('/', StringSplitOptions.RemoveEmptyEntries);
        for (int i = 0; i < parts.Length - 1; i++)
            if (string.Equals(parts[i], "CharacterAssets", StringComparison.OrdinalIgnoreCase)) return true;
        return false;
    }

    internal static bool IsInsideRoot(string root, string path)
    {
        string fullRoot = Path.GetFullPath(root);
        string full = Path.GetFullPath(path);
        if (fullRoot.Length > 0 && fullRoot[^1] == Path.DirectorySeparatorChar)
            fullRoot = fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

        if (string.Equals(full, fullRoot, StringComparison.OrdinalIgnoreCase)) return true;
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) return false;
        if (fullRoot.Length >= full.Length) return false;
        return full[fullRoot.Length] == Path.DirectorySeparatorChar ||
               full[fullRoot.Length] == Path.AltDirectorySeparatorChar;
    }

    internal static string ArchiveWarning(string archivePath, Exception error) =>
        $"{Path.GetFileName(archivePath)}: {error.Message.Split('\n')[0].Trim()}";

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

        var pending = new Stack<string>();
        pending.Push(Path.GetFullPath(root));
        while (pending.Count > 0)
        {
            string folder = pending.Pop();

            string[] files;
            try { files = Directory.GetFiles(folder); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { continue; }
            foreach (string file in files)
            {
                if (!IsInsideRoot(root, file)) continue;
                if (IsReparsePoint(file)) continue;
                yield return file;
            }

            string[] children;
            try { children = Directory.GetDirectories(folder); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { continue; }
            foreach (string child in children)
            {
                if (!IsInsideRoot(root, child)) continue;
                if (IsReparsePoint(child)) continue;
                pending.Push(child);
            }
        }
    }

    private static bool IsReparsePoint(string path)
    {
        try
        {
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return true;
        }

        try
        {
            FileSystemInfo info = Directory.Exists(path) ? new DirectoryInfo(path) : new FileInfo(path);
            return info.LinkTarget != null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            return true;
        }
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
