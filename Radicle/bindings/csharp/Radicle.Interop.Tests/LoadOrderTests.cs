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
    public void ReadsPluginListAndCccWithBomCrlfCommentsAndDuplicates()
    {
        using var temp = new TempDirectory();
        var list = temp.Write("plugins.txt", Encoding.UTF8.GetBytes("\uFEFF# comment\r\n*Base.esm\r\nx\r\nMod.esp\r\n*Base.esm\r\n"));
        var ccc = temp.Write("Fallout4.ccc", Encoding.UTF8.GetBytes("\uFEFFcc_b.esl \r\n\r\ncc_a.esl\n"));

        Assert.Equal(new[] { new PluginListEntry("Base.esm", true, 2), new PluginListEntry("Mod.esp", false, 4) },
                     LoadOrderFiles.ReadPluginList(list).ToArray());
        Assert.Equal(new[] { "cc_b.esl", "cc_a.esl" }, LoadOrderFiles.ReadCcc(ccc).ToArray());
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
}
