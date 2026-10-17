using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

/* One decoded mip level: RGBA8 pixels, four bytes per pixel. */
public readonly record struct DdsPixels(int Width, int Height, byte[] Rgba);

/* A DDS container. The bytes are copied when the image is read, so decoding a
   mip later needs no native handle. */
public sealed class DdsImage
{
    private readonly byte[] _bytes;

    private DdsImage(byte[] bytes, uint format, int mipCount, int width, int height, long payloadSize, bool isBc5)
    {
        _bytes = bytes;
        Format = format;
        MipCount = mipCount;
        Width = width;
        Height = height;
        PayloadSize = payloadSize;
        IsBc5 = isBc5;
    }

    /* The DXGI format code: 71 BC1_UNORM, 77 BC3_UNORM, 83 BC5_UNORM, 98 BC7_UNORM and so on. */
    public uint Format { get; }

    public int MipCount { get; }

    public int Width { get; }

    public int Height { get; }

    /* The byte size of every mip together, every array slice and depth slice included. */
    public long PayloadSize { get; }

    /* True for the BC5 normal-map layouts, 82, 83 and 84. */
    public bool IsBc5 { get; }

    public static unsafe DdsImage FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a DDS needs at least a header", nameof(bytes));
        uint format = 0;
        uint mipCount = 0;
        uint width = 0;
        uint height = 0;
        ulong payloadSize = 0;
        int bc5 = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.DdsInfo(pointer, (nuint)bytes.Length, &format, &mipCount, &width, &height,
                &payloadSize, &bc5));
        }
        return new DdsImage(bytes.ToArray(), format, checked((int)mipCount), checked((int)width), checked((int)height),
            checked((long)payloadSize), bc5 != 0);
    }

    /* Decodes one mip level into RGBA8. reconstructZ rebuilds the blue channel of
       an unsigned BC5 normal map and is refused for every other format. */
    public unsafe DdsPixels DecodePixels(int mip = 0, bool reconstructZ = false)
    {
        if (mip < 0 || mip >= MipCount) throw new ArgumentOutOfRangeException(nameof(mip));
        byte* rgba = null;
        uint width = 0;
        uint height = 0;
        fixed (byte* pointer = _bytes)
        {
            Call.Check(RadicleNative.DdsDecodeRgba(pointer, (nuint)_bytes.Length, (uint)mip, reconstructZ ? (byte)1 : (byte)0,
                &rgba, &width, &height));
        }
        return new DdsPixels(checked((int)width), checked((int)height), Call.TakeBytes(rgba, (nuint)width * height * 4));
    }

    /* Decodes one mip level into RGBA8 pixels only. */
    public byte[] Decode(int mip = 0, bool reconstructZ = false) => DecodePixels(mip, reconstructZ).Rgba;
}
