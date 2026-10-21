using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OpenCommonwealth.Services.Archive;

public sealed partial class GameData
{
    public sealed record AssetWinner(string Path, string Source, string? ArchivePath, IReadOnlyList<string> Shadowed);

    public IEnumerable<AssetWinner> EnumerateWinners(string prefix, params string[] extensions)
    {
        string want = prefix.Replace('\\', '/').TrimStart('/').ToLowerInvariant();
        var winners = new Dictionary<string, (string Path, string Source, string? ArchivePath, List<string> Shadowed)>(
            StringComparer.Ordinal);

        void Offer(string path, string source, string? archivePath)
        {
            string flat = path.Replace('\\', '/');
            string key = flat.ToLowerInvariant();
            if (!key.StartsWith(want, StringComparison.Ordinal)) return;
            if (extensions.Length > 0 && !extensions.Any(ext => key.EndsWith(ext, StringComparison.OrdinalIgnoreCase)))
                return;
            if (winners.TryGetValue(key, out var known)) known.Shadowed.Add(source);
            else winners[key] = (flat, source, archivePath, new List<string>());
        }

        foreach (string root in ModRoots.Reverse())
        {
            if (!Directory.Exists(Path.Combine(root, "Meshes"))) continue;
            foreach (string file in LooseFiles(root))
                Offer(Path.GetRelativePath(root, file), "loose", null);
        }
        foreach (string file in LooseFiles(DataFolder))
            Offer(Path.GetRelativePath(DataFolder, file), "loose", null);
        foreach (string archive in ArchivePaths)
            foreach (var entry in Entries(archive))
                Offer(entry.Name, Path.GetFileName(archive), archive);

        return winners.Values
            .OrderBy(w => w.Path, StringComparer.OrdinalIgnoreCase)
            .Select(w => new AssetWinner(w.Path, w.Source, w.ArchivePath, w.Shadowed));
    }

    private static List<string> LooseFiles(string root)
    {
        try { return Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).ToList(); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return new List<string>(); }
    }

    public byte[]? TryReadEntry(string archivePath, string entryName)
    {
        string want = entryName.Replace('\\', '/');
        var entry = Entries(archivePath).FirstOrDefault(e => string.Equals(e.Name, want, StringComparison.OrdinalIgnoreCase));
        if (entry == null) return null;
        try { return Open(archivePath).Read(entry); }
        catch (Exception e) when (IsSkippableArchiveFailure(e)) { return null; }
    }

    public byte[] ReadEntry(string archivePath, string entryName) =>
        TryReadEntry(archivePath, entryName) ??
        throw new FileNotFoundException($"{entryName} is missing or unreadable in {Path.GetFileName(archivePath)}");
}
