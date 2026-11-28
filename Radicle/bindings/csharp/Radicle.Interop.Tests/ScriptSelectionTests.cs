using System;
using Xunit;

namespace Radicle.Interop.Tests;

public class ScriptSelectionTests
{
    private static ScriptCandidateRow Row(string name, ScriptCandidateKind kind, string container, int loadIndex) =>
        new(name, kind, container, loadIndex, 10);

    [Fact]
    public void LooseBeatsArchiveThenTheHigherLoadIndexThenTheHigherContainer()
    {
        var winners = ScriptSelection.Resolve(new[]
        {
            Row("a", ScriptCandidateKind.Archive, "x.ba2", 5),
            Row("a", ScriptCandidateKind.Loose, string.Empty, 0),
            Row("b", ScriptCandidateKind.Archive, "one.ba2", 1),
            Row("b", ScriptCandidateKind.Archive, "two.ba2", 2),
            Row("c", ScriptCandidateKind.Archive, "a.ba2", 3),
            Row("c", ScriptCandidateKind.Archive, "b.ba2", 3),
        });
        Assert.Equal(new[] { new ScriptWinnerRow("a", 1, 1), new ScriptWinnerRow("b", 3, 1), new ScriptWinnerRow("c", 5, 1) }, winners);
    }

    [Fact]
    public void WinnersAreOrderedByNameWhateverTheCandidateOrder()
    {
        var winners = ScriptSelection.Resolve(new[]
        {
            Row("zeta", ScriptCandidateKind.Loose, string.Empty, 0),
            Row("alpha", ScriptCandidateKind.Loose, string.Empty, 0),
        });
        Assert.Equal(new[] { new ScriptWinnerRow("alpha", 1, 0), new ScriptWinnerRow("zeta", 0, 0) }, winners);
    }

    [Fact]
    public void NonAsciiNamesGroupByTheirUtf8Bytes()
    {
        var winners = ScriptSelection.Resolve(new[]
        {
            Row("café", ScriptCandidateKind.Archive, "x.ba2", 1),
            Row("café", ScriptCandidateKind.Loose, string.Empty, 0),
        });
        Assert.Equal(new[] { new ScriptWinnerRow("café", 1, 1) }, winners);
    }

    [Fact]
    public void ANullNameAndContainerAreTreatedAsEmpty()
    {
        var winners = ScriptSelection.Resolve(new[] { new ScriptCandidateRow(null!, ScriptCandidateKind.Loose, null!, 0, 1) });
        Assert.Equal(new[] { new ScriptWinnerRow(string.Empty, 0, 0) }, winners);
    }

    [Fact]
    public void NoCandidatesResolveToNoWinners()
    {
        Assert.Empty(ScriptSelection.Resolve(Array.Empty<ScriptCandidateRow>()));
    }

    [Fact]
    public void AnUnknownKindIsRefusedAndANullListThrows()
    {
        Assert.Throws<RadicleException>(() => ScriptSelection.Resolve(new[] { Row("a", (ScriptCandidateKind)7, string.Empty, 0) }));
        Assert.Throws<ArgumentNullException>(() => ScriptSelection.Resolve(null!));
    }
}
