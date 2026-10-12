using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class SkyParameterTests
{
    [Fact]
    public void TimingEditsSaveReloadAndPreserveUntouchedBytes()
    {
        using var temp = new TempDirectory();
        var source = Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x800, new[] { ("XUNK", new byte[] { 222, 173, 190, 239 }),
                ("TNAM", new byte[] { 0, 1, 2, 3, 4, 5 }) })),
            Top("WTHR", new Rec("WTHR", 0x801, new[] { ("DATA", Enumerable.Range(0, 20).Select(i => (byte)i).ToArray()) })));
        var path = temp.Write("Sky.esm", source);
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Sky.esm", 0x800);
        Assert.True(plugin.SetClimateTiming(key, new ClimateTiming(0, 1, 2, 3, 4, 5)));
        Assert.Equal(source, plugin.ToBytes());
        Assert.False(plugin.SetClimateTiming(new FormKey("Sky.esm", 0x999), new ClimateTiming(0, 1, 2, 3, 4, 5)));
        Assert.Throws<ArgumentNullException>(() => plugin.SetClimateTiming(key, null!));
        Assert.Throws<RadicleException>(() => plugin.SetClimateTiming(new FormKey("Sky.esm", 0x801), new ClimateTiming(0, 1, 2, 3, 4, 5)));
        Assert.Equal(source, plugin.ToBytes());
        var requested = new ClimateTiming(128, 255, 36, 114, 99, 195);
        Assert.True(plugin.SetClimateTiming(key, requested));
        var edited = plugin.ToBytes();
        Assert.Equal(source.Length, edited.Length);
        Assert.Equal(6, source.Zip(edited).Count(pair => pair.First != pair.Second));
        plugin.Write(path);
        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal(requested, env.Climate(env.Winner(key)!.Value).Timing);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Theory]
    [InlineData(0, false)]
    [InlineData(5, false)]
    [InlineData(7, false)]
    [InlineData(6, true)]
    public void TimingEditsRefuseMissingDuplicateAndUnknownLayouts(int size, bool duplicate)
    {
        using var temp = new TempDirectory();
        var fields = Enumerable.Range(0, duplicate ? 2 : size == 0 ? 0 : 1)
            .Select(_ => ("TNAM", new byte[size])).ToArray();
        var path = temp.Write("Sky.esm", Build(1, Array.Empty<string>(), Top("CLMT", new Rec("CLMT", 0x800, fields))));
        using var plugin = PluginFile.Read(path);
        var before = plugin.ToBytes();
        var error = Assert.Throws<RadicleException>(() => plugin.SetClimateTiming(new FormKey("Sky.esm", 0x800), new ClimateTiming(1, 2, 3, 4, 5, 6)));
        Assert.Equal(Status.ParseError, error.Status);
        Assert.Equal(before, plugin.ToBytes());
    }

    [Theory]
    [InlineData(19)]
    [InlineData(20)]
    public void ExposesStoredClimateTimingAndWeatherParameters(int size)
    {
        using var temp = new TempDirectory();
        var bytes = Enumerable.Range(0, size).Select(i => (byte)i).ToArray();
        var plugin = temp.Write("Sky.esm", Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x800, new[] { ("TNAM", new byte[] { 30, 42, 102, 114, 10, 195 }) })),
            Top("WTHR", new Rec("WTHR", 0x801, new[] { ("DATA", bytes) }, FormVersion: (ushort)(size == 20 ? 119 : 118)))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        var timing = env.Climate(env.Winner(new FormKey("Sky.esm", 0x800))!.Value).Timing;
        Assert.Equal(new ClimateTiming(30, 42, 102, 114, 10, 195), timing);
        Assert.Equal(5f, timing!.SunriseBeginHours);
        Assert.Equal(7f, timing.SunriseEndHours);
        Assert.Equal(17f, timing.SunsetBeginHours);
        Assert.Equal(19f, timing.SunsetEndHours);
        Assert.Equal((byte)3, timing.DaysPerPhase);
        Assert.True(timing.UsesMasser);
        Assert.True(timing.UsesSecunda);
        var parameters = env.Weather(env.Winner(new FormKey("Sky.esm", 0x801))!.Value).Parameters;
        Assert.NotNull(parameters);
        Assert.Equal(new WeatherParameters(0, 3, 4, 5, 6, 7, 8, 9, 10, 11, new ByteColor(12, 13, 14),
                                           15, 16, 17, 18, size == 20 ? (byte?)19 : null), parameters);
    }

    [Theory]
    [InlineData(0, 0)]
    [InlineData(128, 0x40)]
    [InlineData(255, 0xFF)]
    public void TimingUsesUnsignedHoursAndPreservesPackedMoons(byte value, byte moons)
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Sky.esm", Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x800, new[] { ("TNAM", new byte[] { value, value, value, value, 255, moons }) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        var timing = env.Climate(env.Winner(new FormKey("Sky.esm", 0x800))!.Value).Timing!;
        Assert.Equal(value * (1f / 6f), timing.SunsetEndHours);
        Assert.Equal((byte)(moons & 63), timing.DaysPerPhase);
        Assert.Equal((moons & 64) != 0, timing.UsesSecunda);
        Assert.Equal((moons & 128) != 0, timing.UsesMasser);
        Assert.Equal(moons, timing.MoonsPhaseLength);
    }

    [Fact]
    public void DerivedTimingAndTransitionHelpersMatchNativeFixtures()
    {
        Assert.Null(new ClimateTiming(0, 0, 0, 0, 0, 0x00).MoonPhase(10));
        Assert.Equal((byte)0, new ClimateTiming(0, 0, 0, 0, 0, 0x3F).MoonPhase(0));
        Assert.Equal((byte)1, new ClimateTiming(0, 0, 0, 0, 0, 0x3F).MoonPhase(63));
        Assert.Equal((byte)0, new ClimateTiming(0, 0, 0, 0, 0, 0x3F).MoonPhase(504));
        Assert.Equal((byte)1, new ClimateTiming(0, 0, 0, 0, 0, 0x41).MoonPhase(9));
        Assert.Equal((byte)2, new ClimateTiming(0, 0, 0, 0, 0, 0xC3).MoonPhase(200));
        Assert.Equal(1f, new ClimateTiming(0, 0, 0, 0, 255, 0).VolatilityHoldHours(1f, 1f, 23f));
        Assert.Equal(23f, new ClimateTiming(0, 0, 0, 0, 0, 0).VolatilityHoldHours(1f, 1f, 23f));
        Assert.Equal(11.9569f, new ClimateTiming(0, 0, 0, 0, 128, 0).VolatilityHoldHours(1f, 1f, 23f), 4);
        Assert.Equal(0.01f, Transition(0).TransitionHours(0.01f, 1f), 6);
        Assert.Equal(0.013882353f, Transition(1).TransitionHours(0.01f, 1f), 6);
        Assert.Equal(0.50305885f, Transition(127).TransitionHours(0.01f, 1f), 6);
        Assert.Equal(0.5069412f, Transition(128).TransitionHours(0.01f, 1f), 6);
        Assert.Equal(1f, Transition(255).TransitionHours(0.01f, 1f), 6);
        Assert.Equal(3f, Transition(255).TransitionHours(0.01f, 3f), 6);
        Assert.Equal(1.5108628f, Transition(128).TransitionHours(0.01f, 3f), 6);
    }

    private static WeatherParameters Transition(byte delta) =>
        new(0, delta, 0, 0, 0, 0, 0, 0, 0, 0, new ByteColor(0, 0, 0), 0, 0, 0, 0, null);
}
