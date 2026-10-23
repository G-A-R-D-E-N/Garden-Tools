using System.IO;
using System.Linq;
using System.Text;
using Radicle.Interop.Native;
using Xunit;
using static Radicle.Interop.Tests.PluginWriter;

namespace Radicle.Interop.Tests;

public class LoadOrderTests
{
    [Fact]
    public void ReadsPluginListAndCccWithBomKeptCrlfCommentsAndDuplicates()
    {
        using var temp = new TempDirectory();
        var list = temp.Write("plugins.txt", Encoding.UTF8.GetBytes("\uFEFF*Base.esm\r\n# comment\r\nx\r\nMod.esp\r\n*mod.esp\r\n"));
        var ccc = temp.Write("Fallout4.ccc", Encoding.UTF8.GetBytes("\uFEFFcc_b.esl \r\n\r\ncc_a.esl\n"));

        Assert.Equal(new[] { new PluginListEntry("ï»¿*Base.esm", false, 1), new PluginListEntry("x", false, 3), new PluginListEntry("Mod.esp", true, 4) },
                     LoadOrderFiles.ReadPluginList(list).ToArray());
        Assert.Equal(new[] { "ï»¿cc_b.esl ", "cc_a.esl" }, LoadOrderFiles.ReadCcc(ccc).ToArray());
    }

    [Fact]
    public void ReadPluginListSkipsNamesWithControlCharactersWithoutThrowing()
    {
        using var temp = new TempDirectory();
        var list = temp.Write("plugins.txt", Encoding.ASCII.GetBytes("*Good.esp\r\n*Bad\tName.esp\r\nLast.esp\r\n"));

        Assert.Equal(new[] { new PluginListEntry("Good.esp", true, 1), new PluginListEntry("Last.esp", false, 3) },
                     LoadOrderFiles.ReadPluginList(list).ToArray());
    }

    [Fact]
    public void ReadPluginListSplitsTrailingTabFieldsFromTheRight()
    {
        using var temp = new TempDirectory();
        var comments = string.Concat(Enumerable.Repeat("# note\r\n", 10));
        var list = temp.Write("plugins.txt", Encoding.ASCII.GetBytes(comments + "*Mod With Spaces.esp\r\nLate.esp\r\n"));

        Assert.Equal(new[] { new PluginListEntry("Mod With Spaces.esp", true, 11), new PluginListEntry("Late.esp", false, 12) },
                     LoadOrderFiles.ReadPluginList(list).ToArray());
    }

    [Fact]
    public void AssemblesOfficialThenCccThenPluginListWithHoistAndChecks()
    {
        using var temp = new TempDirectory();
        var data = Path.Combine(temp.Path, "Data");
        temp.Write("Data/Fallout4.esm", Build(0x1, System.Array.Empty<string>()));
        temp.Write("Data/DLCRobot.esm", Build(0x1, new[] { "Fallout4.esm" }));
        temp.Write("Data/cc_a.esl", Build(0, System.Array.Empty<string>()));
        temp.Write("Data/Mod.esp", Build(0, new[] { "DLCRobot.esm" }));
        var list = temp.Write("plugins.txt", Encoding.ASCII.GetBytes("*Mod.esp\r\n"));
        var ccc = temp.Write("Fallout4.ccc", Encoding.ASCII.GetBytes("cc_a.esl\r\nGone.esl\r\n"));

        var order = LoadOrderFiles.Assemble(data, list, ccc, OfficialTable.OgNg);

        Assert.False(order.Failed);
        Assert.Equal(new[] { "Fallout4.esm", "DLCRobot.esm", "cc_a.esl", "Mod.esp" },
                     order.Entries.Select(entry => entry.Name).ToArray());
        Assert.All(order.Entries, entry => Assert.True(entry.Checked));
        Assert.Contains(order.Findings, finding => finding.Code == "missing_file" && finding.Plugin == "Gone.esl");
    }

    [Fact]
    public void ReportsTooNewMasterAndSilentlyUncheckedTooNewPlugin()
    {
        using var temp = new TempDirectory();
        var data = Path.Combine(temp.Path, "Data");
        temp.Write("Data/Old.esp", Build(0, 1.5f, System.Array.Empty<string>(), System.Array.Empty<Node>()));
        temp.Write("Data/Child.esp", Build(0, 1.0f, new[] { "Old.esp" }, System.Array.Empty<Node>()));
        temp.Write("Data/Bad.esp", Build(0, 1.5f, System.Array.Empty<string>(), System.Array.Empty<Node>()));
        var list = temp.Write("plugins.txt", Encoding.ASCII.GetBytes("*Child.esp\r\n"));

        var tooNew = LoadOrderFiles.Assemble(data, list, null, OfficialTable.Ae);
        Assert.True(tooNew.Failed);
        Assert.Contains(tooNew.Findings, finding => finding.Code == "master_too_new" && finding.Plugin == "Child.esp");

        var list2 = temp.Write("plugins2.txt", Encoding.ASCII.GetBytes("*Bad.esp\r\n"));
        var uncheckedPlugin = LoadOrderFiles.Assemble(data, list2, null, OfficialTable.Ae);
        Assert.False(uncheckedPlugin.Failed);
        Assert.False(uncheckedPlugin.Entries.Single(entry => entry.Name == "Bad.esp").Checked);
        Assert.Contains(uncheckedPlugin.Findings, finding => finding.Code == "version_unchecked" && finding.Plugin == "Bad.esp");
    }

    [Fact]
    public void ActivePluginWithMissingMasterIsNeverCheckedAndMutualMastersAreReported()
    {
        using var temp = new TempDirectory();
        var data = Path.Combine(temp.Path, "Data");
        temp.Write("Data/Orphan.esp", Build(0, new[] { "Missing.esm" }));
        var list = temp.Write("plugins.txt", Encoding.ASCII.GetBytes("*Orphan.esp\r\n"));

        var orphan = LoadOrderFiles.Assemble(data, list, null, OfficialTable.OgNg);
        Assert.False(orphan.Failed);
        Assert.False(orphan.Entries.Single().Checked);

        temp.Write("Data/A.esm", Build(0x1, new[] { "B.esm" }));
        temp.Write("Data/B.esm", Build(0x1, new[] { "A.esm" }));
        var cycle = LoadOrderFiles.Assemble(data, null, null, OfficialTable.OgNg);
        Assert.True(cycle.Failed);
        Assert.Contains(cycle.Findings, finding => finding.Code == "hoist_cycle");
    }

    [Fact]
    public void ReadPluginListAndCccOfAMissingFileThrowIoError()
    {
        using var temp = new TempDirectory();
        var missing = Path.Combine(temp.Path, "none.txt");

        Assert.Equal(Status.IoError, Assert.Throws<RadicleException>(() => LoadOrderFiles.ReadPluginList(missing)).Status);
        Assert.Equal(Status.IoError, Assert.Throws<RadicleException>(() => LoadOrderFiles.ReadCcc(missing)).Status);
    }

    [Fact]
    public void AssembleRefusesANonDirectoryAndAnUnknownTable()
    {
        using var temp = new TempDirectory();
        var file = temp.Write("plain.txt", Encoding.ASCII.GetBytes("not a directory"));

        Assert.Equal(Status.IoError, Assert.Throws<RadicleException>(() => LoadOrderFiles.Assemble(file, null, null, OfficialTable.OgNg)).Status);
        Assert.Equal(Status.InvalidArgument, Assert.Throws<RadicleException>(() => LoadOrderFiles.Assemble(temp.Path, null, null, (OfficialTable)7)).Status);
    }

    [Fact]
    public void AssembleWithAMissingPluginListOrCccPathThrowsIoError()
    {
        using var temp = new TempDirectory();
        var missing = Path.Combine(temp.Path, "missing.txt");

        Assert.Equal(Status.IoError, Assert.Throws<RadicleException>(() => LoadOrderFiles.Assemble(temp.Path, missing, null, OfficialTable.Ae)).Status);
        Assert.Equal(Status.IoError, Assert.Throws<RadicleException>(() => LoadOrderFiles.Assemble(temp.Path, null, missing, OfficialTable.OgNg)).Status);
    }

    [Fact]
    public void ReadsMo2InstanceAndProfileFromAPortableFixture()
    {
        using var temp = new TempDirectory();
        temp.Write("ModOrganizer.ini", Encoding.UTF8.GetBytes("selected_profile=Test\r\nbase_directory=%BASE_DIR%\r\n"));
        temp.Write("Stock Folder/Data/Fallout4.esm", System.Array.Empty<byte>());
        temp.Write("profiles/Test/modlist.txt", Encoding.UTF8.GetBytes("+High\r\n-Off\r\n+Tools_separator\r\n"));
        temp.Write("profiles/Test/plugins.txt", Encoding.ASCII.GetBytes("*Mod.esp\r\n"));

        var instance = LoadOrderFiles.ReadMo2Instance(temp.Path);
        var profile = LoadOrderFiles.ReadMo2Profile(temp.Path, null);

        Assert.Equal("Test", instance.SelectedProfile);
        Assert.Equal(Path.Combine(temp.Path, "Stock Folder", "Data"), instance.DataDir);
        Assert.Equal(Path.Combine(temp.Path, "mods"), instance.ModsDir);
        Assert.Equal(new[] { new Mo2Mod("High", true, false), new Mo2Mod("Off", false, false), new Mo2Mod("Tools_separator", true, true) },
                     profile.ModsHighToLow);
        Assert.Equal(new[] { new PluginListEntry("Mod.esp", true, 1) }, profile.Plugins);
    }

    [Fact]
    public void ResolvesAssetRootsAndPluginPathsWithTheHigherModWinning()
    {
        using var temp = new TempDirectory();
        temp.Write("ModOrganizer.ini", Encoding.UTF8.GetBytes("base_directory=%BASE_DIR%\r\n"));
        temp.Write("profiles/Test/modlist.txt", Encoding.UTF8.GetBytes("+High\r\n+Low\r\n-Ghost\r\n"));
        temp.Write("profiles/Test/plugins.txt", Encoding.ASCII.GetBytes("*Shared.esp\r\n"));
        temp.Write("mods/Low/Shared.esp", System.Array.Empty<byte>());
        temp.Write("mods/High/Shared.esp", System.Array.Empty<byte>());
        temp.Write("mods/Ghost/Ghost.esp", System.Array.Empty<byte>());

        var roots = LoadOrderFiles.ReadMo2AssetRoots(temp.Path, "Test");
        var paths = LoadOrderFiles.ReadMo2PluginPaths(temp.Path, "Test");

        Assert.Equal(new[] { Path.Combine(temp.Path, "mods", "Low"), Path.Combine(temp.Path, "mods", "High"), Path.Combine(temp.Path, "overwrite") },
                     roots);
        Assert.Equal(new[] { "shared.esp" }, paths.Keys.ToArray());
        Assert.Equal(Path.Combine(temp.Path, "mods", "High", "Shared.esp"), paths["shared.esp"]);
    }
}
