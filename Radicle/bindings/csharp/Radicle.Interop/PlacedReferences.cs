using System;
using System.Collections.Generic;
using System.Linq;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct Vector3(float X, float Y, float Z);

public sealed record LinkedReference(FormKey? Keyword, FormKey? Reference);

public sealed record ActivateParent(FormKey? Reference, float Delay);

public sealed record Portal(FormKey? Origin, FormKey? Destination);

public sealed record TopicReference(uint Type, FormKey? Topic, uint Subtype);

public sealed record Patrol(float IdleTime, FormKey? Idle, FormKey? Topic, IReadOnlyList<TopicReference> Topics);

public sealed record ScriptElement(FormKey? Object, short Alias, string? Text, int Integer, float Real, bool Boolean);

public sealed record ScriptStructMember(string Name, byte Type, byte Flags, ScriptElement Value);

public sealed record ScriptStruct(IReadOnlyList<ScriptStructMember> Members);

public sealed record ScriptProperty(string Name, byte Type, byte Flags, FormKey? Object, short? Alias, string? Text,
                                   int? Integer, float? Real, bool? Boolean, uint? ElementCount,
                                   IReadOnlyList<ScriptElement>? Elements = null,
                                   IReadOnlyList<ScriptStruct>? Structs = null);

public sealed record Script(string Name, byte Flags, IReadOnlyList<ScriptProperty> Properties);

public sealed record MapMarker(byte? Flags, NameKind NameKind, string? NameText, uint NameStringId, byte? Type);

public sealed record Teleport(FormKey? Door, Vector3 Position, Vector3 Rotation, uint Flags, FormKey? TransitionInterior);

public sealed record Primitive(Vector3 Bounds, (float R, float G, float B, float A) Color, uint Type);

public sealed record Spline(float Slack, float Thickness, Vector3 HalfExtents, bool? WindDetachedEnd);

public sealed record RoomPortal((float Width, float Height) Size, Vector3 Position, (float X, float Y, float Z, float W) Rotation);

public sealed record Room(byte LinkedRoomCount, byte Flags, FormKey? LightingTemplate, FormKey? ImageSpace);

public sealed record PlacedReference(
    string Signature, FormKey? Base, Vector3? Position, Vector3? Rotation, float? Scale, FormKey? Layer,
    FormKey? EncounterZone, FormKey? LocationReference, FormKey? AttachRef, IReadOnlyList<FormKey?> LocationRefTypes,
    FormKey? EnableParent, byte? EnableParentFlags, FormKey? Owner, bool? NoCrime, int? FactionRank,
    IReadOnlyList<LinkedReference> LinkedReferences, byte? ActivateParentFlags,
    IReadOnlyList<ActivateParent> ActivateParents, string? Comments, bool IgnoredBySandbox, bool OpenByDefault,
    MapMarker? MapMarker, Teleport? Teleport, Primitive? Primitive, Spline? Spline, IReadOnlyList<Portal> Portals,
    RoomPortal? RoomPortal, Room? Room, IReadOnlyList<FormKey?> LinkedRooms, IReadOnlyList<Patrol> Patrols,
    int? Count, int? LevelModifier, bool HasScripts, short ScriptVersion, short ScriptObjectFormat,
    bool ScriptsUnsupported, IReadOnlyList<Script> Scripts, FormKey? Emittance = null);

public sealed unsafe partial class RadicleEnvironment
{
    public PlacedReference PlacedReference(Record record)
    {
        using var use = Use(record);
        PlacedRef* r;
        Call.Check(RadicleNative.PlacedRefDecode(use.Pointer, record.Handle, &r));
        try
        {
            return Convert(r);
        }
        finally
        {
            RadicleNative.PlacedRefFree(r);
        }
    }

    private static Vector3 V3(float* v) => new(v[0], v[1], v[2]);

    private static FormKey?[] Keys(IntPtr items, nuint count) =>
        Call.Copy<Native.FormKey>(items, count).Select(FormKey.FromNative).ToArray();

    internal static ScriptStruct[] ScriptStructs(IntPtr structs, nuint count) =>
        Call.Copy<Native.ScriptStruct>(structs, count)
            .Select(s => new ScriptStruct(Call.Copy<Native.ScriptStructMember>(s.Members, s.MemberCount)
                .Select(m => new ScriptStructMember(Utf8.Read(m.Name) ?? string.Empty, m.Type, m.Flags,
                                                    new ScriptElement(FormKey.FromNative(m.Value.Object), m.Value.Alias,
                                                                      Utf8.Read(m.Value.Text), m.Value.Integer,
                                                                      m.Value.Real, m.Value.Boolean != 0)))
                .ToArray()))
            .ToArray();

    private static PlacedReference Convert(PlacedRef* r)
    {
        var linked = Call.Copy<LinkedRef>(r->LinkedRefs, r->LinkedRefCount)
            .Select(l => new LinkedReference(FormKey.FromNative(l.Keyword), FormKey.FromNative(l.Reference))).ToArray();
        var parents = Call.Copy<Native.ActivateParent>(r->ActivateParents, r->ActivateParentCount)
            .Select(p => new ActivateParent(FormKey.FromNative(p.Reference), p.Delay)).ToArray();
        var portals = Call.Copy<Native.Portal>(r->Portals, r->PortalCount)
            .Select(p => new Portal(FormKey.FromNative(p.Origin), FormKey.FromNative(p.Destination))).ToArray();
        var patrols = Call.Copy<Native.Patrol>(r->Patrols, r->PatrolCount)
            .Select(p => new Patrol(p.IdleTime, FormKey.FromNative(p.Idle), FormKey.FromNative(p.Topic),
                                    Call.Copy<TopicRef>(p.Topics, p.TopicCount)
                                        .Select(t => new TopicReference(t.Type, FormKey.FromNative(t.Topic), t.Subtype))
                                        .ToArray()))
            .ToArray();
        var scripts = Call.Copy<Native.Script>(r->Scripts, r->ScriptCount)
            .Select(s => new Script(Utf8.Read(s.Name) ?? string.Empty, s.Flags,
                                    Call.Copy<Native.ScriptProperty>(s.Properties, s.PropertyCount)
                                        .Select(p => new ScriptProperty(
                                            Utf8.Read(p.Name) ?? string.Empty, p.Type, p.Flags, FormKey.FromNative(p.Object),
                                            p.HasAlias != 0 ? p.Alias : null,
                                            p.Text == IntPtr.Zero ? null : System.Text.Encoding.UTF8.GetString(Utf8.Raw(p.Text, p.TextSize)),
                                            p.HasInteger != 0 ? p.Integer : null, p.HasReal != 0 ? p.Real : null,
                                            p.HasBoolean != 0 ? p.Boolean != 0 : null,
                                            p.HasElementCount != 0 ? p.ElementCount : null,
                                            p.HasElements != 0 ? Call.Copy<Native.ScriptElement>(p.Elements, p.ElementValueCount)
                                                .Select(e => new ScriptElement(FormKey.FromNative(e.Object), e.Alias, Utf8.Read(e.Text),
                                                                               e.Integer, e.Real, e.Boolean != 0))
                                                .ToArray() : null,
                                            p.HasStructs != 0 ? ScriptStructs(p.Structs, p.StructCount) : null))
                                        .ToArray()))
            .ToArray();

        MapMarker? marker = r->HasMapMarker == 0 ? null : new MapMarker(
            r->HasMapMarkerFlags != 0 ? r->MapMarkerFlags : null, r->MapMarkerNameKind, Utf8.Read(r->MapMarkerNameText),
            r->MapMarkerNameStringId, r->HasMapMarkerType != 0 ? r->MapMarkerType : null);
        Teleport? teleport = r->HasTeleport == 0 ? null : new Teleport(
            FormKey.FromNative(r->TeleportDoor), V3(r->TeleportPosition), V3(r->TeleportRotation), r->TeleportFlags,
            FormKey.FromNative(r->TeleportTransitionInterior));
        Primitive? primitive = r->HasPrimitive == 0 ? null : new Primitive(
            V3(r->PrimitiveBounds), (r->PrimitiveColor[0], r->PrimitiveColor[1], r->PrimitiveColor[2], r->PrimitiveColor[3]),
            r->PrimitiveType);
        Spline? spline = r->HasSpline == 0 ? null : new Spline(
            r->SplineSlack, r->SplineThickness, V3(r->SplineHalfExtents),
            r->HasSplineWindDetachedEnd != 0 ? r->SplineWindDetachedEnd != 0 : null);
        RoomPortal? roomPortal = r->HasRoomPortal == 0 ? null : new RoomPortal(
            (r->RoomPortalSize[0], r->RoomPortalSize[1]), V3(r->RoomPortalPosition),
            (r->RoomPortalRotation[0], r->RoomPortalRotation[1], r->RoomPortalRotation[2], r->RoomPortalRotation[3]));
        Room? room = r->HasRoom == 0 ? null : new Room(
            r->RoomLinkedRoomCount, r->RoomFlags, FormKey.FromNative(r->RoomLightingTemplate),
            FormKey.FromNative(r->RoomImageSpace));

        return new PlacedReference(
            Signature(r->Signature), FormKey.FromNative(r->Base),
            r->HasPlacement != 0 ? V3(r->Position) : null, r->HasPlacement != 0 ? V3(r->Rotation) : null,
            r->HasScale != 0 ? r->Scale : null, FormKey.FromNative(r->Layer), FormKey.FromNative(r->EncounterZone),
            FormKey.FromNative(r->LocationReference), FormKey.FromNative(r->AttachRef),
            Keys(r->LocationRefTypes, r->LocationRefTypeCount),
            r->HasEnableParent != 0 ? FormKey.FromNative(r->EnableParent) : null,
            r->HasEnableParentFlags != 0 ? r->EnableParentFlags : null,
            r->HasOwnership != 0 ? FormKey.FromNative(r->Owner) : null, r->HasNoCrime != 0 ? r->NoCrime != 0 : null,
            r->HasFactionRank != 0 ? r->FactionRank : null, linked,
            r->HasActivateParentFlags != 0 ? r->ActivateParentFlags : null, parents, Utf8.Read(r->Comments),
            r->IgnoredBySandbox != 0, r->OpenByDefault != 0, marker, teleport, primitive, spline, portals, roomPortal,
            room, Keys(r->LinkedRooms, r->LinkedRoomCount), patrols, r->HasCount != 0 ? r->Count : null,
            r->HasLevelModifier != 0 ? r->LevelModifier : null, r->HasScripts != 0, r->ScriptVersion,
            r->ScriptObjectFormat, r->ScriptsUnsupported != 0, scripts,
            r->HasEmittance != 0 ? FormKey.FromNative(r->Emittance) : null);
    }
}
