using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using Radicle.Interop;

namespace FO4RecordEditor.Services;

// Constraint: one document owns one history, and every edit goes through it so undo/redo is the only writer.
public sealed class SwfDocument : IDisposable
{
    static SwfDocument() => RadicleNative.EnsureLoaded();

    private readonly SwfEditHistory _history;
    private readonly bool _resolved;
    private byte[] _saved;
    private byte[] _current;
    private SwfFile _movie;

    private SwfDocument(string? path, SwfFile file, SwfEditHistory history, bool resolved, byte[] saved)
    {
        Path = path;
        _movie = file;
        _history = history;
        _resolved = resolved;
        _saved = saved;
        _current = saved;
    }

    public string? Path { get; }

    // Invariant: Movie is reopened from the history bytes after every Apply, Undo, Redo and SwfVerbs call (Verb).
    public SwfFile Movie => _movie;

    public ulong Revision => _history.Revision;

    public int UndoCount => _history.UndoCount;

    public int RedoCount => _history.RedoCount;

    public bool Dirty => !_current.AsSpan().SequenceEqual(_saved);

    public static SwfDocument OpenBytes(ReadOnlySpan<byte> bytes, string? path = null) =>
        Create(bytes, path, resolved: false);

    // Requires the host's asset resolution; the logical asset path is kept and Save needs an explicit target.
    public static SwfDocument OpenResolved(string assetPath, Func<string, byte[]?> resolve)
    {
        var bytes = resolve(assetPath) ?? throw new FileNotFoundException("the asset resolver returned no bytes", assetPath);
        return Create(bytes, assetPath, resolved: true);
    }

    public static SwfDocument OpenFile(string path) =>
        Create(System.IO.File.ReadAllBytes(path), path, resolved: false);

    private static SwfDocument Create(ReadOnlySpan<byte> bytes, string? path, bool resolved)
    {
        var saved = bytes.ToArray();
        var file = SwfFile.FromBytes(saved);
        var history = file.OpenHistory();
        return new SwfDocument(path, file, history, resolved, history.WriteBytes());
    }

    public IReadOnlyList<SwfCharacterRecord> Characters() => Movie.Characters();

    public IReadOnlyList<SwfFrameLabelRecord> FrameLabels() => Movie.FrameLabels;

    public IReadOnlyList<SwfFrameLabelRow> FrameLabelRows(int timeline) => SwfVerbs.FrameLabelRows(_history, (ushort)timeline);

    public IReadOnlyList<SwfFrameInstanceRecord> Frame(int timeline, int frame)
    {
        using var frames = Movie.OpenFrames();
        return frames.Instances(timeline, frame);
    }

    public (ushort CharacterId, int FrameCount) Timeline(int index)
    {
        using var frames = Movie.OpenFrames();
        return frames.TimelineAt(index);
    }

    // Requires a plan built from the flat radicle_swf_tag_at indices; a refusal throws the Radicle error.
    public SwfPlanResultRecord Apply(SwfPlanSpec plan)
    {
        var result = _history.Apply(plan);
        Reopen();
        return result;
    }

    public SwfPlanResultRecord Undo()
    {
        var result = _history.Undo();
        Reopen();
        return result;
    }

    public SwfPlanResultRecord Redo()
    {
        var result = _history.Redo();
        Reopen();
        return result;
    }

    public byte[] WriteBytes() => _history.WriteBytes();

    public SwfEditTextStateRecord EditText(int tagIndex) => SwfVerbs.ReadEditText(_history, tagIndex);

    public string InspectText() => Movie.InspectText(ToString());

    // Requires a DoAction (12) or DoInitAction (59) flat tag index; Radicle refuses any other tag and the refusal is thrown as its RadicleException.
    public string Avm1Disassemble(int tagIndex) => Movie.Avm1Disassemble(tagIndex);

    // Requires a frame the chosen timeline reaches; Radicle refuses a duplicate name on that timeline.
    public SwfPlanResultRecord AddLabel(int frame, string name, int timeline = 0) =>
        Verb(history => SwfVerbs.AddFrameLabel(history, (ushort)timeline, (uint)frame, name));

    // Requires a label index from FrameLabelRows on that timeline, not a tag index.
    public SwfPlanResultRecord RenameLabel(int timeline, int labelIndex, string name) =>
        Verb(history => SwfVerbs.RenameFrameLabel(history, (ushort)timeline, labelIndex, name));

    public IReadOnlyList<SwfPlacementRecord> Placements() => Movie.Placements();

    // Requires a placement tag index; a root placement moves its creating instance through the native depth verb.
    public SwfPlanResultRecord SetDepth(int tagIndex, ushort depth)
    {
        if (Movie.Tags[tagIndex].InsideSprite) return Apply(new SwfPlanSpec().Replace(
            tagIndex, SwfPlacementCodec.WithDepth(Movie.Payload(tagIndex), Movie.Tags[tagIndex].Code, depth)));
        var (rootFrame, instanceId) = RootInstance(RootRows(row => row.CreateTagIndex == tagIndex));
        return Verb(history => SwfVerbs.ReassignInstanceDepth(history, rootFrame, instanceId, depth, out _));
    }

    // Requires a DefineEditText tag index; the verb commits one history command like every other edit.
    public SwfPlanResultRecord EditEditText(int tagIndex, SwfEditTextEdit edit) =>
        Verb(history => SwfVerbs.ApplyEditText(history, tagIndex, edit));

    public SwfPlacementMatrix PlacementTransform(int tagIndex) => Movie.PlacementMatrix(tagIndex);

    // Requires a placement tag index; a root transform source with a row is edited natively, and any other placement takes the managed write.
    public SwfPlanResultRecord SetPlacementTransform(int tagIndex, SwfPlacementTransformEdit edit)
    {
        var tag = Movie.Tags[tagIndex];
        var rows = tag.InsideSprite ? Array.Empty<SwfFrameInstanceRecord>() : RootRows(row => row.TransformTagIndex == tagIndex);
        if (rows.Length > 0)
        {
            var transform = ToVerbTransform(Movie.PlacementMatrix(tagIndex), edit);
            var root = rows[0];
            return Verb(history => SwfVerbs.ApplyInstanceTransforms(history, root.TimelineFrame,
                new[] { new SwfInstanceTransformEdit(root.InstanceId, transform) }));
        }
        var payload = SwfPlacementMatrixCodec.Write(Movie.PlacementMatrix(tagIndex), tag.Code,
            Movie.Payload(tagIndex), edit);
        return Apply(new SwfPlanSpec().Replace(tagIndex, payload));
    }

    // Requires a PlaceObject2 tag and a reached frame; the copy becomes a move row at that frame without its character id.
    public SwfPlanResultRecord InsertKeyframe(int tagIndex, int frame)
    {
        var tag = Movie.Tags[tagIndex];
        if (tag.Code != 26) throw new InvalidOperationException("only a PlaceObject2 placement can carry a keyframe this round");
        if (!tag.InsideSprite)
        {
            var showFrames = Movie.Tags.Count(item => item.Code == 1 && !item.InsideSprite);
            if (frame < 0 || frame >= showFrames) throw new ArgumentOutOfRangeException(nameof(frame));
            var (rootFrame, instanceId) = RootInstance(RootRows(
                row => row.CreateTagIndex == tagIndex || row.TransformTagIndex == tagIndex, frame));
            return Verb(history => SwfVerbs.InsertInstanceTransformKeyframe(history, rootFrame, instanceId));
        }
        var payload = Movie.Payload(tagIndex);
        if ((payload[0] & 0x02) != 0) payload = payload.Take(3).Concat(payload.Skip(5)).ToArray();
        payload[0] = (byte)((payload[0] & ~0x02) | 0x01);
        var sprite = Movie.Tags[tag.Parent];
        if (sprite.Code != 39) throw new InvalidOperationException("the placement parent is not a DefineSprite");
        return InsertSpriteChild(sprite, frame, 26, payload);
    }

    // Requires a label index from FrameLabelRows on that timeline; the removal is one history command and undo restores the tag.
    public SwfPlanResultRecord RemoveLabel(int timeline, int labelIndex) =>
        Verb(history => SwfVerbs.RemoveFrameLabel(history, (ushort)timeline, labelIndex));

    // Requires an instruction boundary in a method body of the ABC block; the edit lands as one plan over its DoABC tag.
    public SwfPlanResultRecord InsertAbcInstruction(int abc, int body, int offset, string text) =>
        ApplyNativeEdit(Movie.AbcBlocks[abc].TagIndex, scratch => scratch.InsertAbcInstruction(abc, body, offset, text));

    // Requires the same arguments as the insert; the instruction at offset is replaced in place.
    public SwfPlanResultRecord ReplaceAbcInstruction(int abc, int body, int offset, string text) =>
        ApplyNativeEdit(Movie.AbcBlocks[abc].TagIndex, scratch => scratch.ReplaceAbcInstruction(abc, body, offset, text));

    // Requires a DefineFont tag index; the glyph shape lands as one plan over that font tag.
    public SwfPlanResultRecord ReplaceFontGlyph(int tagIndex, int glyph, byte fillBits, byte lineBits,
                                                IReadOnlyList<SwfShapeCommandRecord> records) =>
        ApplyNativeEdit(tagIndex, scratch => scratch.ReplaceFontGlyph(tagIndex, glyph, fillBits, lineBits, records));

    // Constraint: the save goes through the existing WriteService asset path, which backs the asset up first.
    public string Save(string? targetPath = null)
    {
        if (targetPath == null && _resolved)
            throw new InvalidOperationException("a resolved archive asset is read-only; save it to an explicit target path");
        var destination = targetPath ?? Path ?? throw new InvalidOperationException("the document has no path to save to");
        var saved = WriteService.SaveAsset(destination, _current);
        if (string.Equals(destination, Path, StringComparison.OrdinalIgnoreCase)) _saved = _current;
        return saved;
    }

    public void Dispose()
    {
        _history.Dispose();
        _movie.Dispose();
    }

    public override string ToString() => Path ?? "<bytes>";

    private SwfPlanResultRecord Verb(Func<SwfEditHistory, SwfPlanResultRecord> verb)
    {
        var result = verb(_history);
        Reopen();
        return result;
    }

    // Requires the rows of the first frame that names the placement, or of frame when one is given; later frames are not read.
    private SwfFrameInstanceRecord[] RootRows(Func<SwfFrameInstanceRecord, bool> addresses, int? frame = null)
    {
        using var frames = Movie.OpenFrames();
        var candidates = frame is { } index ? new[] { index } : Enumerable.Range(0, frames.TimelineAt(0).FrameCount).ToArray();
        foreach (var candidate in candidates)
        {
            var rows = frames.Instances(0, candidate).Where(row => row.TimelineId == 0 && addresses(row)).ToArray();
            if (rows.Length > 0) return rows;
        }
        return Array.Empty<SwfFrameInstanceRecord>();
    }

    // Requires one instance among the rows; two instances on one frame refuse instead of guessing.
    private static (uint Frame, uint InstanceId) RootInstance(SwfFrameInstanceRecord[] rows)
    {
        if (rows.Length == 0) throw new InvalidOperationException("no root-timeline instance is addressed by that placement");
        var instances = rows.Select(row => row.InstanceId).Distinct().ToArray();
        if (instances.Length != 1) throw new InvalidOperationException("the placement addresses more than one instance; refusing to guess");
        return (rows[0].TimelineFrame, instances[0]);
    }

    // Requires MATRIX fixed-point fields as unit-one doubles and twips as points, the units the native verbs take.
    private static SwfTransformRecord ToVerbTransform(SwfPlacementMatrix matrix, SwfPlacementTransformEdit edit)
    {
        var scaleX = edit.ScaleX ?? (matrix.HasScale ? matrix.ScaleX : 0x10000);
        var scaleY = edit.ScaleY ?? (matrix.HasScale ? matrix.ScaleY : 0x10000);
        var rotate0 = edit.Rotate0 ?? (matrix.HasRotate ? matrix.Rotate0 : 0);
        var rotate1 = edit.Rotate1 ?? (matrix.HasRotate ? matrix.Rotate1 : 0);
        return new SwfTransformRecord(scaleX / 65536.0, rotate0 / 65536.0, rotate1 / 65536.0, scaleY / 65536.0,
            (edit.TranslateX ?? matrix.TranslateX) / 20.0, (edit.TranslateY ?? matrix.TranslateY) / 20.0);
    }

    private void Reopen()
    {
        var bytes = _history.WriteBytes();
        var next = SwfFile.FromBytes(bytes);
        var old = _movie;
        _current = bytes;
        _movie = next;
        old.Dispose();
    }

    // Requires a reached frame of the sprite; the child tag is spliced in front of that frame's ShowFrame header.
    private SwfPlanResultRecord InsertSpriteChild(SwfTagRecord sprite, int frame, ushort code, byte[] payload)
    {
        var spriteFrames = Movie.Tags.Where(item => item.Parent == sprite.Index && item.Code == 1).ToArray();
        if (frame < 0 || frame >= spriteFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
        var target = spriteFrames[frame];
        var headerStart = (int)(target.Offset - (target.LongHeader ? 6 : 2));
        var header = payload.Length >= 0x3F
            ? BitConverter.GetBytes((ushort)((code << 6) | 0x3F)).Concat(BitConverter.GetBytes((uint)payload.Length))
            : BitConverter.GetBytes((ushort)((code << 6) | payload.Length));
        var spritePayload = Movie.Payload(sprite.Index);
        var next = spritePayload.Take(headerStart).Concat(header).Concat(payload)
            .Concat(spritePayload.Skip(headerStart)).ToArray();
        return Apply(new SwfPlanSpec().Replace(sprite.Index, next));
    }

    // Requires the native edit to run on a scratch copy, so only the tag it lands as enters the history.
    private SwfPlanResultRecord ApplyNativeEdit(int tagIndex, Action<SwfFile> edit)
    {
        using var scratch = SwfFile.FromBytes(_current);
        edit(scratch);
        return Apply(new SwfPlanSpec().Replace(tagIndex, scratch.Payload(tagIndex)));
    }
}
