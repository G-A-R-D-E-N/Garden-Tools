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
public sealed record NaviDoorLink(uint Crc, FormKey? Door);
public sealed record NavmeshGeometry(uint Version, NavmeshParent Parent, IReadOnlyList<NavmeshVertex> Vertices,
    IReadOnlyList<NavmeshTriangle> Triangles, IReadOnlyList<NavmeshEdgeLink> Edges, IReadOnlyList<NavmeshDoorLink> Doors, byte[] OpaqueTail);
public sealed record NavmeshInfo(FormKey? Navmesh, uint Flags, NavmeshVertex Location, float Preferred,
    IReadOnlyList<FormKey?> Edges, IReadOnlyList<FormKey?> PreferredEdges, IReadOnlyList<NaviDoorLink> Doors,
    bool HasIsland, byte[] Island, NavmeshParent Parent);
public sealed record NaviAnchor(FormKey? Navmesh, uint Value);
public sealed record NavmeshPathData(IReadOnlyList<IReadOnlyList<FormKey?>> Paths, IReadOnlyList<NaviAnchor> Anchors);

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
}
