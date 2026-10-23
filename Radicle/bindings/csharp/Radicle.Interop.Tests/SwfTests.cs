using System;
using System.Collections.Generic;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfTests
{
    private sealed class BitWriter
    {
        private readonly List<bool> _bits = new();

        public void Push(uint value, int count)
        {
            for (var index = count - 1; index >= 0; --index) _bits.Add(((value >> index) & 1u) != 0);
        }

        public byte[] ToBytes()
        {
            while (_bits.Count % 8 != 0) _bits.Add(false);
            var bytes = new byte[_bits.Count / 8];
            for (var index = 0; index < _bits.Count; ++index)
            {
                if (_bits[index]) bytes[index / 8] |= (byte)(1 << (7 - (index % 8)));
            }
            return bytes;
        }
    }

    private static byte[] Rect(int xMin, int xMax, int yMin, int yMax)
    {
        const int bits = 15;
        var writer = new BitWriter();
        writer.Push(bits, 5);
        writer.Push((uint)xMin & 0x7FFFu, bits);
        writer.Push((uint)xMax & 0x7FFFu, bits);
        writer.Push((uint)yMin & 0x7FFFu, bits);
        writer.Push((uint)yMax & 0x7FFFu, bits);
        return writer.ToBytes();
    }

    private static byte[] Tag(ushort code, byte[] payload)
    {
        var bytes = new List<byte>();
        ushort header = (ushort)((code << 6) | (payload.Length & 0x3F));
        if (payload.Length >= 0x3F)
        {
            header = (ushort)((code << 6) | 0x3F);
            bytes.AddRange(BitConverter.GetBytes(header));
            bytes.AddRange(BitConverter.GetBytes((uint)payload.Length));
        }
        else
        {
            bytes.AddRange(BitConverter.GetBytes(header));
        }
        bytes.AddRange(payload);
        return bytes.ToArray();
    }

    // An FWS movie whose tag walk, label decode and payload copy must need no game data.
    private static byte[] Movie()
    {
        var body = new List<byte>();
        body.AddRange(Rect(0, 100, 0, 50));
        body.AddRange(BitConverter.GetBytes((ushort)0x0C00));
        body.AddRange(BitConverter.GetBytes((ushort)1));
        body.AddRange(Tag(69, BitConverter.GetBytes(0x79u)));
        body.AddRange(Tag(43, "start\0"u8.ToArray()));
        body.AddRange(Tag(0, Array.Empty<byte>()));

        var file = new List<byte> { (byte)'F', (byte)'W', (byte)'S', 10 };
        file.AddRange(BitConverter.GetBytes((uint)(body.Count + 8)));
        file.AddRange(body);
        return file.ToArray();
    }

    [Fact]
    public void ReadsTheTagTreeAndFrameLabelsAndCopiesPayloads()
    {
        using var swf = SwfFile.FromBytes(Movie());

        Assert.Equal((byte)10, swf.Info.Version);
        Assert.Equal((ushort)1, swf.Info.FrameCount);
        Assert.Equal(12.0f, swf.Info.FrameRate);
        Assert.True(swf.Info.ActionScript3);
        Assert.True(swf.Info.UseGpu);
        Assert.Equal(3, swf.Info.TagCount);

        Assert.Collection(
            swf.Tags,
            tag =>
            {
                Assert.Equal((ushort)69, tag.Code);
                Assert.Equal("FileAttributes", tag.Name);
                Assert.Equal(-1, tag.Parent);
                Assert.False(tag.InsideSprite);
            },
            tag => Assert.Equal((ushort)43, tag.Code),
            tag => Assert.Equal((ushort)0, tag.Code));

        Assert.Equal(new byte[] { 0x79, 0x00, 0x00, 0x00 }, swf.Payload(0));
        Assert.Single(swf.FrameLabels);
        Assert.Equal("start", swf.FrameLabels[0].Name);
        Assert.Equal(1, swf.FrameLabels[0].TagIndex);
        Assert.Empty(swf.Symbols);
        Assert.Empty(swf.Exports);
        Assert.Empty(swf.AbcBlocks);
    }

    [Fact]
    public void RefusesBytesThatAreNotAMovieAndAHandleThatIsDisposed()
    {
        Assert.Throws<RadicleException>(() => SwfFile.FromBytes(new byte[] { 0x5A, 0x57, 0x53, 13, 0, 0, 0, 0 }));

        var swf = SwfFile.FromBytes(Movie());
        swf.Dispose();
        Assert.Throws<ObjectDisposedException>(() => swf.Payload(0));
    }
}
