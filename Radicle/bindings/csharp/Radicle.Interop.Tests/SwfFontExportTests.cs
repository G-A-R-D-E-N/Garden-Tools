using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfFontExportTests
{
    private const int Bits = 14;

    private static void Pair(SwfBitWriter bits, int x, int y)
    {
        bits.Push((uint)x & 0x3FFF, Bits);
        bits.Push((uint)y & 0x3FFF, Bits);
    }

    private static byte[] Square()
    {
        var bits = new SwfBitWriter();
        bits.Push(0x10, 8);
        bits.Push(0, 1);
        bits.Push(0x05, 5);
        bits.Push(Bits, 5);
        Pair(bits, 1000, 1000);
        bits.Push(1, 1);
        foreach (var (dx, dy) in new[] { (4000, 0), (0, 4000), (-4000, 0), (0, -4000) })
        {
            bits.Push(3, 2);
            bits.Push(Bits - 2, 4);
            bits.Push(1, 1);
            Pair(bits, dx, dy);
        }
        bits.Push(0, 6);
        return bits.ToArray();
    }

    private static byte[] Blank() => new byte[] { 0x10, 0x00 };

    private static byte[] FontMovie(bool layout)
    {
        var glyphs = new[] { Square(), Blank() };
        var payload = new List<byte>();
        payload.AddRange(SwfFixtures.U16(5));
        payload.Add((byte)(layout ? 0x84 : 0x04));
        payload.Add(1);
        payload.Add(4);
        payload.AddRange(Encoding.ASCII.GetBytes("Test"));
        payload.AddRange(SwfFixtures.U16(2));
        var offset = 6;
        foreach (var glyph in glyphs)
        {
            payload.AddRange(SwfFixtures.U16(offset));
            offset += glyph.Length;
        }
        payload.AddRange(SwfFixtures.U16(offset));
        foreach (var glyph in glyphs) payload.AddRange(glyph);
        payload.AddRange(SwfFixtures.U16(0x41));
        payload.AddRange(SwfFixtures.U16(0x20));
        if (layout)
        {
            payload.AddRange(SwfFixtures.U16(16000));
            payload.AddRange(SwfFixtures.U16(4000));
            payload.AddRange(SwfFixtures.U16(0));
            payload.AddRange(SwfFixtures.U16(3000));
            payload.AddRange(SwfFixtures.U16(1500));
            for (var glyph = 0; glyph < 2; ++glyph) payload.Add(0);
            payload.AddRange(SwfFixtures.U16(1));
            payload.AddRange(SwfFixtures.U16(0x41));
            payload.AddRange(SwfFixtures.U16(0x20));
            payload.AddRange(SwfFixtures.U16(-100 & 0xFFFF));
        }
        var tags = new List<byte>();
        tags.AddRange(SwfFixtures.Tag(75, payload.ToArray(), true));
        tags.AddRange(SwfFixtures.Tag(0, Array.Empty<byte>()));
        var header = new List<byte>();
        header.AddRange(Encoding.ASCII.GetBytes("FWS"));
        header.Add(10);
        header.AddRange(BitConverter.GetBytes((uint)(8 + 9 + 4 + tags.Count)));
        var frame = new SwfBitWriter();
        frame.Rect(0, 11000, 0, 8000, 15);
        header.AddRange(frame.ToArray());
        header.AddRange(SwfFixtures.U16(0x0C00));
        header.AddRange(SwfFixtures.U16(1));
        header.AddRange(tags);
        return header.ToArray();
    }

    [Fact]
    public void FontMetricsReturnsAdvancesAsEmFractionsAndKerning()
    {
        using var movie = SwfFile.FromBytes(FontMovie(layout: true));
        var metrics = movie.FontMetrics(0);
        Assert.Equal(20480u, metrics.UnitsPerEm);
        Assert.Equal((ushort)16000, metrics.Ascent);
        Assert.Equal((ushort)4000, metrics.Descent);
        Assert.Equal(2, metrics.Advances.Count);
        Assert.Equal(3000.0, metrics.Advances[0] * metrics.UnitsPerEm, 6);
        Assert.Equal(1500.0, metrics.Advances[1] * metrics.UnitsPerEm, 6);
        var pair = Assert.Single(metrics.Kerning);
        Assert.Equal(new SwfFontKerningRecord(0x41, 0x20, -100), pair);
    }

    [Fact]
    public void FontTtfReturnsASfntWithTheGlyphsAndANotdef()
    {
        using var movie = SwfFile.FromBytes(FontMovie(layout: true));
        var ttf = movie.FontTtf(0);
        Assert.Equal(0x00010000u, BinaryPrimitives.ReadUInt32BigEndian(ttf));
        uint sum = 0;
        for (var index = 0; index < ttf.Length; index += 4)
            sum += BinaryPrimitives.ReadUInt32BigEndian(ttf.AsSpan(index, 4));
        Assert.Equal(0xB1B0AFBAu, sum);
        var tables = BinaryPrimitives.ReadUInt16BigEndian(ttf.AsSpan(4, 2));
        for (var index = 0; index < tables; ++index)
        {
            var entry = 12 + 16 * index;
            if (Encoding.ASCII.GetString(ttf, entry, 4) != "maxp") continue;
            var offset = (int)BinaryPrimitives.ReadUInt32BigEndian(ttf.AsSpan(entry + 8, 4));
            Assert.Equal(3, BinaryPrimitives.ReadUInt16BigEndian(ttf.AsSpan(offset + 4, 2)));
            return;
        }
        Assert.Fail("the sfnt has no maxp table");
    }

    [Fact]
    public void FontTtfRefusesAFontWithoutALayoutAndATagThatIsNotAFont()
    {
        using var movie = SwfFile.FromBytes(FontMovie(layout: false));
        Assert.Throws<RadicleException>(() => movie.FontTtf(0));
        Assert.Throws<RadicleException>(() => movie.FontTtf(1));
        Assert.Throws<RadicleException>(() => movie.FontMetrics(1));
        Assert.Empty(movie.FontMetrics(0).Advances);
    }
}
