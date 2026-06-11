using System.IO;
using FluentAssertions;
using FO4RecordEditor.Services;

namespace FO4RecordEditor.Tests;

public class ConflictToolsTests
{
    private const string Instance = @"D:\Games\ModlistDownloads";
    private static bool Available => Directory.Exists(Path.Combine(Instance, "profiles"));

    private static PluginToolExecutor MakeExecutor(out object env)
    {
        var info = Mo2ProfileLoader.ReadInstanceInfo(Instance);
        (env, _) = Mo2ProfileLoader.Load(Instance, info.Profile, info.GameDataFolder);
        var captured = env;
        return new PluginToolExecutor(() => captured, () => Instance);
    }

    [Fact]
    public void GetConflicts_ShowsTheOverrideChainAndWinner()
    {
        if (!Available) return;
        var exec = MakeExecutor(out _);

        // workshopScrapRecipe_Bush: overridden by A Forest.esp over vanilla -> A Forest wins.
        var result = exec.Execute("get_conflicts", """{"id":"054C84:Fallout4.esm"}""");
        result.Should().Contain("workshopScrapRecipe_Bush")
            .And.Contain("WINNER")
            .And.Contain("load order");
    }

    [Fact]
    public void GetWinningRecord_ReturnsTheEffectivePluginAndFields()
    {
        if (!Available) return;
        var exec = MakeExecutor(out _);

        var result = exec.Execute("get_winning_record", """{"id":"054C84:Fallout4.esm"}""");
        result.Should().Contain("Winning plugin:").And.Contain("workshopScrapRecipe_Bush");
    }

    [Fact]
    public void SearchRobcoConfigs_FindsRuntimePatchLines()
    {
        if (!Available) return;
        var exec = MakeExecutor(out _);

        // A RobCo Patcher key that exists in this modlist -> proves the INI search path works.
        var result = exec.Execute("search_robco_configs", """{"query":"filterByKeyword"}""");
        result.Should().Contain("RobCo Patcher matches").And.Contain(".ini");
    }
}
