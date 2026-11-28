using System;
using System.IO;
using System.Linq;
using Radicle.Interop.Native;
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

    // Detects: add and remove navmesh wrappers must append or drop exactly one row, report a missing record as false and refuse a null edge target.
    [Fact]
    public void AddAndRemoveRowsRoundtripThroughSaveAndReload()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVM", new Rec("NAVM", 0x800, new[] { ("NVNM", Geometry(false)) }),
                new Rec("NAVM", 0x802, new[] { ("NVNM", Geometry(false)) }))));
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Navigation.esm", 0x800);
        Assert.True(plugin.AddNavmeshVertex(key, new(4, 5, 6)));
        Assert.True(plugin.AddNavmeshEdgeLink(key, new(1, new("Navigation.esm", 0x802), 0, 1)));
        Assert.True(plugin.RemoveNavmeshDoor(key, 0));
        Assert.False(plugin.AddNavmeshVertex(new FormKey("Navigation.esm", 0x999), new(1, 2, 3)));
        Assert.Throws<RadicleException>(() => plugin.AddNavmeshEdgeLink(key, new(0, null, 0, 0)));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.RemoveNavmeshVertex(key, -1));
        var saved = temp.Write("Saved.esm", plugin.ToBytes());
        using var env = RadicleEnvironment.Load(new[] { saved });
        var mesh = env.Navmesh(env.Winner(new FormKey("Saved.esm", 0x800))!.Value)!;
        Assert.Equal(2, mesh.Vertices.Count);
        Assert.Equal(new NavmeshVertex(4, 5, 6), mesh.Vertices[1]);
        Assert.Equal(2, mesh.Edges.Count);
        Assert.Equal(new NavmeshEdgeLink(1, new("Saved.esm", 0x802), 0, 1), mesh.Edges[1]);
        Assert.Empty(mesh.Doors);
    }

    // Detects: managed edge target edits must keep all other geometry fields after save and reload.
    [Fact]
    public void EdgeTargetRoundtripsWithoutChangingOtherFields()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVM", new Rec("NAVM", 0x800, new[] { ("NVNM", Geometry(false)) }),
                new Rec("NAVM", 0x802, new[] { ("NVNM", Geometry(false)) }))));
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Navigation.esm", 0x800);
        Assert.True(plugin.SetNavmeshEdgeTarget(key, 0, new("Navigation.esm", 0x802)));
        Assert.False(plugin.SetNavmeshEdgeTarget(key, 1, new("Navigation.esm", 0x802)));
        var saved = temp.Write("Saved.esm", plugin.ToBytes());
        using var env = RadicleEnvironment.Load(new[] { saved });
        var mesh = env.Navmesh(env.Winner(new FormKey("Saved.esm", 0x800))!.Value)!;
        Assert.Equal(new NavmeshEdgeLink(0xDEADBEEF, new("Saved.esm", 0x802), -3, 2), Assert.Single(mesh.Edges));
        Assert.Equal(new byte[48], mesh.OpaqueTail);
    }

    [Fact]
    public void EdgeIsExtraReadsFlagBitsZeroToTwoThroughTheNativeAccessor()
    {
        var triangle = new NavmeshTriangle(new ushort[] { 0, 1, 2 }, new short[] { -1, 0, 7 }, 4.5f, 0xAA, 0xABCD8012);
        Assert.False(triangle.EdgeIsExtra(0)); Assert.True(triangle.EdgeIsExtra(1)); Assert.False(triangle.EdgeIsExtra(2));
        Assert.Throws<RadicleException>(() => triangle.EdgeIsExtra(3));
    }

    // Detects: managed nested-path edits must survive save without changing anchors.
    [Fact]
    public void PathLinkReplacementSurvivesSave()
    {
        using var temp = new TempDirectory();
        var nvpp = Payload(w => { w.Write(2u); w.Write(1u); w.Write(0x800u); w.Write(1u); w.Write(0x800u); w.Write(1u); w.Write(0x811u); w.Write(7u); });
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVI", new Rec("NAVI", 0x801, new[] { ("NVER", U32(15)), ("NVPP", nvpp) })),
            Top("NAVM", new Rec("NAVM", 0x802, Array.Empty<(string, byte[])>()))));
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Navigation.esm", 0x801);
        Assert.True(plugin.SetNaviPathLink(key, 1, 0, new("Navigation.esm", 0x802)));
        Assert.False(plugin.SetNaviPathLink(key, 2, 0, new("Navigation.esm", 0x802)));
        var saved = temp.Write("Saved.esm", plugin.ToBytes());
        using var env = RadicleEnvironment.Load(new[] { saved });
        var result = env.NavmeshPaths(env.Winner(new FormKey("Saved.esm", 0x801))!.Value);
        Assert.Equal(new FormKey("Saved.esm", 0x800), Assert.Single(result.Paths[0]));
        Assert.Equal(new FormKey("Saved.esm", 0x802), Assert.Single(result.Paths[1]));
        Assert.Equal(new NaviAnchor(new("Saved.esm", 0x811), 7), Assert.Single(result.Anchors));
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

    private static byte[] Lock(byte level, int size)
    {
        var data = new byte[16];
        data[0] = level;
        BitConverter.GetBytes(0x830u).CopyTo(data, 4);
        return data[..size];
    }

    private static string DoorPlugin(TempDirectory temp) => temp.Write("Doors.esm", Build(1, Array.Empty<string>(),
        Top("NAVM", new Rec("NAVM", 0x800, new[] { ("NVNM", Geometry(false)) })),
        Top("REFR",
            new Rec("REFR", 0x820, new[] { ("NAME", U32(0x900)), ("XLOC", Lock(255, 16)) }),
            new Rec("REFR", 0x821, new[] { ("NAME", U32(0x900)), ("XLOC", Lock(1, 16)) }),
            new Rec("REFR", 0x822, new[] { ("NAME", U32(0x900)), ("XLOC", Lock(255, 4)) }),
            new Rec("REFR", 0x823, new[] { ("NAME", U32(0x900)) }),
            new Rec("REFR", 0x824, new[] { ("NAME", U32(0x900)), ("XTEL", Teleport(0x820)) }),
            new Rec("REFR", 0x825, new[] { ("NAME", U32(0x900)), ("XTEL", Teleport(0x9FF)) }),
            new Rec("REFR", 0x826, new[] { ("NAME", U32(0x900)) }))));

    private static byte[] Teleport(uint door) => U32(door).Concat(new byte[24]).Concat(U32(0, 0)).ToArray();

    [Fact]
    public void TeleportLegNamesTheDestinationDoorAndLeavesUnbuiltPartsNull()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { DoorPlugin(temp) });
        FormKey Ref(uint id) => new("Doors.esm", id);
        var leg = env.DoorTeleportLeg(Ref(0x824), false, false);
        Assert.Equal(Ref(0x820), leg.DestinationDoor);
        Assert.Null(leg.Navmesh);
        Assert.Null(leg.Triangle);
        Assert.Null(leg.Approach);
        Assert.Equal(NavVerdict.Unknown, leg.Verdict);
        Assert.Equal(Ref(0x9FF), env.DoorTeleportLeg(Ref(0x825), false, false).DestinationDoor);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.DoorTeleportLeg(Ref(0x826), false, false)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => env.DoorTeleportLeg(Ref(0x9FF), false, false)).Status);
    }

    [Fact]
    public void EdgeLinkUsableDecidesByLinkType()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { DoorPlugin(temp) });
        Assert.Equal(NavVerdict.Passable, env.EdgeLinkUsable(0, null));
        Assert.Equal(NavVerdict.Unknown, env.EdgeLinkUsable(1, null));
    }

    [Fact]
    public void DoorPassableFollowsLockLevelKeyAndCallerState()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { DoorPlugin(temp) });
        FormKey Ref(uint id) => new("Doors.esm", id);
        Assert.Equal(NavVerdict.Blocked, env.DoorPassable(null, Ref(0x820), true, false));
        Assert.Equal(NavVerdict.NeedsOpening, env.DoorPassable(null, Ref(0x820), true, true));
        Assert.Equal(NavVerdict.Passable, env.DoorPassable(null, Ref(0x820), false, false));
        Assert.Equal(NavVerdict.NeedsOpening, env.DoorPassable(Ref(0x823), Ref(0x820), true, true));
        Assert.Equal(NavVerdict.Unknown, env.DoorPassable(null, Ref(0x821), true, true));
        Assert.Equal(NavVerdict.Unknown, env.DoorPassable(null, Ref(0x822), true, true));
        Assert.Equal(NavVerdict.Unknown, env.DoorPassable(null, Ref(0x823), true, false));
    }

    [Fact]
    public void DoorPassableRefusesBadKeysUnknownReferencesAndADisposedEnvironment()
    {
        using var temp = new TempDirectory();
        var path = DoorPlugin(temp);
        var env = RadicleEnvironment.Load(new[] { path });
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.DoorPassable(null, default, true, false)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.DoorPassable(null, new("", 0x820), true, false)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => env.DoorPassable(null, new("Doors.esm", 0x01000820), true, false)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => env.DoorPassable(null, new("Doors.esm", 0x9FF), true, false)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => env.DoorPassable(null, new("Doors.esm", 0x800), true, false)).Status);
        env.Dispose();
        Assert.Throws<ObjectDisposedException>(() => env.DoorPassable(null, new("Doors.esm", 0x820), true, false));
    }

    private static byte[] PathMesh((float X, float Y, float Z)[] vertices,
        (ushort A, ushort B, ushort C, short E0, short E1, short E2, byte Traversal, uint Flags)[] triangles,
        (uint Type, uint Navmesh, short Triangle, byte Edge)[] links, (short Triangle, uint Door)[] doors) => Payload(w =>
    {
        w.Write(15u); w.Write(0x12345678u); w.Write(0u); w.Write(0x811u);
        w.Write((uint)vertices.Length);
        foreach (var v in vertices) { w.Write(v.X); w.Write(v.Y); w.Write(v.Z); }
        w.Write((uint)triangles.Length);
        foreach (var t in triangles)
        {
            w.Write(t.A); w.Write(t.B); w.Write(t.C); w.Write(t.E0); w.Write(t.E1); w.Write(t.E2);
            w.Write(4.5f); w.Write(t.Traversal); w.Write((ushort)(t.Flags & 0xFFFF)); w.Write((ushort)(t.Flags >> 16));
        }
        w.Write((uint)links.Length);
        foreach (var l in links) { w.Write(l.Type); w.Write(l.Navmesh); w.Write(l.Triangle); w.Write(l.Edge); }
        w.Write((uint)doors.Length);
        foreach (var d in doors) { w.Write(d.Triangle); w.Write(0xABCDEFu); w.Write(d.Door); }
        w.Write(new byte[48]);
    });

    private static string PathPlugin(TempDirectory temp) => temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
        Top("NAVM",
            new Rec("NAVM", 0x800, new[] { ("NVNM", PathMesh(
                new (float, float, float)[] { (0, 0, 0), (3, 0, 0), (0, 3, 0), (3, 3, 0), (6, 3, 0), (100, 0, 0), (103, 0, 0), (100, 3, 0) },
                new (ushort, ushort, ushort, short, short, short, byte, uint)[]
                {
                    (0, 1, 2, -1, 1, -1, 0, 0), (1, 3, 2, 2, -1, 0, 1, 0), (1, 4, 3, -1, -1, 1, 0, 0), (5, 6, 7, 0, -1, -1, 0, 1),
                },
                new (uint, uint, short, byte)[] { (1, 0x802, 0, 0) },
                new (short, uint)[] { (1, 0x820), (2, 0x821) })) }),
            new Rec("NAVM", 0x802, new[] { ("NVNM", PathMesh(
                new (float, float, float)[] { (0, 0, 0), (3, 0, 0), (0, 3, 0) },
                new (ushort, ushort, ushort, short, short, short, byte, uint)[] { (0, 1, 2, -1, -1, -1, 0, 0) },
                Array.Empty<(uint, uint, short, byte)>(), Array.Empty<(short, uint)>())) })),
        Top("REFR",
            new Rec("REFR", 0x820, new[] { ("NAME", U32(0x900)), ("XLOC", Lock(255, 16)) }),
            new Rec("REFR", 0x821, new[] { ("NAME", U32(0x900)), ("XLOC", Lock(1, 16)) }))));

    // Detects: the managed size-query then buffer calls must return the A* route in start-first order with the centroid cost, report no route as Found false, and map unresolved endpoints to NotFound.
    [Fact]
    public void FindPathReturnsTheRouteAndCostAndReportsNoRouteAndBadEndpoints()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { PathPlugin(temp) });
        FormKey Mesh(uint id) => new("Navigation.esm", id);
        var route = env.FindNavmeshPath(Mesh(0x800), 0, Mesh(0x800), 2);
        Assert.True(route.Found);
        Assert.Equal(new[] { 0, 1, 2 }, route.Nodes.Select(n => n.Triangle));
        Assert.All(route.Nodes, n => { Assert.Equal(Mesh(0x800), n.Navmesh); Assert.False(n.ViaLink); Assert.False(n.NeedsOpening); });
        Assert.InRange(route.Cost, 3.4132f, 3.4152f);

        var same = env.FindNavmeshPath(Mesh(0x800), 1, Mesh(0x800), 1);
        Assert.True(same.Found);
        Assert.Equal(1, Assert.Single(same.Nodes).Triangle);
        Assert.Equal(0f, same.Cost);

        var none = env.FindNavmeshPath(Mesh(0x800), 0, Mesh(0x800), 3);
        Assert.False(none.Found);
        Assert.Empty(none.Nodes);
        Assert.Equal(0f, none.Cost);

        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => env.FindNavmeshPath(Mesh(0x800), 9, Mesh(0x800), 2)).Status);
        Assert.Equal(Status.NotFound, Assert.Throws<RadicleException>(() => env.FindNavmeshPath(Mesh(0x9FF), 0, Mesh(0x800), 2)).Status);
        Assert.Throws<ArgumentOutOfRangeException>(() => env.FindNavmeshPath(Mesh(0x800), -1, Mesh(0x800), 2));
    }

    // Detects: path options must reach the native search, so a swapped flag bit, a dropped class mask or door state, or a lost link flag changes the route.
    [Fact]
    public void FindPathHonoursClassMaskDoorStatesAndUnknownLinkAndDoorFlags()
    {
        using var temp = new TempDirectory();
        using var env = RadicleEnvironment.Load(new[] { PathPlugin(temp) });
        FormKey Mesh(uint id) => new("Navigation.esm", id);
        FormKey Ref(uint id) => new("Navigation.esm", id);
        NavmeshPathResult Find(NavmeshPathOptions options) => env.FindNavmeshPath(Mesh(0x800), 0, Mesh(0x800), 2, options);

        Assert.False(Find(new(ForbiddenClass: 1)).Found);
        Assert.True(Find(new(ForbiddenClass: 2)).Found);

        Assert.False(Find(new(Doors: new[] { new NavmeshPathDoorState(Ref(0x820), true, false) })).Found);
        var opened = Find(new(Doors: new[] { new NavmeshPathDoorState(Ref(0x820), true, true) }));
        Assert.True(opened.Found);
        Assert.Equal(new[] { false, true, false }, opened.Nodes.Select(n => n.NeedsOpening));

        var unknownDoor = new[] { new NavmeshPathDoorState(Ref(0x821), true, true) };
        Assert.False(Find(new(Doors: unknownDoor)).Found);
        Assert.True(Find(new(AllowUnknownDoors: true, Doors: unknownDoor)).Found);
        Assert.False(Find(new(AllowUnknownLinks: true, Doors: unknownDoor)).Found);

        var viaLink = env.FindNavmeshPath(Mesh(0x800), 3, Mesh(0x802), 0);
        Assert.False(viaLink.Found);
        var crossed = env.FindNavmeshPath(Mesh(0x800), 3, Mesh(0x802), 0, new(AllowUnknownLinks: true));
        Assert.True(crossed.Found);
        Assert.Equal(new[] { (Mesh(0x800), 3, false), (Mesh(0x802), 0, true) }, crossed.Nodes.Select(n => (n.Navmesh, n.Triangle, n.ViaLink)));
        Assert.False(env.FindNavmeshPath(Mesh(0x800), 3, Mesh(0x802), 0, new(AllowUnknownDoors: true)).Found);
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

    // Detects: managed NVMI link edits must survive reload without changing island or parent data.
    [Fact]
    public void NaviLinksRoundtripThroughManagedWriter()
    {
        using var temp = new TempDirectory();
        var path = temp.Write("Navigation.esm", Build(1, Array.Empty<string>(),
            Top("NAVI", new Rec("NAVI", 0x801, new[] { ("NVER", U32(15)), ("NVMI", Info(true)) })),
            Top("NAVM", new Rec("NAVM", 0x802, Array.Empty<(string, byte[])>())),
            Top("REFR", new Rec("REFR", 0x803, Array.Empty<(string, byte[])>()))));
        using var plugin = PluginFile.Read(path);
        var key = new FormKey("Navigation.esm", 0x801);
        Assert.True(plugin.SetNaviLink(key, 0, NaviLinkKind.Edge, 0, new("Navigation.esm", 0x802)));
        Assert.True(plugin.SetNaviLink(key, 0, NaviLinkKind.PreferredEdge, 0, new("Navigation.esm", 0x802)));
        Assert.True(plugin.SetNaviLink(key, 0, NaviLinkKind.Door, 0, new("Navigation.esm", 0x803)));
        Assert.False(plugin.SetNaviLink(key, 0, NaviLinkKind.Edge, 1, new("Navigation.esm", 0x802)));
        Assert.Throws<ArgumentOutOfRangeException>(() => plugin.SetNaviLink(key, -1, NaviLinkKind.Edge, 0, new("Navigation.esm", 0x802)));
        var before = plugin.ToBytes();
        Assert.Throws<RadicleException>(() => plugin.SetNaviLink(key, 0, NaviLinkKind.Door, 0, new("Navigation.esm", 0x802)));
        Assert.Equal(before, plugin.ToBytes());
        var saved = temp.Write("Saved.esm", before);
        using var env = RadicleEnvironment.Load(new[] { saved });
        var info = Assert.Single(env.NavmeshInfos(env.Winner(new FormKey("Saved.esm", 0x801))!.Value));
        Assert.Equal(new FormKey("Saved.esm", 0x802), Assert.Single(info.Edges));
        Assert.Equal(new FormKey("Saved.esm", 0x802), Assert.Single(info.PreferredEdges));
        Assert.Equal(new NaviDoorLink(0xDEADBEEF, new("Saved.esm", 0x803)), Assert.Single(info.Doors));
        Assert.Equal(50, info.Island.Length);
        Assert.Equal(new FormKey("Saved.esm", 0x811), info.Parent.Cell);
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
