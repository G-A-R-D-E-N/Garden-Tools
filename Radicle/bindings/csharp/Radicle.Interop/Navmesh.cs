using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record NavmeshParent(uint Crc, FormKey? World, FormKey? Cell, short GridY, short GridX);
public sealed record NavmeshVertex(float X, float Y, float Z);
public sealed record NavmeshTriangle(ushort[] Vertices, short[] Edges, float Clearance, byte Traversal, uint Flags)
{
    public unsafe bool EdgeIsExtra(int edge)
    {
        var native = new Native.NavmeshTriangle { Flags = Flags };
        var extra = 0;
        Call.Check(RadicleNative.NavmeshTriangleEdgeIsExtra(&native, (nuint)(uint)edge, &extra));
        return extra != 0;
    }
}
public sealed record NavmeshEdgeLink(uint Type, FormKey? Navmesh, short Triangle, byte Edge);
public sealed record NavmeshDoorLink(short Triangle, uint Crc, FormKey? Door);
public enum NaviLinkKind { Edge, PreferredEdge, Door }
public sealed record NaviDoorLink(uint Crc, FormKey? Door);
public sealed record NavmeshGeometry(uint Version, NavmeshParent Parent, IReadOnlyList<NavmeshVertex> Vertices,
    IReadOnlyList<NavmeshTriangle> Triangles, IReadOnlyList<NavmeshEdgeLink> Edges, IReadOnlyList<NavmeshDoorLink> Doors, byte[] OpaqueTail);
public sealed record NavmeshInfo(FormKey? Navmesh, uint Flags, NavmeshVertex Location, float Preferred,
    IReadOnlyList<FormKey?> Edges, IReadOnlyList<FormKey?> PreferredEdges, IReadOnlyList<NaviDoorLink> Doors,
    bool HasIsland, byte[] Island, NavmeshParent Parent);
public sealed record NaviAnchor(FormKey? Navmesh, uint Value);
public sealed record NavmeshPathData(IReadOnlyList<IReadOnlyList<FormKey?>> Paths, IReadOnlyList<NaviAnchor> Anchors);
public sealed record NavmeshCover(ushort[] Vertices, byte[] Data);
public sealed record NavmeshCoverMap(ushort Cover, ushort Triangle);
public sealed record NavmeshWaypoint(NavmeshVertex Position, short Triangle, uint Flags);
public sealed record NavmeshPathDoorState(FormKey Door, bool Locked, bool NpcHasKey);
public sealed record NavmeshPathOptions(bool AllowUnknownLinks = false, bool AllowUnknownDoors = false, uint ForbiddenClass = 0,
    IReadOnlyList<NavmeshPathDoorState>? Doors = null);
public sealed record NavmeshPathNode(FormKey Navmesh, int Triangle, bool ViaLink, bool NeedsOpening);
public sealed record NavmeshPathResult(bool Found, float Cost, IReadOnlyList<NavmeshPathNode> Nodes);

public sealed unsafe partial class RadicleEnvironment
{
    private static NavmeshParent NavParent(Native.NavmeshParent p) =>
        new(p.Crc, FormKey.FromNative(p.World), FormKey.FromNative(p.Cell), p.GridY, p.GridX);

    public NavmeshGeometry? Navmesh(Record record)
    {
        using var use = Use(record);
        Native.Navmesh* n;
        Call.Check(RadicleNative.NavmeshDecode(use.Pointer, record.Handle, &n));
        try
        {
            if (n->HasGeometry == 0) return null;
            var triangles = new List<NavmeshTriangle>();
            foreach (var t in Call.Copy<Native.NavmeshTriangle>(n->Triangles, n->TriangleCount))
                triangles.Add(new(new[] { t.Vertices[0], t.Vertices[1], t.Vertices[2] },
                    new[] { t.Edges[0], t.Edges[1], t.Edges[2] }, t.Clearance, t.Traversal, t.Flags));
            return new(n->Version, NavParent(n->Parent),
                Call.Copy<Native.NavmeshVertex>(n->Vertices, n->VertexCount).Select(p => new NavmeshVertex(p.X, p.Y, p.Z)).ToArray(),
                triangles, Call.Copy<Native.NavmeshEdgeLink>(n->Edges, n->EdgeCount)
                    .Select(e => new NavmeshEdgeLink(e.Type, FormKey.FromNative(e.Navmesh), e.Triangle, e.Edge)).ToArray(),
                Call.Copy<Native.NavmeshDoorLink>(n->Doors, n->DoorCount)
                    .Select(d => new NavmeshDoorLink(d.Triangle, d.Crc, FormKey.FromNative(d.Door))).ToArray(),
                Call.Copy<byte>(n->OpaqueTail, n->OpaqueTailSize));
        }
        finally { RadicleNative.NavmeshFree(n); }
    }

    public IReadOnlyList<NavmeshInfo> NavmeshInfos(Record record)
    {
        using var use = Use(record);
        Native.Navi* n;
        Call.Check(RadicleNative.NaviDecode(use.Pointer, record.Handle, &n));
        try
        {
            return Call.Copy<Native.NavmeshInfo>(n->Infos, n->InfoCount).Select(i => new NavmeshInfo(
                FormKey.FromNative(i.Navmesh), i.Flags, new(i.Location.X, i.Location.Y, i.Location.Z), i.Preferred,
                Call.Copy<Native.FormKey>(i.Edges, i.EdgeCount).Select(FormKey.FromNative).ToArray(),
                Call.Copy<Native.FormKey>(i.PreferredEdges, i.PreferredEdgeCount).Select(FormKey.FromNative).ToArray(),
                Call.Copy<Native.NaviDoorLink>(i.Doors, i.DoorCount).Select(d => new NaviDoorLink(d.Crc, FormKey.FromNative(d.Door))).ToArray(),
                i.HasIsland != 0, Call.Copy<byte>(i.Island, i.IslandSize), NavParent(i.Parent))).ToArray();
        }
        finally { RadicleNative.NaviFree(n); }
    }

    public NavmeshPathData NavmeshPaths(Record record)
    {
        using var use = Use(record);
        Native.NaviPathList* list;
        Call.Check(RadicleNative.NaviPathListDecode(use.Pointer, record.Handle, &list));
        try
        {
            var paths = Call.Copy<Native.NaviPath>(list->Paths, list->PathCount).Select(p =>
                (IReadOnlyList<FormKey?>)Call.Copy<Native.FormKey>(p.Navmeshes, p.NavmeshCount).Select(FormKey.FromNative).ToArray()).ToArray();
            return new(paths, Call.Copy<Native.NaviAnchor>(list->Anchors, list->AnchorCount)
                .Select(a => new NaviAnchor(FormKey.FromNative(a.Navmesh), a.Value)).ToArray());
        }
        finally { RadicleNative.NaviPathListFree(list); }
    }

    public IReadOnlyList<Record> CellNavmeshes(FormKey cell)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr* items;
        nuint count;
        Call.Check(RadicleNative.EnvCellNavmeshes(use.Pointer, s.Key(cell), &items, &count));
        return Records(items, count);
    }
}

public sealed unsafe partial class RadicleEnvironment
{
    // Requires: triangle indexes are non-negative; no route is Found false with no nodes, and an unresolved navmesh or triangle throws NotFound.
    public NavmeshPathResult FindNavmeshPath(FormKey startNavmesh, int startTriangle, FormKey goalNavmesh, int goalTriangle,
        NavmeshPathOptions? options = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(startTriangle);
        ArgumentOutOfRangeException.ThrowIfNegative(goalTriangle);
        using var use = Use();
        using var s = new NativeStrings();
        var doors = (options?.Doors ?? Array.Empty<NavmeshPathDoorState>()).Select(d => new Native.NavmeshPathDoor
        {
            Door = s.Key(d.Door),
            State = new Native.DoorState { Locked = (byte)(d.Locked ? 1 : 0), NpcHasKey = (byte)(d.NpcHasKey ? 1 : 0) },
        }).ToArray();
        var native = new Native.NavmeshPathOptions
        {
            Flags = (options?.AllowUnknownLinks == true ? 1u : 0u) | (options?.AllowUnknownDoors == true ? 2u : 0u),
            ForbiddenClass = options?.ForbiddenClass ?? 0,
            Doors = s.AddArray(doors),
            DoorCount = (nuint)doors.Length,
        };
        var start = s.Key(startNavmesh);
        var goal = s.Key(goalNavmesh);
        nuint count;
        int found;
        float cost;
        Call.Check(RadicleNative.NavmeshFindPath(use.Pointer, start, (uint)startTriangle, goal, (uint)goalTriangle, &native,
            null, 0, &count, &found, &cost));
        if (found == 0) return new(false, 0f, Array.Empty<NavmeshPathNode>());
        var nodes = new Native.NavmeshPathNode[checked((int)count)];
        fixed (Native.NavmeshPathNode* buffer = nodes)
            Call.Check(RadicleNative.NavmeshFindPath(use.Pointer, start, (uint)startTriangle, goal, (uint)goalTriangle, &native,
                buffer, (nuint)nodes.Length, &count, &found, &cost));
        return new(true, cost, nodes.Select(n => new NavmeshPathNode(
            FormKey.Required(n.Navmesh, "path node navmesh"), checked((int)n.Triangle), (n.Flags & 1) != 0, (n.Flags & 2) != 0)).ToArray());
    }
}

public sealed unsafe partial class PluginFile
{
    public bool SetNaviPathLink(FormKey record, int path, int index, FormKey target)
    {
        var pathIndex = Index(path);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginSetNaviPathLink(plugin, key, pathIndex, at, s.Key(target)));
    }

    public bool SetNaviLink(FormKey record, int infoIndex, NaviLinkKind kind, int index, FormKey target)
    {
        var info = Index(infoIndex);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginSetNaviLink(plugin, key, info, (Native.NaviLinkKind)kind, at, s.Key(target)));
    }

    public bool SetNavmeshEdgeTarget(FormKey record, int index, FormKey target)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginSetNavmeshEdgeTarget(plugin, key, at, s.Key(target)));
    }

    public bool SetNavmeshVertex(FormKey record, int index, NavmeshVertex value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshVertex { X = value.X, Y = value.Y, Z = value.Z };
            return RadicleNative.PluginSetNavmeshVertex(plugin, key, at, &native);
        });
    }

    public bool SetNavmeshTriangle(FormKey record, int index, NavmeshTriangle value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Vertices is not { Length: 3 } || value.Edges is not { Length: 3 })
            throw new ArgumentException("A triangle requires three vertices and edges.", nameof(value));
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshTriangle { Clearance = value.Clearance, Traversal = value.Traversal, Flags = value.Flags };
            for (var i = 0; i < 3; ++i) { native.Vertices[i] = value.Vertices[i]; native.Edges[i] = value.Edges[i]; }
            return RadicleNative.PluginSetNavmeshTriangle(plugin, key, at, &native);
        });
    }

    public bool SetNavmeshDoor(FormKey record, int index, NavmeshDoorLink value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshDoorLink { Triangle = value.Triangle, Crc = value.Crc, Door = s.Key(value.Door) };
            return RadicleNative.PluginSetNavmeshDoor(plugin, key, at, &native);
        });
    }

    public FormKey CreateNavmesh(FormKey cell)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Native.FormKey created;
        Call.Check(RadicleNative.PluginCreateNavmesh(use.Pointer, s.Key(cell), &created));
        return FormKey.FromNative(created)!.Value;
    }

    public bool AddNavmeshVertex(FormKey record, NavmeshVertex value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshVertex { X = value.X, Y = value.Y, Z = value.Z };
            return RadicleNative.PluginAddNavmeshVertex(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshVertex(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshVertex(plugin, key, at));
    }

    public bool AddNavmeshTriangle(FormKey record, NavmeshTriangle value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Vertices is not { Length: 3 } || value.Edges is not { Length: 3 })
            throw new ArgumentException("A triangle requires three vertices and edges.", nameof(value));
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshTriangle { Clearance = value.Clearance, Traversal = value.Traversal, Flags = value.Flags };
            for (var i = 0; i < 3; ++i) { native.Vertices[i] = value.Vertices[i]; native.Edges[i] = value.Edges[i]; }
            return RadicleNative.PluginAddNavmeshTriangle(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshTriangle(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshTriangle(plugin, key, at));
    }

    public bool AddNavmeshEdgeLink(FormKey record, NavmeshEdgeLink value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshEdgeLink { Type = value.Type, Navmesh = s.Key(value.Navmesh), Triangle = value.Triangle, Edge = value.Edge };
            return RadicleNative.PluginAddNavmeshEdgeLink(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshEdgeLink(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshEdgeLink(plugin, key, at));
    }

    public bool AddNavmeshDoor(FormKey record, NavmeshDoorLink value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshDoorLink { Triangle = value.Triangle, Crc = value.Crc, Door = s.Key(value.Door) };
            return RadicleNative.PluginAddNavmeshDoor(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshDoor(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshDoor(plugin, key, at));
    }

    public bool AddNavmeshCover(FormKey record, NavmeshCover value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.Vertices is not { Length: 2 } || value.Data is not { Length: 4 })
            throw new ArgumentException("A cover requires two vertices and four data bytes.", nameof(value));
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshCover();
            for (var i = 0; i < 2; ++i) native.Vertices[i] = value.Vertices[i];
            for (var i = 0; i < 4; ++i) native.Data[i] = value.Data[i];
            return RadicleNative.PluginAddNavmeshCover(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshCover(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshCover(plugin, key, at));
    }

    public bool AddNavmeshCoverMap(FormKey record, NavmeshCoverMap value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshCoverMap { Cover = value.Cover, Triangle = value.Triangle };
            return RadicleNative.PluginAddNavmeshCoverMap(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshCoverMap(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshCoverMap(plugin, key, at));
    }

    public bool AddNavmeshWaypoint(FormKey record, NavmeshWaypoint value)
    {
        ArgumentNullException.ThrowIfNull(value);
        ArgumentNullException.ThrowIfNull(value.Position);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NavmeshWaypoint { Triangle = value.Triangle, Flags = value.Flags };
            native.Position[0] = value.Position.X; native.Position[1] = value.Position.Y; native.Position[2] = value.Position.Z;
            return RadicleNative.PluginAddNavmeshWaypoint(plugin, key, &native);
        });
    }

    public bool RemoveNavmeshWaypoint(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNavmeshWaypoint(plugin, key, at));
    }

    public bool AddNaviInfo(FormKey record, NavmeshInfo value)
    {
        ArgumentNullException.ThrowIfNull(value);
        ArgumentNullException.ThrowIfNull(value.Location);
        ArgumentNullException.ThrowIfNull(value.Parent);
        return Edit(record, (plugin, s, key) =>
        {
            var edges = value.Edges.Select(e => s.Key(e)).ToArray();
            var preferred = value.PreferredEdges.Select(e => s.Key(e)).ToArray();
            var doors = value.Doors.Select(d => new Native.NaviDoorLink { Crc = d.Crc, Door = s.Key(d.Door) }).ToArray();
            var island = value.Island ?? Array.Empty<byte>();
            fixed (byte* islandBytes = island)
            {
                var native = new Native.NavmeshInfo
                {
                    Navmesh = s.Key(value.Navmesh), Flags = value.Flags,
                    Location = new Native.NavmeshVertex { X = value.Location.X, Y = value.Location.Y, Z = value.Location.Z },
                    Preferred = value.Preferred,
                    Edges = s.AddArray(edges), EdgeCount = (nuint)edges.Length,
                    PreferredEdges = s.AddArray(preferred), PreferredEdgeCount = (nuint)preferred.Length,
                    Doors = s.AddArray(doors), DoorCount = (nuint)doors.Length,
                    HasIsland = value.HasIsland ? 1 : 0, Island = (IntPtr)islandBytes, IslandSize = (nuint)island.Length,
                    Parent = new Native.NavmeshParent
                    {
                        Crc = value.Parent.Crc, World = s.Key(value.Parent.World), Cell = s.Key(value.Parent.Cell),
                        GridY = value.Parent.GridY, GridX = value.Parent.GridX,
                    },
                };
                return RadicleNative.PluginAddNaviInfo(plugin, key, &native);
            }
        });
    }

    public bool RemoveNaviInfo(FormKey record, int infoIndex)
    {
        var info = Index(infoIndex);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNaviInfo(plugin, key, info));
    }

    public bool AddNaviLink(FormKey record, int infoIndex, NaviLinkKind kind, FormKey target)
    {
        var info = Index(infoIndex);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginAddNaviLink(plugin, key, info, (Native.NaviLinkKind)kind, s.Key(target)));
    }

    public bool RemoveNaviLink(FormKey record, int infoIndex, NaviLinkKind kind, int index)
    {
        var info = Index(infoIndex);
        var at = Index(index);
        return Edit(record, (plugin, s, key) =>
            RadicleNative.PluginRemoveNaviLink(plugin, key, info, (Native.NaviLinkKind)kind, at));
    }

    public bool AddNaviDoor(FormKey record, int infoIndex, NaviDoorLink value)
    {
        ArgumentNullException.ThrowIfNull(value);
        var info = Index(infoIndex);
        return Edit(record, (plugin, s, key) =>
        {
            var native = new Native.NaviDoorLink { Crc = value.Crc, Door = s.Key(value.Door) };
            return RadicleNative.PluginAddNaviDoor(plugin, key, info, &native);
        });
    }

    public bool AddNaviDeleted(FormKey record, FormKey target) =>
        Edit(record, (plugin, s, key) => RadicleNative.PluginAddNaviDeleted(plugin, key, s.Key(target)));

    public bool RemoveNaviDeleted(FormKey record, int index)
    {
        var at = Index(index);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNaviDeleted(plugin, key, at));
    }

    public bool AddNaviPath(FormKey record, IReadOnlyList<FormKey> links)
    {
        ArgumentNullException.ThrowIfNull(links);
        return Edit(record, (plugin, s, key) =>
        {
            var native = links.Select(l => s.Key(l)).ToArray();
            fixed (Native.FormKey* p = native)
                return RadicleNative.PluginAddNaviPath(plugin, key, p, (nuint)native.Length);
        });
    }

    public bool RemoveNaviPath(FormKey record, int path)
    {
        var at = Index(path);
        return Edit(record, (plugin, s, key) => RadicleNative.PluginRemoveNaviPath(plugin, key, at));
    }
}
