using System;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Xml.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxSkeletonReadTests
{
    private static string VanillaFixture => Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla",
        "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");

    [Fact]
    public void TheVanillaFixtureReadsEverySkeletonMember()
    {
        var skeleton = new HkxBinaryReader().ReadSkeleton(VanillaFixture);

        Assert.Equal("Root", skeleton.Name);
        Assert.Equal(95, skeleton.BoneNames.Count);
        Assert.Equal(new[] { "Root", "COM", "Pelvis", "LLeg_Thigh", "LLeg_Calf", "LLeg_Foot" },
            skeleton.BoneNames.Take(6));
        Assert.Equal("CamTarget", skeleton.BoneNames[^1]);

        Assert.Equal(95, skeleton.ParentIndices.Count);
        Assert.Equal(new[] { -1, 0, 1, 2, 3, 4 }, skeleton.ParentIndices.Take(6));
        Assert.Equal(95, skeleton.ReferencePose.Count);
        Assert.Equal(Vector3.Zero, skeleton.ReferencePose[0].Translation);
        Assert.Equal(1f, skeleton.ReferencePose[0].Rotation.W, 5);

        Assert.Equal(95, skeleton.LockTranslation.Count);
        Assert.Equal(93, skeleton.LockTranslation.Count(locked => locked));
        Assert.False(skeleton.LockTranslation[0]);
        Assert.True(skeleton.LockTranslation[2]);

        Assert.Empty(skeleton.ReferenceFloats);
        Assert.Empty(skeleton.FloatSlots);
        Assert.Empty(skeleton.LocalFrames);
        Assert.Empty(skeleton.Partitions);
    }

    [Fact]
    public void ASkeletonWithoutThoseArraysReadsThemAsEmpty()
    {
        var crafted = new CraftedSkeleton();
        var skeleton = crafted.Object("hkaSkeleton");
        int root = crafted.Text("Root");
        int bones = crafted.Block(new byte[16]);
        crafted.Point(bones, root);
        crafted.Element("hkaSkeleton", "bones", skeleton, bones, 1);

        var read = new HkxBinaryReader().ReadSkeleton(crafted.Build());

        Assert.Equal(new[] { "Root" }, read.BoneNames);
        Assert.Single(read.LockTranslation);
        Assert.False(read.LockTranslation[0]);
        Assert.Empty(read.ParentIndices);
        Assert.Empty(read.ReferencePose);
        Assert.Empty(read.ReferenceFloats);
        Assert.Empty(read.FloatSlots);
        Assert.Empty(read.LocalFrames);
        Assert.Empty(read.Partitions);
    }

    [Fact]
    public void FloatSlotsReferenceFloatsPartitionsAndLocalFramesAllReadBack()
    {
        var skeleton = new HkxBinaryReader().ReadSkeleton(CraftedSkeletonFile());

        Assert.Equal("Crafted ragdoll", skeleton.Name);
        Assert.Equal(new[] { "Root", "Child" }, skeleton.BoneNames);
        Assert.Equal(new[] { -1, 0 }, skeleton.ParentIndices);
        Assert.Equal(new[] { true, false }, skeleton.LockTranslation);
        Assert.Equal(2, skeleton.ReferencePose.Count);
        Assert.Equal(new Vector3(1f, 2f, 3f), skeleton.ReferencePose[0].Translation);
        Assert.Equal(new Vector3(4f, 5f, 6f), skeleton.ReferencePose[1].Translation);
        Assert.Equal(new Vector3(1f, 1f, 1f), skeleton.ReferencePose[1].Scale);

        Assert.Equal(new[] { 0.25f, 0.5f }, skeleton.ReferenceFloats);
        Assert.Equal(new[] { "UpperArm", "LowerArm" }, skeleton.FloatSlots);

        Assert.Equal(2, skeleton.LocalFrames.Count);
        Assert.Equal(1, skeleton.LocalFrames[0].BoneIndex);
        Assert.True(skeleton.LocalFrames[0].HasFrame);
        Assert.True(skeleton.LocalFrames[0].FrameOffset > 0);
        Assert.Equal(0, skeleton.LocalFrames[1].BoneIndex);
        Assert.False(skeleton.LocalFrames[1].HasFrame);
        Assert.Equal(-1, skeleton.LocalFrames[1].FrameOffset);

        Assert.Equal(2, skeleton.Partitions.Count);
        Assert.Equal("Upper", skeleton.Partitions[0].Name);
        Assert.Equal(0, skeleton.Partitions[0].StartBoneIndex);
        Assert.Equal(1, skeleton.Partitions[0].NumBones);
        Assert.Equal("Lower", skeleton.Partitions[1].Name);
        Assert.Equal(1, skeleton.Partitions[1].StartBoneIndex);
        Assert.Equal(1, skeleton.Partitions[1].NumBones);
    }

    [Fact]
    public void TheSkeletonJsonRoundTripKeepsTheNewMembersAndStillReadsOlderDocuments()
    {
        var skeleton = new HkxBinaryReader().ReadSkeleton(CraftedSkeletonFile());
        string json = SkeletonJson.Write(skeleton, "crafted.hkx");
        var back = SkeletonJson.Read(json);

        Assert.Equal(skeleton.BoneNames, back.BoneNames);
        Assert.Equal(skeleton.ParentIndices, back.ParentIndices);
        Assert.Equal(skeleton.LockTranslation, back.LockTranslation);
        Assert.Equal(skeleton.ReferenceFloats, back.ReferenceFloats);
        Assert.Equal(skeleton.FloatSlots, back.FloatSlots);
        Assert.Equal(
            skeleton.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)),
            back.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)));
        Assert.Equal(
            skeleton.LocalFrames.Select(f => (f.BoneIndex, f.HasFrame, f.FrameOffset)),
            back.LocalFrames.Select(f => (f.BoneIndex, f.HasFrame, f.FrameOffset)));

        const string older = """
            { "format": "fo4-skeleton", "version": 1, "name": "Old",
              "boneCount": 1,
              "bones": [ { "name": "Root", "parent": -1,
                           "translation": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1] } ] }
            """;
        var legacy = SkeletonJson.Read(older);
        Assert.Equal(new[] { "Root" }, legacy.BoneNames);
        Assert.Empty(legacy.FloatSlots);
        Assert.Empty(legacy.ReferenceFloats);
        Assert.Empty(legacy.LocalFrames);
        Assert.Empty(legacy.Partitions);
    }

    [Fact]
    public void AnUneditedVanillaSkeletonLoadsAndSavesByteIdentical()
    {
        byte[] source = File.ReadAllBytes(VanillaFixture);
        string path = Path.Combine(Path.GetTempPath(), $"bgs-661-vanilla-{Guid.NewGuid():N}.hkx");
        File.WriteAllBytes(path, source);
        try
        {
            var document = new SkeletonDocument(path);
            Assert.Equal(2, document.Skeletons.Count);
            Assert.All(document.Skeletons, item => Assert.Empty(item.Partitions));
            Assert.All(document.Skeletons, item => Assert.Empty(item.LocalFrames));
            Assert.False(document.Dirty);

            document.Save();

            Assert.Equal(source, File.ReadAllBytes(path));
        }
        finally { File.Delete(path); File.Delete(path + ".bak"); }
    }

    [Fact]
    public void EditingAPoseKeepsPartitionsLocalFramesFloatSlotsAndReferenceFloats()
    {
        byte[] source = CraftedSkeletonFile();
        string path = Path.Combine(Path.GetTempPath(), $"bgs-661-keeps-{Guid.NewGuid():N}.hkx");
        File.WriteAllBytes(path, source);
        try
        {
            var before = new HkxBinaryReader().ReadSkeleton(source);

            var document = new SkeletonDocument(path);
            var exposed = document.Skeletons[0];
            Assert.Equal(before.FloatSlots, exposed.FloatSlots);
            Assert.Equal(before.ReferenceFloats, exposed.ReferenceFloats);
            Assert.Equal(before.LockTranslation, exposed.LockTranslation);
            Assert.Equal(
                before.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)),
                exposed.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)));
            Assert.Equal(before.LocalFrames.Select(f => f.BoneIndex),
                         exposed.LocalFrames.Select(f => f.BoneIndex));
            Assert.True(exposed.LocalFrames[0].HasFrame);
            Assert.False(exposed.LocalFrames[1].HasFrame);

            document.Edit(0, edit =>
            {
                HkxBonePose pose = edit.PoseOf(1);
                pose.Translation = new Vector3(9.5f, 8.5f, 7.5f);
                edit.SetPose(1, pose);
            });
            Assert.True(document.Dirty);
            document.Save();

            byte[] saved = File.ReadAllBytes(path);
            Assert.NotEqual(source, saved);

            var after = new HkxBinaryReader().ReadSkeleton(saved);
            Assert.Equal(new Vector3(9.5f, 8.5f, 7.5f), after.ReferencePose[1].Translation);
            Assert.Equal(before.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)),
                         after.Partitions.Select(p => (p.Name, p.StartBoneIndex, p.NumBones)));
            Assert.Equal(before.LocalFrames.Select(f => (f.BoneIndex, f.HasFrame, f.FrameOffset)),
                         after.LocalFrames.Select(f => (f.BoneIndex, f.HasFrame, f.FrameOffset)));
            Assert.Equal(before.FloatSlots, after.FloatSlots);
            Assert.Equal(before.ReferenceFloats, after.ReferenceFloats);
            Assert.Equal(before.LockTranslation, after.LockTranslation);
            Assert.Equal(before.ParentIndices, after.ParentIndices);
            Assert.Equal(before.BoneNames, after.BoneNames);

            string xml = NativeXml.From(saved);
            XElement savedSkeleton = XDocument.Parse(xml).Descendants("hkobject")
                .Single(e => (string?)e.Attribute("class") == "hkaSkeleton");
            Assert.Equal("2", (string?)savedSkeleton.Elements("hkparam")
                .Single(e => (string?)e.Attribute("name") == "partitions")!.Attribute("numelements"));
            Assert.Equal("2", (string?)savedSkeleton.Elements("hkparam")
                .Single(e => (string?)e.Attribute("name") == "localFrames")!.Attribute("numelements"));

            var reopened = new SkeletonDocument(path);
            Assert.False(reopened.Dirty);
            reopened.Save();
            Assert.Equal(saved, File.ReadAllBytes(path));
        }
        finally { File.Delete(path); File.Delete(path + ".bak"); }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ANonEmptyPartitionArrayStopsAStandaloneSkeletonFromChangingBoneCount(bool withPartitions)
    {
        string path = Path.Combine(Path.GetTempPath(), $"bgs-661-standalone-{Guid.NewGuid():N}.hkx");
        var image = new PackfileImage { Predicates = new byte[16] };
        foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
        {
            var tag = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
            image.Sections.Add(new PackfileSection { TagBytes = tag });
        }
        NativeAppend.Object(image, "hkaSkeleton");
        byte[] seed = image.Rebuild();
        string before = NativeXml.From(seed);
        var xml = XDocument.Parse(before);
        XElement skeleton = xml.Descendants("hkobject")
            .Single(e => (string?)e.Attribute("class") == "hkaSkeleton");
        XElement Field(string name) => skeleton.Elements("hkparam")
            .Single(e => (string?)e.Attribute("name") == name);

        Field("name").Value = "Standalone";
        Field("bones").SetAttributeValue("numelements", 1);
        Field("bones").Add(new XElement("hkobject",
            new XElement("hkparam", new XAttribute("name", "name"), "Root"),
            new XElement("hkparam", new XAttribute("name", "lockTranslation"), "true")));
        Field("parentIndices").SetAttributeValue("numelements", 1);
        Field("parentIndices").Value = "-1";
        Field("referencePose").SetAttributeValue("numelements", 1);
        Field("referencePose").Value = "(0 0 0 0) (0 0 0 1) (1 1 1 0)";
        if (withPartitions)
        {
            Field("partitions").SetAttributeValue("numelements", 1);
            Field("partitions").Add(new XElement("hkobject",
                new XElement("hkparam", new XAttribute("name", "name"), "All"),
                new XElement("hkparam", new XAttribute("name", "startBoneIndex"), "0"),
                new XElement("hkparam", new XAttribute("name", "numBones"), "1")));
        }

        var plan = NativeSave.Compare(before, xml.ToString());
        Assert.True(plan.Possible, plan.Refusal);
        File.WriteAllBytes(path, NativeSave.Apply(seed, plan));
        try
        {
            var document = new SkeletonDocument(path);
            if (!withPartitions)
            {
                Assert.Empty(document.Skeletons[0].Partitions);
                Assert.True(document.CanChangeBoneCount);
                return;
            }

            Assert.Equal("All", document.Skeletons[0].Partitions[0].Name);
            Assert.Equal(1, document.Skeletons[0].Partitions[0].NumBones);
            Assert.False(document.CanChangeBoneCount);
            Assert.Throws<NotSupportedException>(() => document.Edit(0, edit => edit.AddBone("Extra")));
        }
        finally { File.Delete(path); File.Delete(path + ".bak"); }
    }

    [Fact]
    public void ACountThatCannotFitInTheSectionIsRefused()
    {
        Assert.Equal(new[] { 0.25f, 0.5f }, new HkxBinaryReader().ReadSkeleton(CraftedSkeletonFile()).ReferenceFloats);

        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
            crafted.SetCount("hkaSkeleton", "referenceFloats", skeleton, int.MaxValue));

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Fact]
    public void ANegativeArrayCountIsRefused()
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
            crafted.SetCount("hkaSkeleton", "referenceFloats", skeleton, -4));

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Theory]
    [InlineData("referenceFloats")]
    [InlineData("partitions")]
    [InlineData("floatSlots")]
    [InlineData("localFrames")]
    [InlineData("parentIndices")]
    [InlineData("referencePose")]
    public void AnArrayPointerPastTheDataSectionIsRefused(string member)
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
        {
            crafted.ClearLocal("hkaSkeleton", member, skeleton);
            crafted.Retarget("hkaSkeleton", member, skeleton, crafted.DataLength + 64);
        });

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Theory]
    [InlineData("referenceFloats")]
    [InlineData("partitions")]
    [InlineData("parentIndices")]
    public void AnArrayPointerBeforeTheDataSectionIsRefused(string member)
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
        {
            crafted.ClearLocal("hkaSkeleton", member, skeleton);
            crafted.Retarget("hkaSkeleton", member, skeleton, -16);
        });

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Theory]
    [InlineData("referenceFloats")]
    [InlineData("partitions")]
    public void AnArrayPointerPastTheEndOfTheFileIsRefused(string member)
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
        {
            crafted.ClearLocal("hkaSkeleton", member, skeleton);
            crafted.Retarget("hkaSkeleton", member, skeleton, crafted.DataLength + 1_000_000);
        });

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Fact]
    public void ATruncatedSkeletonFileIsRefused()
    {
        byte[] whole = CraftedSkeletonFile();
        Assert.Equal(2, new HkxBinaryReader().ReadSkeleton(whole).ReferenceFloats.Count);

        byte[] cut = whole[..(whole.Length - 512)];

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(cut));
    }

    [Fact]
    public void AGlobalFixupIntoTheDataSectionResolvesASkeletonMember()
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
        {
            int target = crafted.LocalTarget("hkaSkeleton", "referenceFloats", skeleton);
            crafted.ClearLocal("hkaSkeleton", "referenceFloats", skeleton);
            crafted.SetGlobal("hkaSkeleton", "referenceFloats", skeleton, crafted.DataSectionIndex, target);
        });

        var read = new HkxBinaryReader().ReadSkeleton(bytes);

        Assert.Equal(new[] { 0.25f, 0.5f }, read.ReferenceFloats);
    }

    [Fact]
    public void AGlobalFixupIntoAnotherSectionIsRefused()
    {
        byte[] bytes = CraftedSkeletonFile((crafted, skeleton) =>
        {
            int target = crafted.LocalTarget("hkaSkeleton", "referenceFloats", skeleton);
            int types = crafted.SectionIndex("__types__");
            Assert.NotEqual(crafted.DataSectionIndex, types);
            crafted.ClearLocal("hkaSkeleton", "referenceFloats", skeleton);
            crafted.SetGlobal("hkaSkeleton", "referenceFloats", skeleton, types, target);
        });

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadSkeleton(bytes));
    }

    [Fact]
    public void AMissingSlotOrPartitionNameIsKeptApartFromAnEmptyOne()
    {
        var crafted = new CraftedSkeleton();
        int root = crafted.Text("Root");
        int bones = crafted.Block(new byte[16]);
        crafted.Point(bones, root);

        int slots = crafted.Block(new byte[2 * 8]);
        crafted.Point(slots, crafted.Text(""));
        int partitions = crafted.Block(new byte[2 * 16]);
        crafted.Point(partitions, crafted.Text("Upper"));

        var skeleton = crafted.Object("hkaSkeleton");
        crafted.Element("hkaSkeleton", "bones", skeleton, bones, 1);
        crafted.Element("hkaSkeleton", "floatSlots", skeleton, slots, 2);
        crafted.Element("hkaSkeleton", "partitions", skeleton, partitions, 2);

        var read = new HkxBinaryReader().ReadSkeleton(crafted.Build());

        Assert.Equal(new string?[] { "", null }, read.FloatSlots);
        Assert.Equal(new string?[] { "Upper", null }, read.Partitions.Select(p => p.Name));

        var back = SkeletonJson.Read(SkeletonJson.Write(read, "names.hkx"));

        Assert.Equal(new string?[] { "", null }, back.FloatSlots);
        Assert.Equal(new string?[] { "Upper", null }, back.Partitions.Select(p => p.Name));
    }

    [Fact]
    public void ANonFiniteReferenceFloatIsRefusedOnWrite()
    {
        var good = Skeleton("Good");
        good.ReferenceFloats.Add(0.5f);
        Assert.Contains("0.5", SkeletonJson.Write(good, "good.hkx"));

        var bad = Skeleton("Bad");
        bad.ReferenceFloats.Add(float.NaN);

        Assert.Throws<InvalidDataException>(() => SkeletonJson.Write(bad, "bad.hkx"));
    }

    [Fact]
    public void ANonFinitePoseValueIsRefusedOnWrite()
    {
        var skeleton = Skeleton("Bad");

        skeleton.ReferencePose[0] = new HkxBonePose(
            new Vector3(float.PositiveInfinity, 0f, 0f), Quaternion.Identity, Vector3.One);
        Assert.Throws<InvalidDataException>(() => SkeletonJson.Write(skeleton, "bad.hkx"));

        skeleton.ReferencePose[0] = new HkxBonePose(
            Vector3.Zero, new Quaternion(0f, 0f, 0f, float.NaN), Vector3.One);
        Assert.Throws<InvalidDataException>(() => SkeletonJson.Write(skeleton, "bad.hkx"));

        skeleton.ReferencePose[0] = new HkxBonePose(
            Vector3.Zero, Quaternion.Identity, new Vector3(1f, float.NegativeInfinity, 1f));
        Assert.Throws<InvalidDataException>(() => SkeletonJson.Write(skeleton, "bad.hkx"));
    }

    [Theory]
    [InlineData("""{ "format": "fo4-skeleton", "bones": [] }""")]
    [InlineData("""{ "format": 7, "name": "Bad", "bones": [] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": {} }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [ 7 ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [ { "name": "Root", "parent": "zero", "translation": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1] } ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [ { "name": "Root", "parent": -1, "translation": [0,0], "rotation": [0,0,0,1], "scale": [1,1,1] } ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [ { "name": "Root", "parent": -1, "translation": [0,0,0], "rotation": [0,0,1], "scale": [1,1,1] } ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [ { "name": "Root", "parent": -1, "translation": [0,0,0], "rotation": [0,0,0,1], "scale": "one" } ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [], "floatSlots": 7 }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [], "referenceFloats": "0.5" }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [], "referenceFloats": ["0.5"] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [], "partitions": [ { "name": 3 } ] }""")]
    [InlineData("""{ "format": "fo4-skeleton", "name": "Bad", "bones": [], "localFrames": [ { "boneIndex": "0" } ] }""")]
    public void WronglyTypedJsonIsRefused(string json)
    {
        Assert.Throws<InvalidDataException>(() => SkeletonJson.Read(json));
    }

    private static HkxSkeleton Skeleton(string name)
    {
        var skeleton = new HkxSkeleton { Name = name };
        skeleton.BoneNames.Add("Root");
        skeleton.ParentIndices.Add(-1);
        skeleton.ReferencePose.Add(new HkxBonePose(Vector3.Zero, Quaternion.Identity, Vector3.One));
        skeleton.LockTranslation.Add(false);
        return skeleton;
    }

    private static byte[] CraftedSkeletonFile(
        Action<CraftedSkeleton, NativeAppend.Added>? corrupt = null)
    {
        var crafted = new CraftedSkeleton();

        int name = crafted.Text("Crafted ragdoll");
        int root = crafted.Text("Root");
        int child = crafted.Text("Child");
        int upperArm = crafted.Text("UpperArm");
        int lowerArm = crafted.Text("LowerArm");
        int upper = crafted.Text("Upper");
        int lower = crafted.Text("Lower");

        int parents = crafted.Block(new byte[2 * 2]);
        BitConverter.GetBytes((short)-1).CopyTo(crafted.Data.Data, parents);
        BitConverter.GetBytes((short)0).CopyTo(crafted.Data.Data, parents + 2);

        int bones = crafted.Block(new byte[2 * 16]);
        crafted.Point(bones, root);
        crafted.Point(bones + 16, child);
        crafted.Data.Data[bones + 8] = 1;

        int poses = crafted.Block(new byte[2 * 48]);
        WritePose(crafted, poses, new Vector3(1f, 2f, 3f));
        WritePose(crafted, poses + 48, new Vector3(4f, 5f, 6f));

        int floats = crafted.Block(new byte[2 * 4]);
        BitConverter.GetBytes(0.25f).CopyTo(crafted.Data.Data, floats);
        BitConverter.GetBytes(0.5f).CopyTo(crafted.Data.Data, floats + 4);

        int slots = crafted.Block(new byte[2 * 8]);
        crafted.Point(slots, upperArm);
        crafted.Point(slots + 8, lowerArm);

        int frames = crafted.Block(new byte[2 * 16]);
        BitConverter.GetBytes((short)1).CopyTo(crafted.Data.Data, frames + 8);

        int partitions = crafted.Block(new byte[2 * 16]);
        crafted.Point(partitions, upper);
        BitConverter.GetBytes((short)0).CopyTo(crafted.Data.Data, partitions + 8);
        BitConverter.GetBytes((short)1).CopyTo(crafted.Data.Data, partitions + 10);
        crafted.Point(partitions + 16, lower);
        BitConverter.GetBytes((short)1).CopyTo(crafted.Data.Data, partitions + 16 + 8);
        BitConverter.GetBytes((short)1).CopyTo(crafted.Data.Data, partitions + 16 + 10);

        var frame = crafted.Object("hkLocalFrame");
        var skeleton = crafted.Object("hkaSkeleton");
        crafted.Point(frames, frame.Offset);
        crafted.Point(skeleton.Offset + Field("hkaSkeleton", "name"), name);
        crafted.Element("hkaSkeleton", "parentIndices", skeleton, parents, 2);
        crafted.Element("hkaSkeleton", "bones", skeleton, bones, 2);
        crafted.Element("hkaSkeleton", "referencePose", skeleton, poses, 2);
        crafted.Element("hkaSkeleton", "referenceFloats", skeleton, floats, 2);
        crafted.Element("hkaSkeleton", "floatSlots", skeleton, slots, 2);
        crafted.Element("hkaSkeleton", "localFrames", skeleton, frames, 2);
        crafted.Element("hkaSkeleton", "partitions", skeleton, partitions, 2);
        corrupt?.Invoke(crafted, skeleton);
        return crafted.Build();
    }

    private static void WritePose(CraftedSkeleton crafted, int at, Vector3 translation)
    {
        byte[] data = crafted.Data.Data;
        BitConverter.GetBytes(translation.X).CopyTo(data, at);
        BitConverter.GetBytes(translation.Y).CopyTo(data, at + 4);
        BitConverter.GetBytes(translation.Z).CopyTo(data, at + 8);
        BitConverter.GetBytes(0f).CopyTo(data, at + 16);
        BitConverter.GetBytes(0f).CopyTo(data, at + 20);
        BitConverter.GetBytes(0f).CopyTo(data, at + 24);
        BitConverter.GetBytes(1f).CopyTo(data, at + 28);
        BitConverter.GetBytes(1f).CopyTo(data, at + 32);
        BitConverter.GetBytes(1f).CopyTo(data, at + 36);
        BitConverter.GetBytes(1f).CopyTo(data, at + 40);
    }

    private static int Field(string className, string member) =>
        HavokClasses.Shipped.Field(className, member)!.Offset;

    [Fact]
    public void AClipWithAMalformedEmbeddedSkeletonStillDecodesAndSaves()
    {
        string fixture = Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla",
            "Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx");
        byte[] original = File.ReadAllBytes(fixture);
        Assert.DoesNotContain("hkaSkeleton", Encoding.ASCII.GetString(original), StringComparison.Ordinal);

        byte[] bytes = WithMalformedSkeleton(original);
        Assert.Contains("hkaSkeleton", Encoding.ASCII.GetString(bytes), StringComparison.Ordinal);

        var reader = new HkxBinaryReader();
        Assert.True(reader.TryReadAnimation(bytes, out HkxAnimationData read));
        Assert.Equal(NativeAnimation.SplineClass, read.AnimationClass);
        Assert.True(read.NumFrames > 1);
        Assert.Null(read.Skeleton);
        Assert.Throws<InvalidDataException>(() => reader.ReadSkeleton(bytes));

        using var scope = new TestArchives.TempDirectory("bgs-malformed-skeleton");
        string path = Path.Combine(scope.Path, "clip.hkx");
        File.WriteAllBytes(path, bytes);

        var saved = AnimationSaveTransaction.Commit(
            path, read, DocumentSourceStamp.Capture(path), editedTrack: 0, editedFrame: 1);

        Assert.True(saved.Committed, saved.Message);
        var reopened = new HkxBinaryReader().ParseHkx(File.ReadAllBytes(path));
        Assert.Equal(read.NumFrames, reopened.NumFrames);
        Assert.Equal(read.Tracks.Count, reopened.Tracks.Count);
    }

    private static byte[] WithMalformedSkeleton(byte[] hkx)
    {
        var image = PackfileImage.Read(hkx);
        var data = image.Section("__data__")!;
        var skeleton = NativeAppend.Object(image, "hkaSkeleton");

        int name = data.AppendData(Encoding.ASCII.GetBytes("Ragdoll\0"));
        data.AlignData(16);
        int bones = data.AppendData(new byte[0x10]);
        data.SetLocal(bones, name);
        int bonesField = skeleton.Offset + HavokClasses.Shipped.Field("hkaSkeleton", "bones")!.Offset;
        data.SetLocal(bonesField, bones);
        BitConverter.GetBytes(1).CopyTo(data.Data, bonesField + image.Layout.PointerSize);

        int floatsField = skeleton.Offset + HavokClasses.Shipped.Field("hkaSkeleton", "referenceFloats")!.Offset;
        data.SetGlobal(floatsField, (image.Sections.IndexOf(data) + 1) % image.Sections.Count, 0);

        FixupOrder.Reorder(image);
        return image.Rebuild();
    }

    private sealed class CraftedSkeleton
    {
        private readonly PackfileImage _image = new() { Predicates = new byte[16] };

        public CraftedSkeleton()
        {
            foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
            {
                var tag = new byte[20];
                Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
                _image.Sections.Add(new PackfileSection { TagBytes = tag });
            }
        }

        public PackfileSection Data => _image.Section("__data__")!;

        private int PointerSize => _image.Layout.PointerSize;

        public NativeAppend.Added Object(string className) => NativeAppend.Object(_image, className);

        public int Block(byte[] bytes)
        {
            Data.AlignData(16);
            return Data.AppendData(bytes);
        }

        public int Text(string value) => Block(Encoding.ASCII.GetBytes(value + "\0"));

        public void Point(int at, int target) => Data.SetLocal(at, target);

        public void Element(string className, string member, NativeAppend.Added obj, int arrayAt, int count)
        {
            int field = obj.Offset + Field(className, member);
            Data.SetLocal(field, arrayAt);
            BitConverter.GetBytes(count).CopyTo(Data.Data, field + PointerSize);
        }

        public int FieldOffset(string className, string member, NativeAppend.Added obj) =>
            obj.Offset + Field(className, member);

        public int DataSectionIndex => _image.Sections.IndexOf(Data);

        public int SectionIndex(string tag) => _image.Sections.FindIndex(section => section.Tag == tag);

        public int DataLength => Data.Data.Length;

        public int LocalTarget(string className, string member, NativeAppend.Added obj) =>
            Data.Locals().First(fixup => fixup.Source == FieldOffset(className, member, obj)).Destination;

        public void ClearLocal(string className, string member, NativeAppend.Added obj) =>
            Data.SetLocal(FieldOffset(className, member, obj), -1);

        public void SetGlobal(string className, string member, NativeAppend.Added obj, int section, int destination) =>
            Data.SetGlobal(FieldOffset(className, member, obj), section, destination);

        public void Retarget(string className, string member, NativeAppend.Added obj, int destination) =>
            Data.SetLocals(Data.Locals().Append((FieldOffset(className, member, obj), destination)));

        public void SetCount(string className, string member, NativeAppend.Added obj, int count) =>
            BitConverter.GetBytes(count).CopyTo(Data.Data, FieldOffset(className, member, obj) + PointerSize);

        public byte[] Build()
        {
            FixupOrder.Reorder(_image);
            return _image.Rebuild();
        }
    }
}
