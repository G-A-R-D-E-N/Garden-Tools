using System;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Archive;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using Xunit;
using R = Radicle.Interop;

namespace BehaviourStudio.Tests;

public sealed class NodeTreeTests
{
    private static R.NifNode Node(string name, R.NifNode? parent, Vector3 offset)
    {
        var node = new R.NifNode
        {
            Block = 0, Type = "NiNode", Name = name, ParentIndex = parent == null ? -1 : 0,
            ChildBlocks = Array.Empty<int>(), Children = Array.Empty<R.NifNode?>(),
            Translation = offset, Rotation = new float[] { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, Scale = 1,
        };
        typeof(R.NifNode).GetProperty(nameof(R.NifNode.Parent))!.SetValue(node, parent);
        return node;
    }

    [Fact]
    public void ResolverUsesNearestExactAncestorAndOrderedSources()
    {
        var skeleton = new HkxSkeleton();
        skeleton.BoneNames.AddRange(new[] { "Root", "Arm" });
        var root = Node("Root", null, new Vector3(100));
        var arm = Node("Arm", root, new Vector3(50));
        var helper = Node("helper", arm, new Vector3(1, 2, 3));
        var tree = new NodeTree(skeleton);
        tree.Add(new[] { root, arm, helper }, "skeleton.nif");
        tree.Add(new[] { Node("helper", root, new Vector3(90)), root }, "body NIF");
        var link = tree.Resolve("HELPER")!.Value;
        Assert.Equal(1, link.SkeletonBone);
        Assert.Equal(new Vector3(1, 2, 3), link.LocalChain.Translation);
        Assert.Equal("skeleton.nif", link.Source);
    }

    [Fact]
    public void DisconnectedAndCyclicTreesFallBackWithoutChangingExactBind()
    {
        var skeleton = new HkxSkeleton();
        skeleton.BoneNames.Add("Arm");
        var helper = Node("Arm_skin", null, Vector3.One);
        typeof(R.NifNode).GetProperty(nameof(R.NifNode.Parent))!.SetValue(helper, helper);
        var tree = new NodeTree(skeleton);
        tree.Add(new[] { helper }, "body NIF");
        Assert.Equal("interim approximation", tree.Resolve("Arm_skin")!.Value.Source);
        Assert.Null(tree.Resolve("absent"));
        var shape = new NifShape();
        shape.BoneNames.Add("Arm_skin");
        Assert.Equal(-1, SkinnedMesh.Bind(shape, skeleton).ToSkeleton[0]);
    }

    [SkinDataFact]
    public void VanillaMaleRestPoseReproducesVerticesAfterOutputPlacementRemoval()
    {
        string folder = Environment.GetEnvironmentVariable("BGS_SKIN_DATA")!;
        using var data = GameData.Discover(folder);
        byte[] Read(string name) => data.ReadEntry(Path.Combine(folder,
            name.EndsWith(".hkx", StringComparison.Ordinal) ? "Fallout4 - Animations.ba2" : "Fallout4 - Meshes.ba2"),
            "Meshes/Actors/Character/CharacterAssets/" + name)!;
        var skeleton = new HkxBinaryReader().ReadSkeleton(Read("skeleton.hkx"));
        var pose = AnimationPose.ReferencePose(skeleton);
        var tree = new NodeTree(skeleton);
        tree.Add(R.NifFile.FromBytes(Read("skeleton.nif")).Nodes, "skeleton.nif");
        var bytes = Read("MaleBody.nif");
        var native = R.NifFile.FromBytes(bytes);
        foreach (var shape in NifGeometry.Shapes(NifFile.Parse(bytes, "MaleBody.nif")))
        {
            var parity = native.Shapes.Single(s => s.Name == shape.Name);
            Assert.Equal(shape.Vertices.Count, parity.Positions.Length);
            Assert.Equal(shape.BoneNames, parity.Bones.Select(b => b.Name));
            for (int i = 0; i < shape.Vertices.Count; i++) Assert.Equal(shape.Vertices[i], parity.Positions[i]);
            var binding = SkinnedMesh.Bind(shape, skeleton);
            SkinnedMesh.Resolve(binding, shape, tree);
            Assert.Empty(binding.Unmatched);
            int anchor = Array.FindIndex(binding.ToSkeleton, b => b >= 0);
            Assert.True(Matrix4x4.Invert(SkinnedMesh.BoneMatrix(shape, binding, pose, anchor)!.Value, out var inverse));
            var output = SkinnedMesh.Pose(shape, binding, pose, skeleton);
            var errors = output.Select((p, i) => Vector3.Distance(Vector3.Transform(p, inverse), shape.Vertices[i])).ToArray();
            Assert.InRange(errors.Max(), 0, 0.21f);
            Assert.InRange(errors.Average(), 0, 0.003f);
        }
    }
}
