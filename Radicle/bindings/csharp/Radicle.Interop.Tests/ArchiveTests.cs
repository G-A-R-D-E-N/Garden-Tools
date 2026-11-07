using System;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.Bytes;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class ArchiveTests
{
    [Fact]
    public void ReadsArchivesWithGamePrecedenceAndArchivedStrings()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        var plugin = temp.Write("Data/Base.esm", Build(0x81, System.Array.Empty<string>(),
            Top("WRLD", new Rec("WRLD", 0x800, new[] { ("FULL", U32(0x10)) }))));
        var low = temp.Write("Data/Base - Main.ba2", Ba2Writer.General(
            ("Meshes\\Shared.nif", Encoding.ASCII.GetBytes("low")),
            ("Strings\\Base_en.STRINGS", StringsWriter.Table((0x10, "Archived World")))));
        var high = temp.Write("Data/Base - Textures.ba2", Ba2Writer.General(("meshes\\shared.nif", Encoding.ASCII.GetBytes("high"))));
        temp.Write("Data/Meshes/Loose.nif", Encoding.ASCII.GetBytes("loose"));

        var order = AssetResolver.ArchiveLoadOrder(data, new[] { "Missing.ba2" }, new[] { "Base.esm" });
        Assert.Equal(new[] { "Base - Main.ba2", "Base - Textures.ba2" }, order.Select(System.IO.Path.GetFileName));

        using (var archive = Ba2Archive.Open(low))
        {
            Assert.Equal(Ba2Kind.General, archive.Kind);
            Assert.Equal(2, archive.Count);
            Assert.Equal("Meshes\\Shared.nif", archive.Entry(0).Path);
            var index = archive.Find("meshes/SHARED.nif");
            Assert.Equal(0, index);
            Assert.Equal("low", Encoding.ASCII.GetString(archive.Extract(index!.Value)));
            Assert.Null(archive.Find("meshes/none.nif"));
        }

        using var assets = AssetResolver.Open(data, order);
        Assert.Equal(2, assets.ArchiveCount);
        Assert.Equal(2, assets.Archive(0).Count);
        Assert.Equal(new AssetLocation(AssetOrigin.Archive, 1, 0), assets.Locate("Meshes/Shared.nif"));
        Assert.Equal("high", Encoding.ASCII.GetString(assets.Read("meshes\\shared.nif")!));
        Assert.Equal(AssetOrigin.Loose, assets.Locate("meshes/loose.nif")!.Value.Origin);
        Assert.Null(assets.Read("meshes/none.nif"));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => assets.Locate("../x")).Status);

        using var env = RadicleEnvironment.Load(new[] { plugin });
        using var strings = StringTables.Load(env, System.IO.Path.Combine(data, "Strings"), "en", assets);
        Assert.EndsWith("Strings/Base_en.STRINGS", strings.TablePath(0, StringSource.Normal)!.Replace('\\', '/'));
        Assert.Equal("Archived World", Encoding.Latin1.GetString(strings.ResolveName(env.Winner(new FormKey("Base.esm", 0x800))!.Value)!));
        Assert.Null(strings.Lookup(0, StringSource.Dl, 0x10));
        Assert.Equal(StringSource.Il, StringTables.SourceFor("INFO", "NAM1"));
    }
    [Fact]
    public void WriterEmitsReadableArchivesWithPathHashesAndRefusesUnknownVersions()
    {
        using var temp = new TempDirectory();
        var big = new byte[4096];
        Array.Fill(big, (byte)'m');
        var files = new[] { ("Meshes\\Gun.nif", Encoding.ASCII.GetBytes("gun")), ("Materials\\Gun.bgsm", big) };
        var path = temp.Write("Data/Written - Main.ba2", Ba2Writer.General(files, version: 8, compress: true));

        using var archive = Ba2Archive.Open(path);
        Assert.Equal(8u, archive.Version);
        Assert.Equal(new[] { "Meshes\\Gun.nif", "Materials\\Gun.bgsm" }, archive.Entries.Select(e => e.Path));
        Assert.All(archive.Entries, e => Assert.NotEqual(0u, e.NameHash));
        Assert.Equal(big, archive.Extract(archive.Find("materials/gun.bgsm")!.Value));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => Ba2Writer.General(files, version: 5, compress: false)).Status);
    }

    [Fact]
    public void WriterRefusesNullFileDataBeforePinning()
    {
        var files = new (string Path, byte[] Data)[] { ("Meshes\\Gun.nif", null!) };
        Assert.Throws<ArgumentException>(() => Ba2Writer.General(files, 1, false));
    }

    [Fact]
    public void WriterRefusesNullFileListAndPathWithArgumentExceptions()
    {
        Assert.Throws<ArgumentNullException>(() => Ba2Writer.General(null!));
        var files = new (string Path, byte[] Data)[] { (null!, Encoding.ASCII.GetBytes("x")) };
        Assert.Throws<ArgumentException>(() => Ba2Writer.General(files, 1, false));
    }

    [Fact]
    public void WriterWritesHeaderOnlyArchiveForNoFiles()
    {
        using var temp = new TempDirectory();
        var bytes = Ba2Writer.General(Array.Empty<(string Path, byte[] Data)>(), 1, false);
        Assert.Equal(new byte[] { 0x42, 0x54, 0x44, 0x58, 1, 0, 0, 0, 0x47, 0x4E, 0x52, 0x4C, 0, 0, 0, 0, 0x18, 0, 0, 0, 0, 0, 0, 0 }, bytes);
        using var archive = Ba2Archive.Open(temp.Write("Data/Empty.ba2", bytes));
        Assert.Equal(1u, archive.Version);
        Assert.Equal(0, archive.Count);
    }

    [Fact]
    public void OpenRootsLayersLooseRootsHighToLowOverArchives()
    {
        using var temp = new TempDirectory();
        var archive = temp.Write("Data/Base - Main.ba2", Ba2Writer.General(("meshes\\a.nif", Encoding.ASCII.GetBytes("arc"))));
        temp.Write("ModHigh/Meshes/A.nif", Encoding.ASCII.GetBytes("high"));
        temp.Write("ModLow/Meshes/A.nif", Encoding.ASCII.GetBytes("low"));
        var roots = new[] { System.IO.Path.Combine(temp.Path, "ModHigh"), System.IO.Path.Combine(temp.Path, "ModLow") };

        using var assets = AssetResolver.OpenRoots(roots, new[] { archive });
        var all = assets.List("meshes", ".nif", includeShadowed: true);
        Assert.Equal(new[] { false, true, true }, all.Select(a => a.Shadowed));
        Assert.Equal(new[] { AssetOrigin.Loose, AssetOrigin.Loose, AssetOrigin.Archive }, all.Select(a => a.Location.Origin));
        Assert.Equal(4UL, all[0].Size);
    }

    [Fact]
    public void ListsWinningAssetsWithOriginSizeAndShadowedCopies()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        var low = temp.Write("Data/Base - Main.ba2", Ba2Writer.General(
            ("Meshes\\Shared.nif", Encoding.ASCII.GetBytes("low")),
            ("Meshes\\Only.nif", Encoding.ASCII.GetBytes("only")),
            ("Textures\\T.dds", Encoding.ASCII.GetBytes("t"))));
        var high = temp.Write("Data/Base - Textures.ba2", Ba2Writer.General(("meshes\\shared.nif", Encoding.ASCII.GetBytes("high"))));
        temp.Write("Data/Meshes/Shared.NIF", Encoding.ASCII.GetBytes("loose!"));

        using var assets = AssetResolver.Open(data, new[] { low, high });
        var winners = assets.List("meshes", ".nif");
        Assert.Equal(new[] { "meshes/only.nif", "meshes/shared.nif" }, winners.Select(w => w.Path));
        Assert.Equal(new AssetLocation(AssetOrigin.Archive, 0, 1), winners[0].Location);
        Assert.Equal((AssetOrigin.Loose, 6UL, false), (winners[1].Location.Origin, winners[1].Size, winners[1].Shadowed));

        var all = assets.List("meshes", ".nif", includeShadowed: true);
        Assert.Equal(new[] { false, false, true, true },
            all.Select(a => a.Shadowed));
        Assert.Equal(new[] { AssetOrigin.Loose, AssetOrigin.Archive, AssetOrigin.Archive },
            all.Skip(1).Select(a => a.Location.Origin));
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => assets.List("../x")).Status);
    }

    [Fact]
    public void ScriptSetOpensWithoutAnAssetHandleAndReportsSkippedArchives()
    {
        using var temp = new TempDirectory();
        var data = System.IO.Path.Combine(temp.Path, "Data");
        temp.Write("Data/Scripts/SyntheticObject.pex", PexModelTests.SyntheticPex());
        temp.Write("Data/Scripts/Junk.pex", Encoding.ASCII.GetBytes("not a pex"));
        var good = temp.Write("Mods/Good - Main.ba2", Ba2Writer.General(("Scripts\\Other.pex", Encoding.ASCII.GetBytes("other"))));
        var broken = temp.Write("Mods/Broken.ba2", Encoding.ASCII.GetBytes("not a ba2"));

        Assert.Throws<RadicleException>(() => ScriptSet.Open(new[] { data }, new[] { good, broken }));

        using var set = ScriptSet.Open(new[] { data }, new[] { good, broken }, ArchiveFailurePolicy.Skip);
        Assert.Equal(new[] { "junk", "other", "syntheticobject" }, set.Winners.Select(w => w.Name));
        var failure = Assert.Single(set.Failures);
        Assert.Equal(1, failure.ArchiveIndex);
        Assert.Equal(broken, failure.Path);
        Assert.NotEmpty(failure.Message);
        Assert.Equal("other", Encoding.ASCII.GetString(set.Read(1)));

        Assert.False(set.ParentChainValid("SyntheticObject"));
        Assert.False(set.ParentChainValid("Junk"));
        Assert.False(set.ParentChainValid("Missing"));
        var census = set.Census();
        Assert.Equal(1, census.Objects);
        Assert.Equal(new[] { "junk", "other" }, census.Unreadable);
        Assert.Empty(census.Sites);
    }
}
