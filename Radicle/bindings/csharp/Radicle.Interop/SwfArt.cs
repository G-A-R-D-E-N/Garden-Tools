using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfGradientStopRecord(byte Ratio, uint Color);

public sealed record SwfFillStyleRecord(
    byte Kind,
    uint Color,
    ushort BitmapId,
    byte Spread,
    byte Interpolation,
    short? FocalPoint,
    SwfMatrixRecord? Matrix,
    IReadOnlyList<SwfGradientStopRecord> Stops);

public sealed record SwfLineStyleRecord(ushort Width, ushort Flags, uint Color, ushort? MiterLimit,
                                        SwfFillStyleRecord? Fill);

public enum SwfShapeCommandKind
{
    Move,
    StyleChange,
    StraightEdge,
    CurvedEdge,
}

public sealed record SwfShapeCommandRecord(
    SwfShapeCommandKind Kind,
    int X,
    int Y,
    int ControlX,
    int ControlY,
    ushort Fill0,
    ushort Fill1,
    ushort Line);

public sealed record SwfShapeRecord(
    int TagIndex,
    ushort CharacterId,
    byte Version,
    bool HasEdgeBounds,
    byte Flags,
    SwfRectRecord Bounds,
    SwfRectRecord EdgeBounds,
    uint EdgeCount,
    uint CurvedEdgeCount,
    uint StyleChangeCount,
    IReadOnlyList<SwfFillStyleRecord> Fills,
    IReadOnlyList<SwfLineStyleRecord> Lines,
    IReadOnlyList<SwfShapeCommandRecord> Commands);

public sealed record SwfBitmapRecord(int TagIndex, ushort CharacterId, byte Format, ushort Width, ushort Height,
                                     byte? ColorTableSize, long DataSize);

public sealed record SwfJpegRecord(int TagIndex, ushort CharacterId, ushort Deblock, long ImageSize, long? AlphaSize);

public sealed record SwfJpegTablesRecord(int TagIndex, long Size);

public sealed unsafe partial class SwfFile
{
    private static readonly ushort[] ShapeCodes = { 2, 22, 32, 83 };
    private static readonly ushort[] BitmapCodes = { 20, 36 };
    private static readonly ushort[] JpegCodes = { 6, 21, 35, 90 };

    public IReadOnlyList<SwfShapeRecord> Shapes { get; }

    public IReadOnlyList<SwfBitmapRecord> Bitmaps { get; }

    public IReadOnlyList<SwfJpegRecord> Jpegs { get; }

    public IReadOnlyList<SwfJpegTablesRecord> JpegTables { get; }

    private static IReadOnlyList<SwfShapeRecord> ReadShapes(SwfHandle handle, IReadOnlyList<SwfTagRecord> tags)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var shapes = new List<SwfShapeRecord>();
        foreach (var tag in tags)
        {
            if (Array.IndexOf(ShapeCodes, tag.Code) < 0)
            {
                continue;
            }
            SwfShape shape;
            Call.Check(RadicleNative.SwfShapeAt(use.Pointer, (nuint)tag.Index, &shape));
            shapes.Add(new SwfShapeRecord(
                tag.Index,
                shape.CharacterId,
                shape.Version,
                shape.HasEdgeBounds != 0,
                shape.Flags,
                ToRect(shape.Bounds),
                ToRect(shape.EdgeBounds),
                shape.EdgeCount,
                shape.CurvedEdgeCount,
                shape.StyleChangeCount,
                ReadShapeFills(use.Pointer, tag.Index, (int)shape.FillStyleCount),
                ReadShapeLines(use.Pointer, tag.Index, (int)shape.LineStyleCount),
                ReadShapeCommands(use.Pointer, tag.Index, (int)shape.RecordCount)));
        }
        return shapes;
    }

    private delegate Status RecordReader(IntPtr swf, nuint tagIndex, nuint record,
                                        Native.SwfShapeRecord* outRecord);

    private static IReadOnlyList<SwfShapeCommandRecord> ReadCommands(IntPtr swf, int tagIndex, int count,
                                                                     RecordReader reader)
    {
        var commands = new List<SwfShapeCommandRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            Native.SwfShapeRecord record;
            Call.Check(reader(swf, (nuint)tagIndex, (nuint)index, &record));
            commands.Add(new SwfShapeCommandRecord(ToCommandKind(record.Kind), record.X, record.Y, record.ControlX,
                                                   record.ControlY, record.Fill0, record.Fill1, record.Line));
        }
        return commands;
    }

    private static IReadOnlyList<SwfShapeCommandRecord> ReadShapeCommands(IntPtr swf, int tagIndex, int count) =>
        ReadCommands(swf, tagIndex, count, RadicleNative.SwfShapeRecordAt);

    private static SwfShapeCommandKind ToCommandKind(SwfRecordKind kind) => kind switch
    {
        SwfRecordKind.Move => SwfShapeCommandKind.Move,
        SwfRecordKind.StyleChange => SwfShapeCommandKind.StyleChange,
        SwfRecordKind.StraightEdge => SwfShapeCommandKind.StraightEdge,
        SwfRecordKind.CurvedEdge => SwfShapeCommandKind.CurvedEdge,
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "unknown shape record kind"),
    };

    private static IReadOnlyList<SwfFillStyleRecord> ReadShapeFills(IntPtr swf, int tagIndex, int count)
    {
        var fills = new List<SwfFillStyleRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfFillStyle fill;
            Call.Check(RadicleNative.SwfShapeFillAt(swf, (nuint)tagIndex, (nuint)index, &fill));
            fills.Add(ToFillStyle(fill, ReadShapeFillStops(swf, tagIndex, index, (int)fill.StopCount)));
        }
        return fills;
    }

    private static IReadOnlyList<SwfGradientStopRecord> ReadShapeFillStops(IntPtr swf, int tagIndex, int fillIndex,
                                                                          int count)
    {
        var stops = new List<SwfGradientStopRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfGradientStop stop;
            Call.Check(RadicleNative.SwfShapeFillStopAt(swf, (nuint)tagIndex, (nuint)fillIndex, (nuint)index, &stop));
            stops.Add(new SwfGradientStopRecord(stop.Ratio, stop.Color));
        }
        return stops;
    }

    private static IReadOnlyList<SwfGradientStopRecord> ReadShapeLineFillStops(IntPtr swf, int tagIndex, int lineIndex,
                                                                              int count)
    {
        var stops = new List<SwfGradientStopRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfGradientStop stop;
            Call.Check(RadicleNative.SwfShapeLineFillStopAt(swf, (nuint)tagIndex, (nuint)lineIndex, (nuint)index,
                                                           &stop));
            stops.Add(new SwfGradientStopRecord(stop.Ratio, stop.Color));
        }
        return stops;
    }

    private static SwfFillStyleRecord ToFillStyle(SwfFillStyle fill, IReadOnlyList<SwfGradientStopRecord> stops) =>
        new(fill.Kind,
            fill.Color,
            fill.BitmapId,
            fill.Spread,
            fill.Interpolation,
            fill.Kind == 0x13 ? fill.FocalPoint : null,
            ToMatrix(fill.HasMatrix, fill.Matrix),
            stops);

    private static IReadOnlyList<SwfLineStyleRecord> ReadShapeLines(IntPtr swf, int tagIndex, int count)
    {
        var lines = new List<SwfLineStyleRecord>(count);
        for (var index = 0; index < count; ++index)
        {
            SwfLineStyle line;
            Call.Check(RadicleNative.SwfShapeLineAt(swf, (nuint)tagIndex, (nuint)index, &line));
            SwfFillStyleRecord? fill = null;
            if (line.HasFill != 0)
            {
                SwfFillStyle nativeFill;
                Call.Check(RadicleNative.SwfShapeLineFillAt(swf, (nuint)tagIndex, (nuint)index, &nativeFill));
                fill = ToFillStyle(nativeFill,
                                   ReadShapeLineFillStops(swf, tagIndex, index, (int)nativeFill.StopCount));
            }
            lines.Add(new SwfLineStyleRecord(
                line.Width,
                line.Flags,
                line.Color,
                line.HasMiterLimit != 0 ? line.MiterLimit : null,
                fill));
        }
        return lines;
    }

    private static IReadOnlyList<SwfBitmapRecord> ReadBitmaps(SwfHandle handle, IReadOnlyList<SwfTagRecord> tags)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var bitmaps = new List<SwfBitmapRecord>();
        foreach (var tag in tags)
        {
            if (Array.IndexOf(BitmapCodes, tag.Code) < 0)
            {
                continue;
            }
            SwfBitmapInfo bitmap;
            Call.Check(RadicleNative.SwfBitmapAt(use.Pointer, (nuint)tag.Index, &bitmap));
            bitmaps.Add(new SwfBitmapRecord(
                tag.Index,
                bitmap.CharacterId,
                bitmap.Format,
                bitmap.Width,
                bitmap.Height,
                bitmap.HasColorTable != 0 ? bitmap.ColorTableSize : null,
                (long)bitmap.DataSize));
        }
        return bitmaps;
    }

    private static IReadOnlyList<SwfJpegRecord> ReadJpegs(SwfHandle handle, IReadOnlyList<SwfTagRecord> tags)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var jpegs = new List<SwfJpegRecord>();
        foreach (var tag in tags)
        {
            if (Array.IndexOf(JpegCodes, tag.Code) < 0)
            {
                continue;
            }
            SwfJpegInfo jpeg;
            Call.Check(RadicleNative.SwfJpegAt(use.Pointer, (nuint)tag.Index, &jpeg));
            jpegs.Add(new SwfJpegRecord(
                tag.Index,
                jpeg.CharacterId,
                jpeg.Deblock,
                (long)jpeg.ImageSize,
                jpeg.HasAlpha != 0 ? (long)jpeg.AlphaSize : null));
        }
        return jpegs;
    }

    private static IReadOnlyList<SwfJpegTablesRecord> ReadJpegTables(SwfHandle handle, IReadOnlyList<SwfTagRecord> tags)
    {
        using var use = new HandleUse(handle, nameof(SwfFile));
        var tables = new List<SwfJpegTablesRecord>();
        foreach (var tag in tags)
        {
            if (tag.Code != 8)
            {
                continue;
            }
            SwfJpegTablesInfo info;
            Call.Check(RadicleNative.SwfJpegTablesAt(use.Pointer, (nuint)tag.Index, &info));
            tables.Add(new SwfJpegTablesRecord(tag.Index, (long)info.Size));
        }
        return tables;
    }
}
