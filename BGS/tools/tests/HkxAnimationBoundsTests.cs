using System;
using System.IO;
using System.Numerics;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxAnimationBoundsTests
{
    private const string LosslessClass = "hkaLosslessCompressedAnimation";
    private const string InterleavedClass = "hkaInterleavedUncompressedAnimation";
    private const string BindingClass = "hkaAnimationBinding";
    private const string SplineClass = "hkaSplineCompressedAnimation";
    private const string QuantizedClass = "hkaQuantizedAnimation";
    private const string PredictiveClass = "hkaPredictiveCompressedAnimation";

    [Fact]
    public void HugeDeclaredArrayCountIsRejectedBeforeAllocation()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", int.MaxValue);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void NegativeDeclaredArrayCountIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicRotations", -1);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void ArrayRunningOneElementPastTheBufferIsRejectedInsteadOfReadingZeros()
    {
        byte[] probe = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", 0);
        }, out int contentAbs);
        int count = (probe.Length - contentAbs) / 4 + 1;

        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", count);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void ArrayEndingExactlyAtBufferEndIsNotRejectedAsPastTheEnd()
    {
        byte[] probe = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", 0);
        }, out int contentAbs);
        int available = probe.Length - contentAbs;
        Assert.Equal(0, available % 4);

        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", available / 4);
        }, out _);

        var error = Record.Exception(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.DoesNotContain("past the end of the file", error?.Message ?? string.Empty);
    }

    [Fact]
    public void HugeAnnotationTrackCountIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, "hkaAnimation", "annotationTracks", int.MaxValue);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void HugeLosslessFrameCountIsRejectedBeforeLooping()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) => SetLossless(c, a, frames: int.MaxValue, tracks: 1), out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void HugeInterleavedTrackCountIsRejected()
    {
        byte[] bytes = Packfile(InterleavedClass, (c, a) =>
        {
            c.SetScalar(a, "hkaAnimation", "numberOfTransformTracks", int.MaxValue);
            c.SetArray(a, InterleavedClass, "transforms", 1);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void HugeInterleavedTransformCountIsRejected()
    {
        byte[] bytes = Packfile(InterleavedClass, (c, a) =>
        {
            c.SetScalar(a, "hkaAnimation", "numberOfTransformTracks", 1);
            c.SetArray(a, InterleavedClass, "transforms", int.MaxValue);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void HugeBindingIndexCountIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            var binding = c.Object(BindingClass);
            c.SetArray(binding, BindingClass, "transformTrackToBoneIndices", int.MaxValue);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void BindingWithAnIndexCountButNoFixupIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 0, tracks: 1);
            var binding = c.Object(BindingClass);
            c.SetWord(c.Offset(binding, BindingClass, "transformTrackToBoneIndices") + c.Image.Layout.PointerSize, 2);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("no fixup", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AnnotationEventsSharedByTracksStayWithinBudget()
    {
        const int tracks = 1024, events = 1024;
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            int run = c.AppendBlock(events * 0x10);
            int array = c.AppendBlock(tracks * 0x18);
            for (int t = 0; t < tracks; t++)
            {
                c.SetPointer(array + t * 0x18 + 0x08, run);
                c.SetWord(array + t * 0x18 + 0x10, events);
            }
            c.SetArray(a, "hkaAnimation", "annotationTracks", tracks, array);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void HugeSplineTrackCountIsRejectedBeforeAllocation()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1 << 20);
            c.SetWord(a.Offset + 0x38, 1);
            c.SetWord(a.Offset + 0x3C, 1);
            c.SetWord(a.Offset + 0x40, 1);
            c.SetWord(a.Offset + 0x60, 1);
            c.SetPointer(a.Offset + 0x58, a.Offset);
            c.SetPointer(a.Offset + 0x98, a.Offset);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void SplineBlockFramesAreCappedAtNumFrames()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0x3C, 2);
            c.SetWord(a.Offset + 0x40, int.MaxValue);
            int blob = c.AppendBlock(8);
            int offsets = c.AppendBlock(8);
            c.SetWord(offsets, 0);
            c.SetWord(offsets + 4, 4);
            c.SetWord(a.Offset + 0x60, 2);
            c.SetPointer(a.Offset + 0x58, offsets);
            c.SetPointer(a.Offset + 0x98, blob);
            c.SetWord(a.Offset + 0xA0, 8);
        }, out _);

        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        Assert.Equal(2, animation.Tracks[0].Translations.Count);
    }

    [Fact]
    public void TryReadAnimationReportsHugeCountAsInvalidData()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            c.SetArray(a, LosslessClass, "dynamicTranslations", int.MaxValue);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().TryReadAnimation(bytes, out _));
    }

    [Fact]
    public void ValidAnimationFixtureStillParses()
    {
        var animation = new HkxBinaryReader().ReadAnimation(
            TestFixtures.Path("Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx"));

        Assert.NotEmpty(animation.AnimationClass);
        Assert.NotEmpty(animation.Tracks);
    }

    [Fact]
    public void InterleavedClipWithAPartialFrameIsRejected()
    {
        byte[] bytes = Packfile(InterleavedClass, (c, a) => SetInterleaved(c, a, tracks: 2, transforms: 3), out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("partial frame", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void InterleavedClipWithWholeFramesStillParses()
    {
        byte[] bytes = Packfile(InterleavedClass, (c, a) => SetInterleaved(c, a, tracks: 2, transforms: 4), out _);

        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        Assert.Equal(2, animation.Tracks.Count);
        Assert.Equal(2, animation.Tracks[0].Translations.Count);
        Assert.Equal(2, animation.Tracks[1].Translations.Count);
    }

    [Fact]
    public void AnnotationTrackNamesStayOutOfTheSkeletonNames()
    {
        byte[] bytes = Packfile(InterleavedClass, (c, a) =>
        {
            SetInterleaved(c, a, tracks: 1, transforms: 2);
            int track = c.AppendBlock(0x18);
            c.SetPointer(track, c.AppendData(Encoding.ASCII.GetBytes("spine\0")));
            c.SetArray(a, "hkaAnimation", "annotationTracks", 1, track);
        }, out _);

        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        Assert.Empty(animation.BoneNames);
        Assert.Equal(new[] { "spine" }, animation.AnnotationTrackNames);
    }

    [Fact]
    public void AnnotationTrackArrayWithoutItsPointerIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            SetCountWithoutPointer(c, a, "hkaAnimation", "annotationTracks", 1);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("Annotation track array", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AnnotationEventArrayWithoutItsPointerIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            int track = c.AppendBlock(0x18);
            c.SetWord(track + 0x10, 2);
            c.SetArray(a, "hkaAnimation", "annotationTracks", 1, track);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("no event array", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AnnotationEventArrayEndingBeforeItsCountIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 4, tracks: 1);
            int events = c.AppendBlock(0x10);
            int track = c.AppendBlock(0x18);
            c.SetPointer(track + 0x08, events);
            c.SetWord(track + 0x10, 1000);
            c.SetArray(a, "hkaAnimation", "annotationTracks", 1, track);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("past the end of the file", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void LosslessSampleArrayWithoutItsPointerIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 2, tracks: 1);
            SetLosslessWords(c, a, translation: 0);
            SetCountWithoutPointer(c, a, LosslessClass, "dynamicTranslations", 3);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("no data pointer", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void LosslessTrackWithoutItsTypeWordsIsRejected()
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 2, tracks: 1);
            int rotations = c.AppendBlock(2);
            c.SetArray(a, LosslessClass, "rotationTypeAndOffsets", 1, rotations);
            int scales = c.AppendBlock(8);
            c.SetArray(a, LosslessClass, "scaleTypeAndOffsets", 1, scales);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("type words", error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData("staticTranslations", 1)]
    [InlineData("dynamicTranslations", 2)]
    public void LosslessSampleOffsetPastItsArrayIsRejected(string member, int type)
    {
        byte[] bytes = Packfile(LosslessClass, (c, a) =>
        {
            SetLossless(c, a, frames: 2, tracks: 1);
            int samples = c.AppendBlock(3 * 4);
            c.SetArray(a, LosslessClass, member, 3, samples);
            SetLosslessWords(c, a, translation: (100 << 2) | type);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("points past the sampled values", error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(QuantizedClass, false)]
    [InlineData(PredictiveClass, true)]
    public void CompressedPayloadRunningIntoFixupDataIsRejected(string animationClass, bool predictive)
    {
        byte[] probe = CompressedPayloadClip(animationClass, predictive, 1, out int payloadAbs);
        byte[] bytes = CompressedPayloadClip(animationClass, predictive, probe.Length - payloadAbs, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("__data__ section", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void SplineClipWithoutBlobPointerIsRejected()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0x60, 2);
            c.SetPointer(a.Offset + 0x58, a.Offset);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("no blob pointer", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void SplineClipWithoutBlockOffsetsIsRejected()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0xA0, 16);
            c.SetPointer(a.Offset + 0x98, a.Offset);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("no block offsets", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void SplineClipDeclaringNoFramesIsAnEmptyContainer()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) => c.SetWord(a.Offset + 0x18, 1), out _);

        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        Assert.Empty(animation.Tracks);
    }

    [Fact]
    public void SplineMaskSizeSmallerThanTheTrackMasksIsRejected()
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0x3C, 1);
            c.SetWord(a.Offset + 0x40, 2);
            c.SetWord(a.Offset + 0x44, 1);
            int blob = c.AppendBlock(16);
            c.SetWord(blob, 0x100);
            c.SetWord(blob + 4, 0x3F800000);
            int offsets = c.AppendBlock(4);
            c.SetWord(a.Offset + 0x60, 1);
            c.SetPointer(a.Offset + 0x58, offsets);
            c.SetPointer(a.Offset + 0x98, blob);
            c.SetWord(a.Offset + 0xA0, 16);
        }, out _);

        var error = Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
        Assert.Contains("mask byte", error.Message, StringComparison.Ordinal);
    }

    [Theory]
    [InlineData(new byte[] { 0x0C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xD4, 0xAA, 0x69 }, 0.2357023f, -0.2357023f, 0.4714045f, 0.8164966f)]
    [InlineData(new byte[] { 0x0C, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x54, 0x3F, 0xBF }, -0.9718253f, 0.2357023f, 0f, 0f)]
    [InlineData(new byte[] { 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x87, 0x2B }, 0f, 0.1543033f, 0.6172134f, -0.7715167f)]
    public void SplineRotationInFormat3Or4DecodesToRadicleQuaternion(byte[] blob, float x, float y, float z, float w)
    {
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0x3C, 1);
            c.SetWord(a.Offset + 0x40, 2);
            c.SetWord(a.Offset + 0x44, 4);
            int data = c.AppendData(blob);
            int offsets = c.AppendBlock(4);
            c.SetWord(a.Offset + 0x60, 1);
            c.SetPointer(a.Offset + 0x58, offsets);
            c.SetPointer(a.Offset + 0x98, data);
            c.SetWord(a.Offset + 0xA0, blob.Length);
        }, out _);

        var animation = new HkxBinaryReader().ReadAnimation(bytes);

        var expected = new Quaternion(x, y, z, w);
        Assert.Equal(2, animation.Tracks[0].Rotations.Count);
        foreach (var actual in animation.Tracks[0].Rotations)
            AssertQuaternionNear(expected, actual);
    }

    [Fact]
    public void SplineStraight16RotationWithEveryNibbleSevenIsRefused()
    {
        byte[] blob = { 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x77, 0x77 };
        byte[] bytes = Packfile(SplineClass, (c, a) =>
        {
            c.SetWord(a.Offset + 0x18, 1);
            c.SetWord(a.Offset + 0x38, 2);
            c.SetWord(a.Offset + 0x3C, 1);
            c.SetWord(a.Offset + 0x40, 2);
            c.SetWord(a.Offset + 0x44, 4);
            int data = c.AppendData(blob);
            int offsets = c.AppendBlock(4);
            c.SetWord(a.Offset + 0x60, 1);
            c.SetPointer(a.Offset + 0x58, offsets);
            c.SetPointer(a.Offset + 0x98, data);
            c.SetWord(a.Offset + 0xA0, blob.Length);
        }, out _);

        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    private static void AssertQuaternionNear(Quaternion expected, Quaternion actual)
    {
        Assert.InRange(actual.X, expected.X - 1e-5f, expected.X + 1e-5f);
        Assert.InRange(actual.Y, expected.Y - 1e-5f, expected.Y + 1e-5f);
        Assert.InRange(actual.Z, expected.Z - 1e-5f, expected.Z + 1e-5f);
        Assert.InRange(actual.W, expected.W - 1e-5f, expected.W + 1e-5f);
    }

    private static void SetInterleaved(CraftedPackfile crafted, NativeAppend.Added animation, int tracks, int transforms)
    {
        crafted.SetScalar(animation, "hkaAnimation", "numberOfTransformTracks", tracks);
        int run = crafted.AppendBlock(transforms * HkxBinaryReader.QsTransformSize);
        crafted.SetArray(animation, InterleavedClass, "transforms", transforms, run);
    }

    private static byte[] CompressedPayloadClip(string animationClass, bool predictive, int count, out int payloadAbs)
    {
        var crafted = new CraftedPackfile();
        var animation = crafted.Object(animationClass);
        if (predictive) crafted.SetWord(animation.Offset + 0xa4, 1);
        int payload = crafted.AppendBlock(16);
        crafted.SetWord(animation.Offset + 0x40, count);
        crafted.SetPointer(animation.Offset + 0x38, payload);
        byte[] bytes = crafted.Build(out int dataStart);
        payloadAbs = dataStart + payload;
        return bytes;
    }

    private static void SetCountWithoutPointer(CraftedPackfile crafted, NativeAppend.Added animation,
        string className, string member, int count) =>
        crafted.SetWord(crafted.Offset(animation, className, member) + crafted.Image.Layout.PointerSize, count);

    private static void SetLosslessWords(CraftedPackfile crafted, NativeAppend.Added animation, int translation)
    {
        int translations = crafted.AppendBlock(8);
        crafted.SetWord(translations, translation);
        crafted.SetArray(animation, LosslessClass, "translationTypeAndOffsets", 1, translations);
        int rotations = crafted.AppendBlock(2);
        crafted.SetArray(animation, LosslessClass, "rotationTypeAndOffsets", 1, rotations);
        int scales = crafted.AppendBlock(8);
        crafted.SetArray(animation, LosslessClass, "scaleTypeAndOffsets", 1, scales);
    }

    private static void SetLossless(CraftedPackfile crafted, NativeAppend.Added animation, int frames, int tracks)
    {
        crafted.SetScalar(animation, "hkaAnimation", "numberOfTransformTracks", tracks);
        crafted.SetScalar(animation, LosslessClass, "numFrames", frames);
    }

    private static byte[] Packfile(string animationClass,
        Action<CraftedPackfile, NativeAppend.Added> fill, out int contentAbs)
    {
        var crafted = new CraftedPackfile();
        var animation = crafted.Object(animationClass);
        fill(crafted, animation);
        byte[] bytes = crafted.Build(out int dataStart);
        contentAbs = dataStart + animation.Offset;
        return bytes;
    }
}
