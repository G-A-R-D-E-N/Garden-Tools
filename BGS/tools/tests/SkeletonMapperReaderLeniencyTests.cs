using System;
using System.IO;
using System.Linq;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;

namespace BehaviourStudio.Tests;

public sealed class SkeletonMapperReaderLeniencyTests
{
    private readonly ITestOutputHelper _out;

    public SkeletonMapperReaderLeniencyTests(ITestOutputHelper output) => _out = output;

    [Fact]
    public void AnUnreadableChainArrayStillYieldsTheSimpleRowsToThePhysicsExtractor()
    {
        byte[] patched = Strip(File.ReadAllBytes(FixturePath()), "chainMappings", 0);

        var objects = new PackfileObjects(PackfileImage.Read(patched));
        var mappers = SkeletonMapperReader.Read(objects);
        Assert.Equal(2, mappers.Count);

        var forward = Assert.Single(mappers, mapper => !mapper.OptionalMembersRead);
        var intact = Assert.Single(mappers, mapper => mapper.OptionalMembersRead);

        Assert.Equal(18, forward.SimpleMappings.Count);
        Assert.Empty(forward.ChainMappings);
        Assert.Equal(4, intact.ChainMappings.Count);
        Assert.NotEmpty(intact.UnmappedBones);

        foreach (var row in forward.SimpleMappings)
        {
            Assert.InRange(row.BoneA, 0, forward.SkeletonA.BoneNames.Count - 1);
            Assert.InRange(row.BoneB, 0, forward.SkeletonB.BoneNames.Count - 1);
        }

        var model = HavokPhysicsExtractor.TryExtract(patched);
        Assert.NotNull(model);
        _out.WriteLine($"the extractor still sees {model!.Mappings.Count} mapping(s) from the degraded file");
        Assert.Equal(18, model.Mappings.Count);
        Assert.Equal(95, model.AnimationSkeleton!.BoneNames.Count);

        var refusal = Assert.Throws<InvalidDataException>(() => new SkeletonMapperDocument(patched));
        _out.WriteLine(refusal.Message);
        Assert.Contains("cannot read", refusal.Message);

        var instance = Instance(objects, 0);
        var read = SkeletonMapperReader.Read(objects, instance)!;
        var node = new SkeletonMapperNode(instance, objects.FieldAt(instance, "mapping")!.Value, read);
        var write = Assert.Throws<InvalidOperationException>(() => SkeletonMapperWriteback.Write(
            patched, new[] { node }, new[] { new SkeletonMapperChange(node, node.Copy()) }));
        _out.WriteLine(write.Message);
        Assert.Contains("cannot read", write.Message);
    }

    [Fact]
    public void AnUnreadableUnmappedBoneArrayStillYieldsTheSimpleRows()
    {
        byte[] patched = Strip(File.ReadAllBytes(FixturePath()), "unmappedBones", 1);

        var mappers = SkeletonMapperReader.Read(new PackfileObjects(PackfileImage.Read(patched)));
        Assert.Equal(2, mappers.Count);

        var degraded = Assert.Single(mappers, mapper => !mapper.OptionalMembersRead);
        Assert.Equal(18, degraded.SimpleMappings.Count);
        Assert.Empty(degraded.UnmappedBones);
        Assert.Equal(4, degraded.ChainMappings.Count);

        var model = HavokPhysicsExtractor.TryExtract(patched);
        Assert.NotNull(model);
        Assert.Equal(18, model!.Mappings.Count);
    }

    private static PackfileObjects.Instance Instance(PackfileObjects objects, int index) =>
        objects.Instances.Where(instance => instance.ClassName == "hkaSkeletonMapper").ElementAt(index);

    private static byte[] Strip(byte[] source, string member, int index)
    {
        var image = PackfileImage.Read(source);
        var data = image.Section("__data__")!;
        int section = image.Sections.IndexOf(data);
        var objects = new PackfileObjects(image);
        var instance = Instance(objects, index);
        int at = objects.FieldAt(instance, "mapping")!.Value
                 + HavokClasses.Shipped.Field("hkaSkeletonMapperData", member)!.Offset;

        if (data.Locals().Any(entry => entry.Source == at)) data.SetLocal(at, -1);
        if (data.Globals().Any(entry => entry.Source == at)) data.SetGlobal(at, section, -1);
        return image.Rebuild();
    }

    private static string FixturePath() => Path.Combine(
        AppContext.BaseDirectory, "fixtures", "vanilla",
        "Meshes", "Actors", "Character", "CharacterAssets", "skeleton.hkx");
}
