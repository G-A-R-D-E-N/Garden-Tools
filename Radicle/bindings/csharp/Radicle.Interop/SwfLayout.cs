using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record SwfLayoutTextRecord(
    ushort? FontId,
    double? FontHeight,
    uint? Color,
    byte? Align,
    double? LeftMargin,
    double? RightMargin,
    double? Indent,
    double? Leading,
    bool Multiline,
    bool WordWrap,
    bool AutoSize,
    bool Html,
    bool Border,
    string Variable,
    string Text,
    string? FontClass);

public sealed record SwfLayoutRowRecord(
    string Path,
    string ParentPath,
    ushort Depth,
    ushort CharacterId,
    SwfCharacterKind Kind,
    bool Visible,
    bool HasRect,
    SwfBoundsRecord Rect,
    SwfTransformRecord WorldTransform,
    ushort? ClipDepth,
    uint ClipFrameCount,
    uint ClipFirstFrame,
    uint ClipLastFrame,
    bool Scripted,
    bool HasAs3,
    bool LegacyTimelineScript,
    bool DuplicatePath,
    SwfLayoutTextRecord? Text);

public sealed record SwfLayoutFontRecord(
    ushort FontId,
    byte Version,
    string Name,
    bool? Bold,
    bool? Italic,
    byte? Language,
    uint? GlyphCount,
    bool HasOutlines,
    ushort? Ascent,
    ushort? Descent,
    short? Leading,
    uint? UnitsPerEm,
    IReadOnlyList<ushort>? Codes,
    string? FontName,
    string? FontCopyright,
    string? FontClass);

public sealed record SwfLayoutStateRecord(
    string Label,
    ushort TimelineId,
    uint Frame,
    IReadOnlyList<SwfLayoutRowRecord> Rows);

public sealed record SwfLayoutRecord(
    SwfBoundsRecord Stage,
    double FrameRate,
    bool Scripted,
    bool HasAs3,
    bool LegacyTimelineScript,
    IReadOnlyList<SwfLayoutFontRecord> Fonts,
    IReadOnlyList<SwfLayoutStateRecord> States)
{
    // Constraint: compact output has no line endings, so its bytes and hash match on every platform.
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = false,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    public string ToJson() => JsonSerializer.Serialize(this, JsonOptions);

    public string Sha256() => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(ToJson())));
}

public sealed partial class SwfFile
{
    private const byte ScriptedFlag = 1;
    private const byte DuplicatePathFlag = 2;
    private const byte HasAs3Flag = 4;
    private const byte LegacyMovesFlag = 8;

    public unsafe SwfLayoutRecord Layout()
    {
        using var use = Use();
        IntPtr handle;
        Call.Check(RadicleNative.SwfLayoutOpen(use.Pointer, &handle));
        try
        {
            return ReadLayout(handle);
        }
        finally
        {
            RadicleNative.SwfLayoutDestroy(handle);
        }
    }

    private static unsafe SwfLayoutRecord ReadLayout(IntPtr layout)
    {
        SwfLayoutInfo info = default;
        Call.Check(RadicleNative.SwfLayoutGetInfo(layout, &info));
        var fontCount = checked((int)info.FontCount);
        var fonts = new List<SwfLayoutFontRecord>(fontCount);
        for (var index = 0; index < fontCount; ++index)
        {
            SwfLayoutFont raw = default;
            Call.Check(RadicleNative.SwfLayoutFontAt(layout, (nuint)index, &raw));
            fonts.Add(ToLayoutFont(layout, index, raw));
        }
        var stateCount = checked((int)info.StateCount);
        var states = new List<SwfLayoutStateRecord>(stateCount);
        for (var index = 0; index < stateCount; ++index)
        {
            SwfLayoutState state = default;
            Call.Check(RadicleNative.SwfLayoutStateAt(layout, (nuint)index, &state));
            var rowCount = checked((int)state.RowCount);
            var firstRow = checked((int)state.FirstRow);
            var rows = new List<SwfLayoutRowRecord>(rowCount);
            for (var row = 0; row < rowCount; ++row)
            {
                SwfLayoutRow raw = default;
                Call.Check(RadicleNative.SwfLayoutRowAt(layout, (nuint)(firstRow + row), &raw));
                rows.Add(ToLayoutRow(raw));
            }
            states.Add(new SwfLayoutStateRecord(Utf8.Read(state.Label) ?? string.Empty, state.TimelineId,
                                                state.Frame, rows));
        }
        var stage = new SwfBoundsRecord(info.Stage.X, info.Stage.Y, info.Stage.Width, info.Stage.Height);
        return new SwfLayoutRecord(stage, info.FrameRate, info.Scripted != 0, info.HasAs3 != 0,
                                   info.LegacyMoves != 0, fonts, states);
    }

    private static SwfLayoutRowRecord ToLayoutRow(SwfLayoutRow raw)
    {
        SwfLayoutTextRecord? text = raw.HasText != 0 ? ToLayoutText(raw.Text) : null;
        var transform = raw.WorldTransform;
        return new SwfLayoutRowRecord(
            Utf8.Read(raw.Path) ?? string.Empty,
            Utf8.Read(raw.ParentPath) ?? string.Empty,
            raw.Depth,
            raw.CharacterId,
            (SwfCharacterKind)raw.Kind,
            raw.Visible != 0,
            raw.HasRect != 0,
            new SwfBoundsRecord(raw.Rect.X, raw.Rect.Y, raw.Rect.Width, raw.Rect.Height),
            new SwfTransformRecord(transform.ScaleX, transform.RotateSkew0, transform.RotateSkew1,
                                   transform.ScaleY, transform.TranslateX, transform.TranslateY),
            raw.HasClipDepth != 0 ? (ushort?)raw.ClipDepth : null,
            raw.ClipFrameCount,
            raw.ClipFirstFrame,
            raw.ClipLastFrame,
            (raw.Flags & ScriptedFlag) != 0,
            (raw.Flags & HasAs3Flag) != 0,
            (raw.Flags & LegacyMovesFlag) != 0,
            (raw.Flags & DuplicatePathFlag) != 0,
            text);
    }

    private static SwfLayoutFontRecord ToLayoutFont(IntPtr layout, int fontIndex, SwfLayoutFont raw) => new(
        raw.FontId,
        raw.Version,
        Utf8.Read(raw.Name) ?? string.Empty,
        raw.HasStyle != 0 ? (bool?)(raw.Bold != 0) : null,
        raw.HasStyle != 0 ? (bool?)(raw.Italic != 0) : null,
        raw.HasLanguage != 0 ? (byte?)raw.Language : null,
        raw.HasGlyphCount != 0 ? (uint?)raw.GlyphCount : null,
        raw.HasOutlines != 0,
        raw.HasMetrics != 0 ? (ushort?)raw.Ascent : null,
        raw.HasMetrics != 0 ? (ushort?)raw.Descent : null,
        raw.HasMetrics != 0 ? (short?)raw.Leading : null,
        raw.HasMetrics != 0 ? (uint?)raw.UnitsPerEm : null,
        raw.HasCodes != 0 ? ReadCodes(layout, fontIndex, checked((int)raw.CodeCount)) : null,
        raw.HasFontName != 0 ? Utf8.Read(raw.FontName) ?? string.Empty : null,
        raw.HasFontName != 0 ? Utf8.Read(raw.FontCopyright) ?? string.Empty : null,
        raw.HasFontClass != 0 ? Utf8.Read(raw.FontClass) ?? string.Empty : null);

    private static unsafe IReadOnlyList<ushort> ReadCodes(IntPtr layout, int fontIndex, int count)
    {
        var codes = new List<ushort>(count);
        for (var index = 0; index < count; ++index)
        {
            ushort code = 0;
            Call.Check(RadicleNative.SwfLayoutFontCodeAt(layout, (nuint)fontIndex, (nuint)index, &code));
            codes.Add(code);
        }
        return codes;
    }

    private static SwfLayoutTextRecord ToLayoutText(SwfLayoutText raw) => new(
        raw.HasFontId != 0 ? (ushort?)raw.FontId : null,
        raw.HasFontHeight != 0 ? (double?)raw.FontHeight : null,
        raw.HasColor != 0 ? (uint?)raw.Color : null,
        raw.HasLayout != 0 ? (byte?)raw.Align : null,
        raw.HasLayout != 0 ? (double?)raw.LeftMargin : null,
        raw.HasLayout != 0 ? (double?)raw.RightMargin : null,
        raw.HasLayout != 0 ? (double?)raw.Indent : null,
        raw.HasLayout != 0 ? (double?)raw.Leading : null,
        raw.Multiline != 0,
        raw.WordWrap != 0,
        raw.AutoSize != 0,
        raw.Html != 0,
        raw.Border != 0,
        Utf8.Read(raw.Variable) ?? string.Empty,
        Utf8.Read(raw.Text) ?? string.Empty,
        raw.HasFontClass != 0 ? Utf8.Read(raw.FontClass) ?? string.Empty : null);
}
