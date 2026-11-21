using System;
using System.Runtime.InteropServices;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class HkxPlainTests
{
    private static string? LastError() => Marshal.PtrToStringUTF8(RadicleNative.LastError());

    [Fact]
    public void EmptyBytesAreRefusedBeforeCallingNative()
    {
        Assert.Throws<ArgumentException>(() => HkxPlain.SkeletonRest(ReadOnlySpan<byte>.Empty));
        Assert.Throws<ArgumentException>(() => HkxPlain.Sample(ReadOnlySpan<byte>.Empty, new byte[64]));
        Assert.Throws<ArgumentException>(() => HkxPlain.ClipRoles(ReadOnlySpan<byte>.Empty));
    }

    [Fact]
    public void BytesThatAreNotAPackfileSurfaceAsAParseError()
    {
        var junk = new byte[64];
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => HkxPlain.SkeletonRest(junk)).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => HkxPlain.Sample(junk, junk)).Status);
        Assert.Equal(Status.ParseError, Assert.Throws<RadicleException>(() => HkxPlain.ClipRoles(junk, "animations", "1hm")).Status);
    }

    [Theory]
    [InlineData(-1f)]
    [InlineData(241f)]
    [InlineData(float.NaN)]
    [InlineData(float.PositiveInfinity)]
    [InlineData(float.NegativeInfinity)]
    public unsafe void ARateOutsideZeroToTheCapIsRejectedBeforeAnyHandleIsRead(float rate)
    {
        IntPtr sampled = new IntPtr(1);
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipSample(IntPtr.Zero, IntPtr.Zero, rate, 0, &sampled));
        Assert.Equal(IntPtr.Zero, sampled);
        Assert.Contains("rate", LastError());
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(2)]
    public unsafe void ALoopFlagThatIsNotZeroOrOneIsRejected(int loop)
    {
        IntPtr sampled = new IntPtr(1);
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipSample(IntPtr.Zero, IntPtr.Zero, 30f, loop, &sampled));
        Assert.Equal(IntPtr.Zero, sampled);
        Assert.Contains("loop", LastError());
    }

    [Fact]
    public unsafe void NullHandlesAndMissingOutputsAreInvalidArguments()
    {
        IntPtr sampled = IntPtr.Zero;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipSample(IntPtr.Zero, IntPtr.Zero, 0f, 0, &sampled));
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipSample(IntPtr.Zero, IntPtr.Zero, 0f, 0, null));

        float* values = null;
        nuint count = 0;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainSkeletonRest(IntPtr.Zero, &values, &count));

        HkxPlainClipInfo info;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipReadInfo(IntPtr.Zero, &info));
        int bone = 0;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxPlainClipTrackBone(IntPtr.Zero, 0, &bone));

        IntPtr roles = IntPtr.Zero;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxClipRolesRead(IntPtr.Zero, null, null, &roles));
        nuint roleCount = 0;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxClipRolesCount(IntPtr.Zero, &roleCount));
        HkxClipRoleInfo roleInfo;
        Assert.Equal(Status.InvalidArgument, RadicleNative.HkxClipRoleReadInfo(IntPtr.Zero, 0, &roleInfo));
    }
}
