using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public sealed class WorldFixture : IDisposable
{
    public WorldFixture()
    {
        Directory = new TempDirectory();
        var stat = new Rec("STAT", 0x810, new[] { ("EDID", Z("Rock")), ("OBND", new byte[12]), ("MODL", Z("Rocks\\Rock01.nif")) });
        var refr = new Rec("REFR", 0x803, new[]
        {
            ("EDID", Z("RockRef")), ("NAME", U32(0x810)), ("XAPD", new byte[] { 1 }), ("XAPR", U32(0x804).Concat(F32(0.5f)).ToArray()),
            ("XOWN", U32(0x830, 0, 0)), ("XESP", U32(0x804, 1)), ("XLKR", U32(0, 0x804)), ("XSCL", F32(2)),
            ("DATA", F32(10, 20, 30, 0, 0, 1.5f)), ("MNAM", Z("note")),
        });
        var achr = new Rec("ACHR", 0x804, new[] { ("NAME", U32(0x810)), ("DATA", F32(0, 0, 0, 0, 0, 0)) });
        var land = new Rec("LAND", 0x805, new[]
        {
            ("DATA", U32(1)),
            ("VHGT", F32(-1024).Concat(new byte[] { 1 }).Concat(new byte[1091]).ToArray()),
            ("BTXT", U32(0x811).Concat(new byte[] { 0, 0, 0xFF, 0xFF }).ToArray()),
            ("ATXT", U32(0x810).Concat(new byte[] { 0, 0, 0, 0 }).ToArray()),
            ("VTXT", new byte[] { 0, 0, 0, 0 }.Concat(F32(1)).ToArray()),
        });
        var interiorRef = new Rec("REFR", 0x821, new[] { ("NAME", U32(0x810)), ("DATA", F32(1, 1, 1, 0, 0, 0)) });
        Master = Directory.Write("Master.esm", Build(0x1, Array.Empty<string>(),
            Top("STAT", stat),
            Top("FACT", new Rec("FACT", 0x830, new[] { ("EDID", Z("Owners")) })),
            Top("LCTN", new Rec("LCTN", 0x831, Array.Empty<(string, byte[])>())),
            Top("CELL", new Grp(0, 2, new Node[] { new Grp(0, 3, new Node[]
            {
                new Rec("CELL", 0x820, new[] { ("DATA", new byte[] { 1 }), ("EDID", Z("Vault")) }),
                new Grp(0x820, 6, new Node[] { new Grp(0x820, 8, new Node[] { interiorRef }) }),
            }) })),
            Top("WRLD",
                new Rec("WRLD", 0x800, new[] { ("EDID", Z("World")), ("DNAM", F32(-2048, 0)) }),
                new Grp(0x800, 1, new Node[]
                {
                    new Rec("CELL", 0x801, new[] { ("DATA", new byte[] { 2 }) }),
                    new Grp(0x801, 6, new Node[] { new Grp(0x801, 8, Array.Empty<Node>()) }),
                    new Grp(0, 4, new Node[] { new Grp(0, 5, new Node[]
                    {
                        new Rec("CELL", 0x802, new[] { ("DATA", new byte[] { 2 }), ("XCLC", U32(1, 2, 0)) }),
                        new Grp(0x802, 6, new Node[] { new Grp(0x802, 9, new Node[] { refr, achr, land }) }),
                    }) }),
                }))));
        Patch = Directory.Write("Patch.esp", Build(0, new[] { "Master.esm" },
            Top("WRLD",
                new Rec("WRLD", 0x800, new[] { ("EDID", Z("World")), ("DNAM", F32(-2048, 0)) }),
                new Grp(0x800, 1, new Node[] { new Grp(0, 4, new Node[] { new Grp(0, 5, new Node[]
                {
                    new Rec("CELL", 0x802, new[] { ("DATA", new byte[] { 2 }), ("XCLC", U32(1, 2, 0)) }),
                    new Grp(0x802, 6, new Node[] { new Grp(0x802, 9, new Node[]
                    {
                        new Rec("REFR", 0x803, new[] { ("EDID", Z("RockRef")), ("NAME", U32(0x810)), ("DATA", F32(99, 0, 0, 0, 0, 0)) }),
                    }) }),
                }) }) }))));
    }

    public TempDirectory Directory { get; }
    public string Master { get; }
    public string Patch { get; }

    public void Dispose() => Directory.Dispose();
}

public class EnvironmentTests : IClassFixture<WorldFixture>
{
    private readonly WorldFixture _world;

    public EnvironmentTests(WorldFixture world) => _world = world;

    private static FormKey M(uint id) => new("Master.esm", id);

    [Fact]
    public void FormKeyEqualityIgnoresPluginCase()
    {
        Assert.Equal(new FormKey("Strong_PA.esp", 0x808), new FormKey("strong_PA.esp", 0x808));
        Assert.Single(new HashSet<FormKey> { new("Strong_PA.esp", 0x808), new("strong_PA.esp", 0x808) });
        Assert.NotEqual(new FormKey("Strong_PA.esp", 0x808), new FormKey("Strong_PA.esp", 0x809));
        Assert.Single(new HashSet<FormKey> { default, default });
    }

    [Fact]
    public void LandscapeHeightsAndBlendDecodeTheCellLand()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master, _world.Patch });
        var land = env.CellLandscape(M(0x802))!.Value;
        Assert.Equal(-8184f, env.LandscapeHeights(land)!.Heights[0]);
        var blend = env.LandscapeBlend(land);
        Assert.Equal(M(0x811), blend.Base[0]);
        Assert.Equal(M(0x810), blend.Layers[0].Texture);
        Assert.Equal(1f, blend.Layers[0].Weights[0]);
    }

    [Fact]
    public void LoadsPluginsAndResolvesVersions()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master, _world.Patch });
        Assert.Equal(new[] { "Master.esm", "Patch.esp" }, env.Plugins.Select(p => p.Filename));
        Assert.True(env.Plugins[0].MasterFlag);
        Assert.Equal(new[] { "Master.esm" }, env.Plugins[1].Masters);
        Assert.Equal(1, env.FindPlugin("PATCH.ESP"));
        Assert.Null(env.FindPlugin("Missing.esp"));

        var versions = env.Versions(M(0x803));
        Assert.Equal(2, versions.Count);
        var winner = env.Winner(M(0x803))!.Value;
        Assert.Equal(versions[1], winner);
        var info = env.Info(winner);
        Assert.Equal(M(0x803), info.FormKey);
        Assert.Equal("REFR", info.Signature);
        Assert.Equal(1, info.PluginIndex);
        Assert.Equal("RockRef", info.EditorId);
        Assert.Null(env.Winner(M(0x999)));
        Assert.Empty(env.Versions(M(0x999)));

        Assert.Equal(M(0x820), env.Info(env.FindEditorId("vault")!.Value).FormKey);
        Assert.Null(env.FindEditorId("Vault", "STAT"));
        Assert.Single(env.WinnersOf("STAT"));
        Assert.Equal(new[] { M(0x810), M(0x830), M(0x831), M(0x820), M(0x821), M(0x800), M(0x801), M(0x802), M(0x803), M(0x804), M(0x805) },
                     env.PluginRecords(0).Select(r => env.Info(r).FormKey));
        var stat = env.Winner(M(0x810))!.Value;
        Assert.Equal("Rocks\\Rock01.nif", env.PrimaryModelPath(stat));
        Assert.Equal("Rocks\\Rock01.nif", env.Info(stat).ModelPath);
        Assert.NotNull(env.Info(stat).Bounds);
        Assert.Throws<RadicleException>(() => env.PluginRecords(5));
    }

    [Fact]
    public void WalksWorldsCellsAndReferences()
    {
        using var env = RadicleEnvironment.Load(new[] { _world.Master, _world.Patch });
        var world = env.Worldspace(env.Winner(M(0x800))!.Value);
        Assert.Equal(-2048f, world.DefaultLandHeight);
        Assert.Equal(M(0x801), env.Info(env.WorldspaceTopCell(M(0x800))!.Value).FormKey);
        Assert.Equal(new[] { M(0x802) }, env.WorldspaceCells(M(0x800)).Select(r => env.Info(r).FormKey));
        Assert.Equal(new[] { M(0x820) }, env.InteriorCells().Select(r => env.Info(r).FormKey));
        var cell = env.Cell(env.Winner(M(0x802))!.Value);
        Assert.Equal((1, 2), cell.Grid);
        Assert.Equal(M(0x800), cell.Worldspace);

        var children = env.CellChildren(M(0x802));
        Assert.Equal(new[] { M(0x803), M(0x804), M(0x805) }, children.Select(c => env.Info(c.Record).FormKey));
        Assert.Equal(1, env.Info(children[0].Record).PluginIndex);
        Assert.All(children, c => Assert.False(c.Persistent));
        Assert.True(env.CellChildren(M(0x820)).Single().Persistent);
        var container = env.Container(children[1].Record);
        Assert.Equal(9, container.GroupType);
        Assert.Equal(M(0x802), container.Cell);
        Assert.Equal(M(0x805), env.Info(env.CellLandscape(M(0x802))!.Value).FormKey);
        Assert.Equal(1u, env.Landscape(env.CellLandscape(M(0x802))!.Value).Flags);

        var original = env.PlacedReference(env.Versions(M(0x803))[0]);
        Assert.Equal("REFR", original.Signature);
        Assert.Equal(M(0x810), original.Base);
        Assert.Equal(new Vector3(10, 20, 30), original.Position);
        Assert.Equal(2f, original.Scale);
        Assert.Equal(M(0x830), original.Owner);
        Assert.Equal(M(0x804), original.EnableParent);
        Assert.Equal((byte)1, original.EnableParentFlags);
        Assert.Equal(M(0x804), original.LinkedReferences.Single().Reference);
        Assert.Null(original.LinkedReferences.Single().Keyword);
        Assert.Equal(0.5f, original.ActivateParents.Single().Delay);
        Assert.Equal("note", original.Comments);
        Assert.Equal(new Vector3(99, 0, 0), env.PlacedReference(children[0].Record).Position);
        Assert.Throws<RadicleException>(() => env.PlacedReference(env.Winner(M(0x810))!.Value));
    }

    [Fact]
    public void ExposesEmittanceOnlyWhenXemiIsPresent()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("Emit.esp", Build(0, Array.Empty<string>(),
            Top("REFR",
                new Rec("REFR", 0x800, new[] { ("NAME", U32(0x801)), ("XEMI", U32(0x802)), ("DATA", F32(0, 0, 0, 0, 0, 0)) }),
                new Rec("REFR", 0x803, new[] { ("NAME", U32(0x801)), ("DATA", F32(0, 0, 0, 0, 0, 0)) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var withXemi = env.PlacedReference(env.Winner(new FormKey("Emit.esp", 0x800))!.Value);
        Assert.Equal(new FormKey("Emit.esp", 0x802), withXemi.Emittance);
        var withoutXemi = env.PlacedReference(env.Winner(new FormKey("Emit.esp", 0x803))!.Value);
        Assert.Null(withoutXemi.Emittance);
    }
}
