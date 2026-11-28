using System;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class RecordFlagTableTests
{
    [Fact]
    public void IsMarkerIsBitTwentyThreeOnTheTablesThatHaveIt()
    {
        foreach (var signature in new[] { "ACTI", "DOOR", "FURN", "STAT" })
        {
            Assert.Equal(23, RecordFlagTable.For(signature).Single(f => f.Name == "Is Marker").Bit);
        }
        Assert.Equal("Headtrack Marker", RecordFlagTable.For("STAT").Single(f => f.Bit == 10).Name);
    }

    [Fact]
    public void OtherSignaturesGetOnlyTheCommonRows()
    {
        Assert.Equal(new[] { 5, 12 }, RecordFlagTable.For("MSTT").Select(f => f.Bit));
        Assert.Equal("Deleted", RecordFlagTable.For("MSTT").Single(f => f.Bit == 5).Name);
    }

    [Fact]
    public void EnableParentBitZeroIsTheOppositeFlag()
    {
        var rows = EnableParentFlagTable.Rows();
        Assert.Equal(new[] { 0, 1 }, rows.Select(f => f.Bit));
        Assert.Equal("Set Enable State to Opposite of Parent", rows[0].Name);
        Assert.Equal("Pop In", rows[1].Name);
    }

    [Fact]
    public void BadArgumentsAreRefused()
    {
        Assert.Throws<ArgumentNullException>(() => RecordFlagTable.For(null!));
        Assert.Throws<ArgumentException>(() => RecordFlagTable.For("ST\0AT"));
    }

    [Fact]
    public void RecordClassNamesFollowTheEnginesIdentifiersAndLeaveOmodUnnamed()
    {
        Assert.Equal("PlacedObject", Vocabulary.ClassName("REFR"));
        Assert.Equal("Holotape", Vocabulary.ClassName("NOTE"));
        Assert.Equal("Npc", Vocabulary.ClassName("NPC_"));
        Assert.Null(Vocabulary.ClassName("OMOD"));
        Assert.Null(Vocabulary.ClassName("XXXX"));
        Assert.Equal(new SignatureTraits(true, false), Vocabulary.Traits("OMOD"));
        Assert.Equal(new SignatureTraits(false, true), Vocabulary.Traits("PGRE"));
        Assert.Equal(new SignatureTraits(false, false), Vocabulary.Traits("REFR"));
    }

    [Fact]
    public void MarkerTypesAreIdentifiersNotXeditLabels()
    {
        Assert.Equal("DiamondCity", Vocabulary.Name(Native.VocabTable.MarkerType, 2));
        Assert.Equal("FactoryOrIndustrialSite", Vocabulary.Name(Native.VocabTable.MarkerType, 4));
        Assert.StartsWith("MechanistLair", Vocabulary.Name(Native.VocabTable.MarkerType, 65));
        Assert.Null(Vocabulary.Name(Native.VocabTable.MarkerType, 66));
        Assert.Equal("ParentActivateOnly", Vocabulary.Name(Native.VocabTable.ActivateParentFlags, 1));
        Assert.Null(Vocabulary.Name(Native.VocabTable.ActivateParentFlags, 0));
    }

    [Fact]
    public void SignaturesMustBeFourAsciiCharacters()
    {
        Assert.Equal(0x52464552u, Vocabulary.Pack("REFR"));
        Assert.Throws<ArgumentException>(() => Vocabulary.Pack("REF"));
        Assert.Throws<ArgumentException>(() => Vocabulary.Pack("RE\u00C9R"));
        Assert.Throws<ArgumentNullException>(() => Vocabulary.Traits(null!));
    }
}
