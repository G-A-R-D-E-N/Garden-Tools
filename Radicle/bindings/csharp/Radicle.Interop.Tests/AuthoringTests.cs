using System;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class AuthoringTests
{
    private static int IndexOf(byte[] bytes, string text) =>
        bytes.AsSpan().IndexOf(Encoding.ASCII.GetBytes(text));

    private static void AssertOrder(byte[] bytes, params string[] signatures)
    {
        var positions = signatures.Select(s => IndexOf(bytes, s)).ToArray();
        Assert.All(positions, p => Assert.True(p >= 0));
        Assert.Equal(positions.OrderBy(p => p), positions);
    }

    [Fact]
    public void CreateAddAndRemoveRoundTripThroughTheNativeLibrary()
    {
        using var plugin = PluginFile.Create("Authoring.esp");
        var armor = plugin.CreateRecord("ARMO", "NewArmor");
        Assert.Equal("Authoring.esp", armor.Plugin);

        Assert.True(plugin.AddSubrecord(armor, "MICO"));
        Assert.True(plugin.AddSubrecord(armor, "ICON"));
        Assert.True(plugin.AddSubrecord(armor, "ICON"));
        var bytes = plugin.ToBytes();
        AssertOrder(bytes, "EDID", "ICON", "MICO", "DATA");
        Assert.Equal(IndexOf(bytes, "ICON"), bytes.AsSpan().LastIndexOf(Encoding.ASCII.GetBytes("ICON")));

        Assert.True(plugin.RemoveSubrecord(armor, "MICO"));
        Assert.False(plugin.RemoveSubrecord(armor, "MICO"));
        Assert.False(plugin.RemoveSubrecord(armor, "ICON", 1));
        Assert.True(IndexOf(plugin.ToBytes(), "MICO") < 0);
    }

    [Fact]
    public void RawRecordAndSubrecordBytesRoundTripThroughTheNativeLibrary()
    {
        using var plugin = PluginFile.Create("Authoring.esp");
        var record = plugin.CreateRawRecord("GLOB");
        Assert.Equal(0x800u, record.LocalId);
        var malformed = new byte[] { 0xA1, 0xB2, 0xC3 };

        Assert.True(plugin.SetSubrecordBytes(record, "FLTV", malformed));
        Assert.True(plugin.SetSubrecordBytes(record, "EDID", Encoding.ASCII.GetBytes("Raw\0")));
        Assert.True(plugin.SetSubrecordBytes(record, "ZERO", ReadOnlySpan<byte>.Empty));
        var bytes = plugin.ToBytes();
        AssertOrder(bytes, "FLTV", "EDID", "ZERO");
        Assert.True(bytes.AsSpan().IndexOf(malformed) >= 0);

        Assert.True(plugin.SetSubrecordBytes(record, "FLTV", new byte[] { 0xD4 }));
        var replaced = plugin.ToBytes();
        Assert.True(replaced.AsSpan().IndexOf(malformed) < 0);
        Assert.Equal(IndexOf(replaced, "FLTV"), replaced.AsSpan().LastIndexOf(Encoding.ASCII.GetBytes("FLTV")));

        Assert.False(plugin.SetSubrecordBytes(new Radicle.Interop.FormKey("Authoring.esp", 0x8FF), "FLTV", malformed));
        Assert.False(plugin.SetSubrecordBytes(record, "FLTV", malformed, 5));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.SetSubrecordBytes(record, "FLT", malformed)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRawRecord("CELL")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRawRecord("LAND")).Status);
        Assert.Throws<ArgumentNullException>(() => plugin.CreateRawRecord(null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetSubrecordBytes(record, "FLTV", malformed, -1));
        Assert.Equal(0x801u, plugin.CreateRawRecord("GLOB", 131).LocalId);
    }

    [Fact]
    public void CellChildrenAndRawCompressedPayloadRoundTripThroughTheNativeLibrary()
    {
        using var plugin = PluginFile.Create("Authoring.esp");
        var world = plugin.CreateRawRecord("WRLD");
        var top = plugin.CreateWorldspacePersistentCell(world);
        var grid = plugin.CreateExteriorCell(world, 3, -2, "Grid");
        var persistent = plugin.CreateRawCellChild(top, "REFR", persistent: true, localId: 0xA00);
        var land = plugin.CreateRawCellChild(grid, "LAND", localId: 0xA01, formVersion: 131);
        var next = plugin.CreateRawCellChild(grid, "ACHR");
        Assert.Equal(0xA00u, persistent.LocalId);
        Assert.Equal(0xA01u, land.LocalId);
        Assert.Equal(0xA02u, next.LocalId);

        var broken = new byte[] { 0x4E, 0x4F, 0x54, 0x5A, 0x4C, 0x49, 0x42, 0xFF, 0xEE };
        Assert.True(plugin.SetSubrecordBytes(persistent, "NAME", new byte[] { 1, 2, 3, 4 }));
        Assert.True(plugin.SetRecordRawCompressedPayload(persistent, 77, broken));
        Assert.True(plugin.ToBytes().AsSpan().IndexOf(broken) >= 0);
        Assert.False(plugin.SetRecordRawCompressedPayload(new Radicle.Interop.FormKey("Authoring.esp", 0x8FF), 1, broken));

        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRawCellChild(grid, "LAND", persistent: true)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRawCellChild(grid, "REFR", localId: 0xA00)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateWorldspacePersistentCell(world)).Status);
        Assert.Throws<ArgumentNullException>(() => plugin.CreateRawCellChild(grid, null!));
    }

    [Fact]
    public void RefusalsMapToExceptionsOrFalse()
    {
        using var plugin = PluginFile.Create("Authoring.esp");
        var armor = plugin.CreateRecord("ARMO", "NewArmor");
        var missing = new Radicle.Interop.FormKey("Authoring.esp", 0x8FF);

        Assert.False(plugin.AddSubrecord(missing, "ICON"));
        Assert.False(plugin.RemoveSubrecord(missing, "ICON"));
        foreach (var signature in new[] { "DESC", "KWDA", "ICO" })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.AddSubrecord(armor, signature)).Status);
        }
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(armor, "DATA")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("BPTD", "X")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("CLAS", "X")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("ARMO", "\u4e2d")).Status);
        Assert.Throws<ArgumentException>(() => plugin.CreateRecord("ARMO", "A\0B"));
        Assert.Throws<ArgumentNullException>(() => plugin.AddSubrecord(armor, null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.RemoveSubrecord(armor, "ICON", -1));
        Assert.Equal(0x801u, plugin.CreateRecord("LVSP", "List").LocalId);
    }

    [Fact]
    public void CoupledSubrecordsAreRefusedWithoutChangingBytes()
    {
        using var temp = new TempDirectory();
        var fixture = Build(0, Array.Empty<string>(),
            Top("ARMO", new Rec("ARMO", 0x800, new[] { ("EDID", Z("Armor")), ("MOD2", Z("a.nif")), ("DESC", Z("Desc")), ("DATA", new byte[12]) })),
            Top("PROJ", new Rec("PROJ", 0x801, new[] { ("EDID", Z("Shot")), ("OBND", new byte[12]), ("DEST", new byte[8]), ("DSTD", new byte[20]), ("DSTF", Array.Empty<byte>()), ("DATA", new byte[92]), ("DNAM", new byte[4]), ("NAM1", Z("m.nif")), ("VNAM", new byte[4]) })));
        using var plugin = PluginFile.Read(temp.Write("Authoring.esp", fixture));
        var armor = new Radicle.Interop.FormKey("Authoring.esp", 0x800);
        var projectile = new Radicle.Interop.FormKey("Authoring.esp", 0x801);
        var before = plugin.ToBytes();

        foreach (var (key, signature) in new[] { (projectile, "MODL"), (projectile, "DEST"), (projectile, "DSTD") })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.AddSubrecord(key, signature)).Status);
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(key, signature)).Status);
        }
        foreach (var signature in new[] { "MOD2", "MOD4" })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(armor, signature)).Status);
        }
        Assert.Equal(before, plugin.ToBytes());
        Assert.True(plugin.RemoveSubrecord(armor, "DESC"));
        Assert.True(IndexOf(plugin.ToBytes(), "MOD2") >= 0);
    }
}
