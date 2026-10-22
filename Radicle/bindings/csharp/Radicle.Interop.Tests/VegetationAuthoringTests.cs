using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class VegetationAuthoringTests
{
    private const string Plugin = "Vegetation.esm";

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey Gras = K(0x800), Tree = K(0x801), TreeBare = K(0x802), Flor = K(0x803),
        Ltex = K(0x804), LtexBare = K(0x805), Sndr = K(0x810), Misc = K(0x811), Stat = K(0x812);

    private static byte[] Hex(string text) => Convert.FromHexString(text);

    private static byte[] Source()
    {
        var edid = ("EDID", Z("Veg"));
        var obnd = ("OBND", new byte[12]);
        var modl = ("MODL", Z("m.nif"));
        var seasons = new byte[] { 1, 2, 3, 4 };
        var cnam = F32(Enumerable.Range(1, 12).Select(i => (float)i).ToArray());
        return Build(1, Array.Empty<string>(),
            Top("GRAS",
                new Rec("GRAS", 0x800, new[] { edid, obnd, modl, ("DATA", Hex("1e005a00000000000000000000000042cdcc4c3e0000003f0000204100000000")) }),
                new Rec("GRAS", 0x806, new[] { edid, obnd, modl })),
            Top("TREE",
                new Rec("TREE", 0x801, new[] { edid, obnd, modl, ("PFPC", seasons), ("CNAM", cnam) }),
                new Rec("TREE", 0x802, new[] { edid, obnd, modl, ("CNAM", cnam) })),
            Top("FLOR", new Rec("FLOR", 0x803, new[] { edid, obnd, ("FULL", U32(7)), modl, ("KSIZ", U32(1)), ("KWDA", U32(0x810)),
                ("FNAM", new byte[2]), ("PFIG", U32(0x811)), ("SNAM", U32(0x810)), ("PFPC", seasons) })),
            Top("LTEX",
                new Rec("LTEX", 0x804, new[] { edid, ("MNAM", U32(0)), ("HNAM", Hex("1e1e")), ("SNAM", Hex("1e")), ("GNAM", U32(0x800)), ("GNAM", U32(0x806)) }),
                new Rec("LTEX", 0x805, new[] { edid, ("MNAM", U32(0)), ("HNAM", Hex("1e1e")), ("SNAM", Hex("1e")) })),
            Top("SNDR", new Rec("SNDR", 0x810, new[] { edid })),
            Top("MISC", new Rec("MISC", 0x811, new[] { edid })),
            Top("STAT", new Rec("STAT", 0x812, new[] { edid })));
    }

    private static Status Refusal(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Theory]
    [InlineData("TREE", "TREE\\Ingredient", "PFIG", "MISC")]
    [InlineData("TREE", "TREE\\HarvestSound", "SNAM", "SNDR")]
    [InlineData("FLOR", "FLOR\\Ingredient", "PFIG", "MISC")]
    [InlineData("FLOR", "FLOR\\HarvestSound", "SNAM", "SNDR")]
    [InlineData("LTEX", "LTEX\\Grasses", "GNAM", "GRAS")]
    public void MalformedLinkPayloadsRefuseWithoutChangingBytes(string record, string field, string subrecord, string targetType)
    {
        foreach (var size in new[] { 0, 3, 5, 8 })
        {
            using var temp = new TempDirectory();
            var source = Build(1, Array.Empty<string>(),
                Top(record, new Rec(record, 0x820, new[] { (subrecord, new byte[size]) })),
                Top(targetType, new Rec(targetType, 0x821, new[] { ("EDID", Z("Target")) })));
            using var plugin = PluginFile.Read(temp.Write(Plugin, source));
            Assert.Equal(Status.InvalidArgument, Refusal(() =>
            {
                if (record == "LTEX") plugin.FormLinkListSet(K(0x820), field, 0, K(0x821));
                else plugin.SetFormLink(K(0x820), field, K(0x821));
            }));
            Assert.Equal(source, plugin.ToBytes());
        }
    }

    [Fact]
    public void LinkEditsEnforceTargetTypesAndNullabilityAndSurviveReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);

        Assert.True(plugin.SetFormLink(Flor, "FLOR\\Ingredient", Misc));
        Assert.True(plugin.SetFormLink(Flor, "FLOR\\Ingredient", null));
        Assert.True(plugin.SetFormLink(Flor, "FLOR\\HarvestSound", Sndr));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Flor, "FLOR\\Ingredient", Stat)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Flor, "FLOR\\HarvestSound", null)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Flor, "FLOR\\HarvestSound", Misc)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Flor, "FLOR\\HarvestSound", new Radicle.Interop.FormKey("Other.esm", 1))));
        Assert.False(plugin.SetFormLink(Tree, "TREE\\Ingredient", Misc));
        Assert.True(plugin.AddSubrecord(Tree, "PFIG"));
        Assert.True(plugin.AddSubrecord(Tree, "SNAM"));
        Assert.True(plugin.SetFormLink(Tree, "TREE\\Ingredient", Stat));
        Assert.True(plugin.SetFormLink(Tree, "TREE\\HarvestSound", Sndr));
        Assert.True(plugin.SetFormLink(Tree, "TREE\\HarvestSound", null));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Tree, "TREE\\Ingredient", Sndr)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Tree, "TREE\\HarvestSound", Misc)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.SetFormLink(Flor, "TREE\\HarvestSound", Sndr)));
        Assert.False(plugin.SetFormLink(Flor, "FLOR\\HarvestSound", Sndr, 1));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        var flora = env.Flora(env.Winner(Flor)!.Value);
        Assert.Null(flora.Harvest.Ingredient);
        Assert.Equal(Sndr, flora.Harvest.Sound);
        var tree = env.Tree(env.Winner(Tree)!.Value);
        Assert.Equal(Stat, tree.Harvest.Ingredient);
        Assert.Null(tree.Harvest.Sound);
        Assert.Equal(new byte[] { 1, 2, 3, 4 }, tree.Harvest.Seasons);
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void LandscapeGrassesEditCountAddRemoveAndDecode()
    {
        using var temp = new TempDirectory();
        var path = temp.Write(Plugin, Source());
        using var plugin = PluginFile.Read(path);
        var grass2 = K(0x806);

        Assert.Equal(2, plugin.FormLinkListCount(Ltex, "LTEX\\Grasses"));
        Assert.Equal(0, plugin.FormLinkListCount(LtexBare, "LTEX\\Grasses"));
        Assert.Throws<System.Collections.Generic.KeyNotFoundException>(() => plugin.FormLinkListCount(K(0x999), "LTEX\\Grasses"));
        Assert.True(plugin.FormLinkListSet(Ltex, "LTEX\\Grasses", 1, Gras));
        var settled = plugin.ToBytes();
        Assert.True(plugin.FormLinkListSet(Ltex, "LTEX\\Grasses", 1, Gras));
        Assert.Equal(settled, plugin.ToBytes());
        Assert.False(plugin.FormLinkListSet(Ltex, "LTEX\\Grasses", 2, Gras));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.FormLinkListSet(Ltex, "LTEX\\Grasses", 0, null)));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.FormLinkListSet(Ltex, "LTEX\\Grasses", 0, Stat)));
        Assert.Equal(settled, plugin.ToBytes());
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Ltex, "GNAM")));

        Assert.True(plugin.FormLinkListInsert(Ltex, "LTEX\\Grasses", 2, Gras));
        Assert.Equal(3, plugin.FormLinkListCount(Ltex, "LTEX\\Grasses"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.FormLinkListInsert(Ltex, "LTEX\\Grasses", 0, Stat)));
        Assert.True(plugin.RemoveSubrecord(Ltex, "GNAM", 0));
        Assert.False(plugin.RemoveSubrecord(Ltex, "GNAM", 2));
        Assert.True(plugin.FormLinkListInsert(LtexBare, "LTEX\\Grasses", 0, Gras));
        Assert.True(plugin.FormLinkListInsert(LtexBare, "LTEX\\Grasses", 1, grass2));
        Assert.True(plugin.RemoveSubrecord(LtexBare, "GNAM", 1));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Ltex, "SNAM")));
        var edited = plugin.ToBytes();
        plugin.Write(path);

        using var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal(new Radicle.Interop.FormKey?[] { Gras, Gras }, env.LandscapeGrasses(env.Winner(Ltex)!.Value));
        Assert.Equal(new Radicle.Interop.FormKey?[] { Gras }, env.LandscapeGrasses(env.Winner(LtexBare)!.Value));
        using var reloaded = PluginFile.Read(path);
        Assert.Equal(edited, reloaded.ToBytes());
    }

    [Fact]
    public void OptionalSubrecordsAddAndRemoveWhileRequiredAndCoupledOnesAreRefused()
    {
        using var temp = new TempDirectory();
        using var plugin = PluginFile.Read(temp.Write(Plugin, Source()));

        Assert.True(plugin.AddSubrecord(TreeBare, "PFPC"));
        Assert.True(plugin.AddSubrecord(TreeBare, "PFPC"));
        Assert.True(plugin.RemoveSubrecord(Tree, "PFPC"));
        Assert.False(plugin.RemoveSubrecord(Tree, "PFPC"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Tree, "CNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Flor, "PFPC")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Flor, "FULL")));
        foreach (var coupled in new[] { "KSIZ", "KWDA", "MODL", "DEST" })
        {
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Flor, coupled)));
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Flor, coupled)));
        }
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Flor, "PRPS")));
        Assert.True(plugin.RemoveSubrecord(Flor, "SNAM"));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.AddSubrecord(Flor, "SNAM")));
        Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.RemoveSubrecord(Gras, "DATA")));
        Assert.True(plugin.RemoveSubrecord(Ltex, "GNAM", 1));
    }

    [Fact]
    public void CreatedVegetationRecordsDecodeWithTheirDefaultsAfterSaveAndReload()
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, "Created", Plugin);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using (var plugin = PluginFile.Create(Plugin))
        {
            var newGras = plugin.CreateRecord("GRAS", "NewGrass");
            var newTree = plugin.CreateRecord("TREE", "NewTree");
            var newFlor = plugin.CreateRecord("FLOR", "NewFlora");
            var newLtex = plugin.CreateRecord("LTEX", "NewLand");
            Assert.Equal(Status.InvalidArgument, Refusal(() => plugin.CreateRecord("BPTD", "X")));
            Assert.True(plugin.AddSubrecord(newFlor, "FNAM"));
            Assert.True(plugin.SetScalar(newFlor, "FLOR\\Flags", "513"));
            Assert.True(plugin.SetScalar(newGras, "GRAS\\Density", "44"));
            Assert.True(plugin.AddSubrecord(newTree, "PFIG"));
            Assert.True(plugin.FormLinkListInsert(newLtex, "LTEX\\Grasses", 0, newGras));
            plugin.Write(path);
            Assert.Equal(new[] { 0x800u, 0x801u, 0x802u, 0x803u }, new[] { newGras.LocalId, newTree.LocalId, newFlor.LocalId, newLtex.LocalId });
        }

        using var env = RadicleEnvironment.Load(new[] { path });
        var grass = env.Grass(env.Winner(K(0x800))!.Value)!;
        Assert.Equal(new Grass(44, 0, 90, 0, 0, 32f, 0.2f, 0.5f, 10f, 0), grass);
        var tree = env.Tree(env.Winner(K(0x801))!.Value);
        Assert.Equal(new float[] { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 }, tree.Wind);
        Assert.Null(tree.Harvest.Ingredient);
        var flora = env.Flora(env.Winner(K(0x802))!.Value);
        Assert.Equal((ushort)513, flora.Flags);
        Assert.Equal(new byte[] { 0, 0, 0, 0 }, flora.Harvest.Seasons);
        Assert.Equal(new Radicle.Interop.FormKey?[] { K(0x800) }, env.LandscapeGrasses(env.Winner(K(0x803))!.Value));
    }
}
