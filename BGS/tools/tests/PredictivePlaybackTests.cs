using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Threading.Tasks;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class PredictivePlaybackTests
{
    [Theory]
    [InlineData(1, 1, false)]
    [InlineData(15, 1, false)]
    [InlineData(16, 1, false)]
    [InlineData(17, 2, false)]
    [InlineData(31, 2, false)]
    [InlineData(32, 1, true)]
    public void PackfileDecodePoseAndPlayback(int frames, int bones, bool fixedRange)
    {
        var animation = new HkxBinaryReader().ReadAnimation(Fixture(frames, bones, fixedRange));
        Assert.False(animation.HasUnsupportedAnimation);
        Assert.True(animation.CompressionDecoded);
        Assert.Equal(frames, animation.NumFrames);
        Assert.Equal((frames - 1) / 15 + 1, animation.NumBlocks);
        var skeleton = new HkxSkeleton();
        for (int b = 0; b < bones; b++)
        {
            skeleton.BoneNames.Add("bone" + b);
            skeleton.ParentIndices.Add(-1);
            skeleton.ReferencePose.Add(new HkxBonePose());
        }
        var playback = new PlaybackSession();
        playback.Load(frames, animation.FrameDuration);
        Assert.Equal(frames > 1, playback.Start(1));
        float scale = fixedRange ? BitConverter.Int32BitsToSingle(0x39000400) : 0.25f;
        for (int frame = 0; frame < frames; frame++)
        {
            playback.Show(frame);
            var pose = AnimationPose.At(skeleton, animation, playback.Frame);
            for (int b = 0; b < bones; b++)
                Assert.Equal((frame + b * 100) * scale, pose.Bones[b].Position.X);
        }
        if (frames == 31)
        {
            Assert.Equal(3.625f, AnimationPose.At(skeleton, animation, 14.5f).Bones[0].Position.X);
            Assert.Equal(3.75f, AnimationPose.At(skeleton, animation, 15).Bones[0].Position.X);
        }
    }

    [Theory]
    [InlineData("width")]
    [InlineData("float")]
    [InlineData("boundary")]
    [InlineData("static")]
    [InlineData("renorm")]
    [InlineData("mixed")]
    [InlineData("reference")]
    public void UndefinedSitesAreRefusedWithoutDecoding(string fault)
    {
        byte[] bytes = Fixture(31, 1, false, fault);

        Assert.False(new HkxBinaryReader().TryReadAnimation(bytes, out var animation));
        Assert.Equal(AnimationCompression.PredictiveClass, animation.AnimationClass);
        Assert.False(animation.CompressionDecoded);
        Assert.True(animation.HasUnsupportedAnimation);
        Assert.Throws<UnsupportedAnimationException>(() => new HkxBinaryReader().ReadAnimation(bytes));
    }

    [Fact]
    public void TruncatedPayloadIsMalformedNotRefused()
    {
        Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().TryReadAnimation(Fixture(31, 1, false, "truncated"), out _));
    }

    [Fact(Timeout = 60000)]
    public async Task NegativeFixupTableStartIsRefusedWithoutScanning()
    {
        byte[] bytes = Fixture(32, 1, true);
        bytes[243] = 0xa5;
        bytes[252] = 0x40;
        bytes[350] = 0xff;

        await Task.Run(() => Assert.Throws<InvalidDataException>(() => new HkxBinaryReader().ReadAnimation(bytes)));
    }

    [Fact]
    public void JunkInAPaddingLaneDoesNotRefuseTheFile()
    {
        var animation = new HkxBinaryReader().ReadAnimation(Fixture(31, 1, false, "padding"));

        Assert.Equal(31, animation.NumFrames);
        Assert.True(animation.CompressionDecoded);
    }
    internal static byte[] Fixture(int frames, int bones, bool fixedRange, string fault = "")
    {
        const string cls = AnimationCompression.PredictiveClass;
        return CraftedPackfile.Animation(cls, (file, obj) =>
        {
            var compressed = new List<byte>();
            var ints = new List<ushort>();
            int blocks = (frames - 1) / 15 + 1;
            int count = 10 * bones;
            int groups = (count + 15) / 16;
            for (int block = 0; block < blocks; block++)
            {
                for (int group = 0; group < groups; group++)
                {
                    var samples = new int[16, 16];
                    for (int lane = 0; lane < 16; lane++)
                        for (int sample = 0; sample < 16; sample++)
                        {
                            int index = group * 16 + lane, component = index % 10;
                            int frame = Math.Min(frames - 1, block * 15 + sample);
                            samples[lane, sample] = component switch
                            {
                                0 => frame + index / 10 * 100,
                                6 or 7 or 8 or 9 => fixedRange ? 8191 : 4,
                                _ => 0
                            };
                        }
                    byte[] bytes = PredictiveTestEncoder.Samples(samples);
                    if (fault == "width" && block == 0 && group == 0)
                    {
                        int firstEnd = 16 + (bytes[0] >> 4) + 1;
                        bytes[firstEnd - 1] |= 15;
                    }
                    if (fault == "padding" && block == 0 && group == 0)
                    {
                        int start = 16;
                        for (int lane = 0; lane < 10; lane++) start += (bytes[lane] >> 4) + 1 + (bytes[lane] & 15) + 1;
                        int firstEnd = start + (bytes[10] >> 4) + 1;
                        bytes[firstEnd - 1] |= 15;
                    }
                    compressed.AddRange(bytes);
                }
                ints.Add((ushort)compressed.Count);
                ints.Add((ushort)(compressed.Count >> 16));
            }
            int[] offsets = new int[9];
            offsets[1] = offsets[2] = ints.Count;
            for (int i = 0; i < groups; i++) ints.Add(ushort.MaxValue);
            offsets[3] = ints.Count;
            for (int i = 0; i < groups; i++) ints.Add(fixedRange ? ushort.MaxValue : (ushort)(fault == "mixed" ? 1 : 0));
            offsets[4] = ints.Count;
            for (int b = 0; b < bones; b++)
                foreach (int c in new[] { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10 }) ints.Add((ushort)(b * 12 + c));
            offsets[5] = ints.Count;
            if (fault == "boundary") ints.Add(0);
            offsets[6] = ints.Count;
            if (fault == "static") ints.Add(0);
            offsets[7] = offsets[8] = ints.Count;
            if (fault == "renorm") ints.Add(0);
            if (fault == "reference") ints[offsets[4] + 9] = 11;
            if (fault == "truncated") compressed.RemoveAt(compressed.Count - 1);
            byte[] integerBytes = new byte[ints.Count * 2];
            for (int i = 0; i < ints.Count; i++) BitConverter.GetBytes(ints[i]).CopyTo(integerBytes, i * 2);
            byte[] floats = new byte[fixedRange ? 0 : count * 8];
            if (!fixedRange)
                for (int i = 0; i < count; i++) BitConverter.GetBytes(0.25f).CopyTo(floats, i * 4);
            file.SetArray(obj, cls, "compressedData", compressed.Count, file.AppendData(compressed.ToArray()));
            file.SetArray(obj, cls, "intData", ints.Count, file.AppendData(integerBytes));
            file.SetArray(obj, cls, "floatData", floats.Length / 4, file.AppendData(floats));
            for (int i = 0; i < 9; i++) file.SetWord(obj.Offset + 0x58 + i * 4, offsets[i]);
            file.SetWord(obj.Offset + 0x90, 0);
            file.SetWord(obj.Offset + 0x94, 0);
            file.SetWord(obj.Offset + 0x98, fixedRange ? 0 : count);
            file.SetScalar(obj, cls, "numBones", bones);
            file.SetScalar(obj, cls, "numFloatSlots", fault == "float" ? 1 : 0);
            file.SetScalar(obj, cls, "numFrames", frames);
            file.SetReal(obj, cls, "duration", Math.Max(1, frames - 1));
            file.SetScalar(obj, cls, "numberOfTransformTracks", bones);
            file.SetScalar(obj, cls, "numberOfFloatTracks", fault == "float" ? 1 : 0);
        });
    }
}
