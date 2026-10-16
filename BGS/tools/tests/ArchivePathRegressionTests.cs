using System;
using OpenCommonwealth.Services.Archive;
using Xunit;

namespace BehaviourStudio.Tests;

public sealed class ArchivePathRegressionTests
{
    [Fact]
    public void ArchiveNamesWithColonAreRejectedBeforeExtraction()
    {
        using var root = new TestArchives.TempDirectory("bgs-ba2-colon");
        string archive = Path.Combine(root.Path, "colon.ba2");
        TestArchives.WriteBa2(archive, "a.hkx:stream", new byte[] { 1, 2, 3 });

        var error = Assert.Throws<InvalidDataException>(() => Ba2.Open(archive));
        Assert.Contains("path with a colon", error.Message, StringComparison.Ordinal);
    }
}
