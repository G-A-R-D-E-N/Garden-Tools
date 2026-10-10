using System;
using System.IO;
using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HkxAnimationBoundsTests
{
    private const string LosslessClass = "hkaLosslessCompressedAnimation";
    private const string InterleavedClass = "hkaInterleavedUncompressedAnimation";
    private const string BindingClass = "hkaAnimationBinding";

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
            Fixture("Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx"));

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

    private static string Fixture(string relative) =>
        Path.Combine(AppContext.BaseDirectory, "fixtures/vanilla", relative.Replace('/', Path.DirectorySeparatorChar));

    private sealed class CraftedPackfile
    {
        private readonly PackfileImage _image = new() { Predicates = new byte[16] };

        public CraftedPackfile()
        {
            foreach (string name in new[] { "__classnames__", "__types__", "__data__" })
            {
                var tag = new byte[20];
                Encoding.ASCII.GetBytes(name).CopyTo(tag, 0);
                _image.Sections.Add(new PackfileSection { TagBytes = tag });
            }
        }

        public NativeAppend.Added Object(string className) => NativeAppend.Object(_image, className);

        public void SetArray(NativeAppend.Added obj, string className, string member, int count)
        {
            var data = _image.Section("__data__")!;
            int field = obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset;
            data.SetLocal(field, obj.Offset);
            BitConverter.GetBytes(count).CopyTo(data.Data, field + _image.Layout.PointerSize);
        }

        public void SetScalar(NativeAppend.Added obj, string className, string member, int value) =>
            BitConverter.GetBytes(value).CopyTo(_image.Section("__data__")!.Data,
                obj.Offset + HavokClasses.Shipped.Field(className, member)!.Offset);

        public byte[] Build(out int dataStart)
        {
            FixupOrder.Reorder(_image);
            byte[] bytes = _image.Rebuild();
            dataStart = 0;
            for (int i = 0; i < 3; i++)
            {
                int header = 0x50 + i * 0x40;
                if (Encoding.ASCII.GetString(bytes, header, 16).TrimEnd('\0') == "__data__")
                    dataStart = BitConverter.ToInt32(bytes, header + 0x14);
            }
            return bytes;
        }
    }
}
