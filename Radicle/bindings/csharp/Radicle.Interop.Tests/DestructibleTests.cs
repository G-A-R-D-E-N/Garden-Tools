using System.IO;
using System.Linq;
using Radicle.Interop.Native;
using Xunit;

namespace Radicle.Interop.Tests;

public class DestructibleTests
{
    // Constraint: the managed surface has no scalar text getter, so the value checks live in the
// Constraint: native CApiDestructible test that reads the reloaded DEST/DSTD bytes; here the reloads are proved by the counted stage rows and the mutator statuses across a real write/read cycle.
    private static void CrudAndReload(string signature, string tag)
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, signature + tag + ".esp");
        var percent = signature + "\\DestructionStageHealthPercent";
        FormKey key;

        using (var plugin = PluginFile.Create(signature + tag + ".esp"))
        {
            key = plugin.CreateRecord(signature, "Destr" + tag);
            Assert.Equal(DestructibleHeaderStatus.Updated, plugin.InsertDestructibleHeader(key));
            Assert.Equal(DestructibleHeaderStatus.Present, plugin.InsertDestructibleHeader(key));
            Assert.True(plugin.SetScalar(key, signature + "\\DestructionHealth", "750"));
            Assert.Equal(DestructibleStageStatus.Updated, plugin.InsertDestructibleStage(key, 0));
            Assert.Equal(DestructibleStageStatus.Updated, plugin.InsertDestructibleStage(key, 1));
            Assert.True(plugin.SetScalar(key, percent, "25", 0));
            Assert.True(plugin.SetScalar(key, percent, "60", 1));
            Assert.Equal(DestructibleStageMemberStatus.Updated,
                plugin.InsertDestructibleStageMember(key, 0, "DSTA"));
            Assert.Equal(2, plugin.FieldCount(key, percent));
            plugin.Write(path);
        }

        using (var plugin = PluginFile.Read(path))
        {
            Assert.Equal(2, plugin.FieldCount(key, percent));
            Assert.Equal(DestructibleStageStatus.Updated, plugin.RemoveDestructibleStage(key, 0));
            plugin.Write(path);
        }

        using (var plugin = PluginFile.Read(path))
        {
            Assert.Equal(1, plugin.FieldCount(key, percent));
            Assert.Equal(DestructibleHeaderStatus.Updated, plugin.RemoveDestructibleHeader(key));
            plugin.Write(path);
        }

        using (var plugin = PluginFile.Read(path))
        {
            Assert.Equal(0, plugin.FieldCount(key, percent));
            Assert.Equal(DestructibleStageStatus.HeaderMissing,
                plugin.InsertDestructibleStage(key, 0));
        }
    }

    [Fact]
    public void CreatesStagesAndReloadsOnTwoCarriers()
    {
        CrudAndReload("ARMO", "Two");
        CrudAndReload("ACTI", "Two");
    }

// Constraint: The fifteen DEST carriers the two-carrier test above does not reach. Before this, a carrier whose authoring rules or DEST slot differ could only regress silently. The whole-group read has to copy every stage member out of the plugin. If it returned the native pointers, an edit that reallocates the record would change an already-read snapshot, and a reload would disagree with what was read in memory.
    [Fact]
    public void WholeGroupReadCopiesEveryStageMemberAndSurvivesAnEditAndAReload()
    {
        using var temp = new TempDirectory();
        var path = Path.Combine(temp.Path, "GroupRead.esp");
        FormKey key;
        DestructibleGroup snapshot;

        using (var plugin = PluginFile.Create("GroupRead.esp"))
        {
            key = plugin.CreateRecord("ACTI", "GroupRead");
            Assert.Equal(DestructibleHeaderStatus.Updated, plugin.InsertDestructibleHeader(key));
            Assert.Equal(DestructibleStageStatus.Updated, plugin.InsertDestructibleStage(key, 0));
            Assert.Equal(DestructibleStageMemberStatus.Updated,
                plugin.InsertDestructibleStageMember(key, 0, "DSTA"));
            Assert.Equal(DestructibleStageStatus.Updated, plugin.InsertDestructibleStage(key, 1));
            Assert.Equal(DestructibleStageMemberStatus.Updated,
                plugin.InsertDestructibleStageMember(key, 1, "DMDL"));

            var read = plugin.ReadDestructibleGroup(key);
            Assert.True(read.Present);
            Assert.Equal(DestructibleStageStatus.Present, read.Status);
            snapshot = read.Group!;
            Assert.Equal(new DestructibleGroupHeader(0, 2, 0), snapshot.Header);
            Assert.Empty(snapshot.Resistances);
            Assert.Equal(new[] { "DSTD", "DSTA", "DSTF" }, Signatures(snapshot.Stages[0]));
            Assert.Equal(new[] { "DSTD", "DMDL", "DSTF" }, Signatures(snapshot.Stages[1]));
            Assert.Equal(new byte[20], snapshot.Stages[0].Members[0].Data);
            Assert.Equal(new byte[] { 0 }, snapshot.Stages[0].Members[1].Data);
            Assert.Empty(snapshot.Stages[0].Members[2].Data);

// Constraint: Removing a stage reallocates the record's subrecords, so a borrowed view would move here.
            Assert.Equal(DestructibleStageStatus.Updated, plugin.RemoveDestructibleStage(key, 0));
            Assert.Equal(new[] { "DSTD", "DSTA", "DSTF" }, Signatures(snapshot.Stages[0]));
            Assert.Equal(new byte[20], snapshot.Stages[0].Members[0].Data);

            var afterEdit = plugin.ReadDestructibleGroup(key);
            Assert.Equal((byte)1, afterEdit.Group!.Header.StageCount);
            Assert.Equal(new[] { "DSTD", "DMDL", "DSTF" }, Signatures(afterEdit.Group.Stages[0]));
            plugin.Write(path);
        }

        using (var plugin = PluginFile.Read(path))
        {
            var reloaded = plugin.ReadDestructibleGroup(key);
            Assert.True(reloaded.Present);
            Assert.Equal(new DestructibleGroupHeader(0, 1, 0), reloaded.Group!.Header);
            Assert.Single(reloaded.Group.Stages);
            Assert.Equal(new[] { "DSTD", "DMDL", "DSTF" }, Signatures(reloaded.Group.Stages[0]));
            Assert.Equal(new byte[20], reloaded.Group.Stages[0].Members[0].Data);
            Assert.Equal(new byte[] { 0 }, reloaded.Group.Stages[0].Members[1].Data);
        }
    }

    // A refusal must not hand back half a group, so the managed read reports a null group.
    [Fact]
    public void WholeGroupReadRefusesWithNoGroupWhenTheHeaderIsMissing()
    {
        using var plugin = PluginFile.Create("GroupReadMissing.esp");
        var key = plugin.CreateRecord("ACTI", "GroupReadMissing");

        var read = plugin.ReadDestructibleGroup(key);

        Assert.False(read.Present);
        Assert.Equal(DestructibleStageStatus.HeaderMissing, read.Status);
        Assert.Null(read.Group);
    }

    private static string[] Signatures(DestructibleGroupStage stage) =>
        stage.Members.Select(member => member.Signature).ToArray();

    [Theory]
    [InlineData("ALCH")]
    [InlineData("AMMO")]
    [InlineData("BOOK")]
    [InlineData("CONT")]
    [InlineData("DOOR")]
    [InlineData("FLOR")]
    [InlineData("FURN")]
    [InlineData("INGR")]
    [InlineData("KEYM")]
    [InlineData("LIGH")]
    [InlineData("MISC")]
    [InlineData("MSTT")]
    [InlineData("NPC_")]
    [InlineData("TACT")]
    [InlineData("WEAP")]
    public void CrudAndReloadOnRemainingCarriers(string signature) =>
        CrudAndReload(signature, "Remaining");
}
