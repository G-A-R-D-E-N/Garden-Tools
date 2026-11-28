using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum NavVerdict { Unknown = 0, Passable = 1, NeedsOpening = 2, Blocked = 3 }
public sealed record NavmeshWalkLink(NavmeshVertex Start, NavmeshVertex End, int SourceTriangle, int SourceEdge,
    FormKey? TargetNavmesh, int TargetTriangle, int TargetEdge, NavVerdict Verdict);
public sealed record NavmeshWalkDoor(FormKey? Door, int[] Triangles);
public sealed record NavmeshWalkView(FormKey? Navmesh, int? GridX, int? GridY, float MetresPerUnit, NavmeshVertex[] Vertices,
    int[] Triangles, uint[] TriangleClass, int SkippedTriangles, NavmeshWalkLink[] Links, NavmeshWalkDoor[] Doors);
public sealed record NavAgentProfile(float Radius, float Height, bool Large, uint ForbiddenClass);
public sealed record NavTeleportLeg(FormKey DestinationDoor, FormKey? DestinationCell, FormKey? Navmesh, short? Triangle,
    NavmeshVertex? Approach, NavVerdict Verdict);

public sealed unsafe partial class RadicleEnvironment
{
    public NavmeshWalkView? NavmeshWalk(Record record, float linkInsetMetres)
    {
        using var use = Use(record);
        Native.WalkView* v;
        Call.Check(RadicleNative.NavmeshWalkView(use.Pointer, record.Handle, linkInsetMetres, &v));
        try
        {
            if (v->HasGeometry == 0) return null;
            var doorTriangles = Call.Copy<uint>(v->DoorTriangles, v->DoorTriangleCount);
            return new(FormKey.FromNative(v->Navmesh), v->HasGrid != 0 ? (int?)v->GridX : null, v->HasGrid != 0 ? (int?)v->GridY : null,
                v->MetresPerUnit,
                Call.Copy<Native.NavmeshVertex>(v->Vertices, v->VertexCount).Select(p => new NavmeshVertex(p.X, p.Y, p.Z)).ToArray(),
                Call.Copy<uint>(v->Triangles, v->TriangleCount * 3).Select(i => (int)i).ToArray(),
                Call.Copy<uint>(v->TriangleClass, v->TriangleCount), (int)v->SkippedTriangleCount,
                Call.Copy<Native.WalkLink>(v->Links, v->LinkCount).Select(l => new NavmeshWalkLink(
                    new(l.Start.X, l.Start.Y, l.Start.Z), new(l.End.X, l.End.Y, l.End.Z), (int)l.SourceTriangle, (int)l.SourceEdge,
                    FormKey.FromNative(l.TargetNavmesh), (int)l.TargetTriangle, (int)l.TargetEdge, (NavVerdict)(int)l.Verdict)).ToArray(),
                Call.Copy<Native.WalkDoor>(v->Doors, v->DoorCount).Select(d => new NavmeshWalkDoor(FormKey.FromNative(d.Door),
                    doorTriangles.Skip((int)d.FirstTriangle).Take((int)d.TriangleCount).Select(i => (int)i).ToArray())).ToArray());
        }
        finally { RadicleNative.NavmeshWalkViewFree(v); }
    }

    public NavVerdict DoorPassable(FormKey? npcKey, FormKey doorReference, bool locked, bool npcHasKey)
    {
        using var use = Use();
        using var s = new NativeStrings();
        var state = new Native.DoorState { Locked = (byte)(locked ? 1 : 0), NpcHasKey = (byte)(npcHasKey ? 1 : 0) };
        Native.PathVerdict verdict;
        Call.Check(RadicleNative.DoorPassable(use.Pointer, s.Key(npcKey), s.Key(doorReference), &state, &verdict));
        return (NavVerdict)(int)verdict;
    }

    public NavVerdict EdgeLinkUsable(uint linkType, FormKey? npcKey)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Native.PathVerdict verdict;
        Call.Check(RadicleNative.NavmeshEdgeLinkUsable(use.Pointer, linkType, s.Key(npcKey), &verdict));
        return (NavVerdict)(int)verdict;
    }

    // Constraint: the reference must carry a teleport destination; Navmesh, Triangle and Approach are null when the native leg has none.
    public NavTeleportLeg DoorTeleportLeg(FormKey loadDoorReference, bool destinationLocked, bool npcHasKey)
    {
        using var use = Use();
        using var s = new NativeStrings();
        var state = new Native.DoorState { Locked = (byte)(destinationLocked ? 1 : 0), NpcHasKey = (byte)(npcHasKey ? 1 : 0) };
        Native.TeleportLeg leg;
        Call.Check(RadicleNative.DoorTeleportLeg(use.Pointer, s.Key(loadDoorReference), &state, &leg));
        return new(FormKey.Required(leg.DestinationDoor, "teleport destination door"), FormKey.FromNative(leg.DestinationCell),
            leg.HasNavmesh == 0 ? null : FormKey.FromNative(leg.Navmesh),
            leg.HasNavmesh == 0 ? null : leg.Triangle,
            leg.HasApproach == 0 ? null : new NavmeshVertex(leg.Approach.X, leg.Approach.Y, leg.Approach.Z),
            (NavVerdict)(int)leg.Verdict);
    }

    public NavAgentProfile WalkAgentProfile(FormKey? race)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Native.AgentProfile profile;
        Call.Check(RadicleNative.WalkAgentProfile(use.Pointer, s.Key(race), &profile));
        return new(profile.Radius, profile.Height, profile.Large != 0, profile.ForbiddenClass);
    }
}
