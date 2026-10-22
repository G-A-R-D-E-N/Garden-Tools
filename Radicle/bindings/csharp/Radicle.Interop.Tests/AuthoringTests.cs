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
    public void RefusalsMapToExceptionsOrFalse()
    {
        using var plugin = PluginFile.Create("Authoring.esp");
        var armor = plugin.CreateRecord("ARMO", "NewArmor");
        var missing = new Radicle.Interop.FormKey("Authoring.esp", 0x8FF);

        Assert.False(plugin.AddSubrecord(missing, "ICON"));
        Assert.False(plugin.RemoveSubrecord(missing, "ICON"));
        foreach (var signature in new[] { "DESC", "MOD2", "KWDA", "ICO" })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.AddSubrecord(armor, signature)).Status);
        }
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(armor, "DATA")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("BPTD", "X")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("EYES", "X")).Status);
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

        foreach (var (key, signature) in new[] { (armor, "MOD2"), (armor, "MOD4"), (projectile, "MODL"), (projectile, "DEST"), (projectile, "DSTD") })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.AddSubrecord(key, signature)).Status);
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(key, signature)).Status);
        }
        Assert.Equal(before, plugin.ToBytes());
        Assert.True(plugin.RemoveSubrecord(armor, "DESC"));
        Assert.True(IndexOf(plugin.ToBytes(), "MOD2") >= 0);
    }
}
