using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct DdsPixels(int Width, int Height, byte[] Rgba);

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

    public uint Format { get; }

    public int MipCount { get; }

    public int Width { get; }

    public int Height { get; }

    public long PayloadSize { get; }

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

    public byte[] Decode(int mip = 0, bool reconstructZ = false) => DecodePixels(mip, reconstructZ).Rgba;
}
