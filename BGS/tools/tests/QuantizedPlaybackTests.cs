using System;
using System.Buffers.Binary;
using System.IO;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class QuantizedPlaybackTests
{
    [Theory]
    [InlineData(1)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(17)]
    [InlineData(31)]
    public void PackfileDecodePosePlaybackAndFloats(int frames)
    {
        byte[] bytes = Fixture(frames, false);
        var animation = new HkxBinaryReader().ReadAnimation(bytes);
        Assert.False(animation.HasUnsupportedAnimation);
        Assert.Equal(frames, animation.NumFrames);
        var skeleton = new HkxSkeleton();
        skeleton.BoneNames.Add("root");
        skeleton.ParentIndices.Add(-1);
        skeleton.ReferencePose.Add(new HkxBonePose());
        var playback = new PlaybackSession();
        playback.Load(animation.NumFrames, animation.FrameDuration);
        Assert.Equal(frames > 1, playback.Start(1));
        for (int frame = 0; frame < frames; frame++)
        {
            playback.Show(frame);
            var pose = AnimationPose.At(skeleton, animation, playback.Frame);
            Assert.InRange(MathF.Abs(pose.Bones[0].Position.X - frame * 0.25f), 0, 0.00001f);
            Assert.Equal(new Vector3(1, 2, 3), animation.Tracks[0].Scales[frame]);
            Assert.Equal(12.5f, animation.FloatTracks[0][frame]);
            Assert.Equal(frame * 0.5f - 2, animation.FloatTracks[1][frame]);
            int pattern = frame % 8;
            int omitted = pattern & 3;
            var q = animation.Tracks[0].Rotations[frame];
            float component = omitted switch { 0 => q.X, 1 => q.Y, 2 => q.Z, _ => q.W };
            Assert.Equal((pattern & 4) != 0, component < 0);
            Assert.InRange(MathF.Abs(component), 0.9999f, 1.0001f);
        }
    }

    [Fact]
    public void StaticRotationsAndRawScalarsDecode()
    {
        var animation = new HkxBinaryReader().ReadAnimation(Fixture(3, true));
        Assert.All(animation.Tracks[0].Rotations, q => Assert.InRange(MathF.Abs(q.W), 0.9999f, 1.0001f));
        Assert.All(animation.Tracks[0].Translations, v => Assert.Equal(new Vector3(4, 5, 6), v));
    }

    [Fact]
    public void TruncationOverlapAndMissingSlotsRefuse()
    {
        byte[] blob = Blob(3, false);
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.Decode(blob.AsSpan(0, blob.Length - 1), new()));
        BinaryPrimitives.WriteUInt16LittleEndian(blob.AsSpan(30), 0);
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.Decode(blob, new()));
        blob = Blob(3, false);
        BinaryPrimitives.WriteUInt16LittleEndian(blob.AsSpan(64), 3);
        Assert.Throws<NotSupportedException>(() => QuantizedAnimationDecoder.Decode(blob, new()));
    }

    internal static byte[] Fixture(int frames, bool allStatic)
    {
        return CraftedPackfile.Animation(AnimationCompression.QuantizedClass, (file, obj) =>
        {
            byte[] blob = Blob(frames, allStatic);
            int target = file.AppendData(blob);
            file.SetArray(obj, AnimationCompression.QuantizedClass, "data", blob.Length, target);
            file.SetReal(obj, AnimationCompression.QuantizedClass, "duration", Math.Max(1, frames - 1));
            file.SetScalar(obj, AnimationCompression.QuantizedClass, "numberOfTransformTracks", 1);
            file.SetScalar(obj, AnimationCompression.QuantizedClass, "numberOfFloatTracks", 2);
        });
    }

    private static byte[] Blob(int frames, bool allStatic)
    {
        int[] w = new int[20];
        w[0] = 256; w[1] = 1; w[2] = 2; w[3] = frames;
        w[6] = allStatic ? 6 : 5; w[7] = allStatic ? 1 : 0; w[9] = 1;
        w[10] = allStatic ? 0 : 1; w[11] = allStatic ? 0 : 1; w[13] = 1;
        w[14] = allStatic ? 2 : 10;
        w[15] = 64; w[16] = 96; w[17] = 128; w[18] = 192; w[19] = 224;
        var blob = new byte[256 + frames * w[14]];
        for (int i = 0; i < 20; i++) U16(blob, i * 2, w[i]);
        F32(blob, 8, Math.Max(1, frames - 1));
        int[] scalarSlots = allStatic ? new[] { 0, 1, 2, 8, 9, 10 } : new[] { 1, 2, 8, 9, 10 };
        for (int i = 0; i < scalarSlots.Length; i++)
        {
            U16(blob, 64 + 2 * i, scalarSlots[i]);
            F32(blob, 128 + 4 * i, scalarSlots[i] switch { 0 => 4, 1 => 5, 2 => 6, 8 => 1, 9 => 2, _ => 3 });
        }
        int ns = scalarSlots.Length;
        if (allStatic) { U16(blob, 64 + 2 * ns, 1); Rotation(blob, 128 + 4 * ns, 3); }
        U16(blob, 64 + 2 * (ns + w[7]), 0);
        int staticFloat = (128 + 4 * ns + 6 * w[7] + 15) & ~15;
        F32(blob, staticFloat, 12.5f);
        int floatOffset = 192, floatScale = 224;
        if (!allStatic)
        {
            U16(blob, 96, 0); U16(blob, 98, 1);
            F32(blob, 192, 0); F32(blob, 224, 0.25f);
            floatOffset = 208; floatScale = 240;
        }
        U16(blob, 96 + 2 * (w[10] + w[11]), 1);
        F32(blob, floatOffset, -2); F32(blob, floatScale, 0.5f);
        for (int frame = 0; frame < frames; frame++)
        {
            int at = 256 + frame * w[14];
            if (!allStatic)
            {
                U16(blob, at, frame);
                Rotation(blob, at + 2, frame % 8);
            }
            U16(blob, at + 2 * w[10] + 6 * w[11], frame);
        }
        return blob;
    }

    private static void Rotation(byte[] blob, int at, int pattern)
    {
        int zero = (int)MathF.Round(0.70710677f / 4.3161006e-05f);
        U16(blob, at, zero | ((pattern & 1) << 15));
        U16(blob, at + 2, zero | (((pattern >> 1) & 1) << 15));
        U16(blob, at + 4, zero | (((pattern >> 2) & 1) << 15));
    }

    private static void U16(byte[] blob, int at, int value) => BinaryPrimitives.WriteUInt16LittleEndian(blob.AsSpan(at), (ushort)value);
    private static void F32(byte[] blob, int at, float value) => BinaryPrimitives.WriteInt32LittleEndian(blob.AsSpan(at), BitConverter.SingleToInt32Bits(value));
}
