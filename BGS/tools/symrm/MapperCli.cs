using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Hkx;

namespace BehaviourStudio.Tools;

public static partial class Program
{
    private static int Mapper(string[] argv)
    {
        string? target = null;
        bool recompute = false;
        bool force = false;
        string? outPath = null;
        string? dataFolder = null;

        for (int i = 1; i < argv.Length; i++)
        {
            string flag = argv[i];

            if (!flag.StartsWith("-", StringComparison.Ordinal))
            {
                if (target != null)
                    return MapperUsage($"this command reads one file, and '{flag}' is a second");

                if (FullPath(flag) is not string file) return MapperUsage($"'{flag}' is not a usable path");
                target = file;
                continue;
            }

            switch (flag)
            {
                case "--recompute":
                    recompute = true;
                    continue;

                case "--force":
                    force = true;
                    continue;

                case "--out":
                    if (i + 1 >= argv.Length) return MapperUsage("--out needs the file to write");
                    if (FullPath(argv[++i]) is not string outFile) return MapperUsage("--out is not a usable path");
                    outPath = outFile;
                    continue;

                case "--data":
                    if (i + 1 >= argv.Length) return MapperUsage("--data needs the Data folder to sweep");
                    if (FullPath(argv[++i]) is not string folder) return MapperUsage("--data is not a usable path");
                    dataFolder = folder;
                    continue;

                default:
                    return MapperUsage($"'{flag}' is not a mapper switch");
            }
        }

        if (dataFolder != null) return MapperSweep(dataFolder, recompute, outPath);

        if (target == null) return MapperUsage("mapper needs a file to read, or --data with a Data folder");

        byte[] source;
        try { source = InputFilePolicy.ReadHkx(target); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"cannot read {target}: {error.Message}");
            return 1;
        }

        return MapperOne(target, source, recompute, outPath, force);
    }

    private static int MapperUsage(string problem)
    {
        Console.Error.WriteLine(problem);
        Console.Error.WriteLine();
        Console.Error.WriteLine(SymrmHelp.TryCommand("mapper", out string help)
            ? help
            : "symrm mapper <skeleton.hkx> [--recompute] [--out <file>] [--force] [--data <Data folder>]");
        return 1;
    }

    private static string? FullPath(string value)
    {
        try { return Path.GetFullPath(value); }
        catch (Exception error) when (error is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }
    }

    private static int MapperOne(string file, byte[] source, bool recompute, string? outPath, bool force)
    {
        SkeletonMapperDocument document;
        try { document = new SkeletonMapperDocument(source); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"{Path.GetFileName(file)}: {error.Message}");
            return 1;
        }

        Console.WriteLine($"{Path.GetFileName(file)}  {source.Length} bytes, {document.Mappers.Count} mapper(s)");
        foreach (var node in document.Mappers) DescribeMapper(node);

        if (recompute)
        {
            float worstTranslation = 0f, worstRotation = 0f;
            int rows = 0;
            for (int mapper = 0; mapper < document.Mappers.Count; mapper++)
            {
                var node = document.Mappers[mapper];
                for (int row = 0; row < node.Rows.Count; row++)
                {
                    var stored = node.Rows[row];
                    var wanted = SkeletonMapperDocument.RecomputedTransform(
                        node.SkeletonA, stored.BoneA, node.SkeletonB, stored.BoneB);

                    worstTranslation = MathF.Max(worstTranslation,
                        Vector3.Distance(stored.AFromB.Translation, wanted.Translation));
                    worstRotation = MathF.Max(worstRotation,
                        RotationDistance(stored.AFromB.Rotation, wanted.Rotation));
                    document.RecomputeSimpleRow(mapper, row);
                    rows++;
                }
            }

            float extent = MathF.Max(SkeletonExtent(document.Mappers[0].SkeletonA),
                                     SkeletonExtent(document.Mappers[0].SkeletonB));
            Console.WriteLine($"  recomputed {rows} simple row(s): worst translation error " +
                              $"{worstTranslation:0.####e+0} ({worstTranslation / extent:0.####e+0} of the larger " +
                              $"skeleton's {extent:0.###} extent), worst rotation error {worstRotation:0.####e+0} rad");
            Console.WriteLine("  chain rows were left alone, interior and endpoints alike: nothing reduces " +
                              "their distribution to numbers");
        }

        byte[] produced;
        try { produced = document.Dirty ? document.Serialize() : document.ForceRebuild(); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"  refused: {error.Message}");
            return 1;
        }

        int firstDifference = FirstDifference(source, produced);
        Console.WriteLine(firstDifference < 0
            ? $"  round trip: byte-identical ({source.Length} bytes)"
            : $"  round trip: {source.Length} bytes in, {produced.Length} out, " +
              $"first difference at 0x{firstDifference:x}");

        if (outPath == null)
        {
            Console.WriteLine(recompute
                ? "  nothing was written: --recompute reports the error on its own; pass --out <file> to keep it"
                : "  nothing was written: pass --out <file> to keep the rebuilt file");
            return 0;
        }

        string sourcePath = Path.GetFullPath(file);
        bool replacesSource = string.Equals(outPath, sourcePath, StringComparison.OrdinalIgnoreCase);

        if (!force)
        {
            if (replacesSource)
            {
                Console.Error.WriteLine($"  refused: {outPath} is the file being read; pass --force to overwrite it");
                return 1;
            }

            if (File.Exists(outPath))
            {
                Console.Error.WriteLine($"  refused: {outPath} already exists; pass --force to overwrite it");
                return 1;
            }
        }

        try
        {
            if (replacesSource) FileSafety.ReplaceChecked(outPath, produced, DocumentSourceStamp.Capture(source));
            else FileSafety.Replace(outPath, produced);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"  cannot write {outPath}: {error.Message}");
            return 1;
        }

        Console.WriteLine(replacesSource
            ? $"  written to {outPath}; the version that was read is kept as {Path.GetFileName(outPath)}.bak"
            : $"  written to {outPath}");
        return 0;
    }

    private static float SkeletonExtent(HavokRagdollSkeleton skeleton)
    {
        float extent = 0f;
        foreach (var pose in skeleton.ReferencePose)
            extent = MathF.Max(extent, pose.Translation.Length());
        return MathF.Max(extent, 1e-3f);
    }

    private static void DescribeMapper(SkeletonMapperNode node)
    {
        Console.WriteLine($"  mapper #{node.ObjectId}: '{node.SkeletonA.Name}' ({node.SkeletonA.BoneNames.Count} bones) " +
                          $"-> '{node.SkeletonB.Name}' ({node.SkeletonB.BoneNames.Count} bones), " +
                          $"type {node.MappingType}, keepUnmappedLocal {node.KeepUnmappedLocal.ToString().ToLowerInvariant()}");
        Console.WriteLine($"    simple rows {node.Rows.Count}, chain rows {node.ChainMappings.Count}, " +
                          $"unmapped bones {node.UnmappedBones.Count}, " +
                          $"extracted motion {node.ExtractedMotionMapping}");

        foreach (var row in node.Rows)
            Console.WriteLine($"    {BoneName(node.SkeletonA, row.BoneA)} -> {BoneName(node.SkeletonB, row.BoneB)}  " +
                              $"translate ({row.AFromB.Translation.X:0.#####} {row.AFromB.Translation.Y:0.#####} {row.AFromB.Translation.Z:0.#####})  " +
                              $"scale ({row.AFromB.Scale.X:0.#####} {row.AFromB.Scale.Y:0.#####} {row.AFromB.Scale.Z:0.#####})");

        foreach (var chain in node.ChainMappings)
            Console.WriteLine($"    chain {BoneName(node.SkeletonA, chain.StartBoneA)}..{BoneName(node.SkeletonA, chain.EndBoneA)}" +
                              $" -> {BoneName(node.SkeletonB, chain.StartBoneB)}..{BoneName(node.SkeletonB, chain.EndBoneB)}");
    }

    private static string BoneName(HavokRagdollSkeleton skeleton, int bone) =>
        bone >= 0 && bone < skeleton.BoneNames.Count ? $"'{skeleton.BoneNames[bone]}'[{bone}]" : $"bone {bone}";

    private static float RotationDistance(Quaternion a, Quaternion b)
    {
        if (a.LengthSquared() < 1e-12f || b.LengthSquared() < 1e-12f) return float.MaxValue;
        var first = Quaternion.Normalize(a);
        var second = Quaternion.Normalize(b);
        return 2f * MathF.Acos(Math.Clamp(MathF.Abs(Quaternion.Dot(first, second)), 0f, 1f));
    }

    private static int MapperSweep(string dataFolder, bool recompute, string? outPath)
    {
        if (outPath != null)
        {
            Console.Error.WriteLine($"--out is refused in sweep mode ({outPath}): " +
                                    "a sweep reads a whole Data folder and writes nothing");
            return 1;
        }

        if (!Directory.Exists(dataFolder))
        {
            Console.Error.WriteLine($"--data folder not found: {dataFolder}");
            return 1;
        }

        using var data = OpenCommonwealth.Services.Archive.Mo2ScanData.OpenScanData(dataFolder);

        var candidates = data.EnumerateWinners("", IsMapperCandidate)
                             .OrderBy(asset => asset.ArchivePath, StringComparer.Ordinal)
                             .ThenBy(asset => asset.VirtualPath, StringComparer.Ordinal)
                             .ToList();

        int files = 0, mappers = 0, byteIdentical = 0, unreadable = 0;
        int recomputed = 0, structuralOk = 0, structuralDuplicated = 0, structuralFailed = 0;
        float worstTranslation = 0f, worstRotation = 0f;
        var failures = new List<string>();

        var archiveSet = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var asset in candidates)
        {
            if (asset.ArchivePath != null) archiveSet.Add(asset.ArchivePath);
            foreach (var loser in asset.Shadowed)
                if (loser.ArchivePath != null) archiveSet.Add(loser.ArchivePath);
        }
        int archives = archiveSet.Count;

        foreach (var asset in candidates)
        {
            if (!data.TryRead(asset, out byte[] bytes, out string? warning))
            {
                unreadable++;
                failures.Add($"{asset.VirtualPath}: {warning ?? "the winning copy could not be read"}");
                continue;
            }

            int count;
            try { count = SkeletonMapperReader.Read(new PackfileObjects(PackfileImage.Read(bytes))).Count; }
            catch (Exception) { unreadable++; continue; }
            if (count == 0) continue;

            files++;
            mappers += count;

            try
            {
                var document = new SkeletonMapperDocument(bytes);
                byte[] rebuilt = document.ForceRebuild();
                if (!rebuilt.AsSpan().SequenceEqual(bytes))
                {
                    int at = FirstDifference(bytes, rebuilt);
                    failures.Add($"{asset.VirtualPath}: rebuilding the unedited file was not byte-identical " +
                                 $"({bytes.Length} bytes in, {rebuilt.Length} out, " +
                                 $"first difference at 0x{at:x})");
                }
                else
                {
                    byteIdentical++;
                }

                if (recompute)
                    foreach (var node in document.Mappers)
                        for (int row = 0; row < node.Rows.Count; row++)
                        {
                            var stored = node.Rows[row];
                            var wanted = SkeletonMapperDocument.RecomputedTransform(
                                node.SkeletonA, stored.BoneA, node.SkeletonB, stored.BoneB);
                            worstTranslation = MathF.Max(worstTranslation,
                                Vector3.Distance(stored.AFromB.Translation, wanted.Translation));
                            worstRotation = MathF.Max(worstRotation,
                                RotationDistance(stored.AFromB.Rotation, wanted.Rotation));
                            recomputed++;
                        }

                var mapper = document.Mappers[0];
                var original = mapper.Rows.ToList();
                var free = FreePair(mapper);
                var pair = free ?? (BoneA: mapper.Rows[0].BoneA, BoneB: mapper.Rows[0].BoneB);

                try
                {
                    int at = mapper.Rows.Count;
                    document.AddSimpleRow(0, pair.BoneA, pair.BoneB, free == null);
                    if (free == null) structuralDuplicated++;
                    var grown = new SkeletonMapperDocument(document.Serialize());
                    if (grown.Mappers[0].Rows.Count != original.Count + 1)
                        throw new InvalidDataException("the grown file did not read back with one more row");

                    grown.RemoveSimpleRow(0, at);
                    var shrunk = new SkeletonMapperDocument(grown.Serialize());
                    if (!shrunk.Mappers[0].Rows.SequenceEqual(original))
                        throw new InvalidDataException("the shrunk file did not read back as the loaded rows");

                    structuralOk++;
                }
                catch (Exception error) { structuralFailed++; failures.Add($"{asset.VirtualPath}: {error.Message}"); }
            }
            catch (Exception error) { failures.Add($"{asset.VirtualPath}: {error.Message}"); }
        }

        foreach (string failure in failures.Take(20)) Console.WriteLine("  " + failure);
        if (failures.Count > 20) Console.WriteLine($"  ... and {failures.Count - 20} more");

        Console.WriteLine($"\n{archives} archive(s): {files} file(s) with an hkaSkeletonMapper, {mappers} mapper(s), " +
                          $"{unreadable} unreadable");
        Console.WriteLine($"unedited forced rebuild: {byteIdentical} byte-identical, " +
                          $"{files - byteIdentical} did not reproduce their bytes");
        if (recompute)
            Console.WriteLine($"recomputed {recomputed} simple row(s): worst translation error " +
                              $"{worstTranslation:0.####e+0}, worst rotation error {worstRotation:0.####e+0} rad");
        Console.WriteLine($"add-then-remove round trip: {structuralOk} read back as edited " +
                          $"({structuralDuplicated} needed a duplicated bone pair), {structuralFailed} failed");

        return failures.Count == 0 ? 0 : 1;
    }

    private static bool IsMapperCandidate(string virtualPath)
    {
        string flat = virtualPath.Replace('\\', '/');
        if (!flat.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase)) return false;
        if (!flat.Contains("CharacterAssets", StringComparison.OrdinalIgnoreCase)) return false;
        if (OpenCommonwealth.Services.Archive.GameData.IsSkeletonName(flat)) return true;

        int slash = flat.LastIndexOf('/');
        string name = slash >= 0 ? flat[(slash + 1)..] : flat;
        return name.Contains("skeleton", StringComparison.OrdinalIgnoreCase);
    }

    private static (int BoneA, int BoneB)? FreePair(SkeletonMapperNode node)
    {
        var usedA = node.Rows.Select(row => row.BoneA).ToHashSet();
        var usedB = node.Rows.Select(row => row.BoneB).ToHashSet();

        for (int a = 0; a < node.SkeletonA.BoneNames.Count; a++)
        {
            if (usedA.Contains(a)) continue;
            for (int b = 0; b < node.SkeletonB.BoneNames.Count; b++)
                if (!usedB.Contains(b)) return (a, b);
        }

        return null;
    }
}
