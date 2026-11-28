using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class ClimateWeatherAuthoringTests
{
    private const string Plugin = "Sky.esm";
    private const byte SunGlare = 15;

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Clmt = K(0x800), Wthr = K(0x801), WthrOld = K(0x802), ClmtBare = K(0x803),
        Glob = K(0x810), Sndr = K(0x811), Stat = K(0x812), Lens = K(0x813), Imgs = K(0x815), Misc = K(0x817);

    private static byte[] Fill(int size, byte value) => Enumerable.Repeat(value, size).ToArray();

    private static (string, byte[])[] Weather(bool modern)
    {
        var subs = new List<(string, byte[])> { ("EDID", Z(modern ? "Modern" : "Legacy")) };
        if (modern) subs.Add(("00TX", Z("sky\\a.dds")));
        subs.Add(("LNAM", U32(16)));
        subs.Add(("MNAM", U32(0)));
        subs.Add(("NNAM", U32(0)));
        subs.Add(("RNAM", Fill(32, 127)));
        subs.Add(("QNAM", Fill(32, 127)));
        subs.Add(("PNAM", new byte[modern ? 1024 : 512]));
        subs.Add(("JNAM", new byte[modern ? 1024 : 512]));
        subs.Add(("NAM0", new byte[modern ? 608 : 272]));
        if (modern) subs.Add(("NAM4", new byte[128]));
        subs.Add(("FNAM", new byte[modern ? 72 : 32]));
        subs.Add(("DATA", new byte[modern ? 20 : 19]));
        subs.Add(("NAM1", U32(0xFFFFFFFF)));
        if (modern)
        {
            subs.Add(("SNAM", U32(0x811, 1)));
            subs.Add(("TNAM", U32(0x812)));
            subs.Add(("TNAM", U32(0)));
        }
        subs.Add(("IMSP", new byte[modern ? 32 : 16]));
        if (modern) subs.Add(("WGDR", new byte[32]));
        foreach (var size in modern ? Enumerable.Repeat(32, 8) : new[] { 24, 28, 32, 32 }) subs.Add(("DALC", new byte[size]));
        if (modern) subs.Add(("GNAM", U32(0x813)));
        subs.Add(("UNAM", new byte[modern ? 24 : 12]));
        if (modern)
        {
            subs.Add(("VNAM", F32(1)));
            subs.Add(("WNAM", F32(1)));
        }
        return subs.ToArray();
    }

    private static byte[] Source()
    {
        var timing = new byte[] { 30, 54, 102, 126, 0, 68 };
        var edid = ("EDID", Z("Sky"));
        var others = new[] { ("GLOB", 0x810u), ("SNDR", 0x811u), ("STAT", 0x812u), ("LENS", 0x813u), ("IMGS", 0x815u), ("MISC", 0x817u) };
        return Build(1, Array.Empty<string>(),
            new[]
            {
                Top("CLMT",
                    new Rec("CLMT", 0x800, new[] { ("EDID", Z("Clim")), ("WLST", U32(0x801, 60, 0, 0x802, 40, 0x810)), ("FNAM", Z("sun.dds")),
                        ("GNAM", Z("glare.dds")), ("MODL", Z("m.nif")), ("MODT", Fill(12, 3)), ("TNAM", timing) }),
                    new Rec("CLMT", 0x803, new[] { ("EDID", Z("Bare")), ("TNAM", timing) })),
                Top("WTHR", new Rec("WTHR", 0x801, Weather(true)), new Rec("WTHR", 0x802, Weather(false), FormVersion: 110)),
            }.Concat(others.Select(o => Top(o.Item1, new Rec(o.Item1, o.Item2, new[] { edid })))).ToArray());
    }

    private static Status Refusal(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void ClimateFieldsLinksAndOptionalSubrecordsSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Clmt, "CLMT\\SunriseBegin", "36"));
        Assert.True(plugin.SetScalar(Clmt, "CLMT\\WeatherChance", "-7", 1));
        Assert.False(plugin.SetScalar(Clmt, "CLMT\\WeatherChance", "1", 2));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Clmt, "CLMT\\Volatility", "256")));
        Assert.Equal(2, plugin.FieldCount(Clmt, "CLMT\\Weather"));
        Assert.True(plugin.SetFormLink(Clmt, "CLMT\\WeatherGlobal", Glob));
        Assert.True(plugin.SetFormLink(Clmt, "CLMT\\Weather", null, 1));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Clmt, "CLMT\\Weather", Misc)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Clmt, "CLMT\\WeatherGlobal", Stat)));
        Assert.True(plugin.SetString(Clmt, "CLMT\\SunTexture", "sky\\new.dds"));
        Assert.False(plugin.SetString(ClmtBare, "CLMT\\SunTexture", "sky\\new.dds"));
        Assert.True(plugin.AddSubrecord(ClmtBare, "GNAM"));
        Assert.True(plugin.AddSubrecord(ClmtBare, "WLST"));
        Assert.True(plugin.SetFormLink(ClmtBare, "CLMT\\Weather", Wthr));
        Assert.True(plugin.SetScalar(ClmtBare, "CLMT\\WeatherChance", "100"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(ClmtBare, "MODL")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(ClmtBare, "MODT")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(ClmtBare, "TNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Clmt, "MODL")));
        Assert.True(plugin.RemoveSubrecord(Clmt, "MODT"));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var climate = env.Climate(env.Winner(Clmt)!.Value);
        Assert.Equal(new[] { new ClimateWeather(Wthr, 60, Glob), new ClimateWeather(null, -7, Glob) }, climate.Weathers);
        Assert.Equal("sky\\new.dds", climate.SunTexture);
        Assert.Equal((byte)36, climate.Timing!.SunriseBegin);
        var bare = env.Climate(env.Winner(ClmtBare)!.Value);
        Assert.Equal(new[] { new ClimateWeather(Wthr, 100, null) }, bare.Weathers);
        Assert.Equal("", bare.SunGlareTexture);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void WeatherFieldsLinksAndRepeatedSubrecordsSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetScalar(Wthr, "WTHR\\CloudSpeedY", "216", 31));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Wthr, "WTHR\\CloudSpeedY", "-1")));
        Assert.True(plugin.SetScalar(Wthr, "WTHR\\CloudAlphaDay", "0.5", 31));
        Assert.False(plugin.SetScalar(WthrOld, "WTHR\\CloudAlphaDay", "0.5"));
        Assert.True(plugin.SetScalar(Wthr, "WTHR\\FogDayNear", "-6400"));
        Assert.True(plugin.SetScalar(Wthr, "WTHR\\WindSpeed", "40"));
        Assert.False(plugin.SetScalar(WthrOld, "WTHR\\WindTurbulence", "4"));
        Assert.True(plugin.SetScalar(WthrOld, "WTHR\\AmbientSpecularRed", "9", 1));
        Assert.False(plugin.SetScalar(WthrOld, "WTHR\\AmbientSpecularRed", "9", 0));
        Assert.Equal(8, plugin.FieldCount(Wthr, "WTHR\\ImageSpace"));
        Assert.Equal(4, plugin.FieldCount(WthrOld, "WTHR\\ImageSpace"));
        Assert.Equal(2, plugin.FieldCount(Wthr, "WTHR\\SkyStatic"));
        Assert.True(plugin.SetFormLink(Wthr, "WTHR\\ImageSpace", Imgs, 7));
        Assert.False(plugin.SetFormLink(WthrOld, "WTHR\\ImageSpace", Imgs, 4));
        Assert.True(plugin.SetFormLink(Wthr, "WTHR\\SkyStatic", null, 0));
        Assert.True(plugin.SetFormLink(Wthr, "WTHR\\SkyStatic", Stat, 1));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Wthr, "WTHR\\SkyStatic", Misc, 1)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Wthr, "WTHR\\SunGlareLensFlare", null)));
        Assert.True(plugin.SetFormLink(Wthr, "WTHR\\SunGlareLensFlare", Lens));

        Assert.True(plugin.AddSubrecord(Wthr, "SNAM"));
        Assert.True(plugin.SetFormLink(Wthr, "WTHR\\Sound", Sndr, 1));
        Assert.True(plugin.SetScalar(Wthr, "WTHR\\SoundType", "3", 1));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetScalar(Wthr, "WTHR\\SoundType", "4", 1)));
        Assert.True(plugin.AddSubrecord(WthrOld, "TNAM"));
        Assert.True(plugin.AddSubrecord(WthrOld, "10TX"));
        Assert.True(plugin.SetString(WthrOld, "WTHR\\CloudTexture1", "sky\\c.dds"));
        Assert.True(plugin.RemoveSubrecord(Wthr, "TNAM", 0));
        Assert.False(plugin.RemoveSubrecord(Wthr, "TNAM", 5));
        foreach (var refused in new[] { "DALC", "VNAM", "WNAM", "UNAM", "NAM4", "WGDR" })
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Wthr, refused)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Wthr, "LNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(WthrOld, "VNAM")));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var weather = env.Weather(env.Winner(Wthr)!.Value);
        Assert.Equal((byte)40, weather.Parameters!.WindSpeed);
        Assert.Equal(-6400f, weather.FogDistances[0]);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
        Assert.Equal(2, reloaded.FieldCount(Wthr, "WTHR\\Sound"));
        Assert.Equal(1, reloaded.FieldCount(Wthr, "WTHR\\SkyStatic"));
        Assert.Equal(1, reloaded.FieldCount(WthrOld, "WTHR\\SkyStatic"));
    }

    [Fact]
    public void ClimateCreatesFromDefaultsAndWeatherCreationIsRefused()
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, "Created", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using (var plugin = PluginFile.Create(Plugin))
        {
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("WTHR", "NewWeather")));
            var climate = plugin.CreateRecord("CLMT", "NewClimate");
            Assert.Equal(0x800u, climate.LocalId);
            Assert.Equal(0, plugin.FieldCount(climate, "CLMT\\Weather"));
            Assert.True(plugin.AddSubrecord(climate, "WLST"));
            Assert.Equal(1, plugin.FieldCount(climate, "CLMT\\Weather"));
            Assert.True(plugin.SetScalar(climate, "CLMT\\WeatherChance", "100"));
            Assert.True(plugin.SetScalar(climate, "CLMT\\SunriseBegin", "30"));
            plugin.Write(path);
        }

        using var env = RadicleEnvironment.Load(new[] { path });
        var created = env.Climate(env.Winner(K(0x800))!.Value);
        Assert.Equal(new[] { new ClimateWeather(null, 100, null) }, created.Weathers);
        Assert.Equal(new ClimateTiming(30, 0, 0, 0, 0, 3), created.Timing);
    }

    [Fact]
    public void SetClimateWeathersRoundTripsEveryEntryThroughWlst()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        var weathers = new[] { new ClimateWeather(WthrOld, -3, Glob), new ClimateWeather(Wthr, 7, null) };
        Assert.True(plugin.SetClimateWeathers(Clmt, weathers));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var climate = env.Climate(env.Winner(Clmt)!.Value);
        Assert.Equal(weathers, climate.Weathers);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void SetClimateWeathersEmptyListClearsWlstInsteadOfRefusingTheNullPointer()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetClimateWeathers(Clmt, Array.Empty<ClimateWeather>()));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Empty(env.Climate(env.Winner(Clmt)!.Value).Weathers);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void SetWeatherColorsWritesOneGroupAtTheFormVersionTimeStride()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        var modern = Enumerable.Range(1, 8).Select(t => new ByteColor((byte)t, (byte)(t * 2), (byte)(t * 3))).ToArray();
        var legacy = Enumerable.Range(1, 4).Select(t => new ByteColor((byte)(t + 100), (byte)t, 9)).ToArray();
        Assert.True(plugin.SetWeatherColors(Wthr, SunGlare, modern));
        Assert.True(plugin.SetWeatherColors(WthrOld, SunGlare, legacy));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var weather = env.Weather(env.Winner(Wthr)!.Value);
        for (var time = 0; time < modern.Length; time++) Assert.Equal(modern[time], weather.Color(SunGlare, time));
        Assert.Equal(new ByteColor(0, 0, 0), weather.Color(SunGlare + 1, 7));
        var legacyWeather = env.Weather(env.Winner(WthrOld)!.Value);
        for (var time = 0; time < legacy.Length; time++) Assert.Equal(legacy[time], legacyWeather.Color(SunGlare, time));
        Assert.Equal(new ByteColor(0, 0, 0), legacyWeather.Color(SunGlare + 1, 0));
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void SetWeatherFogWritesEveryDistanceToFnamAtTheModernCount()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        var fog = Enumerable.Range(1, 18).Select(i => i * 0.5f).ToArray();
        Assert.True(plugin.SetWeatherFog(Wthr, fog));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal(fog, env.Weather(env.Winner(Wthr)!.Value).FogDistances);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void SetWeatherFogRefusesADistanceCountThatMissesFnamAndKeepsTheBytes()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);
        var before = plugin.ToBytes();

        Assert.Equal(Status.ParseError, Refusal(() => plugin.SetWeatherFog(Wthr, new float[8])));
        Assert.Equal(before, plugin.ToBytes());
    }

    [Fact]
    public void SetWeatherTransitionWritesOnlyTheTransitionByteOfData()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetWeatherTransition(Wthr, 200));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var parameters = env.Weather(env.Winner(Wthr)!.Value).Parameters!;
        Assert.Equal((byte)200, parameters.TransitionDelta);
        Assert.Equal((byte)0, parameters.WindSpeed);
        Assert.Equal((byte)0, parameters.SunGlare);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }
}
