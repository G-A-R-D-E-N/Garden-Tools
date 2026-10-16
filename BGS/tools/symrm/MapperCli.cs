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
        if (argv.Length < 2) { Usage(); return 1; }

        string target = Path.GetFullPath(argv[1]);
        bool recompute = false;
        string? outPath = null;
        string? dataFolder = null;

        for (int i = 2; i < argv.Length; i++)
        {
            if (argv[i] == "--recompute") { recompute = true; continue; }
            if (argv[i] == "--out" && i + 1 < argv.Length) { outPath = Path.GetFullPath(argv[++i]); continue; }
            if (argv[i] == "--data" && i + 1 < argv.Length) { dataFolder = Path.GetFullPath(argv[++i]); continue; }
        }

        if (dataFolder != null) return MapperSweep(dataFolder, recompute);

        byte[] source;
        try { source = InputFilePolicy.ReadHkx(target); }
        catch (Exception error)
        {
            Console.Error.WriteLine($"cannot read {target}: {error.Message}");
            return 1;
        }

        return MapperOne(target, source, recompute, outPath);
    }

    private static int MapperOne(string file, byte[] source, bool recompute, string? outPath)
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

            Console.WriteLine($"  recomputed {rows} simple row(s): worst translation error " +
                              $"{worstTranslation:0.####e+0}, worst rotation error {worstRotation:0.####e+0} rad");
            Console.WriteLine("  chain rows were left alone: their interior distribution is not reduced to numbers");
        }

        byte[] produced;
        try { produced = document.Serialize(); }
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

        if (outPath != null)
        {
            File.WriteAllBytes(outPath, produced);
            Console.WriteLine($"  written to {outPath}");
        }

        return 0;
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

    private static int MapperSweep(string dataFolder, bool recompute)
    {
        if (!Directory.Exists(dataFolder))
        {
            Console.Error.WriteLine($"--data folder not found: {dataFolder}");
            return 1;
        }

        using var data = OpenCommonwealth.Services.Archive.GameData.Discover(dataFolder);

        var candidates = data.EnumerateEntries()
                             .Where(item => item.Entry.Name.EndsWith(".hkx", StringComparison.OrdinalIgnoreCase) &&
                                            item.Entry.Name.Contains("CharacterAssets", StringComparison.OrdinalIgnoreCase) &&
                                            item.Entry.FileName.Contains("skeleton", StringComparison.OrdinalIgnoreCase))
                             .GroupBy(item => item.ArchivePath, StringComparer.OrdinalIgnoreCase)
                             .OrderBy(group => group.Key, StringComparer.Ordinal)
                             .ToList();

        int archives = 0, files = 0, mappers = 0, byteIdentical = 0, unreadable = 0;
        int recomputed = 0, structuralOk = 0, structuralDuplicated = 0, structuralFailed = 0;
        float worstTranslation = 0f, worstRotation = 0f;
        var failures = new List<string>();

        foreach (var group in candidates)
        {
            archives++;
            using var archive = OpenCommonwealth.Services.Archive.Ba2.Open(group.Key);

            var entries = group.Select(item => item.Entry)
                               .OrderBy(entry => entry.Name, StringComparer.Ordinal)
                               .ToList();

            foreach (var entry in entries)
            {
                byte[] bytes;
                try { bytes = archive.Read(entry); }
                catch (Exception error) { failures.Add($"{entry.Name}: {error.Message}"); continue; }

                int count;
                try { count = SkeletonMapperReader.Read(new PackfileObjects(PackfileImage.Read(bytes))).Count; }
                catch (Exception) { unreadable++; continue; }
                if (count == 0) continue;

                files++;
                mappers += count;

                try
                {
                    var document = new SkeletonMapperDocument(bytes);
                    byte[] unedited = document.Serialize();
                    if (!unedited.AsSpan().SequenceEqual(bytes))
                        failures.Add($"{entry.Name}: an unedited round trip did not reproduce the file");

                    byteIdentical++;

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
                    catch (Exception error) { structuralFailed++; failures.Add($"{entry.Name}: {error.Message}"); }
                }
                catch (Exception error) { failures.Add($"{entry.Name}: {error.Message}"); }
            }
        }

        foreach (string failure in failures.Take(20)) Console.WriteLine("  " + failure);
        if (failures.Count > 20) Console.WriteLine($"  ... and {failures.Count - 20} more");

        Console.WriteLine($"\n{archives} archive(s): {files} file(s) with an hkaSkeletonMapper, {mappers} mapper(s), " +
                          $"{unreadable} unreadable");
        Console.WriteLine($"unedited round trip: {byteIdentical} byte-identical, " +
                          $"{files - byteIdentical} did not reproduce their bytes");
        if (recompute)
            Console.WriteLine($"recomputed {recomputed} simple row(s): worst translation error " +
                              $"{worstTranslation:0.####e+0}, worst rotation error {worstRotation:0.####e+0} rad");
        Console.WriteLine($"add-then-remove round trip: {structuralOk} read back as edited " +
                          $"({structuralDuplicated} needed a duplicated bone pair), {structuralFailed} failed");

        return failures.Count == 0 ? 0 : 1;
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
