using FluentAssertions;

namespace FO4RecordEditor.Core.Tests;

public class Mo2ProfileLoaderMasterTests
{
    [Fact]
    public void EnsuresFallout4IsRegisteredWhenMutagenImplicitMastersOmitIt()
    {
        var order = FO4RecordEditor.Services.Mo2ProfileLoader.EnsureRequiredGameMasters(
            new[] { "DLCCoast.esm", "SomeMod.esp" });

        order[0].Should().Be("Fallout4.esm");
        order.Should().ContainInOrder("Fallout4.esm", "DLCCoast.esm", "SomeMod.esp");
    }

    [Fact]
    public void MovesFallout4ToTheFrontWhenPresentButNotFirst()
    {
        var order = FO4RecordEditor.Services.Mo2ProfileLoader.EnsureRequiredGameMasters(
            new[] { "DLCCoast.esm", "Fallout4.esm", "SomeMod.esp" });

        order[0].Should().Be("Fallout4.esm");
        order.Should().ContainInOrder("Fallout4.esm", "DLCCoast.esm", "SomeMod.esp");
        order.Should().HaveCount(3, "the existing entry is relocated, not duplicated");
    }

    [Fact]
    public void CollapsesDuplicateAndMiscasedFallout4EntriesToOneAtTheFront()
    {
        var order = FO4RecordEditor.Services.Mo2ProfileLoader.EnsureRequiredGameMasters(
            new[] { "FALLOUT4.ESM", "SomeMod.esp", "fallout4.esm" });

        order.Should().Equal("Fallout4.esm", "SomeMod.esp");
    }

    [Fact]
    public void LeavesAnAlreadyCorrectOrderUnchanged()
    {
        var order = FO4RecordEditor.Services.Mo2ProfileLoader.EnsureRequiredGameMasters(
            new[] { "Fallout4.esm", "DLCCoast.esm", "SomeMod.esp" });

        order.Should().Equal("Fallout4.esm", "DLCCoast.esm", "SomeMod.esp");
    }

    [Fact]
    public void AddsFallout4ToAnEmptyLoadOrder()
    {
        var order = FO4RecordEditor.Services.Mo2ProfileLoader.EnsureRequiredGameMasters(
            System.Array.Empty<string>());

        order.Should().Equal("Fallout4.esm");
    }
}
