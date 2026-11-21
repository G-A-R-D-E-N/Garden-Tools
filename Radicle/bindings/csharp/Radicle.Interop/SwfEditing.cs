using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.Win32.SafeHandles;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public enum SwfCharacterKind
{
    Shape = 0,
    Morph = 1,
    Bitmap = 2,
    Text = 3,
    EditText = 4,
    Font = 5,
    Sprite = 6,
    Button = 7,
    Sound = 8,
    Video = 9,
    Imported = 10,
}

public readonly record struct SwfRectRecord(int XMin, int XMax, int YMin, int YMax);

public sealed record SwfCharacterRecord(
    ushort CharacterId,
    SwfCharacterKind Kind,
    int TagIndex,
    bool HasBounds,
    SwfRectRecord Bounds,
    bool HasBitmapSize,
    ushort BitmapWidth,
    ushort BitmapHeight,
    string? Name,
    string? ImportedFrom,
    string? ImportedName);

public sealed record SwfLiveCounts(int Tags, int Symbols, int Exports, int FrameLabels, int AbcBlocks);

public sealed record SwfFontGlyphRecord(byte FillBits, byte LineBits, IReadOnlyList<SwfShapeCommandRecord> Commands);

public sealed record SwfSoundRecord(
    ushort SoundId,
    byte Format,
    byte Rate,
    bool Sample16Bit,
    bool Stereo,
    uint SampleCount,
    long DataSize);

public sealed record SwfStartSoundRecord(
    ushort SoundId,
    bool SyncStop,
    bool SyncNoMultiple,
    uint? InPoint,
    uint? OutPoint,
    ushort? LoopCount,
    int EnvelopeCount);

public readonly record struct SwfTransformRecord(
    double ScaleX,
    double RotateSkew0,
    double RotateSkew1,
    double ScaleY,
    double TranslateX,
    double TranslateY);

public readonly record struct SwfBoundsRecord(double X, double Y, double Width, double Height);

// Constraint: both indices must be flat radicle_swf_tag_at indices, and TransformTagIndex must be uint.MaxValue when no placement authored the MATRIX.
public sealed record SwfFrameInstanceRecord(
    uint InstanceId,
    uint TimelineFrame,
    ushort Depth,
    ushort CharacterId,
    ushort TimelineId,
    ushort ClipDepth,
    bool IsSprite,
    bool HasBounds,
    bool HasClipDepth,
    SwfTransformRecord Transform,
    SwfTransformRecord WorldTransform,
    SwfBoundsRecord Bounds,
    string? Name,
    string? Path,
    uint CreateTagIndex,
    uint TransformTagIndex);

public sealed record SwfFrameEffectsRecord(
    bool HasColorTransform,
    bool HasAlpha,
    bool HasBlendMode,
    byte BlendMode,
    bool WorldVisible,
    int FilterCount,
    IReadOnlyList<int> ColorMultiply,
    IReadOnlyList<int> ColorAdd);

public sealed record SwfFrameFilterRecord(
    byte Id,
    bool Inner,
    bool Knockout,
    byte Passes,
    uint Color,
    uint HighlightColor,
    double BlurX,
    double BlurY,
    double Angle,
    double Distance,
    double Strength);

public sealed record SwfPlanResultRecord(bool Changed, ulong Revision);

// Constraint: a plan carries the flat radicle_swf_tag_at indices, exactly like the native plan.
public sealed class SwfPlanSpec
{
    public List<(int TagIndex, byte[] Payload)> Replacements { get; } = new();

    public List<(int BeforeTagIndex, ushort Code, byte[] Payload)> Insertions { get; } = new();

    public List<int> Removals { get; } = new();

    public SwfPlanSpec Replace(int tagIndex, ReadOnlySpan<byte> payload)
    {
        Replacements.Add((tagIndex, payload.ToArray()));
        return this;
    }

    public SwfPlanSpec Insert(int beforeTagIndex, ushort code, ReadOnlySpan<byte> payload)
    {
        Insertions.Add((beforeTagIndex, code, payload.ToArray()));
        return this;
    }
    public SwfPlanSpec Remove(int tagIndex)
    {
        Removals.Add(tagIndex);
        return this;
    }
}

internal sealed class SwfFramesHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public SwfFramesHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.SwfFramesDestroy(handle);
        return true;
    }
}

internal sealed class SwfHistoryHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public SwfHistoryHandle(IntPtr handle) : base(ownsHandle: true) => SetHandle(handle);

    protected override bool ReleaseHandle()
    {
        RadicleNative.SwfHistoryDestroy(handle);
        return true;
    }
}

public sealed class SwfFrameSet : IDisposable
{
    private readonly SwfFramesHandle _handle;

    private SwfFrameSet(IntPtr handle) => _handle = new SwfFramesHandle(handle);

    internal static unsafe SwfFrameSet Open(SwfFile file)
    {
        IntPtr handle;
        using var use = file.UseForEditor();
        Call.Check(RadicleNative.SwfFramesOpen(use.Pointer, &handle));
        return new SwfFrameSet(handle);
    }

    public int TimelineCount
    {
        get
        {
            using var use = new HandleUse(_handle, nameof(SwfFrameSet));
            return checked((int)RadicleNative.SwfFramesTimelineCount(use.Pointer));
        }
    }

    public unsafe (ushort CharacterId, int FrameCount) TimelineAt(int index)
    {
        using var use = new HandleUse(_handle, nameof(SwfFrameSet));
        ushort characterId = 0;
        nuint frameCount = 0;
        Call.Check(RadicleNative.SwfFramesTimelineAt(use.Pointer, (nuint)index, &characterId, &frameCount));
        return (characterId, checked((int)frameCount));
    }

    public unsafe IReadOnlyList<SwfFrameInstanceRecord> Instances(int timeline, int frame)
    {
        using var use = new HandleUse(_handle, nameof(SwfFrameSet));
        nuint count = 0;
        Call.Check(RadicleNative.SwfFramesInstanceCount(use.Pointer, (nuint)timeline, (nuint)frame, &count));
        var instances = new SwfFrameInstanceRecord[checked((int)count)];
        for (var index = 0; index < instances.Length; ++index)
        {
            SwfFramesInstance raw = default;
            Call.Check(RadicleNative.SwfFramesInstanceAt(use.Pointer, (nuint)timeline, (nuint)frame,
                                                         (nuint)index, &raw));
            instances[index] = new SwfFrameInstanceRecord(
                raw.InstanceId, raw.TimelineFrame, raw.Depth, raw.CharacterId, raw.TimelineId, raw.ClipDepth,
                raw.IsSprite != 0, raw.HasBounds != 0, raw.HasClipDepth != 0,
                Transform(raw.Transform), Transform(raw.WorldTransform),
                new SwfBoundsRecord(raw.Bounds.X, raw.Bounds.Y, raw.Bounds.Width, raw.Bounds.Height),
                Utf8.Read(raw.Name), Utf8.Read(raw.Path), raw.CreateTagIndex, raw.TransformTagIndex);
        }
        return instances;
    }

    // Constraint: the indices are those of Instances; an index past the display list is InvalidArgument.
    public unsafe SwfFrameEffectsRecord InstanceEffects(int timeline, int frame, int index)
    {
        using var use = new HandleUse(_handle, nameof(SwfFrameSet));
        SwfFramesEffects raw = default;
        Call.Check(RadicleNative.SwfFramesInstanceEffects(use.Pointer, (nuint)timeline, (nuint)frame, (nuint)index, &raw));
        return new SwfFrameEffectsRecord(
            raw.HasColorTransform != 0, raw.HasAlpha != 0, raw.HasBlendMode != 0, raw.BlendMode, raw.WorldVisible != 0,
            checked((int)raw.FilterCount),
            new[] { raw.ColorMultiply[0], raw.ColorMultiply[1], raw.ColorMultiply[2], raw.ColorMultiply[3] },
            new[] { raw.ColorAdd[0], raw.ColorAdd[1], raw.ColorAdd[2], raw.ColorAdd[3] });
    }

    // Constraint: filter must be below the FilterCount that InstanceEffects reports for the same instance.
    public unsafe SwfFrameFilterRecord InstanceFilter(int timeline, int frame, int index, int filter)
    {
        using var use = new HandleUse(_handle, nameof(SwfFrameSet));
        SwfFramesFilter raw = default;
        Call.Check(RadicleNative.SwfFramesInstanceFilterAt(use.Pointer, (nuint)timeline, (nuint)frame, (nuint)index, (nuint)filter, &raw));
        return new SwfFrameFilterRecord(
            raw.Id, raw.Inner != 0, raw.Knockout != 0, raw.Passes, raw.Color, raw.HighlightColor,
            raw.BlurX, raw.BlurY, raw.Angle, raw.Distance, raw.Strength);
    }

    private static SwfTransformRecord Transform(SwfTransform value) =>
        new(value.ScaleX, value.RotateSkew0, value.RotateSkew1, value.ScaleY, value.TranslateX, value.TranslateY);

    public void Dispose() => _handle.Dispose();
}

public sealed class SwfEditHistory : IDisposable
{
    private readonly SwfHistoryHandle _handle;

    private SwfEditHistory(IntPtr handle) => _handle = new SwfHistoryHandle(handle);

    internal static unsafe SwfEditHistory Create(SwfFile file)
    {
        IntPtr handle;
        using var use = file.UseForEditor();
        Call.Check(RadicleNative.SwfHistoryCreate(use.Pointer, &handle));
        return new SwfEditHistory(handle);
    }

    private HandleUse Use() => new(_handle, nameof(SwfEditHistory));

    internal HandleUse UseForVerbs() => Use();

    public ulong Revision
    {
        get
        {
            using var use = Use();
            return RadicleNative.SwfHistoryRevision(use.Pointer);
        }
    }

    public unsafe SwfPlanResultRecord Apply(SwfPlanSpec plan)
    {
        using var use = Use();
        using var native = SwfNativePlan.Create(plan);
        SwfTagPlan value = native.Value;
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfHistoryApply(use.Pointer, &value, &result));
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    public unsafe SwfPlanResultRecord Undo()
    {
        using var use = Use();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfHistoryUndo(use.Pointer, &result));
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    public unsafe SwfPlanResultRecord Redo()
    {
        using var use = Use();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfHistoryRedo(use.Pointer, &result));
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    public unsafe int UndoCount
    {
        get
        {
            using var use = Use();
            nuint count = 0;
            Call.Check(RadicleNative.SwfHistoryUndoCount(use.Pointer, &count));
            return checked((int)count);
        }
    }

    public unsafe int RedoCount
    {
        get
        {
            using var use = Use();
            nuint count = 0;
            Call.Check(RadicleNative.SwfHistoryRedoCount(use.Pointer, &count));
            return checked((int)count);
        }
    }

    public unsafe byte[] WriteBytes()
    {
        using var use = Use();
        byte* bytes = null;
        nuint size = 0;
        Call.Check(RadicleNative.SwfHistoryWriteBytes(use.Pointer, &bytes, &size));
        return Call.TakeBytes(bytes, size);
    }

    public void Dispose() => _handle.Dispose();
}

internal sealed unsafe class SwfNativePlan : IDisposable
{
    private readonly List<IntPtr> _payloads;
    private readonly IntPtr _replacements;
    private readonly IntPtr _insertions;
    private readonly IntPtr _removals;

    private SwfNativePlan(IntPtr replacements, IntPtr insertions, IntPtr removals, List<IntPtr> payloads)
    {
        _replacements = replacements;
        _insertions = insertions;
        _removals = removals;
        _payloads = payloads;
    }

    public SwfTagPlan Value;

    public static SwfNativePlan Create(SwfPlanSpec spec)
    {
        var payloads = new List<IntPtr>();
        var replacements = IntPtr.Zero;
        var insertions = IntPtr.Zero;
        var removals = IntPtr.Zero;
        try
        {
            foreach (var (_, payload) in spec.Replacements) payloads.Add(Copy(payload));
            foreach (var (_, _, payload) in spec.Insertions) payloads.Add(Copy(payload));
            replacements = spec.Replacements.Count == 0
                ? IntPtr.Zero
                : System.Runtime.InteropServices.Marshal.AllocHGlobal(
                    checked(spec.Replacements.Count * sizeof(SwfTagReplacement)));
            insertions = spec.Insertions.Count == 0
                ? IntPtr.Zero
                : System.Runtime.InteropServices.Marshal.AllocHGlobal(
                    checked(spec.Insertions.Count * sizeof(SwfTagInsertion)));
            removals = spec.Removals.Count == 0
                ? IntPtr.Zero
                : System.Runtime.InteropServices.Marshal.AllocHGlobal(
                    checked(spec.Removals.Count * sizeof(SwfTagRemoval)));
            var plan = new SwfNativePlan(replacements, insertions, removals, payloads);
            var replacementPtr = (SwfTagReplacement*)replacements;
            for (var index = 0; index < spec.Replacements.Count; ++index)
            {
                var (tagIndex, payload) = spec.Replacements[index];
                replacementPtr[index] = new SwfTagReplacement
                {
                    TagIndex = (nuint)tagIndex,
                    Payload = payloads[index],
                    Size = (nuint)payload.Length,
                };
            }
            var insertionPtr = (SwfTagInsertion*)insertions;
            for (var index = 0; index < spec.Insertions.Count; ++index)
            {
                var (beforeIndex, code, payload) = spec.Insertions[index];
                insertionPtr[index] = new SwfTagInsertion
                {
                    BeforeTagIndex = (nuint)beforeIndex,
                    Code = code,
                    Payload = payloads[spec.Replacements.Count + index],
                    Size = (nuint)payload.Length,
                };
            }
            var removalPtr = (SwfTagRemoval*)removals;
            for (var index = 0; index < spec.Removals.Count; ++index)
            {
                removalPtr[index] = new SwfTagRemoval { TagIndex = (nuint)spec.Removals[index] };
            }
            plan.Value = new SwfTagPlan
            {
                Replacements = replacements,
                ReplacementCount = (nuint)spec.Replacements.Count,
                Insertions = insertions,
                InsertionCount = (nuint)spec.Insertions.Count,
                Removals = removals,
                RemovalCount = (nuint)spec.Removals.Count,
            };
            return plan;
        }
        catch
        {
            Free(new[] { replacements, insertions, removals }.Concat(payloads));
            throw;
        }
    }

    private static IntPtr Copy(byte[] payload)
    {
        if (payload.Length == 0) return IntPtr.Zero;
        var memory = System.Runtime.InteropServices.Marshal.AllocHGlobal(payload.Length);
        System.Runtime.InteropServices.Marshal.Copy(payload, 0, memory, payload.Length);
        return memory;
    }

    public void Dispose() => Free(new[] { _replacements, _insertions, _removals }.Concat(_payloads));

    private static void Free(IEnumerable<IntPtr> pointers)
    {
        foreach (var pointer in pointers)
        {
            if (pointer != IntPtr.Zero) System.Runtime.InteropServices.Marshal.FreeHGlobal(pointer);
        }
    }
}

public sealed unsafe partial class SwfFile
{
    // Constraint: a model refusal surfaces through Call.Check with the Radicle message, never a silent empty list.
    public IReadOnlyList<SwfCharacterRecord> Characters()
    {
        using var use = UseForEditor();
        nuint count = 0;
        Call.Check(RadicleNative.SwfCharacterCount(use.Pointer, &count));
        var characters = new SwfCharacterRecord[checked((int)count)];
        for (var index = 0; index < characters.Length; ++index)
        {
            SwfCharacter raw = default;
            Call.Check(RadicleNative.SwfCharacterAt(use.Pointer, (nuint)index, &raw));
            characters[index] = ToCharacterRecord(raw);
        }
        return characters;
    }

    public SwfCharacterRecord? CharacterById(ushort characterId)
    {
        using var use = UseForEditor();
        SwfCharacter raw = default;
        return Call.Found(RadicleNative.SwfCharacterById(use.Pointer, characterId, &raw)) ? ToCharacterRecord(raw) : null;
    }

    private static SwfCharacterRecord ToCharacterRecord(SwfCharacter raw) => new(
        raw.CharacterId, (SwfCharacterKind)raw.Kind, checked((int)raw.TagIndex),
        raw.HasBounds != 0,
        new SwfRectRecord(raw.Bounds.XMin, raw.Bounds.XMax, raw.Bounds.YMin, raw.Bounds.YMax),
        raw.HasBitmapSize != 0, raw.BitmapWidth, raw.BitmapHeight,
        Utf8.Read(raw.Name), Utf8.Read(raw.ImportedFrom), Utf8.Read(raw.ImportedName));

    public SwfLiveCounts LiveCounts()
    {
        using var use = UseForEditor();
        Radicle.Interop.Native.SwfInfo info = default;
        Call.Check(RadicleNative.SwfGetInfo(use.Pointer, &info));
        return new SwfLiveCounts(
            checked((int)info.TagCount),
            checked((int)info.SymbolCount),
            checked((int)info.ExportCount),
            checked((int)info.FrameLabelCount),
            checked((int)info.AbcCount));
    }

    public IReadOnlyList<SwfFontGlyphRecord> FontGlyphs(int tagIndex)
    {
        using var use = UseForEditor();
        nuint count = 0;
        Call.Check(RadicleNative.SwfFontGlyphCount(use.Pointer, (nuint)tagIndex, &count));
        var glyphs = new SwfFontGlyphRecord[checked((int)count)];
        for (var glyph = 0; glyph < glyphs.Length; ++glyph)
        {
            SwfGlyph raw = default;
            Call.Check(RadicleNative.SwfFontGlyphAt(use.Pointer, (nuint)tagIndex, (nuint)glyph, &raw));
            var index = glyph;
            glyphs[glyph] = new SwfFontGlyphRecord(raw.FillBits, raw.LineBits,
                ReadCommands(use.Pointer, tagIndex, checked((int)raw.RecordCount),
                    (IntPtr swf, nuint tag, nuint record, Native.SwfShapeRecord* output) =>
                        RadicleNative.SwfFontGlyphRecordAt(swf, tag, (nuint)index, record, output)));
        }
        return glyphs;
    }

    public SwfSoundRecord Sound(int tagIndex)
    {
        using var use = UseForEditor();
        SwfSound raw = default;
        Call.Check(RadicleNative.SwfSoundAt(use.Pointer, (nuint)tagIndex, &raw));
        return new SwfSoundRecord(raw.SoundId, raw.Format, raw.Rate, raw.Sample16bit != 0, raw.Stereo != 0,
            raw.SampleCount, (long)raw.DataSize);
    }

    public SwfStartSoundRecord StartSound(int tagIndex)
    {
        using var use = UseForEditor();
        SwfStartSound raw = default;
        Call.Check(RadicleNative.SwfStartSoundAt(use.Pointer, (nuint)tagIndex, &raw));
        return new SwfStartSoundRecord(
            raw.SoundId, raw.SyncStop != 0, raw.SyncNoMultiple != 0,
            raw.HasInPoint != 0 ? raw.InPoint : null,
            raw.HasOutPoint != 0 ? raw.OutPoint : null,
            raw.HasLoopCount != 0 ? raw.LoopCount : null,
            checked((int)raw.EnvelopeCount));
    }

    public SwfPlanResultRecord ReplaceSoundData(SwfSoundRecord header, ReadOnlySpan<byte> data)
    {
        using var use = UseForEditor();
        var native = new SwfSound
        {
            SoundId = header.SoundId,
            Format = header.Format,
            Rate = header.Rate,
            Sample16bit = (byte)(header.Sample16Bit ? 1 : 0),
            Stereo = (byte)(header.Stereo ? 1 : 0),
            SampleCount = header.SampleCount,
            DataSize = (nuint)data.Length,
        };
        SwfPlanResult result;
        fixed (byte* bytes = data)
        {
            Call.Check(RadicleNative.SwfReplaceSoundData(use.Pointer, &native, bytes, (nuint)data.Length, &result));
        }
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    public SwfPlanResultRecord RetargetStartSound(int tagIndex, ushort newSoundId)
    {
        using var use = UseForEditor();
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfRetargetStartSound(use.Pointer, (nuint)tagIndex, newSoundId, &result));
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    public SwfFrameSet OpenFrames() => SwfFrameSet.Open(this);

    public SwfEditHistory OpenHistory() => SwfEditHistory.Create(this);

    public ulong Revision
    {
        get
        {
            using var use = UseForEditor();
            return RadicleNative.SwfRevision(use.Pointer);
        }
    }

    public unsafe SwfPlanResultRecord ApplyPlan(SwfPlanSpec plan)
    {
        using var use = UseForEditor();
        using var native = SwfNativePlan.Create(plan);
        SwfTagPlan value = native.Value;
        SwfPlanResult result;
        Call.Check(RadicleNative.SwfApplyPlan(use.Pointer, &value, &result));
        return new SwfPlanResultRecord(result.Changed != 0, result.Revision);
    }

    internal HandleUse UseForEditor() => Use();
}
