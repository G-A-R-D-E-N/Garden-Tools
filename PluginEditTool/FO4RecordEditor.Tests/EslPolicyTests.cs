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
