using System;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WaterSelectionTests
{
    [Fact]
    public void ResolvesWinningWaterAndHeightAndLeavesMissingDefaultsExplicit()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("WATR", new Rec("WATR", 0x840, Array.Empty<(string, byte[])>())),
            Top("WRLD", new Rec("WRLD", 0x800, new[] { ("NAM2", U32(0x840)), ("DNAM", F32(-2048, 450)) }),
                new Rec("WRLD", 0x801, new[] { ("WNAM", U32(0x800)), ("PNAM", new byte[] { 9, 0 }), ("DNAM", F32(-2048, 10)) })),
            Top("CELL", new Rec("CELL", 0x820, new[] { ("DATA", new byte[] { 3 }), ("XCWT", U32(0x840)), ("XCLW", F32(-2147483648f)) }),
                new Rec("CELL", 0x821, new[] { ("DATA", new byte[] { 1 }) }))));
        var patch = temp.Write("Patch.esp", Build(0, new[] { "Fallout4.esm" },
            Top("WATR", new Rec("WATR", 0x840, new[] { ("FNAM", new byte[] { 7 }) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin, patch });
        Record R(uint id) => env.Winner(new FormKey("Fallout4.esm", id))!.Value;
        var inherited = env.EffectiveWater(R(0x801));
        Assert.Equal(new FormKey("Fallout4.esm", 0x840), inherited.Water);
        Assert.Equal(450f, inherited.Height);
        Assert.False(inherited.UsesDefaultWater);
        Assert.Equal((byte?)7, env.Water(inherited.Record!.Value).Flags);
        var interior = env.EffectiveWater(R(0x820));
        Assert.Equal(inherited.Water, interior.Water);
        Assert.Null(interior.Height);
        var fallback = env.EffectiveWater(R(0x821));
        Assert.Equal(new FormKey("Fallout4.esm", 0x18), fallback.Water);
        Assert.True(fallback.UsesDefaultWater);
        Assert.Null(fallback.Record);
        Assert.Null(fallback.Height);
    }

    [Fact]
    public void CellWithMissingXcwtResolvesToWorldspaceWater()
    {
        using var temp = new TempDirectory();
        var cell = new Rec("CELL", 0x810, new[] { ("DATA", new byte[] { 2 }), ("XCWT", U32(0x999)) });
        var plugin = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("WATR", new Rec("WATR", 0x840, Array.Empty<(string, byte[])>())),
            Top("WRLD", new Rec("WRLD", 0x800, new[] { ("NAM2", U32(0x840)), ("DNAM", F32(-2048, 450)) }),
                new Grp(0x800, 1, new Node[] { new Grp(0, 4, new Node[] { new Grp(0, 5, new Node[] { cell }) }) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        var selection = env.EffectiveWater(env.Winner(new FormKey("Fallout4.esm", 0x810))!.Value);
        Assert.Equal(new FormKey("Fallout4.esm", 0x840), selection.Water);
        Assert.False(selection.UsesDefaultWater);
    }

    [Fact]
    public void ResolveRejectsARecordFromAnotherEnvironment()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Fallout4.esm", Build(1, Array.Empty<string>(),
            Top("WATR", new Rec("WATR", 0x840, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        using var other = RadicleEnvironment.Load(new[] { plugin });
        var water = env.Winner(new FormKey("Fallout4.esm", 0x840))!.Value;
        Assert.Throws<ArgumentException>(() => other.EffectiveWater(water));
    }
}
