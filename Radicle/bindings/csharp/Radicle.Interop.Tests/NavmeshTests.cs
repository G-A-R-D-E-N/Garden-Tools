using System;
using System.IO;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class NavmeshTests
{
    [Fact]
    public void TypedEditsReparseAndRefuseUnknownChanges()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVM", new Rec("NAVM", 0x800, new[] { ("NVNM", Geometry(false)) })),
            Top("REFR", new Rec("REFR", 0x813, Array.Empty<(string, byte[])>()))));
        var key = new FormKey("Navigation.esm", 0x800);
        using var plugin = PluginFile.Read(path);
        Assert.True(plugin.SetNavmeshVertex(key, 0, new(9, 10, 11)));
        Assert.True(plugin.SetNavmeshTriangle(key, 0, new(new ushort[] { 0, 0, 0 }, new short[] { -1, 0, -1 }, 6.5f, 0xAA, 0xABCD8012)));
        Assert.True(plugin.SetNavmeshDoor(key, 0, new(0, 0x87654321, new("Navigation.esm", 0x813))));
        Assert.False(plugin.SetNavmeshVertex(key, 1, new(1, 2, 3)));
        var bytes = plugin.ToBytes();
        Assert.Throws<RadicleException>(() => plugin.SetNavmeshVertex(key, 0, new(float.NaN, 2, 3)));
        Assert.Throws<RadicleException>(() => plugin.SetNavmeshTriangle(key, 0, new(new ushort[] { 0, 0, 0 }, new short[3], 1, 0, 0)));
        for (var bit = 0; bit < 32; ++bit)
            Assert.Throws<RadicleException>(() => plugin.SetNavmeshTriangle(key, 0,
                new(new ushort[] { 0, 0, 0 }, new short[] { -1, 0, -1 }, 6.5f, 0xAA, 0xABCD8012u ^ (1u << bit))));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetNavmeshDoor(key, -1, new(0, 0, null)));
        Assert.Equal(bytes, plugin.ToBytes());
        var saved = temp.Write("Saved.esm", bytes);
        using var env = RadicleEnvironment.Load(new[] { saved });
        var mesh = env.Navmesh(env.Winner(new FormKey("Saved.esm", 0x800))!.Value)!;
        Assert.Equal(new NavmeshVertex(9, 10, 11), Assert.Single(mesh.Vertices));
        Assert.Equal(6.5f, Assert.Single(mesh.Triangles).Clearance);
        Assert.Equal(0x87654321u, Assert.Single(mesh.Doors).Crc);
        Assert.Equal(new byte[48], mesh.OpaqueTail);
    }

    [Fact]
    public void EdgeIsExtraReadsFlagBitsZeroToTwoThroughTheNativeAccessor()
    {
        var triangle = new NavmeshTriangle(new ushort[] { 0, 1, 2 }, new short[] { -1, 0, 7 }, 4.5f, 0xAA, 0xABCD8012);
        Assert.False(triangle.EdgeIsExtra(0)); Assert.True(triangle.EdgeIsExtra(1)); Assert.False(triangle.EdgeIsExtra(2));
        Assert.Throws<RadicleException>(() => triangle.EdgeIsExtra(3));
    }

    [Fact]
    public void PathGetterReadsNvppListsAndAnchorsAndRefusesTruncation()
    {
        using var temp = new TempDirectory();
        var nvpp = Payload(w =>
        {
            w.Write(2u); w.Write(2u); w.Write(0x800u); w.Write(0x810u); w.Write(1u); w.Write(0x810u);
            w.Write(1u); w.Write(0x800u); w.Write(7u);
        });
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(), Top("NAVI",
            new Rec("NAVI", 0x801, new[] { ("NVER", U32(15)), ("NVPP", nvpp) }),
            new Rec("NAVI", 0x802, new[] { ("NVER", U32(15)), ("NVPP", nvpp[..^1]) }))));
        using var env = RadicleEnvironment.Load(new[] { path });
        var good = env.NavmeshPaths(env.Winner(new FormKey("Navigation.esm", 0x801))!.Value);
        Assert.Equal(new FormKey?[] { new("Navigation.esm", 0x800), new("Navigation.esm", 0x810) }, good.Paths[0]);
        Assert.Equal(new FormKey?[] { new("Navigation.esm", 0x810) }, good.Paths[1]);
        Assert.Equal(new NaviAnchor(new("Navigation.esm", 0x800), 7), Assert.Single(good.Anchors));
        Assert.Throws<RadicleException>(() => env.NavmeshPaths(env.Winner(new FormKey("Navigation.esm", 0x802))!.Value));
    }

    private static byte[] Payload(Action<BinaryWriter> write)
    {
        using var stream = new MemoryStream();
        using var w = new BinaryWriter(stream);
        write(w);
        return stream.ToArray();
    }

    private static byte[] Geometry(bool exterior) => Payload(w =>
    {
        w.Write(15u); w.Write(0x12345678u); w.Write(exterior ? 0x810u : 0u);
        if (exterior) { w.Write((short)-2); w.Write((short)7); } else w.Write(0x811u);
        w.Write(1u); w.Write(1.5f); w.Write(-2f); w.Write(3f);
        w.Write(1u); w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)2);
        w.Write((short)-1); w.Write((short)0); w.Write((short)7); w.Write(4.5f);
        w.Write((byte)0xAA); w.Write((ushort)0x8012); w.Write((ushort)0xABCD);
        w.Write(1u); w.Write(0xDEADBEEFu); w.Write(0x812u); w.Write((short)-3); w.Write((byte)2);
        w.Write(1u); w.Write((short)-5); w.Write(0xABCDEFu); w.Write(0x813u);
        w.Write(new byte[48]);
    });

    private static byte[] Info(bool island) => Payload(w =>
    {
        w.Write(0x800u); w.Write(0xFEDCBA98u); w.Write(5f); w.Write(6f); w.Write(7f); w.Write(0.25f);
        w.Write(1u); w.Write(0x812u); w.Write(1u); w.Write(0x814u);
        w.Write(1u); w.Write(0xDEADBEEFu); w.Write(0x813u); w.Write(island);
        if (island)
        {
            for (var i = 0; i < 6; ++i) w.Write((float)i);
            w.Write(1u); w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)2);
            w.Write(1u); w.Write(1f); w.Write(2f); w.Write(3f);
        }
        w.Write(0x87654321u); w.Write(0u); w.Write(0x811u);
    });

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void GeometryGetterCopiesStoredFieldsAndParentUnion(bool exterior)
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVM", new Rec("NAVM", 0x800, new[] { ("NVNM", Geometry(exterior)) }),
                new Rec("NAVM", 0x802, Array.Empty<(string, byte[])>()))));
        NavmeshGeometry mesh;
        using (var env = RadicleEnvironment.Load(new[] { path }))
        {
            mesh = env.Navmesh(env.Winner(new FormKey("Navigation.esm", 0x800))!.Value)!;
            Assert.Null(env.Navmesh(env.Winner(new FormKey("Navigation.esm", 0x802))!.Value));
        }
        Assert.Equal(15u, mesh.Version);
        Assert.Equal(0x12345678u, mesh.Parent.Crc);
        Assert.Equal(exterior ? new FormKey("Navigation.esm", 0x810) : (FormKey?)null, mesh.Parent.World);
        Assert.Equal(exterior ? null : new FormKey("Navigation.esm", 0x811), mesh.Parent.Cell);
        Assert.Equal(exterior ? (short)-2 : (short)0, mesh.Parent.GridY);
        Assert.Equal(exterior ? (short)7 : (short)0, mesh.Parent.GridX);
        Assert.Equal(new NavmeshVertex(1.5f, -2f, 3f), Assert.Single(mesh.Vertices));
        var t = Assert.Single(mesh.Triangles);
        Assert.Equal(new ushort[] { 0, 1, 2 }, t.Vertices); Assert.Equal(new short[] { -1, 0, 7 }, t.Edges);
        Assert.Equal(4.5f, t.Clearance); Assert.Equal((byte)0xAA, t.Traversal);
        Assert.Equal(0xABCD8012u, (uint)t.Flags);
        Assert.Equal(new NavmeshEdgeLink(0xDEADBEEF, new("Navigation.esm", 0x812), -3, 2), Assert.Single(mesh.Edges));
        Assert.Equal(new NavmeshDoorLink(-5, 0xABCDEF, new("Navigation.esm", 0x813)), Assert.Single(mesh.Doors));
        Assert.Equal(new byte[48], mesh.OpaqueTail);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void InfoGetterCopiesLinksParentAndOpaqueIsland(bool island)
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(), Top("NAVI",
            new Rec("NAVI", 0x801, new[] { ("NVER", U32(15)), ("NVMI", Info(island)) }))));
        NavmeshInfo info;
        using (var env = RadicleEnvironment.Load(new[] { path }))
            info = Assert.Single(env.NavmeshInfos(env.Winner(new FormKey("Navigation.esm", 0x801))!.Value));
        Assert.Equal(new FormKey("Navigation.esm", 0x800), info.Navmesh);
        Assert.Equal(0xFEDCBA98u, info.Flags); Assert.Equal(new NavmeshVertex(5, 6, 7), info.Location);
        Assert.Equal(0.25f, info.Preferred);
        Assert.Equal(new FormKey("Navigation.esm", 0x812), Assert.Single(info.Edges));
        Assert.Equal(new FormKey("Navigation.esm", 0x814), Assert.Single(info.PreferredEdges));
        Assert.Equal(new NaviDoorLink(0xDEADBEEF, new("Navigation.esm", 0x813)), Assert.Single(info.Doors));
        Assert.Equal(island, info.HasIsland); Assert.Equal(island ? 50 : 0, info.Island.Length);
        Assert.Equal(new NavmeshParent(0x87654321, null, new("Navigation.esm", 0x811), 0, 0), info.Parent);
    }

    [Fact]
    public void InfoFieldsUseSharedWriterAndPreserveIsland()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(), Top("NAVI",
            new Rec("NAVI", 0x801, new[] { ("NVER", U32(15)), ("NVMI", Info(true)) }))));
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Navigation.esm", 0x801);
        foreach (var field in new[] { "NAVI\\LocationX", "NAVI\\LocationY", "NAVI\\LocationZ", "NAVI\\Preferred" })
            Assert.True(plugin.SetScalar(key, field, "1.25"));
        var bytes = plugin.ToBytes();
        Assert.Throws<RadicleException>(() => plugin.RemoveSubrecord(key, "NVMI"));
        Assert.Throws<RadicleException>(() => plugin.CreateRecord("NAVI", "new"));
        Assert.Equal(bytes, plugin.ToBytes());
        var saved = temp.Write("Saved.esm", bytes);
        using var env = RadicleEnvironment.Load(new[] { saved });
        var info = Assert.Single(env.NavmeshInfos(env.Winner(new FormKey("Saved.esm", 0x801))!.Value));
        Assert.Equal(new NavmeshVertex(1.25f, 1.25f, 1.25f), info.Location);
        Assert.Equal(1.25f, info.Preferred); Assert.Equal(50, info.Island.Length);
    }
}
