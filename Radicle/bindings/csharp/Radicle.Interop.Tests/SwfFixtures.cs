using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Radicle.Interop.Tests;

// Constraint: the fixture bytes are computed here so the tests own the exact movie the native side reads.
internal sealed class SwfBitWriter
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

internal static class SwfFixtures
{
    public static byte[] U16(int value) => new[] { (byte)value, (byte)(value >> 8) };

    public static byte[] Z(string text)
    {
        var bytes = new List<byte>(Encoding.ASCII.GetBytes(text)) { 0 };
        return bytes.ToArray();
    }

    public static byte[] Tag(int code, byte[] payload, bool longHeader = false)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        if (longHeader || payload.Length >= 0x3F)
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

    private static byte[] EditTextPayload()
    {
        var bytes = new List<byte>();
        bytes.AddRange(U16(11));
        var bits = new SwfBitWriter();
        bits.Rect(0, 400, 0, 200, 12);
        bytes.AddRange(bits.ToArray());
        bytes.Add(0);
        bytes.Add(0);
        bytes.AddRange(Z("variable"));
        return bytes.ToArray();
    }

    private static byte[] PlaceObjectPayload()
    {
        var bytes = new List<byte>();
        bytes.Add(0x03);
        bytes.AddRange(U16(1));
        bytes.AddRange(U16(11));
        bytes.Add(0x00);
        return bytes.ToArray();
    }

    private static byte[] SpritePayload()
    {
        var bytes = new List<byte>();
        bytes.AddRange(U16(14));
        bytes.AddRange(U16(1));
        bytes.AddRange(Tag(43, Z("inner")));
        bytes.AddRange(Tag(1, Array.Empty<byte>()));
        bytes.AddRange(Tag(0, Array.Empty<byte>()));
        return bytes.ToArray();
    }

    public static byte[] Movie()
    {
        var tags = new List<byte>();
        tags.AddRange(Tag(37, EditTextPayload()));
        tags.AddRange(Tag(43, Z("start")));
        tags.AddRange(Tag(26, PlaceObjectPayload()));
        tags.AddRange(Tag(1, Array.Empty<byte>()));
        tags.AddRange(Tag(39, SpritePayload()));
        tags.AddRange(Tag(0, Array.Empty<byte>()));

        var header = new List<byte>();
        header.AddRange(Encoding.ASCII.GetBytes("FWS"));
        header.Add(10);
        var total = 8 + 9 + 4 + tags.Count;
        header.AddRange(BitConverter.GetBytes((uint)total));
        var headBits = new SwfBitWriter();
        headBits.Rect(0, 11000, 0, 8000, 15);
        header.AddRange(headBits.ToArray());
        header.AddRange(U16(0x0C00));
        header.AddRange(U16(1));
        header.AddRange(tags);
        return header.ToArray();
    }
}
