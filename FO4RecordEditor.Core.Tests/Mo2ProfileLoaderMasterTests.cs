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
}
