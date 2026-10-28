using System;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfEditingTests
{
    [Fact]
    public void ReadsCharactersThroughTheTypedSurface()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var characters = movie.Characters();
        Assert.Equal(2, characters.Count);
        Assert.Equal(SwfCharacterKind.EditText, characters[0].Kind);
        Assert.Equal(11, characters[0].CharacterId);
        Assert.True(characters[0].HasBounds);
        Assert.Equal(SwfCharacterKind.Sprite, characters[1].Kind);
        Assert.Equal(14, characters[1].CharacterId);
        Assert.Equal((ushort)0, characters[1].BitmapWidth);
    }

    [Fact]
    public void ReadsFrameLabelsAndPerFrameDisplayList()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        Assert.Equal(new[] { "start", "inner" }, movie.FrameLabels.Select(label => label.Name));
        using var frames = movie.OpenFrames();
        Assert.Equal(2, frames.TimelineCount);
        Assert.Equal((ushort)0, frames.TimelineAt(0).CharacterId);
        Assert.Equal((ushort)14, frames.TimelineAt(1).CharacterId);
        var instances = frames.Instances(0, 0);
        Assert.Single(instances);
        Assert.Equal((ushort)11, instances[0].CharacterId);
        Assert.Equal((ushort)1, instances[0].Depth);
        Assert.Equal(0u, instances[0].TimelineFrame);
    }

    [Fact]
    public void APlanEditBecomesACommandThatUndoesAndRedoes()
    {
        var original = SwfFixtures.Movie();
        using var movie = SwfFile.FromBytes(original);
        var showFrame = movie.Tags.First(tag => tag.Code == 1 && !tag.InsideSprite);
        var plan = new SwfPlanSpec().Replace(showFrame.Index, new byte[] { 0x00 });

        var applied = movie.ApplyPlan(plan);
        Assert.True(applied.Changed);
        Assert.Equal(1ul, applied.Revision);
        var edited = movie.WriteBytes();
        Assert.NotEqual(original, edited);

        using var historyMovie = SwfFile.FromBytes(original);
        using var history = historyMovie.OpenHistory();
        var appliedByHistory = history.Apply(new SwfPlanSpec().Replace(showFrame.Index, new byte[] { 0x00 }));
        Assert.True(appliedByHistory.Changed);
        Assert.Equal(1ul, appliedByHistory.Revision);
        Assert.Equal(edited, history.WriteBytes());
        Assert.Equal(1, history.UndoCount);

        history.Undo();
        Assert.Equal(original, history.WriteBytes());
        Assert.Equal(1, history.RedoCount);
        history.Redo();
        Assert.Equal(edited, history.WriteBytes());
        Assert.Equal(0, history.RedoCount);
    }

    [Fact]
    public void VerbsEditTextAndFrameLabelsUndoThroughTheHistory()
    {
        var original = SwfFixtures.Movie();
        using var movie = SwfFile.FromBytes(original);
        using var history = movie.OpenHistory();

        var applied = SwfVerbs.ApplyEditText(history, 0, new SwfEditTextEdit(Bounds: new SwfRectRecord(0, 800, 0, 400)));
        Assert.True(applied.Changed);
        Assert.Equal(800, SwfVerbs.ReadEditText(history, 0).Bounds.XMax);
        Assert.Equal(new[] { "start" }, SwfVerbs.FrameLabelRows(history, 0).Select(row => row.Name));

        SwfVerbs.AddFrameLabel(history, 0, 0, "mid");
        SwfVerbs.RenameFrameLabel(history, 0, 1, "late");
        SwfVerbs.RemoveFrameLabel(history, 0, 0);
        Assert.Equal(new[] { "late" }, SwfVerbs.FrameLabelRows(history, 0).Select(row => row.Name));
        Assert.Equal(4, history.UndoCount);

        for (var step = 0; step < 4; ++step) history.Undo();
        Assert.Equal(original, history.WriteBytes());
        Assert.Equal(new[] { "start" }, SwfVerbs.FrameLabelRows(history, 0).Select(row => row.Name));
    }

    [Fact]
    public void ARefusalCarriesTheRadicleMessage()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var error = Assert.Throws<RadicleException>(() =>
            movie.ApplyPlan(new SwfPlanSpec().Replace(99, new byte[] { 0x00 })));
        Assert.Equal(Status.InvalidArgument, error.Status);
        Assert.False(string.IsNullOrWhiteSpace(error.Message));
    }
}
