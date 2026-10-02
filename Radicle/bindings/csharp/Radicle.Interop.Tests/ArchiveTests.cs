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
}
