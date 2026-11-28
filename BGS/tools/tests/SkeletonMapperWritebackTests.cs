using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class SkeletonMapperWritebackTests
{
    private readonly ITestOutputHelper _out;

    public SkeletonMapperWritebackTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void EveryMapperFixtureRoundTripsByteIdentically()
    {
        var files = Directory.GetFiles(FixturesRoot, "*.hkx", SearchOption.AllDirectories)
                             .OrderBy(file => file, StringComparer.Ordinal)
                             .ToList();

        int withMappers = 0;
        foreach (string file in files)
        {
            byte[] bytes = File.ReadAllBytes(file);
            if (!HasMapper(bytes)) continue;

            withMappers++;
            var document = new SkeletonMapperDocument(bytes);
            byte[] produced = document.Serialize();

            Assert.False(document.Dirty);
            Assert.True(produced.AsSpan().SequenceEqual(bytes),
                $"{Relative(file)} did not round-trip byte-identically when nothing was edited");
            Assert.NotEmpty(document.Mappers);
        }

        _out.WriteLine($"{withMappers} mapper-bearing fixture(s) of {files.Count} round-trip byte-identically");
        Assert.True(withMappers > 0, "no mapper-bearing fixture was found");
    }

    [Fact]
    public void EveryMapperFixtureForcedRebuildIsByteIdenticallyRebuilt()
    {
        var files = Directory.GetFiles(FixturesRoot, "*.hkx", SearchOption.AllDirectories)
                             .OrderBy(file => file, StringComparer.Ordinal)
                             .ToList();

        int withMappers = 0;
        foreach (string file in files)
        {
            byte[] bytes = File.ReadAllBytes(file);
            if (!HasMapper(bytes)) continue;

            withMappers++;
            var document = new SkeletonMapperDocument(bytes);
            byte[] rebuilt = document.ForceRebuild();

            Assert.Equal(bytes.Length, rebuilt.Length);
            Assert.True(rebuilt.AsSpan().SequenceEqual(bytes),
                $"{Relative(file)} was not byte-identical after a forced rebuild through the writer");
        }

        _out.WriteLine($"{withMappers} mapper-bearing fixture(s) of {files.Count} survive an unedited forced rebuild");
        Assert.True(withMappers > 0, "no mapper-bearing fixture was found");
    }

    [Fact]
    public void ValueEditChangesOnlyTheEditedRowAndReadsBack()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];

        int boneA = node.Rows[0].BoneA;
        int boneB = node.Rows[0].BoneB;
        var before = node.Rows[0].AFromB;
        var wanted = HavokSkeletonMappingTransform.From(
            new Vector3(0.25f, -0.5f, 0.125f), Quaternion.CreateFromYawPitchRoll(0.1f, 0.2f, 0.3f), Vector3.One);

        document.SetSimpleRowTransform(0, 0, wanted);

        Assert.True(document.Dirty);
        byte[] produced = document.Serialize();

        Assert.Equal(bytes.Length, produced.Length);
        var range = RowTransformRange(bytes, node, 0);
        for (int at = 0; at < produced.Length; at++)
        {
            if (produced[at] == bytes[at]) continue;
            Assert.True(at >= range.Start && at < range.Start + range.Length,
                $"byte 0x{at:x} changed outside the edited row's transform (0x{range.Start:x}..0x{range.Start + range.Length - 1:x})");
        }

        var reread = new SkeletonMapperDocument(produced);
        Assert.Equal(boneA, reread.Mappers[0].Rows[0].BoneA);
        Assert.Equal(boneB, reread.Mappers[0].Rows[0].BoneB);
        Assert.NotEqual(before, reread.Mappers[0].Rows[0].AFromB);
        Assert.Equal(wanted.Rotation, reread.Mappers[0].Rows[0].AFromB.Rotation);
        Assert.Equal(wanted.Translation, reread.Mappers[0].Rows[0].AFromB.Translation);
        Assert.Equal(node.Rows, reread.Mappers[0].Rows);
    }

    [Fact]
    public void TranslationAndRotationEditKeepsTheStoredScaleLanes()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        var stored = node.Rows[15].AFromB;

        document.SetSimpleRowTransform(0, 15, new Vector3(0.5f, 0.25f, -0.125f),
                                       Quaternion.CreateFromYawPitchRoll(0.4f, 0.1f, 0.2f));
        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(stored.Scale, reread.Mappers[0].Rows[15].AFromB.Scale);
        Assert.Equal(stored[3], reread.Mappers[0].Rows[15].AFromB[3]);
        Assert.Equal(stored[11], reread.Mappers[0].Rows[15].AFromB[11]);
        Assert.NotEqual(stored.Translation, reread.Mappers[0].Rows[15].AFromB.Translation);
    }

    [Fact]
    public void BoneEditChangesOnlyTheEditedRowBonesAndReadsBack()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int freeA = FreeBoneA(node);
        int boneB = node.Rows[1].BoneB;

        document.SetSimpleRowBones(0, 1, freeA, boneB);
        byte[] produced = document.Serialize();

        var reread = new SkeletonMapperDocument(produced);
        Assert.Equal(freeA, reread.Mappers[0].Rows[1].BoneA);
        Assert.Equal(boneB, reread.Mappers[0].Rows[1].BoneB);
        Assert.Equal(node.Rows, reread.Mappers[0].Rows);
        Assert.Equal(node.Rows[0].BoneA, reread.Mappers[0].Rows[0].BoneA);
    }

    [Fact]
    public void RecomputedRowsMatchTheShippedValues()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);

        float worstTranslation = 0f;
        float worstRotation = 0f;
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
                rows++;
            }
        }

        float extent = MathF.Max(Extent(document.Mappers[0].SkeletonA), Extent(document.Mappers[0].SkeletonB));
        float relative = worstTranslation / extent;

        _out.WriteLine($"{rows} shipped simple row(s): worst translation error {worstTranslation:0.####e+0} " +
                       $"({relative:0.####e+0} of the larger skeleton's {extent:0.###} extent), " +
                       $"worst rotation error {worstRotation:0.####e+0} rad");

        Assert.True(extent > 1f, $"the larger skeleton spans only {extent}, so the fixture is not a real rig");
        Assert.True(relative < 1e-5f,
            $"worst translation error {worstTranslation} is {relative} of the {extent} extent");
        Assert.True(worstRotation < 1e-5f, $"worst rotation error {worstRotation}");
    }

    private static float Extent(HavokRagdollSkeleton skeleton) =>
        skeleton.ReferencePose.Count == 0
            ? 0f
            : skeleton.ReferencePose.Max(pose => pose.Translation.Length());

    [Fact]
    public void RecomputedRowsAreWrittenBackAndStillMatch()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        int recomputed = document.RecomputeAllSimpleRows();
        Assert.Equal(36, recomputed);

        for (int mapper = 0; mapper < document.Mappers.Count; mapper++)
            for (int row = 0; row < document.Mappers[mapper].Rows.Count; row++)
            {
                var node = document.Mappers[mapper];
                var wanted = SkeletonMapperDocument.RecomputedTransform(
                    node.SkeletonA, node.Rows[row].BoneA, node.SkeletonB, node.Rows[row].BoneB);
                Assert.Equal(wanted.Translation, node.Rows[row].AFromB.Translation);
                Assert.Equal(wanted.Rotation, node.Rows[row].AFromB.Rotation);
            }

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);
        for (int mapper = 0; mapper < document.Mappers.Count; mapper++)
            Assert.Equal(document.Mappers[mapper].Rows, reread.Mappers[mapper].Rows);
    }

    [Fact]
    public void AddingARowSurvivesTheNativeRelocate()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int freeA = FreeBoneA(node);
        int boneB = node.Rows[0].BoneB;

        int at = document.AddSimpleRow(0, freeA, boneB, allowDuplicatePair: true);

        Assert.Equal(node.Rows.Count, at + 1);
        Assert.Equal(freeA, node.Rows[at].BoneA);
        Assert.Equal(boneB, node.Rows[at].BoneB);
        Assert.Equal(SkeletonMapperDocument.RecomputedTransform(node.SkeletonA, freeA, node.SkeletonB, boneB).Rotation,
                     node.Rows[at].AFromB.Rotation);

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(node.Rows.Count, reread.Mappers[0].Rows.Count);
        Assert.Equal(node.Rows, reread.Mappers[0].Rows);
        Assert.Equal(document.Mappers[1].Rows, reread.Mappers[1].Rows);
        Assert.Equal(document.Mappers[1].ChainMappings, reread.Mappers[1].ChainMappings);
        Assert.Equal(document.Mappers[1].UnmappedBones, reread.Mappers[1].UnmappedBones);
        Assert.NotEqual(bytes.Length, produced.Length);
    }

    [Fact]
    public void InsertingARowAtTheFrontSurvivesTheNativeRelocate()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int freeA = FreeBoneA(node);
        int boneB = node.Rows[0].BoneB;
        var original = node.Rows.ToList();

        document.InsertSimpleRow(0, 0, freeA, boneB, allowDuplicatePair: true);
        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(original.Count + 1, reread.Mappers[0].Rows.Count);
        Assert.Equal(freeA, reread.Mappers[0].Rows[0].BoneA);
        Assert.Equal(original, reread.Mappers[0].Rows.Skip(1).ToList());
    }

    [Fact]
    public void RemovingARowSurvivesTheNativeRelocate()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        var original = node.Rows.ToList();

        document.RemoveSimpleRow(0, 2);
        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(original.Count - 1, reread.Mappers[0].Rows.Count);
        Assert.Equal(original.Where((_, index) => index != 2).ToList(), reread.Mappers[0].Rows);
        Assert.Equal(original.Count - 1, document.Mappers[0].Rows.Count);
    }

    [Fact]
    public void RemovingEveryRowIsWrittenAndReadBack()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var chains = document.Mappers[1].ChainMappings.ToList();
        int rows = document.Mappers[1].Rows.Count;
        for (int row = 0; row < rows; row++) document.RemoveSimpleRow(1, 0);
        Assert.Empty(document.Mappers[1].Rows);

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Empty(reread.Mappers[1].Rows);
        Assert.Equal(chains, reread.Mappers[1].ChainMappings);
        Assert.Equal(document.Mappers[0].Rows, reread.Mappers[0].Rows);
    }

    [Fact]
    public void ChainRowEditSurvivesTheNativeRelocate()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        Assert.NotEmpty(node.ChainMappings);

        var chain = node.ChainMappings[0];
        int startBoneA = chain.StartBoneA + 1;
        int endBoneA = chain.EndBoneA + 1;
        var startTransform = chain.StartAFromB;
        var endTransform = chain.EndAFromB;
        document.SetChainRow(0, 0, startBoneA, endBoneA, chain.StartBoneB, chain.EndBoneB);
        document.SetChainRowTransforms(0, 0, chain.StartAFromB.WithScale(new Vector3(1.5f, 2f, 2.5f)),
                                       chain.EndAFromB.WithScale(new Vector3(0.5f, 0.75f, 1.25f)));

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(startBoneA, reread.Mappers[0].ChainMappings[0].StartBoneA);
        Assert.Equal(endBoneA, reread.Mappers[0].ChainMappings[0].EndBoneA);
        Assert.Equal(document.Mappers[0].ChainMappings, reread.Mappers[0].ChainMappings);
        Assert.Equal(node.Rows, reread.Mappers[0].Rows);
        Assert.Equal(chain.StartBoneA + 1, startBoneA);
        Assert.Equal(chain.StartBoneB, reread.Mappers[0].ChainMappings[0].StartBoneB);
        Assert.NotEqual(startTransform, reread.Mappers[0].ChainMappings[0].StartAFromB);
        Assert.NotEqual(endTransform, reread.Mappers[0].ChainMappings[0].EndAFromB);
    }

    [Fact]
    public void MappingTypeAndKeepUnmappedLocalArePatchedInPlace()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        document.SetMappingType(0, 1);
        document.SetKeepUnmappedLocal(1, false);

        byte[] produced = document.Serialize();
        Assert.Equal(bytes.Length, produced.Length);

        var reread = new SkeletonMapperDocument(produced);
        Assert.Equal(1, reread.Mappers[0].MappingType);
        Assert.False(reread.Mappers[1].KeepUnmappedLocal);
        Assert.Equal(0, reread.Mappers[1].MappingType);
        Assert.True(reread.Mappers[0].KeepUnmappedLocal);
    }

    [Fact]
    public void AddingAndRemovingInOneEditAgreesWithTheEditedModel()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int freeA = FreeBoneA(node);
        int boneB = node.Rows[0].BoneB;
        var expected = node.Rows.ToList();

        document.RemoveSimpleRow(0, 0);
        expected.RemoveAt(0);
        document.InsertSimpleRow(0, expected.Count, freeA, boneB, allowDuplicatePair: true);
        expected.Add(node.Rows[^1]);

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);

        Assert.Equal(expected.Count, reread.Mappers[0].Rows.Count);
        Assert.Equal(expected, reread.Mappers[0].Rows);
    }

    [Fact]
    public void RefusalsAreExplicit()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];

        Assert.Contains("no mapper", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowBones(9, 0, 0, 0)).Message);
        Assert.Contains("no row", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowBones(0, 99, 0, 0)).Message);
        Assert.Contains("not a bone of", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowBones(0, 0, node.SkeletonA.BoneNames.Count, 0)).Message);
        Assert.Contains("not a bone of", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowBones(0, 0, 0, node.SkeletonB.BoneNames.Count)).Message);

        Assert.Contains("already mapped", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowBones(0, 0, node.Rows[1].BoneA, node.Rows[0].BoneB)).Message);
        Assert.Contains("already mapped", Assert.Throws<InvalidOperationException>(
            () => document.AddSimpleRow(0, node.Rows[0].BoneA, node.Rows[0].BoneB)).Message);

        Assert.Contains("non-finite", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowTransform(0, 0, HavokSkeletonMappingTransform.From(
                new Vector3(float.NaN, 0f, 0f), Quaternion.Identity, Vector3.One))).Message);
        Assert.Contains("unit quaternion", Assert.Throws<InvalidOperationException>(
            () => document.SetSimpleRowTransform(0, 0, HavokSkeletonMappingTransform.From(
                Vector3.Zero, new Quaternion(1f, 1f, 1f, 1f), Vector3.One))).Message);

        Assert.Contains("mapping type", Assert.Throws<InvalidOperationException>(
            () => document.SetMappingType(0, 7)).Message);

        Assert.Contains("no chain row", Assert.Throws<InvalidOperationException>(
            () => document.SetChainRow(0, 99, 0, 0, 0, 0)).Message);
        Assert.Contains("no chain row", Assert.Throws<InvalidOperationException>(
            () => document.SetChainRowTransforms(0, 99, HavokSkeletonMappingTransform.Identity,
                                                 HavokSkeletonMappingTransform.Identity)).Message);

        Assert.False(document.Dirty);
    }

    [Fact]
    public void AFileWithoutAMapperIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(Path.Combine(
            FixturesRoot, "Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx"));

        Assert.Throws<InvalidDataException>(() => new SkeletonMapperDocument(bytes));
    }

    [Fact]
    public void AShippedFileWithDuplicateRowsStillLoadsAndCanBeEditedElsewhere()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int freeA = FreeBoneA(node);
        int pairedB = node.Rows[0].BoneB;

        Assert.Contains("already mapped", Assert.Throws<InvalidOperationException>(
            () => document.AddSimpleRow(0, freeA, pairedB)).Message);

        int at = document.AddSimpleRow(0, freeA, pairedB, allowDuplicatePair: true);
        Assert.Equal(node.Rows.Count, at + 1);

        var duplicate = new HavokSkeletonSimpleMappingRow(node.Rows[1].BoneA, node.Rows[1].BoneB,
                                                          node.Rows[1].AFromB);
        Assert.Contains("already mapped", Assert.Throws<InvalidOperationException>(
            () => document.InsertSimpleRow(0, 0, duplicate)).Message);

        byte[] produced = document.Serialize();
        var reread = new SkeletonMapperDocument(produced);
        Assert.Equal(document.Mappers[0].Rows, reread.Mappers[0].Rows);
    }

    [Fact]
    public void SaveWritesTheEditedFileAndReloadsIt()
    {
        string work = Path.Combine(Path.GetTempPath(), "bgs_mapper_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(work);
        string copy = Path.Combine(work, "skeleton.hkx");
        File.WriteAllBytes(copy, File.ReadAllBytes(FixturePath()));

        try
        {
            var document = SkeletonMapperDocument.Open(copy);
            document.SetKeepUnmappedLocal(0, false);
            document.Save();

            Assert.False(document.Dirty);
            var reopened = SkeletonMapperDocument.Open(copy);
            Assert.False(reopened.Mappers[0].KeepUnmappedLocal);
            Assert.Equal(2, reopened.Mappers.Count);
        }
        finally
        {
            Directory.Delete(work, recursive: true);
        }
    }

    private static int FreeBoneA(SkeletonMapperNode node)
    {
        var used = node.Rows.Select(row => row.BoneA).ToHashSet();
        for (int a = 0; a < node.SkeletonA.BoneNames.Count; a++)
            if (!used.Contains(a)) return a;

        throw new InvalidOperationException("every bone on the A side is already mapped");
    }

    private static (int Start, int Length) RowTransformRange(byte[] bytes, SkeletonMapperNode node, int row)
    {
        var objects = new PackfileObjects(PackfileImage.Read(bytes));
        int dataAt = objects.FieldAt(node.Instance, "mapping")!.Value;
        int header = dataAt + HavokClasses.Shipped.Field("hkaSkeletonMapperData", "simpleMappings")!.Offset;
        int stride = HavokClassTypes.Shipped["hkaSkeletonMapperDataSimpleMapping"]!.Size!.Value;
        var run = objects.ArrayAt(header, stride);
        int transform = HavokClasses.Shipped
            .Field("hkaSkeletonMapperDataSimpleMapping", "aFromBTransform")!.Offset;

        int at = DataSectionStart(bytes) + run!.At + row * stride + transform;
        return (at, HavokSkeletonMappingTransform.LaneCount * sizeof(float));
    }

    private static int DataSectionStart(byte[] bytes)
    {
        var image = PackfileImage.Read(bytes);
        int predicateBytes = BitConverter.ToUInt16(bytes, 0x3E);
        int first = 0x40 + predicateBytes;
        int index = image.Sections.IndexOf(image.Section("__data__")!);
        return BitConverter.ToInt32(bytes, first + index * 0x40 + 0x14);
    }

    private static bool HasMapper(byte[] bytes)
    {
        try
        {
            var objects = new PackfileObjects(PackfileImage.Read(bytes));
            return objects.Instances.Any(instance => instance.ClassName == "hkaSkeletonMapper");
        }
        catch (Exception)
        {
            return false;
        }
    }

    private static float RotationDistance(Quaternion a, Quaternion b)
    {
        if (a.LengthSquared() < 1e-12f || b.LengthSquared() < 1e-12f) return float.MaxValue;
        return 2f * MathF.Acos(Math.Clamp(
            MathF.Abs(Quaternion.Dot(Quaternion.Normalize(a), Quaternion.Normalize(b))), 0f, 1f));
    }

    private static string Relative(string file) => Path.GetRelativePath(FixturesRoot, file).Replace('\\', '/');

    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");

    private static string FixturePath() => Path.Combine(
        FixturesRoot, "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");
}
