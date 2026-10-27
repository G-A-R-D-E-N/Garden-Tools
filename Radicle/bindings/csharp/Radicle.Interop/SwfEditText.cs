using System;
using System.Collections.Generic;
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

// Constraint: tag 37 is re-encoded field for field; an edit reuses the original id/RECT bytes unless bounds change.
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
                var current = (_data[_bit >> 3] >> (7 - (_bit & 7))) & 1u;
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

    private static byte[] EncodeRect(SwfRectRecord rect)
    {
        var bits = new List<byte>();
        var pending = 0u;
        var filled = 0;
        void Push(uint value, int count)
        {
            for (var index = 0; index < count; ++index)
            {
                pending = (pending << 1) | ((value >> (count - 1 - index)) & 1u);
                if (++filled == 8)
                {
                    bits.Add((byte)pending);
                    pending = 0;
                    filled = 0;
                }
            }
        }
        static int Needed(int value)
        {
            for (var count = 1; count <= 31; ++count)
            {
                var limit = 1L << (count - 1);
                if (value >= -limit && value <= limit - 1) return count;
            }
            return 32;
        }
        var nbits = Math.Max(Math.Max(Needed(rect.XMin), Needed(rect.XMax)),
                             Math.Max(Needed(rect.YMin), Needed(rect.YMax)));
        if (nbits > 31) throw new ArgumentOutOfRangeException(nameof(rect), "the bounds do not fit a RECT field");
        Push((uint)nbits, 5);
        var mask = (1u << nbits) - 1u;
        Push((uint)rect.XMin & mask, nbits);
        Push((uint)rect.XMax & mask, nbits);
        Push((uint)rect.YMin & mask, nbits);
        Push((uint)rect.YMax & mask, nbits);
        if (filled != 0) Push(0, 8 - filled);
        return bits.ToArray();
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

    private static void U16(List<byte> bytes, int value)
    {
        bytes.Add((byte)value);
        bytes.Add((byte)(value >> 8));
    }

    private static void Z(List<byte> bytes, string text)
    {
        bytes.AddRange(Encoding.UTF8.GetBytes(text));
        bytes.Add(0);
    }

    public static byte[] Write(SwfEditTextRecord record, byte[] originalPayload, SwfEditTextEdit edit)
    {
        var flags1 = record.Flags1;
        var flags2 = record.Flags2;
        if (edit.Text is not null) flags1 |= 0x80;
        if (edit.Color is not null) flags1 |= 0x04;
        var tail = new List<byte> { flags1, flags2 };
        if ((flags1 & 0x01) != 0) U16(tail, record.FontId ?? 0);
        if ((flags2 & 0x80) != 0) Z(tail, record.FontClass);
        if ((flags1 & 0x01) != 0 || (flags2 & 0x80) != 0) U16(tail, record.FontHeightTwips ?? 0);
        if ((flags1 & 0x04) != 0)
        {
            var color = edit.Color ?? record.Color ?? 0u;
            tail.Add((byte)((color >> 16) & 0xFF));
            tail.Add((byte)((color >> 8) & 0xFF));
            tail.Add((byte)(color & 0xFF));
            tail.Add((byte)((color >> 24) & 0xFF));
        }
        if ((flags1 & 0x02) != 0) U16(tail, record.MaxLength ?? 0);
        if ((flags2 & 0x20) != 0)
        {
            var layout = record.Layout ?? throw new InvalidOperationException("the layout flag is set but no layout was decoded");
            tail.Add(layout.Align);
            U16(tail, layout.LeftMargin);
            U16(tail, layout.RightMargin);
            U16(tail, layout.Indent);
            U16(tail, layout.Leading);
        }
        Z(tail, record.Variable);
        if ((flags1 & 0x80) != 0) Z(tail, edit.Text ?? record.Text);

        if (edit.Bounds is null)
        {
            _ = ReadRect(originalPayload, out var afterRect);
            var prefix = new List<byte>(originalPayload.Length);
            for (var index = 0; index < afterRect; ++index) prefix.Add(originalPayload[index]);
            prefix.AddRange(tail);
            return prefix.ToArray();
        }
        var payload = new List<byte>();
        U16(payload, record.CharacterId);
        payload.AddRange(EncodeRect(edit.Bounds.Value));
        payload.AddRange(tail);
        return payload.ToArray();
    }
}

public sealed partial class SwfFile
{
    // Constraint: the tag index must name a decoded DefineEditText; the payload is re-encoded through the plan.
    public SwfEditTextRecord EditText(int tagIndex) =>
        SwfEditTextCodec.Parse(tagIndex, Payload(tagIndex));

    public SwfPlanResultRecord EditEditText(int tagIndex, SwfEditTextEdit edit)
    {
        var record = EditText(tagIndex);
        var payload = SwfEditTextCodec.Write(record, Payload(tagIndex), edit);
        return ApplyPlan(new SwfPlanSpec().Replace(tagIndex, payload));
    }
}
