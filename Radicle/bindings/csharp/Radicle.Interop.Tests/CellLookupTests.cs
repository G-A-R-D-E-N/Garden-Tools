using System;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class CellLookupTests
{
    private sealed record Built(RadicleEnvironment Env, FormKey World, FormKey Interior, FormKey Exterior) : IDisposable
    {
        public void Dispose() => Env.Dispose();
    }

    private static Built Create(TempDirectory temp)
    {
        using var plugin = PluginFile.Create("Cells.esp");
        var world = plugin.CreateRecord("WRLD", "LookupWorld");
        var interior = plugin.CreateInteriorCell("LookupInterior");
        var exterior = plugin.CreateExteriorCell(world, -33, 40, "LookupExterior");
        return new Built(RadicleEnvironment.Load(new[] { temp.Write("Cells.esp", plugin.ToBytes()) }), world, interior, exterior);
    }

    private static Status StatusOf(Action action) => Assert.Throws<RadicleException>(action).Status;

    [Fact]
    public void CreatedCellsAreFoundByGridAndByEditorId()
    {
        using var temp = new TempDirectory();
        using var built = Create(temp);
        var env = built.Env;
        Assert.Equal(built.Exterior, env.FindCellAtGrid(built.World, -33, 40));
        Assert.Equal(built.Exterior, env.Info(env.FindExteriorCell(built.World, -33, 40)!.Value).FormKey);
        Assert.Null(env.FindExteriorCell(built.World, 1, 2));
        Assert.Null(env.FindCellAtGrid(built.World, 1, 2));
        Assert.Null(env.FindPersistentExteriorCell(built.World));
        Assert.Equal(built.Interior, env.ResolveCell("LookupInterior"));
        Assert.Equal(built.Exterior, env.ResolveCell("lookupexterior"));
        Assert.Null(env.ResolveCell("NoSuchCell"));
        Assert.Equal(built.World, env.ResolveWorldspace("LookupWorld"));
        Assert.Null(env.ResolveWorldspace("LookupInterior"));
    }

    [Fact]
    public void GridCoordinatesOutsideTheWorldAreRefused()
    {
        using var temp = new TempDirectory();
        using var built = Create(temp);
        Assert.Equal(Status.InvalidArgument, StatusOf(() => built.Env.FindExteriorCell(built.World, 111, 0)));
        Assert.Equal(Status.InvalidArgument, StatusOf(() => built.Env.FindCellAtGrid(built.World, 200, 0)));
        Assert.Throws<ArgumentNullException>(() => built.Env.ResolveCell(null!));
    }

    private static RadicleEnvironment LoadCellWithOneReference(TempDirectory temp, out FormKey cell, out FormKey stat)
    {
        var statRecord = new Rec("STAT", 0x810, new[]
        {
            ("EDID", Z("Rock")), ("OBND", new byte[12]), ("MODL", Z("Rocks\\Rock01.nif")),
        });
        var reference = new Rec("REFR", 0x803, new[]
        {
            ("EDID", Z("RockRef")), ("NAME", U32(0x810)), ("DATA", F32(10, 20, 30, 0, 0, 0)), ("XSCL", F32(1.5f)),
        });
        var cellRecord = new Rec("CELL", 0x820, new[] { ("DATA", new byte[] { 1 }), ("EDID", Z("Vault")) });
        cell = new FormKey("View.esp", 0x820);
        stat = new FormKey("View.esp", 0x810);
        return RadicleEnvironment.Load(new[]
        {
            temp.Write("View.esp", Build(0, Array.Empty<string>(),
                Top("STAT", statRecord),
                Top("CELL", new Grp(0, 2, new Node[]
                {
                    new Grp(0, 3, new Node[]
                    {
                        cellRecord,
                        new Grp(0x820, 6, new Node[] { new Grp(0x820, 9, new Node[] { reference }) }),
                    }),
                })))),
        });
    }

    // Detects: CellView must copy the interior header and the placed reference out of the native view, then free it.
    [Fact]
    public void CellViewReportsTheInteriorHeaderAndItsPlacedReference()
    {
        using var temp = new TempDirectory();
        using var env = LoadCellWithOneReference(temp, out var cell, out var stat);

        var view = env.CellView(cell);
        Assert.Equal(cell, view.Header.Key);
        Assert.Equal("Vault", view.Header.EditorId);
        Assert.True(view.Header.Interior);
        Assert.Null(view.Header.Grid);
        Assert.Null(view.Header.Worldspace);

        Assert.Single(view.References);
        var entry = view.References[0];
        Assert.Equal("RockRef", entry.EditorId);
        Assert.Equal(stat, entry.Base);
        Assert.Equal((10f, 20f, 30f), entry.Position);
        Assert.Equal(1.5f, entry.Scale);
        Assert.False(entry.Persistent);
        Assert.False(entry.InitiallyDisabled);
        var baseInfo = entry.BaseInfo!;
        Assert.Equal("STAT", baseInfo.Signature);
        Assert.Equal("Rocks\\Rock01.nif", baseInfo.ModelPath);
        Assert.False(baseInfo.EditorMarker);
    }

    // Detects: BaseObject must read the template behind the base form key, including its model path.
    [Fact]
    public void BaseObjectReportsTheStaticTemplateOfAReference()
    {
        using var temp = new TempDirectory();
        using var env = LoadCellWithOneReference(temp, out _, out var stat);

        var info = env.BaseObject(stat);
        Assert.Equal(stat, info.Key);
        Assert.Equal("STAT", info.Signature);
        Assert.Equal("Rock", info.EditorId);
        Assert.Equal("Rocks\\Rock01.nif", info.ModelPath);
        Assert.False(info.EditorMarker);
        Assert.Empty(info.ScolParts);
    }

    [Fact]
    public void CellCreationRefusesReservedFlagsAndAMissingWorldspace()
    {
        using var plugin = PluginFile.Create("Cells.esp");
        var world = plugin.CreateRecord("WRLD", "LookupWorld");
        Assert.Equal(Status.InvalidArgument, StatusOf(() => plugin.CreateExteriorCell(world, 0, 0, "Reserved", flags: 1)));
        Assert.Equal(Status.InvalidArgument,
            StatusOf(() => plugin.CreateExteriorCell(new FormKey("Cells.esp", 0x999), 0, 0, "Missing")));
        Assert.Throws<ArgumentNullException>(() => plugin.CreateInteriorCell(null!));
    }
}
