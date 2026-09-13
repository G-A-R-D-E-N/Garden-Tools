using System;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using OpenCommonwealth.Services.Nif;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class SkinEditingProofTests
{
    [Fact]
    public void SkinEditingNormalisesPrunesAndCopiesWeights()
    {
        var shape = Shape(2, "Root", "Arm", "Hand");
        Set(shape, 0, (0, 0.3f), (1, 0.3f), (2, 0.004f));
        Set(shape, 1, (1, 1f));

        Assert.Equal(1, SkinEdit.Normalise(shape));
        Assert.Equal(1f, SkinEdit.WeightSum(shape, 0), 5);
        Assert.Equal(1, SkinEdit.Prune(shape, 0.01f));
        Assert.Equal(1f, SkinEdit.WeightSum(shape, 0), 5);

        Assert.Equal(2, SkinEdit.CopyWeights(shape, 1, 2));
        Assert.Contains(SkinEdit.InfluencesOf(shape, 0), influence => influence.Bone == 2);
        Assert.Contains(SkinEdit.InfluencesOf(shape, 1), influence => influence.Bone == 2);
    }

    [Fact]
    public void SkinValidationFindsInvalidAndUnboundWeights()
    {
        var shape = Shape(1, "Root", "Arm");
        Set(shape, 0, (0, 0.5f), (1, 0.5f));
        shape.BoneIndices[0] = 9;

        Assert.Contains(SkinValidator.Check(shape), finding =>
            finding.Level == SkeletonValidator.Level.Error &&
            finding.What.Contains("does not have", StringComparison.Ordinal));

        shape.BoneIndices[0] = 0;
        var rig = new SkeletonEdit();
        rig.AddBone("Root");

        Assert.Contains(SkinValidator.Check(shape, SkinnedMesh.Bind(shape, rig.Skeleton)), finding =>
            finding.Level == SkeletonValidator.Level.Warning &&
            finding.What.Contains("skeleton does not have", StringComparison.Ordinal));
    }

    [Theory]
    [InlineData("LArm_Hand", "RArm_Hand")]
    [InlineData("L_RibHelper", "R_RibHelper")]
    [InlineData("WeaponIKTargetL", "WeaponIKTargetR")]
    [InlineData("AnimObjectL1", "AnimObjectR1")]
    [InlineData("Ragdoll_NPC L Thigh", "Ragdoll_NPC R Thigh")]
    public void SkeletonMirrorPairsFalloutSideNames(string left, string right)
    {
        var pairing = SkeletonMirror.Of(new[] { "Root", left, right });

        Assert.Equal(2, pairing.PartnerOf(1));
        Assert.Equal(1, pairing.PartnerOf(2));
        Assert.Single(pairing.Pairs);
    }

    [Fact]
    public void MirrorWeightsCopiesOnlyTheReceivingSide()
    {
        var shape = Shape(2, "Root", "LArm_Hand", "RArm_Hand");
        shape.Vertices[0] = new Vector3(10, 0, 0);
        shape.Vertices[1] = new Vector3(-10, 0, 0);
        Set(shape, 0, (2, 1f));
        Set(shape, 1, (0, 1f));

        var report = SkinEdit.MirrorWeights(
            shape, lane: 0, fromPositiveSide: true, tolerance: 0.01f);

        Assert.Equal(1, report.Mirrored);
        Assert.Equal(1, SkinEdit.InfluencesOf(shape, 1)[0].Bone);
        Assert.Equal(2, SkinEdit.InfluencesOf(shape, 0)[0].Bone);
    }

    private static NifShape Shape(int vertices, params string[] bones)
    {
        var shape = new NifShape { Name = "Proof" };
        shape.BoneNames.AddRange(bones);
        foreach (string _ in bones)
            shape.SkinToBone.Add(Matrix4x4.Identity);

        for (int vertex = 0; vertex < vertices; vertex++)
        {
            shape.Vertices.Add(Vector3.Zero);
            for (int slot = 0; slot < 4; slot++)
            {
                shape.BoneIndices.Add(0);
                shape.BoneWeights.Add(0);
            }
        }

        return shape;
    }

    private static void Set(
        NifShape shape,
        int vertex,
        params (int Bone, float Weight)[] influences) =>
        SkinEdit.SetInfluences(
            shape,
            vertex,
            influences.Select(value => new SkinEdit.Influence(value.Bone, value.Weight)).ToList());
}
