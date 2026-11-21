using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using OpenCommonwealth.Services.Archive;

namespace BehaviourStudio.App;

public static class IndexScan
{
    public static int Run(string[] args)
    {
        bool json = args.Contains("--json");
        string? root = After(args, "--scan-index");
        if (root == null || !Directory.Exists(root))
            return Fail(json, root ?? "", "usage",
                        "--scan-index needs a game Data folder or a Mod Organizer 2 instance folder");

        bool noCache = args.Contains("--no-cache");
        bool fresh = args.Contains("--rebuild");

        AnimationIndexRequest request;
        try
        {
            request = AnimationIndexRequest.FromLoadOrder(root, null);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException
                                       or NotSupportedException or InvalidDataException)
        {
            return Fail(json, root, "config", First(e.Message));
        }

        string cachePath = After(args, "--cache") ?? (noCache ? "" : AnimationIndex.DefaultCachePathFor(request));
        AnimationIndexOptions options = cachePath.Length == 0
            ? AnimationIndexOptions.None
            : fresh ? AnimationIndexOptions.Fresh(cachePath) : AnimationIndexOptions.Cached(cachePath);

        Info(json, $"indexing {request.DataFolder}");
        if (request.ModRootsHighToLow.Count > 0)
            Info(json, $"  {request.ModRootsHighToLow.Count} mod root(s)" +
                       (request.Profile != null ? $", profile {request.Profile}" : ""));
        Info(json, $"  {request.ArchivePaths.Count} archive(s)");
        if (cachePath.Length > 0) Info(json, $"  cache {cachePath}" + (fresh ? " (rebuilt)" : ""));

        AnimationIndex index;
        try
        {
            index = AnimationIndex.Build(request, options,
                progress => Progress($"  {progress.Done}/{progress.Total} {progress.Phase}"), CancellationToken.None);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException
                                       or ArgumentException)
        {
            return Fail(json, root, "index", First(e.Message));
        }

        var summary = new Dictionary<string, int>();
        foreach (AnimationKind kind in Enum.GetValues<AnimationKind>())
            summary[AnimationIndex.Label(kind)] = index.Count(kind);
        summary["entries"] = index.Entries.Count;
        summary["sources"] = index.Sources.Count;
        summary["reusedSources"] = index.ReusedSources;
        summary["scannedSources"] = index.ScannedSources;
        summary["cacheHit"] = index.ScannedSources == 0 && index.Sources.Count > 0 ? 1 : 0;
        summary["elapsedMs"] = (int)index.Elapsed.TotalMilliseconds;

        int exit = index.Entries.Count > 0 ? 0 : 1;
        if (json)
        {
            var context = new ScanContext(root, request.Profile, request.DataFolder,
                                          request.ModRootsHighToLow.Count);
            Console.WriteLine(new ScanReport("scan-index", context, summary,
                                             Array.Empty<ScanFinding>(), exit).ToJson());
            return exit;
        }

        foreach (AnimationKind kind in Enum.GetValues<AnimationKind>())
            Console.WriteLine($"  {AnimationIndex.Label(kind),-10} {index.Count(kind)}");
        Console.WriteLine($"done  {index.Entries.Count} entries from {index.Sources.Count} source(s) " +
                          $"({index.ReusedSources} reused, {index.ScannedSources} scanned) in " +
                          $"{index.Elapsed.TotalSeconds:F2}s" +
                          (index.CacheNote == null ? "" : "  " + index.CacheNote));
        return exit;
    }

    private static void Info(bool json, string line)
    {
        if (json) Console.Error.WriteLine(line);
        else Console.WriteLine(line);
    }

    private static void Progress(string line) => Console.Error.WriteLine(line);

    private static int Fail(bool json, string target, string kind, string message)
    {
        if (json)
        {
            var report = new ScanReport("scan-index", new ScanContext(target, null, null, 0),
                new Dictionary<string, int>(), new[] { new ScanFinding(kind, target, null, new[] { message }) }, 2);
            Console.WriteLine(report.ToJson());
        }
        else
        {
            Console.Error.WriteLine(message);
        }
        return 2;
    }

    private static string? After(string[] args, string flag)
    {
        int at = Array.IndexOf(args, flag);
        return at >= 0 && at + 1 < args.Length ? args[at + 1] : null;
    }

    private static string First(string message) =>
        message.Split('\n', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()?.Trim() ?? message.Trim();
}
