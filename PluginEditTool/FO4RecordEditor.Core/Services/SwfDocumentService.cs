using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using Radicle.Interop;

namespace FO4RecordEditor.Services;

// Constraint: one document owns one history, and every edit goes through it so undo/redo is the only writer.
public sealed class SwfDocument : IDisposable
{
    private readonly SwfEditHistory _history;

    private SwfDocument(string? path, SwfFile file, SwfEditHistory history)
    {
        Path = path;
        Movie = file;
        _history = history;
    }

    public string? Path { get; }

    public SwfFile Movie { get; }

    public ulong Revision => _history.Revision;

    public int UndoCount => _history.UndoCount;

    public int RedoCount => _history.RedoCount;

    public bool Dirty => Revision != 0;

    public static SwfDocument OpenBytes(ReadOnlySpan<byte> bytes, string? path = null)
    {
        var file = SwfFile.FromBytes(bytes);
        return new SwfDocument(path, file, file.OpenHistory());
    }

    // Requires the host's asset resolution; the document keeps the logical path but saves only to an explicit target.
    public static SwfDocument OpenResolved(string assetPath, Func<string, byte[]?> resolve)
    {
        var bytes = resolve(assetPath) ?? throw new FileNotFoundException("the asset resolver returned no bytes", assetPath);
        return OpenBytes(bytes);
    }
    public static SwfDocument OpenFile(string path)
    {
        var bytes = System.IO.File.ReadAllBytes(path);
        return OpenBytes(bytes, path);
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
    public SwfPlanResultRecord Apply(SwfPlanSpec plan) => _history.Apply(plan);

    public SwfPlanResultRecord Undo() => _history.Undo();

    public SwfPlanResultRecord Redo() => _history.Redo();

    public byte[] WriteBytes() => _history.WriteBytes();

    public SwfEditTextRecord EditText(int tagIndex) => Movie.EditText(tagIndex);

    // Requires a root-timeline frame the movie reaches; a duplicate name on the root timeline is refused.
    public SwfPlanResultRecord AddLabel(int frame, string name)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ArgumentException("a frame label needs a name", nameof(name));
        if (Movie.FrameLabels.Any(label => label.SpriteId == 0 && !label.InsideSprite && label.Name == name))
            throw new InvalidOperationException("a frame label with that name already exists on this timeline");
        var showFrames = Movie.Tags.Where(tag => tag.Code == 1 && !tag.InsideSprite).ToArray();
        if (frame < 0 || frame >= showFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
        var payload = System.Text.Encoding.UTF8.GetBytes(name).Concat(new byte[] { 0 }).ToArray();
        return Apply(new SwfPlanSpec().Insert(showFrames[frame].Index, 43, payload));
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
    public SwfPlanResultRecord SetPlacementTransform(int tagIndex, SwfPlacementTransformEdit edit) =>
        Movie.SetPlacementTransform(tagIndex, edit);
    // Requires a root PlaceObject2 tag and a reached frame; the copy becomes a move row at that frame.
    public SwfPlanResultRecord InsertKeyframe(int tagIndex, int frame)
    {
        var tag = Movie.Tags[tagIndex];
        if (tag.Code != 26) throw new InvalidOperationException("only a PlaceObject2 placement can carry a keyframe this round");
        var payload = Movie.Payload(tagIndex);
        payload[0] = (byte)((payload[0] & ~0x02) | 0x01);
        if (!tag.InsideSprite)
        {
            var showFrames = Movie.Tags.Where(item => item.Code == 1 && !item.InsideSprite).ToArray();
            if (frame < 0 || frame >= showFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
            return Apply(new SwfPlanSpec().Insert(showFrames[frame].Index, 26, payload));
        }
        var sprite = Movie.Tags[tag.Parent];
        if (sprite.Code != 39) throw new InvalidOperationException("the placement parent is not a DefineSprite");
        var spriteFrames = Movie.Tags.Where(item => item.Parent == sprite.Index && item.Code == 1).ToArray();
        if (frame < 0 || frame >= spriteFrames.Length) throw new ArgumentOutOfRangeException(nameof(frame));
        var target = spriteFrames[frame];
        var headerSize = target.LongHeader ? 6 : 2;
        var headerStart = target.Offset - (ulong)headerSize;
        var spritePayload = Movie.Payload(sprite.Index);
        var next = new List<byte>(spritePayload.Length + payload.Length + 6);
        for (var index = 0ul; index < headerStart; ++index) next.Add(spritePayload[(int)index]);
        if (payload.Length >= 0x3F)
        {
            next.AddRange(BitConverter.GetBytes((ushort)((26 << 6) | 0x3F)));
            next.AddRange(BitConverter.GetBytes((uint)payload.Length));
        }
        else
        {
            next.AddRange(BitConverter.GetBytes((ushort)((26 << 6) | payload.Length)));
        }
        next.AddRange(payload);
        for (var index = headerStart; index < (ulong)spritePayload.Length; ++index) next.Add(spritePayload[(int)index]);
        return Apply(new SwfPlanSpec().Replace(sprite.Index, next.ToArray()));
    }
    // Constraint: the save must back the on-disk asset up before the first byte of the new file lands.
    public string Save(string? targetPath = null)
    {
        var destination = targetPath ?? Path ?? throw new InvalidOperationException("the document has no path to save to");
        if (System.IO.File.Exists(destination))
        {
            var backup = destination + ".bak";
            System.IO.File.Copy(destination, backup, overwrite: true);
        }
        System.IO.File.WriteAllBytes(destination, WriteBytes());
        return destination;
    }

    public void Dispose()
    {
        _history.Dispose();
        Movie.Dispose();
    }

    public override string ToString() => Path ?? "<bytes>";
}
