using System;
using System.Collections.Generic;
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

    private const ushort BlendSourceAlpha = 0xED;
    private const ushort TestGreater = 0x1200;

    // Detects: a managed ResolveMaterial that must carry the NIF alpha property, UV and the warning list across the C ABI with the full texture table, not drop them.
    [Fact]
    public void ResolveMaterialCarriesTheNifShaderAlphaAndWarningsAcrossTheAbi()
    {
        using var temp = new TempDirectory();
        using var assets = AssetResolver.OpenRoots(new[] { temp.Path }, Array.Empty<string>());
        var nif = NifFile.FromBytes(NifFixtures.MeshMaterialNif(BlendSourceAlpha, 77));

        var resolved = nif.ResolveMaterial(0, assets);

        Assert.Equal(ResolvedMaterialSource.NifOnly, resolved.Source);
        Assert.Equal(ResolvedAlphaMode.Blend, resolved.AlphaMode);
        Assert.Equal((byte)6, resolved.BlendSrc);
        Assert.Equal((byte)7, resolved.BlendDst);
        Assert.Equal((0.25f, 0.5f), resolved.UvOffset);
        Assert.Equal((2.0f, 3.0f), resolved.UvScale);
        Assert.Equal(10, resolved.Textures.Count);
        Assert.False(resolved.Swapped);
        Assert.NotNull(resolved.Warnings);
    }

    // Detects: a test-only alpha property that must report the test mode with its threshold, and a swap that names another material leaving the result unswapped.
    [Fact]
    public void ResolveMaterialReportsAlphaTestAndIgnoresAUnrelatedSwap()
    {
        using var temp = new TempDirectory();
        using var assets = AssetResolver.OpenRoots(new[] { temp.Path }, Array.Empty<string>());
        var nif = NifFile.FromBytes(NifFixtures.MeshMaterialNif(TestGreater, 77));
        var swap = new List<MaterialSubstitution> { new(@"Props\Other.BGSM", @"Props\OtherRust.BGSM", 0.5f) };

        var resolved = nif.ResolveMaterial(0, assets, swap);

        Assert.Equal(ResolvedAlphaMode.Test, resolved.AlphaMode);
        Assert.Equal((byte)77, resolved.AlphaThreshold);
        Assert.False(resolved.Swapped);
        Assert.Null(resolved.ColorRemappingIndex);
        Assert.Throws<ArgumentOutOfRangeException>(() => nif.ResolveMaterial(-1, assets));
        Assert.Throws<RadicleException>(() => nif.ResolveMaterial(9, assets));
    }
}
