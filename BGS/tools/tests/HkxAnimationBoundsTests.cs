using System;
using System.IO;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxAnimationBoundsTests
{
    private const string LosslessClass = "hkaLosslessCompressedAnimation";
    private const string InterleavedClass = "hkaInterleavedUncompressedAnimation";
    private const string BindingClass = "hkaAnimationBinding";
    private const string SplineClass = "hkaSplineCompressedAnimation";

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
            c.SetWord(a.Offset + 0x60, 2);
            c.SetPointer(a.Offset + 0x58, a.Offset);
            c.SetPointer(a.Offset + 0x98, a.Offset);
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
