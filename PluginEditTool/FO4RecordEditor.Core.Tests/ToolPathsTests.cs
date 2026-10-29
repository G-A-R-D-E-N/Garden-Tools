using System;
using System.IO;
using FO4RecordEditor.Services;
using Xunit;

namespace FO4RecordEditor.Core.Tests;

public class ToolPathsTests
{
    [Fact]
    public void APathInsideARootIsAcceptedAndAnyOtherPathIsRefused()
    {
        var root = Path.Combine(Path.GetTempPath(), "pet-roots");
        var inside = Path.Combine(root, "Data", "Interface", "a.swf");
        Assert.Equal(Path.GetFullPath(inside), ToolPaths.RequireUnderRoots(inside, new[] { root }));
        Assert.Throws<UnauthorizedAccessException>(() =>
            ToolPaths.RequireUnderRoots(root + "2" + Path.DirectorySeparatorChar + "a.swf", new[] { root }));
        Assert.Throws<UnauthorizedAccessException>(() =>
            ToolPaths.RequireUnderRoots(Path.Combine(root, "..", "escape.swf"), new[] { root }));
        Assert.Throws<UnauthorizedAccessException>(() =>
            ToolPaths.RequireUnderRoots(inside, Array.Empty<string>()));
    }
}
