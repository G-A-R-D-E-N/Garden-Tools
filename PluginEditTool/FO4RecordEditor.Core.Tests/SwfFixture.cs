using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace FO4RecordEditor.Core.Tests;

// Constraint: the fixture bytes are computed here so the service tests own the exact movie the native side reads.
internal static class SwfFixture
{
    private sealed class Bits
    {
        private readonly List<byte> _bytes = new();
        private uint _pending;
        private int _filled;

        public void Push(uint value, int count)
        {
            for (var index = 0; index < count; ++index)
            {
                _pending = (_pending << 1) | ((value >> (count - 1 - index)) & 1u);
                if (++_filled == 8)
                {
                    _bytes.Add((byte)_pending);
                    _pending = 0;
                    _filled = 0;
                }
            }
        }

        public void Rect(int xMin, int xMax, int yMin, int yMax, int nbits)
        {
            Push((uint)nbits, 5);
            var mask = (1u << nbits) - 1u;
            Push((uint)xMin & mask, nbits);
            Push((uint)xMax & mask, nbits);
            Push((uint)yMin & mask, nbits);
            Push((uint)yMax & mask, nbits);
            Align();
        }

        public void Align()
        {
            if (_filled != 0) Push(0, 8 - _filled);
        }

        public byte[] ToArray()
        {
            Align();
            return _bytes.ToArray();
        }
    }

    private static byte[] U16(int value) => new[] { (byte)value, (byte)(value >> 8) };

    private static byte[] Z(string text) => Encoding.ASCII.GetBytes(text).Concat(new byte[] { 0 }).ToArray();

    private static byte[] Tag(int code, byte[] payload)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        if (payload.Length >= 0x3F)
        {
            writer.Write((ushort)(((code & 0x3FF) << 6) | 0x3F));
            writer.Write((uint)payload.Length);
        }
        else
        {
            writer.Write((ushort)(((code & 0x3FF) << 6) | payload.Length));
        }
        writer.Write(payload);
        return stream.ToArray();
    }

    public static byte[] Movie()
    {
        var editText = new List<byte>();
        editText.AddRange(U16(11));
        var bounds = new Bits();
        bounds.Rect(0, 400, 0, 200, 12);
        editText.AddRange(bounds.ToArray());
        editText.Add(0);
        editText.Add(0);
        editText.AddRange(Z("variable"));

        var place = new List<byte> { 0x03 };
        place.AddRange(U16(1));
        place.AddRange(U16(11));
        place.Add(0x00);

        var sprite = new List<byte>();
        sprite.AddRange(U16(14));
        sprite.AddRange(U16(1));
        sprite.AddRange(Tag(43, Z("inner")));
        sprite.AddRange(Tag(1, Array.Empty<byte>()));
        sprite.AddRange(Tag(0, Array.Empty<byte>()));

        var tags = new List<byte>();
        tags.AddRange(Tag(37, editText.ToArray()));
        tags.AddRange(Tag(43, Z("start")));
        tags.AddRange(Tag(26, place.ToArray()));
        tags.AddRange(Tag(1, Array.Empty<byte>()));
        tags.AddRange(Tag(39, sprite.ToArray()));
        tags.AddRange(Tag(0, Array.Empty<byte>()));

        var head = new List<byte>();
        head.AddRange(Encoding.ASCII.GetBytes("FWS"));
        head.Add(10);
        head.AddRange(BitConverter.GetBytes((uint)(8 + 9 + 4 + tags.Count)));
        var frame = new Bits();
        frame.Rect(0, 11000, 0, 8000, 15);
        head.AddRange(frame.ToArray());
        head.AddRange(U16(0x0C00));
        head.AddRange(U16(1));
        head.AddRange(tags);
        return head.ToArray();
    }
}
