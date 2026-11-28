using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class SkeletonMapperWriterTests
{
    private readonly ITestOutputHelper _out;

    public SkeletonMapperWriterTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void AStructuralSaveTouchesOnlyItsOwnRowsAndTables()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var document = new SkeletonMapperDocument(bytes);
        var node = document.Mappers[0];
        int header = SimpleHeader(node);
        int oldAt = RunAt(bytes, node);
        int oldRows = node.Rows.Count;
        int stride = SimpleStride;

        document.AddSimpleRow(0, FreeBoneA(node), node.Rows[0].BoneB, allowDuplicatePair: true);
        byte[] produced = document.Serialize();

        int newAt = RunAt(produced, new SkeletonMapperDocument(produced).Mappers[0]);
        var allowed = new List<(int Start, int Length)>
        {
            (header, 16),
            (oldAt, oldRows * stride),
            (newAt, (oldRows + 1) * stride),
        };

        byte[] before = Data(bytes);
        byte[] after = Data(produced);
        int escaped = 0;
        for (int at = 0; at < before.Length; at++)
        {
            if (before[at] == after[at]) continue;
            if (allowed.Any(range => at >= range.Start && at < range.Start + range.Length)) continue;
            escaped++;
            _out.WriteLine($"0x{at:x} changed outside the array header and the row runs");
        }

        int stray = 0;
        for (int at = before.Length; at < after.Length; at++)
        {
            if (after[at] == 0) continue;
            if (at >= newAt && at < newAt + (oldRows + 1) * stride) continue;
            stray++;
            _out.WriteLine($"0x{at:x} in the appended tail is neither padding nor run");
        }

        _out.WriteLine($"{after.Length - before.Length} byte(s) appended for one extra simple row");
        Assert.Equal(0, escaped);
        Assert.Equal(0, stray);
        Assert.True(after.Length > before.Length);

        Assert.Equal(UntouchedFixups(bytes, header), UntouchedFixups(produced, header));
    }

    [Fact]
    public void TheOldRowRunIsZeroedAndEveryStructuralSaveAppendsAnotherRun()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var first = new SkeletonMapperDocument(bytes);
        var node = first.Mappers[0];
        int oldAt = RunAt(bytes, node);
        int oldRows = node.Rows.Count;
        int stride = SimpleStride;

        first.AddSimpleRow(0, FreeBoneA(node), node.Rows[0].BoneB, allowDuplicatePair: true);
        byte[] once = first.Serialize();
        byte[] onceData = Data(once);

        for (int i = 0; i < oldRows * stride; i++)
            Assert.Equal(0, onceData[oldAt + i]);

        var second = new SkeletonMapperDocument(once);
        int rows = second.Mappers[0].Rows.Count;
        second.RemoveSimpleRow(0, 0);
        byte[] twice = second.Serialize();

        _out.WriteLine($"{oldRows} row(s) of {stride} bytes were zeroed and left in place; a second save added " +
                       $"{twice.Length - once.Length} byte(s)");
        Assert.True(twice.Length - once.Length >= (rows - 1) * stride,
            $"a second structural save grew the file by only {twice.Length - once.Length} byte(s)");
    }

    [Fact]
    public void AFixupIntoADroppedRowIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        int header = SimpleHeader(nodes[0]);
        int rows = nodes[0].Rows.Count;
        int dropped = RunAt(bytes, nodes[0]) + (rows - 1) * SimpleStride + 8;

        byte[] patched = Edited(bytes, (image, data) => data.SetLocal(data.Data.Length - 8, dropped));
        string message = Refusal(patched, nodes, edited =>
        {
            for (int i = 0; i < 3; i++) edited.Rows.RemoveAt(edited.Rows.Count - 1);
        });

        _out.WriteLine(message);
        Assert.Contains("shares its row storage", message);
        Assert.True(rows - 3 < rows);
        Assert.True(header > 0);
    }

    [Fact]
    public void SharedRowStorageIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        int run = RunAt(bytes, nodes[0]);

        byte[] patched = Edited(bytes, (image, data) => data.SetLocal(data.Data.Length - 8, run + 4));
        string message = Refusal(patched, nodes, edited => edited.Rows.RemoveAt(0));

        _out.WriteLine(message);
        Assert.Contains("shares its row storage", message);
    }

    [Fact]
    public void AHeaderWithBothFixupsIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        int header = SimpleHeader(nodes[0]);
        int run = RunAt(bytes, nodes[0]);

        byte[] patched = Edited(bytes, (image, data) =>
            data.SetGlobal(header, image.Sections.IndexOf(data), run));
        string message = Refusal(patched, nodes, edited => edited.Rows.RemoveAt(0));

        _out.WriteLine(message);
        Assert.Contains("both a local and a global fixup", message);
    }

    [Fact]
    public void AHeaderWithNeitherFixupIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);

        byte[] patched = Edited(bytes, (image, data) => RemoveFixup(image, data, SimpleHeader(nodes[0])));
        string message = Refusal(patched, nodes, edited => edited.Rows.RemoveAt(0));

        _out.WriteLine(message);
        Assert.Contains("cannot be located", message);
    }

    [Fact]
    public void AGlobalFixupHeaderIsRelocated()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        int header = SimpleHeader(nodes[0]);
        int rows = nodes[0].Rows.Count;
        int run = RunAt(bytes, nodes[0]);

        byte[] patched = Edited(bytes, (image, data) =>
        {
            RemoveFixup(image, data, header);
            data.SetGlobal(header, image.Sections.IndexOf(data), run);
        });

        var fresh = Nodes(patched);
        Assert.Equal(rows, fresh[0].Rows.Count);

        byte[] produced = AddRow(patched, fresh);
        var reloaded = new SkeletonMapperDocument(produced);

        Assert.Equal(rows + 1, reloaded.Mappers[0].Rows.Count);
        Assert.Equal(fresh[1].Rows, reloaded.Mappers[1].Rows);
        Assert.Equal(fresh[1].UnmappedBones, reloaded.Mappers[1].UnmappedBones);
    }

    [Fact]
    public void AGlobalFixupInAnotherSectionOnTheSameOffsetIsIgnored()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        int header = SimpleHeader(nodes[0]);

        byte[] patched = Edited(bytes, (image, data) =>
        {
            var names = image.Section("__classnames__")!;
            names.SetGlobal(header, image.Sections.IndexOf(names), 0);
        });

        byte[] produced = AddRow(patched, Nodes(patched));
        var reloaded = new SkeletonMapperDocument(produced);

        Assert.Equal(nodes[0].Rows.Count + 1, reloaded.Mappers[0].Rows.Count);
        Assert.Equal(nodes[1].Rows, reloaded.Mappers[1].Rows);
    }

    [Fact]
    public void AStructuralChangeWithAChainCountChangeIsRefused()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        Assert.NotEmpty(nodes[0].ChainMappings);

        string message = Refusal(bytes, nodes, edited =>
        {
            edited.Rows.RemoveAt(0);
            edited.ChainMappings.Clear();
        });

        _out.WriteLine(message);
        Assert.Contains("only simple rows are resized", message);
        Assert.Contains("chain", message);
    }

    [Fact]
    public void AnUnrelatedMapperSurvivesAStructuralEditBesideIt()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        var untouched = nodes[1].Copy();
        int otherAt = RunAt(bytes, nodes[1]);
        int otherRows = nodes[1].Rows.Count;
        int span = HavokClassTypes.Shipped["hkaSkeletonMapper"]!.Size!.Value;
        int offset = new PackfileObjects(PackfileImage.Read(bytes)).Instances
            .Where(instance => instance.ClassName == "hkaSkeletonMapper").ElementAt(1).Offset;

        byte[] produced = AddRow(bytes, Nodes(bytes));
        var reloaded = new SkeletonMapperDocument(produced);

        Assert.Equal(2, reloaded.Mappers.Count);
        Assert.Equal(untouched.Rows, reloaded.Mappers[1].Rows);
        Assert.Equal(untouched.ChainMappings, reloaded.Mappers[1].ChainMappings);
        Assert.Equal(untouched.UnmappedBones, reloaded.Mappers[1].UnmappedBones);
        Assert.Equal(untouched.ExtractedMotionMapping, reloaded.Mappers[1].ExtractedMotionMapping);

        byte[] before = Data(bytes);
        byte[] after = Data(produced);
        for (int i = 0; i < span; i++) Assert.Equal(before[offset + i], after[offset + i]);
        for (int i = 0; i < otherRows * SimpleStride; i++)
            Assert.Equal(before[otherAt + i], after[otherAt + i]);
    }

    [Fact]
    public void AForcedRebuildRunsTheWritePathRatherThanReturningTheSource()
    {
        byte[] bytes = File.ReadAllBytes(FixturePath());
        var nodes = Nodes(bytes);
        byte[] strip = Edited(bytes, (image, data) => RemoveFixup(image, data, SimpleHeader(nodes[0])));

        byte[] clone = new SkeletonMapperDocument(bytes).Serialize();
        Assert.True(clone.AsSpan().SequenceEqual(bytes));

        var error = Record.Exception(() => SkeletonMapperWriteback.Write(
            strip, nodes, Array.Empty<SkeletonMapperChange>(), force: true));

        Assert.NotNull(error);
        _out.WriteLine(error!.Message);
        Assert.Contains("cannot be located", error.Message);
    }

    private static byte[] AddRow(byte[] bytes, List<SkeletonMapperNode> nodes)
    {
        var original = nodes[0].Copy();
        var edited = nodes[0];
        edited.Rows.Add(new HavokSkeletonSimpleMappingRow(FreeBoneA(edited), edited.Rows[0].BoneB,
                                                          HavokSkeletonMappingTransform.Identity));
        return SkeletonMapperWriteback.Write(bytes, nodes, new List<SkeletonMapperChange> { new(edited, original) });
    }

    private static string Refusal(byte[] bytes, List<SkeletonMapperNode> nodes,
                                 Action<SkeletonMapperNode> edit)
    {
        var original = nodes[0].Copy();
        var edited = nodes[0];
        edit(edited);

        var error = Record.Exception(() => SkeletonMapperWriteback.Write(
            bytes, nodes, new List<SkeletonMapperChange> { new(edited, original) }));

        Assert.NotNull(error);
        return error!.Message;
    }

    private static byte[] Edited(byte[] source, Action<PackfileImage, PackfileSection> patch)
    {
        var image = PackfileImage.Read(source);
        patch(image, image.Section("__data__")!);
        return image.Rebuild();
    }

    private static void RemoveFixup(PackfileImage image, PackfileSection data, int source)
    {
        int section = image.Sections.IndexOf(data);
        if (data.Locals().Any(entry => entry.Source == source)) data.SetLocal(source, -1);
        if (data.Globals().Any(entry => entry.Source == source)) data.SetGlobal(source, section, -1);
    }

    private static List<(int Source, int Destination)> UntouchedFixups(byte[] bytes, int header)
    {
        var data = PackfileImage.Read(bytes).Section("__data__")!;
        return data.Locals().Where(entry => entry.Source != header)
                   .OrderBy(entry => entry.Source).ThenBy(entry => entry.Destination).ToList();
    }

    private static List<SkeletonMapperNode> Nodes(byte[] bytes)
    {
        var objects = new PackfileObjects(PackfileImage.Read(bytes));
        var nodes = new List<SkeletonMapperNode>();
        foreach (var instance in objects.Instances)
        {
            if (instance.ClassName != "hkaSkeletonMapper") continue;
            if (SkeletonMapperReader.Read(objects, instance) is not { } read) continue;
            nodes.Add(new SkeletonMapperNode(instance, objects.FieldAt(instance, "mapping")!.Value, read));
        }

        return nodes;
    }

    private static byte[] Data(byte[] bytes) => PackfileImage.Read(bytes).Section("__data__")!.Data;

    private static int RunAt(byte[] bytes, SkeletonMapperNode node)
    {
        var objects = new PackfileObjects(PackfileImage.Read(bytes));
        return objects.ArrayAt(SimpleHeader(node), SimpleStride)!.At;
    }

    private static int SimpleHeader(SkeletonMapperNode node) =>
        node.DataAt + HavokClasses.Shipped.Field("hkaSkeletonMapperData", "simpleMappings")!.Offset;

    private static int SimpleStride =>
        HavokClassTypes.Shipped["hkaSkeletonMapperDataSimpleMapping"]!.Size!.Value;

    private static int FreeBoneA(SkeletonMapperNode node)
    {
        var used = node.Rows.Select(row => row.BoneA).ToHashSet();
        for (int a = 0; a < node.SkeletonA.BoneNames.Count; a++)
            if (!used.Contains(a)) return a;

        throw new InvalidOperationException("every bone on the A side is already mapped");
    }

    private static string FixturePath() => Path.Combine(
        FixturesRoot, "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");

    private static string FixturesRoot => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla");
}
