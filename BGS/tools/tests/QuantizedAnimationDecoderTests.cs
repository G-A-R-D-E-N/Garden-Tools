using System;
using System.Buffers.Binary;
using System.IO;
using System.Numerics;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class QuantizedAnimationDecoderTests
{
    [Fact]
    public void RandomQuaternionRoundTripsCoverEveryOmittedComponentAndSign()
    {
        var random = new Random(52016773);
        var seen = new bool[8];
        for (int i = 0; i < 10000; i++)
        {
            var q = Quaternion.Normalize(new Quaternion(Next(), Next(), Next(), Next()));
            var bytes = Encode(q, out int omitted, out bool negative);
            seen[omitted * 2 + (negative ? 1 : 0)] = true;
            var decoded = QuantizedAnimationDecoder.DecodeQuaternion(bytes);
            Assert.InRange(Quaternion.Dot(q, decoded), 0.9999997f, 1.0000003f);
            Assert.InRange(MathF.Abs(decoded.LengthSquared() - 1), 0, 0.000001f);
        }
        Assert.All(seen, Assert.True);
        float Next() => (float)(random.NextDouble() * 2 - 1);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void AxisQuaternionsPreserveTheOmittedSign(int axis)
    {
        foreach (float sign in new[] { -1f, 1f })
        {
            var values = new float[4];
            values[axis] = sign;
            var q = new Quaternion(values[0], values[1], values[2], values[3]);
            var decoded = QuantizedAnimationDecoder.DecodeQuaternion(Encode(q, out _, out _));
            Assert.InRange(Quaternion.Dot(q, decoded), 0.9999997f, 1.0000003f);
        }
    }

    [Fact]
    public void ScalarsRoundTripWithinHalfAQuantizationStep()
    {
        var random = new Random(26);
        const float scale = 0.002f, offset = -32f;
        for (int i = 0; i < 10000; i++)
        {
            float value = offset + (float)random.NextDouble() * ushort.MaxValue * scale;
            ushort encoded = (ushort)Math.Clamp(MathF.Round((value - offset) / scale), 0, ushort.MaxValue);
            Assert.InRange(MathF.Abs(value - QuantizedAnimationDecoder.DecodeScalar(encoded, scale, offset)), 0, scale * 0.51f);
        }
        Assert.Equal(offset, QuantizedAnimationDecoder.DecodeScalar(65535, 0, offset));
    }

    [Fact]
    public void InvalidValuesAndTruncatedQuaternionsAreRefused()
    {
        for (int count = 0; count < 6; count++)
            Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.DecodeQuaternion(new byte[count]));
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.DecodeQuaternion(new byte[6]));
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.DecodeScalar(1, float.NaN, 0));
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.DecodeScalar(1, 0, float.PositiveInfinity));
        Assert.Throws<InvalidDataException>(() => QuantizedAnimationDecoder.DecodeScalar(65535, float.MaxValue, 0));
    }

    private static byte[] Encode(Quaternion q, out int omitted, out bool negative)
    {
        float[] values = { q.X, q.Y, q.Z, q.W };
        omitted = 0;
        for (int i = 1; i < 4; i++)
            if (MathF.Abs(values[i]) > MathF.Abs(values[omitted])) omitted = i;
        negative = values[omitted] < 0;
        var words = new ushort[3];
        for (int i = 0, j = 0; i < 4; i++)
            if (i != omitted)
                words[j++] = (ushort)Math.Clamp(MathF.Round((values[i] + 0.70710677f) / 4.3161006e-05f), 0, 32767);
        words[0] |= (ushort)((omitted & 1) << 15);
        words[1] |= (ushort)(((omitted >> 1) & 1) << 15);
        words[2] |= (ushort)(negative ? 0x8000 : 0);
        var bytes = new byte[6];
        for (int i = 0; i < 3; i++) BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(i * 2), words[i]);
        return bytes;
    }
}
