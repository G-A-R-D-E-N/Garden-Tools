using System;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class SwfPlacementTests
{
    [Fact]
    public void ReadsTheRootPlacementAndItsDepth()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var placements = movie.Placements();
        var root = Assert.Single(placements.Where(placement => !placement.InsideSprite));
        Assert.Equal(26, root.TagCode);
        Assert.Equal((ushort)1, root.Depth);
        Assert.Equal((ushort)11, root.CharacterId);
    }

    [Fact]
    public void ChangingDepthKeepsEveryOtherPlacementByteAndUndoesCleanly()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var placement = movie.Placements().First(item => !item.InsideSprite);
        var original = movie.Payload(placement.TagIndex);
        var result = movie.SetDepth(placement.TagIndex, 7);
        Assert.True(result.Changed);
        var edited = movie.Payload(placement.TagIndex);
        Assert.Equal(original.Length, edited.Length);
        Assert.Equal((byte)7, edited[1]);
        Assert.Equal((byte)0, edited[2]);
        for (var index = 0; index < original.Length; ++index)
        {
            if (index is 1 or 2) continue;
            Assert.Equal(original[index], edited[index]);
        }
        Assert.Equal((ushort)7, movie.Placements().First(item => !item.InsideSprite).Depth);
        using var historyMovie = SwfFile.FromBytes(SwfFixtures.Movie());
        using var history = historyMovie.OpenHistory();
        history.Apply(new SwfPlanSpec().Replace(placement.TagIndex, edited));
        history.Undo();
        Assert.Equal(original, historyMovie.Payload(placement.TagIndex));
    }

    [Fact]
    public void RefusesATagThatIsNotAPlacement()
    {
        using var movie = SwfFile.FromBytes(SwfFixtures.Movie());
        var label = movie.Tags.First(tag => tag.Code == 43);
        Assert.ThrowsAny<Exception>(() => movie.SetDepth(label.Index, 2));
    }

    [Fact]
    public void ReadsAPlaceObject3CharacterAfterItsClassName()
    {
        var payload = new byte[] { 0x06, 0x08, 0x03, 0x00, (byte)'C', (byte)'l', (byte)'s', 0x00, 0x0B, 0x00, 0x00 };
        var record = SwfPlacementCodec.Parse(new SwfTagRecord(0, 70, "PlaceObject3", (uint)payload.Length, 0, -1, 0, false, false, payload.Length), payload);
        Assert.Equal((ushort)3, record.Depth);
        Assert.Equal((ushort)11, record.CharacterId);
        SwfPlacementMatrixCodec.Read(70, payload, out var matrixStart, out var matrixEnd);
        Assert.Equal(10, matrixStart);
        Assert.Equal(11, matrixEnd);
    }

    [Fact]
    public void IgnoresThePlaceObject3VisibleAndOpaqueFlagsWhenFindingTheCharacter()
    {
        var payload = new byte[] { 0x06, 0x60, 0x03, 0x00, 0x0B, 0x00, 0x00 };
        var record = SwfPlacementCodec.Parse(new SwfTagRecord(0, 70, "PlaceObject3", (uint)payload.Length, 0, -1, 0, false, false, payload.Length), payload);
        Assert.Equal((ushort)11, record.CharacterId);
    }
}