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
    public void RagdollConstraintCarriesMeasuredPivotsAndLimits()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var constraint = Assert.Single(model!.Constraints, c => c.Id == 0);
        Assert.Equal(HavokConstraintKind.Ragdoll, constraint.Kind);
        Assert.Equal(1, constraint.BodyA);
        Assert.Equal(0, constraint.BodyB);

        Assert.NotNull(constraint.FrameA);
        Assert.Equal(16, constraint.FrameA!.Length);
        AssertNear(new Vector3(0f, 0f, 0f), Pivot(constraint.FrameA), 1e-3f);

        Assert.NotNull(constraint.FrameB);
        AssertNear(new Vector3(2.897924787248485E-6f, 4.2483024299144745E-4f, 6.615072250366211f),
                   Pivot(constraint.FrameB!), 1e-3f);

        Assert.Equal(0, constraint.TwistAxis);
        Assert.Equal(1, constraint.TwistRefAxis);
        AssertNear(-0.2617993950843811f, constraint.TwistMinAngle!.Value, 1e-4f);
        AssertNear(0.2617993950843811f, constraint.TwistMaxAngle!.Value, 1e-4f);

        Assert.Equal(0, constraint.ConeTwistAxis);
        Assert.Equal(0, constraint.ConeRefAxis);
        AssertNear(0.5235987901687622f, constraint.ConeMaxAngle!.Value, 1e-4f);
    }

    [Fact]
    public void LimitedHingeConstraintCarriesMeasuredHingeLimit()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var constraint = Assert.Single(model!.Constraints, c => c.Kind == HavokConstraintKind.LimitedHinge && c.BodyA == 4);
        Assert.Equal(1, constraint.BodyB);
        Assert.Equal(0, constraint.LimitAxis);
        AssertNear(-1.919862151145935f, constraint.MinAngle, 1e-4f);
        AssertNear(0.031415924429893494f, constraint.MaxAngle, 1e-4f);

        Assert.NotNull(constraint.FrameB);
        AssertNear(new Vector3(31.592397689819336f, -0.03480081632733345f, -0.41684818267822266f),
                   Pivot(constraint.FrameB!), 1e-3f);
    }

    [Fact]
    public void EveryMeasuredConstraintKindCarriesItsLimits()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        foreach (var constraint in model!.Constraints.Where(c => c.Kind == HavokConstraintKind.Ragdoll))
        {
            Assert.NotNull(constraint.FrameA);
            Assert.NotNull(constraint.FrameB);
            Assert.True(constraint.TwistMinAngle.HasValue);
            Assert.True(constraint.TwistMaxAngle.HasValue);
            Assert.True(constraint.ConeMaxAngle.HasValue);
        }

        foreach (var constraint in model.Constraints.Where(c => c.Kind == HavokConstraintKind.LimitedHinge))
        {
            Assert.NotNull(constraint.FrameA);
            Assert.NotNull(constraint.FrameB);
            Assert.True(constraint.MinAngle < constraint.MaxAngle);
        }
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
    public void PhysicsSkeletonCarriesMeasuredBoneNames()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var skeleton = model!.Skeleton;
        Assert.NotNull(skeleton);
        Assert.Equal("Ragdoll_NPC COM", skeleton!.Name);
        Assert.Equal(18, skeleton.BoneNames.Count);
        Assert.Equal("Ragdoll_NPC COM", skeleton.BoneNames[0]);
        Assert.Equal("Ragdoll_NPC L Thigh", skeleton.BoneNames[1]);
        Assert.Equal("Ragdoll_NPC R Calf", skeleton.BoneNames[5]);
        Assert.Equal("Ragdoll_NPC R Foot", skeleton.BoneNames[8]);
        Assert.Equal("Ragdoll_NPC L Forearm", skeleton.BoneNames[13]);
    }

    [Fact]
    public void PhysicsSkeletonParentIndicesMatchXml()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var parents = model!.Skeleton!.ParentIndices;
        Assert.Equal(18, parents.Count);
        Assert.Equal(new[]
        {
            -1, 0, 0, 0, 1, 2, 3, 4, 5, 6, 9, 9, 9, 10, 11, 12, 13, 15,
        }, parents);
    }

    [Fact]
    public void PhysicsSkeletonReferencePoseMatchesXml()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var poses = model!.Skeleton!.ReferencePose;
        Assert.Equal(18, poses.Count);

        // Translations (the translation column of each measured hkQsTransform).
        AssertNear(new Vector3(-1.668929871811997E-5f, 2.1100042431498878E-5f, 68.91128540039062f),
                   poses[0].Translation, 1e-3f);
        AssertNear(new Vector3(7.62939453125E-6f, 4.249164485372603E-4f, 6.615072250366211f),
                   poses[1].Translation, 1e-3f);
        AssertNear(new Vector3(3.7916259765625f, 2.0265579223632812E-6f, -7.011654815869406E-6f),
                   poses[3].Translation, 1e-3f);
        AssertNear(new Vector3(31.592391967773438f, -0.03480100631713867f, -0.41684722900390625f),
                   poses[4].Translation, 1e-3f);

        // Rotations (the quaternion column of each measured hkQsTransform).
        AssertNear(new Quaternion(-4.6361520844584447E-7f, -0.7071065306663513f,
                                  -4.6361529371097276E-7f, 0.7071069478988647f),
                   poses[0].Rotation, 1e-4f);
        AssertNear(new Quaternion(-0.008273639716207981f, 0.9874358773231506f,
                                  0.14616334438323975f, -0.05948099493980408f),
                   poses[1].Rotation, 1e-4f);
        AssertNear(new Quaternion(7.332672566917608E-7f, -4.470348358154297E-7f,
                                  0.06121326982975006f, 0.9981245994567871f),
                   poses[3].Rotation, 1e-4f);

        // Scale is identity in the measured file (the hkQsTransform scale column).
        Assert.All(poses, pose => AssertNear(Vector3.One, pose.Scale, 1e-3f));
    }

    [Fact]
    public void MotionCinfosProvideMeasuredWorldCenterOfMass()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var com = Assert.Single(model!.Bodies, b => b.Id == 0);
        AssertNear(new Vector3(0.020323412492871284f, -0.0491427443921566f, 69.19477844238281f),
                   com.CenterOfMass, 1e-3f);

        // The motion id mapping is not identity in this file (R Thigh body 2 uses motion 4,
        // L Calf body 4 uses motion 2), so these pin both the lookup and the values.
        var leftThigh = Assert.Single(model.Bodies, b => b.Id == 1);
        AssertNear(new Vector3(-8.432622909545898f, -0.5323612093925476f, 53.22739028930664f),
                   leftThigh.CenterOfMass, 1e-3f);

        var rightThigh = Assert.Single(model.Bodies, b => b.Id == 2);
        AssertNear(new Vector3(8.431289672851562f, -0.5326367616653442f, 53.22724914550781f),
                   rightThigh.CenterOfMass, 1e-3f);

        var leftCalf = Assert.Single(model.Bodies, b => b.Id == 4);
        AssertNear(new Vector3(-11.501089096069336f, -3.5246798992156982f, 21.728782653808594f),
                   leftCalf.CenterOfMass, 1e-3f);
    }

    [Fact]
    public void EveryBodyCarriesAFiniteMeasuredCom()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        Assert.All(model!.Bodies, body =>
        {
            // The fixture measures a center of mass for every body; zero is the sentinel
            // for "no measured COM", so a non-zero finite value proves the read happened.
            Assert.NotEqual(Vector3.Zero, body.CenterOfMass);
            Assert.True(Finite(body.CenterOfMass), $"{body.Name} center of mass is not finite");
        });

        // The COM is genuinely the motion cinfo value, not a copy of the body position:
        // the L Thigh body sits at z 68.91 while its measured COM is at z 53.23, down the
        // capsule toward the knee.
        var thigh = Assert.Single(model.Bodies, b => b.Id == 1);
        Assert.True(MathF.Abs(thigh.CenterOfMass.Z - thigh.Position.Z) > 10f);
    }

    [Fact]
    public void PhysicsSkeletonWorldPoseAgreesWithBodyPositions()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        // The composed reference pose and the measured body cinfo positions are two
        // independent sources for the same file-space placement; the bindings say which
        // body a bone owns, so each bone's composed world position must sit on its body.
        var world = model!.Skeleton!.WorldReferencePositions();
        Assert.Equal(18, world.Count);

        foreach (var binding in model.BoneBindings)
        {
            var body = Assert.Single(model.Bodies, b => b.Id == binding.BodyId);
            Assert.True(Vector3.Distance(world[binding.BoneIndex], body.Position) <= 5e-2f,
                        $"bone {binding.BoneIndex} world {world[binding.BoneIndex]} vs body {body.Id} " +
                        $"{body.Position}");
        }

        // Pin one explicitly to the XML-derived hand-computed value: the root sits at its
        // local translation and the thigh bone hangs below it along the parent's -90 degree
        // Y rotation, landing on the measured L Thigh body position.
        AssertNear(new Vector3(-1.668929871811997E-5f, 2.1100042431498878E-5f, 68.91128540039062f),
                   world[0], 1e-3f);
        AssertNear(new Vector3(-6.6150898933410645f, 4.546909185592E-4f, 68.91129302978516f),
                   world[1], 1e-3f);
    }

    [Fact]
    public void PhysicsSkeletonChainIsTopologicallyOrdered()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        var skeleton = model!.Skeleton!;
        var world = skeleton.WorldReferencePositions();

        // Every non-root link connects two finite, distinct file-space points and always
        // reaches back to an earlier bone (valid skeletons are topologically ordered).
        for (int i = 0; i < skeleton.ParentIndices.Count; i++)
        {
            int parent = skeleton.ParentIndices[i];
            if (parent < 0) continue;

            Assert.True(parent < i, $"bone {i} parent {parent} is not topologically ordered");
            Assert.True(Finite(world[i]), $"bone {i} world position is not finite");
            Assert.True(Finite(world[parent]), $"bone {parent} world position is not finite");
            Assert.True(Vector3.Distance(world[i], world[parent]) > 0f,
                        $"bone {i} sits on its parent {parent}");
        }
    }

    private static bool Finite(Vector3 value) =>
        !float.IsNaN(value.X) && !float.IsInfinity(value.X) &&
        !float.IsNaN(value.Y) && !float.IsInfinity(value.Y) &&
        !float.IsNaN(value.Z) && !float.IsInfinity(value.Z);

    [Fact]
    public void BoneBindingsResolveBodyNamesForLabels()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(Fixture(Skeleton)));
        Assert.NotNull(model);

        // Every binding must point at a body we can label with the skeleton bone's name.
        foreach (var binding in model!.BoneBindings)
        {
            var body = Assert.Single(model.Bodies, b => b.Id == binding.BodyId);
            var boneName = model.Skeleton!.BoneNames[binding.BoneIndex];
            Assert.NotEmpty(boneName);
            Assert.NotEmpty(body.Name);
        }

        var thigh = Assert.Single(model.BoneBindings, binding => binding.BoneIndex == 1);
        Assert.Equal(1, thigh.BodyId);
        Assert.Equal("Ragdoll_NPC L Thigh", model.Skeleton!.BoneNames[thigh.BoneIndex]);
        Assert.Equal("Ragdoll_NPC L Thigh", model.Bodies.Single(b => b.Id == thigh.BodyId).Name);
    }

    [Fact]
    public void ValidatorFlagsBrokenSkeletonConsistency()
    {
        var model = new HavokRagdollModel
        {
            Skeleton = new HavokRagdollSkeleton { Name = "broken" },
        };
        model.Skeleton!.BoneNames.AddRange(new[] { "A", "B" });
        model.Skeleton.ParentIndices.Add(5);
        model.Skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(0, 0, 0), Quaternion.Identity, Vector3.One));
        model.Skeleton.ReferencePose.Add(new HkxBonePose(
            new Vector3(float.NaN, 0, 0), Quaternion.Identity, new Vector3(2, 2, 2)));
        model.BoneBindings.Add(new HavokRagdollBoneBinding(7, 0));

        var findings = HavokPhysicsValidator.Check(model);
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("parent index 5"));
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("2-bone physics skeleton"));
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("1 parent indices"));
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("reference pose"));
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("missing body 0"));
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Warning && f.Message.Contains("not identity"));
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

    private static Vector3 Pivot(float[] frame) => new(frame[12], frame[13], frame[14]);

    private static void AssertNear(float expected, float actual, float tolerance) =>
        Assert.True(MathF.Abs(expected - actual) <= tolerance, $"expected {expected}, measured {actual}");

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