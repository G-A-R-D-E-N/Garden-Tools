using System;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HavokPhysicsExtractorTests
{
    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla",
                     relative.Replace('/', Path.DirectorySeparatorChar));

    private const string Skeleton =
        "Meshes/Actors/Character/CharacterAssets/skeleton.hkx";

    [Fact]
    public void VanillaSkeletonExtractsMeasuredRagdollCounts()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);
        Assert.Equal(18, model!.Bodies.Count);
        Assert.Equal(19, model.Shapes.Count);
        Assert.Equal(17, model.Constraints.Count);
        Assert.Equal(18, model.BoneBindings.Count);
    }

    [Fact]
    public void BodyFieldsMatchXmlValues()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var body = Assert.Single(model!.Bodies, b => b.Name == "Ragdoll_NPC L Thigh");
        Assert.Equal(1, body.Id);
        Assert.Equal(99, body.ShapeId);
        AssertNear(new Vector3(-6.6150898933410645f, 4.546909185592085E-4f, 68.91129302978516f),
                   body.Position, 1e-3f);
        AssertNear(new Quaternion(-0.10920292884111404f, 0.7402823567390442f,
                                  0.09750235825777054f, 0.6561630964279175f),
                   body.Rotation, 1e-3f);
    }

    [Fact]
    public void CapsuleShapeCarriesMeasuredPolytope()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var shape = model!.Shapes.FirstOrDefault(s => s.Id == 96);
        Assert.NotNull(shape);
        Assert.Equal(HavokPhysicsShapeKind.Capsule, shape!.Kind);
        Assert.Equal(7, shape.Vertices.Count);
        Assert.Equal(5, shape.FaceRings.Count);
        AssertNear(new Vector3(0.16652747988700867f, -0.16613319516181946f, 1.934882402420044f),
                   shape.Vertices[0], 1e-3f);
        Assert.Equal(new[] { 7, 6, 2, 3 }, shape.FaceRings[0]);
    }

    [Fact]
    public void ConstraintCinfosProduceMeasuredGraph()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var pairs = model!.Constraints.Select(c => (c.BodyA, c.BodyB)).ToList();
        Assert.Equal(new[]
        {
            (1, 0), (2, 0), (3, 0), (4, 1), (5, 2), (6, 3), (7, 4), (8, 5), (9, 6),
            (10, 9), (11, 9), (12, 9), (13, 10), (14, 11), (15, 12), (16, 13), (17, 15),
        }, pairs);

        Assert.Equal(9, model.Constraints.Count(c => c.Kind == HavokConstraintKind.Ragdoll));
        Assert.Equal(8, model.Constraints.Count(c => c.Kind == HavokConstraintKind.LimitedHinge));
        Assert.DoesNotContain(model.Constraints, c => c.Kind == HavokConstraintKind.Unknown);
    }

    [Fact]
    public void BoneToBodyMapIsMeasured()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        Assert.All(model!.BoneBindings, binding =>
        {
            Assert.Equal(binding.BoneIndex, binding.BodyId);
            Assert.InRange(binding.BoneIndex, 0, 17);
            Assert.InRange(binding.BodyId, 0, model.Bodies.Count - 1);
        });
    }

    [Fact]
    public void ExtractedModelPassesTheValidator()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var findings = HavokPhysicsValidator.Check(model!, skeletonBoneCount: 18);
        Assert.DoesNotContain(findings, finding => finding.Level == HavokPhysicsValidationLevel.Error);
    }

    [Fact]
    public void ClothHairFixtureHasNoRagdollModel()
    {
        string fixture = Fixture("Meshes/Actors/Character/CharacterAssets/Hair/Female/FemaleHair04.hkx");
        Assert.Null(HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(fixture)));
    }

    [Fact]
    public void UnreadableInputFailsClosed()
    {
        Assert.Null(HavokPhysicsExtractor.TryExtract(null));
        Assert.Null(HavokPhysicsExtractor.TryExtract(Array.Empty<byte>()));
        Assert.Null(HavokPhysicsExtractor.TryExtract(new byte[] { 0, 1, 2, 3, 4, 5, 6, 7 }));
    }

    private static void AssertNear(Vector3 expected, Vector3 actual, float tolerance) =>
        Assert.True(Vector3.Distance(expected, actual) <= tolerance,
                    $"expected {expected}, measured {actual}");

    private static void AssertNear(Quaternion expected, Quaternion actual, float tolerance)
    {
        Assert.True(MathF.Abs(expected.X - actual.X) <= tolerance &&
                    MathF.Abs(expected.Y - actual.Y) <= tolerance &&
                    MathF.Abs(expected.Z - actual.Z) <= tolerance &&
                    MathF.Abs(expected.W - actual.W) <= tolerance,
                    $"expected {expected}, measured {actual}");
    }
}