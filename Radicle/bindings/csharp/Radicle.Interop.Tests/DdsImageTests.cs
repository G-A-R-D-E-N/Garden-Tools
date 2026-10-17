using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public class DdsImageTests
{
    private static void PutU32(byte[] bytes, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset, 4), value);

    // Mirrors Radicle's build_dds_header: a canonical DX10 header for one layout.
    private static byte[] Dx10Dds(byte dxgi, byte[] surface, int mipCount = 1, int width = 4, int height = 4)
    {
        var bytes = new byte[148];
        PutU32(bytes, 0, 0x20534444U);
        PutU32(bytes, 4, 124);
        PutU32(bytes, 12, (uint)height);
        PutU32(bytes, 16, (uint)width);
        PutU32(bytes, 28, (uint)mipCount);
        PutU32(bytes, 76, 32);
        PutU32(bytes, 80, 0x4U);
        PutU32(bytes, 84, 0x30315844U);
        PutU32(bytes, 108, 0x1000);
        PutU32(bytes, 128, dxgi);
        PutU32(bytes, 132, 3);
        PutU32(bytes, 140, 1);
        var result = new List<byte>(bytes);
        result.AddRange(surface);
        return result.ToArray();
    }

    private static byte[] Bc1White() => new byte[] { 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    [Fact]
    public void ReportsTheHeader()
    {
        var image = DdsImage.FromBytes(Dx10Dds(71, Bc1White()));
        Assert.Equal(71U, image.Format);
        Assert.Equal(1, image.MipCount);
        Assert.Equal(4, image.Width);
        Assert.Equal(4, image.Height);
        Assert.Equal(8, image.PayloadSize);
        Assert.False(image.IsBc5);
    }

    [Fact]
    public void DecodesBc1Pixels()
    {
        var image = DdsImage.FromBytes(Dx10Dds(71, Bc1White()));
        var pixels = image.DecodePixels();
        Assert.Equal(4, pixels.Width);
        Assert.Equal(4, pixels.Height);
        Assert.Equal(64, pixels.Rgba.Length);
        Assert.Equal(new byte[] { 255, 255, 255, 255 }, pixels.Rgba[..4]);
    }

    [Fact]
    public void DecodesBc5WithReconstructedZ()
    {
        var bc4 = new byte[] { 255, 0, 0, 0, 0, 0, 0, 0 };
        var surface = new List<byte>(bc4);
        surface.AddRange(bc4);
        var image = DdsImage.FromBytes(Dx10Dds(83, surface.ToArray()));
        Assert.True(image.IsBc5);
        Assert.Equal(new byte[] { 255, 255, 0, 255 }, image.DecodePixels().Rgba[..4]);
        Assert.Equal(new byte[] { 255, 255, 128, 255 }, image.DecodePixels(reconstructZ: true).Rgba[..4]);
    }

    [Fact]
    public void DecodesUncompressedRgba8()
    {
        var surface = new byte[64];
        for (var i = 0; i < 16; ++i)
        {
            surface[i * 4] = (byte)i;
            surface[i * 4 + 1] = (byte)(i + 16);
            surface[i * 4 + 2] = (byte)(i + 32);
            surface[i * 4 + 3] = (byte)(i + 48);
        }
        var image = DdsImage.FromBytes(Dx10Dds(28, surface));
        Assert.Equal(28U, image.Format);
        Assert.Equal(new byte[] { 0, 16, 32, 48 }, image.Decode()[..4]);
    }

    [Fact]
    public void DecodesTheRequestedMip()
    {
        var surface = Bc1White();
        var second = new byte[] { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        var both = new List<byte>(surface);
        both.AddRange(second);
        var image = DdsImage.FromBytes(Dx10Dds(71, both.ToArray(), mipCount: 2));
        Assert.Equal(2, image.MipCount);
        Assert.Equal(new byte[] { 255, 255, 255, 255 }, image.Decode(0)[..4]);
        Assert.Equal(new byte[] { 0, 0, 0, 0 }, image.Decode(1)[..4]);
        Assert.Throws<ArgumentOutOfRangeException>(() => image.Decode(2));
    }

    private static void ReadEmpty() => DdsImage.FromBytes(ReadOnlySpan<byte>.Empty);

    [Fact]
    public void RejectsMalformedInput()
    {
        Assert.Throws<ArgumentException>(ReadEmpty);
        var junk = new byte[64];
        var error = Assert.Throws<RadicleException>(() => DdsImage.FromBytes(junk));
        Assert.Equal(Native.Status.ParseError, error.Status);
    }
}
