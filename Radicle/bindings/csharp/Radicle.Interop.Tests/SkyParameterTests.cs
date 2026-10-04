using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class SkyParameterTests
{
    [Theory]
    [InlineData(19)]
    [InlineData(20)]
    public void ExposesStoredClimateTimingAndWeatherParameters(int size)
    {
        using var temp = new TempDirectory();
        var bytes = Enumerable.Range(0, size).Select(i => (byte)i).ToArray();
        var plugin = temp.Write("Sky.esm", Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x800, new[] { ("TNAM", new byte[] { 30, 42, 102, 114, 10, 195 }) })),
            Top("WTHR", new Rec("WTHR", 0x801, new[] { ("DATA", bytes) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        var timing = env.Climate(env.Winner(new FormKey("Sky.esm", 0x800))!.Value).Timing;
        Assert.Equal(new ClimateTiming(30, 42, 102, 114, 10, 195), timing);
        var parameters = env.Weather(env.Winner(new FormKey("Sky.esm", 0x801))!.Value).Parameters;
        Assert.NotNull(parameters);
        Assert.Equal(new WeatherParameters(0, 3, 4, 5, 6, 7, 8, 9, 10, 11, new ByteColor(12, 13, 14),
                                           15, 16, 17, 18, size == 20 ? (byte?)19 : null), parameters);
    }
}
