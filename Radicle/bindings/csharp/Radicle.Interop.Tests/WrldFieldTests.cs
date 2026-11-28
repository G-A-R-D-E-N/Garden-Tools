using System;
using System.Collections.Generic;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class WrldFieldTests
{
    private const string Plugin = "World.esp";

    private static Radicle.Interop.FormKey K(uint id) => new(Plugin, id);

    private static readonly Radicle.Interop.FormKey World = K(0x0C05);
    private static readonly Radicle.Interop.FormKey ShortWorld = K(0x0C06);

// Constraint: source:Radicle/schema/fallout4/fields.json; WLEV carries two per-level members in one 4-byte payload, MHDT starts with two i16 heights, and OFST/CLSZ are 2x2 u32 grids sized by NAM0/NAM9.
    private static byte[] Fixture()
    {
        var mhdt = new byte[12];
        BitConverter.GetBytes((short)-2).CopyTo(mhdt, 0);
        BitConverter.GetBytes((short)-1).CopyTo(mhdt, 2);
        BitConverter.GetBytes((short)3).CopyTo(mhdt, 4);
        BitConverter.GetBytes((short)4).CopyTo(mhdt, 6);
        BitConverter.GetBytes(0x0B0A0908u).CopyTo(mhdt, 8);
        return Build(0, Array.Empty<string>(),
            Top("WRLD",
                new Rec("WRLD", 0x00000C05, new[]
                {
                    ("EDID", Z("OffsetWorld")),
                    ("NAM0", F32(0f, 0f)),
                    ("NAM9", F32(1f, 1f)),
                    ("WLEV", new byte[] { 0xDF, 0x19, 0x07, 0x0C }),
                    ("MHDT", mhdt),
                    ("OFST", U32(11, 12, 13, 14)),
                    ("CLSZ", U32(21, 22, 23, 24)),
                }),
                new Rec("WRLD", 0x00000C06, new[]
                {
                    ("EDID", Z("ShortOffsetWorld")),
                    ("MHDT", new byte[4]),
                })));
    }

    private static PluginFile Open(TempDirectory temp) => PluginFile.Read(temp.Write(Plugin, Fixture()));

// Constraint: Detects a regression where WLEV/MHDT became unreachable through the managed field-text marshaler, where the OFST/CLSZ grids stopped exposing row-major cells, or where a short payload reported a value instead of NOT_FOUND.
    [Fact]
    public void FieldTextReadsWlevMhdtAndTwoDimensionalGrids()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.Equal("7", plugin.FieldText(World, "WRLD\\LevelWidth"));
        Assert.Equal("12", plugin.FieldText(World, "WRLD\\LevelHeight"));
        Assert.Equal("-2", plugin.FieldText(World, "WRLD\\MaxHeightMinX"));
        Assert.Equal("4", plugin.FieldText(World, "WRLD\\MaxHeightMaxY"));

        Assert.Equal("11", plugin.FieldText(World, "WRLD\\Offset"));
        Assert.Equal("14", plugin.FieldText(World, "WRLD\\Offset", 3));
        Assert.Equal("23", plugin.FieldText(World, "WRLD\\CellSize", 2));
        Assert.Equal(4, plugin.FieldCount(World, "WRLD\\Offset"));
        Assert.False(plugin.TryFieldText(World, "WRLD\\Offset", out _, 4));

        Assert.False(plugin.TryFieldText(ShortWorld, "WRLD\\MaxHeightMinX", out var missing));
        Assert.Equal(string.Empty, missing);
        Assert.Throws<KeyNotFoundException>(() => plugin.FieldText(ShortWorld, "WRLD\\MaxHeightMinX"));
        Assert.False(plugin.TryFieldText(K(0x0C99), "WRLD\\LevelWidth", out _));
    }

// Constraint: Detects a regression where WLEV/MHDT or a grid cell could not be edited through SetScalar, or where the edits did not survive a save and fresh read.
    [Fact]
    public void SetScalarWritesLevelMaxHeightAndGridCells()
    {
        using var temp = new TempDirectory();
        using var plugin = Open(temp);

        Assert.True(plugin.SetScalar(World, "WRLD\\LevelHeight", "40"));
        Assert.True(plugin.SetScalar(World, "WRLD\\MaxHeightMinX", "-300"));
        Assert.True(plugin.SetScalar(World, "WRLD\\Offset", "99", 1));
        Assert.True(plugin.SetScalar(World, "WRLD\\CellSize", "77", 3));

        var output = temp.Write("saved/" + Plugin, plugin.ToBytes());
        using var reloaded = PluginFile.Read(output);
        Assert.Equal("40", reloaded.FieldText(World, "WRLD\\LevelHeight"));
        Assert.Equal("-300", reloaded.FieldText(World, "WRLD\\MaxHeightMinX"));
        Assert.Equal("99", reloaded.FieldText(World, "WRLD\\Offset", 1));
        Assert.Equal("77", reloaded.FieldText(World, "WRLD\\CellSize", 3));
    }
}
