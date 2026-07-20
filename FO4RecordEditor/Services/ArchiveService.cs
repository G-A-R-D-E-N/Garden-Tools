using System.IO;
using System.Linq;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;

namespace FO4RecordEditor.Services;

/// <summary>
/// Read-only BA2/BSA archive access via Mutagen's own <see cref="Archive"/> reader (already vendored,
/// already used by TextureService's texture-index lookups) exposed as general-purpose MCP tools so
/// the AI can inspect and pull files out of a mod's packed archive without Archive2.exe.
/// </summary>
public static class ArchiveService
{
    private static IArchiveReader? TryOpen(string archivePath, out string error)
    {
        error = "";
        if (string.IsNullOrWhiteSpace(archivePath)) { error = "Provide an archive path (.ba2 or .bsa)."; return null; }
        if (!File.Exists(archivePath)) { error = ToolError.Fail($"Archive not found: '{archivePath}'."); return null; }
        try { return Archive.CreateReader(GameRelease.Fallout4, new Noggog.FilePath(archivePath)); }
        catch (Exception ex) { error = ToolError.Fail($"Could not read '{archivePath}': {ex.Message}"); return null; }
    }

    public static string ListArchive(string archivePath, string? filter, int limit)
    {
        var reader = TryOpen(archivePath, out var err);
        if (reader == null) return err;
        if (limit <= 0) limit = 500;

        var files = reader.Files.AsEnumerable();
        if (!string.IsNullOrWhiteSpace(filter))
            files = files.Where(f => (f.Path ?? "").Contains(filter, StringComparison.OrdinalIgnoreCase));

        var all = files.ToList();
        if (all.Count == 0)
            return $"No entries in '{Path.GetFileName(archivePath)}'" + (string.IsNullOrWhiteSpace(filter) ? "." : $" matching '{filter}'.");

        var shown = all.Take(limit).ToList();
        var header = $"{Path.GetFileName(archivePath)}: {all.Count} entr{(all.Count == 1 ? "y" : "ies")}" +
                     (string.IsNullOrWhiteSpace(filter) ? "" : $" matching '{filter}'") +
                     (all.Count > shown.Count ? $" (showing first {shown.Count})" : "") + ":";
        return header + "\n" + string.Join("\n", shown.Select(f => $"{f.Path}  ({f.Size:N0} bytes)"));
    }

    public static string ExtractFile(string archivePath, string innerPath, string outPath)
    {
        var reader = TryOpen(archivePath, out var err);
        if (reader == null) return err;
        if (string.IsNullOrWhiteSpace(innerPath))
            return ToolError.Fail("Provide the file's path INSIDE the archive (as shown by archive_list), e.g. 'Meshes\\mymod\\thing.nif'.");
        if (string.IsNullOrWhiteSpace(outPath))
            return ToolError.Fail("Provide an output path to write the extracted file to.");

        var norm = innerPath.Replace('/', '\\').TrimStart('\\');
        var entry = reader.Files.FirstOrDefault(f =>
            string.Equals((f.Path ?? "").Replace('/', '\\'), norm, StringComparison.OrdinalIgnoreCase));
        if (entry == null)
            return ToolError.Fail($"'{innerPath}' is not in '{Path.GetFileName(archivePath)}'. Call archive_list first to see exact entry paths.");

        try
        {
            var fullOut = Path.GetFullPath(outPath);
            Directory.CreateDirectory(Path.GetDirectoryName(fullOut)!);
            File.WriteAllBytes(fullOut, entry.GetBytes());
        }
        catch (Exception ex) { return ToolError.Fail($"Extract failed: {ex.Message}"); }

        return $"Extracted '{innerPath}' ({entry.Size:N0} bytes) from '{Path.GetFileName(archivePath)}' to '{outPath}'.";
    }

    public static string ExtractAll(string archivePath, string outDir, string? filter, int limit)
    {
        var reader = TryOpen(archivePath, out var err);
        if (reader == null) return err;
        if (string.IsNullOrWhiteSpace(outDir)) return ToolError.Fail("Provide an output directory.");
        if (limit <= 0) limit = 2000;

        var files = reader.Files.AsEnumerable();
        if (!string.IsNullOrWhiteSpace(filter))
            files = files.Where(f => (f.Path ?? "").Contains(filter, StringComparison.OrdinalIgnoreCase));
        var all = files.ToList();
        if (all.Count == 0)
            return $"No entries" + (string.IsNullOrWhiteSpace(filter) ? "" : $" matching '{filter}'") + $" in '{Path.GetFileName(archivePath)}'.";

        // No silent truncation: a caller who didn't mean to unpack thousands of files gets a chance
        // to narrow with 'filter' first, instead of quietly writing a partial extraction to disk.
        if (all.Count > limit)
            return ToolError.Fail($"'{Path.GetFileName(archivePath)}' has {all.Count} matching entries, over the {limit} limit. " +
                "Narrow with 'filter' (a path substring, e.g. 'Meshes\\mymod\\') or raise 'limit'.");

        int written = 0;
        var failures = new List<string>();
        foreach (var f in all)
        {
            try
            {
                var rel = (f.Path ?? "").Replace('/', '\\').TrimStart('\\');
                var dest = Path.Combine(outDir, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                File.WriteAllBytes(dest, f.GetBytes());
                written++;
            }
            catch (Exception ex) { failures.Add($"{f.Path}: {ex.Message}"); }
        }

        var msg = $"Extracted {written} of {all.Count} file(s) from '{Path.GetFileName(archivePath)}' to '{outDir}'.";
        if (failures.Count > 0) msg += $" {failures.Count} FAILED: " + string.Join("; ", failures.Take(10));
        return msg;
    }
}
