using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.App;

public static partial class ModlistScan
{
    public static int ScanSkeletonPhysics(string[] args)
    {
        bool json = args.Contains("--json");
        string? root = After(args, "--scan-skeleton-physics");
        if (root == null || !Directory.Exists(root))
            return Fail(json, "scan-skeleton-physics", root ?? "", "usage",
                        "--scan-skeleton-physics needs an existing directory");

        var findings = new List<ScanFinding>();
        var counts = new Dictionary<PhysicsLoadStatus, int>();
        int scanned = 0, archives = 0, skippedArchives = 0, unreadable = 0, shadowed = 0;

        using GameData data = Mo2ScanData.OpenScanData(root);
        Info(json, $"scanning skeleton .hkx under {root}");

        var failedArchives = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var winners = data.EnumerateWinners("", GameData.IsSkeletonName, (archivePath, message) =>
        {
            if (failedArchives.Add(archivePath)) skippedArchives++;
            string where = Rel(root, archivePath);
            if (!json) Console.WriteLine($"skip     {where}: {message}");
            findings.Add(new ScanFinding("skip", where, null, new[] { message }));
        });

        var packed = winners.Where(winner => winner.Origin == GameAssetOrigin.Archive)
                            .OrderBy(winner => winner.ArchivePath, StringComparer.Ordinal)
                            .ThenBy(winner => winner.VirtualPath, StringComparer.OrdinalIgnoreCase)
                            .ToList();
        var loose = winners.Where(winner => winner.Origin == GameAssetOrigin.Loose)
                           .OrderBy(winner => winner.FullPath, StringComparer.OrdinalIgnoreCase)
                           .ToList();

        var archiveSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (GameAsset winner in winners)
        {
            CountArchive(winner);
            foreach (GameAsset loser in winner.Shadowed) CountArchive(loser);
            shadowed += winner.Shadowed.Count;
        }
        archives = archiveSet.Count;

        Info(json, $"found {packed.Count} packed and {loose.Count} loose skeleton file(s)");

        int processed = 0;
        foreach (GameAsset winner in packed)
        {
            if (StepDue(++processed, packed.Count)) Progress($"  read {processed}/{packed.Count} packed file(s)...");
            ReadSkeleton(winner, winner.VirtualPath, Rel(root, winner.ArchivePath!));
        }

        processed = 0;
        foreach (GameAsset winner in loose)
        {
            if (StepDue(++processed, loose.Count)) Progress($"  checked {processed}/{loose.Count} loose file(s)...");
            ReadSkeleton(winner, Rel(root, winner.FullPath!), "loose");
        }

        int ok = counts.GetValueOrDefault(PhysicsLoadStatus.Ok);
        int unsupported = counts.GetValueOrDefault(PhysicsLoadStatus.UnsupportedShapes);
        int skeletonOnly = counts.GetValueOrDefault(PhysicsLoadStatus.SkeletonOnly);
        int noUsable = counts.GetValueOrDefault(PhysicsLoadStatus.LegacyPhysics) +
                       counts.GetValueOrDefault(PhysicsLoadStatus.MapperMissing) +
                       counts.GetValueOrDefault(PhysicsLoadStatus.NoBodies) +
                       counts.GetValueOrDefault(PhysicsLoadStatus.WrongContainer) +
                       counts.GetValueOrDefault(PhysicsLoadStatus.ParseError);
        int exit = noUsable > 0 || unreadable > 0 ? 1 : 0;

        var summary = new Dictionary<string, int>
        {
            ["archives"] = archives,
            ["scanned"] = scanned,
            ["ok"] = ok,
            ["skeletonOnly"] = skeletonOnly,
            ["legacyPhysics"] = counts.GetValueOrDefault(PhysicsLoadStatus.LegacyPhysics),
            ["unsupportedShapes"] = unsupported,
            ["mapperMissing"] = counts.GetValueOrDefault(PhysicsLoadStatus.MapperMissing),
            ["noBodies"] = counts.GetValueOrDefault(PhysicsLoadStatus.NoBodies),
            ["wrongContainer"] = counts.GetValueOrDefault(PhysicsLoadStatus.WrongContainer),
            ["parseError"] = counts.GetValueOrDefault(PhysicsLoadStatus.ParseError),
            ["unreadable"] = unreadable,
            ["skippedArchives"] = skippedArchives,
            ["shadowed"] = shadowed,
        };

        if (json)
            Console.WriteLine(new ScanReport("scan-skeleton-physics", new ScanContext(root, null, null, 0),
                                             summary, findings, exit).ToJson());
        else
            Console.WriteLine($"done  {scanned} skeleton file(s) scanned across {archives} archive(s) + loose, " +
                              $"{ok} with a readable ragdoll, {unsupported} with preview gaps, " +
                              $"{skeletonOnly} skeleton-only, {noUsable} with no usable ragdoll, " +
                              $"{unreadable} unreadable");
        return exit;

        void CountArchive(GameAsset asset)
        {
            if (asset.Origin == GameAssetOrigin.Archive && asset.ArchivePath != null) archiveSet.Add(asset.ArchivePath);
        }

        void ReadSkeleton(GameAsset asset, string path, string source)
        {
            if (!data.TryRead(asset, out byte[] bytes, out string? warning))
            {
                unreadable++;
                string why = warning ?? "the winning copy could not be read";
                if (!json) Console.WriteLine($"UNREADABLE  {path}  [{source}]: {why}");
                findings.Add(new ScanFinding("unreadable", path, source, new[] { why }));
                return;
            }

            scanned++;
            RecordSkeletonPhysics(bytes, path, source, json, findings, counts, ShadowDetails(root, asset));
        }
    }

    private static List<string>? ShadowDetails(string root, GameAsset asset)
    {
        if (asset.Shadowed.Count == 0) return null;

        var details = new List<string>();
        foreach (GameAsset loser in asset.Shadowed)
        {
            string where = "loose";
            if (loser.Origin == GameAssetOrigin.Archive && loser.ArchivePath != null) where = Rel(root, loser.ArchivePath);
            else if (loser.FullPath != null) where = Rel(root, loser.FullPath);
            details.Add($"shadowed: {loser.VirtualPath} [{where}]");
        }
        return details;
    }

    private static void RecordSkeletonPhysics(byte[] bytes, string path, string? source, bool json,
                                              List<ScanFinding> findings,
                                              Dictionary<PhysicsLoadStatus, int> counts,
                                              IReadOnlyList<string>? shadowed = null)
    {
        PhysicsLoadReport report = PhysicsLoadClassifier.Inspect(bytes);
        counts[report.Status] = counts.TryGetValue(report.Status, out int seen) ? seen + 1 : 1;

        var details = new List<string>
        {
            $"status: {report.Status} ({report.StatusLabel})",
            $"bodies: {report.DescribeBodies()}",
            $"shapes: {report.ShapeCount} read",
            $"constraints: {report.ConstraintCount} read, {report.SkippedConstraints} skipped",
            $"physics classes: {PhysicsLoadReport.DescribeClasses(report.PhysicsClasses)}",
            $"shape classes: {PhysicsLoadReport.DescribeClasses(report.ShapeClasses)}",
            $"constraint classes: {PhysicsLoadReport.DescribeClasses(report.ConstraintClasses)}",
            $"mapper: {report.DescribeMapper()}",
            $"{PhysicsLoadReport.MaterialClass}: {(report.MaterialPresent ? "present" : "absent")}",
            $"why: {report.Headline}",
        };
        if (report.Hint is { Length: > 0 } hint) details.Add("next: " + hint);
        if (shadowed != null) details.AddRange(shadowed);
        findings.Add(new ScanFinding(report.StatusSlug, path, source, details));

        if (json) return;

        Console.WriteLine($"{report.StatusLabel,-17} {path}" + (source == null ? "" : $"  [{source}]"));
        if (report.Status != PhysicsLoadStatus.Ok)
        {
            Console.WriteLine("    " + report.Headline);
            if (report.Hint is { Length: > 0 } next) Console.WriteLine("    next: " + next);
        }
        Console.WriteLine($"    bodies {report.UsableBodies} usable of {report.BodyCount}, {report.SkippedBodies} skipped; " +
                          $"shapes {report.ShapeCount}; constraints {report.ConstraintCount}, {report.SkippedConstraints} skipped; " +
                          $"mapper {report.DescribeMapper()}; " +
                          $"{PhysicsLoadReport.MaterialClass} {(report.MaterialPresent ? "present" : "absent")}");
        Console.WriteLine("    shape classes: " + PhysicsLoadReport.DescribeClasses(report.ShapeClasses));
        Console.WriteLine("    constraint classes: " + PhysicsLoadReport.DescribeClasses(report.ConstraintClasses));
        Console.WriteLine("    physics classes: " + PhysicsLoadReport.DescribeClasses(report.PhysicsClasses));
    }
}
