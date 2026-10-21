using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace OpenCommonwealth.Services.Archive;

public sealed partial class GameData
{
    public sealed record AssetCopy(string Source, string Location);

    public sealed record AssetWinner(string Path, string Source, string Location, string? ArchivePath,
                                     IReadOnlyList<AssetCopy> Shadowed);

    public IEnumerable<AssetWinner> EnumerateWinners(string prefix, params string[] extensions)
    {
        string want = FlatName(prefix).ToLowerInvariant();
        if (want.Length > 0 && !want.EndsWith('/')) want += "/";
        var wantExtensions = extensions.Where(ext => ext.Length > 0).Select(ext => ext.StartsWith('.') ? ext : "." + ext).ToList();
        var winners = new Dictionary<string, (string Path, string Source, string Location, string? ArchivePath, List<AssetCopy> Shadowed)>(
            StringComparer.Ordinal);

        void Offer(string path, string source, string location, string? archivePath)
        {
            string flat = FlatName(path);
            string key = flat.ToLowerInvariant();
            if (!key.StartsWith(want, StringComparison.Ordinal)) return;
            if (wantExtensions.Count > 0 && !wantExtensions.Any(ext => key.EndsWith(ext, StringComparison.OrdinalIgnoreCase)))
                return;
            var copy = new AssetCopy(source, location);
            if (winners.TryGetValue(key, out var known)) known.Shadowed.Add(copy);
            else winners[key] = (flat, source, location, archivePath, new List<AssetCopy>());
        }

        foreach (string root in ModRoots.Reverse())
        {
            string source = "loose:" + Path.GetFileName(root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            foreach (string file in LooseFiles(root))
                Offer(Path.GetRelativePath(root, file), source, root, null);
        }
        foreach (string file in LooseFiles(DataFolder))
            Offer(Path.GetRelativePath(DataFolder, file), "loose", DataFolder, null);
        foreach (string archive in ArchivePaths)
            foreach (var entry in Entries(archive))
                Offer(entry.Name, Path.GetFileName(archive), archive, archive);

        return winners.Values
            .OrderBy(w => w.Path, StringComparer.OrdinalIgnoreCase)
            .Select(w => new AssetWinner(w.Path, w.Source, w.Location, w.ArchivePath, w.Shadowed));
    }

    private static List<string> LooseFiles(string root)
    {
        try { return Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).ToList(); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return new List<string>(); }
    }

    private static string FlatName(string name)
    {
        string flat = name.Replace('\\', '/').TrimStart('/');
        if (flat == ".") return "";
        if (flat.StartsWith("./", StringComparison.Ordinal)) flat = flat[2..].TrimStart('/');
        return flat;
    }

    public byte[]? TryReadEntry(string archivePath, string entryName)
    {
        string want = FlatName(entryName);
        var entry = Entries(archivePath).FirstOrDefault(e => string.Equals(FlatName(e.Name), want, StringComparison.OrdinalIgnoreCase));
        if (entry == null) return null;
        try { return Open(archivePath).Read(entry); }
        catch (Exception e) when (IsSkippableArchiveFailure(e)) { return null; }
    }

    public byte[] ReadEntry(string archivePath, string entryName) =>
        TryReadEntry(archivePath, entryName) ??
        throw new FileNotFoundException($"{entryName} is missing or unreadable in {Path.GetFileName(archivePath)}");
}
