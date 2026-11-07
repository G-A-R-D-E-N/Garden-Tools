using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Text.Json;
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

    // Regression: the Havok-to-glTF axis, unit, rest-pose or per-bone track mapping drifts, so an exported vanilla clip no longer re-imports to its own poses.
    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void VanillaClipRoundTripsPerBoneAndFrame(bool binary)
    {
        var skeleton = GltfTestFiles.Skeleton();
        var source = GltfTestFiles.PairedKill();
        var (json, bin) = GltfTestFiles.Export(skeleton, source, embedded: !binary);
        using var work = new GltfWork();
        string path = work.Write(binary ? "clip.glb" : "clip.gltf",
                                 binary ? GltfTestFiles.Glb(json, bin) : Encoding.UTF8.GetBytes(json));

        var result = GltfAnimationImport.Build(GltfScene.Read(path), skeleton);
        var imported = result.Animation;

        Assert.Empty(result.Report.RestMismatch);
        Assert.Equal(source.NumFrames, imported.NumFrames);
        Assert.Equal(source.Duration, imported.Duration, 4);
        var trackOf = AnimationPose.TracksByBone(skeleton, source);
        float position = 0, rotation = 0, scale = 0;
        for (int bone = 0; bone < skeleton.BoneNames.Count; bone++)
            for (int frame = 0; frame < source.NumFrames; frame++)
            {
                var want = AnimationPose.Local(skeleton, source, trackOf[bone], bone, frame);
                var got = AnimationPose.Local(skeleton, imported, bone, bone, frame);
                position = MathF.Max(position, (want.Translation - got.Translation).Length());
                rotation = MathF.Max(rotation, Angle(want.Rotation, got.Rotation));
                scale = MathF.Max(scale, (want.Scale - got.Scale).Length());
            }

        _out.WriteLine($"{(binary ? "glb" : "gltf")}: worst position {position:G3} units, rotation {rotation:G3} rad, scale {scale:G3}");
        Assert.True(position <= PositionTolerance, $"position off by {position} units");
        Assert.True(rotation <= RotationTolerance, $"rotation off by {rotation} rad");
        Assert.True(scale <= ScaleTolerance, $"scale off by {scale}");
    }

    // Regression: glTF metres are read without the Y-up to Z-up root change, the metres-to-units factor, or the armature offset, so a hand-written clip lands in the wrong place or faces the wrong way.
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

        var before = Vector3.TransformNormal(Vector3.UnitX, Matrix4x4.CreateFromQuaternion(animation.Tracks[0].Rotations[14]));
        var after = Vector3.TransformNormal(Vector3.UnitX, Matrix4x4.CreateFromQuaternion(animation.Tracks[0].Rotations[15]));
        AssertNear(Vector3.UnitX, before);
        AssertNear(Vector3.UnitY, after);

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

    // Regression: an unsupported interpolation is read as a straight line, returning a curve the modder did not author.
    [Fact]
    public void RefusesCubicSplineWithItsReason()
    {
        string message = Refusal(new TinyOptions(Cubic: true));
        Assert.Contains("CUBICSPLINE", message, StringComparison.Ordinal);
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

    // Regression: a file with two animations has the first one silently imported, so the wrong clip is written.
    [Fact]
    public void RefusesMoreThanOneAnimation()
    {
        string message = Refusal(new TinyOptions(TwoAnimations: true));
        Assert.Contains("2 animations", message, StringComparison.Ordinal);
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

    // Regression: a template's root-motion reference frame stays attached to a new clip, so the imported clip travels with the template's motion.
    [Fact]
    public void ReplacesATemplatesRootMotionWithAStationaryFrame()
    {
        var skeleton = GltfTestFiles.Skeleton();
        var template = File.ReadAllBytes(GltfTestFiles.PairedKillPath);
        Assert.True(RootMotion.Read(template).Samples.Count > 0, "the template must carry motion for this test");
        var imported = BuildTiny(skeleton);

        var written = GltfAnimationImport.Write(template, imported, skeleton);

        Assert.True(written.MotionReplaced);
        Assert.Equal(2, written.MotionSamples);
        var motion = RootMotion.Read(written.Bytes);
        Assert.Equal(0f, motion.Distance);
        Assert.All(motion.Samples, sample => Assert.Equal(Vector3.Zero, sample.Position));
        Assert.Equal(imported.Animation.Duration, motion.Duration, 4);
    }

    // Regression: a written clip reads back with bones, frames or values shifted, or with error above the encoder's budget.
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

        var written = GltfAnimationImport.Write(SyntheticAnimation.Clip(5, new[] { (0.02f, "start") }), imported, skeleton);

        var back = new HkxBinaryReader().ReadAnimation(written.Bytes);
        Assert.Equal(NativeAnimation.SplineClass, back.AnimationClass);
        Assert.Equal(source.NumFrames, back.NumFrames);
        Assert.Equal(1, written.AnnotationsCleared);
        Assert.Empty(back.Annotations);
        float position = 0, rotation = 0;
        for (int frame = 0; frame < source.NumFrames; frame++)
        {
            var want = AnimationPose.Local(skeleton, source, 0, 0, frame);
            var got = AnimationPose.Local(skeleton, back, 0, 0, frame);
            position = MathF.Max(position, (want.Translation - got.Translation).Length());
            rotation = MathF.Max(rotation, Angle(want.Rotation, got.Rotation));
        }

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
