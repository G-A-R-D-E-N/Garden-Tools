using System;
using System.Collections.Generic;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WorldspaceClimateTests
{
    private static Rec World(uint id, uint parent, byte flags, uint climate)
    {
        var subs = new List<(string, byte[])>();
        if (parent != 0)
        {
            subs.Add(("WNAM", U32(parent)));
            subs.Add(("PNAM", new byte[] { flags, 0 }));
        }
        if (climate != 0) subs.Add(("CNAM", U32(climate)));
        return new Rec("WRLD", id, subs.ToArray());
    }

    [Fact]
    public void ResolvesTheWinningClimateThroughUseClimateData()
    {
        using var temp = new TempDirectory();
        var none = Array.Empty<(string, byte[])>();
        var plugin = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x840, none), new Rec("CLMT", 0x841, none)),
            Top("WTHR", new Rec("WTHR", 0x850, none)),
            Top("WRLD", World(0x800, 0, 0, 0x840), World(0x801, 0x800, 0x10, 0x841), World(0x802, 0x800, 0x08, 0x841),
                World(0x803, 0, 0, 0), World(0x804, 0, 0, 0x850))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Fallout4.esm" },
            Top("CLMT", new Rec("CLMT", 0x840, new[] { ("EDID", Z("Patched")) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin, patch });
        Record R(uint id) => env.Winner(new FormKey("Fallout4.esm", id))!.Value;

        var inherited = env.EffectiveClimate(R(0x801))!.Value;
        var info = env.Info(inherited);
        Assert.Equal(new FormKey("Fallout4.esm", 0x840), info.FormKey);
        Assert.Equal(1, info.PluginIndex);
        Assert.Equal("Patched", info.EditorId);
        Assert.Equal(new FormKey("Fallout4.esm", 0x841), env.Info(env.EffectiveClimate(R(0x802))!.Value).FormKey);
        Assert.Null(env.EffectiveClimate(R(0x803)));
        Assert.Null(env.EffectiveClimate(R(0x804)));
    }

    [Fact]
    public void RejectsANonWorldspaceAndARecordFromAnotherEnvironment()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("CLMT", new Rec("CLMT", 0x840, Array.Empty<(string, byte[])>())),
            Top("WRLD", World(0x800, 0, 0, 0x840))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        using var other = RadicleEnvironment.Load(new[] { plugin });
        Assert.Equal(Native.Status.ParseError,
            Assert.Throws<RadicleException>(() => env.EffectiveClimate(env.Winner(new FormKey("Fallout4.esm", 0x840))!.Value)).Status);
        Assert.Throws<ArgumentException>(() => other.EffectiveClimate(env.Winner(new FormKey("Fallout4.esm", 0x800))!.Value));
    }
}
