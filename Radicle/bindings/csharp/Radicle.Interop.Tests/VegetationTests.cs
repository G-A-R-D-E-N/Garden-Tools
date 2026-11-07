using System;
using System.Linq;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class VegetationTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(33)]
    public void GrassUsesPresentPrefixAndNonmatchingTreeWindStaysOpaque(int size)
    {
        using var temp = new TempDirectory();
        var data = Enumerable.Repeat((byte)1, size).ToArray();
        var path = temp.Write("Vegetation.esm", Build(1, Array.Empty<string>(),
            Top("GRAS", new Rec("GRAS", 0x800, new[] { ("DATA", data) })),
            Top("TREE", new Rec("TREE", 0x801, new[] { ("CNAM", data) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var grass = env.Grass(env.Winner(new FormKey("Vegetation.esm", 0x800))!.Value)!;
        Assert.Equal(size == 0 ? 0 : 1, grass.Density);
        Assert.Equal(size < 29 ? 0 : 1, grass.Flags);
        Assert.Null(env.Tree(env.Winner(new FormKey("Vegetation.esm", 0x801))!.Value).Wind);
    }
    [Fact]
    public void GrassGetterPreservesEncodedFieldsAndExistingMetadata()
    {
        using var temp = new TempDirectory();
        var data = new byte[] { 0xFE, 4, 90, 0xAA, 0xF4, 1, 0xCC, 0xBB }.Concat(U32(7))
            .Concat(F32(32, 0.2f, 0.5f, 10)).Concat(new byte[] { 0x81, 0xBB, 0xCC, 0xDD }).ToArray();
        var path = temp.Write("Vegetation.esm", Build(1, Array.Empty<string>(), Top("GRAS",
            new Rec("GRAS", 0x800, new[] { ("DATA", data), ("MODL", Z("grass.nif")), ("OBND", new byte[12]) }),
            new Rec("GRAS", 0x801, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var record = env.Winner(new FormKey("Vegetation.esm", 0x800))!.Value;
        Assert.Equal(new Grass(0xFE, 4, 90, 500, 7, 32, 0.2f, 0.5f, 10, 0x81), env.Grass(record));
        Assert.Equal("grass.nif", env.Info(record).ModelPath); Assert.Equal(new Bounds(), env.Info(record).Bounds);
        Assert.Null(env.Grass(env.Winner(new FormKey("Vegetation.esm", 0x801))!.Value));
    }

    [Fact]
    public void TreeGetterCopiesWindAndHarvestFields()
    {
        using var temp = new TempDirectory();
        var wind = Enumerable.Range(0, 12).Select(i => (float)i).ToArray();
        var path = temp.Write("Vegetation.esm", Build(1, Array.Empty<string>(), Top("TREE", new Rec("TREE", 0x800,
            new[] { ("CNAM", F32(wind)), ("PFIG", U32(0x810)), ("SNAM", U32(0x811)), ("PFPC", new byte[] { 1, 2, 3, 4 }) }))));
        Tree tree;
        using (var env = RadicleEnvironment.Load(new[] { path })) tree = env.Tree(env.Winner(new FormKey("Vegetation.esm", 0x800))!.Value);
        Assert.Equal(wind, tree.Wind); Assert.Equal(new byte[] { 1, 2, 3, 4 }, tree.Harvest.Seasons);
        Assert.Equal(new FormKey("Vegetation.esm", 0x810), tree.Harvest.Ingredient);
        Assert.Equal(new FormKey("Vegetation.esm", 0x811), tree.Harvest.Sound);
    }

    [Fact]
    public void FloraGetterPreservesFlagsAndHarvestAbsence()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Vegetation.esm", Build(1, Array.Empty<string>(), Top("FLOR",
            new Rec("FLOR", 0x800, new[] { ("FNAM", new byte[] { 0x34, 0x80 }), ("PFIG", U32(0x812)), ("PFPC", new byte[] { 5, 6, 7, 8 }) }),
            new Rec("FLOR", 0x801, Array.Empty<(string, byte[])>()))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var flora = env.Flora(env.Winner(new FormKey("Vegetation.esm", 0x800))!.Value);
        Assert.Equal((ushort)0x8034, flora.Flags); Assert.Equal(new byte[] { 5, 6, 7, 8 }, flora.Harvest.Seasons);
        Assert.Equal(new FormKey("Vegetation.esm", 0x812), flora.Harvest.Ingredient); Assert.Null(flora.Harvest.Sound);
        var absent = env.Flora(env.Winner(new FormKey("Vegetation.esm", 0x801))!.Value);
        Assert.Null(absent.Flags); Assert.Null(absent.Harvest.Seasons); Assert.Null(absent.Harvest.Ingredient);
    }

    [Fact]
    public void LandscapeGrassesGetterCopiesRepeatedLinksInOrder()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Vegetation.esm", Build(1, Array.Empty<string>(), Top("LTEX", new Rec("LTEX", 0x800,
            new[] { ("GNAM", U32(0x810)), ("GNAM", U32(0)), ("GNAM", U32(0x810)) }))));
        FormKey?[] keys;
        using (var env = RadicleEnvironment.Load(new[] { path }))
            keys = env.LandscapeGrasses(env.Winner(new FormKey("Vegetation.esm", 0x800))!.Value).ToArray();
        Assert.Equal(new FormKey?[] { new("Vegetation.esm", 0x810), null, new("Vegetation.esm", 0x810) }, keys);
    }
}
