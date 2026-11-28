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
        Assert.Equal(2u, instances[0].CreateTagIndex);
        Assert.Equal(string.IsNullOrEmpty(instances[0].Name) ? "#11" : instances[0].Name, instances[0].Path);
    }

    [Fact]
    public void AnUnfilteredPlacementReportsDefaultEffectsAndRefusesBadIndexes()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        using var frames = movie.OpenFrames();
        var effects = frames.InstanceEffects(0, 0, 0);
        Assert.False(effects.HasColorTransform);
        Assert.True(effects.WorldVisible);
        Assert.Equal(0, effects.FilterCount);
        Assert.Equal(new[] { 256, 256, 256, 256 }, effects.ColorMultiply);
        Assert.Equal(new[] { 0, 0, 0, 0 }, effects.ColorAdd);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => frames.InstanceFilter(0, 0, 0, 0)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => frames.InstanceEffects(0, 0, 5)).Status);
    }

    [Fact]
    public void FilteredPlacementExposesShadowAndBlurValues()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie(withFilters: true));
        using var frames = movie.OpenFrames();
        Assert.Equal(2, frames.InstanceEffects(0, 0, 0).FilterCount);
        var shadow = frames.InstanceFilter(0, 0, 0, 0);
        Assert.Equal((byte)0, shadow.Id);
        Assert.Equal(0x40102030u, shadow.Color);
        Assert.Equal((2.0, 3.0, 4.0, 1.5), (shadow.BlurX, shadow.BlurY, shadow.Distance, shadow.Strength));
        Assert.Equal(1.0, shadow.Angle);
        Assert.False(shadow.Inner);
        Assert.False(shadow.Knockout);
        Assert.Equal((byte)1, shadow.Passes);
        var blur = frames.InstanceFilter(0, 0, 0, 1);
        Assert.Equal((byte)1, blur.Id);
        Assert.Equal((5.0, 6.0), (blur.BlurX, blur.BlurY));
        Assert.Equal((byte)2, blur.Passes);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => frames.InstanceFilter(0, 0, 0, 2)).Status);
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
    public void ARefusalCarriesTheRadicleMessage()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var error = Assert.Throws<RadicleException>(() =>
            movie.ApplyPlan(new SwfPlanSpec().Replace(99, new byte[] { 0x00 })));
        Assert.Equal(Status.InvalidArgument, error.Status);
        Assert.False(string.IsNullOrWhiteSpace(error.Message));
    }
}
