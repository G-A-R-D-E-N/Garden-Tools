using System;
using System.Collections.Generic;
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
