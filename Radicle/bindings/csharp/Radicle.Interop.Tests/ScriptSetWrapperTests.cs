using Radicle.Interop.Native;
using System.Linq;
using Xunit;

namespace Radicle.Interop.Tests;

public class ScriptSetWrapperTests
{
    [Fact]
    public void ResolveReadsTheWinnersOfAnAssetHandle()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/SyntheticObject.pex", PexModelTests.SyntheticPex());
        using var assets = AssetResolver.Open(data, System.Array.Empty<string>());
        using var set = ScriptSet.Resolve(assets);
        Assert.Equal(new[] { "syntheticobject" }, set.Winners.Select(w => w.Name));
    }

    [Fact]
    public void AResolvedSetKeepsReadingAfterTheCallerDisposesItsAssetHandle()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        var pex = PexModelTests.SyntheticPex();
        temp.Write("Data/Scripts/SyntheticObject.pex", pex);
        var assets = AssetResolver.Open(data, System.Array.Empty<string>());
        using var set = ScriptSet.Resolve(assets);
        assets.Dispose();
        Assert.Equal(pex, set.Read(0));
        Assert.Equal(new[] { "syntheticobject" }, set.Winners.Select(w => w.Name));
    }

    [Fact]
    public void ResolveRefusesADisposedAssetHandle()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/SyntheticObject.pex", PexModelTests.SyntheticPex());
        var assets = AssetResolver.Open(data, System.Array.Empty<string>());
        assets.Dispose();
        Assert.Throws<System.ObjectDisposedException>(() => ScriptSet.Resolve(assets));
    }

    [Fact]
    public void BuildNativeSurfaceOfACodeFreeScriptHasNoRows()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/SyntheticObject.pex", PexModelTests.SyntheticPex());
        using var set = ScriptSet.Open(new[] { data }, System.Array.Empty<string>());
        var surface = set.BuildNativeSurface(new[] { "SyntheticObject", "Missing" }, new[] { "Game.GetPlayer" });
        Assert.Empty(surface.Rows);
    }

    [Fact]
    public void BuildNativeSurfaceListsEachCalledNativeAndMarksTheBoundOnes()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/Probe.pex", PapyrusProbe.Compile());
        using var set = ScriptSet.Open(new[] { data }, System.Array.Empty<string>());
        var surface = set.BuildNativeSurface(new[] { "Probe" }, new[] { "probe.hit" });
        Assert.Empty(surface.Unresolved);
        var rows = surface.Rows.OrderBy(row => row.Native.ToLowerInvariant()).ToArray();
        Assert.Equal(new[] { "hit", "wait" }, rows.Select(row => row.Native.ToLowerInvariant()));
        Assert.Equal(new[] { true, false }, rows.Select(row => row.BoundInHost));
        Assert.Equal(new[] { 1, 1 }, rows.Select(row => row.CallCount));
        Assert.All(rows, row => Assert.Equal("probe", row.DeclaringClass, ignoreCase: true));
        Assert.All(rows, row => Assert.Equal("probe", row.Script, ignoreCase: true));
    }

    [Fact]
    public void BuildNativeSurfaceReportsACallWithNoResolvedDeclaration()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/SyntheticObject.pex", PexModelTests.SyntheticPex(withCode: true));
        using var set = ScriptSet.Open(new[] { data }, System.Array.Empty<string>());
        var surface = set.BuildNativeSurface(new[] { "SyntheticObject" }, System.Array.Empty<string>());
        Assert.Empty(surface.Rows);
        var text = Assert.Single(surface.Unresolved);
        Assert.Contains("no resolved declaration for ObjectReference.SyntheticObject", text);
    }

    [Fact]
    public void NormalizeIgnoresCaseAndSeparatorStyle()
    {
        Assert.Equal(ScriptNames.Normalize("Scripts/Foo.pex"), ScriptNames.Normalize("scripts\\FOO.PEX"));
    }
}
