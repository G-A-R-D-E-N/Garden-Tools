using System;
using System.IO;
using System.Linq;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

public class RadicleNativeTests
{
    [Fact]
    public void TheBaseDirectoryCandidateComesFirst()
    {
        var paths = RadicleNative.CandidatePaths("C:\\app", "C:\\work");
        Assert.Equal(Path.Combine("C:\\app", RadicleNative.LibraryName), paths[0]);
        Assert.Contains(Path.Combine("C:\\app", "native", RadicleNative.LibraryName), paths);
        Assert.Contains(Path.Combine("C:\\app", "runtimes", "win-x64", "native", RadicleNative.LibraryName), paths);
    }

    [Fact]
    public void TheProbeFindsADeployedLibraryAndStopsAtNothing()
    {
        var root = Path.Combine(Path.GetTempPath(), "pet-radicle-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "native"));
            var deployed = Path.Combine(root, "native", RadicleNative.LibraryName);
            File.WriteAllBytes(deployed, Array.Empty<byte>());
            var found = RadicleNative.FindFirstExisting(RadicleNative.CandidatePaths(root, null));
            Assert.Equal(deployed, found);
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void TheProbeReturnsNothingWhenNoCandidateExists()
    {
        var root = Path.Combine(Path.GetTempPath(), "pet-radicle-missing-" + Guid.NewGuid().ToString("N"));
        Assert.Null(RadicleNative.FindFirstExisting(RadicleNative.CandidatePaths(root, null)));
    }
}