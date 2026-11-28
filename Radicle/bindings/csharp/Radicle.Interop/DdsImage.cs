using System;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct DdsPixels(int Width, int Height, byte[] Rgba);

public sealed class DdsImage
{
    private readonly byte[] _bytes;

    private DdsImage(byte[] bytes, uint format, int mipCount, int width, int height, long payloadSize, bool isBc5,
        int dataOffset, int faceCount, bool isCubeMap)
    {
        _bytes = bytes;
        Format = format;
        MipCount = mipCount;
        Width = width;
        Height = height;
        PayloadSize = payloadSize;
        IsBc5 = isBc5;
        DataOffset = dataOffset;
        FaceCount = faceCount;
        IsCubeMap = isCubeMap;
    }

    public uint Format { get; }

    public int MipCount { get; }

    public int Width { get; }

    public int Height { get; }

    public long PayloadSize { get; }

    public bool IsBc5 { get; }

    public int DataOffset { get; }

    public int FaceCount { get; }

    public bool IsCubeMap { get; }

    public static unsafe DdsImage FromBytes(ReadOnlySpan<byte> bytes)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a DDS needs at least a header", nameof(bytes));
        uint format = 0;
        uint mipCount = 0;
        uint width = 0;
        uint height = 0;
        ulong payloadSize = 0;
        int bc5 = 0;
        uint dataOffset = 0;
        uint faceCount = 0;
        int isCubeMap = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.DdsInfo(pointer, (nuint)bytes.Length, &format, &mipCount, &width, &height,
                &payloadSize, &bc5));
            Call.Check(RadicleNative.DdsLayout(pointer, (nuint)bytes.Length, &dataOffset, &faceCount, &isCubeMap));
        }
        return new DdsImage(bytes.ToArray(), format, checked((int)mipCount), checked((int)width), checked((int)height),
            checked((long)payloadSize), bc5 != 0, checked((int)dataOffset), checked((int)faceCount), isCubeMap != 0);
    }

    public unsafe DdsPixels DecodePixels(int mip = 0, bool reconstructZ = false, int face = 0)
    {
        if (mip < 0 || mip >= MipCount) throw new ArgumentOutOfRangeException(nameof(mip));
        if (face < 0 || face >= FaceCount) throw new ArgumentOutOfRangeException(nameof(face));
        byte* rgba = null;
        uint width = 0;
        uint height = 0;
        fixed (byte* pointer = _bytes)
        {
            Call.Check(RadicleNative.DdsDecodeRgbaFace(pointer, (nuint)_bytes.Length, (uint)face, (uint)mip,
                reconstructZ ? (byte)1 : (byte)0, &rgba, &width, &height));
        }
        return new DdsPixels(checked((int)width), checked((int)height), Call.TakeBytes(rgba, (nuint)width * height * 4));
    }

    // Constraint: decodes face 0 only; a cube map needs DecodePixels for any other face.
    public static unsafe DdsPixels DecodeRgba(ReadOnlySpan<byte> bytes, int mip = 0, bool reconstructZ = false)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a DDS needs at least a header", nameof(bytes));
        if (mip < 0) throw new ArgumentOutOfRangeException(nameof(mip));
        byte* rgba = null;
        uint width = 0;
        uint height = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.DdsDecodeRgba(pointer, (nuint)bytes.Length, (uint)mip,
                reconstructZ ? (byte)1 : (byte)0, &rgba, &width, &height));
        }
        return new DdsPixels(checked((int)width), checked((int)height), Call.TakeBytes(rgba, (nuint)width * height * 4));
    }

    public byte[] Decode(int mip = 0, bool reconstructZ = false, int face = 0) =>
        DecodePixels(mip, reconstructZ, face).Rgba;

    // Invariant: the file must be whole, the run is the stored blocks of mips [firstMip, firstMip + mipCount) of one face; unsignedRgtc rewrites signed BC4 and BC5.
    public static unsafe byte[] BlockChain(ReadOnlySpan<byte> bytes, int firstMip, int mipCount, bool unsignedRgtc = false,
        int face = 0)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a DDS needs at least a header", nameof(bytes));
        if (face < 0) throw new ArgumentOutOfRangeException(nameof(face));
        if (firstMip < 0) throw new ArgumentOutOfRangeException(nameof(firstMip));
        if (mipCount <= 0) throw new ArgumentOutOfRangeException(nameof(mipCount));
        byte* blocks = null;
        nuint size = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.DdsBlockChain(pointer, (nuint)bytes.Length, (uint)face, (uint)firstMip, (uint)mipCount,
                unsignedRgtc ? (byte)1 : (byte)0, &blocks, &size));
        }
        return Call.TakeBytes(blocks, size);
    }

    // Requires 1 <= mipCount <= mipsPerFace <= the full chain; face n starts n * (the bytes of mipsPerFace mips) after the header, whatever the header's face count and mip count say.
    public static unsafe byte[] FaceBlocks(ReadOnlySpan<byte> bytes, int face, int mipsPerFace, int mipCount, bool unsignedRgtc = false)
    {
        if (bytes.IsEmpty) throw new ArgumentException("a DDS needs at least a header", nameof(bytes));
        if (face < 0) throw new ArgumentOutOfRangeException(nameof(face));
        if (mipsPerFace <= 0) throw new ArgumentOutOfRangeException(nameof(mipsPerFace));
        if (mipCount <= 0) throw new ArgumentOutOfRangeException(nameof(mipCount));
        byte* blocks = null;
        nuint size = 0;
        fixed (byte* pointer = bytes)
        {
            Call.Check(RadicleNative.DdsFaceBlocks(pointer, (nuint)bytes.Length, (uint)face, (uint)mipsPerFace, (uint)mipCount,
                unsignedRgtc ? (byte)1 : (byte)0, &blocks, &size));
        }
        return Call.TakeBytes(blocks, size);
    }

    public static unsafe byte[] EncodePng(ReadOnlySpan<byte> rgba, int width, int height)
    {
        if (width <= 0) throw new ArgumentOutOfRangeException(nameof(width));
        if (height <= 0) throw new ArgumentOutOfRangeException(nameof(height));
        byte* png = null;
        nuint size = 0;
        fixed (byte* pointer = rgba)
        {
            Call.Check(RadicleNative.PngEncodeRgba(pointer, (nuint)rgba.Length, (uint)width, (uint)height, &png, &size));
        }
        return Call.TakeBytes(png, size);
    }
}
