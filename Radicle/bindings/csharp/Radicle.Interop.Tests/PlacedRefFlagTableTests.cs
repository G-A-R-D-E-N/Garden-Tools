using System;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class PlacedRefFlagTableTests
{
    [Fact]
    public void BitNineMeansADifferentFlagPerBaseType()
    {
        Assert.Equal("Hidden From Local Map", PlacedRefFlagTable.For("REFR", "STAT").Single(f => f.Bit == 9).Name);
        Assert.Equal("Motion Blur", PlacedRefFlagTable.For("REFR", "MSTT").Single(f => f.Bit == 9).Name);
        Assert.Equal("Casts Shadows", PlacedRefFlagTable.For("REFR", "LIGH").Single(f => f.Bit == 9).Name);
        Assert.DoesNotContain(PlacedRefFlagTable.For("REFR", "ZZZZ"), f => f.Bit == 9);
    }

    [Fact]
    public void PlacedNpcRowsIgnoreTheBaseAndMissingBaseIsTheDefaultTable()
    {
        Assert.Equal(new[] { 9, 10, 11, 13, 25, 29 }, PlacedRefFlagTable.For("ACHR", "NPC_").Select(f => f.Bit));
        Assert.Equal("Starts Unconscious", PlacedRefFlagTable.For("ACHR", null).Single(f => f.Bit == 13).Name);
        Assert.Equal(PlacedRefFlagTable.For("REFR", "ZZZZ"), PlacedRefFlagTable.For("REFR", null));
        Assert.Equal("Don't Havok Settle", PlacedRefFlagTable.For("REFR", "STAT").Single(f => f.Bit == 29).Name);
    }

    [Fact]
    public void BadArgumentsAreRefused()
    {
        Assert.Throws<ArgumentNullException>(() => PlacedRefFlagTable.For(null!, "STAT"));
        Assert.Throws<ArgumentException>(() => PlacedRefFlagTable.For("REF\0R", "STAT"));
    }
}
