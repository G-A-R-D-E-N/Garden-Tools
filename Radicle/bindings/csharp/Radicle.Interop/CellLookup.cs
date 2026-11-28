using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public readonly record struct ExteriorLayout(int CellX, int CellY, int BlockX, int BlockY, int SubblockX, int SubblockY);

public sealed record CellScolPart(string? ModelPath, IReadOnlyList<StaticPlacement> Placements);

public sealed record CellBaseObject(
    FormKey Key,
    string Signature,
    string? EditorId,
    string? TypeName,
    string? ModelPath,
    bool EditorMarker,
    bool HasDecal,
    string? DecalDiffuse,
    float? DecalWidth,
    float? DecalHeight,
    IReadOnlyList<CellScolPart> ScolParts);

public sealed record CellViewHeader(
    FormKey Key,
    string? EditorId,
    NameKind NameKind,
    string? NameText,
    uint NameStringId,
    bool Interior,
    (int X, int Y)? Grid,
    FormKey? Worldspace,
    int WinningPlugin,
    bool Localized);

public sealed record CellViewEntry(
    FormKey Key,
    string? RecordType,
    string? EditorId,
    FormKey? Base,
    CellBaseObject? BaseInfo,
    (float X, float Y, float Z) Position,
    (float X, float Y, float Z) Rotation,
    float Scale,
    bool Persistent,
    bool InitiallyDisabled,
    int WinningPlugin);

public sealed record CellViewData(CellViewHeader Header, IReadOnlyList<CellViewEntry> References);

public sealed unsafe partial class RadicleEnvironment
{
    // Constraint: the worldspace plugin must be loaded; a coordinate outside -110..110 is InvalidArgument and an absent cell is null.
    public Record? FindExteriorCell(FormKey worldspace, int x, int y)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        IntPtr record;
        return Call.Found(RadicleNative.EnvFindExteriorCell(use.Pointer, strings.Key(worldspace), x, y, &record)) ? new Record(record, this) : null;
    }

    public Record? FindPersistentExteriorCell(FormKey worldspace)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        IntPtr record;
        return Call.Found(RadicleNative.EnvFindPersistentExteriorCell(use.Pointer, strings.Key(worldspace), &record)) ? new Record(record, this) : null;
    }

    // Constraint: id is an editor ID or a "LOCALID:Plugin" key text; null means no cell matches.
    public FormKey? ResolveCell(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        using var use = Use();
        Native.FormKey key;
        return Call.Found(RadicleNative.EnvResolveCell(use.Pointer, id, &key)) ? FormKey.Required(key, "resolved cell") : null;
    }

    public FormKey? ResolveWorldspace(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        using var use = Use();
        Native.FormKey key;
        return Call.Found(RadicleNative.EnvResolveWorldspace(use.Pointer, id, &key)) ? FormKey.Required(key, "resolved worldspace") : null;
    }

    public FormKey? FindCellAtGrid(FormKey worldspace, int x, int y)
    {
        using var use = Use();
        using var strings = new NativeStrings();
        Native.FormKey key;
        return Call.Found(RadicleNative.EnvFindCellAtGrid(use.Pointer, strings.Key(worldspace), x, y, &key)) ? FormKey.Required(key, "cell at grid") : null;
    }

    // Constraint: a coordinate outside the exterior grid is InvalidArgument.
    public static ExteriorLayout ExteriorCellLayoutFor(int x, int y)
    {
        ExteriorCellLayout layout;
        Call.Check(RadicleNative.ExteriorCellLayoutFor(x, y, &layout));
        return new ExteriorLayout(layout.CellX, layout.CellY, layout.BlockX, layout.BlockY, layout.SubblockX, layout.SubblockY);
    }

    public static bool ExteriorCellCoordinateValid(int coordinate)
    {
        int valid;
        Call.Check(RadicleNative.ExteriorCellCoordinateValid(coordinate, &valid));
        return valid != 0;
    }

    // Invariant: the native view is copied into managed records and freed before return.
    public CellViewData CellView(FormKey cell)
    {
        using var use = Use();
        using var s = new NativeStrings();
        IntPtr view;
        Call.Check(RadicleNative.EnvCellView(use.Pointer, s.Key(cell), &view));
        try
        {
            Native.CellSummary summary;
            Call.Check(RadicleNative.CellViewSummary(view, &summary));
            var references = new CellViewEntry[checked((int)RadicleNative.CellViewReferenceCount(view))];
            for (var i = 0; i < references.Length; ++i)
            {
                Native.CellReference reference;
                Call.Check(RadicleNative.CellViewReference(view, (nuint)i, &reference));
                CellBaseObject? baseInfo = null;
                if (reference.BaseInfo != IntPtr.Zero)
                {
                    var info = (Native.BaseObjectInfo*)reference.BaseInfo;
                    var parts = new CellScolPart[checked((int)info->ScolPartCount)];
                    for (var p = 0; p < parts.Length; ++p)
                    {
                        Native.ScolPart part;
                        Call.Check(RadicleNative.CellViewScolPart(view, (nuint)i, (nuint)p, &part));
                        parts[p] = ToScolPart(part);
                    }
                    baseInfo = ToBaseObject(info, parts);
                }
                references[i] = new CellViewEntry(
                    FormKey.Required(reference.Key, "cell reference"),
                    Utf8.Read(reference.RecordType),
                    Utf8.Read(reference.EditorId),
                    reference.HasBase != 0 ? FormKey.FromNative(reference.Base) : null,
                    baseInfo,
                    (reference.Position[0], reference.Position[1], reference.Position[2]),
                    (reference.Rotation[0], reference.Rotation[1], reference.Rotation[2]),
                    reference.Scale,
                    reference.Persistent != 0,
                    reference.InitiallyDisabled != 0,
                    checked((int)reference.WinningPlugin));
            }
            return new CellViewData(
                new CellViewHeader(
                    FormKey.Required(summary.Key, "cell"),
                    Utf8.Read(summary.EditorId),
                    summary.NameKind,
                    Utf8.Read(summary.NameText),
                    summary.NameStringId,
                    summary.Interior != 0,
                    summary.HasGrid != 0 ? (summary.GridX, summary.GridY) : null,
                    summary.HasWorldspace != 0 ? FormKey.FromNative(summary.Worldspace) : null,
                    checked((int)summary.WinningPlugin),
                    summary.Localized != 0),
                references);
        }
        finally { RadicleNative.CellViewFree(view); }
    }

    public CellBaseObject BaseObject(FormKey baseForm)
    {
        using var use = Use();
        using var s = new NativeStrings();
        Native.BaseObjectInfo* info;
        Call.Check(RadicleNative.EnvBaseObjectInfo(use.Pointer, s.Key(baseForm), &info));
        try
        {
            var parts = Call.Copy<Native.ScolPart>(info->ScolParts, info->ScolPartCount).Select(ToScolPart).ToArray();
            return ToBaseObject(info, parts);
        }
        finally { RadicleNative.BaseObjectInfoFree(info); }
    }

    private static CellScolPart ToScolPart(Native.ScolPart part) =>
        new(Utf8.Read(part.ModelPath),
            Call.Copy<Native.StaticPlacement>(part.Placements, part.PlacementCount)
                .Select(p => new StaticPlacement((p.Position[0], p.Position[1], p.Position[2]),
                                                 (p.Rotation[0], p.Rotation[1], p.Rotation[2]), p.Scale))
                .ToArray());

    private static CellBaseObject ToBaseObject(Native.BaseObjectInfo* info, CellScolPart[] parts) =>
        new(FormKey.Required(info->Key, "base object"),
            Encoding.Latin1.GetString(info->Signature, 4),
            Utf8.Read(info->EditorId),
            Utf8.Read(info->TypeName),
            Utf8.Read(info->ModelPath),
            info->EditorMarker != 0,
            info->HasDecal != 0,
            Utf8.Read(info->DecalDiffuse),
            info->HasDecalWidth != 0 ? info->DecalWidth : null,
            info->HasDecalHeight != 0 ? info->DecalHeight : null,
            parts);
}
