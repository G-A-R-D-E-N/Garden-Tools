using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfPlacementMatrix(
    bool HasScale, int ScaleX, int ScaleY, bool HasRotate, int Rotate0, int Rotate1,
    int TranslateX, int TranslateY);

public sealed record SwfPlacementTransformEdit(
    int? TranslateX = null,
    int? TranslateY = null,
    int? ScaleX = null,
    int? ScaleY = null,
    int? Rotate0 = null,
    int? Rotate1 = null);

// Constraint: the MATRIX is a bit-packed block between byte-aligned fields, so only its span is replaced.
public static class SwfPlacementMatrixCodec
{
    private sealed class Bits
    {
        private readonly byte[] _data;
        private int _bit;

        public Bits(byte[] data, int bitOffset) => (_data, _bit) = (data, bitOffset);

        public int Bit => _bit;

        public uint Read(int count)
        {
            uint value = 0;
            for (var index = 0; index < count; ++index)
            {
                value = (value << 1) | (uint)((_data[_bit >> 3] >> (7 - (_bit & 7))) & 1);
                ++_bit;
            }
            return value;
        }

        public int Signed(int count)
        {
            var raw = Read(count);
            var sign = 1u << (count - 1);
            return (int)((raw ^ sign) - sign);
        }

        public void Align() => _bit = (_bit + 7) & ~7;
    }

    private static int MatrixOffset(int code, byte[] payload, out ushort? characterId)
    {
        characterId = null;
        switch (code)
        {
            case 4:
                if (payload.Length < 4) throw new ArgumentException("the placement payload is too short");
                characterId = (ushort)(payload[0] | (payload[1] << 8));
                return 4;
            case 26:
            {
                if (payload.Length < 3) throw new ArgumentException("the placement payload is too short");
                var flags = payload[0];
                var offset = 3;
                if ((flags & 0x02) != 0)
                {
                    if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
                    characterId = (ushort)(payload[offset] | (payload[offset + 1] << 8));
                    offset += 2;
                }
                return offset;
            }
            case 70:
            {
                if (payload.Length < 4) throw new ArgumentException("the placement payload is too short");
                var flags1 = payload[0];
                var flags2 = payload[1];
                var offset = 4;
                if ((flags2 & 0x20) != 0)
                {
                    while (offset < payload.Length && payload[offset] != 0) ++offset;
                    ++offset;
                }
                if ((flags1 & 0x02) != 0 || (flags2 & 0x40) != 0)
                {
                    if (payload.Length < offset + 2) throw new ArgumentException("the placement payload is too short");
                    characterId = (ushort)(payload[offset] | (payload[offset + 1] << 8));
                    offset += 2;
                }
                return offset;
            }
            default:
                throw new ArgumentException("the tag is not a placement");
        }
    }

    private static bool HasMatrix(int code, byte[] payload) => code switch
    {
        4 => true,
        26 => (payload[0] & 0x01) != 0,
        70 => (payload[0] & 0x04) != 0,
        _ => false,
    };

    public static SwfPlacementMatrix Read(int code, byte[] payload, out int matrixStart, out int matrixEnd)
    {
        matrixStart = MatrixOffset(code, payload, out _);
        if (!HasMatrix(code, payload))
        {
            matrixEnd = matrixStart;
            return new SwfPlacementMatrix(false, 0, 0, false, 0, 0, 0, 0);
        }
        var bits = new Bits(payload, matrixStart * 8);
        var hasScale = bits.Read(1) != 0;
        var scaleX = 0;
        var scaleY = 0;
        if (hasScale)
        {
            var nbits = (int)bits.Read(5);
            scaleX = bits.Signed(nbits);
            scaleY = bits.Signed(nbits);
        }
        var hasRotate = bits.Read(1) != 0;
        var rotate0 = 0;
        var rotate1 = 0;
        if (hasRotate)
        {
            var nbits = (int)bits.Read(5);
            rotate0 = bits.Signed(nbits);
            rotate1 = bits.Signed(nbits);
        }
        var nbitsTranslate = (int)bits.Read(5);
        var translateX = bits.Signed(nbitsTranslate);
        var translateY = bits.Signed(nbitsTranslate);
        bits.Align();
        matrixEnd = bits.Bit / 8;
        return new SwfPlacementMatrix(hasScale, scaleX, scaleY, hasRotate, rotate0, rotate1,
                                      translateX, translateY);
    }

    private static int Needed(int value)
    {
        for (var count = 1; count <= 31; ++count)
        {
            var limit = 1L << (count - 1);
            if (value >= -limit && value <= limit - 1) return count;
        }
        throw new ArgumentOutOfRangeException(nameof(value), "the matrix field does not fit 31 bits");
    }

    public static byte[] Write(SwfPlacementMatrix matrix, int code, byte[] payload, SwfPlacementTransformEdit edit)
    {
        Read(code, payload, out var matrixStart, out var matrixEnd);
        var scaleX = edit.ScaleX ?? matrix.ScaleX;
        var scaleY = edit.ScaleY ?? matrix.ScaleY;
        var rotate0 = edit.Rotate0 ?? matrix.Rotate0;
        var rotate1 = edit.Rotate1 ?? matrix.Rotate1;
        var translateX = edit.TranslateX ?? matrix.TranslateX;
        var translateY = edit.TranslateY ?? matrix.TranslateY;
        var hasScale = matrix.HasScale || edit.ScaleX is not null || edit.ScaleY is not null;
        var hasRotate = matrix.HasRotate || edit.Rotate0 is not null || edit.Rotate1 is not null;
        if (!hasScale) { scaleX = 0; scaleY = 0; }
        if (!hasRotate) { rotate0 = 0; rotate1 = 0; }

        var bytes = new List<byte>();
        var pending = 0u;
        var filled = 0;
        void Push(uint value, int count)
        {
            for (var index = 0; index < count; ++index)
            {
                pending = (pending << 1) | ((value >> (count - 1 - index)) & 1);
                if (++filled == 8) { bytes.Add((byte)pending); pending = 0; filled = 0; }
            }
        }
        Push(hasScale ? 1u : 0u, 1);
        if (hasScale)
        {
            var nbits = Math.Max(Needed(scaleX), Needed(scaleY));
            Push((uint)nbits, 5);
            var mask = (1u << nbits) - 1;
            Push((uint)scaleX & mask, nbits);
            Push((uint)scaleY & mask, nbits);
        }
        Push(hasRotate ? 1u : 0u, 1);
        if (hasRotate)
        {
            var nbits = Math.Max(Needed(rotate0), Needed(rotate1));
            Push((uint)nbits, 5);
            var mask = (1u << nbits) - 1;
            Push((uint)rotate0 & mask, nbits);
            Push((uint)rotate1 & mask, nbits);
        }
        var translateBits = Math.Max(Needed(translateX), Needed(translateY));
        Push((uint)translateBits, 5);
        var translateMask = (1u << translateBits) - 1;
        Push((uint)translateX & translateMask, translateBits);
        Push((uint)translateY & translateMask, translateBits);
        if (filled != 0) Push(0, 8 - filled);

        var result = new List<byte>(payload.Length - (matrixEnd - matrixStart) + bytes.Count);
        for (var index = 0; index < matrixStart; ++index) result.Add(payload[index]);
        result.AddRange(bytes);
        for (var index = matrixEnd; index < payload.Length; ++index) result.Add(payload[index]);
        return result.ToArray();
    }
}

public sealed partial class SwfFile
{
    // Constraint: the transform edit replaces only the MATRIX span and keeps every other placement byte.
    public SwfPlacementMatrix PlacementMatrix(int tagIndex)
    {
        var tag = Tags[tagIndex];
        return SwfPlacementMatrixCodec.Read(tag.Code, Payload(tagIndex), out _, out _);
    }

    public SwfPlanResultRecord SetPlacementTransform(int tagIndex, SwfPlacementTransformEdit edit)
    {
        var tag = Tags[tagIndex];
        var payload = Payload(tagIndex);
        var matrix = SwfPlacementMatrixCodec.Read(tag.Code, payload, out _, out _);
        var next = SwfPlacementMatrixCodec.Write(matrix, tag.Code, payload, edit);
        return ApplyPlan(new SwfPlanSpec().Replace(tagIndex, next));
    }
}