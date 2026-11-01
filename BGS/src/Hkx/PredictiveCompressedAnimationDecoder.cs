using System;
using System.Buffers.Binary;
using System.IO;

namespace OpenCommonwealth.Services.Hkx;

public static class PredictiveCompressedAnimationDecoder
{
    public const int SamplesPerBlock = 16;
    public const int FrameStride = 15;

    public static int DecodeResidual(int width, uint bits)
    {
        if (width < 0 || width > 14)
            throw new NotSupportedException($"Predictive animation residual width {width} is refused; only widths 0..14 have supported game-decoder arithmetic. The file has not been changed.");
        uint mask = (1u << width) - 1;
        if ((bits & ~mask) != 0)
            throw new InvalidDataException("Predictive animation residual exceeds its declared width.");
        if (width == 0) return 0;
        int signed = (int)(bits << (32 - width)) >> (32 - width);
        return unchecked((short)(signed * 4));
    }

    public static int[] ReconstructSamples(ReadOnlySpan<int> residuals)
    {
        if (residuals.Length != SamplesPerBlock)
            throw new InvalidDataException("Predictive animation requires sixteen residuals per block.");
        var values = residuals.ToArray();
        for (int i = 0; i < values.Length; i++) values[i] = unchecked((short)values[i]);
        for (int pass = 0; pass < 3; pass++)
            for (int i = 1; i < values.Length; i++)
                values[i] = unchecked((short)(values[i] + values[i - 1]));
        for (int i = 0; i < values.Length; i++) values[i] >>= 2;
        return values;
    }

    public static int[,] DecodeBlock(ReadOnlySpan<byte> data, int start, int end, out int cursor, int lanes = 16)
    {
        if (start < 0 || end < start || end > data.Length || end - start < 16)
            throw new InvalidDataException("Predictive animation block descriptors are truncated.");
        var output = new int[16, 16];
        cursor = start + 16;
        for (int track = 0; track < 16; track++)
        {
            int descriptor = data[start + track];
            int first = (descriptor >> 4) + 1;
            int second = (descriptor & 15) + 1;
            if (first + second > end - cursor)
                throw new InvalidDataException("Predictive animation block payload is truncated.");
            if (track >= lanes)
            {
                cursor += first + second;
                continue;
            }
            var residuals = new int[16];
            DecodeHalf(data, cursor + first, residuals.AsSpan(0, 8));
            DecodeHalf(data, cursor + first + second, residuals.AsSpan(8, 8));
            cursor += first + second;
            int[] samples = ReconstructSamples(residuals);
            for (int sample = 0; sample < 16; sample++) output[track, sample] = samples[sample];
        }
        return output;
    }

    private static void DecodeHalf(ReadOnlySpan<byte> data, int end, Span<int> residuals)
    {
        if (end < 16 || end > data.Length)
            throw new InvalidDataException("Predictive animation half-window look-behind is outside compressedData.");
        UInt128 window = 0;
        for (int i = 0; i < 16; i++) window |= (UInt128)data[end - i - 1] << (8 * i);
        for (int pair = 0; pair < 4; pair++)
        {
            int width = (int)(window & 15);
            uint mask = (1u << width) - 1;
            residuals[pair * 2] = DecodeResidual(width, (uint)((window >> 4) & mask));
            residuals[pair * 2 + 1] = DecodeResidual(width, (uint)((window >> (4 + width)) & mask));
            window >>= 4 + width * 2;
        }
    }

    public static int BlockForFrame(int frame)
    {
        if (frame < 0) throw new ArgumentOutOfRangeException(nameof(frame));
        return frame / FrameStride;
    }

    public static int ReadBlockOffset(ReadOnlySpan<ushort> intData, int tableOffset, int block)
    {
        long index = (long)tableOffset + (long)block * 2;
        if (tableOffset < 0 || block < 0 || index < 0 || index + 2 > intData.Length)
            throw new InvalidDataException("Predictive animation block offset is outside intData.");
        uint offset = intData[(int)index] | ((uint)intData[(int)index + 1] << 16);
        if (offset > int.MaxValue)
            throw new InvalidDataException("Predictive animation block byte offset exceeds the supported range.");
        return (int)offset;
    }

    public static ulong ReverseLittleEndianWindow(ReadOnlySpan<byte> data)
    {
        if (data.Length < 8)
            throw new InvalidDataException("Predictive animation window requires eight bytes.");
        return BinaryPrimitives.ReverseEndianness(BinaryPrimitives.ReadUInt64LittleEndian(data));
    }

    public static float Dequantize(int value, float scale, float offset)
    {
        float decoded = value * scale + offset;
        if (!float.IsFinite(scale) || !float.IsFinite(offset) || !float.IsFinite(decoded))
            throw new InvalidDataException("Predictive animation dequantization is not finite.");
        return decoded;
    }
}
