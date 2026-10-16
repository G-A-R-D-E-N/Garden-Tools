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
        int scanned = 0, archives = 0, skippedArchives = 0, unreadable = 0;

        var ba2s = Mo2ScanData.EnumerateFilesSafe(root, "*.ba2", recursive: true)
            .Where(path => path.EndsWith(".ba2", StringComparison.OrdinalIgnoreCase))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToList();
        Info(json, $"scanning {ba2s.Count} .ba2 archive(s) under {root}");

        int opened = 0;
        foreach (string ba2path in ba2s)
        {
            if (StepDue(++opened, ba2s.Count)) Progress($"  opened {opened}/{ba2s.Count} archive(s)...");
            Ba2 archive;
            try { archive = Ba2.Open(ba2path); }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException)
            {
                skippedArchives++;
                if (!json) Console.WriteLine($"skip     {Rel(root, ba2path)}: {First(e.Message)}");
                findings.Add(new ScanFinding("skip", Rel(root, ba2path), null, new[] { First(e.Message) }));
                continue;
            }

            using (archive)
            {
                var entries = archive.Entries
                    .Where(IsSkeletonEntry)
                    .OrderBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase)
                    .ToList();
                if (entries.Count == 0) continue;
                archives++;

                foreach (var entry in entries)
                {
                    byte[] bytes;
                    try { bytes = archive.Read(entry); }
                    catch (Exception e)
                    {
                        unreadable++;
                        if (!json)
                            Console.WriteLine($"UNREADABLE  {entry.Name}  [{Rel(root, ba2path)}]: {First(e.Message)}");
                        findings.Add(new ScanFinding("unreadable", entry.Name, Rel(root, ba2path),
                                                     new[] { First(e.Message) }));
                        continue;
                    }

                    scanned++;
                    RecordSkeletonPhysics(bytes, entry.Name, Rel(root, ba2path), json, findings, counts);
                }
            }
        }

        var loose = Mo2ScanData.EnumerateFilesSafe(root, "*.hkx", recursive: true)
            .Where(IsLooseSkeleton)
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToList();
        Info(json, $"scanning {loose.Count} loose skeleton .hkx");

        int checkedLoose = 0;
        foreach (string path in loose)
        {
            if (StepDue(++checkedLoose, loose.Count)) Progress($"  checked {checkedLoose}/{loose.Count} loose file(s)...");
            string relative = Rel(root, path);
            byte[] bytes;
            try { bytes = InputFilePolicy.ReadHkx(path); }
            catch (Exception e) when (e is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException)
            {
                unreadable++;
                if (!json) Console.WriteLine($"UNREADABLE  {relative}: {First(e.Message)}");
                findings.Add(new ScanFinding("unreadable", relative, "loose", new[] { First(e.Message) }));
                continue;
            }

            scanned++;
            RecordSkeletonPhysics(bytes, relative, "loose", json, findings, counts);
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
    }

    private static void RecordSkeletonPhysics(byte[] bytes, string path, string? source, bool json,
                                              List<ScanFinding> findings,
                                              Dictionary<PhysicsLoadStatus, int> counts)
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

    private static bool IsSkeletonEntry(Ba2.Entry entry) =>
        entry.Name.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) &&
        entry.FileName.StartsWith("skeleton", StringComparison.OrdinalIgnoreCase);

    private static bool IsLooseSkeleton(string path) =>
        path.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) &&
        Path.GetFileName(path).StartsWith("skeleton", StringComparison.OrdinalIgnoreCase);

}
