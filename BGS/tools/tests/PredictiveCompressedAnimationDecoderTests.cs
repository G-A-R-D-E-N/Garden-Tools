using System;
using System.Collections.Generic;
using System.IO;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class PredictiveCompressedAnimationDecoderTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    [InlineData(5)]
    [InlineData(6)]
    [InlineData(7)]
    [InlineData(8)]
    [InlineData(9)]
    [InlineData(10)]
    [InlineData(11)]
    [InlineData(12)]
    [InlineData(13)]
    [InlineData(14)]
    public void SignedFieldsAndRandomBlocksRoundTrip(int width)
    {
        int limit = width == 0 ? 0 : 1 << (width - 1);
        uint mask = (1u << width) - 1;
        foreach (int value in width == 0 ? new[] { 0 } : new[] { -limit, -1, 0, limit - 1 })
            Assert.Equal(unchecked((short)(value * 4)), PredictiveCompressedAnimationDecoder.DecodeResidual(width, (uint)value & mask));
        var random = new Random(16773 + width);
        for (int iteration = 0; iteration < 30; iteration++)
        {
            var fields = new int[16, 16];
            for (int t = 0; t < 16; t++)
                for (int s = 0; s < 16; s++)
                    fields[t, s] = width == 0 ? 0 : random.Next(-limit, limit);
            byte[] encoded = PredictiveTestEncoder.Fields(fields, width);
            int[,] decoded = PredictiveCompressedAnimationDecoder.DecodeBlock(encoded, 0, encoded.Length, out int cursor);
            Assert.Equal(encoded.Length, cursor);
            int halfBytes = (4 * (4 + 2 * width) + 7) / 8;
            Assert.Equal((halfBytes - 1) * 17, encoded[0]);
            for (int t = 0; t < 16; t++)
                for (int s = 0; s < 16; s++)
                {
                    long expected = 0;
                    for (int j = 0; j <= s; j++)
                        expected += (long)fields[t, j] * 4 * (s - j + 1) * (s - j + 2) / 2;
                    Assert.Equal(unchecked((short)expected) >> 2, decoded[t, s]);
                }
        }
    }

    [Fact]
    public void InverseThirdDifferencesWrapAtEveryPass()
    {
        var random = new Random(683);
        for (int iteration = 0; iteration < 100; iteration++)
        {
            var samples = new int[16, 16];
            for (int t = 0; t < 16; t++)
                for (int s = 0; s < 16; s++) samples[t, s] = random.Next(-8192, 8192);
            byte[] encoded = PredictiveTestEncoder.Samples(samples);
            int[,] decoded = PredictiveCompressedAnimationDecoder.DecodeBlock(encoded, 0, encoded.Length, out _);
            for (int t = 0; t < 16; t++)
                for (int s = 0; s < 16; s++) Assert.Equal(samples[t, s], decoded[t, s]);
        }
    }

    [Fact]
    public void ReverseLookbehindIncludesPrecedingBytes()
    {
        var bytes = new byte[48];
        bytes[0] = 0;
        bytes[16] = 0x71;
        int[,] decoded = PredictiveCompressedAnimationDecoder.DecodeBlock(bytes, 0, bytes.Length, out int cursor);
        Assert.Equal(48, cursor);
        Assert.Equal(-272, decoded[0, 15]);
        Assert.Equal(0x0102030405060708UL, PredictiveCompressedAnimationDecoder.ReverseLittleEndianWindow(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }));
    }

    [Fact]
    public void UndefinedWidthsTruncationOffsetsAndNonfiniteValuesRefuse()
    {
        Assert.Throws<NotSupportedException>(() => PredictiveCompressedAnimationDecoder.DecodeResidual(15, 0));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.DecodeResidual(0, 1));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.DecodeBlock(new byte[15], 0, 15, out _));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.DecodeBlock(new byte[47], 0, 47, out _));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.ReconstructSamples(new int[15]));
        ushort[] table = { 999, 3, 1, 45, 0 };
        Assert.Equal(65539, PredictiveCompressedAnimationDecoder.ReadBlockOffset(table, 1, 0));
        Assert.Equal(45, PredictiveCompressedAnimationDecoder.ReadBlockOffset(table, 1, 1));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.ReadBlockOffset(table, 1, 2));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.ReadBlockOffset(table, int.MaxValue, int.MaxValue));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.ReadBlockOffset(new ushort[] { 0, 0x8000 }, 0, 0));
        Assert.Equal(1.75f, PredictiveCompressedAnimationDecoder.Dequantize(7, 0.25f, 0));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.Dequantize(1, float.NaN, 0));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.Dequantize(1, 0, float.PositiveInfinity));
        Assert.Throws<InvalidDataException>(() => PredictiveCompressedAnimationDecoder.Dequantize(8191, float.MaxValue, 0));
        for (int frame = 0; frame < 48; frame++) Assert.Equal(frame / 15, PredictiveCompressedAnimationDecoder.BlockForFrame(frame));
    }
}

internal static class PredictiveTestEncoder
{
    internal static byte[] Samples(int[,] samples)
    {
        var fields = new int[16, 16];
        for (int t = 0; t < 16; t++)
        {
            var differences = new short[16];
            for (int s = 0; s < 16; s++) differences[s] = unchecked((short)(samples[t, s] * 4));
            for (int pass = 0; pass < 3; pass++)
                for (int s = 15; s > 0; s--) differences[s] = unchecked((short)(differences[s] - differences[s - 1]));
            for (int s = 0; s < 16; s++) fields[t, s] = differences[s] >> 2;
        }
        return Fields(fields);
    }

    internal static byte[] Fields(int[,] fields, int? forcedWidth = null)
    {
        var bytes = new List<byte>(new byte[16]);
        for (int t = 0; t < 16; t++)
        {
            int descriptor = 0;
            for (int half = 0; half < 2; half++)
            {
                UInt128 bits = 0;
                int position = 0;
                for (int pair = 0; pair < 4; pair++)
                {
                    int a = fields[t, half * 8 + pair * 2], b = fields[t, half * 8 + pair * 2 + 1];
                    int width = forcedWidth ?? 0;
                    if (forcedWidth == null)
                        while (width < 14 && (width == 0 ? a != 0 || b != 0 : a < -(1 << (width - 1)) || a >= (1 << (width - 1)) || b < -(1 << (width - 1)) || b >= (1 << (width - 1)))) width++;
                    uint mask = (1u << width) - 1;
                    bits |= (UInt128)(uint)width << position;
                    bits |= (UInt128)((uint)a & mask) << (position + 4);
                    bits |= (UInt128)((uint)b & mask) << (position + 4 + width);
                    position += 4 + 2 * width;
                }
                int count = (position + 7) / 8;
                descriptor |= (count - 1) << (half == 0 ? 4 : 0);
                for (int i = count - 1; i >= 0; i--) bytes.Add((byte)(bits >> (8 * i)));
            }
            bytes[t] = (byte)descriptor;
        }
        return bytes.ToArray();
    }
}
