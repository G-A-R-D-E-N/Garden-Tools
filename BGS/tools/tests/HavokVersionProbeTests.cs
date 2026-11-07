using System.Text;
using OpenCommonwealth.Services.Hkx;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class HavokVersionProbeTests
{
    [Theory]
    [InlineData("hk_2014.1.0-r1", 24)]
    [InlineData("hk_2018.2.0-r1", 41)]
    public void ReadsVersionTokenAtItsOffset(string version, int offset)
    {
        byte[] header = new byte[HavokVersionProbe.HeaderBytes];
        Encoding.ASCII.GetBytes(version).CopyTo(header, offset);

        var result = HavokVersionProbe.Read(header);

        Assert.NotNull(result);
        Assert.Equal(version, result!.Version);
        Assert.Equal(offset, result.Offset);
    }

    [Fact]
    public void StopsAtTheEndOfTheVersionToken()
    {
        var result = HavokVersionProbe.Read(Encoding.ASCII.GetBytes("xxxxhk_2015.1.0-r1\0other"));

        Assert.NotNull(result);
        Assert.Equal("hk_2015.1.0-r1", result!.Version);
    }

    [Fact]
    public void DoesNotScanPastTheHeaderWindow()
    {
        byte[] bytes = new byte[160];
        Encoding.ASCII.GetBytes("hk_2018.2.0-r1").CopyTo(bytes, 110);

        Assert.Null(HavokVersionProbe.Read(bytes));
    }

    [Fact]
    public void RejectsAVersionCutOffByTheHeaderWindow()
    {
        // The token is still running when the 96-byte window ends, so the readable part
        // ("hk_2018.2") is a plausible-looking version that is not the file's real one.
        byte[] bytes = new byte[HavokVersionProbe.HeaderBytes + 32];
        Encoding.ASCII.GetBytes("hk_2018.2.0-r1").CopyTo(bytes, HavokVersionProbe.HeaderBytes - 9);

        Assert.Null(HavokVersionProbe.Read(bytes));
    }

    [Fact]
    public void ReadsAVersionThatEndsExactlyBeforeTheWindowCloses()
    {
        byte[] bytes = new byte[HavokVersionProbe.HeaderBytes];
        Encoding.ASCII.GetBytes("hk_2018.2.0-r1").CopyTo(bytes, HavokVersionProbe.HeaderBytes - 15);

        var result = HavokVersionProbe.Read(bytes);

        Assert.NotNull(result);
        Assert.Equal("hk_2018.2.0-r1", result!.Version);
    }

    [Fact]
    public void ReturnsNullWhenNoHavokVersionIsPresent()
    {
        Assert.Null(HavokVersionProbe.Read(Encoding.ASCII.GetBytes("not a havok header")));
    }
}
