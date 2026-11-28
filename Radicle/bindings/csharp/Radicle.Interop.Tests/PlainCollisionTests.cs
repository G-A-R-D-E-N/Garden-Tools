using System;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class PlainCollisionTests
{
    private const uint WoodCrateCrc = 341181474U;
    private const uint MetalCrc = 0x064003D4U;

    private static string NamesPlugin(TempDirectory directory) =>
        directory.Write("Names.esm", Build(0x1, Array.Empty<string>(),
            Top("COLL",
                new Rec("COLL", 0x800, new[] { ("BNAM", U32(0)), ("MNAM", Z("L_UNIDENTIFIED")) }),
                new Rec("COLL", 0x801, new[] { ("BNAM", U32(1)), ("MNAM", Z("L_STATIC")) }),
                new Rec("COLL", 0x802, new[] { ("BNAM", U32(3)), ("MNAM", Z("L_CLUTTER")) })),
            Top("MATT",
                new Rec("MATT", 0x803, new[] { ("EDID", Z("MaterialWoodCrate")) }),
                new Rec("MATT", 0x804, new[] { ("EDID", Z("Metal")) }))));

    private static string ListsPlugin(TempDirectory directory) =>
        directory.Write("Lists.esm", Build(0x1, Array.Empty<string>(),
            Top("COLL",
                new Rec("COLL", 0x800, new[] { ("BNAM", U32(0)), ("MNAM", Z("L_UNIDENTIFIED")), ("CNAM", U32(0x801)) }),
                new Rec("COLL", 0x801, new[] { ("BNAM", U32(1)), ("MNAM", Z("L_STATIC")), ("CNAM", U32(0x800, 0x802)) }),
                new Rec("COLL", 0x802, new[] { ("BNAM", U32(3)), ("MNAM", Z("L_CLUTTER")) }),
                new Rec("COLL", 0x805, new[] { ("BNAM", U32(5)), ("MNAM", Z("L_NAVCUT")) }))));

    // Detects (invariant): the managed flag reading the wrong layer or the wrong sense: only an empty list that no other layer lists is true; a listed empty layer, a gap and an index past the table are false.
    [Fact]
    public void FlagsOnlyAnEmptyListNoOtherLayerListsAsCollidingWithNothing()
    {
        using var directory = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { ListsPlugin(directory) });
        using var names = PlainCollisionNames.Build(env);
        Assert.False(names.CollidesWithNothing(0));
        Assert.False(names.CollidesWithNothing(1));
        Assert.False(names.CollidesWithNothing(2));
        Assert.False(names.CollidesWithNothing(3));
        Assert.True(names.CollidesWithNothing(5));
        Assert.False(names.CollidesWithNothing(4000));
        names.Dispose();
        Assert.Throws<ObjectDisposedException>(() => names.CollidesWithNothing(5));
    }

    // Detects (invariant): the managed list read returning FormIDs instead of layer indexes, freeing the table's array, or a gap, an unlisted layer and an index past the table not coming back empty.
    [Fact]
    public void ReturnsTheCollidesWithListAsLayerIndexes()
    {
        using var directory = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { ListsPlugin(directory) });
        using var names = PlainCollisionNames.Build(env);
        Assert.Equal(new uint[] { 1 }, names.CollidesWith(0));
        Assert.Equal(new uint[] { 0, 3 }, names.CollidesWith(1));
        Assert.Equal(new uint[] { 0, 3 }, names.CollidesWith(1));
        Assert.Empty(names.CollidesWith(2));
        Assert.Empty(names.CollidesWith(3));
        Assert.Empty(names.CollidesWith(5));
        Assert.Empty(names.CollidesWith(4000));
        names.Dispose();
        Assert.Throws<ObjectDisposedException>(() => names.CollidesWith(1));
    }

    [Fact]
    public void ANifWithoutCollisionHasNoBodiesOrShapes()
    {
        var model = PlainCollision.FromBytes(NifFixtures.FullNif());
        Assert.Empty(model.Bodies);
        Assert.Empty(model.Shapes);
        Assert.Equal(0u, model.SkippedShapes);
    }

    [Fact]
    public void AnUndecodableCollisionPayloadYieldsNoBodies()
    {
        var model = PlainCollision.FromBytes(NifFixtures.CollisionNif());
        Assert.Empty(model.Bodies);
        Assert.Empty(model.Shapes);
    }

    [Fact]
    public void RefusesEmptyAndMalformedBytes()
    {
        Assert.Throws<ArgumentException>(() => PlainCollision.FromBytes(ReadOnlySpan<byte>.Empty));
        Assert.Throws<RadicleException>(() => PlainCollision.FromBytes(new byte[] { 1, 2, 3, 4 }));
    }

    [Fact]
    public void NamesLayersAndMaterialsFromTheEnvironmentRecords()
    {
        using var directory = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { NamesPlugin(directory) });
        using var names = PlainCollisionNames.Build(env);
        Assert.Equal("L_UNIDENTIFIED", names.Layer(0));
        Assert.Equal("L_STATIC", names.Layer(1));
        Assert.Null(names.Layer(2));
        Assert.Equal("L_CLUTTER", names.Layer(3));
        Assert.Null(names.Layer(4000));
        Assert.Equal(4, names.LayerCount);
        Assert.Equal("MaterialWoodCrate", names.Material(WoodCrateCrc));
        Assert.Equal("Metal", names.Material(MetalCrc));
        Assert.Null(names.Material(1));
        Assert.Equal(2, names.MaterialCount);
    }

    [Fact]
    public void NamesOutliveTheEnvironmentTheyWereBuiltFrom()
    {
        using var directory = new TempDirectory();
        PlainCollisionNames names;
        using (var env = RadicleEnvironment.Load(new[] { NamesPlugin(directory) }))
        {
            names = PlainCollisionNames.Build(env);
        }
        using (names)
        {
            Assert.Equal("L_STATIC", names.Layer(1));
            Assert.Equal("Metal", names.Material(MetalCrc));
        }
    }

    [Fact]
    public void AnEnvironmentWithoutCollOrMattHasEmptyTables()
    {
        using var directory = new TempDirectory();
        var path = directory.Write("Empty.esm", Build(0x1, Array.Empty<string>()));
        using var env = RadicleEnvironment.Load(new[] { path });
        using var names = PlainCollisionNames.Build(env);
        Assert.Null(names.Layer(0));
        Assert.Null(names.Material(MetalCrc));
        Assert.Equal(0, names.LayerCount);
        Assert.Equal(0, names.MaterialCount);
    }

    [Fact]
    public void ADisposedNamesTableRefusesLookups()
    {
        using var directory = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { NamesPlugin(directory) });
        var names = PlainCollisionNames.Build(env);
        names.Dispose();
        names.Dispose();
        Assert.Throws<ObjectDisposedException>(() => names.Layer(0));
        Assert.Throws<ObjectDisposedException>(() => names.Material(MetalCrc));
        Assert.Throws<ObjectDisposedException>(() => names.LayerCount);
    }

    [Fact]
    public void BuildingNamesFromADisposedEnvironmentThrows()
    {
        using var directory = new TempDirectory();
        var env = RadicleEnvironment.Load(new[] { NamesPlugin(directory) });
        env.Dispose();
        Assert.Throws<ObjectDisposedException>(() => PlainCollisionNames.Build(env));
        Assert.Throws<ArgumentNullException>(() => PlainCollisionNames.Build(null!));
    }
}
