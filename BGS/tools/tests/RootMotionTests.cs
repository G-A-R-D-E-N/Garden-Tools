using System;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class RootMotionTests
{
    private static RootMotion.Motion Travel()
    {
        var motion = new RootMotion.Motion { Duration = 2 };
        motion.Samples.Add(new(Vector3.Zero, 0));
        motion.Samples.Add(new(new Vector3(0, 30, 0), MathF.PI / 4));
        motion.Samples.Add(new(new Vector3(-40, 30, 0), MathF.PI / 2));
        return motion;
    }

    private static void Near(Vector3 expected, Vector3 actual) =>
        Assert.True(Vector3.Distance(expected, actual) < 0.0001f, $"{expected} != {actual}");

    [Fact]
    public void InterpolatesComponentsAndAccumulatesCurvedDistance()
    {
        var motion = Travel();
        Near(new Vector3(0, 15, 0), RootMotion.AtTime(motion, 0.5f).Position);
        Assert.Equal(MathF.PI / 8, RootMotion.AtTime(motion, 0.5f).TurnRadians, 5);
        Assert.Equal(70, motion.Distance);
        Assert.Equal(50, motion.Travel.Length());
        Assert.Equal(MathF.PI / 2, motion.Turn, 5);
    }

    [Theory]
    [InlineData(-1, 0)]
    [InlineData(0, 0)]
    [InlineData(2, 1)]
    [InlineData(100, 1)]
    public void QueriesClamp(float time, float fraction)
    {
        var motion = Travel();
        Near(RootMotion.At(motion, fraction).Position, RootMotion.AtTime(motion, time).Position);
    }

    [Theory]
    [InlineData(2, 0)]
    [InlineData(4.5f, 0.5f)]
    [InlineData(-0.5f, 1.5f)]
    public void ExplicitLoopQueriesWrapWithoutAccumulatingPastCycles(float time, float wrapped)
    {
        var motion = Travel();
        Near(RootMotion.AtTime(motion, wrapped).Position, RootMotion.AtTime(motion, time, loop: true).Position);
        Assert.Equal(RootMotion.AtTime(motion, wrapped).TurnRadians,
                     RootMotion.AtTime(motion, time, loop: true).TurnRadians, 5);
    }

    [Fact]
    public void PositiveAngleTurnsForwardLeftAboutUp()
    {
        var motion = Travel();
        Near(-Vector3.UnitX, Vector3.TransformNormal(Vector3.UnitY, RootMotion.TransformAt(motion, 2)));
        motion.Up = Vector3.UnitY;
        Near(Vector3.UnitX, Vector3.TransformNormal(Vector3.UnitZ, RootMotion.TransformAt(motion, 2)));
    }

    [Fact]
    public void SyntheticDefaultFrameParsesThenPlacesWithoutMutatingPose()
    {
        var samples = new[] { new RootMotion.Sample(Vector3.Zero, 0),
                            new RootMotion.Sample(new Vector3(10, 20, 0), MathF.PI / 2) };
        var bytes = SyntheticAnimation.Clip(3, Array.Empty<(float, string)>(), samples);
        var motion = RootMotion.Read(bytes);
        Assert.True(motion.Any);
        Assert.Equal(1f / 30, motion.Duration, 6);
        var pose = new AnimationPose.Pose { Time = motion.Duration, Frame = 2 };
        pose.Bones.Add(new(0, "root", -1, Vector3.UnitY, Quaternion.Identity));
        pose.Bones.Add(new(1, "child", 0, 2 * Vector3.UnitY, Quaternion.Identity));
        pose.Links.Add((0, 1));
        var placed = RootMotion.Place(pose, motion, pose.Time);
        Near(new Vector3(9, 20, 0), placed.Bones[0].Position);
        Near(new Vector3(8, 20, 0), placed.Bones[1].Position);
        Near(-Vector3.UnitX, Vector3.Transform(Vector3.UnitY, placed.Bones[0].Rotation));
        Near(Vector3.UnitY, pose.Bones[0].Position);
        Assert.Equal(pose.Links, placed.Links);
        Assert.Equal(pose.Frame, placed.Frame);
        Near(new Vector3(8, 20, 0), placed.Min);
        Near(new Vector3(9, 20, 0), placed.Max);
    }

    [Theory]
    [InlineData("hkaDefaultAnimatedReferenceFrame", 0, true)]
    [InlineData("hkaDefaultAnimatedReferenceFrame", 1, true)]
    [InlineData("hkaDefaultAnimatedReferenceFrame", 2, false)]
    [InlineData("hkaDefaultAnimatedReferenceFrame", 255, false)]
    [InlineData("hkaAngularReferenceFrame", 0, false)]
    [InlineData("hkaDirectionalReferenceFrame", 0, false)]
    [InlineData("hkaParameterizedReferenceFrame", 0, false)]
    public void FrameKindsHaveExplicitRefusals(string className, int kind, bool supported)
    {
        var crafted = new CraftedPackfile();
        var animation = crafted.Object(SyntheticAnimation.LosslessClass);
        var frame = crafted.Object(className);
        crafted.Attach(animation, "extractedMotion", frame);
        crafted.Data.Data[frame.Offset + 16] = (byte)kind;
        var motion = RootMotion.Read(crafted.Build(out _));
        Assert.Equal(supported, motion.UnsupportedReason == null);
        if (!supported)
        {
            Assert.False(motion.Any);
            Assert.Contains(className, motion.ToString());
            Assert.Contains("not supported", motion.ToString());
            Assert.Equal(Matrix4x4.Identity, RootMotion.TransformAt(motion, 1));
        }
    }

    [Fact]
    public void NullExtractedPointerDoesNotBorrowAnUnrelatedFrame()
    {
        var crafted = new CraftedPackfile();
        crafted.Object(SyntheticAnimation.LosslessClass);
        var frame = crafted.Object("hkaDefaultAnimatedReferenceFrame");
        crafted.Data.Data[frame.Offset + 16] = 2;
        var motion = RootMotion.Read(crafted.Build(out _));
        Assert.False(motion.Any);
        Assert.Null(motion.UnsupportedReason);
    }

    [Fact]
    public void CommittedTurningFixtureHasPositiveAngle()
    {
        string path = System.IO.Path.Combine(AppContext.BaseDirectory, "fixtures", "vanilla", "Meshes", "Actors", "RadRoach", "Animations", "TurnLeft90.hkx");
        var motion = RootMotion.Read(path);
        Assert.True(motion.Any);
        Assert.True(motion.Turn > 0);
        Near(Vector3.Zero, motion.Travel);
    }

    [Theory]
    [InlineData(1000000, true)]
    [InlineData(1000001, false)]
    [InlineData(float.PositiveInfinity, false)]
    [InlineData(float.NaN, false)]
    public void InvalidTravelIsRetainedButNeverPlaced(float distance, bool previewable)
    {
        var motion = new RootMotion.Motion { Duration = 1 };
        motion.Samples.Add(new(Vector3.Zero, 0));
        motion.Samples.Add(new(new Vector3(distance, 0, 0), 0));
        Assert.True(motion.Any);
        Assert.Equal(previewable, motion.Previewable);
        Assert.Equal(!previewable, motion.LooksInvalid);
        Assert.Equal(distance, motion.Distance);
        if (previewable) return;
        Assert.Contains("root motion looks invalid (", motion.ToString());
        Assert.Equal(Vector3.Zero, RootMotion.AtTime(motion, 1).Position);
        Assert.Equal(Matrix4x4.Identity, RootMotion.TransformAt(motion, 1));
        var pose = new AnimationPose.Pose();
        Assert.Same(pose, RootMotion.Place(pose, motion, 1));
    }

    [Fact]
    public void FiniteTravelDoesNotHideNonFiniteAngle()
    {
        var motion = Travel();
        motion.Samples.Add(new(Vector3.Zero, float.NaN));
        Assert.True(float.IsFinite(motion.Distance));
        Assert.False(motion.Previewable);
    }

    [Fact]
    public void MetricsFollowSamplesAddedAfterAQuery()
    {
        var motion = new RootMotion.Motion { Duration = 1 };
        motion.Samples.Add(new(Vector3.Zero, 0));
        Assert.Equal(0, motion.Distance);
        Assert.True(motion.Previewable);
        motion.Samples.Add(new(new Vector3(3, 4, 0), 0));
        Assert.Equal(5, motion.Distance);
    }

    [Fact]
    public void MultiTurnAnglesStayUnwrapped()
    {
        var motion = Travel();
        motion.Samples[^1] = new(Vector3.Zero, -3 * MathF.PI);
        Assert.True(motion.Previewable);
        Assert.Equal(-540, RootMotion.AtTime(motion, motion.Duration).TurnRadians * 180 / MathF.PI, 3);
    }
}
