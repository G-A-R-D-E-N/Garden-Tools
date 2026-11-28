using System;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class HkxTests
{
    [Fact]
    public void BytesThatAreNotAPackfileSurfaceAsAParseErrorFromTheNativeReader()
    {
        var error = Assert.Throws<RadicleException>(() => HkxFile.FromBytes(new byte[64]));
        Assert.Equal(Status.ParseError, error.Status);
    }

    [Fact]
    public void EmptyBytesAreRefusedBeforeCallingNative()
    {
        Assert.Throws<ArgumentException>(() => HkxFile.FromBytes(ReadOnlySpan<byte>.Empty));
    }
}
