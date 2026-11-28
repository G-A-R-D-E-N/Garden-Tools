using System;
using System.IO;
using System.Linq;

namespace OpenCommonwealth.Services.Hkx;

public enum ReplacePolicy
{
    Overwrite,
    CreateOnly,
}

public static class FileSafety
{
    private static readonly TimeSpan InterruptedArtifactAge = TimeSpan.FromMinutes(10);
    private const int TokenLength = 64;
    private const int IdLength = 32;

    public static void Backup(string path, int keep = 3)
    {
        if (!File.Exists(path)) return;
        RotateBackups(path, keep);
        File.Copy(path, path + ".bak");
    }

    public static void Replace(string path, byte[] bytes, ReplacePolicy policy = ReplacePolicy.Overwrite)
    {
        string staging = path + "." + Guid.NewGuid().ToString("N") + ".writing";
        try
        {
            File.WriteAllBytes(staging, bytes);
            try
            {
                File.Move(staging, path, overwrite: policy == ReplacePolicy.Overwrite);
            }
            catch (IOException) when (policy == ReplacePolicy.CreateOnly && File.Exists(path))
            {
                throw new IOException($"{path} already exists, so nothing was replaced.");
            }
        }
        catch
        {
            TryDelete(staging);
            throw;
        }
    }

    internal static string? ReplaceChecked(
        string path,
        byte[] bytes,
        DocumentSourceStamp expected,
        int keep = 3,
        Action? beforeReplace = null,
        Action? afterReplace = null,
        Action? beforeRestore = null)
    {
        RecoverInterrupted(path, keep);

        string id = Guid.NewGuid().ToString("N");
        string staging = path + "." + id + ".writing";
        string saving = path + "." + id + ".saving";
        var replacement = DocumentSourceStamp.Capture(bytes);
        string previous = path + "." + replacement.Token + "." + id + ".previous";
        string displaced = path + "." + id + ".displaced";

        try
        {
            File.WriteAllBytes(staging, bytes);
            if (!expected.Matches(path, out string changed))
                throw new IOException(changed);

            beforeReplace?.Invoke();
            File.WriteAllBytes(saving, Array.Empty<byte>());
            File.Replace(staging, path, previous);
            afterReplace?.Invoke();
            TryTouch(previous);
            TryTouch(saving);

            bool previousWasExpected = expected.Matches(previous, out _);
            bool currentIsReplacement = replacement.Matches(path, out _);

            if (!previousWasExpected)
            {
                if (currentIsReplacement)
                {
                    beforeRestore?.Invoke();
                    try
                    {
                        RestoreDisplaced(path, previous, replacement, displaced);
                    }
                    catch (Exception restoreError)
                        when (restoreError is IOException or UnauthorizedAccessException)
                    {
                        string kept = TryPromotePrevious(path, previous, keep) ?? previous;
                        string where = "the displaced version was kept at " + Path.GetFileName(kept);
                        if (File.Exists(displaced))
                            where += ", and the file found at the path was kept at " + Path.GetFileName(displaced);
                        throw new IOException(
                            DocumentSourceStamp.ChangedFailure +
                            ". Automatic restoration was refused, so " + where + ": " + restoreError.Message,
                            restoreError);
                    }
                }
                else
                {
                    PromotePrevious(path, previous, keep);
                }

                throw new IOException(DocumentSourceStamp.ChangedFailure);
            }

            if (!currentIsReplacement)
            {
                PromotePrevious(path, previous, keep);
                throw new IOException(
                    "the file changed on disk while the save was being committed; the pre-save version " +
                    "was kept as a backup");
            }

            return TryPromotePrevious(path, previous, keep);
        }
        catch
        {
            TryDelete(staging);
            throw;
        }
        finally
        {
            TryDelete(saving);
        }
    }

    internal static string[] RecoverInterrupted(string path, int keep = 3)
    {
        string full;
        try { full = Path.GetFullPath(path); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return Array.Empty<string>();
        }

        string? directory = Path.GetDirectoryName(full);
        if (directory == null || !Directory.Exists(directory)) return Array.Empty<string>();

        string name = Path.GetFileName(full);
        RecoverStale(full, directory, name, keep);
        return Artifacts(directory, name + ".*.previous")
            .Where(file => IsGeneratedArtifact(name, file, ".previous", TokenLength, IdLength))
            .ToArray();
    }

    internal static string KeptNote(string[] kept) =>
        kept.Length == 0
            ? ""
            : " Displaced copies from an earlier interrupted save were kept, not deleted: " +
              string.Join(", ", kept) + ".";

    private static void RecoverStale(string full, string directory, string name, int keep)
    {
        DateTime cutoff = DateTime.UtcNow - InterruptedArtifactAge;

        foreach (string writing in Artifacts(directory, name + ".*.writing"))
            if (IsGeneratedArtifact(name, writing, ".writing", IdLength) && IsStale(writing, cutoff))
                TryDelete(writing);

        foreach (string saving in Artifacts(directory, name + ".*.saving"))
            if (IsGeneratedArtifact(name, saving, ".saving", IdLength) && IsStale(saving, cutoff))
                TryDelete(saving);

        var previous = Artifacts(directory, name + ".*.previous")
            .Where(file => IsGeneratedArtifact(name, file, ".previous", TokenLength, IdLength))
            .Select(file => (File: file, Written: LastWriteUtc(file)))
            .Where(item => item.Written is DateTime written && written <= cutoff)
            .Where(item => !HasFreshSavingMarker(full, item.File, cutoff))
            .OrderByDescending(item => item.Written)
            .Select(item => item.File)
            .ToList();
        if (previous.Count == 0) return;

        if (!File.Exists(full))
        {
            previous.Any(file => TryMove(file, full));
            return;
        }

        string currentToken;
        try { currentToken = DocumentSourceStamp.Capture(full).Token; }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return;
        }

        string marker = "." + currentToken + ".";
        string? newestMatching = previous.FirstOrDefault(file =>
            Path.GetFileName(file).Contains(marker, StringComparison.OrdinalIgnoreCase));

        if (newestMatching != null) TryPromoteRecovered(full, newestMatching, keep);
    }

    private static void RestoreDisplaced(string path, string previous, DocumentSourceStamp replacement, string aside)
    {
        File.Move(path, aside);
        if (!replacement.Matches(aside, out _) || !File.Exists(previous))
        {
            File.Move(aside, path);
            throw new IOException("the file changed again before the displaced version could be restored");
        }

        File.Move(previous, path);
        TryDelete(aside);
    }

    private static bool IsGeneratedArtifact(string name, string candidate, string suffix,
                                            params int[] segmentLengths)
    {
        string fileName = Path.GetFileName(candidate);
        if (!fileName.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) return false;

        string stem = fileName[..^suffix.Length];
        string prefix = name + ".";
        if (!stem.StartsWith(prefix, StringComparison.Ordinal)) return false;

        string[] segments = stem[prefix.Length..].Split('.');
        if (segments.Length != segmentLengths.Length) return false;

        for (int i = 0; i < segments.Length; i++)
            if (segments[i].Length != segmentLengths[i] || !segments[i].All(Uri.IsHexDigit))
                return false;
        return true;
    }

    private static bool HasFreshSavingMarker(string path, string previous, DateTime cutoff)
    {
        string fileName = Path.GetFileName(previous);
        const string suffix = ".previous";
        if (!fileName.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) return false;

        string stem = fileName[..^suffix.Length];
        int dot = stem.LastIndexOf('.');
        if (dot < 0 || dot == stem.Length - 1) return false;

        string id = stem[(dot + 1)..];
        string saving = path + "." + id + ".saving";
        DateTime? written = LastWriteUtc(saving);
        return written is DateTime at && at > cutoff;
    }

    private static bool TryPromoteRecovered(string path, string previous, int keep)
    {
        try
        {
            RotateBackups(path, keep);
            File.Move(previous, path + ".bak");
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static string[] Artifacts(string directory, string pattern)
    {
        try { return Directory.GetFiles(directory, pattern); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return Array.Empty<string>();
        }
    }

    private static DateTime? LastWriteUtc(string path)
    {
        try { return File.Exists(path) ? File.GetLastWriteTimeUtc(path) : null; }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static bool IsStale(string path, DateTime cutoff) =>
        LastWriteUtc(path) is DateTime written && written <= cutoff;

    private static bool TryMove(string source, string destination)
    {
        try
        {
            File.Move(source, destination);
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    private static void TryTouch(string path)
    {
        try { if (File.Exists(path)) File.SetLastWriteTimeUtc(path, DateTime.UtcNow); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
    }

    private static string? TryPromotePrevious(string path, string previous, int keep)
    {
        try { return PromotePrevious(path, previous, keep); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return File.Exists(previous) ? previous : null;
        }
    }

    private static string? PromotePrevious(string path, string previous, int keep)
    {
        if (!File.Exists(previous)) return null;
        RotateBackups(path, keep);
        File.Move(previous, path + ".bak");
        return path + ".bak";
    }

    private static void RotateBackups(string path, int keep)
    {
        if (keep <= 1)
        {
            if (File.Exists(path + ".bak")) File.Delete(path + ".bak");
            return;
        }

        string oldest = path + ".bak." + (keep - 1);
        if (File.Exists(oldest)) File.Delete(oldest);
        for (int i = keep - 1; i >= 2; i--)
        {
            string older = path + ".bak." + (i - 1);
            if (File.Exists(older)) File.Move(older, path + ".bak." + i, overwrite: true);
        }
        if (File.Exists(path + ".bak")) File.Move(path + ".bak", path + ".bak.1", overwrite: true);
    }
}
