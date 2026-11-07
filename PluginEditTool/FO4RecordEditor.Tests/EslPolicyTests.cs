using System.IO;
using System.Linq;
using FluentAssertions;
using FO4RecordEditor.Services;
using Mutagen.Bethesda.Fallout4;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Xunit;

namespace FO4RecordEditor.Tests;

public class EslPolicyTests
{
    private static string NewPlugin(float version, bool withMaster)
    {
        var plugin = $"EslPolicy_{Guid.NewGuid():N}.esp";
        WriteService.CreatePlugin(plugin);
        var mod = WriteService.GetMutable(plugin)!;
        mod.ModHeader.Stats.Version = version;
        if (withMaster) mod.MasterReferences.Add(new MasterReference { Master = "Fallout4.esm" });
        return plugin;
    }

    private static void AddWeapon(IFallout4Mod mod, uint id, string editorId) =>
        mod.Weapons.Add(new Weapon(new FormKey(mod.ModKey, id), Fallout4Release.Fallout4) { EditorID = editorId });

    private static string Args(string plugin) => $"{{\"plugin\":\"{plugin}\"}}";

    private static string LightArgs(string plugin) => $"{{\"plugin\":\"{plugin}\",\"light\":true}}";

    private static void WithReload(PluginToolExecutor exec, string plugin, Action<IFallout4ModGetter> check)
    {
        var outPath = Path.Combine(Path.GetTempPath(), plugin);
        try
        {
            exec.Execute("save_plugin", $"{{\"plugin\":\"{plugin}\",\"path\":\"{outPath.Replace("\\", "\\\\")}\"}}")
                .Should().Contain("Saved");
            using var reload = Fallout4Mod.CreateFromBinaryOverlay(ModPath.FromPath(outPath), Fallout4Release.Fallout4);
            check(reload);
        }
        finally
        {
            try { File.Delete(outPath); } catch { }
        }
    }

    private static int CountNotifications(string plugin, Action act)
    {
        var count = 0;
        Action<string> handler = name => { if (name == plugin) count++; };
        WriteService.PluginChanged += handler;
        try { act(); }
        finally { WriteService.PluginChanged -= handler; }
        return count;
    }

    [Fact]
    public void MasterlessPlugin_RemapsBelowSlotRecordsIntoLegacyRange()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x700, "Esl_Low");
        AddWeapon(mod, 0x5000, "Esl_High");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("check_esl_eligibility", Args(plugin))
            .Should().Contain("2 of 2 native record(s) sit outside 0x800-0xFFF");
        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 2").And.Contain("(0x800-0xFFF)");

        WithReload(exec, plugin, reload =>
        {
            reload.Weapons.First(w => w.EditorID == "Esl_Low").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
            reload.Weapons.First(w => w.EditorID == "Esl_High").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
        });
    }

    [Fact]
    public void PluginWithMasterAndVersionOne_KeepsLowRecordsAndUsesFullRange()
    {
        var plugin = NewPlugin(1f, withMaster: true);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x005, "Esl_Keep");
        AddWeapon(mod, 0x5000, "Esl_Move");
        mod.Weapons.First(w => w.EditorID == "Esl_Keep").Keywords = new()
        {
            new FormLink<IKeywordGetter>(new FormKey("Fallout4.esm", 0x001234))
        };
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("check_esl_eligibility", Args(plugin)).Should().Contain("sit outside 0x001-0xFFF");
        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 1").And.Contain("(0x001-0xFFF)");

        WithReload(exec, plugin, reload =>
        {
            reload.Weapons.First(w => w.EditorID == "Esl_Keep").FormKey.ID.Should().Be(0x005u);
            reload.Weapons.First(w => w.EditorID == "Esl_Move").FormKey.ID.Should().Be(0x001u);
        });
    }

    [Fact]
    public void PluginWithMasterButNoCrossModLinks_UsesHighRangeSoMutagenSaveAccepts()
    {
        var plugin = NewPlugin(1f, withMaster: true);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x005, "NoLink_Low");
        AddWeapon(mod, 0x5000, "NoLink_High");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("check_esl_eligibility", Args(plugin))
            .Should().Contain("2 of 2 native record(s) sit outside 0x800-0xFFF");
        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 2").And.Contain("(0x800-0xFFF)");

        WithReload(exec, plugin, reload =>
        {
            reload.Weapons.First(w => w.EditorID == "NoLink_Low").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
            reload.Weapons.First(w => w.EditorID == "NoLink_High").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
        });
    }

    [Fact]
    public void PluginWithMasterAndVersion095_UsesLegacyRangeAndMovesLowRecordsUp()
    {
        var plugin = NewPlugin(0.95f, withMaster: true);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x005, "Old_Low");
        AddWeapon(mod, 0x5000, "Old_High");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("check_esl_eligibility", Args(plugin))
            .Should().Contain("2 of 2 native record(s) sit outside 0x800-0xFFF");
        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 2").And.Contain("(0x800-0xFFF)");

        WithReload(exec, plugin, reload =>
        {
            reload.Weapons.First(w => w.EditorID == "Old_Low").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
            reload.Weapons.First(w => w.EditorID == "Old_High").FormKey.ID.Should().BeInRange(0x800u, 0xFFFu);
        });
    }

    [Fact]
    public void CompactToEsl_InMemoryNextObjectIdIsLastAssignedPlusOne_AndSaveWritesHighestNativePlusOne()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x900, "Next_Stays");
        AddWeapon(mod, 0x5000, "Next_Moves");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 1");
        mod.ModHeader.Stats.NextFormID.Should().Be(0x801u);

        WithReload(exec, plugin, reload =>
        {
            reload.ModHeader.Stats.NextFormID.Should().Be(0x901u);
            reload.ModHeader.Stats.NextFormID.Should().BeLessThanOrEqualTo(0x1000u);
        });
    }

    [Fact]
    public void CompactToEsl_InMemoryNextObjectIdSkipsRecordsAlreadyInRange()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x801, "Next_Taken");
        AddWeapon(mod, 0x5000, "Next_Moves");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 1");
        mod.Weapons.First(w => w.EditorID == "Next_Moves").FormKey.ID.Should().Be(0x800u);
        mod.ModHeader.Stats.NextFormID.Should().Be(0x802u);
        mod.EnumerateMajorRecords().Any(r => r.FormKey.ID == mod.ModHeader.Stats.NextFormID).Should().BeFalse();
    }

    [Fact]
    public void CompactToEsl_FullRangeAfterCompactionSetsNextToTheCeiling()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        for (uint id = 0x801; id <= 0xFFF; id++) AddWeapon(mod, id, $"Ceil_{id:X3}");
        AddWeapon(mod, 0x5000, "Ceil_High");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("remapped 1");
        mod.Weapons.First(w => w.EditorID == "Ceil_High").FormKey.ID.Should().Be(0x800u);
        mod.ModHeader.Stats.NextFormID.Should().Be(0x1000u);
        mod.EnumerateMajorRecords().Any(r => r.FormKey.ID == mod.ModHeader.Stats.NextFormID).Should().BeFalse();
    }

    [Fact]
    public void CompactToEsl_AlreadyCompatiblePluginLowersOutOfRangeNextObjectId()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x900, "Compat_A");
        mod.ModHeader.Stats.NextFormID = 0x5000;
        var exec = new PluginToolExecutor(() => null);

        var result = "";
        var notifications = CountNotifications(plugin, () => result = exec.Execute("compact_to_esl", Args(plugin)));

        result.Should().Contain("already has all native form IDs within 0x800-0xFFF").And.Contain("lowered HEDR next object ID to 0x901");
        mod.ModHeader.Stats.NextFormID.Should().Be(0x901u);
        notifications.Should().Be(1);
    }

    [Fact]
    public void CompactToEsl_AlreadyCompatiblePluginWithNoLowerNeededDoesNotNotify()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x900, "Noop_A");
        var exec = new PluginToolExecutor(() => null);

        var result = "";
        var notifications = CountNotifications(plugin, () => result = exec.Execute("compact_to_esl", Args(plugin)));

        result.Should().Contain("already ESL-compatible");
        notifications.Should().Be(0);
    }

    [Fact]
    public void CompactToEsl_RefusesPluginWhereAFormKeyIsSharedByTwoRecords()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x700, "Dup_Low");
        AddWeapon(mod, 0x5000, "Dup_Weapon");
        mod.Keywords.Add(new Keyword(new FormKey(mod.ModKey, 0x5000), Fallout4Release.Fallout4) { EditorID = "Dup_Keyword" });
        var exec = new PluginToolExecutor(() => null);
        var before = mod.EnumerateMajorRecords().Count();
        var nextBefore = mod.ModHeader.Stats.NextFormID;

        var result = "";
        var notifications = CountNotifications(plugin, () => result = exec.Execute("compact_to_esl", Args(plugin)));

        result.Should().Contain("Cannot compact").And.Contain("005000").And.Contain("shared by 2 records");
        mod.EnumerateMajorRecords().Count().Should().Be(before);
        mod.Weapons.First(w => w.EditorID == "Dup_Low").FormKey.ID.Should().Be(0x700u);
        mod.Weapons.First(w => w.EditorID == "Dup_Weapon").FormKey.ID.Should().Be(0x5000u);
        mod.ModHeader.Stats.NextFormID.Should().Be(nextBefore);
        notifications.Should().Be(0);
    }

    [Fact]
    public void CheckEsl_ReportsBlockedWhereAFormKeyIsSharedByTwoRecords()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x700, "Dup_Low");
        AddWeapon(mod, 0x5000, "Dup_Weapon");
        mod.Keywords.Add(new Keyword(new FormKey(mod.ModKey, 0x5000), Fallout4Release.Fallout4) { EditorID = "Dup_Keyword" });
        var exec = new PluginToolExecutor(() => null);

        var result = exec.Execute("check_esl_eligibility", Args(plugin));

        result.Should().Contain("would BLOCK on compact_to_esl").And.Contain("005000").And.Contain("shared by 2 records");
        result.Should().NotContain("ESL-ELIGIBLE");
    }

    [Fact]
    public void RenumberPluginFormIds_RefusesPluginWhereAFormKeyIsSharedByTwoRecords()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x700, "Dup_Low");
        AddWeapon(mod, 0x5000, "Dup_Weapon");
        mod.Keywords.Add(new Keyword(new FormKey(mod.ModKey, 0x5000), Fallout4Release.Fallout4) { EditorID = "Dup_Keyword" });
        var exec = new PluginToolExecutor(() => null);
        var before = mod.EnumerateMajorRecords().Count();
        var nextBefore = mod.ModHeader.Stats.NextFormID;

        exec.Execute("renumber_plugin_formids", $"{{\"plugin\":\"{plugin}\",\"start\":\"000800\",\"apply\":true}}")
            .Should().Contain("Cannot renumber").And.Contain("005000").And.Contain("shared by 2 records");
        mod.EnumerateMajorRecords().Count().Should().Be(before);
        mod.Weapons.First(w => w.EditorID == "Dup_Low").FormKey.ID.Should().Be(0x700u);
        mod.Weapons.First(w => w.EditorID == "Dup_Weapon").FormKey.ID.Should().Be(0x5000u);
        mod.ModHeader.Stats.NextFormID.Should().Be(nextBefore);
    }

    [Theory]
    [InlineData(0x005u)]
    [InlineData(0x5000u)]
    public void SetLight_RefusesMasterlessPluginWithIdsOutsideTheRange(uint id)
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, id, "Light_Out");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin))
            .Should().Contain("Cannot flag").And.Contain("0x800-0xFFF").And.Contain("compact_to_esl first");
        mod.ModHeader.Flags.HasFlag(Fallout4ModHeader.HeaderFlag.Small).Should().BeFalse();
    }

    [Fact]
    public void SetLight_LowersOutOfRangeNextFormIdToOnePastTheHighest()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x800, "Light_A");
        AddWeapon(mod, 0x801, "Light_B");
        mod.ModHeader.Stats.NextFormID = 0x5000;
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin)).Should().Contain("Lowered HEDR next object ID to 0x802");
        mod.ModHeader.Flags.HasFlag(Fallout4ModHeader.HeaderFlag.Small).Should().BeTrue();
        mod.ModHeader.Stats.NextFormID.Should().Be(0x802u);
    }

    [Fact]
    public void SetLight_KeepsNextFormIdThatIsAlreadyInRange()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x800, "Light_A");
        AddWeapon(mod, 0x801, "Light_B");
        mod.ModHeader.Stats.NextFormID = 0x900;
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin)).Should().Contain("Set the ESL").And.NotContain("Lowered");
        mod.ModHeader.Stats.NextFormID.Should().Be(0x900u);
    }

    [Fact]
    public void SetLight_EmptyPluginLowersNextFormIdToTheFloor()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        mod.ModHeader.Stats.NextFormID = 0x5000;
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin)).Should().Contain("Lowered HEDR next object ID to 0x800");
        mod.ModHeader.Stats.NextFormID.Should().Be(0x800u);
    }

    [Fact]
    public void SetLight_PluginWithMasterButNoCrossModLinksUsesTheHighFloor()
    {
        var plugin = NewPlugin(1f, withMaster: true);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x005, "NoLink_Light");
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin)).Should().Contain("Cannot flag").And.Contain("0x800-0xFFF");
        mod.ModHeader.Flags.HasFlag(Fallout4ModHeader.HeaderFlag.Small).Should().BeFalse();
    }

    [Fact]
    public void SetLight_PluginWithMasterAndCrossModLinkAcceptsLowRecords()
    {
        var plugin = NewPlugin(1f, withMaster: true);
        var mod = WriteService.GetMutable(plugin)!;
        AddWeapon(mod, 0x005, "Link_Light");
        mod.Weapons.First(w => w.EditorID == "Link_Light").Keywords = new()
        {
            new FormLink<IKeywordGetter>(new FormKey("Fallout4.esm", 0x001234))
        };
        var exec = new PluginToolExecutor(() => null);

        exec.Execute("set_light_flag", LightArgs(plugin)).Should().Contain("Set the ESL");
        mod.ModHeader.Flags.HasFlag(Fallout4ModHeader.HeaderFlag.Small).Should().BeTrue();
    }

    [Fact]
    public void CompactToEsl_RefusesMasterlessPluginWithMoreNativeRecordsThanSlots()
    {
        var plugin = NewPlugin(1f, withMaster: false);
        var mod = WriteService.GetMutable(plugin)!;
        for (uint id = 0x800; id <= 0xFFF; id++) AddWeapon(mod, id, $"Full_{id:X3}");
        AddWeapon(mod, 0x5000, "Full_High");
        var exec = new PluginToolExecutor(() => null);
        var before = mod.EnumerateMajorRecords().Count();

        exec.Execute("check_esl_eligibility", Args(plugin))
            .Should().Contain("CANNOT fit the ESL range: 2049 native record(s) exceed the 2048-slot capacity (0x800-0xFFF)");
        exec.Execute("compact_to_esl", Args(plugin)).Should().Contain("more than 2048 records");

        mod.EnumerateMajorRecords().Count().Should().Be(before);
        mod.Weapons.Any(w => w.FormKey.ID == 0x5000).Should().BeTrue();
        mod.Weapons.Count(w => w.FormKey.ID is >= 0x800 and <= 0xFFF).Should().Be(2048);
    }
}
