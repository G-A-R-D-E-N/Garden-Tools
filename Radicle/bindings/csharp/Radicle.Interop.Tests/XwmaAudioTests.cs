using System;
using System.Buffers.Binary;
using Radicle.Interop;
using Xunit;

namespace Radicle.Interop.Tests;

public class XwmaAudioTests
{
    [Fact]
    public void RejectsBytesThatAreNotASoundContainer()
    {
        Assert.ThrowsAny<Exception>(() => XwmaAudio.Decode(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }));
    }

    [Fact]
    public void RejectsAnEmptyBuffer()
    {
        Assert.Throws<ArgumentException>(() => XwmaAudio.Decode(ReadOnlySpan<byte>.Empty));
    }

    [Fact]
    public void WavHeaderDescribesThePcm()
    {
        var wav = new XwmaPcm(22050, 2, 0, new byte[16]).ToWav();
        Assert.Equal(60, wav.Length);
        Assert.Equal(22050u, BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(24)));
        Assert.Equal((ushort)2, BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(22)));
        Assert.Equal(16u, BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(40)));
        Assert.Equal(52u, BinaryPrimitives.ReadUInt32LittleEndian(wav.AsSpan(4)));
    }
}
