using System;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;

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
        foreach (var signature in new[] { "MOD2", "KWDA", "ICO" })
        {
            Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.AddSubrecord(armor, signature)).Status);
        }
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(armor, "DATA")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("MISC", "X")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("EYES", "X")).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => plugin.CreateRecord("ARMO", "\u4e2d")).Status);
        Assert.Throws<ArgumentException>(() => plugin.CreateRecord("ARMO", "A\0B"));
        Assert.Throws<ArgumentNullException>(() => plugin.AddSubrecord(armor, null!));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.RemoveSubrecord(armor, "ICON", -1));
        Assert.Equal(0x801u, plugin.CreateRecord("LVSP", "List").LocalId);
    }
}
