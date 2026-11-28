using System;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct SwfEditTextLayoutRecord(
    byte Align, ushort LeftMargin, ushort RightMargin, ushort Indent, short Leading);

public sealed record SwfEditTextRecord(
    int TagIndex,
    ushort CharacterId,
    SwfRectRecord Bounds,
    byte Flags1,
    byte Flags2,
    ushort? FontId,
    string FontClass,
    ushort? FontHeightTwips,
    uint? Color,
    ushort? MaxLength,
    SwfEditTextLayoutRecord? Layout,
    string Variable,
    string Text);

public sealed record SwfEditTextEdit(string? Text = null, uint? Color = null, SwfRectRecord? Bounds = null);

public static class SwfEditTextCodec
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
                var current = (uint)((_data[_bit >> 3] >> (7 - (_bit & 7))) & 1);
                value = (value << 1) | current;
                ++_bit;
            }
            return value;
        }

        public int ReadSigned(int count)
        {
            var raw = Read(count);
            var sign = 1u << (count - 1);
            return (int)((raw ^ sign) - sign);
        }

        public void Align() => _bit = (_bit + 7) & ~7;
    }

    private static SwfRectRecord ReadRect(byte[] payload, out int next)
    {
        var bits = new Bits(payload, 16);
        var nbits = (int)bits.Read(5);
        var rect = new SwfRectRecord(bits.ReadSigned(nbits), bits.ReadSigned(nbits),
                                     bits.ReadSigned(nbits), bits.ReadSigned(nbits));
        bits.Align();
        next = bits.Bit / 8;
        return rect;
    }

    private static string ReadZ(byte[] payload, ref int offset)
    {
        var start = offset;
        while (offset < payload.Length && payload[offset] != 0) ++offset;
        var text = Encoding.UTF8.GetString(payload, start, offset - start);
        if (offset < payload.Length) ++offset;
        return text;
    }

    public static SwfEditTextRecord Parse(int tagIndex, byte[] payload)
    {
        if (payload.Length < 4) throw new ArgumentException("the payload is too short for DefineEditText", nameof(payload));
        var id = (ushort)(payload[0] | (payload[1] << 8));
        var bounds = ReadRect(payload, out var offset);
        var flags1 = payload[offset++];
        var flags2 = payload[offset++];
        ushort? fontId = null;
        if ((flags1 & 0x01) != 0)
        {
            fontId = (ushort)(payload[offset] | (payload[offset + 1] << 8));
            offset += 2;
        }
        var fontClass = (flags2 & 0x80) != 0 ? ReadZ(payload, ref offset) : string.Empty;
        ushort? height = null;
        if ((flags1 & 0x01) != 0 || (flags2 & 0x80) != 0)
        {
            height = (ushort)(payload[offset] | (payload[offset + 1] << 8));
            offset += 2;
        }
        uint? color = null;
        if ((flags1 & 0x04) != 0)
        {
            color = ((uint)payload[offset + 3] << 24) | ((uint)payload[offset] << 16) |
                    ((uint)payload[offset + 1] << 8) | payload[offset + 2];
            offset += 4;
        }
        ushort? maxLength = null;
        if ((flags1 & 0x02) != 0)
        {
            maxLength = (ushort)(payload[offset] | (payload[offset + 1] << 8));
            offset += 2;
        }
        SwfEditTextLayoutRecord? layout = null;
        if ((flags2 & 0x20) != 0)
        {
            layout = new SwfEditTextLayoutRecord(payload[offset],
                (ushort)(payload[offset + 1] | (payload[offset + 2] << 8)),
                (ushort)(payload[offset + 3] | (payload[offset + 4] << 8)),
                (ushort)(payload[offset + 5] | (payload[offset + 6] << 8)),
                (short)(payload[offset + 7] | (payload[offset + 8] << 8)));
            offset += 9;
        }
        var variable = ReadZ(payload, ref offset);
        var text = (flags1 & 0x80) != 0 ? ReadZ(payload, ref offset) : string.Empty;
        return new SwfEditTextRecord(tagIndex, id, bounds, flags1, flags2, fontId, fontClass, height,
                                     color, maxLength, layout, variable, text);
    }

}

public sealed partial class SwfFile
{
    // Constraint: the tag index must name a decoded DefineEditText.
    public SwfEditTextRecord EditText(int tagIndex) =>
        SwfEditTextCodec.Parse(tagIndex, Payload(tagIndex));
}
