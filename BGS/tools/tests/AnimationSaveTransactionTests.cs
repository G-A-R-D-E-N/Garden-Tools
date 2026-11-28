using System;
using System.IO;
using System.Linq;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class AnimationSaveTransactionTests
{
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Commit_PublishesVerifiedBytesAndKeepsAnExactBackup(bool spline)
    {
        using var scope = new AnimationScope(spline);
        byte[] source = File.ReadAllBytes(scope.Path);
        int frame = 2;
        var edited = new Vector3(11.5f, -22.25f, 33.75f);
        scope.Animation.Tracks[0].Translations[frame] = edited;

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: frame);

        Assert.True(result.Committed);
        Assert.Equal(spline, result.Spline);
        Assert.NotNull(result.Written);
        Assert.Equal(source, File.ReadAllBytes(scope.Path + ".bak"));
        Assert.False(source.SequenceEqual(File.ReadAllBytes(scope.Path)));

        var reopened = new HkxBinaryReader().ParseHkx(File.ReadAllBytes(scope.Path));
        Assert.Equal(
            spline ? NativeAnimation.SplineClass : NativeAnimation.InterleavedClass,
            reopened.AnimationClass);
        float drift = (reopened.Tracks[0].Translations[frame] - edited).Length();
        Assert.True(drift < (spline ? 0.05f : 0.001f), $"edited frame drifted by {drift}");
        Assert.StartsWith("Saved ", result.Message, StringComparison.Ordinal);
        Assert.Contains(spline ? "spline compressed" : "uncompressed", result.Message,
                        StringComparison.Ordinal);
    }

    // Regression: a change to NativeAnimation.Recompress alters the bytes it writes for existing callers, so saved animations differ from the verified build.
    [Theory]
    [InlineData("Meshes/Actors/Character/Animations/Paired/PairedKill2HMBashKneeAndHead_AttackerLead.hkx",
                "767DDE99C479E6F3B5B14E348A70388AF1925392C2E0DD493E8DA072F5819B3B")]
    [InlineData("Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx",
                "4D8CCF73CFEEC3992A9E1E7160ED3E7674291A66FC20DDA5CE683624BE25C4C9")]
    public void RecompressDefaultOutputIsByteIdenticalToTheVerifiedBuild(string relative, string sha256)
    {
        byte[] bytes = File.ReadAllBytes(TestFixtures.Path(relative));
        var written = NativeAnimation.Recompress(bytes, new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Equal(sha256, Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(written.Bytes)));
    }

    [Fact]
    public void Commit_RejectsASourceChangeInjectedBeforePublication()
    {
        using var scope = new AnimationScope(spline: false);
        byte[] source = File.ReadAllBytes(scope.Path);
        byte[] external = source.ToArray();
        external[^1] ^= 0x01;
        scope.Animation.Tracks[0].Translations[1] = new Vector3(9, 8, 7);

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 1,
            beforeSourceRecheck: () => File.WriteAllBytes(scope.Path, external));

        Assert.False(result.Committed);
        Assert.Equal(external, File.ReadAllBytes(scope.Path));
        Assert.False(File.Exists(scope.Path + ".bak"));
        Assert.Contains("changed on disk", result.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Commit_RejectsAFileThatWasAlreadyChangedAfterOpen()
    {
        using var scope = new AnimationScope(spline: false);
        var opened = DocumentSourceStamp.Capture(scope.Path);
        byte[] external = File.ReadAllBytes(scope.Path);
        external[^1] ^= 0x01;
        File.WriteAllBytes(scope.Path, external);

        var result = AnimationSaveTransaction.Commit(
            scope.Path, scope.Animation, opened, editedTrack: 0, editedFrame: 1);

        Assert.False(result.Committed);
        Assert.Equal(external, File.ReadAllBytes(scope.Path));
        Assert.False(File.Exists(scope.Path + ".bak"));
        Assert.Contains("changed on disk", result.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Commit_NamesThePreviousFileWhenThePromotionToBakFails()
    {
        using var scope = new AnimationScope(spline: false);
        File.WriteAllBytes(scope.Path + ".bak", new byte[] { 9 });
        Directory.CreateDirectory(scope.Path + ".bak.1");
        scope.Animation.Tracks[0].Translations[1] = new Vector3(4, 5, 6);

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 1);

        Assert.True(result.Committed);
        string previous = Assert.Single(
            Directory.EnumerateFiles(Path.GetDirectoryName(scope.Path)!, "*.previous"));
        Assert.Contains(Path.GetFileName(previous), result.Message, StringComparison.Ordinal);
        Assert.DoesNotContain("animation.hkx.bak", result.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Commit_ChecksTheNextSaveAgainstTheBytesThisSaveWrote()
    {
        using var scope = new AnimationScope(spline: false);
        scope.Animation.Tracks[0].Translations[1] = new Vector3(4, 5, 6);
        var first = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 1);
        Assert.True(first.Committed);

        var committed = DocumentSourceStamp.Capture(first.Written!.Bytes);
        byte[] external = File.ReadAllBytes(scope.Path);
        external[^1] ^= 0x01;
        File.WriteAllBytes(scope.Path, external);

        var second = AnimationSaveTransaction.Commit(
            scope.Path, scope.Animation, committed, editedTrack: 0, editedFrame: 1);

        Assert.False(second.Committed);
        Assert.Equal(external, File.ReadAllBytes(scope.Path));
        Assert.Contains("changed on disk", second.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void Commit_NeverPublishesBytesThatFailVerification()
    {
        using var scope = new AnimationScope(spline: false);
        byte[] source = File.ReadAllBytes(scope.Path);
        scope.Animation.Tracks[0].Translations[1] = new Vector3(4, 5, 6);

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 1,
            verificationFault: () => new InvalidDataException("injected verification fault"));

        Assert.False(result.Committed);
        Assert.Equal(source, File.ReadAllBytes(scope.Path));
        Assert.False(File.Exists(scope.Path + ".bak"));
        Assert.Contains("failed verification", result.Message, StringComparison.Ordinal);
        Assert.Contains("injected verification fault", result.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void PathAndByteArrayEncodersProduceTheSameNativeResult(bool spline)
    {
        using var scope = new AnimationScope(spline);
        byte[] source = File.ReadAllBytes(scope.Path);

        NativeAnimation.Result fromPath = spline
            ? NativeAnimation.Recompress(scope.Path, scope.Animation)
            : NativeAnimation.Interleave(scope.Path, scope.Animation);
        NativeAnimation.Result fromBytes = spline
            ? NativeAnimation.Recompress(source, scope.Animation)
            : NativeAnimation.Interleave(source, scope.Animation);

        Assert.Equal(fromPath.Frames, fromBytes.Frames);
        Assert.Equal(fromPath.Tracks, fromBytes.Tracks);
        Assert.Equal(fromPath.From, fromBytes.From);
        Assert.Equal(fromPath.Grew, fromBytes.Grew);
        Assert.Equal(fromPath.Bytes, fromBytes.Bytes);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(3)]
    [InlineData(4)]
    public void SplineEncoderRefusesRotationFormatsItCannotWrite(int format)
    {
        using var scope = new AnimationScope(spline: true);
        byte[] source = File.ReadAllBytes(scope.Path);
        var error = Assert.Throws<NotSupportedException>(() => NativeAnimation.Recompress(
            source, scope.Animation, new SplineEncoder.Options { RotationFormat = format }));

        Assert.Equal(AnimationCompression.DescribeSplineEncodingForWrite(format), error.Message);
    }

    [Theory]
    [InlineData(6)]
    [InlineData(7)]
    public void SplineDecodersRejectRotationFormatsWithoutAKnownLayout(int format)
    {
        using var scope = new AnimationScope(spline: true);
        byte[] written = NativeAnimation.Recompress(File.ReadAllBytes(scope.Path), scope.Animation).Bytes;
        var blob = SplineEncoder.Encode(scope.Animation);
        int mask = blob.BlockOffsets[0] + SplineFormat.QuantByte;
        byte[] data = blob.Data.ToArray();
        data[mask] = (byte)((data[mask] & ~0x3C) | (format << 2));
        int at = written.AsSpan().IndexOf(blob.Data);
        Assert.True(at >= 0, "the recompressed file does not hold the encoded blob");
        data.CopyTo(written, at);

        var error = Assert.Throws<NotSupportedException>(() => new HkxBinaryReader().ParseHkx(written));
        Assert.Equal(AnimationCompression.DescribeSplineEncoding(format), error.Message);
        Assert.Throws<NotSupportedException>(() => SplineEncoder.Decode(
            data, blob.BlockOffsets, 1, scope.Animation.NumFrames, blob.MaskAndQuantizationSize,
            blob.MaxFramesPerBlock, new HkxAnimationData()));
    }

    [Theory]
    [InlineData(new byte[] { 0x0C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xD4, 0xAA, 0x69 }, 0.2357023f, -0.2357023f, 0.4714045f, 0.8164966f)]
    [InlineData(new byte[] { 0x0C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x54, 0x3F, 0xBF }, -0.9718253f, 0.2357023f, 0f, 0f)]
    [InlineData(new byte[] { 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x87, 0x2B }, 0f, 0.1543033f, 0.6172134f, -0.7715167f)]
    public void SplineEncoderDecodesRotationFormats3And4(byte[] blob, float x, float y, float z, float w)
    {
        var into = new HkxAnimationData();
        SplineEncoder.Decode(blob, new[] { 0 }, 1, 2, 4, 2, into);

        var expected = new Quaternion(x, y, z, w);
        Assert.Equal(2, into.Tracks[0].Rotations.Count);
        foreach (var actual in into.Tracks[0].Rotations)
            Assert.True(SplineQuat.AngleBetween(expected, actual) < 1e-4f);
    }

    [Fact]
    public void SplineEncoderRefusesAZeroLengthStraight16Rotation()
    {
        byte[] blob = { 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x77, 0x77 };
        var into = new HkxAnimationData();

        Assert.Throws<InvalidDataException>(() => SplineEncoder.Decode(blob, new[] { 0 }, 1, 2, 4, 2, into));
    }

    [Theory]
    [InlineData(300)]
    [InlineData(512)]
    public void SplineBlocksAssembleOneSamplePerDeclaredFrame(int frames)
    {
        using var scope = new AnimationScope(spline: true, frames);
        byte[] written = NativeAnimation.Recompress(File.ReadAllBytes(scope.Path), scope.Animation).Bytes;
        var reopened = new HkxBinaryReader().ParseHkx(written);
        var blob = SplineEncoder.Encode(scope.Animation);
        var inMemory = new HkxAnimationData();
        SplineEncoder.Decode(blob.Data, blob.BlockOffsets, 1, frames, blob.MaskAndQuantizationSize,
            blob.MaxFramesPerBlock, inMemory);

        var source = scope.Animation.Tracks[0];
        foreach (var track in new[] { reopened.Tracks[0], inMemory.Tracks[0] })
        {
            Assert.Equal(frames, track.Translations.Count);
            Assert.Equal(frames, track.Rotations.Count);
            Assert.Equal(frames, track.Scales.Count);
            foreach (int f in new[] { 254, 255, 256, 509, 510, 511, frames - 1 }.Where(index => index < frames))
            {
                float drift = (track.Translations[f] - source.Translations[f]).Length();
                Assert.True(drift < 0.05f, $"translation at frame {f} drifted by {drift}");
                float turn = SplineQuat.AngleBetween(track.Rotations[f], source.Rotations[f]);
                Assert.True(turn < 0.01f, $"rotation at frame {f} turned by {turn}");
            }
        }
    }

    [Fact]
    public void SplineBlobDeclaredShorterThanItsBlocksIsRefused()
    {
        using var scope = new AnimationScope(spline: true);
        byte[] written = NativeAnimation.Recompress(File.ReadAllBytes(scope.Path), scope.Animation).Bytes;
        var image = PackfileImage.Read(written);
        var data = image.Section("__data__")!;
        var clip = new PackfileObjects(image).Instances.Single(i => i.ClassName == NativeAnimation.SplineClass);
        int blobCountAt = HavokClassTypes.Shipped.Members(NativeAnimation.SplineClass)
            .Single(member => member.Name == "data").Offset + 8;
        int declared = SplineEncoder.Encode(scope.Animation).Data.Length / 2;
        BitConverter.GetBytes(declared).CopyTo(data.Data, clip.Offset + blobCountAt);
        byte[] shortened = image.Rebuild();

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ParseHkx(shortened));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void VerifyRejectsAnAlteredRotation(bool spline)
    {
        using var scope = new AnimationScope(spline);
        byte[] source = File.ReadAllBytes(scope.Path);
        var written = Encode(spline, source, scope.Animation);
        scope.Animation.Tracks[0].Rotations[2] = Quaternion.CreateFromAxisAngle(Vector3.UnitX, 1f);

        var error = Assert.Throws<InvalidDataException>(() =>
            AnimationSaveTransaction.Verify(written, spline, scope.Animation, 0, 2));
        Assert.Contains("does not hold the frames", error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(false, false)]
    [InlineData(false, true)]
    [InlineData(true, false)]
    [InlineData(true, true)]
    public void VerifyRejectsAnAlteredScale(bool spline, bool timeline)
    {
        using var scope = new AnimationScope(spline);
        byte[] source = File.ReadAllBytes(scope.Path);
        var written = Encode(spline, source, scope.Animation);
        scope.Animation.Tracks[0].Scales[2] = new Vector3(2f, 1f, 1f);

        var error = Assert.Throws<InvalidDataException>(() => AnimationSaveTransaction.Verify(
            written, spline, scope.Animation, 0, 2,
            timeline ? NativeAnimation.Timeline.Identity(scope.Animation) : null));
        Assert.Contains("does not hold the frames", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void VerifyRejectsWrittenRootMotionThatDiffersFromTheEdit()
    {
        byte[] source = SyntheticAnimation.Clip(7, Array.Empty<(float, string)>(),
                                                SyntheticAnimation.Travel(3, 1f / 60f).Samples);
        var cut = AnimationEdit.Trim(new HkxBinaryReader().ReadAnimation(source), null, 1, 5);
        var intended = NativeAnimation.Timeline.Of(cut) with { Motion = SyntheticAnimation.Travel(5, 1f / 30f) };
        var written = NativeAnimation.Interleave(source, cut.Animation, intended);
        var differs = intended with { Motion = SyntheticAnimation.Travel(4, 1f / 30f) };

        Assert.Null(Record.Exception(() =>
            AnimationSaveTransaction.Verify(written, false, cut.Animation, -1, -1, intended)));
        var error = Assert.Throws<InvalidDataException>(() =>
            AnimationSaveTransaction.Verify(written, false, cut.Animation, -1, -1, differs));
        Assert.Contains("root motion", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void VerifyRejectsARebuiltClipWithFewerFramesThanTheEditDeclares()
    {
        using var scope = new AnimationScope(spline: false);
        byte[] source = File.ReadAllBytes(scope.Path);
        var shorter = NativeAnimation.Interleave(source, AnimationEdit.Retime(scope.Animation, null, 0.75f).Animation);

        var error = Assert.Throws<InvalidDataException>(() =>
            AnimationSaveTransaction.Verify(shorter, false, scope.Animation, 0, 2));
        Assert.Contains("expected 5", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void VerifyRejectsANonFiniteSample()
    {
        using var scope = new AnimationScope(spline: false);
        byte[] source = File.ReadAllBytes(scope.Path);
        scope.Animation.Tracks[0].Scales[2] = new Vector3(float.NaN, 1f, 1f);
        var written = NativeAnimation.Interleave(source, scope.Animation);

        var error = Assert.Throws<InvalidDataException>(() =>
            AnimationSaveTransaction.Verify(written, false, scope.Animation, 0, 2));
        Assert.Contains("not finite", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Commit_RefusesAZeroRotationAndLeavesTheSourceUntouched()
    {
        using var scope = new AnimationScope(spline: false);
        byte[] source = File.ReadAllBytes(scope.Path);
        scope.Animation.Tracks[0].Rotations[2] = new Quaternion(0f, 0f, 0f, 0f);

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 2);

        Assert.False(result.Committed);
        Assert.Equal(source, File.ReadAllBytes(scope.Path));
        Assert.False(File.Exists(scope.Path + ".bak"));
        Assert.Contains("0, 0, 0, 0", result.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void VerifyRefusesASplineRotationOfZeroLength()
    {
        using var scope = new AnimationScope(spline: true);
        byte[] source = File.ReadAllBytes(scope.Path);
        var written = Encode(true, source, scope.Animation);
        scope.Animation.Tracks[0].Rotations[2] = new Quaternion(0f, 0f, 0f, 0f);

        var error = Assert.Throws<InvalidDataException>(() =>
            AnimationSaveTransaction.Verify(written, true, scope.Animation, 0, 2));
        Assert.Contains("zero length", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Commit_RefusesASplineZeroRotationAndLeavesTheSourceUntouched()
    {
        using var scope = new AnimationScope(spline: true);
        byte[] source = File.ReadAllBytes(scope.Path);
        scope.Animation.Tracks[0].Rotations[2] = new Quaternion(0f, 0f, 0f, 0f);

        var result = AnimationSaveTransaction.Commit(
            scope.Path,
            scope.Animation,
            DocumentSourceStamp.Capture(scope.Path),
            editedTrack: 0,
            editedFrame: 2);

        Assert.False(result.Committed);
        Assert.Equal(source, File.ReadAllBytes(scope.Path));
        Assert.False(File.Exists(scope.Path + ".bak"));
    }

    private static NativeAnimation.Result Encode(bool spline, byte[] source, HkxAnimationData animation) =>
        spline
            ? NativeAnimation.Recompress(source, animation, null, true, null)
            : NativeAnimation.Interleave(source, animation);

    private sealed class AnimationScope : IDisposable
    {
        private readonly string _root =
            Directory.CreateTempSubdirectory("bgs-animation-save").FullName;

        public AnimationScope(bool spline, int frames = 5)
        {
            Path = System.IO.Path.Combine(_root, "animation.hkx");
            (byte[] source, HkxAnimationData animation) = CreateSource(spline, frames);
            Animation = animation;
            File.WriteAllBytes(Path, source);
        }

        public string Path { get; }
        public HkxAnimationData Animation { get; }

        public void Dispose()
        {
            try { Directory.Delete(_root, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }

        private static (byte[] Source, HkxAnimationData Animation) CreateSource(bool spline, int frames)
        {
            string sourceClass = spline
                ? NativeAnimation.SplineClass
                : NativeAnimation.Compressed.Single(name => name != NativeAnimation.SplineClass);
            HkxAnimationData animation = MadeUpAnimation(sourceClass, frames);

            var image = new PackfileImage { Predicates = new byte[16] };
            image.Sections.Add(new PackfileSection { TagBytes = Tag("__classnames__") });
            image.Sections.Add(new PackfileSection { TagBytes = Tag("__types__") });
            image.Sections.Add(new PackfileSection { TagBytes = Tag("__data__") });

            var source = NativeAppend.Object(image, sourceClass);
            var binding = NativeAppend.Object(image, "hkaAnimationBinding");
            NativeAppend.Attach(image, binding.Id, "animation", source.Id);

            var duration = HavokClassTypes.Shipped.Members(sourceClass)
                .Single(member => member.Name == "duration");
            var data = image.Section("__data__")!;
            BitConverter.GetBytes(animation.Duration)
                .CopyTo(data.Data, source.Offset + duration.Offset);
            FixupOrder.Reorder(image);

            return (image.Rebuild(), animation);
        }

        private static HkxAnimationData MadeUpAnimation(string sourceClass, int frames)
        {
            var animation = new HkxAnimationData
            {
                AnimationClass = sourceClass,
                NumFrames = frames,
                NumTracks = 1,
                FrameDuration = 1f / 30f,
                Duration = (frames - 1) / 30f,
            };
            var track = new HkxTrackData { RotationAnimated = true };
            track.TranslationAnimated[0] = true;
            track.TranslationAnimated[1] = true;
            track.TranslationAnimated[2] = true;

            for (int frame = 0; frame < frames; frame++)
            {
                track.Translations.Add(new Vector3(frame, frame * 2, -frame));
                track.Rotations.Add(Quaternion.CreateFromAxisAngle(Vector3.UnitZ, frame * 0.01f));
                track.Scales.Add(Vector3.One);
            }

            animation.Tracks.Add(track);
            return animation;
        }

        private static byte[] Tag(string name)
        {
            var bytes = new byte[20];
            System.Text.Encoding.ASCII.GetBytes(name).CopyTo(bytes, 0);
            return bytes;
        }
    }
}
