using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class BaseRecordTests
{
    [Fact]
    public void DecodesSkyAndBaseRecords()
    {
        using var temp = new TempDirectory();
        var colors = Enumerable.Range(0, 19 * 8).SelectMany(i => new byte[] { (byte)(i / 8), (byte)(i % 8), 0, 0 }).ToArray();
        var speedX = Enumerable.Range(0, 32).Select(i => (byte)i).ToArray();
        var speedY = Enumerable.Range(0, 32).Select(i => (byte)(200 - i)).ToArray();
        var cloudColors = Enumerable.Range(0, 32 * 8).SelectMany(i => new byte[] { (byte)(i / 8), (byte)(i % 8), 9, 255 }).ToArray();
        var cloudAlphas = F32(Enumerable.Range(0, 32 * 8).Select(i => i / 8f).ToArray());
        byte[] dalc(int slot) => slot == 7
            ? Enumerable.Range(0, 6).SelectMany(c => new byte[] { (byte)slot, (byte)c, 3, 0 }).Concat(new byte[] { 7, 8, 9, 0 }).Concat(BitConverter.GetBytes(1.5f)).ToArray()
            : Enumerable.Range(0, 6).SelectMany(c => new byte[] { (byte)slot, (byte)c, 3, 0 }).ToArray();
        var weatherLinks = new[]
        {
            ("LNAM", U32(16)), ("NNAM", U32(0x860)), ("NAM4", F32(Enumerable.Range(0, 32).Select(i => i / 8f).ToArray())),
            ("SNAM", U32(0x861, 2)), ("SNAM", U32(0, 0)), ("TNAM", U32(0x862)), ("TNAM", U32(0)),
            ("IMSP", U32(0x870, 0, 0, 0, 0, 0, 0, 0x877)), ("WGDR", U32(0, 0, 0, 0, 0, 0x878, 0, 0)), ("GNAM", U32(0x879)),
            ("MODL", Z("Sky\\Aurora.nif")), ("UNAM", U32(0x87A).Concat(F32(1.5f)).Concat(U32(0x87B)).ToArray()),
            ("VNAM", F32(0.25f)), ("WNAM", F32(0.75f)),
        };
        var light = U32(0, 256, 0x00FF8040, 0x400).Concat(F32(1, 75, 10, 0, 0, 0, 1, 2)).ToArray();
        var plugin = temp.Write("Sky.esm", Build(0x1, Array.Empty<string>(),
            Top("LIGH", new Rec("LIGH", 0x800, new[] { ("DATA", light), ("FNAM", F32(1)) })),
            Top("LAYR", new Rec("LAYR", 0x810, Array.Empty<(string, byte[])>()), new Rec("LAYR", 0x811, new[] { ("PNAM", U32(0x810)) })),
            Top("CLMT", new Rec("CLMT", 0x820, new[] { ("WLST", U32(0x830, 100, 0)) })),
            Top("WTHR", new Rec("WTHR", 0x830, new[] { ("NAM0", colors), ("FNAM", F32(10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140)), ("30TX", Z("Sky\\Cloud3.dds")), ("QNAM", speedX), ("RNAM", speedY), ("PNAM", cloudColors), ("JNAM", cloudAlphas), ("NAM1", U32(5)), ("MNAM", U32(0x850)) }.Concat(Enumerable.Range(0, 8).Select(i => ("DALC", dalc(i)))).Concat(weatherLinks).ToArray(), FormVersion: 119)),
            Top("SCOL", new Rec("SCOL", 0x840, new[] { ("ONAM", U32(0x850)), ("DATA", F32(1, 2, 3, 0, 0, 0, 1)) })),
            Top("REGN", new Rec("REGN", 0x860, new[] { ("RDAT", U32(3, 0x0A01)), ("RDWT", U32(0x830, 100, 0x850)) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        FormKey K(uint id) => new("Sky.esm", id);

        var lighting = env.Light(env.Winner(K(0x800))!.Value);
        Assert.Equal(256u, lighting.Data!.Radius);
        Assert.Equal(75f, lighting.Data.Fov);
        Assert.Equal(0x400u, lighting.Data.Flags);
        var flags = lighting.Data.FlagSet();
        Assert.True(flags.ShadowSpotlight);
        Assert.True(flags.IsSpot);
        Assert.True(flags.CastsShadows);
        Assert.False(flags.Flicker);
        Assert.False(flags.NonShadowSpotlight);
        Assert.Equal(new ByteColor(0x40, 0x80, 0xFF), lighting.Data.Color);
        Assert.Equal(1f, lighting.Fade);
        Assert.Null(env.LayerParent(env.Winner(K(0x810))!.Value));
        Assert.Equal(K(0x810), env.LayerParent(env.Winner(K(0x811))!.Value));
        var climate = env.Climate(env.Winner(K(0x820))!.Value);
        Assert.Equal(K(0x830), climate.Weathers.Single().Weather);
        Assert.Equal(100, climate.Weathers.Single().Chance);
        var weather = env.Weather(env.Winner(K(0x830))!.Value);
        Assert.Equal((19, 8), (weather.ColorGroups, weather.ColorTimes));
        Assert.Equal(new ByteColor(8, 1, 0), weather.Color(8, Weather.Day));
        Assert.Equal(new[] { 10f, 20f, 30f, 40f, 50f, 60f, 70f, 80f, 90f, 100f, 110f, 120f, 130f, 140f }, weather.FogDistances);
        Assert.Equal("Sky\\Cloud3.dds", weather.CloudTextures[3]);
        Assert.Null(weather.CloudTextures[0]);
        Assert.Equal(32, weather.CloudTextures.Count);
        Assert.Equal(speedX, weather.CloudSpeedX);
        Assert.Equal(speedY, weather.CloudSpeedY);
        Assert.Equal((32, 8), (weather.CloudColorLayers, weather.CloudColorTimes));
        Assert.Equal(new byte[] { 31, 7, 9, 255 }, weather.CloudColors[((31 * 8 + 7) * 4)..][..4]);
        Assert.Equal(31.875f, weather.CloudAlphas[31 * 8 + 7]);
        Assert.Equal(5u, weather.DisabledCloudLayers);
        Assert.Equal(K(0x850), weather.Precipitation);
        Assert.Equal(8, weather.Ambient.Count);
        Assert.Equal(new ByteColor(2, 5, 3), weather.Ambient[2].Directional[5]);
        Assert.Null(weather.Ambient[2].Specular);
        Assert.Equal((new ByteColor(7, 8, 9), 1.5f), (weather.Ambient[7].Specular, weather.Ambient[7].Fresnel));
        Assert.Equal(16u, weather.MaxCloudLayers);
        Assert.Equal(K(0x860), weather.VisualEffect);
        Assert.Equal(3.875f, weather.UnknownNam4[31]);
        Assert.Equal(new WeatherSound(K(0x861), 2), weather.Sounds[0]);
        Assert.Null(weather.Sounds[1].Sound);
        Assert.Equal(new FormKey?[] { K(0x862), null }, weather.SkyStatics);
        Assert.Equal(new FormKey?[] { K(0x870), null, null, null, null, null, null, K(0x877) }, weather.ImageSpaces);
        Assert.Equal(new FormKey?[] { null, null, null, null, null, K(0x878), null, null }, weather.GodRays);
        Assert.Equal(K(0x879), weather.SunGlareLensFlare);
        Assert.Equal("Sky\\Aurora.nif", weather.AuroraModel);
        Assert.Equal(new WeatherMagic(new WeatherMagicSlot(K(0x87A), 1.5f), new WeatherMagicSlot(K(0x87B), null)), weather.Magic);
        Assert.Equal((0.25f, 0.75f), (weather.VolatilityMult, weather.VisibilityMult));
        Assert.Equal(40f, weather.Fog(WeatherFogField.NightFar));
        Assert.Equal(new ByteColor(8, 1, 0), weather.Color(WeatherColorGroup.Horizon, WeatherTime.Day));
        var regionWeather = env.RegionWeathers(env.Winner(K(0x860))!.Value).Single();
        Assert.Equal((K(0x830), 100u, K(0x850), (byte)1, (byte)10),
                     (regionWeather.Weather, regionWeather.Chance, regionWeather.Global, regionWeather.Override, regionWeather.Priority));
        var part = env.StaticCollection(env.Winner(K(0x840))!.Value).Single();
        Assert.Equal(K(0x850), part.Static);
        Assert.Equal((1f, 2f, 3f), part.Placements.Single().Position);
    }

    [Fact]
    public void RegionsAtReturnsContainingRegionsByWeatherPriority()
    {
        using var temp = new TempDirectory();
        (string, byte[])[] Area(float x0, float y0, float x1, float y1) =>
            new[] { ("RPLI", U32(0)), ("RPLD", F32(x0, y0, x1, y0, x1, y1, x0, y1)) };
        Rec Region(uint id, uint world, (string, byte[])[] area, uint priority) =>
            new("REGN", id, new[] { ("WNAM", U32(world)) }.Concat(area).Concat(new[] { ("RDAT", U32(3, priority << 8)), ("RDWT", U32(0x830, 100, 0)) }).ToArray());
        var plugin = temp.Write("Sky.esm", Build(0x1, Array.Empty<string>(),
            Top("REGN",
                Region(0x860, 0x870, Area(0, 0, 10, 10), 5),
                Region(0x861, 0x870, Area(5, 5, 15, 15), 9),
                Region(0x862, 0x871, Area(0, 0, 10, 10), 20))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        FormKey K(uint id) => new("Sky.esm", id);

        Assert.Equal(new FormKey?[] { K(0x861), K(0x860) }, env.RegionsAt(K(0x870), 7f, 7f));
        Assert.Equal(new FormKey?[] { K(0x860) }, env.RegionsAt(K(0x870), 10f, 0f));
        Assert.Equal(new FormKey?[] { K(0x861) }, env.RegionsAt(K(0x870), 15f, 10f));
        Assert.Equal(new FormKey?[] { K(0x862) }, env.RegionsAt(K(0x871), 7f, 7f));
        Assert.Empty(env.RegionsAt(K(0x870), -1f, 5f));
        Assert.Empty(env.RegionsAt(K(0x8FF), 7f, 7f));
    }

    [Fact]
    public void MagicEffectsReadTheWinnersListWithMasterRelativeKeys()
    {
        using var temp = new TempDirectory();
        var efit = (float magnitude, uint area, uint duration) => F32(magnitude).Concat(U32(area, duration)).ToArray();
        var ctda = new byte[32];
        var master = temp.Write("Sky.esm", Build(0x1, Array.Empty<string>(),
            Top("ALCH",
                new Rec("ALCH", 0x870, new[] { ("EDID", Z("Stim")), ("EFID", U32(0x850)), ("EFIT", efit(5, 0, 0)) }),
                new Rec("ALCH", 0x871, new[] { ("EFID", U32(0x850)), ("EFIT", new byte[11]) }),
                new Rec("ALCH", 0x872, new[] { ("EDID", Z("Plain")) }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Sky.esm" },
            Top("ALCH", new Rec("ALCH", 0x870, new[] { ("EDID", Z("Stim")), ("EFID", U32(0x850)), ("EFIT", efit(50, 3, 0)),
                                                      ("EFID", U32(0x01000801)), ("EFIT", efit(2.5f, 0, 60)), ("CTDA", ctda) }))));
        using var env = RadicleEnvironment.Load(new[] { master, patch });
        FormKey K(string plugin, uint id) => new(plugin, id);

        var effects = env.MagicEffects(env.Winner(K("Sky.esm", 0x870))!.Value);
        Assert.Equal(new[]
        {
            new MagicEffectEntry(K("Sky.esm", 0x850), 50f, 3, 0, false),
            new MagicEffectEntry(K("Patch.esp", 0x801), 2.5f, 0, 60, true),
        }, effects);
        Assert.Empty(env.MagicEffects(env.Winner(K("Sky.esm", 0x872))!.Value));
        Assert.Equal(Native.Status.ParseError, Assert.Throws<RadicleException>(() => env.MagicEffects(env.Winner(K("Sky.esm", 0x871))!.Value)).Status);
    }
}
