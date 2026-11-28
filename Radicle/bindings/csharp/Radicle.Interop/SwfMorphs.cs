using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfMatrixRecord(int ScaleX, int ScaleY, int RotateSkew0, int RotateSkew1, int TranslateX,
                                     int TranslateY);

public sealed record SwfMorphGradientStopRecord(byte StartRatio, byte EndRatio, uint StartColor, uint EndColor);

public sealed record SwfMorphFillStyleRecord(
    byte Kind,
    uint StartColor,
    uint EndColor,
    ushort BitmapId,
    byte Spread,
    byte Interpolation,
    short? StartFocalPoint,
    short? EndFocalPoint,
    SwfMatrixRecord? StartMatrix,
    SwfMatrixRecord? EndMatrix,
    IReadOnlyList<SwfMorphGradientStopRecord> Stops);

public sealed record SwfMorphLineStyleRecord(
    ushort StartWidth,
    ushort EndWidth,
    ushort Flags,
    uint StartColor,
    uint EndColor,
    ushort? StartMiterLimit,
    ushort? EndMiterLimit,
    SwfMorphFillStyleRecord? Fill);

public sealed record SwfMorphShapeRecord(
    int TagIndex,
    ushort CharacterId,
    byte Version,
    bool HasEdgeBounds,
    byte Flags,
    SwfRectRecord StartBounds,
    SwfRectRecord EndBounds,
    SwfRectRecord StartEdgeBounds,
    SwfRectRecord EndEdgeBounds,
    uint StartEdgeCount,
    uint EndEdgeCount,
    IReadOnlyList<SwfMorphFillStyleRecord> Fills,
    IReadOnlyList<SwfMorphLineStyleRecord> Lines,
    IReadOnlyList<SwfShapeCommandRecord> StartCommands,
    IReadOnlyList<SwfShapeCommandRecord> EndCommands);

public sealed unsafe partial class SwfFile
{
    private const ushort DefineMorphShapeCode = 46;
    private const ushort DefineMorphShape2Code = 84;

    public IReadOnlyList<SwfMorphShapeRecord> Morphs { get; }

    public IReadOnlyList<SwfShapeCommandRecord> MorphCommandsAt(int tagIndex, double ratio)
    {
        using var use = Use();
        nuint count = 0;
        Call.Check(RadicleNative.SwfMorphFrameCount(use.Pointer, (nuint)tagIndex, ratio, &count));
        var commands = new List<SwfShapeCommandRecord>((int)count);
        for (var index = 0; index < (int)count; ++index)
        {
            Native.SwfShapeRecord record;
            Call.Check(RadicleNative.SwfMorphFrameRecordAt(use.Pointer, (nuint)tagIndex, ratio, (nuint)index,
                                                          &record));
            commands.Add(new SwfShapeCommandRecord(ToCommandKind(record.Kind), record.X, record.Y, record.ControlX,
                                                   record.ControlY, record.Fill0, record.Fill1, record.Line));
        }
        return commands;
    }

    private static IReadOnlyList<SwfMorphShapeRecord> ReadMorphs(SwfHandle handle, IReadOnlyList<SwfTagRecord> tags)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var morphs = new List<SwfMorphShapeRecord>();
        foreach (var tag in tags)
        {
            if (tag.Code != DefineMorphShapeCode && tag.Code != DefineMorphShape2Code)
            {
                continue;
            }
            SwfMorphShape morph;
            Call.Check(RadicleNative.SwfMorphAt(use.Pointer, (nuint)tag.Index, &morph));
            morphs.Add(new SwfMorphShapeRecord(
                tag.Index,
                morph.CharacterId,
                morph.Version,
                morph.HasEdgeBounds != 0,
                morph.Flags,
                ToRect(morph.StartBounds),
                ToRect(morph.EndBounds),
                ToRect(morph.StartEdgeBounds),
                ToRect(morph.EndEdgeBounds),
                morph.StartEdgeCount,
                morph.EndEdgeCount,
                ReadFillStyles(use.Pointer, tag.Index, morph.FillStyleCount),
                ReadLineStyles(use.Pointer, tag.Index, morph.LineStyleCount),
                ReadCommands(use.Pointer, tag.Index, (int)morph.StartRecordCount,
                             RadicleNative.SwfMorphStartRecordAt),
                ReadCommands(use.Pointer, tag.Index, (int)morph.EndRecordCount,
                             RadicleNative.SwfMorphEndRecordAt)));
        }
        return morphs;
    }

    private static IReadOnlyList<SwfMorphFillStyleRecord> ReadFillStyles(IntPtr swf, int tagIndex, uint count)
    {
        var fills = new List<SwfMorphFillStyleRecord>((int)count);
        for (var index = 0; index < (int)count; ++index)
        {
            SwfMorphFillStyle fill;
            Call.Check(RadicleNative.SwfMorphFillAt(swf, (nuint)tagIndex, (nuint)index, &fill));
            fills.Add(ToFillStyle(fill, ReadFillStops(swf, tagIndex, index, (int)fill.StopCount)));
        }
        return fills;
    }

    private static IReadOnlyList<SwfMorphLineStyleRecord> ReadLineStyles(IntPtr swf, int tagIndex, uint count)
    {
        var lines = new List<SwfMorphLineStyleRecord>((int)count);
        for (var index = 0; index < (int)count; ++index)
        {
            SwfMorphLineStyle line;
            Call.Check(RadicleNative.SwfMorphLineAt(swf, (nuint)tagIndex, (nuint)index, &line));
            SwfMorphFillStyleRecord? fill = null;
            if (line.HasFill != 0)
            {
                SwfMorphFillStyle nativeFill;
                Call.Check(RadicleNative.SwfMorphLineFillAt(swf, (nuint)tagIndex, (nuint)index, &nativeFill));
                fill = ToFillStyle(nativeFill,
                                   ReadLineFillStops(swf, tagIndex, index, (int)nativeFill.StopCount));
            }
            lines.Add(new SwfMorphLineStyleRecord(
                line.StartWidth,
                line.EndWidth,
                line.Flags,
                line.StartColor,
                line.EndColor,
                line.HasStartMiter != 0 ? line.StartMiter : null,
                line.HasEndMiter != 0 ? line.EndMiter : null,
                fill));
        }
        return lines;
    }

    private static IReadOnlyList<SwfMorphGradientStopRecord> ReadFillStops(IntPtr swf, int tagIndex, int fillIndex,
                                                                          int count)
    {
        var stops = new List<SwfMorphGradientStopRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfMorphGradientStop stop;
            Call.Check(RadicleNative.SwfMorphFillStopAt(swf, (nuint)tagIndex, (nuint)fillIndex, (nuint)index, &stop));
            stops.Add(ToStop(stop));
        }
        return stops;
    }

    private static IReadOnlyList<SwfMorphGradientStopRecord> ReadLineFillStops(IntPtr swf, int tagIndex, int lineIndex,
                                                                              int count)
    {
        var stops = new List<SwfMorphGradientStopRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfMorphGradientStop stop;
            Call.Check(RadicleNative.SwfMorphLineFillStopAt(swf, (nuint)tagIndex, (nuint)lineIndex, (nuint)index,
                                                           &stop));
            stops.Add(ToStop(stop));
        }
        return stops;
    }

    private static SwfMorphFillStyleRecord ToFillStyle(SwfMorphFillStyle fill,
                                                       IReadOnlyList<SwfMorphGradientStopRecord> stops) =>
        new(fill.Kind,
            fill.StartColor,
            fill.EndColor,
            fill.BitmapId,
            fill.Spread,
            fill.Interpolation,
            fill.HasStartFocal != 0 ? fill.StartFocal : null,
            fill.HasEndFocal != 0 ? fill.EndFocal : null,
            ToMatrix(fill.HasStartMatrix, fill.StartMatrix),
            ToMatrix(fill.HasEndMatrix, fill.EndMatrix),
            stops);

    private static SwfMatrixRecord? ToMatrix(int present, int* values) =>
        present == 0
            ? null
            : new SwfMatrixRecord(values[0], values[1], values[2], values[3], values[4], values[5]);

    private static SwfRectRecord ToRect(in SwfRect rect) => new(rect.XMin, rect.XMax, rect.YMin, rect.YMax);

    private static SwfMorphGradientStopRecord ToStop(in SwfMorphGradientStop stop) =>
        new(stop.StartRatio, stop.EndRatio, stop.StartColor, stop.EndColor);
}
