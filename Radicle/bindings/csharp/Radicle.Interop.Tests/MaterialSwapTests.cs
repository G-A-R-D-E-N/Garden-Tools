using System;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class MaterialSwapTests
{
    [Fact]
    public void ReadsSwapsAndTheModelColorRemappingIndex()
    {
        using var temp = new TempDirectory();
        var plugin = temp.Write("Swaps.esm", Build(0x1, Array.Empty<string>(),
            Top("MSWP", new Rec("MSWP", 0x800, new[] { ("BNAM", Z(@"Props\Box.BGSM")), ("SNAM", Z(@"Props\BoxRust.BGSM")), ("CNAM", F32(0.5f)) })),
            Top("STAT",
                new Rec("STAT", 0x810, new[] { ("MODL", Z(@"Props\Box.nif")), ("MODC", F32(0.25f)), ("MODS", U32(0x800)) }),
                new Rec("STAT", 0x811, new[] { ("MODL", Z(@"Props\Box.nif")) }))));
        using var env = RadicleEnvironment.Load(new[] { plugin });
        FormKey K(uint id) => new("Swaps.esm", id);

        var swap = Assert.Single(env.MaterialSwap(env.Winner(K(0x800))!.Value));
        Assert.Equal(new MaterialSubstitution(@"Props\Box.BGSM", @"Props\BoxRust.BGSM", 0.5f), swap);
        var swapped = env.Winner(K(0x810))!.Value;
        Assert.Equal(K(0x800), env.PrimaryModelMaterialSwap(swapped));
        Assert.Equal(0.25f, env.PrimaryModelColorRemappingIndex(swapped));
        Assert.Null(env.PrimaryModelColorRemappingIndex(env.Winner(K(0x811))!.Value));
    }
}
