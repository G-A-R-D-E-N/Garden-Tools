using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed partial class HavokPhysicsExtractorTests
{
    [Fact]
    public void ConstraintInspectionLineShowsMeasuredLimitsAndPivots()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var ragdoll = model.Constraints.First(c => c.Id == 0);
        Assert.Equal("ragdoll: c0 Ragdoll_NPC L Thigh(1) -> Ragdoll_NPC COM(0) - " +
                     "twist -15.0..15.0 deg, cone 30.0 deg, " +
                     "pivotA (0.00, 0.00, 0.00), pivotB (0.00, 0.00, 6.62)",
                     SkeletonView.ConstraintInspectionLine(ragdoll, model.Bodies));

        var hinge = model.Constraints.First(c => c.Id == 3);
        Assert.Equal("limited hinge: c3 Ragdoll_NPC L Calf(4) -> Ragdoll_NPC L Thigh(1) - " +
                     "hinge -110.0..1.8 deg, " +
                     "pivotA (0.00, 0.00, 0.00), pivotB (31.59, -0.03, -0.42)",
                     SkeletonView.ConstraintInspectionLine(hinge, model.Bodies));

        var bare = new HavokConstraint { Id = 9, Kind = HavokConstraintKind.Hinge, BodyA = 42, BodyB = 43 };
        Assert.Equal("hinge: c9 body 42(42) -> body 43(43) - no measured limits",
                     SkeletonView.ConstraintInspectionLine(bare, model.Bodies));
    }

    [Fact]
    public void ConstraintPinCyclesOnClick()
    {
        Assert.Equal(0, SkeletonView.PinnedAfterClick(-1, 0));
        Assert.Equal(2, SkeletonView.PinnedAfterClick(0, 2));
        Assert.Equal(-1, SkeletonView.PinnedAfterClick(0, 0));
        Assert.Equal(-1, SkeletonView.PinnedAfterClick(0, -1));
        Assert.Equal(-1, SkeletonView.PinnedAfterClick(-1, -1));
    }

    [Fact]
    public void ConstraintPinIsIndependentOfHoverAndClearsOnReset()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var view = new SkeletonView();
        view.SetBodies(model);
        view.ShowConstraints = true;
        view.PinConstraintForTest(0);
        Assert.Equal(0, view.PinnedConstraintId);
        Assert.Equal(-1, view.HoveredConstraintId);

        view.Reset();
        Assert.Equal(-1, view.PinnedConstraintId);
        Assert.Equal(-1, view.HoveredConstraintId);
    }

    [Fact]
    public void EngineeringFrameModeClearsWithTheView()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var view = new SkeletonView { EngineeringFrames = true };
        view.SetBodies(model);
        Assert.True(view.EngineeringFrames);

        view.Reset();
        Assert.False(view.EngineeringFrames);
        Assert.Equal(0, view.DrawnBodies);
    }

    [Fact]
    public void BodyFrameLabelShowsMeasuredComAndStub()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var thigh = Assert.Single(model!.Bodies, b => b.Id == 1);
        Assert.Equal("COM (-8.43, -0.53, 53.23)  stub 3.61",
                     SkeletonView.BodyFrameLabel(thigh, 3.614246f));

        var root = Assert.Single(model.Bodies, b => b.Id == 0);
        Assert.Equal("COM (0.02, -0.05, 69.19)  stub 3.61",
                     SkeletonView.BodyFrameLabel(root, 3.614246f));

        var bare = new HavokRigidBody { Id = 42 };
        Assert.Equal("no measured COM  stub 3.61", SkeletonView.BodyFrameLabel(bare, 3.614246f));
    }

    [Fact]
    public void FrameTableReadsEveryBodysOriginAndCom()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        Assert.Equal(18, model!.Bodies.Count);
        var rows = model.Bodies.Select(SkeletonView.FrameTableRow).ToList();
        Assert.Equal(model.Bodies.Count, rows.Count);

        Assert.Equal("  0  Ragdoll_NPC COM   frame (-0.00, 0.00, 68.91)  COM (0.02, -0.05, 69.19)",
                     rows[0]);
        Assert.Equal("  1  Ragdoll_NPC L Thigh  frame (-6.62, 0.00, 68.91)  COM (-8.43, -0.53, 53.23)",
                     rows[1]);
        Assert.Equal(" 16  Ragdoll_NPC L Hand  frame (-38.12, 7.71, 83.98)  COM (-39.39, 11.19, 80.34)",
                     rows[16]);

        var bare = new HavokRigidBody { Id = 42 };
        Assert.Equal(" 42  body 42           frame (0.00, 0.00, 0.00)  COM no measured COM",
                     SkeletonView.FrameTableRow(bare));
    }

    [Fact]
    public void BodyFramePinsPersistTogetherAndClear()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var view = new SkeletonView();
        view.SetBodies(model);
        view.ShowFrames = true;
        view.ToggleBodyPinForTest(0);
        view.ToggleBodyPinForTest(1);
        view.ToggleBodyPinForTest(2);
        Assert.Equal(new[] { 0, 1, 2 }, view.PinnedBodyIds.OrderBy(x => x).ToArray());
        Assert.Equal(-1, view.HoveredBodyId);

        view.ToggleBodyPinForTest(1);
        Assert.Equal(new[] { 0, 2 }, view.PinnedBodyIds.OrderBy(x => x).ToArray());

        view.ToggleBodyPinForTest(0);
        view.ToggleBodyPinForTest(2);
        Assert.Empty(view.PinnedBodyIds);

        view.ToggleBodyPinForTest(1);
        view.Reset();
        Assert.Empty(view.PinnedBodyIds);
        Assert.False(view.ShowFrames);
    }

    [Fact]
    public void ConstraintHoverLabelShowsMeasuredLimits()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var ragdoll = Assert.Single(model!.Constraints, c => c.Kind == HavokConstraintKind.Ragdoll && c.Id == 0);
        string ragdollLabel = SkeletonView.ConstraintLabel(ragdoll, model.Bodies);
        Assert.Contains("ragdoll: ", ragdollLabel);
        Assert.Contains(" -> ", ragdollLabel);
        Assert.Contains("twist -15.0..15.0 deg", ragdollLabel);
        Assert.Contains("cone 30.0 deg", ragdollLabel);

        var hinge = Assert.Single(model.Constraints, c => c.Kind == HavokConstraintKind.LimitedHinge && c.BodyA == 4);
        string hingeLabel = SkeletonView.ConstraintLabel(hinge, model.Bodies);
        Assert.Contains("limited hinge: ", hingeLabel);
        Assert.Contains("hinge -110.0..1.8 deg", hingeLabel);

        var bare = new HavokConstraint
        {
            Id = 99,
            Kind = HavokConstraintKind.Hinge,
            BodyA = 0,
            BodyB = 1,
            MinAngle = 0,
            MaxAngle = 0,
        };
        string bareLabel = SkeletonView.ConstraintLabel(bare, new List<HavokRigidBody>());
        Assert.Contains("no measured limits", bareLabel);
        Assert.Contains("body 0 -> body 1", bareLabel);
    }

    [Fact]
    public void RagdollBodyFitAndFrameStubAreMeasurable()
    {
        var model = HavokPhysicsExtractor.TryExtract(File.ReadAllBytes(TestFixtures.Path(Skeleton)));
        Assert.NotNull(model);

        var min = new Vector3(float.MaxValue);
        var max = new Vector3(float.MinValue);
        foreach (var body in model!.Bodies)
        {
            var shape = model.Shapes.FirstOrDefault(s => s.Id == body.ShapeId);
            if (shape != null)
                foreach (var vertex in shape.Vertices)
                {
                    var world = body.Position + Vector3.Transform(vertex, body.Rotation);
                    min = Vector3.Min(min, world);
                    max = Vector3.Max(max, world);
                }
            min = Vector3.Min(min, body.Position);
            max = Vector3.Max(max, body.Position);
        }

        var size = max - min;
        float fit = Math.Max(0.001f, Math.Max(size.X, Math.Max(size.Y, size.Z)) * 0.5f);
        float stub = Math.Max(0.5f, fit * 0.06f);

        AssertNear(60.237434f, fit, 2e-2f);
        AssertNear(3.614246f, stub, 2e-3f);
        Assert.Equal(3.614246f, Math.Max(0.5f, fit * 0.06f), 3);
    }

    [Fact]
    public void ValidatorFlagsBrokenAnimationMappings()
    {
        var model = new HavokRagdollModel
        {
            Skeleton = new HavokRagdollSkeleton { Name = "physics" },
            AnimationSkeleton = new HavokRagdollSkeleton { Name = "animation" },
        };
        model.Skeleton!.BoneNames.AddRange(new[] { "A", "B" });
        model.Skeleton.ParentIndices.AddRange(new[] { -1, 0 });
        model.Skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(0, 0, 0), Quaternion.Identity, Vector3.One));
        model.Skeleton.ReferencePose.Add(new HkxBonePose(new Vector3(0, 0, 1), Quaternion.Identity, Vector3.One));
        model.AnimationSkeleton!.BoneNames.AddRange(new[] { "X" });
        model.AnimationSkeleton.ParentIndices.Add(-1);
        model.AnimationSkeleton.ReferencePose.Add(new HkxBonePose(Vector3.Zero, Quaternion.Identity, Vector3.One));

        model.Mappings.Add(new HavokRagdollMapping(7, 0, Vector3.Zero, Quaternion.Identity));

        var findings = HavokPhysicsValidator.Check(model);
        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("outside the animation skeleton"));

        Assert.Contains(findings, f =>
            f.Level == HavokPhysicsValidationLevel.Warning && f.Message.Contains("not covered"));

        model.Mappings.Add(new HavokRagdollMapping(1, 0, Vector3.Zero, Quaternion.Identity));
        var duplicate = HavokPhysicsValidator.Check(model);
        Assert.Contains(duplicate, f =>
            f.Level == HavokPhysicsValidationLevel.Error && f.Message.Contains("mapped more than once"));
    }
}
