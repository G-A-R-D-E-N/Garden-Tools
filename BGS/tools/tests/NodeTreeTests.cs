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

    private static HkxSkeleton Skeleton(params string[] names)
    {
        var skeleton = new HkxSkeleton();
        skeleton.BoneNames.AddRange(names);
        return skeleton;
    }

    private static Vector3 SkinnedVertex(HkxSkeleton skeleton, string bone)
    {
        var shape = new NifShape();
        shape.Vertices.Add(new Vector3(1, 0, 0));
        shape.BoneNames.Add(bone);
        shape.SkinToBone.Add(Matrix4x4.Identity);
        shape.BoneIndices.AddRange(new[] { 0, 0, 0, 0 });
        shape.BoneWeights.AddRange(new[] { 1f, 0f, 0f, 0f });
        var binding = SkinnedMesh.Bind(shape, skeleton);
        return SkinnedMesh.Pose(shape, binding, AnimationPose.ReferencePose(skeleton), skeleton)[0];
    }

    [Fact]
    public void ScaledRootScalesAChildInfluence()
    {
        var skeleton = Skeleton("Root", "Child");
        skeleton.ParentIndices.AddRange(new[] { -1, 0 });
        skeleton.ReferencePose.Add(new HkxBonePose(Vector3.Zero, Quaternion.Identity, new Vector3(2)));
        skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(1, 0, 0), Quaternion.Identity, Vector3.One));

        Assert.Equal(new Vector3(4, 0, 0), SkinnedVertex(skeleton, "Child"));
    }

    [Fact]
    public void ScaledParentScalesAChildInfluence()
    {
        var skeleton = Skeleton("Root", "Parent", "Child");
        skeleton.ParentIndices.AddRange(new[] { -1, 0, 1 });
        skeleton.ReferencePose.Add(new HkxBonePose(Vector3.Zero, Quaternion.Identity, Vector3.One));
        skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(1, 0, 0), Quaternion.Identity, new Vector3(2)));
        skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(1, 0, 0), Quaternion.Identity, Vector3.One));

        Assert.Equal(new Vector3(5, 0, 0), SkinnedVertex(skeleton, "Child"));
    }

    [Fact]
    public void HelperNodesWithDifferentChainsAreReportedAsAmbiguous()
    {
        var skeleton = Skeleton("Root", "Arm");
        var root = Node("Root", null, Vector3.Zero);
        var arm = Node("Arm", root, Vector3.Zero);
        var tree = new NodeTree(skeleton);
        tree.Add(new[] { root, arm, Node("Helper", arm, new Vector3(1, 0, 0)) }, "skeleton.nif");
        tree.Add(new[] { root, arm, Node("Helper", arm, new Vector3(2, 0, 0)) }, "body NIF");
        var shape = new NifShape();
        shape.BoneNames.Add("Helper");

        var binding = SkinnedMesh.Bind(shape, skeleton);
        SkinnedMesh.Resolve(binding, shape, tree);

        Assert.Empty(binding.Unmatched);
        Assert.Equal(new[] { "Helper" }, binding.Ambiguous);
        Assert.Contains("Helper", SkinnedMesh.BindingWarning(new[] { binding }));
    }

    [Fact]
    public void DuplicateSkeletonNamesAreReportedAsAmbiguousNotSilentlyBound()
    {
        var skeleton = Skeleton("Root", "Hand", "hand");
        var shape = new NifShape();
        shape.BoneNames.Add("Hand");

        var binding = SkinnedMesh.Bind(shape, skeleton);
        SkinnedMesh.Resolve(binding, shape, new NodeTree(skeleton));

        Assert.Empty(binding.Unmatched);
        Assert.Equal(1, binding.ToSkeleton[0]);
        Assert.Equal(new[] { "Hand" }, binding.Ambiguous);
        Assert.Contains("Hand", SkinnedMesh.BindingWarning(new[] { binding }));
    }

    [Fact]
    public void RebindingToAnIncompleteSkeletonKeepsTheIncompleteWarning()
    {
        var shape = new NifShape();
        shape.BoneNames.AddRange(new[] { "Root", "Hand" });
        string? Warning(HkxSkeleton skeleton)
        {
            var binding = SkinnedMesh.Bind(shape, skeleton);
            SkinnedMesh.Resolve(binding, shape, new NodeTree(skeleton));
            return SkinnedMesh.BindingWarning(new[] { binding });
        }
        var complete = Skeleton("Root", "Hand");
        var incomplete = Skeleton("Root");

        Assert.Null(Warning(complete));
        string? warning = Warning(incomplete);
        Assert.Contains("1 bone did not match", warning);
        Assert.Contains("Hand", warning);
        Assert.Null(Warning(complete));
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
