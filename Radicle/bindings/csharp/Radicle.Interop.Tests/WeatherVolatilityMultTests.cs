using System;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WeatherVolatilityMultTests
{
    [Fact]
    public void ReadsTheWinnersMultiplierAndNullWhenAbsent()
    {
        using var temp = new TempDirectory();
        var none = Array.Empty<(string, byte[])>();
        var master = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("WTHR",
                new Rec("WTHR", 0x800, new[] { ("VNAM", F32(2.5f)) }),
                new Rec("WTHR", 0x801, none),
                new Rec("WTHR", 0x802, new[] { ("VNAM", F32(0f)) }),
                new Rec("WTHR", 0x803, new[] { ("VNAM", F32(1f)) })),
            Top("CLMT", new Rec("CLMT", 0x810, new[] { ("TNAM", new byte[] { 0x24, 0x2A, 0x6C, 0x72, 128, 0xC3 }) }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Fallout4.esm" },
            Top("WTHR", new Rec("WTHR", 0x800, new[] { ("VNAM", F32(4f)) }))));
        using var env = RadicleEnvironment.Load(new[] { master, patch });
        Record R(uint id) => env.Winner(new FormKey("Fallout4.esm", id))!.Value;

        Assert.Equal(4f, env.WeatherVolatilityMult(R(0x800)));
        Assert.Null(env.WeatherVolatilityMult(R(0x801)));
        Assert.Equal(0f, env.WeatherVolatilityMult(R(0x802)));
        Assert.Null(env.WeatherVolatilityMult(R(0x810)));

        var timing = env.Climate(R(0x810)).Timing!;
        Assert.Equal(11.9569f, timing.VolatilityHoldHours(env.WeatherVolatilityMult(R(0x803))!.Value, 1f, 23f), 3);
    }
}
