using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using OpenCommonwealth.Services;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AuditRegressionTests
{
    [Fact]
    public void GrowingEmptyArrayPreservesCapacityFlagsAndMarksEmbeddedStorage()
    {
        var image = NewPackfile();
        var graphData = NativeAppend.Object(image, "hkbBehaviorGraphData");
        var field = HavokClasses.Shipped.Field("hkbBehaviorGraphData", "variableBounds")!;
        var data = image.Section("__data__")!;
        int header = graphData.Offset + field.Offset;
        BitConverter.GetBytes(0x40000000u).CopyTo(data.Data, header + 12);
        FixupOrder.Reorder(image);
        byte[] source = image.Rebuild();

        var plan = new NativeSave.Plan(
            new List<NativeSave.Change>
            {
                new("hkbBehaviorGraphData", 0, "variableBounds", "1",
                    Element: 0, Grow: true, Id: graphData.Id),
            },
            null);

        byte[] rebuilt = NativeSave.Apply(source, plan);
        var rebuiltImage = PackfileImage.Read(rebuilt);
        var objects = new PackfileObjects(rebuiltImage);
        var instance = objects.Instances.Single(i => i.ClassName == "hkbBehaviorGraphData");
        int at = objects.FieldAt(instance, "variableBounds")!.Value;
        var array = objects.ArrayAt(at);
        uint capacity = BitConverter.ToUInt32(rebuiltImage.Section("__data__")!.Data, at + 12);

        Assert.NotNull(array);
        Assert.Equal(1, array!.Count);
        Assert.Equal(1u, capacity & 0x3fffffffu);
        Assert.NotEqual(0u, capacity & 0x80000000u);
        Assert.NotEqual(0u, capacity & 0x40000000u);
    }

    [Fact]
    public void VerifierAcceptsDeleteAndAddInTheSameSave()
    {
        var image = NewPackfile();
        var first = NativeAppend.Object(image, "hkbClipGenerator");
        NativeAppend.Object(image, "hkbClipGenerator");
        FixupOrder.Reorder(image);
        byte[] source = image.Rebuild();
        int addedId = NativeGraphModel.FirstId + 2;

        var plan = new NativeSave.Plan(
            new List<NativeSave.Change>
            {
                new("hkbClipGenerator", 2, "", "#" + addedId,
                    Added: true, Id: addedId),
            },
            null,
            new List<int> { first.Id });

        byte[] rebuilt = NativeSave.Apply(source, plan);
        Exception? error = Record.Exception(() => SaveVerifier.Verify(source, rebuilt, plan));

        Assert.Null(error);
        Assert.Equal(2, new PackfileObjects(PackfileImage.Read(rebuilt)).Instances.Count);
    }

    [Fact]
    public void LargeButEntirelyValidArrayIsStillReadable()
    {
        const int count = 150_000;
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        data.Data = new byte[count + 32];
        data.SetLocal(0, 16);
        BitConverter.GetBytes(count).CopyTo(data.Data, 8);
        var objects = new PackfileObjects(image);

        var array = objects.ArrayAt(0, 1);
        var values = objects.ReadValueArrayAt(0, 1, (bytes, at) => bytes[at]);

        Assert.NotNull(array);
        Assert.Equal(count, array!.Count);
        Assert.NotNull(values);
        Assert.Equal(count, values!.Count);
    }

    [Fact]
    public void LargeButEntirelyValidByteArrayStillRendersAsValues()
    {
        const int count = 150_000;
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        data.Data = new byte[count + 32];
        data.SetLocal(0, 16);
        BitConverter.GetBytes(count).CopyTo(data.Data, 8);
        var objects = new PackfileObjects(image);
        var member = new HavokClassTypes.Member { Name = "data", VSub = "TYPE_UINT8" };

        int rendered = NativeGraphModel.Elements(
            objects, HavokClassTypes.Shipped, 0, member, (_, _) => "null").Count();

        Assert.Equal(count, rendered);
    }

    [Fact]
    public void MalformedArrayBoundsReturnNullInsteadOfThrowing()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        data.Data = new byte[64];
        data.SetLocal(0, 60);
        BitConverter.GetBytes(20).CopyTo(data.Data, 8);
        var objects = new PackfileObjects(image);

        Assert.Null(objects.ArrayAt(0));
        Assert.Null(objects.ReadRefArrayAt(0));
        Assert.Null(objects.ReadStringArrayAt(0));
        Assert.Null(objects.ReadValueArrayAt(0, 1, (bytes, at) => bytes[at]));
    }

    [Fact]
    public void ArrayReaderChecksElementWidthBeforeAllocating()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        data.Data = new byte[64];
        data.SetLocal(0, 40);
        BitConverter.GetBytes(4).CopyTo(data.Data, 8);
        var objects = new PackfileObjects(image);

        Assert.Null(objects.ReadRefArrayAt(0));
    }

    [Fact]
    public void FolderExtractionRejectsParentTraversalOnEveryPlatform()
    {
        using var root = new TestArchives.TempDirectory("bgs-ba2-parent");
        string archive = Path.Combine(root.Path, "parent.ba2");
        string output = Path.Combine(root.Path, "out");
        TestArchives.WriteBa2(archive, "../outside/escape.hkx", new byte[] { 1, 2, 3 });

        Assert.Throws<InvalidDataException>(() =>
            Ba2.ExtractMatching(archive, "", output, ".hkx", _ => { }, keepFolders: true));
        Assert.False(File.Exists(Path.Combine(root.Path, "outside", "escape.hkx")));
    }

    [Fact]
    public void DriveQualifiedArchiveNamesAreRejectedOnEveryPlatform()
    {
        using var root = new TestArchives.TempDirectory("bgs-ba2-drive");
        string archive = Path.Combine(root.Path, "drive.ba2");
        TestArchives.WriteBa2(archive, "C:\\escape.hkx", new byte[] { 1, 2, 3 });

        var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(archive));
        Assert.Contains("rooted archive path", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ArchiveNamesWithColonAreRejectedBeforeExtraction()
    {
        using var root = new TestArchives.TempDirectory("bgs-ba2-colon");
        string archive = Path.Combine(root.Path, "colon.ba2");
        TestArchives.WriteBa2(archive, "a.hkx:stream", new byte[] { 1, 2, 3 });

        var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(archive));
        Assert.Contains("path with a colon", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void FlatExtractionKeepsNestedNamesInsideTheOutputDirectory()
    {
        using var root = new TestArchives.TempDirectory("bgs-ba2-flat");
        string archive = Path.Combine(root.Path, "flat.ba2");
        string output = Path.Combine(root.Path, "out");
        TestArchives.WriteBa2(archive, "folder/sub/file.hkx", new byte[] { 1, 2, 3 });

        int written = Ba2.ExtractMatching(
            archive, "", output, ".hkx", _ => { }, keepFolders: false);

        Assert.Equal(1, written);
        Assert.True(File.Exists(Path.Combine(output, "folder_sub_file.hkx")));
        Assert.Equal("a.hkx_stream", Ba2.FlatFileName("a.hkx:stream"));
    }

    [Fact]
    public void CheckedReplacementKeepsThePreviousVersionAsBackup()
    {
        using var root = new TestArchives.TempDirectory("bgs-checked-replace");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] original = { 1, 2, 3, 4 };
        byte[] replacement = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, original);

        FileSafety.ReplaceChecked(path, replacement, DocumentSourceStamp.Capture(path));

        Assert.Equal(replacement, File.ReadAllBytes(path));
        Assert.Equal(original, File.ReadAllBytes(path + ".bak"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.previous"));
    }

    [Fact]
    public void CheckedReplacementRestoresAnInterveningExternalEdit()
    {
        using var root = new TestArchives.TempDirectory("bgs-checked-race");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] original = { 1, 2, 3, 4 };
        byte[] replacement = { 5, 6, 7, 8 };
        byte[] external = { 9, 10, 11, 12 };
        File.WriteAllBytes(path, original);
        var stamp = DocumentSourceStamp.Capture(path);

        var error = Assert.Throws<IOException>(() =>
            FileSafety.ReplaceChecked(
                path,
                replacement,
                stamp,
                beforeReplace: () => File.WriteAllBytes(path, external)));

        Assert.Contains("changed on disk", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(external, File.ReadAllBytes(path));
        Assert.False(File.Exists(path + ".bak"));
        Assert.Empty(Directory.EnumerateFiles(root.Path, "*.previous"));
    }

    [Fact]
    public void ReadHkxDoesNotTouchSaveSidecars()
    {
        using var root = new TestArchives.TempDirectory("bgs-read-sidecars");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] current = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, current);
        string token = DocumentSourceStamp.Capture(current).Token;
        string previous = path + "." + token + "." + Id("read") + ".previous";
        string writing = path + "." + Id("read") + ".writing";
        File.WriteAllBytes(previous, new byte[] { 1, 2, 3, 4 });
        File.WriteAllBytes(writing, new byte[] { 9, 9, 9, 9 });
        MakeStale(previous);
        MakeStale(writing);

        byte[] read = InputFilePolicy.ReadHkx(path);

        Assert.Equal(current, read);
        Assert.True(File.Exists(previous));
        Assert.True(File.Exists(writing));
        Assert.False(File.Exists(path + ".bak"));
    }

    [Fact]
    public void RecoveryRotatesOnceAndPromotesOnlyTheNewestMatchingStaleSidecar()
    {
        using var root = new TestArchives.TempDirectory("bgs-recovery-chain");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] current = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, current);
        File.WriteAllBytes(path + ".bak", new byte[] { 1 });
        File.WriteAllBytes(path + ".bak.1", new byte[] { 2 });
        File.WriteAllBytes(path + ".bak.2", new byte[] { 3 });

        string token = DocumentSourceStamp.Capture(current).Token;
        string newest = path + "." + token + "." + Id("new") + ".previous";
        string older = path + "." + token + "." + Id("old") + ".previous";
        string unrelated = path + "." + new string('0', 64) + "." + Id("other") + ".previous";
        File.WriteAllBytes(newest, new byte[] { 11 });
        File.WriteAllBytes(older, new byte[] { 12 });
        File.WriteAllBytes(unrelated, new byte[] { 13 });
        MakeStale(newest, minutes: 20);
        MakeStale(older, minutes: 30);
        MakeStale(unrelated, minutes: 40);

        FileSafety.RecoverInterrupted(path);

        Assert.Equal(new byte[] { 11 }, File.ReadAllBytes(path + ".bak"));
        Assert.Equal(new byte[] { 1 }, File.ReadAllBytes(path + ".bak.1"));
        Assert.Equal(new byte[] { 2 }, File.ReadAllBytes(path + ".bak.2"));
        Assert.False(File.Exists(newest));
        Assert.Equal(new byte[] { 12 }, File.ReadAllBytes(older));
        Assert.Equal(new byte[] { 13 }, File.ReadAllBytes(unrelated));
    }

    [Fact]
    public void RecoverySweepsOnlyStaleWritingArtifacts()
    {
        using var root = new TestArchives.TempDirectory("bgs-recovery-writing");
        string path = Path.Combine(root.Path, "graph.hkx");
        File.WriteAllBytes(path, new byte[] { 5, 6, 7, 8 });
        string stale = path + "." + Id("stale") + ".writing";
        string fresh = path + "." + Id("fresh") + ".writing";
        File.WriteAllBytes(stale, new byte[] { 1 });
        File.WriteAllBytes(fresh, new byte[] { 2 });
        MakeStale(stale);

        FileSafety.RecoverInterrupted(path);

        Assert.False(File.Exists(stale));
        Assert.True(File.Exists(fresh));
    }

    [Fact]
    public void RecoveryLeavesFreshPreviousArtifactsAlone()
    {
        using var root = new TestArchives.TempDirectory("bgs-recovery-fresh");
        string path = Path.Combine(root.Path, "graph.hkx");
        byte[] current = { 5, 6, 7, 8 };
        File.WriteAllBytes(path, current);
        string token = DocumentSourceStamp.Capture(current).Token;
        string previous = path + "." + token + "." + Id("fresh") + ".previous";
        File.WriteAllBytes(previous, new byte[] { 1, 2, 3, 4 });

        FileSafety.RecoverInterrupted(path);

        Assert.True(File.Exists(previous));
        Assert.False(File.Exists(path + ".bak"));
    }

    private static string Id(string seed) => seed switch
    {
        "read" => new string('1', 32),
        "new" => new string('2', 32),
        "old" => new string('3', 32),
        "other" => new string('4', 32),
        "stale" => new string('5', 32),
        _ => new string('6', 32),
    };

    [Fact]
    public void RelayoutRemapsAnotherSectionsPointerIntoTheMovedSurvivor()
    {
        var image = NewPackfile();
        var types = image.Section("__types__")!;
        types.Data = new byte[16];
        var removed = NativeAppend.Object(image, "hkbClipGenerator");
        var survivor = NativeAppend.Object(image, "hkbClipGenerator");
        types.SetGlobal(0, image.Sections.IndexOf(image.Section("__data__")!), survivor.Offset);

        NativeRemove.Delete(image, new[] { removed.Id });

        var rebuilt = PackfileImage.Read(image.Rebuild());
        var moved = new PackfileObjects(rebuilt).Instances.Single();
        Assert.NotEqual(survivor.Offset, moved.Offset);
        Assert.Equal(moved.Offset, rebuilt.Section("__types__")!.Globals().Single().Destination);
    }

    [Fact]
    public void DeleteTakesOneNonRootObjectOutAndTheFileStillReads()
    {
        var image = NewPackfile();
        var first = NativeAppend.Object(image, "hkbClipGenerator");
        var root = NativeAppend.Object(image, "hkbClipGenerator");
        image.ContentsSectionIndex = image.Sections.IndexOf(image.Section("__data__")!);
        image.ContentsSectionOffset = root.Offset;

        var gone = NativeRemove.Delete(image, new[] { first.Id });

        Assert.Equal(1, gone.Objects);
        Assert.Single(new PackfileObjects(image).Instances);
        var rebuilt = PackfileImage.Read(image.Rebuild());
        Assert.NotNull(rebuilt.Section("__data__"));
        Assert.Equal(new PackfileObjects(rebuilt).Instances.Single().Offset, rebuilt.ContentsSectionOffset);
    }

    [Fact]
    public void DeleteRefusesAnObjectThatAnotherObjectInTheSameSectionStillPointsAt()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        var holder = NativeAppend.Object(image, "hkbClipGenerator");
        var target = NativeAppend.Object(image, "hkbClipGenerator");
        data.SetGlobal(FieldAt(holder, "hkbClipGenerator", "variableBindingSet"),
                       image.Sections.IndexOf(data), target.Offset);

        var error = Assert.Throws<InvalidOperationException>(() =>
            NativeRemove.Delete(image, new[] { target.Id }));

        Assert.Contains("still points at", error.Message, StringComparison.Ordinal);
        Assert.Contains("Detach", error.Message, StringComparison.Ordinal);
        Assert.Equal(2, new PackfileObjects(image).Instances.Count);
    }

    [Fact]
    public void DeleteRefusesToRemoveTheFilesRootObjectAndLeavesTheFileAsItWas()
    {
        var image = NewPackfile();
        var root = NativeAppend.Object(image, "hkbClipGenerator");
        NativeAppend.Object(image, "hkbClipGenerator");
        image.ContentsSectionIndex = image.Sections.IndexOf(image.Section("__data__")!);
        image.ContentsSectionOffset = root.Offset;
        byte[] before = image.Rebuild();

        var error = Assert.Throws<InvalidOperationException>(() =>
            NativeRemove.Delete(image, new[] { root.Id }));

        Assert.Contains("root object", error.Message, StringComparison.Ordinal);
        Assert.Equal(before, image.Rebuild());
    }

    [Fact]
    public void DeleteRefusesWhileAnotherSectionStillPointsAtTheObject()
    {
        var image = NewPackfile();
        var types = image.Section("__types__")!;
        types.Data = new byte[16];
        var target = NativeAppend.Object(image, "hkbClipGenerator");
        types.SetGlobal(0, image.Sections.IndexOf(image.Section("__data__")!), target.Offset);

        var error = Assert.Throws<InvalidOperationException>(() =>
            NativeRemove.Delete(image, new[] { target.Id }));

        Assert.Contains("__types__", error.Message);
    }

    [Fact]
    public void DeleteRefusesToRemoveTheOwnerOfAStringASurvivorShares()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        var owner = NativeAppend.Object(image, "hkbClipGenerator");
        var sharer = NativeAppend.Object(image, "hkbClipGenerator");
        int text = data.AppendData(Encoding.ASCII.GetBytes("walk\0"));
        data.SetLocal(FieldAt(owner, "hkbClipGenerator", "animationBundleName"), text);
        data.SetLocal(FieldAt(sharer, "hkbClipGenerator", "animationBundleName"), text);

        var error = Assert.Throws<InvalidOperationException>(() =>
            NativeRemove.Delete(image, new[] { owner.Id }));
        Assert.Contains("shared", error.Message);

        NativeRemove.Delete(image, new[] { sharer.Id });
        var objects = new PackfileObjects(PackfileImage.Read(image.Rebuild()));
        Assert.Equal("walk", objects.ReadString(objects.Instances.Single(), "animationBundleName"));
    }

    [Fact]
    public void DeleteRefusesToRemoveTheOwnerOfAnArraySurvivorShares()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        var owner = NativeAppend.Object(image, "hclClothContainer");
        var sharer = NativeAppend.Object(image, "hclClothContainer");
        int payload = data.AppendData(new byte[16]);
        SetArray(data, FieldAt(owner, "hclClothContainer", "collidables"), payload, 2);
        SetArray(data, FieldAt(sharer, "hclClothContainer", "collidables"), payload, 2);

        Assert.Throws<InvalidOperationException>(() => NativeRemove.Delete(image, new[] { owner.Id }));

        NativeRemove.Delete(image, new[] { sharer.Id });
        var objects = new PackfileObjects(PackfileImage.Read(image.Rebuild()));
        Assert.Equal(2, objects.ReadArray(objects.Instances.Single(), "collidables")!.Count);
    }

    [Fact]
    public void RelayoutMovesTheContentsObjectWithItsHeaderOffset()
    {
        var image = RootedClips(out var first, out _);

        NativeRemove.Delete(image, new[] { first.Id });

        var rebuilt = PackfileImage.Read(image.Rebuild());
        Assert.Equal(new PackfileObjects(rebuilt).Instances.Single().Offset, rebuilt.ContentsSectionOffset);
    }

    [Fact]
    public void DeleteRefusesToRemoveTheContentsObject()
    {
        var image = RootedClips(out _, out var root);

        var error = Assert.Throws<InvalidOperationException>(() =>
            NativeRemove.Delete(image, new[] { root.Id }));

        Assert.Contains("root", error.Message);
    }

    [Fact]
    public void DeleteRelocatesASurvivingRelativeArrayPayloadWithItsHeader()
    {
        var file = new CollisionPackfile();
        file.AddObject("hknpSphereShape");
        int polytope = file.AddObject("hknpConvexPolytopeShape");
        file.RelArray(polytope + CollisionPackfile.Member("hknpConvexPolytopeShape", "vertices"),
                      CollisionPackfile.Floats(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0), 3);
        file.RelArray(polytope + CollisionPackfile.Member("hknpConvexPolytopeShape", "indices"),
                      new byte[] { 7, 8, 9 }, 3);
        var image = PackfileImage.Read(file.Finish());
        image.ContentsSectionOffset = new PackfileObjects(image).Instances[1].Offset;

        NativeRemove.Delete(image, new[] { NativeGraphModel.FirstId });

        var rebuilt = PackfileImage.Read(image.Rebuild());
        var objects = new PackfileObjects(rebuilt);
        var shape = objects.Instances.Single();
        var data = rebuilt.Section("__data__")!.Data;
        var indices = objects.RelArrayAt(objects.FieldAt(shape, "indices")!.Value, 1)!;
        var vertices = objects.RelArrayAt(objects.FieldAt(shape, "vertices")!.Value, 16)!;
        Assert.Equal(shape.Offset, rebuilt.ContentsSectionOffset);
        Assert.Equal(3, vertices.Count);
        Assert.Equal(new byte[] { 7, 8, 9 }, data[indices.At..(indices.At + 3)]);
    }

    [Fact]
    public void OrphanShrinkKeepsEachSurvivingElementInItsOwnSection()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        var types = image.Section("__types__")!;
        types.Data = new byte[32];
        int dataIndex = image.Sections.IndexOf(data);
        int typesIndex = image.Sections.IndexOf(types);
        var gone = NativeAppend.Object(image, "hkbClipGenerator");
        var kept = NativeAppend.Object(image, "hkbClipGenerator");
        var holder = NativeAppend.Object(image, "hclClothContainer");
        int payload = data.AppendData(new byte[24]);
        SetArray(data, FieldAt(holder, "hclClothContainer", "collidables"), payload, 3);
        data.SetGlobal(payload, dataIndex, gone.Offset);
        data.SetGlobal(payload + 8, dataIndex, kept.Offset);
        data.SetGlobal(payload + 16, typesIndex, 16);

        NativeRemove.Orphan(image, gone.Id);
        NativeRemove.Delete(image, new[] { gone.Id });

        var rebuilt = PackfileImage.Read(image.Rebuild());
        var objects = new PackfileObjects(rebuilt);
        var array = objects.ReadArray(objects.Instances.Single(i => i.ClassName == "hclClothContainer"),
                                      "collidables")!;
        var pointers = rebuilt.Section("__data__")!.Globals().ToList();
        Assert.Equal(2, array.Count);
        Assert.Contains((array.At, dataIndex, objects.Instances.Single(i => i.ClassName == "hkbClipGenerator").Offset),
                        pointers);
        Assert.Contains((array.At + 8, typesIndex, 16), pointers);
    }

    [Fact]
    public void OrphanAndDeleteRefuseAFourByteLayoutAndLeaveTheImageUnchanged()
    {
        var image = NewPackfile();
        var data = image.Section("__data__")!;
        var target = NativeAppend.Object(image, "hkbClipGenerator");
        var holder = NativeAppend.Object(image, "hclClothContainer");
        int payload = data.AppendData(new byte[16]);
        SetArray(data, FieldAt(holder, "hclClothContainer", "collidables"), payload, 2);
        data.SetGlobal(payload, image.Sections.IndexOf(data), target.Offset);
        image.LayoutRules = new byte[] { 4, 1, 0, 1 };
        var before = data.Data.ToArray();
        var globalsBefore = data.Globals().ToList();

        var error = Assert.Throws<NotSupportedException>(() => NativeRemove.Orphan(image, target.Id));

        Assert.Contains("8-byte", error.Message);
        var deleteError = Assert.Throws<NotSupportedException>(() => NativeRemove.Delete(image, new[] { target.Id }));
        Assert.Contains("8-byte", deleteError.Message);
        Assert.Equal(before, data.Data);
        Assert.Equal(globalsBefore, data.Globals().ToList());
    }

    private static int FieldAt(NativeAppend.Added obj, string className, string member) =>
        obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset;

    private static void SetArray(PackfileSection data, int header, int payload, int count)
    {
        data.SetLocal(header, payload);
        BitConverter.GetBytes(count).CopyTo(data.Data, header + 8);
        BitConverter.GetBytes(count).CopyTo(data.Data, header + 12);
    }

    private static PackfileImage RootedClips(out NativeAppend.Added first, out NativeAppend.Added root)
    {
        var image = NewPackfile();
        image.ContentsSectionIndex = image.Sections.IndexOf(image.Section("__data__")!);
        first = NativeAppend.Object(image, "hkbClipGenerator");
        root = NativeAppend.Object(image, "hkbClipGenerator");
        image.ContentsSectionOffset = root.Offset;
        return image;
    }

    private static PackfileImage NewPackfile()
    {
        var image = new PackfileImage { Predicates = new byte[16] };
        image.Sections.Add(new PackfileSection { TagBytes = Tag("__classnames__") });
        image.Sections.Add(new PackfileSection { TagBytes = Tag("__types__") });
        image.Sections.Add(new PackfileSection { TagBytes = Tag("__data__") });
        return image;
    }

    private static byte[] Tag(string name)
    {
        var bytes = new byte[20];
        Encoding.ASCII.GetBytes(name).CopyTo(bytes, 0);
        return bytes;
    }

    private static void MakeStale(string path, int minutes = 20) =>
        File.SetLastWriteTimeUtc(path, DateTime.UtcNow - TimeSpan.FromMinutes(minutes));

    [Fact]
    public void AParentCycleIsRefusedInsteadOfRecursingForever()
    {
        // A custom table whose parents form a loop. CanPlace has to notice the cycle before
        // deriving alignment: that derivation walks parents itself (NaturalAlign -> Of) and Of
        // does not cache until it returns, so entering it partway along the loop never comes back.
        const string json = """
        {
          "classes": {
            "Loopy": { "parent": "Loopier", "signature": "0x11111111", "size": 16, "members": [] },
            "Loopier": { "parent": "Loopy", "signature": "0x22222222", "size": 16, "members": [] }
          }
        }
        """;
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        var types = HavokClassTypes.Parse(stream);

        Assert.False(LayoutWalker.CanPlace(types, "Loopy"));
        Assert.False(LayoutWalker.CanPlace(types, "Loopier"));
        Assert.Null(LayoutWalker.Active(types, "Loopy", 4));
    }
}
