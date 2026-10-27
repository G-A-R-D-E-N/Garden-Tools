using System;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfPlacementMatrixTests
{
    private static int RootPlacement(SwfFile movie) =>
        movie.Placements().First(item => !item.InsideSprite).TagIndex;

    [Fact]
    public void ReadsTheIdentityMatrix()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var matrix = movie.PlacementMatrix(RootPlacement(movie));
        Assert.False(matrix.HasScale);
        Assert.False(matrix.HasRotate);
        Assert.Equal(0, matrix.TranslateX);
        Assert.Equal(0, matrix.TranslateY);
    }

    [Fact]
    public void MovesThePlacementAndKeepsTheOtherBytes()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var tagIndex = RootPlacement(movie);
        var original = movie.Payload(tagIndex);
        var result = movie.SetPlacementTransform(tagIndex, new SwfPlacementTransformEdit(TranslateX: 100, TranslateY: -50));
        Assert.True(result.Changed);
        var matrix = movie.PlacementMatrix(tagIndex);
        Assert.Equal(100, matrix.TranslateX);
        Assert.Equal(-50, matrix.TranslateY);
        var edited = movie.Payload(tagIndex);
        for (var index = 0; index < 3; ++index) Assert.Equal(original[index], edited[index]);
        Assert.Equal((ushort)1, movie.Placements().First(item => !item.InsideSprite).Depth);
    }

    [Fact]
    public void AddsAScaleWhenTheOriginalMatrixHadNone()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var tagIndex = RootPlacement(movie);
        movie.SetPlacementTransform(tagIndex, new SwfPlacementTransformEdit(ScaleX: 0x10000, ScaleY: 0x8000));
        var matrix = movie.PlacementMatrix(tagIndex);
        Assert.True(matrix.HasScale);
        Assert.Equal(0x10000, matrix.ScaleX);
        Assert.Equal(0x8000, matrix.ScaleY);
    }

    [Fact]
    public void RefusesATagThatIsNotAPlacement()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var label = movie.Tags.First(tag => tag.Code == 43);
        Assert.ThrowsAny<Exception>(() => movie.SetPlacementTransform(label.Index, new SwfPlacementTransformEdit(TranslateX: 1)));
    }
}