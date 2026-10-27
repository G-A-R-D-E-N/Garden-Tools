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

    // Invariant: Movie is reopened from the history bytes after every Apply, Undo and Redo.
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

    public SwfEditTextRecord EditText(int tagIndex) => Movie.EditText(tagIndex);

    // Requires a root-timeline frame the movie reaches; a duplicate name on the root timeline is refused.
    public SwfPlanResultRecord AddLabel(int frame, string name, int timeline = 0)
    {
        var payload = LabelPayload(name, timeline, -1);
        if (timeline == 0)
        {
            var showFrames = Movie.Tags.Where(tag => tag.Code == 1 && !tag.InsideSprite).ToArray();
            if (frame < 0 || frame >= showFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
            return Apply(new SwfPlanSpec().Insert(showFrames[frame].Index, 43, payload));
        }
        var sprite = Movie.Tags.FirstOrDefault(tag => tag.Code == 39 && tag.SpriteId == (ushort)timeline);
        if (sprite == null) throw new InvalidOperationException($"timeline {timeline} is not a DefineSprite in this movie");
        return InsertSpriteChild(sprite, frame, 43, payload);
    }

    // Requires a frame-label tag index; the rename keeps every byte after the name's NUL (the named-anchor flag).
    public SwfPlanResultRecord RenameLabel(int tagIndex, string name)
    {
        if (Movie.Tags[tagIndex].Code != 43) throw new InvalidOperationException("the tag is not a frame label");
        var label = Movie.FrameLabels.First(item => item.TagIndex == tagIndex);
        var head = LabelPayload(name, label.InsideSprite ? label.SpriteId : 0, tagIndex);
        var old = Movie.Payload(tagIndex);
        var nul = Array.IndexOf(old, (byte)0);
        if (nul < 0) throw new InvalidOperationException("the frame label has no name terminator");
        return Apply(new SwfPlanSpec().Replace(tagIndex, head.Concat(old.Skip(nul + 1)).ToArray()));
    }

    public IReadOnlyList<SwfPlacementRecord> Placements() => Movie.Placements();

    // Requires a placement tag index; the depth change is one history command.
    public SwfPlanResultRecord SetDepth(int tagIndex, ushort depth) => Apply(new SwfPlanSpec().Replace(
        tagIndex, SwfPlacementCodec.WithDepth(Movie.Payload(tagIndex), Movie.Tags[tagIndex].Code, depth)));

    // Constraint: an edit-text write goes through the same plan and history path as every other edit.
    public SwfPlanResultRecord EditEditText(int tagIndex, SwfEditTextEdit edit)
    {
        var record = Movie.EditText(tagIndex);
        var payload = SwfEditTextCodec.Write(record, Movie.Payload(tagIndex), edit);
        return Apply(new SwfPlanSpec().Replace(tagIndex, payload));
    }

    public SwfPlacementMatrix PlacementTransform(int tagIndex) => Movie.PlacementMatrix(tagIndex);

    // Requires a placement tag index; the transform edit keeps the depth and every other placement byte.
    public SwfPlanResultRecord SetPlacementTransform(int tagIndex, SwfPlacementTransformEdit edit)
    {
        var payload = SwfPlacementMatrixCodec.Write(Movie.PlacementMatrix(tagIndex), Movie.Tags[tagIndex].Code,
            Movie.Payload(tagIndex), edit);
        return Apply(new SwfPlanSpec().Replace(tagIndex, payload));
    }

    // Requires a PlaceObject2 tag and a reached frame; the copy becomes a move row at that frame without its character id.
    public SwfPlanResultRecord InsertKeyframe(int tagIndex, int frame)
    {
        var tag = Movie.Tags[tagIndex];
        if (tag.Code != 26) throw new InvalidOperationException("only a PlaceObject2 placement can carry a keyframe this round");
        var payload = Movie.Payload(tagIndex);
        if ((payload[0] & 0x02) != 0) payload = payload.Take(3).Concat(payload.Skip(5)).ToArray();
        payload[0] = (byte)((payload[0] & ~0x02) | 0x01);
        if (!tag.InsideSprite)
        {
            var showFrames = Movie.Tags.Where(item => item.Code == 1 && !item.InsideSprite).ToArray();
            if (frame < 0 || frame >= showFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
            return Apply(new SwfPlanSpec().Insert(showFrames[frame].Index, 26, payload));
        }
        var sprite = Movie.Tags[tag.Parent];
        if (sprite.Code != 39) throw new InvalidOperationException("the placement parent is not a DefineSprite");
        return InsertSpriteChild(sprite, frame, 26, payload);
    }

    // Requires the tag index of a frame label; the removal is one history command and undo restores the tag.
    public SwfPlanResultRecord RemoveLabel(int tagIndex)
    {
        var tag = Movie.Tags[tagIndex];
        if (tag.Code != 43) throw new InvalidOperationException("the tag is not a frame label");
        return Apply(new SwfPlanSpec().Remove(tagIndex));
    }

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

    private void Reopen()
    {
        var bytes = _history.WriteBytes();
        var next = SwfFile.FromBytes(bytes);
        var old = _movie;
        _current = bytes;
        _movie = next;
        old.Dispose();
    }

    private byte[] LabelPayload(string name, int timeline, int ownTagIndex)
    {
        if (string.IsNullOrWhiteSpace(name) || name.Contains('\0'))
            throw new ArgumentException("a frame label needs a name without NUL characters", nameof(name));
        if (Movie.FrameLabels.Any(label => label.TagIndex != ownTagIndex && label.SpriteId == timeline &&
                                           label.InsideSprite == (timeline != 0) && label.Name == name))
            throw new InvalidOperationException("a frame label with that name already exists on this timeline");
        return System.Text.Encoding.UTF8.GetBytes(name).Concat(new byte[] { 0 }).ToArray();
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
}
