using System;
using System.Globalization;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class HkxBehaviorGraphTests
{
    private static byte[] Fixture(string underActors)
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            var file = Path.Combine(dir.FullName, "Radicle", "tests", "fixtures", "hkx", "Meshes", "Actors", underActors);
            if (File.Exists(file)) return File.ReadAllBytes(file);
        }

        // Constraint: the committed fixture is restored in CI, so a missing file fails the test instead of passing silently.
        throw new FileNotFoundException("Missing committed HKX fixture " + underActors);
    }

    [Fact]
    public void ALocomotionGraphReadsStatesTransitionsBlendsAndClips()
    {
        var bytes = Fixture("Character/Behaviors/Locomotion_8wayBlend.hkx");
        var file = HkxFile.FromBytes(bytes);
        Assert.Null(file.BehaviorError);
        var graph = file.Behavior!;
        Assert.Equal("Locomotion_8wayBlend.hkb", graph.Name);
        Assert.Equal(26, graph.Events.Length);
        Assert.Equal("walkStart", graph.EventName(5));
        Assert.Equal(9, graph.Variables.Length);
        Assert.Equal("Speed", graph.VariableName(1));
        Assert.Equal(HkbVariableType.Int32, graph.Variables[7].Kind);

        Assert.Equal(2, graph.StateMachines.Length);
        Assert.Equal(5, graph.States.Length);
        var root = graph.StateMachines[0];
        Assert.Equal("LocomotionRoot", graph.NodeAt(root.Node)!.Name);
        var wildcards = graph.WildcardsOf(root).ToArray();
        Assert.Equal(3, wildcards.Length);
        Assert.Equal("walkStart", graph.EventName(wildcards[0].EventId));
        Assert.Equal(0, wildcards[0].ToStateId);
        Assert.Equal(2560u, wildcards[0].Flags);
        Assert.Equal("blend_0.50s_Sync_0.5s", graph.NodeAt(wildcards[0].EffectNode)!.Name);
        Assert.Equal(2, graph.Effects.Length);
        var effect = graph.EffectOf(wildcards[0].EffectNode)!;
        Assert.Equal(0.5f, effect.Duration);
        Assert.Equal(0f, effect.StartFraction);
        Assert.Equal(0u, effect.Flags);
        Assert.Equal(0, effect.EndMode);
        Assert.Equal(0, effect.BlendCurve);
        Assert.Equal(8f / 30f, graph.Effects.Single(e => graph.NodeAt(e.Node)!.Name == "blend_0.50s_Sync_8f").Duration, 5);
        Assert.Null(graph.EffectOf(-1));

        Assert.Equal(12, graph.Blenders.Length);
        Assert.Equal(42, graph.BlendChildren.Length);
        var walkRun = graph.Blenders.Single(b => graph.NodeAt(b.Node)!.Name == "MeleeWalkRunBlend_Forward");
        Assert.True(walkRun.Parametric);
        Assert.Equal("Speed", graph.VariableName(walkRun.BlendParameterVariable));
        Assert.False(walkRun.Resolved);
        Assert.Equal(new[] { 80f, 370f }, graph.ChildrenOf(walkRun).Select(c => c.Weight).ToArray());

        Assert.Equal(34, graph.Clips.Length);
        Assert.Equal(@"Animations\1HM\WalkForward.hkt", graph.Clips[0].Animation);
        Assert.Equal(1, graph.Clips[0].Mode);
    }

    [Fact]
    public void AGraphWhoseOnlyStateSwapsGraphsHasNoClips()
    {
        var bytes = Fixture("DLC03/RadChicken/Behaviors/CritterRootBehavior.hkx");
        var file = HkxFile.FromBytes(bytes);
        var graph = file.Behavior!;
        Assert.Equal("CritterRootBehavior.hkb", graph.Name);
        Assert.Equal(17, graph.Events.Length);
        Assert.Equal("RunSpeedMult", graph.Variables[4].Name);
        Assert.Equal(1f, graph.Variables[4].RealValue);
        Assert.Equal("DefaultState", Assert.Single(graph.States).Name);
        Assert.Empty(graph.Clips);
    }

    [Fact]
    public void LocomotionCyclicBlendWrappersExposeTheirBlenderEdges()
    {
        var bytes = Fixture("Character/Behaviors/Locomotion_8wayBlend.hkx");
        var graph = HkxFile.FromBytes(bytes).Behavior!;
        Assert.Equal(4, graph.Edges.Length);
        Assert.All(graph.Edges, any =>
        {
            Assert.Equal(HkbEdgeRole.Wrapped, any.Role);
            Assert.Equal(HkbNodeKind.CyclicBlendTransition, graph.Nodes[any.Parent].Kind);
            Assert.Equal(HkbNodeKind.Blender, graph.Nodes[any.Child].Kind);
        });
        var walk = Array.FindIndex(graph.Nodes, node => node.Name == "MeleeWalkState_CBT");
        var edge = Assert.Single(graph.EdgesOf(walk));
        Assert.Equal("WalkForwardBlend", graph.Nodes[edge.Child].Name);
        Assert.Equal(0, edge.Slot);
        Assert.False(edge.HasBoneWeights);
    }

    [Fact]
    public void AScalarClipMemberIsListedWrittenAndReadBackByTheGraphReader()
    {
        var bytes = Fixture("Character/Behaviors/Locomotion_8wayBlend.hkx");
        var graph = HkxFile.FromBytes(bytes).Behavior!;
        var clip = graph.Clips[0];
        var node = graph.NodeAt(clip.Node)!.Object;

        var members = HkxScalarEdit.List(bytes, node);
        var speed = members.Single(member => member.Name == "playbackSpeed");
        Assert.Equal(HkxScalarKind.Real, speed.Kind);
        Assert.Equal(clip.PlaybackSpeed, float.Parse(speed.Value, CultureInfo.InvariantCulture));
        var mode = members.Single(member => member.Name == "mode");
        Assert.True(mode.IsEnum);
        Assert.Equal("MODE_LOOPING", mode.EnumName);
        Assert.DoesNotContain(members, member => member.Name == "animationName");

        var written = HkxScalarEdit.Set(bytes, node, "playbackSpeed", "2.5");
        Assert.Equal(bytes.Length, written.Length);
        Assert.NotEqual(bytes, written);
        var after = HkxFile.FromBytes(written).Behavior!;
        Assert.Equal(2.5f, after.Clips[0].PlaybackSpeed);
        Assert.Equal(graph.Clips[1].PlaybackSpeed, after.Clips[1].PlaybackSpeed);
        Assert.Equal(clip.Animation, after.Clips[0].Animation);
        Assert.Equal(bytes, HkxScalarEdit.Set(bytes, node, "playbackSpeed", speed.Value));
    }

    [Fact]
    public void ARefusedScalarWriteThrowsWithTheStatusAndReason()
    {
        var bytes = Fixture("Character/Behaviors/Locomotion_8wayBlend.hkx");
        var graph = HkxFile.FromBytes(bytes).Behavior!;
        var node = graph.NodeAt(graph.Clips[0].Node)!.Object;

        var missing = Assert.Throws<RadicleException>(() => HkxScalarEdit.Set(bytes, node, "noSuchMember", "1"));
        Assert.Equal(Status.NotFound, missing.Status);
        var text = Assert.Throws<RadicleException>(() => HkxScalarEdit.Set(bytes, node, "animationName", "x"));
        Assert.Equal(Status.InvalidArgument, text.Status);
        Assert.Contains("animationName", text.Message);
        var range = Assert.Throws<RadicleException>(() => HkxScalarEdit.Set(bytes, node, "mode", "128"));
        Assert.Equal(Status.InvalidArgument, range.Status);
        Assert.Throws<RadicleException>(() => HkxScalarEdit.List(bytes, graph.Nodes.Length + bytes.Length));
        Assert.Throws<ArgumentOutOfRangeException>(() => HkxScalarEdit.List(bytes, -1));
    }
}
