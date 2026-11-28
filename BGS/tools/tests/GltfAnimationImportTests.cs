using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
using BehaviourStudio.App;
using OpenCommonwealth.Services.Hkx;
using Xunit;
using Xunit.Abstractions;
using static BehaviourStudio.Tests.GltfTestFiles;

namespace BehaviourStudio.Tests;

public sealed class GltfAnimationImportTests
{
    private const float PositionTolerance = 1e-3f;
    private const float RotationTolerance = 1e-4f;
    private const float ScaleTolerance = 1e-4f;
    private const float Metre = GltfAnimationImport.MetresPerUnit;

    private readonly ITestOutputHelper _out;

    public GltfAnimationImportTests(ITestOutputHelper output) => _out = output;

    // Detects: the Havok-to-glTF axis, unit, rest-pose, per-bone track mapping or root motion split drifts, so an exported vanilla clip no longer re-imports to the poses BGS places from its source.
    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void VanillaClipRoundTripsPerBoneAndFrame(bool binary)
    {
        var skeleton = GltfTestFiles.Skeleton();
        var source = GltfTestFiles.PairedKill();
        var motion = RootMotion.Read(GltfTestFiles.PairedKillPath);
        var (json, bin) = GltfTestFiles.Export(skeleton, source, embedded: !binary, motion);
        using var work = new GltfWork();
        string path = work.Write(binary ? "clip.glb" : "clip.gltf",
                                 binary ? GltfTestFiles.Glb(json, bin) : Encoding.UTF8.GetBytes(json));

        var result = GltfAnimationImport.Build(GltfScene.Read(path), skeleton);
        var imported = result.Animation;

        Assert.Empty(result.Report.RestMismatch);
        Assert.Equal(source.NumFrames, imported.NumFrames);
        Assert.Equal(source.Duration, imported.Duration, 4);
        Assert.Equal(source.NumFrames, result.Motion.Samples.Count);
        var (motionPosition, motionTurn) = GltfTestFiles.CompareMotion(motion, result.Motion);
        Assert.True(motionPosition <= PositionTolerance, $"root travel off by {motionPosition} units");
        Assert.True(motionTurn <= RotationTolerance, $"root turn off by {motionTurn} rad");
        var (position, rotation, scale) = GltfTestFiles.ComparePlaced(skeleton, source, motion, imported, result.Motion);

        _out.WriteLine($"{(binary ? "glb" : "gltf")}: worst position {position:G3} units, rotation {rotation:G3} rad, scale {scale:G3}");
        Assert.True(position <= PositionTolerance, $"position off by {position} units");
        Assert.True(rotation <= RotationTolerance, $"rotation off by {rotation} rad");
        Assert.True(scale <= ScaleTolerance, $"scale off by {scale}");
    }

    // Detects: glTF metres are read without the Y-up to Z-up root change, the metres-to-units factor, or the armature offset, or the root's travel and turn are left in the track, so a hand-written clip lands in the wrong place or faces the wrong way.
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void HandWrittenGltfImportsWithTheGltfToHavokConvention(bool binary)
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(), binary);

        var result = GltfAnimationImport.Build(GltfScene.Read(path), skeleton);
        var animation = result.Animation;

        Assert.Equal(31, animation.NumFrames);
        Assert.Equal(1f, animation.Duration, 5);
        Assert.Equal(GltfAnimationImport.MinimumFrameRate, result.Report.FrameRate);
        Assert.Equal(new[] { "Root", "COM" }, result.Report.Animated);
        Assert.Contains("Spine1", result.Report.ReferenceOnly);
        Assert.Empty(result.Report.Unmapped);
        Assert.Equal(new[] { "Root", "COM" }, result.Report.RestMismatch);

        Vector3 Root(int frame) => animation.Tracks[0].Translations[frame];
        Assert.Equal(0.5f / Metre, Root(0).Z, 3);
        Assert.Equal(0.55f / Metre, Root(15).Z, 3);
        Assert.Equal(0.6f / Metre, Root(30).Z, 3);
        Assert.True(Root(15).X * Root(15).X + Root(15).Y * Root(15).Y < 1e-6f, "root moved off the Havok Z axis");

        Assert.Equal(0f, result.Motion.Samples[14].TurnRadians, 4);
        Assert.Equal(MathF.PI / 2, result.Motion.Samples[15].TurnRadians, 4);
        Assert.True(result.Motion.Distance < 1e-4f, $"the root travels {result.Motion.Distance} units, not none");
        var before = Vector3.TransformNormal(Vector3.UnitX, Matrix4x4.CreateFromQuaternion(animation.Tracks[0].Rotations[14]));
        var after = Vector3.TransformNormal(Vector3.UnitX, Matrix4x4.CreateFromQuaternion(animation.Tracks[0].Rotations[15]));
        AssertNear(Vector3.UnitX, before);
        AssertNear(Vector3.UnitX, after);

        var com = animation.Tracks[1].Translations;
        Assert.Equal(0.2f / Metre, com[14].Y, 3);
        Assert.Equal(0.4f / Metre, com[15].Y, 3);
        Assert.True(com[15].X * com[15].X + com[15].Z * com[15].Z < 1e-6f, "a child bone was converted like the root");

        int spine = skeleton.BoneNames.IndexOf("Spine1");
        Assert.Equal(skeleton.ReferencePose[spine].Translation, animation.Tracks[spine].Translations[7]);
        Assert.Equal(skeleton.ReferencePose[spine].Rotation, animation.Tracks[spine].Rotations[7]);
    }

    // Regression: the fixture skeleton's axes or height change, so the Z-up, +Y-forward, human-scale reading the conversion depends on stops holding.
    [Fact]
    public void FixtureSkeletonIsZUpForwardYAtHumanScale()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var rest = AnimationPose.ReferencePose(skeleton);
        Vector3 At(string name) => rest.Bones.Single(bone => bone.Name == name).Position;

        var head = At("Head");
        var foot = At("LLeg_Foot");
        var toe = At("LLeg_Toe1");
        var hand = At("LArm_Hand");
        Assert.True(head.Z - foot.Z > 100f, $"head {head.Z} is not far above foot {foot.Z}, so +Z is not up");
        Assert.True(toe.Y - foot.Y > 5f, $"toe {toe.Y} is not ahead of foot {foot.Y}, so +Y is not forward");
        Assert.True(hand.X < -20f, $"left hand {hand.X} is not on -X, so +X is not right");
        Assert.InRange(head.Z * Metre, 1.5f, 2.0f);
    }

    // Regression: the exported fixture rest pose stops standing upright in glTF or faces +Z, so a Blender import shows the character lying down or backwards.
    [Fact]
    public void ExportedRestPoseStandsUpAndFacesMinusZ()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var (json, _) = GltfTestFiles.Export(skeleton, null, embedded: true);
        var world = GltfTestFiles.WorldPositions(json);

        Assert.True(world["Head"].Y - world["LLeg_Foot"].Y > 1.5f, "head is not above the foot in glTF +Y");
        Assert.True(world["LLeg_Toe1"].Z < world["LLeg_Foot"].Z - 0.05f, "toe is not forward, which is glTF -Z");
        Assert.True(world["LArm_Hand"].X < 0f, "left hand is not on glTF -X");
    }

    // Detects: CUBICSPLINE keys read as steps or as straight lines between keys, instead of the Hermite curve their tangents define.
    [Fact]
    public void ResamplesCubicSplineKeysAlongTheirTangents()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var animation = GltfAnimationImport.Build(GltfScene.Read(work.Tiny(new TinyOptions(Cubic: true), binary: false)),
                                                  skeleton).Animation;

        var com = animation.Tracks[1].Translations;
        const float Tolerance = 1e-3f;
        Assert.True(MathF.Abs(com[7].Y - (0.2f + 0.4f * 7 / 30f) / Metre) < Tolerance,
                    $"frame 7 reads {com[7].Y * Metre} m, not the 0.29333 m the tangent line gives");
        Assert.True(MathF.Abs(com[15].Y - 0.4f / Metre) < Tolerance, "the last key is not held at frame 15");
        Assert.True(MathF.Abs(com[30].Y - 0.4f / Metre) < Tolerance, "the end is not held after the last key");
    }

    [Fact]
    public void EvaluatesCubicRotationKeysAsStoredInsteadOfShortestPath()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var animation = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(CubicRotation: true), binary: false)), skeleton).Animation;

        var start = new Quaternion(0f, 0f, 0f, 1f);
        var startTangent = new Quaternion(0f, 0f, 0.5f, 0f);
        var endTangent = new Quaternion(0f, 0f, -0.5f, 0f);
        var end = new Quaternion(0f, 0f, -MathF.Sin(MathF.PI / 3), -MathF.Cos(MathF.PI / 3));
        Assert.True(Quaternion.Dot(start, end) < 0f, "the fixture keys no longer have a negative dot product");

        float s = (7 / 30f) / 0.5f, s2 = s * s, s3 = s2 * s;
        Quaternion Weighted(float weight, Quaternion q) => new(weight * q.X, weight * q.Y, weight * q.Z, weight * q.W);
        var spec = Quaternion.Normalize(
            Weighted(2 * s3 - 3 * s2 + 1, start) + Weighted((s3 - 2 * s2 + s) * 0.5f, startTangent) +
            Weighted(-2 * s3 + 3 * s2, end) + Weighted((s3 - s2) * 0.5f, endTangent));

        var baked = animation.Tracks[1].Rotations[7];
        Assert.True(1f - MathF.Abs(Quaternion.Dot(baked, spec)) < 1e-5f,
                    $"frame 7 bakes {baked}, not the spec orientation {spec}");
    }

    [Fact]
    public void GltfImportSummaryReportsIgnoredChannels()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var imported = BuildTiny(skeleton);
        var written = GltfAnimationImport.Write(File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton);
        var ignored = imported with { Report = imported.Report with { WeightChannels = 2, NonBoneChannels = 1 } };

        string summary = MainWindow.GltfImportSummary(ignored, written);
        Assert.Contains("Morph-weight channels ignored: 2", summary, StringComparison.Ordinal);
        Assert.Contains("Non-bone channels ignored: 1", summary, StringComparison.Ordinal);
        Assert.Contains("dropped", summary, StringComparison.Ordinal);
        Assert.DoesNotContain("dropped", MainWindow.GltfImportSummary(imported, written), StringComparison.Ordinal);
    }

    // Regression: an animated bone the skeleton does not name is dropped or paired with a guessed bone instead of refused by name.
    [Fact]
    public void RefusesAnAnimatedBoneTheSkeletonDoesNotHave()
    {
        string message = Refusal(new TinyOptions(Com: "Helper"));
        Assert.Contains("does not have: Helper", message, StringComparison.Ordinal);
    }

    // Regression: a Blender-style armature node carrying the Y-up export rotation stops cancelling the root conversion, so a rig authored in Havok's frame lands rotated.
    [Fact]
    public void BlenderArmatureYUpRotationCancelsTheRootConversion()
    {
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(ArmatureYUp: true), binary: false);
        var animation = GltfAnimationImport.Build(GltfScene.Read(path), GltfTestFiles.Skeleton()).Animation;

        var rotation = Matrix4x4.CreateFromQuaternion(animation.Tracks[0].Rotations[0]);
        AssertNear(Vector3.UnitX, Vector3.TransformNormal(Vector3.UnitX, rotation));
        AssertNear(Vector3.UnitY, Vector3.TransformNormal(Vector3.UnitY, rotation));
        Assert.True(animation.Tracks[0].Translations[0].Length() < 1e-4f, "the root moved at frame 0");
    }

    // Regression: a joint whose glTF parent differs from the skeleton's parent is paired by name anyway, so its motion is applied in the wrong frame.
    [Fact]
    public void RefusesAParentThatDiffersFromTheSkeleton()
    {
        string message = Refusal(new TinyOptions(RootParentsCom: false));
        Assert.Contains("sits under nothing in the glTF but under Root", message, StringComparison.Ordinal);
    }

    // Regression: animation on a non-bone node above the bones is ignored, so the bones move without the motion the file gave them.
    [Fact]
    public void RefusesAnimatedNodeAboveTheBones()
    {
        string message = Refusal(new TinyOptions(ArmatureAnimated: true));
        Assert.Contains("is not a bone", message, StringComparison.Ordinal);
    }

    // Detects: a file with two animations has the first one silently imported, or the refusal does not list the names the caller can pick from.
    [Fact]
    public void RefusesMoreThanOneAnimationAndListsTheNames()
    {
        string message = Refusal(new TinyOptions(TwoAnimations: true));
        Assert.Contains("2 animations (tiny, again)", message, StringComparison.Ordinal);
    }

    // Detects: the animation the caller names is not the one imported, or a name the file does not hold is not refused with the names it does hold.
    [Fact]
    public void ImportsTheNamedAnimationAndRefusesAMissingName()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(TwoAnimations: true), binary: false));

        Assert.Equal(31, GltfAnimationImport.Build(scene, skeleton, "again").Animation.NumFrames);
        var error = Assert.Throws<InvalidOperationException>(() => GltfAnimationImport.Build(scene, skeleton, "missing"));
        Assert.Contains("no animation named 'missing'. It holds: tiny, again.", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RefusesSeveralSkinsWithoutAChoiceAndListsThem()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(TwoSkins: true), binary: false));

        var error = Assert.Throws<InvalidOperationException>(() => GltfAnimationImport.Build(scene, skeleton));
        Assert.Contains("holds 2 skins: 0 (Armor), 1 (Body).", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ImportsTheChosenSkinsJointsAndNamesItsTracks()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(TwoSkins: true), binary: false));

        foreach (string choice in new[] { "1", "Body" })
        {
            var result = GltfAnimationImport.Build(scene, skeleton, skinChoice: choice);

            Assert.Equal(new[] { "Root", "COM" }, result.Report.Animated);
            Assert.Equal(new[] { "Prop" }, result.Report.Unmapped);
        }
    }

    [Fact]
    public void RefusesAnimatedJointThatBelongsOnlyToAnotherSkin()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(TwoSkins: true), binary: false));

        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Build(scene, skeleton, skinChoice: "Armor"));
        Assert.Contains("node 'COM' is animated and is a joint of another skin", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RefusesASkinChoiceThatTwoSkinsShareByTheirRootJoint()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(TwoSkins: true), binary: false));

        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Build(scene, skeleton, skinChoice: "Root"));
        Assert.Contains("'Root' matches 2 skins, so the choice would be a guess", error.Message, StringComparison.Ordinal);
    }

    // Regression: a template whose tracks are a subset of the skeleton's bones is refused, although its binding names the bone each track drives.
    [Fact]
    public void WritesATemplateWhoseTracksAreASubsetOfTheBones()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var template = File.ReadAllBytes(GltfTestFiles.PairedKillPath);
        var imported = BuildTiny(skeleton);

        var written = GltfAnimationImport.Write(template, imported, skeleton);

        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        Assert.Equal(94, written.Tracks);
        Assert.Equal(94, back.Tracks.Count);
        Assert.Equal(new HkxBinaryReader().ReadAnimation(template).TrackToBoneIndices, back.TrackToBoneIndices);
    }

    // Regression: an animated bone the template has no track for is written onto no track, or onto a neighbouring track, instead of refused by name.
    [Fact]
    public void RefusesAnAnimatedBoneTheTemplateHasNoTrackFor()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var source = GltfTestFiles.PairedKill();
        var withCamTarget = new HkxAnimationData
        {
            Duration = source.Duration,
            NumFrames = source.NumFrames,
            NumTracks = 95,
            FrameDuration = source.FrameDuration,
            BoneNames = source.BoneNames,
            TrackToBoneIndices = Enumerable.Range(0, 95).ToList(),
            Tracks = source.Tracks.Append(source.Tracks[0]).ToList(),
        };
        var (json, bin) = GltfTestFiles.Export(skeleton, withCamTarget, embedded: false);
        using var work = new GltfWork();
        var imported = GltfAnimationImport.Build(GltfScene.Read(work.Write("camtarget.glb", GltfTestFiles.Glb(json, bin))), skeleton);

        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Write(File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton));
        Assert.Contains("CamTarget", error.Message, StringComparison.Ordinal);
        Assert.Contains("no track for", error.Message, StringComparison.Ordinal);
    }

    // Detects: a template's root-motion reference frame stays attached to a new clip, or the glTF root's turn is not written into that frame, so the clip does not turn where its bones do.
    [Fact]
    public void WritesTheGltfRootTurnIntoTheTemplatesReferenceFrame()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var template = File.ReadAllBytes(GltfTestFiles.PairedKillPath);
        Assert.True(RootMotion.Read(template).Samples.Count > 0, "the template must carry motion for this test");
        var imported = BuildTiny(skeleton);

        var written = GltfAnimationImport.Write(template, imported, skeleton);

        Assert.True(written.MotionReplaced);
        var motion = RootMotion.Read(written.Bytes);
        Assert.Equal(imported.Animation.NumFrames, motion.Samples.Count);
        Assert.True(motion.Distance < 1e-4f, $"the root travels {motion.Distance} units, not none");
        Assert.Equal(MathF.PI / 2, motion.Samples[15].TurnRadians, 4);
        Assert.Equal(imported.Animation.Duration, motion.Duration, 4);
    }

    [Fact]
    public void RefusesATemplateWhoseRootMotionFrameIsNotZUp()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var imported = BuildTiny(skeleton);
        var template = SyntheticAnimation.Clip(5, new[] { (0.02f, "start") },
            new[] { new RootMotion.Sample(Vector3.Zero, 0f), new RootMotion.Sample(Vector3.Zero, 0f) }, Vector3.UnitY);

        var error = Assert.Throws<InvalidOperationException>(() => GltfAnimationImport.Write(template, imported, skeleton));
        Assert.Contains("not Z", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RefusesATemplateWhoseExtractedMotionIsAnUnsupportedFrameKind()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var imported = BuildTiny(skeleton);
        var clip = SyntheticAnimation.Clip(5, new[] { (0.02f, "start") },
            new[] { new RootMotion.Sample(Vector3.Zero, 0f), new RootMotion.Sample(Vector3.Zero, 0f) });
        var image = PackfileImage.Read(clip);
        var frame = new PackfileObjects(image).Instances.Single(i => i.ClassName == NativeAnimation.ReferenceFrameClass);
        image.Section("__data__")!.Data[frame.Offset + 16] = 2;
        var template = image.Rebuild();
        Assert.Equal("hkaDefaultAnimatedReferenceFrame, frameType 2", RootMotion.Read(template).UnsupportedReason);

        var error = Assert.Throws<InvalidOperationException>(() => GltfAnimationImport.Write(template, imported, skeleton));
        Assert.Contains("frameType 2", error.Message, StringComparison.Ordinal);
    }

    // Detects: glTF animation extras annotations are dropped, copied from the template, or an annotation past the end of the clip is written past it instead of refused by name.
    [Fact]
    public void WritesGltfAnnotationsAndRefusesOneOutsideTheClip()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        var scene = GltfScene.Read(work.Tiny(new TinyOptions(Notes: new[] { (0.5f, "HitFrame"), (0.25f, "footstep") }),
                                             binary: false));
        var imported = GltfAnimationImport.Build(scene, skeleton);
        Assert.Equal(new[] { "footstep", "HitFrame" }, imported.Animation.Annotations.Select(note => note.Text));

        var written = GltfAnimationImport.Write(File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton);

        Assert.Equal(7, written.AnnotationsCleared);
        Assert.Equal(2, written.AnnotationsWritten);
        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        Assert.Equal(new[] { "footstep", "HitFrame" }, back.Annotations.Select(note => note.Text));
        Assert.True(MathF.Abs(back.Annotations[0].Time - 0.25f) < 1e-4f, $"footstep reads at {back.Annotations[0].Time}");

        var late = GltfScene.Read(work.Tiny(new TinyOptions(Notes: new[] { (2f, "late") }), binary: false));
        var error = Assert.Throws<InvalidOperationException>(() => GltfAnimationImport.Build(late, skeleton));
        Assert.Contains("'late' at 2.000 s is outside the clip", error.Message, StringComparison.Ordinal);
    }

    // Detects: a root that starts away from the origin, turns and tilts is split into a motion and a track that place a different pose, because the rebasing to frame 0 or the turn-and-tilt quaternion order is wrong.
    [Fact]
    public void SplitsARootThatStartsAwayFromTheOriginAndTurns()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var source = GltfTestFiles.PairedKill();
        source.Tracks[0].RotationAnimated = true;
        for (int f = 0; f < source.NumFrames; f++)
            source.Tracks[0].Rotations[f] = Quaternion.Normalize(
                Quaternion.CreateFromAxisAngle(Vector3.UnitX, 0.2f) * source.Tracks[0].Rotations[f]);
        var motion = new RootMotion.Motion { Duration = source.Duration };
        for (int f = 0; f < source.NumFrames; f++)
            motion.Samples.Add(new RootMotion.Sample(new Vector3(5f + 0.3f * f, 3f + 0.2f * f, 0), 0.6f + 0.02f * f));
        var (json, bin) = GltfTestFiles.Export(skeleton, source, embedded: false, motion);
        using var work = new GltfWork();
        var result = GltfAnimationImport.Build(GltfScene.Read(work.Write("placed.glb", GltfTestFiles.Glb(json, bin))), skeleton);

        var (position, rotation, _) = GltfTestFiles.ComparePlaced(skeleton, source, motion, result.Animation, result.Motion);
        Assert.True(position <= PositionTolerance, $"placed position off by {position} units");
        Assert.True(rotation <= RotationTolerance, $"placed rotation off by {rotation} rad");
        Assert.Equal(Vector3.Zero, result.Motion.Samples[0].Position);
        Assert.Equal(0f, result.Motion.Samples[0].TurnRadians);
    }

    // Detects: a root that turns half a turn about a horizontal axis has no turn about up, so its root motion is guessed instead of refused by name.
    [Fact]
    public void RefusesARootThatTurnsHalfATurnAboutAHorizontalAxis()
    {
        string message = Refusal(new TinyOptions(RootHalfTurn: true));
        Assert.Contains("turns half a turn about a horizontal axis at frame 15", message, StringComparison.Ordinal);
    }

    // Detects: a written clip reads back with bones, frames or values shifted, with error above the encoder's budget, or with its root turn left out of the placed pose.
    [Fact]
    public void WrittenClipReadsBackWithinTheEncoderBudget()
    {
        var full = GltfTestFiles.Skeleton();
        int bone = full.BoneNames.IndexOf("LLeg_Calf");
        var skeleton = GltfTestFiles.Single(full, bone);
        var source = GltfTestFiles.OneTrack(GltfTestFiles.PairedKill(), bone);
        Assert.True(source.Tracks[0].Rotations.Any(q => q != source.Tracks[0].Rotations[0]),
                    "the chosen bone does not rotate, so the spline fit is not exercised");
        var (json, bin) = GltfTestFiles.Export(skeleton, source, embedded: false);
        using var work = new GltfWork();
        var imported = GltfAnimationImport.Build(GltfScene.Read(work.Write("root.glb", GltfTestFiles.Glb(json, bin))), skeleton);

        var template = SyntheticAnimation.Clip(5, new[] { (0.02f, "start") },
            new[] { new RootMotion.Sample(Vector3.Zero, 0f), new RootMotion.Sample(Vector3.Zero, 0f) });
        var written = GltfAnimationImport.Write(template, imported, skeleton);

        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        Assert.Equal(NativeAnimation.SplineClass, back.AnimationClass);
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Equal(1, written.AnnotationsCleared);
        Assert.Empty(back.Annotations);
        var (position, rotation, _) = GltfTestFiles.ComparePlaced(skeleton, source, null, back, RootMotion.Read(written.Bytes));

        _out.WriteLine($"written: worst position {position:G3} units, rotation {rotation:G3} rad; encoder reports {written.PositionError:G3} and {written.RotationError:G3}");
        Assert.True(position <= 0.01f, $"position off by {position} units");
        Assert.True(rotation <= 0.001f, $"rotation off by {rotation} rad");
    }

    // Regression: an external buffer URI that escapes the glTF's folder reads a file beside or above it, through ../ or percent-encoding.
    [Theory]
    [InlineData("../secret.bin")]
    [InlineData("..%2Fsecret.bin")]
    [InlineData("..%5Csecret.bin")]
    public void RefusesAnExternalBufferThatEscapesTheGltfFolder(string uri)
    {
        using var work = new GltfWork();
        string folder = Directory.CreateDirectory(work.At("gltf")).FullName;
        work.Write("secret.bin", new byte[] { 1, 2, 3, 4 });

        var error = Assert.Throws<NotSupportedException>(() => GltfScene.Parse(BufferJson(uri), folder));
        Assert.Contains("outside the glTF's folder", error.Message, StringComparison.Ordinal);
    }

    // Regression: a percent-encoded absolute path reads a file anywhere on disk, because the rooted check ran before the path was unescaped.
    [Fact]
    public void RefusesAPercentEncodedAbsolutePath()
    {
        using var work = new GltfWork();
        string folder = Directory.CreateDirectory(work.At("gltf")).FullName;
        string secret = work.Write("secret.bin", new byte[] { 1, 2, 3, 4 });

        var error = Assert.Throws<NotSupportedException>(() =>
            GltfScene.Parse(BufferJson(Uri.EscapeDataString(secret)), folder));
        Assert.Contains("must be a relative file beside the glTF", error.Message, StringComparison.Ordinal);
    }

    // Regression: the containment check refuses a buffer that really sits beside the glTF, which would make every relative-buffer .gltf unreadable.
    [Fact]
    public void ReadsAnExternalBufferBesideTheGltf()
    {
        using var work = new GltfWork();
        string folder = Directory.CreateDirectory(work.At("gltf")).FullName;
        File.WriteAllBytes(Path.Combine(folder, "buffer 0.bin"), new byte[] { 1, 2, 3, 4 });

        var scene = GltfScene.Parse(BufferJson("buffer%200.bin"), folder);
        Assert.Empty(scene.Nodes);
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void RefusesAViewThatRunsPastTheBufferDeclaredLength(bool binary)
    {
        var bin = new byte[12];
        string buffer = binary
            ? "{\"byteLength\":10}"
            : "{\"byteLength\":10,\"uri\":\"data:application/octet-stream;base64," + Convert.ToBase64String(bin) + "\"}";
        string json = "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"name\":\"Root\"}]," +
            "\"animations\":[{\"samplers\":[{\"input\":0,\"output\":0,\"interpolation\":\"LINEAR\"}]," +
            "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}]," +
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"}]," +
            "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}],\"buffers\":[" + buffer + "]}";
        byte[] file = binary ? Glb(json, bin) : Encoding.UTF8.GetBytes(json);

        var error = Assert.Throws<InvalidDataException>(() => GltfScene.Parse(file, "."));
        Assert.Contains("a buffer view runs past the end of its buffer", error.Message, StringComparison.Ordinal);
    }

    // Regression: malformed glTF JSON escapes as a JsonException, which the CLI does not catch, so the process crashes instead of refusing the file.
    [Fact]
    public void RefusesMalformedJsonAsInvalidData()
    {
        using var work = new GltfWork();
        string folder = Directory.CreateDirectory(work.At("gltf")).FullName;

        var error = Assert.Throws<InvalidDataException>(() =>
            GltfScene.Parse(Encoding.UTF8.GetBytes("{ \"nodes\": [ "), folder));
        Assert.Contains("malformed", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RefusesAChannelWithoutATargetInsteadOfSkippingIt()
    {
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(TargetlessChannel: true), binary: false);

        var error = Assert.Throws<InvalidDataException>(() => GltfScene.Read(path));
        Assert.Contains("no target", error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(0f)]
    [InlineData(1e-7f)]
    public void RefusesANodeRotationWithNoDirectionInsteadOfIdentity(float w)
    {
        var skeleton = GltfTestFiles.Skeleton();
        var rest = skeleton.ReferencePose[skeleton.BoneNames.IndexOf("COM")];
        rest.Rotation = new Quaternion(0f, 0f, 0f, w);
        using var work = new GltfWork();

        var error = Assert.Throws<InvalidDataException>(() =>
            GltfScene.Read(work.Tiny(new TinyOptions(ComRest: rest), binary: false)));
        Assert.Contains("rotation is zero, tiny or not finite", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RefusesAZeroRotationKeyInsteadOfBakingIdentity()
    {
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(ZeroRotationKey: true), binary: false);

        var error = Assert.Throws<InvalidDataException>(() => GltfScene.Read(path));
        Assert.Contains("key 1 is zero", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void NormalisesAValidNonUnitNodeRotationBeforeTheRestCheck()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var rest = skeleton.ReferencePose[skeleton.BoneNames.IndexOf("COM")];
        var q = rest.Rotation;
        rest.Rotation = new Quaternion(2f * q.X, 2f * q.Y, 2f * q.Z, 2f * q.W);
        using var work = new GltfWork();

        var imported = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(ComRest: rest), binary: false)), skeleton);
        Assert.DoesNotContain("COM", imported.Report.RestMismatch);
    }

    // Regression: a clip over the frame-track read-back limit is baked in full before the limit is checked, so memory grows before the clip is refused.
    [Fact]
    public void RefusesAClipOverTheFrameTrackLimitBeforeBaking()
    {
        var skeleton = GltfTestFiles.Skeleton();
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(EndSeconds: 1500f), binary: false);

        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Build(GltfScene.Read(path), skeleton));
        Assert.Contains("4,194,304", error.Message, StringComparison.Ordinal);
        Assert.Contains("45001 frame(s)", error.Message, StringComparison.Ordinal);
    }

    // Regression: channels that share one accessor each keep their own decoded copy, so a rig with many channels multiplies memory by the channel count.
    [Fact]
    public void ChannelsSharingAnAccessorShareOneDecodedArray()
    {
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(), binary: false);

        var channels = GltfScene.Read(path).Animations[0].Channels;
        Assert.Same(channels[1].Times, channels[2].Times);
    }

    [Fact]
    public void RestScaleMismatchIsReportedAndTheSkeletonScaleIsWritten()
    {
        var skeleton = GltfTestFiles.Skeleton();
        int com = skeleton.BoneNames.IndexOf("COM");
        var rest = skeleton.ReferencePose[com];
        var doubled = rest;
        doubled.Scale *= 2f;
        using var work = new GltfWork();

        var matching = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(ComRest: rest), binary: false)), skeleton);
        Assert.DoesNotContain("COM", matching.Report.RestMismatch);

        var scaled = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(ComRest: doubled), binary: false)), skeleton);
        Assert.Contains("COM", scaled.Report.RestMismatch);
        AssertNear(rest.Scale, scaled.Animation.Tracks[com].Scales[0]);
    }

    [Fact]
    public void ReportsSamplingLossForAnOffGridPeakThroughTheSavedClip()
    {
        var skeleton = GltfTestFiles.Skeleton();
        int com = skeleton.BoneNames.IndexOf("COM");
        using var work = new GltfWork();
        var imported = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(OffGridPeak: true), binary: false)), skeleton);

        const float Peak = 0.05f;
        float loss = Peak / 3 / Metre;
        Assert.InRange(imported.Report.Sampling.Position, loss - 0.05f, loss + 0.05f);

        var written = GltfAnimationImport.Write(File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton);
        Assert.True(imported.Report.Sampling.Position > written.PositionError, "the loss is no larger than the encoder's error");
        Assert.Contains("Sampling loss against the glTF curves: 1.1", MainWindow.GltfImportSummary(imported, written),
                        StringComparison.Ordinal);

        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        float savedPeak = back.Tracks[back.TrackToBoneIndices.IndexOf(com)].Translations.Max(t => t.Y);
        float sampledPeak = 2f * Peak / 3f / Metre;
        Assert.InRange(savedPeak, sampledPeak - 0.05f, sampledPeak + 0.05f);
    }

    [Fact]
    public void ReportsSamplingLossForAnOffGridStepThroughTheSavedClip()
    {
        var skeleton = GltfTestFiles.Skeleton();
        int com = skeleton.BoneNames.IndexOf("COM");
        using var work = new GltfWork();
        var imported = GltfAnimationImport.Build(
            GltfScene.Read(work.Tiny(new TinyOptions(OffGridStep: true), binary: false)), skeleton);

        const float Step = 0.05f;
        float loss = Step / 2 / Metre;
        Assert.InRange(imported.Report.Sampling.Position, loss - 0.05f, loss + 0.05f);

        var written = GltfAnimationImport.Write(File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton);
        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        var y = back.Tracks[back.TrackToBoneIndices.IndexOf(com)].Translations;
        Assert.InRange(y[1].Y, -0.05f, 0.05f);
        Assert.InRange(y[2].Y, Step / Metre - 0.05f, Step / Metre + 0.05f);
    }

    [Fact]
    public void RefusesASkeletonWithoutAReferencePoseForEachBone()
    {
        var skeleton = GltfTestFiles.Skeleton();
        skeleton.ReferencePose.Clear();
        using var work = new GltfWork();
        string path = work.Tiny(new TinyOptions(), binary: false);

        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Build(GltfScene.Read(path), skeleton));
        Assert.Contains("reference pose", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AMissingReferencePoseFixupRefusesTheImportAndWritesNothing()
    {
        using var work = new GltfWork();
        string skeletonPath = work.Write("skeleton.hkx", SkeletonWithoutReferencePoseFixup());
        string gltf = work.Tiny(new TinyOptions(), binary: false);
        string output = work.At("written.hkx");

        var error = Assert.Throws<InvalidDataException>(() =>
        {
            var skeleton = new HkxBinaryReader().ReadSkeleton(skeletonPath);
            var imported = GltfAnimationImport.Build(GltfScene.Read(gltf), skeleton);
            File.WriteAllBytes(output, GltfAnimationImport.Write(
                File.ReadAllBytes(GltfTestFiles.PairedKillPath), imported, skeleton).Bytes);
        });
        Assert.Contains("referencePose", error.Message, StringComparison.Ordinal);
        Assert.False(File.Exists(output));
    }

    private static byte[] SkeletonWithoutReferencePoseFixup()
    {
        var image = new PackfileImage { Predicates = new byte[16] };
        foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
        {
            var tag = new byte[20];
            Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
            image.Sections.Add(new PackfileSection { TagBytes = tag });
        }

        var skeleton = NativeAppend.Object(image, "hkaSkeleton");
        var data = image.Section("__data__")!;
        data.AlignData(16);
        int bones = data.AppendData(new byte[16]);
        int bonesField = skeleton.Offset + HavokClasses.Shipped.Field("hkaSkeleton", "bones")!.Offset;
        data.SetLocal(bonesField, bones);
        BitConverter.GetBytes(1).CopyTo(data.Data, bonesField + image.Layout.PointerSize);
        int poseField = skeleton.Offset + HavokClasses.Shipped.Field("hkaSkeleton", "referencePose")!.Offset;
        BitConverter.GetBytes(1).CopyTo(data.Data, poseField + image.Layout.PointerSize);
        FixupOrder.Reorder(image);
        return image.Rebuild();
    }

    private static byte[] BufferJson(string uri) =>
        Encoding.UTF8.GetBytes("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"" + uri + "\",\"byteLength\":4}]}");

    private static GltfAnimationImport.Result BuildTiny(HkxSkeleton skeleton)
    {
        using var work = new GltfWork();
        return GltfAnimationImport.Build(GltfScene.Read(work.Tiny(new TinyOptions(), binary: false)), skeleton);
    }

    private static string Refusal(TinyOptions options)
    {
        using var work = new GltfWork();
        string path = work.Tiny(options, binary: false);
        var error = Assert.Throws<InvalidOperationException>(() =>
            GltfAnimationImport.Build(GltfScene.Read(path), GltfTestFiles.Skeleton()));
        return error.Message;
    }

    private static void AssertNear(Vector3 want, Vector3 got) =>
        Assert.True((want - got).Length() < 1e-4f, $"expected {want} but got {got}");

}
